// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// DMD view

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

namespace ConfigVars
{
	static const TCHAR *DMDWinVarPrefix = _T("DMDWindow");
};

// DMD sprite.  This is a simple subclass of the regular sprite
// that uses the special DMD shader, which renders a simulation
// of the visible pixel structure of a physical DMD.
class DMDSprite : public Sprite
{
protected:
	virtual Shader *GetShader() const override { return Application::Get()->dmdShader.get(); }
};


// construction
DMDView::DMDView() : SecondaryView(IDR_DMD_CONTEXT_MENU, ConfigVars::DMDWinVarPrefix),
	highScoreRequestSeqNo(0),
	highScorePos(highScoreImages.end()),
	nHighScoreThreads(0)
{
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

// still image display time, for the high-score slide show
const int StillImageDisplayTime = 7000;

// native DMD size
static const int dmdWidth = 128, dmdHeight = 32;

void DMDView::GenerateHighScoreImages()
{
	// remove any previous high-score graphics
	ClearHighScoreImages();

	// Advance the high score request sequence number.  This lets us
	// determine if the asynchronous results from the thread we launch
	// are the results we most recently requested.  We discard any
	// results that arrive after we've already switched to a new game.
	++highScoreRequestSeqNo;

	// if a game is active, and it has high scores, generate graphics
	if (auto game = currentBackground.game; game != nullptr && game->highScores.size() != 0)
	{
		// get this game's high score style setting; if it's not set,
		// use "auto" as the default
		const TCHAR *style = GameList::Get()->GetHighScoreStyle(game);
		if (style == nullptr || style[0] == 0)
			style = _T("auto");
		
		// if the style is "none", skip high score display for this game
		if (_tcsicmp(style, _T("none")) == 0)
			return;

		// If the style is "auto", figure out which actual style to use:
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
		if (_tcsicmp(style, _T("auto")) == 0)
		{
			// DMD is the default if we don't find some other type
			style = _T("DMD");

			// Check for cases where we override the DMD default
			if (game->tableType == _T("EM") || game->tableType == _T("ME"))
			{
				// electromechanical or pure mechanical - use typewriter style
				style = _T("TT");
			}
			else if (game->year != 0 && game->year < 1978)
			{
				// almost everything before 1978 is EM, so use typewriter style
				style = _T("TT");
			}
			else if (game->tableType == _T("SS") && game->year != 0 && game->year <= 1990)
			{
				// It's a solid state table from 1990 or earlier.  All such
				// tables should be alphanumeric.
				style = _T("Alpha");
			}
			else if (game->tableType == _T("") && game->year >= 1978 && game->year <= 1990)
			{
				// This machine doesn't have a type setting, but most
				// machines during this period were solid-state with
				// alphanumeric displays, so use that by default.
				style = _T("Alpha");
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
					style = _T("Alpha");
			}
		}

		// Get the VPinMAME ROM key for the game, if possible
		TSTRING rom;
		HKEYHolder hkey;
		bool keyOk = false;
		if (VPinMAMEIfc::FindRom(rom, game))
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

		// If we got a key, retrieve the VPM DMD color settings for the 
		// game, so that we can use the same color scheme for our text. 
		// (As a default, use an orange that approximates the color of
		// the original plasma DMDs on the 1990s machines.)
		RGBQUAD txtColor = { 32, 88, 255 };
		if (keyOk)
		{
			// query one of the values from the key
			auto queryf = [&hkey](const TCHAR *valName, DWORD &val)
			{
				DWORD typ, siz = sizeof(val);
				return (RegQueryValueEx(hkey, valName, NULL, &typ, (BYTE*)&val, &siz) == ERROR_SUCCESS
					&& typ == REG_DWORD);
			};
			DWORD r, g, b;
			if (queryf(_T("dmd_red"), r) && queryf(_T("dmd_green"), g) && queryf(_T("dmd_blue"), b))
				txtColor = { (BYTE)b, (BYTE)g, (BYTE)r };
		}

		// Creating the high-score images can be rather time-consuming, 
		// especially for the alphanumeric style, as GDI+ is painfully
		// slow at copying the character cell images.  Alphanumeric prep
		// times can be as long as 400ms.  That's way too long to stall
		// the UI, so we have to do it on a background thread.
		struct ThreadInfo
		{
			ThreadInfo(DMDView *view, DWORD seqno, RGBQUAD txtColor, const TCHAR *style) :
				seqno(seqno),
				txtColor(txtColor),
				style(style)
			{
				// assign the view pointer explicitly, to add a ref count
				this->view = view;
			}

			// associated view window
			RefPtr<DMDView> view;

			// high score request sequence number
			DWORD seqno;

			// text color for the DMD or alpha display
			RGBQUAD txtColor;

			// display style for the game
			TSTRING style;

			// Messages to display.  Each sublist is a set of strings to
			// display together on one display slide; the overall list is
			// the set of slides to display in time sequence.
			std::list<std::list<TSTRING>> messages;

			// thread entrypoint
			static DWORD WINAPI SMain(LPVOID lParam) { return static_cast<ThreadInfo*>(lParam)->Main(); }
			DWORD Main()
			{

				// Figure the background color, using the text color at reduced
				// brightness.  This helps simulate the visible pixel structure of 
				// a real DMD by showing a little of the text color even in pixels
				// that are fully "off".
				RGBQUAD bgColor = { (BYTE)(txtColor.rgbBlue / 10), (BYTE)(txtColor.rgbGreen / 10), (BYTE)(txtColor.rgbRed / 10) };

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

				// Build a color index table, with a ramp of brightness values
				// from the background color to the full-brightness text color.
				int redSpan = txtColor.rgbRed - bgColor.rgbRed;
				int greenSpan = txtColor.rgbGreen - bgColor.rgbGreen;
				int blueSpan = txtColor.rgbBlue - bgColor.rgbBlue;
				DMDFont::Color colors[16];
				for (int i = 0; i < 16; ++i)
				{
					colors[i].Set(
						bgColor.rgbRed + redSpan * i / 15,
						bgColor.rgbGreen + greenSpan * i / 15,
						bgColor.rgbBlue + blueSpan * i / 15);
				}

				// Count the character cells in an alphanumeric string.  This is
				// slightly more complicated than just counting the characters,
				// because of the special handling of '.' and ',': these combine
				// with the previous character, since the dot/comma element in
				// each cell can be "illuminated" in addition to any other glyph.
				auto CountAlphaCells = [](const TCHAR *str)
				{
					int nCells = 0;
					for (TCHAR prvChar = 0; *str != 0; ++str)
					{
						// get this character
						TCHAR c = *str;

						// Check for combining characters.  A '.' or ',' can combine
						// with the previous character to form a single cell, provided
						// that the previous character isn't also '.' or ',', and that
						// this isn't the first cell.
						if (!((c == '.' || c == ',') && !(nCells == 0 || prvChar == '.' || prvChar == ',')))
							++nCells;

						// this is the next character for the next iteration
						prvChar = c;
					}

					// return the cell count
					return nCells;
				};

				// If we're using alphanumeric segmented display style, we
				// have a limited repertoire of colors for the pre-drawn images.
				// Find the color that's closest to the VPM display color. 
				//
				// While we're at it, also figure the required grid size.  Alpha-
				// numeric segmented displays use fixed character cells, so the 
				// simulation is most convincing if the whole series of messages 
				// is displayed on the same fixed grid layout.  This supports the
				// illusion that the messages are being displayed on a physical
				// segmented display unit.  Use the 1990-91 era Williams machines
				// (e.g., Funhouse or Whirlwind) as the reference for the default 
				// display size; these had two lines of 16 cells.  But we'll
				// increase the width and/or height from there if any message
				// groups require more, to make sure everything fits.
				std::unique_ptr<Gdiplus::Bitmap> alphanumImage;
				int alphaGridWid = 16, alphaGridHt = 2;
				if (_tcsicmp(style.c_str(), _T("alpha")) == 0)
				{
					static const struct
					{
						COLORREF color;
						int imageId;
					} colors[] = {
						{ RGB(255, 88, 32), IDB_ALPHANUM_AMBER },
					{ RGB(255, 0, 0), IDB_ALPHANUM_RED },
					{ RGB(0, 255, 0), IDB_ALPHANUM_GREEN },
					{ RGB(0, 0, 255), IDB_ALPHANUM_BLUE },
					{ RGB(255, 255, 0), IDB_ALPHANUM_YELLOW },
					{ RGB(255, 0, 255), IDB_ALPHANUM_PURPLE },
					{ RGB(255, 255, 255), IDB_ALPHANUM_WHITE }
					};

					int dMin = 1000000;
					int alphanumImageId = IDB_ALPHANUM_AMBER;
					for (size_t i = 0; i < countof(colors); ++i)
					{
						// figure the distance between this color and the desired text
						// color, in RGB vector space
						int dr = GetRValue(colors[i].color) - txtColor.rgbRed;
						int dg = GetGValue(colors[i].color) - txtColor.rgbGreen;
						int db = GetBValue(colors[i].color) - txtColor.rgbBlue;
						int d = dr * dr + dg * dg + db * db;

						// if this is the closest match so far, keep it
						if (d < dMin)
						{
							dMin = d;
							alphanumImageId = colors[i].imageId;
						}
					}

					// load the image we settled on
					alphanumImage.reset(GPBitmapFromPNG(alphanumImageId));

					// scan the group to determine the required grid size
					for (auto &group : messages)
					{
						// if this is the tallest message so far, remember it
						if ((int)group.size() > alphaGridHt)
							alphaGridHt = (int)group.size();

						// scan the group for the widest line
						for (auto s : group)
						{
							// if this is the widest line so far, remember it
							int wid = CountAlphaCells(s.c_str());
							if (wid > alphaGridWid)
								alphaGridWid = wid;
						}
					}
				}

				// load the background image for typewriter mode, if applicable
				std::unique_ptr<Gdiplus::Bitmap> ttBkgImage;
				if (_tcsicmp(style.c_str(), _T("tt")) == 0)
				{
					ttBkgImage.reset(GPBitmapFromPNG(IDB_INDEX_CARD));
				}

				// generate the graphics for each text group
				std::list<HighScoreImage> images;
				for (auto &group : messages)
				{
					// note the number of lines in this message
					int nLines = (int)group.size();

					// draw into the image, creating a new DIB of the given size for it
					auto DrawToImage = [&images](int width, int height, std::function<void(Gdiplus::Graphics&)> drawFunc)
					{
						// emplace a new high-score image in the list
						images.emplace_back(HighScoreImage::NormalSpriteType, 3500);
						HighScoreImage &image = images.back();

						// draw the image into a new DIB through the callback
						DrawOffScreen(&image.hbmp, width, height, [&image, &drawFunc]
						    (HDC hdc, HBITMAP, const void *dibits, const BITMAPINFO &bmi)
						{
							// save the bitmap data to the image object
							image.dibits = dibits;
							memcpy(&image.bmi, &bmi, sizeof(bmi));

							// set up the GDI+ context
							Gdiplus::Graphics g(hdc);

							// do the caller's drawing
							drawFunc(g);

							// flush the bitmap
							g.Flush();
						});
					};

					// create a graphic according to the style
					if (_tcsicmp(style.c_str(), _T("alpha")) == 0)
					{
						// Alphanumeric segmented display style

						// Figure the pixel size required for the generated image.  The 
						// image will consist of alphaGridHt x alphaGridWid character cells,
						// plus margins and vertical padding between lines.  The character
						// cells are of fixed size; we can determine the size of a cell from
						// the size of the 'alphanumImage' PNG, which is laid out in a 16x8
						// (col x row) grid.
						//
						// The margins and line spacing depend on the number of lines:
						//
						// - For a 2-line image, draw with 1/2 line of spacing top and bottom,
						//   and 1/2 line of spacing between the two rows
						//
						// - For a 3-line image, draw with 1/2 line of spacing top and bottom
						//   and 1/4 line between rows
						//
						// - For a 4-line image, draw with 1/4 line of spacing top and bottom
						//   and 1/4 line between rows
						//
						const int charCellWid = alphanumImage.get()->GetWidth() / 16;
						const int charCellHt = alphanumImage.get()->GetHeight() / 8;
						int yPadding = alphaGridHt <= 2 ? charCellHt / 2 : charCellHt / 4;
						int yMargin = alphaGridHt <= 2 ? charCellHt / 2 : charCellHt / 4;
						int pixWid = alphaGridWid * charCellWid;
						int pixHt = alphaGridHt * charCellHt + 2 * yMargin + yPadding * (alphaGridHt - 1);

						// figure the top left cell position with these margins
						int x0 = 0, y0 = yMargin;

						// Pad this out to a 4:1 aspect ratio.  The video DMD display window
						// is usually sized roughly 4:1 to match the proportions of real
						// pinball DMDs from the 1990s, which were mostly 128x32.  The
						// renderer will scale our image to the actual display size, so we
						// don't have to match the exact size or proportions, but the result
						// will look better if the image proportions are close to the display
						// proportions, since that will cause less geometric distortion.
						float aspect = (float)pixWid / (float)pixHt;
						if (aspect > 4.0f)
						{
							y0 += (pixWid / 4 - pixHt) / 2;
							pixHt = pixWid / 4;
						}
						else if (aspect < 4.0f)
						{
							x0 += (pixHt * 4 - pixWid) / 2;
							pixWid = pixHt * 4;
						}

						// create the image
						DrawToImage(pixWid, pixHt, [&alphanumImage, &group,
							pixWid, pixHt, charCellWid, charCellHt, alphaGridWid, alphaGridHt,
							x0, y0, yPadding, &CountAlphaCells]
							(Gdiplus::Graphics &g)
						{
							// fill the background with black
							Gdiplus::SolidBrush bkg(Gdiplus::Color(0, 0, 0));
							g.FillRectangle(&bkg, 0, 0, charCellWid, charCellHt);

							// center it vertically
							int y = y0;
							int blankLines = alphaGridHt - (int)group.size();
							int blankTopLines = blankLines / 2;

							// draw each line
							auto s = group.begin();
							for (int line = 0; line < alphaGridHt; ++line)
							{
								// get the next item, if available, otherwise show a blank line
								const TCHAR *txt = _T("");
								if (line >= blankTopLines && s != group.end())
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
								auto DrawSpaces = [&x, &alphanumImage, &g, &y, charCellWid, charCellHt](int n)
								{
									for (int i = 0; i < n; ++i)
									{
										// draw a space (code point 32 - grid row 2, column 0)
										g.DrawImage(
											alphanumImage.get(),
											Gdiplus::RectF((float)x, (float)y, (float)charCellWid, (float)charCellHt),
											0.0f, 2.0f*charCellHt, (float)charCellWid, (float)charCellHt,
											Gdiplus::UnitPixel);

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
									// Figure the cell coordinates in the image.  The image
									// is a 16x8 grid arranged in Unicode order.  Note that
									// only the first 128 code points (the basic ASCII set)
									// are present; replacing anything else with '*'.
									if (c > 127)
										c = '*';
									int cellx = (c % 16) * charCellWid;
									int celly = (c / 16) * charCellHt;

									// draw the character
									g.DrawImage(
										alphanumImage.get(),
										Gdiplus::RectF((float)x, (float)y, (float)charCellWid, (float)charCellHt),
										(float)cellx, (float)celly, (float)charCellWid, (float)charCellHt,
										Gdiplus::UnitPixel);

									// advance to the next character
									prvChar = c;
									c = *++p;

									// advance to the next character cell, unless this is a
									// non-advancing character
									if (!((c == ',' || c == '.') && !(prvChar == ',' || prvChar == '.')))
										x += charCellWid;
								}

								// draw the right spaces
								DrawSpaces(rightSpaces);

								// advance to the next line
								y += charCellHt + yPadding;
							}
						});
					}
					else if (_tcsicmp(style.c_str(), _T("tt")) == 0)
					{
						// typewriter style

						// size the image to match the background
						int wid = ttBkgImage.get()->GetWidth();
						int ht = ttBkgImage.get()->GetHeight();

						// draw the image
						DrawToImage(wid, ht, [&group, wid, ht, &ttBkgImage](Gdiplus::Graphics &g)
						{
							// copy the background
							g.DrawImage(ttBkgImage.get(), 0, 0, wid, ht);

							// get the font
							std::unique_ptr<Gdiplus::Font> font(CreateGPFontPixHt(_T("Courier New"), ht / 8, 400));

							// combine the text into a single string separated by line breaks
							TSTRING txt;
							for (auto s : group)
							{
								if (txt.length() != 0)
									txt += _T("\n");
								txt += s.c_str();
							}

							// draw it centered horizontally and vertically
							Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
							fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
							fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
							Gdiplus::SolidBrush br(Gdiplus::Color(32, 32, 32));
							g.DrawString(txt.c_str(), -1, font.get(), Gdiplus::RectF(0, 0, (float)wid, (float)ht), &fmt, &br);
						});
					}
					else
					{
						// DMD style (this is also the default if the style setting
						// isn't recognized)

						// create the DIB buffer at 4 bytes per pixel
						BYTE *pix = new BYTE[dmdWidth*dmdHeight * 4];

						// clear the buffer to the background color
						BYTE *dst = pix;
						for (int i = 0; i < dmdWidth*dmdHeight; ++i, dst += 4)
							memcpy(dst, &colors[0], 4);

						// pick the font
						const DMDFont *font = PickHighScoreFont(group);

						// figure the starting y offset, centering the text overall vertically
						int totalTextHeight = font->cellHeight * nLines;
						int y = (dmdHeight - totalTextHeight) / 2;

						// draw each string
						for (auto s : group)
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
						images.emplace_back(HighScoreImage::DMDSpriteType, bmi, pix, 3500);
					}
				}

				// Send the sprite list back to the window
				if (auto view = Application::Get()->GetDMDView(); view != nullptr)
					view->SendMessage(DMVMsgHighScoreImage, seqno, reinterpret_cast<LPARAM>(&images));

				// count the thread exist in the view object
				InterlockedDecrement(&view->nHighScoreThreads);

				// delete 'self'
				delete this;

				// done (thread return code isn't used)
				return 0;
			}
		};
		ThreadInfo *ti = new ThreadInfo(this, highScoreRequestSeqNo, txtColor, style);
		
		// capture the message list to the thread
		game->DispHighScoreGroups([&ti, &style](const std::list<const TSTRING*> &group)
		{
			if (_tcsicmp(style, _T("alpha")) == 0 && group.size() > 2)
			{
				// We're in alphanumeric mode, so limit messages to two
				// lines.  For an odd number of lines, add a one liner
				// first, then add pairs.  Otherwise just add pairs.
				auto it = group.begin();
				auto Add = [&it, &ti](int nLines)
				{
					// add a message group
					auto &list = ti->messages.emplace_back();
					auto AddLine = [&list](const TSTRING *s) { list.emplace_back(*s); };

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
				auto &list = ti->messages.emplace_back();
				for (auto s : group)
					list.emplace_back(*s);
			}
		});

		// count the thread
		InterlockedIncrement(&nHighScoreThreads);

		// launch the thread
		DWORD tid;
		HandleHolder hThread = CreateThread(NULL, 0, &ThreadInfo::SMain, ti, 0, &tid);
		if (hThread == nullptr)
		{
			// we couldn't launch the thread, so do the work inline instead
			ti->Main();
		}
	}
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
	case DMVMsgHighScoreImage:
		SetHighScoreImages((DWORD)wParam, reinterpret_cast<std::list<HighScoreImage> *>(lParam));
		return true;
	}

	// it's not one of ours - inherit the default handling
	return __super::OnUserMessage(msg, wParam, lParam);
}

void DMDView::SetHighScoreImages(DWORD seqno, std::list<HighScoreImage> *images)
{
	// If the sequence number matches the current request, install
	// this list of images
	if (seqno == highScoreRequestSeqNo)
	{
		// transfer the images to our high score list
		for (auto &i : *images)
			highScoreImages.emplace_back(i);

		// If there's only one item in the list, display it for longer than
		// the default, which assumes that it's only one of several items.
		if (highScoreImages.size() == 1)
			highScoreImages.front().displayTime += 2000;

		// set up at the end of the high score list, to indicate that we're
		// not currently showing one of these images
		highScorePos = highScoreImages.end();

		// set a timer to start the slide show
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

const DMDFont *DMDView::PickHighScoreFont(const std::list<const TSTRING*> &group)
{
	// font list, in descending size order
	static const DMDFont *fonts[] = {
		&DMDFonts::Font_CC_20px_az,
		&DMDFonts::Font_CC_15px_az,
		&DMDFonts::Font_CC_12px_az,
		&DMDFonts::Font_CC_9px_az,
		&DMDFonts::Font_CC_7px_az,
		&DMDFonts::Font_CC_5px_AZ
	};

	// start with the largest font that will fit the vertical space
	int nLines = (int)group.size();
	int fontIndex = 0;
	const DMDFont *font = fonts[fontIndex];
	while (fontIndex + 1 < countof(fonts))
	{
		// if it fits vertically, we can stop here
		if (nLines * font->cellHeight <= dmdHeight)
			break;

		// go to the next font
		font = fonts[++fontIndex];
	}

	// now downsize the font if necessary to fit the longest line horizontally
	while (fontIndex + 1 < countof(fonts))
	{
		// find the widest line
		int maxWid = 0;
		for (auto it = group.begin(); it != group.end(); ++it)
		{
			SIZE sz = font->MeasureString((*it)->c_str());
			if (sz.cx > maxWid)
				maxWid = sz.cx;
		}

		// if it fits, we can stop
		if (maxWid <= dmdWidth)
			break;

		// get the next font
		font = fonts[++fontIndex];
	}

	// return the selected font
	return font;
}

bool DMDView::OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	// If we're looping the video, check for high score images: if
	// present, start a slide show of the high score images instead
	// of going directly to a replay of the video.  If a game is
	// currently running, skip the score display and just loop the
	// video - we suppress score display while running.
	if (msg == AVPMsgLoopNeeded && highScoreImages.size() != 0 && !Application::Get()->IsGameRunning())
	{
		// stop the video
		if (currentBackground.sprite != nullptr && currentBackground.sprite->IsVideo())
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
		if (highScorePos->sprite == nullptr)
		{
			// try creating the sprite
			highScorePos->sprite.Attach(highScorePos->spriteType == HighScoreImage::DMDSpriteType ? new DMDSprite() : new Sprite());
			if (!highScorePos->sprite->Load(highScorePos->bmi, highScorePos->dibits, SilentErrorHandler(), _T("high score slide")))
			{
				// failed to create the sprite
				highScorePos->sprite = nullptr;
			}
		}

		// add it to the sprite list
		if (highScorePos->sprite != nullptr)
			sprites.push_back(highScorePos->sprite);
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
		ScaleSprite(i.sprite, 1.0f, false);
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
		// Restart the video
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
