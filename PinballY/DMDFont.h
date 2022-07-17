// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// DMD Font
//
// This class defines a raster font format that we use for generating
// text displays on real and simulated DMDs.  Regular GDI fonts aren't
// suitable for generating DMD text because they're optimized for the
// much higher dot pitch of a video monitor; they look terrible at DMD
// resolution.  Instead, we need fonts that are rasterized specifically
// for a DMD's dot pitch.  That's what this class provides.
//
// Our font data is compiled into the program as static const byte 
// arrays.  These are in turn generated from DMD font layout data from
// other open-source pinball projects.  See the DMDFontTool subproject
// for details on how the font data sets are generated.

#pragma once

class DMDFont
{
public:
	DMDFont(const BYTE *pix, int pixWidth, int cellHeight,
		const BYTE *charWidths, const int *charOffsets);
	~DMDFont();

	// Measure a string
	SIZE MeasureString(const TCHAR *str) const;

	// Color table entry for DrawString32.  For efficiency, the color
	// bytes are laid out in the same order as in the DIB.
	struct Color
	{
		Color() { Set(0xff, 0x00, 0x00, 0x00); }
		Color(BYTE a, BYTE r, BYTE g, BYTE b) { Set(a, r, g, b); }
		Color(BYTE a, RGBQUAD q) { Set(a, q.rgbRed, q.rgbGreen, q.rgbBlue); }

		// set the bytes
		void Set(int r, int g, int b)
		{
			c[3] = 0xFF;
			c[2] = static_cast<BYTE>(r);
			c[1] = static_cast<BYTE>(g);
			c[0] = static_cast<BYTE>(b);
		}
		void Set(int a, int r, int g, int b)
		{
			c[3] = static_cast<BYTE>(a);
			c[2] = static_cast<BYTE>(r);
			c[1] = static_cast<BYTE>(g);
			c[0] = static_cast<BYTE>(b);
		}

		void R(BYTE r) { c[2] = r; }
		void G(BYTE g) { c[1] = g; }
		void B(BYTE b) { c[0] = b; }
		void A(BYTE a) { c[3] = a; }

		BYTE c[4];  // B, G, R, A
	};
	
	// Draw a string into a 128x32 pixel array, with 32 bits per pixel,
	// using the given color table.  The color table entries give the
	// RGB values for grayscale values 0..15, where 0 is fully off and
	// 15 is fully on.
	void DrawString32(const TCHAR *str, BYTE *pix, int x, int y, const Color *colors) const;

	// Draw a string into a 128x32 pixel array, in 4-bit grayscale.
	// Each pixel is represented by one byte.  We only store 4-bit
	// values, so every byte written will have a value 0..15.
	void DrawString4(const TCHAR *str, BYTE *pix, int x, int y) const;

	// cell height
	int cellHeight;

	// Pixel array.  This is a rectangular array of pixels, with a height
	// equal to the character cell height, and a row width of pixWidth.
	// all of the characters at the widths listed in charWidths[].  The
	// pixel array can be thought of as a one-dimensional character grid,
	// with the character shape for ASCII code point 32 in the leftmost
	// cell.  The cells are of varying widths; charOffsets gives the
	// pixel offset within the array of each character.
	const BYTE *pix;

	// width of the pixel array
	int pixWidth;

	// character width array, for ASCII code points 32..126
	const BYTE *charWidths;

	// character offsets, for ASCII code points 32..126
	const int *charOffsets;
};

// predefined fonts
namespace DMDFonts
{
	extern const DMDFont Font_CC_5px_AZ;
	extern const DMDFont Font_CC_7px_az;
	extern const DMDFont Font_CC_9px_az;
	extern const DMDFont Font_CC_12px_az;
	extern const DMDFont Font_CC_15px_az;
	extern const DMDFont Font_CC_20px_az;
}

