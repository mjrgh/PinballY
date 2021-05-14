// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// DMD view
//
// This class provides VIDEO DISPLAY SIMULATION of DMD and segmented
// alphanumeric displays.  This isn't for physical DMD devices like 
// PinDMD3's; for that, refer to RealDMD.h and .cpp.
//
// This view can display several types of data:
//
// - Regular static images and videos, for displaying user-supplied
//   media files, such as screen captures of the video DMD during
//   game play.  This is the default case.
//
// - Generated graphics simulating the appearance of a 128x32 plasma
//   or DMD screen, as in the 1990s pinball machines, to display
//   text messages.  We have a rendering routine that creates images
//   in this style from text strings.  We have a collection of fonts
//   of different sizes that approximate the dot matrix fonts used 
//   in the 1990s Williams games.  (Regular Windows fonts don't work
//   well in this context, because they're designed to be rasterized
//   at much higher resolutions.  For a 128x32 layout, you need fonts
//   that are specifically tailored to the coarse dot matrix.)  We
//   generate the DMD image at exactly 128x32 pixels, and then use
//   a custom shader to render the 128x32 image onto the video 
//   display, using the high resolution of the video display to
//   simulate the dot structure of the original displays by drawing
//   each dot as a round spot at approximately life-size.  
//
// - Generated graphics simulating the appearance of a 16-segment
//   alphanumeric display, as in many late 1980s pinball machines.
//   This is the same idea as the generated DMD "dots" images, just
//   using a different style that simulates 16-segment plasma/LED
//   displays.  In this case, there's no special shader involved;
//   we simply generate a full-scale rasterized image and display
//   it directly.  Our rendering algorithm for the simulated 
//   segmented display characters is based on the excellent design
//   Freezy developed for his dmd-extensions system.
//
// - Generated graphics in an ad hoc "typewriter" style.  We use
//   this to generate high score displays for older EM-era games,
//   where there's no equivalent of a DMD or alphanumeric display
//   to use for high score messages.  As with the alphanumeric
//   style, we simply generate a rasterized image and display it
//   directly.
//

#include "stdafx.h"
#include "../Utilities/Config.h"
#include "../Utilities/GraphicsUtil.h"
#include "DMDView.h"
#include "Resource.h"
#include "D3D.h"
#include "D3DWin.h"
#include "Camera.h"
#include "TextDraw.h"
#include "VersionInfo.h"
#include "VideoSprite.h"
#include "GameList.h"
#include "Application.h"
#include "DMDShader.h"
#include "MouseButtons.h"
#include "VPinMAMEIfc.h"
#include "DMDFont.h"

using namespace DirectX;

// still image display time, for the high-score slide show
const int StillImageDisplayTime = 7000;

// native DMD size
static const int dmdWidth = 128, dmdHeight = 32;

// DMD view config vars
namespace ConfigVars
{
	static const TCHAR *DMDWinVarPrefix = _T("DMDWindow");
	static const TCHAR *TTHighScoreFont = _T("TTHighScoreFont");
	static const TCHAR *TTHighScoreTextColor = _T("TTHighScoreTextColor");
	static const TCHAR *DotsFixedAspectRatio = _T("DMDWindow.Dots.FixedAspectRatio");
};

// construction
DMDView::DMDView() : SecondaryView(IDR_DMD_CONTEXT_MENU, ConfigVars::DMDWinVarPrefix),
	highScorePos(highScoreImages.end())
{
	// process the initial configuration settings
	OnConfigChange();

	// subscribe for future config updates
	ConfigManager::GetInstance()->Subscribe(this);
}

void DMDView::OnConfigChange()
{
	auto cfg = ConfigManager::GetInstance();
	ttHighScoreFont.ParseConfig(ConfigVars::TTHighScoreFont, _T("Courier New"));
	ttHighScoreTextColor = cfg->GetColor(ConfigVars::TTHighScoreTextColor, RGB(0x20, 0x20, 0x20));
	dotsFixedAspectRatio = cfg->GetBool(ConfigVars::DotsFixedAspectRatio, false);
}

// get the background media info
const MediaType *DMDView::GetBackgroundImageType() const { return &GameListItem::dmdImageType; }
const MediaType *DMDView::GetBackgroundVideoType() const { return &GameListItem::dmdVideoType; }

void DMDView::ClearMedia()
{
	// discard any high score images
	ClearHighScoreImages();

	// do the base class work
	__super::ClearMedia();
}

void DMDView::ClearHighScoreImages()
{
	// clear the list
	highScoreImages.clear();

	// reset the list position pointer
	highScorePos = highScoreImages.end();

	// update the drawing list in case we're currently showing a
	// high score screen
	UpdateDrawingList();

	// kill any pending slide-show timer 
	KillTimer(hWnd, StartHighScoreTimerID);
	KillTimer(hWnd, NextHighScoreTimerID);
}

void DMDView::OnUpdateHighScores(GameListItem *game)
{
	// if the update is for the game we're currently displaying,
	// re-generate the high score graphics
	if (game != nullptr && game == currentBackground.game)
		GenerateHighScoreImages();
}

void DMDView::OnChangeBackgroundImage()
{
	// re-generate high score images
	GenerateHighScoreImages();
}


// font list, in descending size order
static const struct
{
	// name of the font, for matching search requests from Javascript
	const TCHAR *name;

	// font object
	const DMDFont *font;
}
dmdFonts[] = {
	{ _T("dmd-20px"), &DMDFonts::Font_CC_20px_az },
	{ _T("dmd-15px"), &DMDFonts::Font_CC_15px_az },
	{ _T("dmd-12px"), &DMDFonts::Font_CC_12px_az },
	{ _T("dmd-9px"),  &DMDFonts::Font_CC_9px_az },
	{ _T("dmd-7px"),  &DMDFonts::Font_CC_7px_az },
	{ _T("dmd-5px"),  &DMDFonts::Font_CC_5px_AZ },
};

// Pick the largest DMD font that will make the given text
// list fit the display screen.
const DMDFont *DMDView::PickHighScoreFont(const std::list<const TSTRING*> &group)
{
	// start with the largest font that will fit the vertical space
	int nLines = (int)group.size();
	int fontIndex = 0;
	auto font = &dmdFonts[fontIndex];
	while (fontIndex + 1 < countof(dmdFonts))
	{
		// if it fits vertically, we can stop here
		if (nLines * font->font->cellHeight <= dmdHeight)
			break;

		// go to the next font
		font = &dmdFonts[++fontIndex];
	}

	// now downsize the font if necessary to fit the longest line horizontally
	while (fontIndex + 1 < countof(dmdFonts))
	{
		// find the widest line
		int maxWid = 0;
		for (auto it = group.begin(); it != group.end(); ++it)
		{
			SIZE sz = font->font->MeasureString((*it)->c_str());
			if (sz.cx > maxWid)
				maxWid = sz.cx;
		}

		// if it fits, we can stop
		if (maxWid <= dmdWidth)
			break;

		// get the next font
		font = &dmdFonts[++fontIndex];
	}

	// return the selected font
	return font->font;
}

// Alphanumeric display segment encodings, borrowed from the similar 
// rendering code in Freezy's dmd-extension.  This array is indexed 
// by ASCII character code (from 32 to 127 only).  The hex number 
// contained at each character position gives a bitwise set of the 
// segments that "light up" on the display to draw that character.  
// For example, alphaSegmentMask['A'-32] is the segment encoding 
// for capital 'A', which is 0x0877.
//
// The segment bit layout is:
//
//    Segment#   Bit Mask   Description
//    --------   --------   -----------
//       0        0x0001    Top
//       1        0x0002    Right upper
//       2        0x0004    Right lower
//       3        0x0008    Bottom
//       4        0x0010    Left lower
//       5        0x0020    Left upper
//       6        0x0040    Middle left
//       7        0x0080    Comma
//       8        0x0100    Diagonal left upper
//       9        0x0200    Center upper
//       10       0x0400    Diagonal right upper
//       11       0x0800    Middle right
//       12       0x1000    Diagonal right lower
//       13       0x2000    Center lower
//       14       0x4000    Diagonal left lower
//       15       0x8000    Dot
//
//                    top
//                  -------
//      left upper  |\   /|  right upper
//                  | \ / |
// middle right ->  |-- --|  <- middle left
//                  | / \ |
//      left lower  |/   \|  right lower
//                  -------
//                  bottom
// 
static const UINT alphaSegmentMask[] = {
	0x0000,     // 0x20 ' '
	0x0086,     // 0x21 '!'
	0x0202,     // 0x22 '"'
	0x2a4e,     // 0x23 '#'
	0x2a6d,     // 0x24 '$'
	0x7f64,     // 0x25 '%'
	0x1359,     // 0x26 '&'
	0x0200,     // 0x27 '\''
	0x1400,     // 0x28 '('
	0x4100,     // 0x29 ')'
	0x7f40,     // 0x2a '*'
	0x2a40,     // 0x2b '+'
	0x8080,     // 0x2c ','
	0x0840,     // 0x2d '-'
	0x8000,     // 0x2e '.'
	0x4400,     // 0x2f '/'
	0x003f,     // 0x30 '0'
	0x0006,     // 0x31 '1'
	0x085b,     // 0x32 '2'
	0x080f,     // 0x33 '3'
	0x0866,     // 0x34 '4'
	0x086d,     // 0x35 '5'
	0x087d,     // 0x36 '6'
	0x0007,     // 0x37 '7'
	0x087f,     // 0x38 '8'
	0x086f,     // 0x39 '9'
	0x2200,     // 0x3a ':'
	0x4200,     // 0x3b ';'
	0x1440,     // 0x3c '<'
	0x0848,     // 0x3d '='
	0x4900,     // 0x3e '>'
	0x2883,     // 0x3f '?'
	0x0a3b,     // 0x40 '@'
	0x0877,     // 0x41 'A'
	0x2a0f,     // 0x42 'B'
	0x0039,     // 0x43 'C'
	0x220f,     // 0x44 'D'
	0x0079,     // 0x45 'E'
	0x0071,     // 0x46 'F'
	0x083d,     // 0x47 'G'
	0x0876,     // 0x48 'H'
	0x2209,     // 0x49 'I'
	0x001e,     // 0x4a 'J'
	0x1470,     // 0x4b 'K'
	0x0038,     // 0x4c 'L'
	0x0536,     // 0x4d 'M'
	0x1136,     // 0x4e 'N'
	0x003f,     // 0x4f 'O'
	0x0873,     // 0x50 'P'
	0x103f,     // 0x51 'Q'
	0x1873,     // 0x52 'R'
	0x086d,     // 0x53 'S'
	0x2201,     // 0x54 'T'
	0x003e,     // 0x55 'U'
	0x4430,     // 0x56 'V'
	0x5036,     // 0x57 'W'
	0x5500,     // 0x58 'X'
	0x086e,     // 0x59 'Y'
	0x4409,     // 0x5a 'Z'
	0x0039,     // 0x5b '['
	0x1100,     // 0x5c '\'
	0x000f,     // 0x5d ']'
	0x5000,     // 0x5e '^'
	0x0008,     // 0x5f '_'
	0x0100,     // 0x60 '`'
	0x2058,     // 0x61 'a'
	0x1078,     // 0x62 'b'
	0x0858,     // 0x63 'c'
	0x480e,     // 0x64 'd'
	0x4058,     // 0x65 'e'
	0x2c40,     // 0x66 'f'
	0x0c0e,     // 0x67 'g'
	0x2070,     // 0x68 'h'
	0x2000,     // 0x69 'i'
	0x4210,     // 0x6a 'j'
	0x3600,     // 0x6b 'k'
	0x0030,     // 0x6c 'l'
	0x2854,     // 0x6d 'm'
	0x2050,     // 0x6e 'n'
	0x085c,     // 0x6f 'o'
	0x0170,     // 0x70 'p'
	0x0c06,     // 0x71 'q'
	0x0050,     // 0x72 'r'
	0x1808,     // 0x73 's'
	0x0078,     // 0x74 't'
	0x001c,     // 0x75 'u'
	0x4010,     // 0x76 'v'
	0x5014,     // 0x77 'w'
	0x5500,     // 0x78 'x'
	0x0a0e,     // 0x79 'y'
	0x4048,     // 0x7a 'z'
	0x4149,     // 0x7b '{'
	0x2200,     // 0x7c '|'
	0x1c09,     // 0x7d '}'
	0x4c40,     // 0x7e '~'
};

// Mask to use for missing characters.  This is an analog of the
// GUI convention of displaying an "empty box" glyph to represent
// a character that isn't defined in the selected font when drawing
// text.  The obvious equivalent in the alphanumeric format would
// look the same as a zero or capital "O", so that's not good.
// The empty box shape isn't important in itself, though; GUIs use
// it because it stands out visually as an obvious placeholder
// that's not part of the font, making it apparent that something's
// missing.  I think the best equivalent for the alphanumeric 
// format is to light up all of the segments (except for dot and 
// comma).  Like the empty box glyph in a GUI, it looks "wrong",
// which makes it a good placeholder and error indicator.
static const UINT alphaSegmentMissingCharMask = 0x7F7F;

// Alphanumeric display "fonts".  These aren't really fonts in
// the usual sense, but rather a collection of outlines for the
// individual segments that make up a character cell in the
// alphanumeric display.  To draw a character:
//
// 1. Look up the segment mask for the character in alphaSegmentsMask[]
// 
// 2. For segment in 0..15, draw this segment if the corresponding
//    bit (1 << segment) is set to '1' in the segment mask from
//    step 1
//
// The segment polygons below were mechanically translated from
// the SVG layouts for alphanumeric display segments in Freezy's 
// dmd-extensions.  (The original SVG files amount to lists of
// vertex coordinates that define the polygons, so these are just
// C++ representations of the same vertex lists.)
//
struct AlphanumFont
{
	// bounding box size
	float boxWidth, boxHeight;

	// Segment polygon list.  See the character segment table above
	// for the segment layout.  Each segment is described by a GDI+
	// polygon (given as a list of points) with the bounds of the 
	// segment for drawing.
	struct Segment
	{
		int nPoints;
		const Gdiplus::PointF *points;
	};
	Segment segment[16];
};
static const Gdiplus::PointF alphanumFontThinSeg00[] = { { 158.46f,19.87f }, { 28.78f,19.87f }, { 25.77f,21.88f }, { 35.26f,31.37f }, { 151.98f,31.37f }, { 161.47f,21.88f }, };
static const Gdiplus::PointF alphanumFontThinSeg01[] = { { 167.37f,27.78f }, { 167.37f,154.37f }, { 162.47f,154.37f }, { 155.87f,147.77f }, { 155.87f,35.13f }, { 165.3f,25.71f }, };
static const Gdiplus::PointF alphanumFontThinSeg02[] = { { 167.37f,285.96f }, { 167.37f,159.37f }, { 162.47f,159.37f }, { 155.87f,165.97f }, { 155.87f,278.61f }, { 165.3f,288.03f }, };
static const Gdiplus::PointF alphanumFontThinSeg03[] = { { 158.46f,293.87f }, { 28.78f,293.87f }, { 25.77f,291.86f }, { 35.26f,282.37f }, { 151.98f,282.37f }, { 161.47f,291.86f }, };
static const Gdiplus::PointF alphanumFontThinSeg04[] = { { 19.87f,285.96f }, { 19.87f,159.37f }, { 24.77f,159.37f }, { 31.37f,165.97f }, { 31.37f,278.61f }, { 21.94f,288.03f }, };
static const Gdiplus::PointF alphanumFontThinSeg05[] = { { 19.87f,27.78f }, { 19.87f,154.37f }, { 24.77f,154.37f }, { 31.37f,147.77f }, { 31.37f,35.13f }, { 21.94f,25.71f }, };
static const Gdiplus::PointF alphanumFontThinSeg06[] = { { 84.04f,151.12f }, { 35.18f,151.12f }, { 29.43f,156.87f }, { 35.18f,162.62f }, { 84.04f,162.62f }, { 89.79f,156.87f }, };
static const Gdiplus::PointF alphanumFontThinSeg07[] = { { 172.37f,298.87f }, { 172.37f,303.86f }, { 173.12f,329.61f }, { 183.87f,303.86f }, { 183.87f,298.87f }, };
static const Gdiplus::PointF alphanumFontThinSeg08[] = { { 36.37f,36.37f }, { 36.37f,55.27f }, { 80.03f,146.12f }, { 82.87f,146.12f }, { 82.87f,127.22f }, { 39.21f,36.37f }, };
static const Gdiplus::PointF alphanumFontThinSeg09[] = { { 87.87f,36.37f }, { 87.87f,147.29f }, { 93.62f,153.04f }, { 99.37f,147.29f }, { 99.37f,36.37f }, };
static const Gdiplus::PointF alphanumFontThinSeg10[] = { { 150.87f,36.37f }, { 150.87f,55.27f }, { 107.21f,146.12f }, { 104.37f,146.12f }, { 104.37f,127.22f }, { 148.03f,36.37f }, };
static const Gdiplus::PointF alphanumFontThinSeg11[] = { { 103.2f,151.12f }, { 152.06f,151.12f }, { 157.81f,156.87f }, { 152.06f,162.62f }, { 103.2f,162.62f }, { 97.45f,156.87f }, };
static const Gdiplus::PointF alphanumFontThinSeg12[] = { { 150.87f,277.37f }, { 150.87f,258.47f }, { 107.21f,167.62f }, { 104.37f,167.62f }, { 104.37f,186.52f }, { 148.03f,277.37f }, };
static const Gdiplus::PointF alphanumFontThinSeg13[] = { { 87.87f,277.37f }, { 87.87f,166.45f }, { 93.62f,160.7f }, { 99.37f,166.45f }, { 99.37f,277.37f }, };
static const Gdiplus::PointF alphanumFontThinSeg14[] = { { 36.37f,277.37f }, { 36.37f,258.47f }, { 80.03f,167.62f }, { 82.87f,167.62f }, { 82.87f,186.52f }, { 39.21f,277.37f }, };
static const Gdiplus::PointF alphanumFontThinSeg15[] = { { 172.37f,282.37f }, { 183.87f,282.37f }, { 183.87f,293.87f }, { 172.37f,293.87f }, };
static const AlphanumFont alphanumFontThin = {
	203.74f, 332.59f,
	{
		6, alphanumFontThinSeg00,
		6, alphanumFontThinSeg01,
		6, alphanumFontThinSeg02,
		6, alphanumFontThinSeg03,
		6, alphanumFontThinSeg04,
		6, alphanumFontThinSeg05,
		6, alphanumFontThinSeg06,
		5, alphanumFontThinSeg07,
		6, alphanumFontThinSeg08,
		5, alphanumFontThinSeg09,
		6, alphanumFontThinSeg10,
		6, alphanumFontThinSeg11,
		6, alphanumFontThinSeg12,
		5, alphanumFontThinSeg13,
		6, alphanumFontThinSeg14,
		4, alphanumFontThinSeg15,
	 }
};

static const Gdiplus::PointF alphanumFontBoldSeg00[] = { { 155.42f,14.8f }, { 31.74f,14.8f }, { 24.73f,21.81f }, { 39.22f,36.3f }, { 147.94f,36.3f }, { 162.43f,21.81f } };
static const Gdiplus::PointF alphanumFontBoldSeg01[] = { { 172.33f,31.71f }, { 172.33f,154.8f }, { 162.43f,154.8f }, { 150.83f,143.2f }, { 150.83f,39.06f }, { 165.26f,24.64f } };
static const Gdiplus::PointF alphanumFontBoldSeg02[] = { { 172.33f,281.64f }, { 172.33f,158.55f }, { 162.43f,158.55f }, { 150.83f,170.15f }, { 150.83f,274.29f }, { 165.26f,288.71f } };
static const Gdiplus::PointF alphanumFontBoldSeg03[] = { { 155.42f,298.55f }, { 31.74f,298.55f }, { 24.73f,291.54f }, { 39.22f,277.05f }, { 147.94f,277.05f }, { 162.43f,291.54f } };
static const Gdiplus::PointF alphanumFontBoldSeg04[] = { { 14.83f,281.64f }, { 14.83f,158.55f }, { 24.73f,158.55f }, { 36.33f,170.15f }, { 36.33f,274.29f }, { 21.9f,288.71f } };
static const Gdiplus::PointF alphanumFontBoldSeg05[] = { { 14.83f,31.71f }, { 14.83f,154.8f }, { 24.73f,154.8f }, { 36.33f,143.2f }, { 36.33f,39.06f }, { 21.9f,24.64f } };
static const Gdiplus::PointF alphanumFontBoldSeg06[] = { { 80,146.05f }, { 39.14f,146.05f }, { 28.39f,156.8f }, { 39.14f,167.55f }, { 80,167.55f }, { 90.75f,156.8f } };
static const Gdiplus::PointF alphanumFontBoldSeg07[] = { { 176.83f,302.55f }, { 176.83f,311.29f }, { 177.26f,330.71f }, { 198.33f,318.64f }, { 198.33f,302.55f } };
static const Gdiplus::PointF alphanumFontBoldSeg08[] = { { 40.33f,40.3f }, { 40.33f,75.11f }, { 74.53f,142.05f }, { 78.83f,142.05f }, { 78.83f,108.15f }, { 44.17f,40.3f } };
static const Gdiplus::PointF alphanumFontBoldSeg09[] = { { 82.83f,40.03f }, { 82.83f,143.22f }, { 93.58f,153.97f }, { 104.33f,143.22f }, { 104.33f,40.03f } };
static const Gdiplus::PointF alphanumFontBoldSeg10[] = { { 146.83f,40.3f }, { 146.83f,75.11f }, { 112.63f,142.05f }, { 108.33f,142.05f }, { 108.33f,108.15f }, { 142.99f,40.3f } };
static const Gdiplus::PointF alphanumFontBoldSeg11[] = { { 107.16f,146.05f }, { 148.02f,146.05f }, { 158.77f,156.8f }, { 148.02f,167.55f }, { 107.16f,167.55f }, { 96.41f,156.8f } };
static const Gdiplus::PointF alphanumFontBoldSeg12[] = { { 146.83f,273.05f }, { 146.83f,238.24f }, { 112.63f,171.3f }, { 108.33f,171.3f }, { 108.33f,205.2f }, { 142.99f,273.05f } };
static const Gdiplus::PointF alphanumFontBoldSeg13[] = { { 82.83f,273.32f }, { 82.83f,170.13f }, { 93.58f,159.38f }, { 104.33f,170.13f }, { 104.33f,273.32f } };
static const Gdiplus::PointF alphanumFontBoldSeg14[] = { { 40.33f,273.05f }, { 40.33f,238.24f }, { 74.53f,171.3f }, { 78.83f,171.3f }, { 78.83f,205.2f }, { 44.17f,273.05f } };
static const Gdiplus::PointF alphanumFontBoldSeg15[] = { { 176.83f,277.05f }, { 198.33f,277.05f }, { 198.33f,298.55f }, { 176.83f,298.55f } };
static const AlphanumFont alphanumFontBold = {
	203.74f, 332.59f,
	{
		6, alphanumFontBoldSeg00,
		6, alphanumFontBoldSeg01,
		6, alphanumFontBoldSeg02,
		6, alphanumFontBoldSeg03,
		6, alphanumFontBoldSeg04,
		6, alphanumFontBoldSeg05,
		6, alphanumFontBoldSeg06,
		5, alphanumFontBoldSeg07,
		6, alphanumFontBoldSeg08,
		5, alphanumFontBoldSeg09,
		6, alphanumFontBoldSeg10,
		6, alphanumFontBoldSeg11,
		6, alphanumFontBoldSeg12,
		5, alphanumFontBoldSeg13,
		6, alphanumFontBoldSeg14,
		4, alphanumFontBoldSeg15,
	 }
};

// alphanumeric display fonts by name
static const struct
{
	// name of the font, for matching search requests from Javascript
	const TCHAR *name;

	// font object
	const AlphanumFont *font;
}
alphaFonts[] = {
	{ _T("alphanum-thin"), &alphanumFontThin },
	{ _T("alphanum-bold"), &alphanumFontBold },
};

// Initialize alphanumeric layer options based on a foreground color
void DMDView::AlphanumOptions::InitFromSegColor(BYTE r, BYTE g, BYTE b)
{
	// Convert from RGB space to HSL space
	BYTE h, s, l;
	RGBtoHSL(r, g, b, h, s, l);

	// Set the foreground layer to a brightened version of the
	// base color.  This layer forms the core of the segments,
	// so it has no dilation or blur.  Since this is meant to
	// look like the part of the segments that's directly 
	// emitting light, it should be almost white.  When you're
	// looking at a real display of this type, the core of
	// the segments is typically so bright that it washes out
	// its color and looks white to the eye; the color comes
	// more from the halo around the direct emitting area.
	BYTE rr, gg, bb;
	HSLtoRGB(h, s, l > 225 ? l : 225, rr, gg, bb);
	lit = { Gdiplus::Color(0xff, rr, gg, bb), 0, 0, 0 };

	// set the top glow layer to the base color, partially 
	// transparent, with a little dilation and blur
	glow1 = { Gdiplus::Color(0xa0, r, g, b), 10, 7, 15 };

	// set the bottom glow layer to a desaturated version of the
	// top glow color
	s = static_cast<BYTE>(s * 65 / 100);
	HSLtoRGB(h, s, l, rr, gg, bb);
	glow2 = { Gdiplus::Color(0x40, rr, gg, bb), 40, 20, 30 };

	// Set the bottom glow layer to low-alpha white, which
	// will make it look gray.
	unlit = { Gdiplus::Color(0x20, 0xff, 0xff, 0xff), 0, 0, 5 };
}

// Creating the high-score images can be rather time-consuming, 
// especially for the alphanumeric style, which require non-trivial
// rendering.  We want to avoid as much as possible doing any long
// compute operations on the UI thread, since video frame updates 
// pause whenever the main thread is busy.  This rendering task can
// take significantly longer than a video refresh cycle, so we have 
// to do it on a background thread.
struct HighScoreGraphicsGenThread
{
	typedef DMDView::HighScoreImage HighScoreImage;

	HighScoreGraphicsGenThread(BaseView *view, DWORD seqno, const DMDView::DMDPalette &palette, const TCHAR *style) :
		view(view, RefCounted::DoAddRef),
		seqno(seqno),
		palette(palette),
		style(style)
	{
		// get the DMD view, if available
		if (auto dmdview = Application::Get()->GetDMDView(); dmdview != nullptr)
		{
			// count the thread
			InterlockedIncrement(&dmdview->nHighScoreThreads);

			// remember the default font for the typewriter-style display
			ttHighScoreFont = dmdview->ttHighScoreFont;
			ttHighScoreTextColor = dmdview->ttHighScoreTextColor;
		}

		// Initialize the alphanumeric options with the full ON color.  The
		// alphanumeric format doesn't use the standard dots, so it doesn't
		// use the full 16-shade monochrome palette that the dot generator
		// needs, but we do want the alphanumeric segment color at 100%
		// brightness match the 100% dot color.
		alphanumOptions.InitFromSegColor(palette.color[15]);

		// if we're in TT mode, look for a custom background image
		TCHAR ttImg[MAX_PATH];
		if (_tcsicmp(this->style.c_str(), _T("tt")) == 0
			&& GameList::Get()->FindGlobalImageFile(ttImg, _T("Images"), _T("TT High Score Background")))
			ttBkgImageFile = ttImg;
	}

	virtual ~HighScoreGraphicsGenThread()
	{
		// count the thread exist in the view object
		if (auto dmdview = Application::Get()->GetDMDView(); dmdview != nullptr)
			InterlockedDecrement(&dmdview->nHighScoreThreads);
	}

	// The view window that requested the sprite generation.  PinballY itself
	// only ever uses this code from the DMD view, but the DMD images are just
	// specialized sprites, so there's no technical reason to limit them to the
	// DMD window.  And since we expose the DMD sprites through Javascript,
	// they can in fact be drawn in any window that Javascript can access 
	// (which is all of our main windows).
	RefPtr<BaseView> view;

	// high score request sequence number
	DWORD seqno;

	// VPinMAME 16-shade monochrome palette for generated dots; the 100%
	// brightness entry is also used as the color for the simulated segments
	// in the 16-segment alphanumeric display mode.
	DMDView::DMDPalette palette;

	// background color
	RGBQUAD bgColor = { 0, 0, 0 };

	// background opacity
	BYTE bgAlpha = 0xFF;

	// display style for the game
	TSTRING style;

	// Pre-selected font.  If this is null, we'll select a font 
	// automatically based on the style and text.  For DMD output,
	// this is the name of one of the internal DMD fonts ("20px", 
	// etc).
	TSTRING fontName;

	// Alphanumeric display options
	DMDView::AlphanumOptions alphanumOptions;

	// For the TT (typewriter) display style, the background image file name
	TSTRING ttBkgImageFile;

	// Default font for the TT display style
	FontPref ttHighScoreFont{ 24 };
	COLORREF ttHighScoreTextColor;

	// Messages to display.  Each slide is a set of strings to
	// display together on one screen; the overall list is the set 
	// of slides to display in time sequence.
	struct Slide
	{
		// display time in milliseconds for this slide
		DWORD displayTime = 0;

		// messages in the slide
		std::list<TSTRING> messages;
	};
	std::list<Slide> slides;

	// Generated images
	std::list<HighScoreImage> images;

	// launch the thread
	void Launch()
	{

		// create the thread
		DWORD tid;
		HandleHolder hThread = CreateThread(NULL, 0, &HighScoreGraphicsGenThread::SMain, this, 0, &tid);
		if (hThread == nullptr)
		{
			// we couldn't launch the thread, so do the work inline instead
			Main();
		}
	}

	// thread entrypoint
	static DWORD WINAPI SMain(LPVOID lParam) { return static_cast<HighScoreGraphicsGenThread*>(lParam)->Main(); }
	DWORD Main()
	{
		// Once the thread starts, we're the sole reference to 'this',
		// so make sure we delete the object on exiting the thread routine.
		std::unique_ptr<HighScoreGraphicsGenThread> thisptr(this);

		// create the graphics according to the style
		if (_tcsicmp(style.c_str(), _T("alpha")) == 0)
		{
			// Alphanumeric segmented display style
			RenderAlphanum();
		}
		else if (_tcsicmp(style.c_str(), _T("tt")) == 0)
		{
			// typewriter style
			RenderTT();
		}
		else
		{
			// "Dots" style - this is also the default if the style setting
			// isn't recognized
			RenderDots();
		}

		// Send the sprite list back to the window
		view->SendMessage(BVMsgDMDImageReady, seqno, reinterpret_cast<LPARAM>(&images));

		// done (thread return code isn't used)
		return 0;
	}

	// Get the font setting, as a list of strings
	std::list<TSTRING> SplitFonts()
	{
		// if the string is empty, return an empty list
		if (fontName.length() == 0)
			return std::list<TSTRING>();

		// split the string on comma delimiters
		std::list<TSTRING> fonts = StrSplit<TSTRING>(fontName.c_str(), ',');

		// trim extra spaces from each string
		for (auto it = fonts.begin(); it != fonts.end(); ++it)
			it->assign(TrimString<TSTRING>(it->c_str()));

		// return the list
		return fonts;
	}

	// Count the character cells in an alphanumeric string.  This is
	// slightly more complicated than just counting the characters,
	// because of the special handling of '.' and ',': these combine
	// with the previous character, since the dot/comma element in
	// each cell can be "illuminated" in addition to any other glyph.
	static int CountAlphaCells(const TCHAR *str)
	{
		int nCells = 0;
		for (TCHAR prvChar = 0; *str != 0; ++str)
		{
			// get this character
			TCHAR c = *str;

			// Check for combining characters.  '.' and ',' combine with
			// the previous character to form a single cell, provided that 
			// the previous character isn't also '.' or ',', and that this
			// isn't the first cell.
			if (!((c == '.' || c == ',') && !(nCells == 0 || prvChar == '.' || prvChar == ',')))
				++nCells;

			// this now becomes the previous character for the next iteration
			prvChar = c;
		}

		// return the cell count
		return nCells;
	}

	// draw into the image, creating a new DIB of the given size for it
	void DrawToImage(Slide &group, int width, int height,
		std::function<void(Gdiplus::Graphics&, BYTE *dibits)> drawFunc)
	{
		// emplace a new high-score image in the list
		auto &image = images.emplace_back(HighScoreImage::NormalSpriteType, group.displayTime);

		// draw the image into a new DIB through the callback
		DrawOffScreen<BYTE>(&image.hbmp, width, height, [&image, &drawFunc]
		(HDC hdc, HBITMAP, BYTE *dibits, const BITMAPINFO &bmi)
		{
			// save the bitmap data to the image object
			image.dibits = dibits;
			memcpy(&image.bmi, &bmi, sizeof(bmi));

			// set up the GDI+ context
			Gdiplus::Graphics g(hdc, dibits);

			// do the caller's drawing
			drawFunc(g, dibits);

			// flush the bitmap
			g.Flush();
		});
	}

	// Render using the 128x32 dots style
	void RenderDots()
	{
		// Set up a DIB descriptor for the 32bpp bitmap.  We'll use this
		// to create the D3D texture for the DMD sprite.
		BITMAPINFO bmi;
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = dmdWidth;
		bmi.bmiHeader.biHeight = -dmdHeight;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = 0;
		bmi.bmiHeader.biXPelsPerMeter = 0;
		bmi.bmiHeader.biYPelsPerMeter = 0;
		bmi.bmiHeader.biClrUsed = 0;
		bmi.bmiHeader.biClrImportant = 0;

		// Convert the DMD palette from RGBQUAD format to DIB palette format
		int alphaSpan = 0xFF - bgAlpha;
		DMDFont::Color colors[16];
		for (int i = 0; i < 16; ++i)
		{
			RGBQUAD rgb = palette.color[i];
			colors[i].Set(
				static_cast<BYTE>(roundf(bgAlpha + alphaSpan * (i + 2.0f) / 17.0f)),
				rgb.rgbRed, rgb.rgbGreen, rgb.rgbBlue);
		}

#if 0
		int alphaSpan = 0xFF - bgAlpha;
		int redSpan = txtColor.rgbRed - bgColor.rgbRed;
		int greenSpan = txtColor.rgbGreen - bgColor.rgbGreen;
		int blueSpan = txtColor.rgbBlue - bgColor.rgbBlue;
		DMDFont::Color colors[16];
		for (int i = 0; i < 16; ++i)
		{
			float step = (i + 2.0f) / 17.0f;
			colors[i].Set(
				static_cast<BYTE>(roundf(bgAlpha + alphaSpan * step)),
				static_cast<BYTE>(roundf(bgColor.rgbRed + redSpan * step)),
				static_cast<BYTE>(roundf(bgColor.rgbGreen + greenSpan * step)),
				static_cast<BYTE>(roundf(bgColor.rgbBlue + blueSpan * step)));
		}
#endif

		// process each group
		for (auto &group : slides)
		{
			// create the DIB buffer at 4 bytes per pixel
			BYTE *pix = new BYTE[dmdWidth*dmdHeight * 4];

			// Clear the buffer to the "off" pixel color (colors[0]).  Note
			// that we DON'T set this to the background color!  The background
			// color doesn't appear anywhere in the dot image itself (the
			// 128x32-pixel image we're building here), because each pixel in
			// the dot image represents exactly one DMD dot.  The background
			// color is for the voids *between* the dots.  Those voids aren't
			// represented anywhere in this 128x32 image.  Rather, the voids
			// are drawn when we render the DMD image onto the final video
			// surface through the DMD shader.
			BYTE *dst = pix;
			for (int i = 0; i < dmdWidth*dmdHeight; ++i, dst += 4)
				memcpy(dst, &colors[0], 4);

			// if a font was specified by name, search for a match
			const DMDFont *font = nullptr;
			for (auto &f : SplitFonts())
			{
				// if it starts with "dmd-", search for a match
				if (!_tcsnicmp(f.c_str(), _T("dmd-"), 4) == 0)
				{
					for (size_t i = 0; i < countof(dmdFonts); ++i)
					{
						if (_tcsicmp(dmdFonts[i].name, f.c_str()) == 0)
						{
							font = dmdFonts[i].font;
							break;
						}
					}
				}

				// stop when we find a matching DMD font
				if (font != nullptr)
					break;
			}

			// if we didn't find the font or not font was specified, pick
			// one automatically based on the text size
			if (font == nullptr)
				font = DMDView::PickHighScoreFont(group.messages);

			// figure the starting y offset, centering the text overall vertically
			int nLines = static_cast<int>(group.messages.size());
			int totalTextHeight = font->cellHeight * nLines;
			int y = (dmdHeight - totalTextHeight) / 2;

			// draw each string
			for (auto s : group.messages)
			{
				// measure the string
				const TCHAR *str = s.c_str();
				SIZE sz = font->MeasureString(str);

				// draw it centered horizontally
				font->DrawString32(str, pix, (dmdWidth - sz.cx) / 2, y, colors);

				// advance to the next line
				y += font->cellHeight;
			}

			// store the image
			images.emplace_back(HighScoreImage::DMDSpriteType, bmi, pix, group.displayTime, bgColor, bgAlpha);
		}
	}

	// Render using the alphanumeric style
	void RenderAlphanum()
	{
		// Scan the message group to determine the required grid size.
		// Start with the standard 16x2 character cells of the later 
		// System 11 games, but expand this as needed to fit the actual
		// text.
		int alphaGridWid = 16, alphaGridHt = 2;
		for (auto &group : slides)
		{
			// if this is the tallest message so far, remember it
			if (static_cast<int>(group.messages.size()) > alphaGridHt)
				alphaGridHt = static_cast<int>(group.messages.size());

			// scan the group for the widest line
			for (auto s : group.messages)
			{
				// if this is the widest line so far, remember it
				int wid = CountAlphaCells(s.c_str());
				if (wid > alphaGridWid)
					alphaGridWid = wid;
			}
		}

		// Get the "font" (which is actually the segment layout)
		const AlphanumFont *font = nullptr;
		for (auto &f : SplitFonts())
		{
			// if it starts with "alphanum-", consider it
			if (_tcsnicmp(f.c_str(), _T("alphanum-"), 9) == 0)
			{
				for (size_t i = 0; i < countof(alphaFonts); ++i)
				{
					if (_tcsicmp(f.c_str(), alphaFonts[i].name) == 0)
					{
						font = alphaFonts[i].font;
						break;
					}
				}
			}

			// stop if we found a match
			if (font != nullptr)
				break;
		}

		// If we didn't find a font, use the 'thin' font by default
		if (font == nullptr)
			font = &alphanumFontThin;

		// Rasterize to the layout size of the target window.  The actual
		// image could be stretched to a different size (either by changing
		// the window size, or by drawing it on a rescaled Javascript
		// drawing layer), but we have no way to guess at a better size,
		// and in the typical case, the window size will be exactly right.
		SIZE viewSize = view->GetLayoutSize();

		// We'll draw the display with a 4:1 aspect ratio, since that's
		// the window size most commonly used for the DMD window.  Start
		// with a 4:1 fit based on the full window width.
		int pixWid = viewSize.cx;
		int pixHt = pixWid / 4;

		// If that's too tall, shrink it to fit the view height instead
		if (pixHt > viewSize.cy)
			pixHt = viewSize.cy, pixWid = pixHt * 4;

		// Figure the cell width, by dividing the available width by the number
		// of characters, plus half a character of padding on each side.
		int charCellWid = pixWid / (alphaGridWid + 1);

		// Now figure the cell height based on the width, such that we maintain
		// the aspect ratio of the cells.
		int charCellHt = static_cast<int>(roundf(charCellWid * font->boxHeight / font->boxWidth));

		// If the total height is now too high, shrink the cells proportionally 
		// to fit.  Include 1/2 line for padding (1/4 at top and 1/4 at bottom)
		int impliedHeight = charCellHt*alphaGridHt + charCellHt/2;
		if (impliedHeight > viewSize.cy)
		{
			float reduce = static_cast<float>(viewSize.cy) / static_cast<float>(impliedHeight);
			charCellWid = static_cast<int>(charCellWid * reduce);
			charCellHt = static_cast<int>(charCellHt * reduce);
		}

		// Calculate the scaling factor from the abstract vector
		// layout coordinate system.  We use a uniform scale factor
		// on both axes, so we can calculate it from either axis.
		float scale = static_cast<float>(charCellWid) / font->boxWidth;

		// Figure the inset to center the image
		int xInset = (viewSize.cx - (charCellWid * alphaGridWid)) / 2;
		int yInset = (viewSize.cy - (charCellHt * alphaGridHt)) / 2;

		// figure the top left cell position with these margins
		int x0 = xInset, y0 = yInset;

		// Calculate the shear distance
		auto shear_dx = tanf(10.0f * DirectX::XM_PI / 180.0f);

		// Pre-calculate the scale-then-shear matrix.  Note that the 
		// GDI+ Y axis is top-down, so the shear distance will apply
		// to the *bottom* of the character cell.  We want the cells
		// to slant to the right - that is, the top is sheared to the
		// right.  Since it's the bottom end that we're shearing,
		// though, this means we want the shear to be to the left,
		// thus negative.
		auto xform = Gdiplus::Matrix();
		xform.Scale(scale, scale);
		xform.Shear(-shear_dx, 0);

		// Adjust the left edge by half of the shear distance, to keep
		// the overall image centered.  The shear will make the image
		// wider by the shear distance than the nominal cell widths.
		// Again, since GDI+ will apply the shear as a leftward shear
		// at the bottom of the image, the extra space from the shear
		// will be added at the left side, so we need to move right to
		// compensate.
		x0 = static_cast<int>(x0 + shear_dx * charCellWid / 2.0f);

		// draw the slides
		for (auto &group : slides)
		{
			// create the image
			DrawToImage(group, viewSize.cx, viewSize.cy, [this, &group, font, viewSize,
				charCellWid, charCellHt, alphaGridWid, alphaGridHt, scale, x0, y0, &xform]
				(Gdiplus::Graphics &gMain, BYTE *dibits)
			{
				// Draw a layer.  fixedSegmentMask is a bit mask of the segments to
				// draw unconditionally for this layer; this is OR'd with the segments
				// for the characters in the layer's message strings.  This is used
				// to draw the unlit segments onto the background layer.
				auto DrawLayer = [this, &gMain, &group, charCellWid, charCellHt, viewSize,
					alphaGridHt, alphaGridWid, scale,  font, &xform, x0, y0](
						const DMDView::AlphanumOptions::Layer &layerDesc, UINT fixedSegmentMask)
				{
					// create a DIB for our working surface for this layer
					std::unique_ptr<Gdiplus::Bitmap> layerBitmap(new Gdiplus::Bitmap(viewSize.cx, viewSize.cy, PixelFormat32bppARGB));
					Gdiplus::Graphics g(layerBitmap.get());

					// clear the background to solid transparent
					Gdiplus::SolidBrush trbr(Gdiplus::Color(0, 0, 0, 0));
					g.FillRectangle(&trbr, 0, 0, viewSize.cx, viewSize.cy);

					// use anti-aliasing for our polygon drawing
					g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

					// center it vertically
					int y = y0;
					int blankLines = alphaGridHt - static_cast<int>(group.messages.size());
					int blankTopLines = blankLines / 2;

					// draw a single character
					auto DrawChar = [fixedSegmentMask, &layerDesc, charCellWid, charCellHt, font, &g, &xform]
					(int x, int y, TCHAR ch, TCHAR chNxt)
					{
						// Get the segment mapping.  The segment mapping table only
						// covers characters from code points 32 to 127; substitute
						// the "missing character" mask for other cases.
						UINT mask = (ch >= 32 && ch <= 127 ? alphaSegmentMask[ch - 32] : alphaSegmentMissingCharMask);

						// add in the fixed segments
						mask |= fixedSegmentMask;

						// If the next character is a comma or period, and this
						// character isn't a comma or period, combine the comma/
						// period into this cell.
						bool combine = ((chNxt == '.' || chNxt == ',') && (ch != '.' && ch != ','));
						if (combine)
							mask |= alphaSegmentMask[chNxt - 32];

						// draw each segment
						Gdiplus::SolidBrush br(layerDesc.color);
						for (int seg = 0, bit = 1; seg < 16; ++seg, bit <<= 1)
						{
							// draw the segment if it's lit up
							if ((mask & bit) != 0)
							{
								// get the segment descriptor
								auto &segdesc = font->segment[seg];

								// set the drawing transforms for this cell - (scale * shear) * translate
								g.SetTransform(&xform);
								g.TranslateTransform(static_cast<float>(x), static_cast<float>(y), Gdiplus::MatrixOrderAppend);

								// fill the polygon
								g.FillPolygon(&br, segdesc.points, segdesc.nPoints);
							}
						}

						// return the combining status
						return combine;
					};

					// draw each line
					auto s = group.messages.begin();
					for (int line = 0; line < alphaGridHt; ++line)
					{
						// get the next item, if available, otherwise show a blank line
						const TCHAR *txt = _T("");
						if (line >= blankTopLines && s != group.messages.end())
						{
							txt = s->c_str();
							++s;
						}

						// start at the left edge
						int x = x0;

						// figure the number of spaces to the left and
						// right to center the line within the cell width
						int nAdvChars = CountAlphaCells(txt);
						int extraSpaces = alphaGridWid - nAdvChars;
						int leftSpaces = extraSpaces / 2;
						int rightSpaces = extraSpaces - leftSpaces;

						// draw the left spaces
						auto DrawSpaces = [&x, &y, charCellWid, charCellHt, &DrawChar](int n)
						{
							for (int i = 0; i < n; ++i)
							{
								// draw a space character
								DrawChar(x, y, ' ', 0);
								x += charCellWid;
							}
						};
						DrawSpaces(leftSpaces);

						// draw the characters
						TCHAR prvChar = 0;
						const TCHAR *p = txt;
						TCHAR c = *p;
						while (c != 0)
						{
							// draw it; skip an extra character if it combines with the next
							if (DrawChar(x, y, c, p[1]))
								++p;

							// skip to the next character
							c = *++p;
							x += charCellWid;
						}

						// draw the right spaces
						DrawSpaces(rightSpaces);

						// advance to the next line
						y += charCellHt;
					}

					// flush the graphics to the DIB
					g.Flush();

					// apply the dilation
					if (layerDesc.dilationx != 0 && layerDesc.dilationy != 0)
						layerBitmap.reset(Gdiplus::DilationEffectRect(
							layerBitmap.get(),
							static_cast<int>(static_cast<float>(layerDesc.dilationx) * scale),
							static_cast<int>(static_cast<float>(layerDesc.dilationy) * scale)));

					// apply the blur
					if (layerDesc.blur != 0)
					{
						// set up the blur effect
						Gdiplus::Blur blur;
						Gdiplus::BlurParams params = { fminf(scale * static_cast<float>(layerDesc.blur), 255.0f), FALSE };
						blur.SetParameters(&params);

						// apply it
						RECT rcEffect = { 0, 0, viewSize.cx, viewSize.cy };
						layerBitmap->ApplyEffect(&blur, &rcEffect);
					}

					// superimpose this layer onto the main layer
					gMain.DrawImage(layerBitmap.get(), 0, 0);
				};

				// fill the background layer with the background color
				Gdiplus::SolidBrush bkg(Gdiplus::Color(bgAlpha, bgColor.rgbRed, bgColor.rgbGreen, bgColor.rgbBlue));
				gMain.FillRectangle(&bkg, 0, 0, viewSize.cx, viewSize.cy);

				// Draw the unlit segments layer.  This layer is special because
				// we draw all of the segments instead of the lit segments.
				DrawLayer(alphanumOptions.unlit, 0xFFFF);

				// draw the back glow layer
				DrawLayer(alphanumOptions.glow2, 0);

				// draw the front glow layer
				DrawLayer(alphanumOptions.glow1, 0);

				// draw the lit segments layer
				DrawLayer(alphanumOptions.lit, 0);
			});
		}
	}

	// Render in the typewriter style
	void RenderTT()
	{
		// process each slide
		for (auto &group : slides)
		{
			// Load the background image.  If we found a user media file, try loading
			// that first.
			std::unique_ptr<Gdiplus::Image> ttBkgImage;
			if (ttBkgImageFile.size() != 0)
			{
				// We have a file - try loading it.  If that fails, discard the image.
				ttBkgImage.reset(Gdiplus::Image::FromFile(ttBkgImageFile.c_str(), TRUE));
				if (ttBkgImage != nullptr && ttBkgImage->GetLastStatus() != Gdiplus::Status::Ok)
					ttBkgImage.reset();
			}

			// if we didn't successfully load a user background image, load the default
			// index card background
			if (ttBkgImage == nullptr)
				ttBkgImage.reset(GPBitmapFromPNG(IDB_INDEX_CARD));

			// size the image to match the background
			int wid = ttBkgImage.get()->GetWidth();
			int ht = ttBkgImage.get()->GetHeight();

			// draw the image
			DrawToImage(group, wid, ht, [this, &group, wid, ht, &ttBkgImage](Gdiplus::Graphics &g, void *)
			{
				// copy the background
				g.DrawImage(ttBkgImage.get(), 0, 0, wid, ht);

				// Figure the font name.  If the caller specified a font list, use that
				// with default weight and style, otherwise use the TTHighScoresFont
				// setting (with the weight and style specified there).
				const TCHAR *family = fontName.c_str();
				int weight = 400;
				int ptSize = 0;
				bool italic = false;
				if (fontName.length() == 0)
				{
					family = ttHighScoreFont.family.c_str();
					weight = ttHighScoreFont.weight;
					italic = ttHighScoreFont.italic;
					ptSize = ttHighScoreFont.ptSize;
				}

				// If a non-default font size was specified in the settings (indicated
				// by a non-zero size in the FontPref), draw the text in the specified
				// point size.  Otherwise draw it at a pixel height of 1/8 of the
				// background image height, so that we can fit about 8 lines of text
				// into the image.  That harmonizes well with the default "index card"
				// background image.  (But not necessarily with a custom background,
				// which is why we make this an option.)
				std::unique_ptr<Gdiplus::Font> font;
				if (ptSize != 0)
					font.reset(CreateGPFont(family, ptSize, weight, italic));
				else
				{
					int gpStyleBits = (weight >= 700 ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular);
					if (italic) gpStyleBits |= Gdiplus::FontStyleItalic;
					font.reset(CreateGPFontPixHt(family, ht / 8, static_cast<Gdiplus::FontStyle>(gpStyleBits)));
				}

				// combine the text into a single string separated by line breaks
				TSTRING txt;
				for (auto s : group.messages)
				{
					if (txt.length() != 0)
						txt += _T("\n");
					txt += s.c_str();
				}

				// draw it centered horizontally and vertically
				Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
				fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
				fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
				Gdiplus::SolidBrush br(GPColorFromCOLORREF(ttHighScoreTextColor));
				g.DrawString(txt.c_str(), -1, font.get(), Gdiplus::RectF(0, 0, (float)wid, (float)ht), &fmt, &br);
			});
		}
	}

};

DWORD DMDView::GenerateDMDImage(
	BaseView *view, std::list<TSTRING> &messages,
	const TCHAR *style, const TCHAR *fontName,
	RGBQUAD *pTxtColor, RGBQUAD *bgColor, BYTE bgAlpha,
	AlphanumOptions *alphanumOptions)
{
	// generate a sequence number
	DWORD seqno = nextImageRequestSeqNo++;

	// If the style is "auto" or not specified, get the current game's
	// explicit style setting
	if (style == nullptr || _tcsicmp(style, _T("auto")) == 0)
		style = GetCurGameHighScoreStyle();

	// build the color ramp based on the text and background color if provided,
	// or based on the current game's VPinMAME colors if not
	DMDPalette pal;
	GetCurGameHighScoreColor(pal, pTxtColor, bgColor);

	// create the thread object
	std::unique_ptr<HighScoreGraphicsGenThread> th(new HighScoreGraphicsGenThread(
		view, seqno, pal, style));

	// set the font, if provided
	if (fontName != nullptr)
		th->fontName = fontName;

	// set the background color, if provided
	if (bgColor != nullptr)
		th->bgColor = *bgColor;

	// set the background alpha
	th->bgAlpha = bgAlpha;

	// set the alphanumeric options, if provided
	if (alphanumOptions != nullptr)
		th->alphanumOptions = *alphanumOptions;
	else
		th->alphanumOptions.InitFromSegColor(pal.color[15]);

	// create a slide and populate it with the messages
	auto &slide = th->slides.emplace_back();
	for (auto const &m : messages)
		slide.messages.emplace_back(m);

	// launch the generator thread
	th.release()->Launch();

	// return the sequence number
	return seqno;
}

void DMDView::GenerateHighScoreImages()
{
	// remove any previous high-score graphics
	ClearHighScoreImages();

	// Assign the high score request sequence number.  This lets us
	// determine if the asynchronous results from the thread we launch
	// are the results we most recently requested.  We discard any
	// results that arrive after we've already switched to a new game.
	pendingImageRequestSeqNo = nextImageRequestSeqNo++;

	// if a game is active, and it has high scores, generate graphics
	if (auto game = currentBackground.game; game != nullptr && game->highScores.size() != 0)
	{
		// get this game's high score style setting
		const TCHAR *style = GetCurGameHighScoreStyle();
		
		// if the style is "none", skip high score display for this game
		if (_tcsicmp(style, _T("none")) == 0)
			return;

		// get the default dot color
		DMDPalette pal;
		GetCurGameHighScoreColor(pal);

		// create the high score thread
		HighScoreGraphicsGenThread *th = new HighScoreGraphicsGenThread(
			this, pendingImageRequestSeqNo, pal, style);
		
		// capture the message list to the thread
		game->DispHighScoreGroups([&th, &style](const std::list<const TSTRING*> &group)
		{
			if (_tcsicmp(style, _T("alpha")) == 0 && group.size() > 2)
			{
				// We're in alphanumeric mode, so limit messages to two
				// lines.  For an odd number of lines, add a one liner
				// first, then add pairs.  Otherwise just add pairs.
				auto it = group.begin();
				auto Add = [&it, &th](int nLines)
				{
					// add a message group
					auto &slide = th->slides.emplace_back();
					slide.displayTime = 3500;
					auto AddLine = [&slide](const TSTRING *s) { slide.messages.emplace_back(*s); };

					// special case: if we're adding one really long line, break it up
					if (nLines == 1 && (*it)->length() > 16)
					{
						// find the last space or punctuation mark before the 16th column
						const TCHAR *start = (*it)->c_str();
						const TCHAR *punct = nullptr;
						for (const TCHAR *p = start; *p != 0 && p - start < 16; ++p)
						{
							if (*p == ' ' && p - start <= 16)
								punct = p;
							else if (p - start <= 15 && (*p == '.' || *p == ',' || *p == '-'))
								punct = p;
						}

						// if we found a break point, break there
						if (punct != nullptr)
						{
							TSTRING l1(start, punct - start + (*punct == ' ' ? 0 : 1));
							TSTRING l2(punct + 1);
							AddLine(&l1);
							AddLine(&l2);
							++it;
							return;
						}
					}

					// add the lines to the new message group
					for ( ; nLines != 0 ; --nLines, ++it)					
						AddLine(*it);
				};

				// if we have an odd number of lines, add the first line
				// as its own group
				if ((group.size() & 1) == 1)
					Add(1);

				// now add pairs until we exhaust the list
				while (it != group.end())
					Add(2);
			}
			else
			{
				// add the group exactly as it came from PinEMhi
				auto &slide = th->slides.emplace_back();
				slide.displayTime = 3500;
				for (auto s : group)
					slide.messages.emplace_back(*s);
			}
		});


		// if there's only one message in the list, increase its display
		// time slightly
		if (th->slides.size() == 1)
			th->slides.begin()->displayTime += 2000;

		// launch the thread
		th->Launch();
	}
}

const TCHAR *DMDView::GetCurGameHighScoreStyle()
{
	// if there's no game, use "DMD" as the default
	auto game = currentBackground.game;
	if (game == nullptr)
		return _T("DMD");

	// if the game has an explicit setting that's not null, empty,
	// or "auto", use the game's setting
	auto style = GameList::Get()->GetHighScoreStyle(game);
	if (style != nullptr && style[0] != 0 && _tcsicmp(style, _T("auto")) != 0)
		return style;

	// Okay, the game doesn't have an explicit setting.  Figure the
	// style based on the game's type and era:
	//
	// - Typewriter style: all tables with type "EM" (electromechanical)
	//   and "ME" (pure mechanical); any table from before 1978.
	//
	// - Alphanumeric 16-segment style:  any machine from 1978-1990;
	//   any type "SS" machine from 1990 or earlier; and the handful of
	//   1991 Williams titles that used segmented displays, namely 
	//   Funhouse, Harley-Davidson, and The Machine: Bride of PinBot.
	// 
	//   Note that the year alone isn't a perfect criterion for the
	//   machine type.  By starting in 1978, we'll exclude some of the
	//   very early SS machines (IPDB's first SS listing is in 1974,
	//   and a handful can be found in each year from 1975-77), and
	//   we'll misclassify a number of 1978-79 EM machines as SS: 1978
	//   was about a 50/50 mix, and there were still a few made in
	//   1979.  (EM machines are practically non-existent from 1980
	//   onwards, though.)  But 1978 is definitely the turning point;
	//   it's the first year in which SS machines represented a
	//   significant fraction of the total, and the last in which EM
	//   machines did.  And for our purposes, it's better to err on
	//   the side of SS, because for the most part we can only get
	//   high score data for SS machines anyway - we get the data via
	//   PINemHi, which reads from NVRAM, which mostly exists only
	//   for SS machines.
	//
	// - DMD: anything else.
	//
	// These rules should be pretty reliable at matching the table
	// type as long as the game's metadata are correct.  The main
	// weakness is the reliance on title matching for the special
	// 1991 machines, since that will be fooled by translated names.
	// But the algorithm really doesn't have to be perfect, as the
	// user can easily override the auto style selection in the game
	// metadata.
	//
	if (game->tableType == _T("EM") || game->tableType == _T("ME"))
	{
		// electromechanical or pure mechanical - use typewriter style
		return _T("TT");
	}
	else if (game->year != 0 && game->year < 1978)
	{
		// almost everything before 1978 is EM, so use typewriter style
		return _T("TT");
	}
	else if (game->tableType == _T("SS") && game->year != 0 && game->year <= 1990)
	{
		// It's a solid state table from 1990 or earlier.  All such
		// tables should be alphanumeric.
		return _T("Alpha");
	}
	else if (game->tableType == _T("") && game->year >= 1978 && game->year <= 1990)
	{
		// This machine doesn't have a type setting, but most
		// machines during this period were solid-state with
		// alphanumeric displays, so use that by default.
		return _T("Alpha");
	}
	else if (game->year == 1991)
	{
		// It's a 1991 title.  This was on the cusp of the transition
		// from alphanumeric to DMD.  Check for the handful of alpha
		// titles from this year.
		const static std::basic_regex<TCHAR> an1991Titles(
			_T("funhouse|harley.*davidson|bride\\s*of\\s*pin.?bot"),
			std::regex_constants::icase);
		if (std::regex_search(game->title, an1991Titles))
			return _T("Alpha");
	}

	// We didn't find a reason to use any other style - use the default DMD style
	return _T("DMD");
}

void DMDView::GetCurGameHighScoreColor(DMDPalette &pal, RGBQUAD *pTxtColor, RGBQUAD *pBgColor)
{
	// Start with a foreground (text) color set to an amber that approximates
	// the hue of the original monochrome plasma displays in the 1990s machines,
	// and a black background.
	RGBQUAD txtColor = { 32, 88, 255 };
	RGBQUAD bgColor = { 0, 0, 0 };

	// if the caller supplied text and/or background colors, override the defaults
	if (pTxtColor != nullptr)
		txtColor = *pTxtColor;
	if (pBgColor != nullptr)
		bgColor = *pBgColor;

	// Get the VPinMAME ROM key for the game, if possible
	TSTRING rom;
	HKEYHolder hkey;
	bool keyOk = false;
	auto game = currentBackground.game;
	if (game != nullptr && VPinMAMEIfc::FindRom(rom, game))
	{
		// open the registry key for the game
		MsgFmt romkey(_T("%s\\%s"), VPinMAMEIfc::configKey, rom.c_str());
		keyOk = (RegOpenKey(HKEY_CURRENT_USER, romkey, &hkey) == ERROR_SUCCESS);
	}

	// if we didn't get a key that way, try the VPM "default"
	// key, which contains default settings for new tables
	if (!keyOk)
	{
		MsgFmt dfltkey(_T("%s\\default"), VPinMAMEIfc::configKey);
		keyOk = (RegOpenKey(HKEY_CURRENT_USER, dfltkey, &hkey) == ERROR_SUCCESS);
	}

	// If we got a key (either game-specific or default), retrieve the VPM DMD color
	// settings, so that we can replicate the VPM display colors.
	//
	// VPM has a complex set of variables for the DMD color scheme that evolved over
	// several feature additions.  Each new feature basically replaced the previous
	// scheme, so the variables look like they have redundant and potentially
	// contradictory information, which they do.  The conflicts are resolved by
	// giving the variables a precedence hierarchy.  A variable "exists" if at least
	// one of the variables in its group has a positive RGB value.  (VPM could
	// define variable existence more rigorously by distinguishing a "null" value
	// for items that don't appear in the registry, but they don't bother; they
	// just default missing items to RGB {0,0,0} values and use {0,0,0} as both
	// black and null.  This makes color settings composed entirely of black
	// impossible to represent, but I doubt this bothers anyone, since all-black
	// would be a pathological case for this application.  The VPM code also
	// treats negative component values as nulls, but I don't think it actually
	// stores negative values.  Even so, we should tolerate negative values.)
	struct VPMDMDVars
	{
		// Note: we'll use the VPM registry key names to clarify the correspondences.

		// Colorization option flag.  Boolean (as integer 0 or 1).
		bool dmd_colorize = false;

		// 2-bit brightness ramp.  These give the intensities relative to the
		// color value at the 0%, 33%, and 66% points.  Note that when we say
		// "X%", we're talking about the NOMINAL brightness - what we really
		// mean by "0%" is "brightness at palette level 0", and likewise for
		// 33% = palette 1 and 66% = palette 2.  VPM thinks about these as
		// percentage brightnesses because the original physical displays
		// achieved different brightness levels by duty-cycle modulation, with
		// duty cycles of approximately all-off, 1/3 time, 2/3 time, and all-on,
		// or 0%, 33%, 67%, and 100%.  When translating to a video display, it
		// was found to be desirable to tweak the levels a bit because of the
		// different physical display properties (black level, dynamic range,
		// gamma curve).  These are integer percentage values, 0..100.
		int dmd_perc0 = 20, dmd_perc33 = 33, dmd_perc66 = 67;

		// DMD monochrome base color.  This gives the "100%" color value, with
		// the 0%, 33%, and 66% levels in the 2-bit ramp derived by applying a
		// fixed brightness ramp to these values.  These are RGB component
		// values, 00..FF.
		int dmd_red = 255, dmd_green = 88, dmd_blue = 32;

		// Colorized DMD: these give the individual 0%, 33%, and 66% levels
		// for the 2-bit brightness ramp.  This allows turning the brightness
		// levels into a full 2-bit color palette, to create colorization effects.
		// RGB component values, 00..FF.
		int dmd_red0 = 0, dmd_green0 = 0, dmd_blue0 = 0;
		int dmd_red33 = 0, dmd_green33 = 0, dmd_blue33 = 0;
		int dmd_red66 = 0, dmd_green66 = 0, dmd_blue66 = 0;
	};
	VPMDMDVars vars;

	if (keyOk)
	{
		// query one of the values from the key
		auto queryBool = [&hkey](const TCHAR *valName, bool &val)
		{
			DWORD typ, tmp, siz = sizeof(tmp);
			if (RegQueryValueEx(hkey, valName, NULL, &typ, (BYTE*)&tmp, &siz) == ERROR_SUCCESS
				&& typ == REG_DWORD)
				val = (tmp != 0);
		};
		auto queryInt = [&hkey](const TCHAR *valName, int &val)
		{
			DWORD typ, tmp, siz = sizeof(tmp);
			if (RegQueryValueEx(hkey, valName, NULL, &typ, (BYTE*)&tmp, &siz) == ERROR_SUCCESS
				&& typ == REG_DWORD)
				val = static_cast<int>(tmp);
		};
		queryBool(_T("dmd_colorize"), vars.dmd_colorize);
		queryInt(_T("dmd_perc0"), vars.dmd_perc0);
		queryInt(_T("dmd_perc33"), vars.dmd_perc33);
		queryInt(_T("dmd_perc66"), vars.dmd_perc66);
		queryInt(_T("dmd_red"), vars.dmd_red);
		queryInt(_T("dmd_green"), vars.dmd_green);
		queryInt(_T("dmd_blue"), vars.dmd_blue);
		queryInt(_T("dmd_red0"), vars.dmd_red0);
		queryInt(_T("dmd_green0"), vars.dmd_green0);
		queryInt(_T("dmd_blue0"), vars.dmd_blue0);
		queryInt(_T("dmd_red33"), vars.dmd_red33);
		queryInt(_T("dmd_green33"), vars.dmd_green33);
		queryInt(_T("dmd_blue33"), vars.dmd_blue33);
		queryInt(_T("dmd_red66"), vars.dmd_red66);
		queryInt(_T("dmd_green66"), vars.dmd_green66);
		queryInt(_T("dmd_blue66"), vars.dmd_blue66);
	}

	// Now apply the VPM variables.  The algorithm is complex and ad hoc, so I
	// won't try to explain it here; I'll just more or less reproduce the VPM
	// code.  Refer to the VPM source code at src/wpc/core.c:PALETTE_INIT().
	int rStart = 0xFF, gStart = 0xE0, bStart = 0x20;
	int perc66 = 67, perc33 = 33, perc0 = 20;
	if (vars.dmd_red > 0 || vars.dmd_green > 0 || vars.dmd_blue > 0)
	{
		rStart = vars.dmd_red;
		gStart = vars.dmd_green;
		bStart = vars.dmd_blue;
	}
	if (vars.dmd_perc0 > 0 || vars.dmd_perc33 > 0 || vars.dmd_perc66 > 0)
	{
		perc66 = vars.dmd_perc66;
		perc33 = vars.dmd_perc33;
		perc0 = vars.dmd_perc0;
	}
	
	// Generate the monochrome color ramp entries for 0%, 33%, 66%, and 100%
	// pal[0]  -> 0%
	pal.color[0].rgbRed = static_cast<BYTE>(rStart * perc0 / 100);
	pal.color[0].rgbGreen = static_cast<BYTE>(gStart * perc0 / 100);
	pal.color[0].rgbBlue = static_cast<BYTE>(bStart * perc0 / 100);

	// pal[5]  -> 33%
	pal.color[5].rgbRed = static_cast<BYTE>(rStart * perc33 / 100);
	pal.color[5].rgbGreen = static_cast<BYTE>(gStart * perc33 / 100);
	pal.color[5].rgbBlue = static_cast<BYTE>(bStart * perc33 / 100);

	// pal[10] -> 66%
	pal.color[10].rgbRed = static_cast<BYTE>(rStart * perc66 / 100);
	pal.color[10].rgbGreen = static_cast<BYTE>(gStart * perc66 / 100);
	pal.color[10].rgbBlue = static_cast<BYTE>(bStart * perc66 / 100);

	// pal[15] -> 100%
	pal.color[15].rgbRed = static_cast<BYTE>(rStart);
	pal.color[15].rgbGreen = static_cast<BYTE>(gStart);
	pal.color[15].rgbBlue = static_cast<BYTE>(bStart);

	// If colorization mode is set, treat it as a paletted display rather than
	// a monochrome brightness ramp, using the individual palette entries from
	// the options.
	if (vars.dmd_colorize)
	{
		if (vars.dmd_red0 != 0 || vars.dmd_green0 != 0 || vars.dmd_blue0 != 0)
		{
			pal.color[0].rgbRed = static_cast<BYTE>(vars.dmd_red0);
			pal.color[0].rgbGreen = static_cast<BYTE>(vars.dmd_green0);
			pal.color[0].rgbBlue = static_cast<BYTE>(vars.dmd_blue0);
		}
		if (vars.dmd_red33 != 0 || vars.dmd_green33 != 0 || vars.dmd_blue33 != 0)
		{
			pal.color[5].rgbRed = static_cast<BYTE>(vars.dmd_red33);
			pal.color[5].rgbGreen = static_cast<BYTE>(vars.dmd_green33);
			pal.color[5].rgbBlue = static_cast<BYTE>(vars.dmd_blue33);
		}
		if (vars.dmd_red66 != 0 || vars.dmd_green66 != 0 || vars.dmd_blue66 != 0)
		{
			pal.color[10].rgbRed = static_cast<BYTE>(vars.dmd_red66);
			pal.color[10].rgbGreen = static_cast<BYTE>(vars.dmd_green66);
			pal.color[10].rgbBlue = static_cast<BYTE>(vars.dmd_blue66);
		}
	}

	// Interpolate colors between the defined palette points (0%, 33%, 66%, 100%)
	auto Interpolate = [&pal](int base)
	{
		// get the color at the starting position
		float r = static_cast<float>(pal.color[base].rgbRed);
		float g = static_cast<float>(pal.color[base].rgbGreen);
		float b = static_cast<float>(pal.color[base].rgbBlue);

		// figure the increment at each step from here to the next stop, on a linear ramp
		float dr = static_cast<float>(pal.color[base + 5].rgbRed - pal.color[base].rgbRed) / 5.0f;
		float dg = static_cast<float>(pal.color[base + 5].rgbGreen - pal.color[base].rgbGreen) / 5.0f;
		float db = static_cast<float>(pal.color[base + 5].rgbBlue - pal.color[base].rgbBlue) / 5.0f;

		// fill in the intermediate colors
		for (int i = base + 1; i < base + 5; ++i)
		{
			pal.color[i].rgbRed = static_cast<BYTE>(roundf(r += dr));
			pal.color[i].rgbGreen = static_cast<BYTE>(roundf(g += dg));
			pal.color[i].rgbBlue = static_cast<BYTE>(roundf(b += db));
		}
	};
	Interpolate(0);
	Interpolate(5);
	Interpolate(10);
}

void DMDView::WaitForHighScoreThreads(DWORD timeout)
{
	// get the starting time
	DWORD t0 = GetTickCount();

	// wait, but not forever
	while (nHighScoreThreads != 0 && (timeout == INFINITE || (DWORD)(GetTickCount() - t0) < timeout))
		Sleep(100);
}

bool DMDView::OnUserMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	// look for our recognized messages
	switch (msg)
	{
	case BVMsgDMDImageReady:
		// check if it's our background image
		SetHighScoreImages(static_cast<DWORD>(wParam), reinterpret_cast<std::list<HighScoreImage> *>(lParam));

		// also pass it on to the base view, in case it's a Javascript request
		break;
	}

	// it's not one of ours - inherit the default handling
	return __super::OnUserMessage(msg, wParam, lParam);
}

void DMDView::SetHighScoreImages(DWORD seqno, std::list<HighScoreImage> *images)
{
	// If the sequence number matches the current request, install
	// this list of images
	if (seqno == pendingImageRequestSeqNo)
	{
		// clear the old images
		highScoreImages.clear();

		// transfer the images to our high score list
		for (auto &i : *images)
			highScoreImages.emplace_back(i);

		// set up at the end of the high score list, to indicate that we're
		// not currently showing one of these images
		highScorePos = highScoreImages.end();

		// update the image list
		UpdateDrawingList();

		// Set a timer to start the slide show
		SetTimer(hWnd, StartHighScoreTimerID, StillImageDisplayTime, NULL);
	}
}

const DMDFont *DMDView::PickHighScoreFont(const std::list<TSTRING> &group)
{
	// build the pointer-based list
	std::list<const TSTRING*> pgroup;
	for (auto &s : group)
		pgroup.emplace_back(&s);

	// call the common handler with the pointer-based list
	return PickHighScoreFont(pgroup);
}

bool DMDView::OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	// If we're looping the video, check for high score images: if
	// present, start a slide show of the high score images instead
	// of going directly to a replay of the video.  If a game is
	// currently running, skip the score display and just loop the
	// video - we suppress score display while running.
	if (msg == AVPMsgLoopNeeded 
		&& highScoreImages.size() != 0 
		&& !Application::Get()->IsGameActive()
		&& currentBackground.sprite != nullptr
		&& currentBackground.sprite->IsVideo()
		&& currentBackground.sprite->GetMediaCookie() == wParam)
	{
		// stop the video
		currentBackground.sprite->GetVideoPlayer()->Stop(SilentErrorHandler());

		// start the high score slideshow
		StartHighScorePlayback();

		// skip the system handling, as we don't want to loop the video yet
		return true;
	}

	// inherit the base handling
	return __super::OnAppMessage(msg, wParam, lParam);
}

void DMDView::StartHighScorePlayback()
{
	if (highScoreImages.size() != 0)
	{
		// if a video is playing, stop it
		if (currentBackground.sprite != nullptr && currentBackground.sprite->IsVideo())
			currentBackground.sprite->GetVideoPlayer()->Stop(SilentErrorHandler());

		// start at the first high score image
		highScorePos = highScoreImages.begin();

		// rebuild the image list
		UpdateDrawingList();

		// set a timer to rotate to the next image
		SetTimer(hWnd, NextHighScoreTimerID, highScorePos->displayTime, NULL);
	}
}

bool DMDView::OnTimer(WPARAM timer, LPARAM callback)
{
	switch (timer)
	{
	case StartHighScoreTimerID:
		// this is a one-shot timer, so remove it
		KillTimer(hWnd, timer);

		// check if the background is a video
		if (currentBackground.sprite != nullptr && currentBackground.sprite->IsVideo())
		{
			// It's a video, so ignore the timer message.  We coordinate the
			// slide show timing with the video loop cycle instead.
		}
		else
		{
			// it's a still image, so it has no loop timing of its own; start
			// the slide show on the timer
			StartHighScorePlayback();
		}

		// start the high score slideshow playback
		return true;

	case NextHighScoreTimerID:
		// this is a one-shot timer, so remove it
		KillTimer(hWnd, timer);

		// advance to the next high score position
		if (highScorePos != highScoreImages.end())
			highScorePos++;

		// update the drawing list with the new image
		UpdateDrawingList();

		// display the next image, or return to the background image
		if (highScorePos != highScoreImages.end())
		{
			// set a new timer to advance when this image is done
			SetTimer(hWnd, NextHighScoreTimerID, highScorePos->displayTime, NULL);
		}
		else
		{
			// if we have a video start, restart playback
			if (currentBackground.sprite != nullptr && currentBackground.sprite->IsVideo())
			{
				// restart playback
				currentBackground.sprite->GetVideoPlayer()->Replay(SilentErrorHandler());
			}
			else
			{
				// it's a still image, so start a timer to switch to the high
				// score slide show after the image has been displayed a while
				SetTimer(hWnd, StartHighScoreTimerID, StillImageDisplayTime, NULL);
			}
		}

		// handled
		return true;
	}

	// return the base handling
	return __super::OnTimer(timer, callback);
}

void DMDView::AddBackgroundToDrawingList()
{
	// if we have a high score image, draw that; otherwise use the
	// base background image
	if (highScoreImages.size() != 0 && highScorePos != highScoreImages.end())
	{
		// if we haven't created a sprite for this background yet, do so now
		highScorePos->CreateSprite();

		// add it to the sprite list if we have a valid sprite
		AddToDrawingList(highScorePos->sprite);
	}
	else
	{
		// no high score image to display - use the default background
		__super::AddBackgroundToDrawingList();
	}
}

void DMDView::ScaleSprites()
{
	// do the base class work
	__super::ScaleSprites();

	// scale the high score images
	for (auto &i : highScoreImages)
		ScaleSprite(i.sprite, 1.0f, dotsFixedAspectRatio);
}

void DMDView::BeginRunningGameMode(GameListItem *game, GameSystem *system, bool &hasVideos)
{
	// do the base class work
	__super::BeginRunningGameMode(game, system, hasVideos);

	// if we're showing high score images, return to the base image and 
	// cancel the high score rotation
	if (highScoreImages.size() != 0)
	{
		// kill any pending timers
		KillTimer(hWnd, StartHighScoreTimerID);
		KillTimer(hWnd, NextHighScoreTimerID);

		// If a high score image is currently being displayed, and we have a
		// video, the video is currently stopped while showing high scores.
		// Restart the video.
		if (highScorePos != highScoreImages.end() && currentBackground.sprite != nullptr && currentBackground.sprite->IsVideo())
			currentBackground.sprite->GetVideoPlayer()->Replay(SilentErrorHandler());

		// go to the end of the high score rotation
		highScorePos = highScoreImages.end();

		// update the drawing list so that we're showing the background media
		// (instead of a high score slide)
		UpdateDrawingList();
	}
}

void DMDView::EndRunningGameMode()
{
	// do the base class work
	__super::EndRunningGameMode();

	// if we have high score images, re-start the high score rotation
	if (highScoreImages.size() != 0)
		SetTimer(hWnd, StartHighScoreTimerID, StillImageDisplayTime, NULL);
}

// -----------------------------------------------------------------------
//
// DMD sprite
//
Shader *DMDSprite::GetShader() const
{
	return Application::Get()->dmdShader.get();
}

void DMDSprite::Render(Camera *camera)
{
	// set the background color in the shader
	Application::Get()->dmdShader->SetBgColor(bgColor, bgAlpha);

	// do the base class rendering
	__super::Render(camera);
}
