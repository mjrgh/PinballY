// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "DMDFont.h"

// include the generated font data to instantiate the fonts
#include "DMDFonts/Font_CC_5px_AZ.h"
#include "DMDFonts/Font_CC_7px_az.h"
#include "DMDFonts/Font_CC_9px_az.h"
#include "DMDFonts/Font_CC_12px_az.h"
#include "DMDFonts/Font_CC_15px_az.h"
#include "DMDFonts/Font_CC_20px_az.h"


// DMD Font class

DMDFont::DMDFont(const BYTE *pix, int pixWidth, int cellHeight, const BYTE *charWidths, const int *charOffsets) :
	pix(pix),
	pixWidth(pixWidth),
	cellHeight(cellHeight),
	charWidths(charWidths),
	charOffsets(charOffsets)
{
}

DMDFont::~DMDFont()
{

}

SIZE DMDFont::MeasureString(const TCHAR *str) const
{
	// start with the font height and zero width
	SIZE s = { 0, cellHeight };

	// add up the character widths
	for (const TCHAR *p = str; *p != 0; ++p)
	{
		// get the next character
		TCHAR c = *p;

		// if it's a lower-case character, and the font doesn't contain
		// this character, convert to upper-case
		if (c >= 'a' && c <= 'z' && charWidths[c] == 0)
			c = c - 'a' + 'A';

		// if it's in range, add its width
		if (c >= 32 && c <= 126)
			s.cx += charWidths[c - 32];
	}

	// return the tally
	return s;
}

// fixed DMD height and width
const int dmdHeight = 32;
const int dmdWidth = 128;

// draw in 32-bit RGBA, four bytes per pixel
void DMDFont::DrawString32(const TCHAR *str, BYTE *dmdPix, int x, int y, const Color *colors) const
{
	// get a pointer to the starting row positions
	BYTE *dstRow = dmdPix + (y*dmdWidth*4) + x*4;
	const BYTE *srcRow = pix;

	// work across each row
	for (int row = 0, ycur = y; row < cellHeight; ++row, dstRow += dmdWidth*4, srcRow += pixWidth, ++ycur)
	{
		// start at the current row position
		BYTE *dst = dstRow;
		int xcur = x;

		// visit each character
		for (const TCHAR *p = str; *p != 0; ++p)
		{
			// get the next character
			TCHAR c = *p;

			// if it's a lower-case character, and the font doesn't contain
			// this character, convert to upper-case
			if (c >= 'a' && c <= 'z' && charWidths[c] == 0)
				c = c - 'a' + 'A';

			// if it's in range, draw it
			if (c >= 32 && c <= 126)
			{
				// convert the character code point to a data index
				c -= 32;

				// get the first byte of this row of this character in the
				// font data
				const BYTE *src = srcRow + charOffsets[c];

				// copy the pixels
				for (int colRem = charWidths[c]; colRem != 0; --colRem, ++xcur, ++src, dst += 4)
				{
					// clip to 128x32
					if (xcur >= 0 && xcur < dmdWidth && ycur >= 0 && ycur < dmdHeight)
						memcpy(dst, &colors[*src & 0xf].c, 4);
				}
			}
		}
	}
}

// draw in 4-bit grayscale, one byte per pixel
void DMDFont::DrawString4(const TCHAR *str, BYTE *dmdPix, int x, int y) const
{
	// get a pointer to the starting row positions
	BYTE *dstRow = dmdPix + (y*dmdWidth) + x;
	const BYTE *srcRow = pix;

	// work across each row
	for (int row = 0, ycur = y; row < cellHeight; ++row, dstRow += dmdWidth, srcRow += pixWidth, ++ycur)
	{
		// start at the current row position
		BYTE *dst = dstRow;
		int xcur = x;

		// visit each character
		for (const TCHAR *p = str; *p != 0; ++p)
		{
			// get the next character
			TCHAR c = *p;

			// if it's a lower-case character, and the font doesn't contain
			// this character, convert to upper-case
			if (c >= 'a' && c <= 'z' && charWidths[c] == 0)
				c = c - 'a' + 'A';

			// if it's in range, draw it
			if (c >= 32 && c <= 126)
			{
				// convert the character code point to a data index
				c -= 32;

				// get the first byte of this row of this character in the
				// font data
				const BYTE *src = srcRow + charOffsets[c];

				// copy the pixels
				for (int colRem = charWidths[c]; colRem != 0; --colRem, ++xcur, ++dst, ++src)
				{
					// clip to 128x32
					if (xcur >= 0 && xcur < dmdWidth && ycur >= 0 && ycur < dmdHeight)
						*dst = *src & 0x0f;
				}
			}
		}
	}
}
