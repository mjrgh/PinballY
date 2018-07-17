// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// DMD view

#include "stdafx.h"
#include "../Utilities/Config.h"
#include "DMDView.h"
#include "Resource.h"
#include "D3D.h"
#include "D3DWin.h"
#include "GraphicsUtil.h"
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
DMDView::DMDView() : BorderlessSecondaryView(IDR_DMD_CONTEXT_MENU, ConfigVars::DMDWinVarPrefix)
{
	highScorePos = highScoreImages.end();
}

// get the background media info
const MediaType *DMDView::GetBackgroundImageType() const { return &GameListItem::dmdImageType; }
const MediaType *DMDView::GetBackgroundVideoType() const { return &GameListItem::dmdVideoType; }
const TCHAR *DMDView::GetDefaultBackgroundImage() const { return _T("assets\\DefaultDMD.png"); }

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

	// if a game is active, and it has high scores, generate graphics
	if (auto game = currentBackground.game; game != nullptr && game->highScores.size() != 0)
	{
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
		
		// Figure the background color, using the text color at reduced
		// brightness.  This helps simulate the visible pixel structure of 
		// a real DMD by showing a little of the text color even in pixels
		// that are fully "off".
		RGBQUAD bgColor = { (BYTE)(txtColor.rgbBlue/10), (BYTE)(txtColor.rgbGreen/10), (BYTE)(txtColor.rgbRed/10) };

		// Set up the 128x32 32bpp pixel array buffer
		static const int dmdBytes = dmdWidth * dmdHeight * 4;
		BYTE pix[dmdBytes];

		// Set up a DIB descriptor for the 32bpp bitmap.  We'll use this
		// to create the D3D texture for the sprite.
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
				bgColor.rgbRed + redSpan*i/15,
				bgColor.rgbGreen + greenSpan*i/15,
				bgColor.rgbBlue + blueSpan*i/15);
		}

		// generate the graphics for each text group
		game->DispHighScoreGroups([this, &pix, &bmi, &colors](const std::list<const TSTRING*> &group)
		{
			// clear the buffer to the background color
			BYTE *dst = pix;
			for (int i = 0; i < dmdWidth*dmdHeight; ++i, dst += 4)
				memcpy(dst, &colors[0], 4);

			// pick the font
			const DMDFont *font = PickHighScoreFont(group);

			// figure the starting y offset, centering the text overall vertically
			int nLines = group.size();
			int totalTextHeight = font->cellHeight * nLines;
			int y = (dmdHeight - totalTextHeight)/2;

			// draw each string
			for (auto it = group.begin(); it != group.end(); ++it)
			{
				// measure the string
				const TCHAR *str = (*it)->c_str();
				SIZE sz = font->MeasureString(str);

				// draw it centered horizontally
				font->DrawString32(str, pix, (dmdWidth - sz.cx)/2, y, colors);

				// advance to the next line
				y += font->cellHeight;
			}

			// create the sprite
			RefPtr<Sprite> sprite(new DMDSprite());
			SilentErrorHandler eh;
			sprite->Load(bmi, pix, eh, _T("DMD high score graphics"));

			// add it to the list, handing over our reference on the sprite
			highScoreImages.emplace_back(sprite.Detach(), nLines == 1 ? 2500 : 3500);
		});

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
	int nLines = group.size();
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
	// of going directly to a replay of the video.
	if (msg == AVPMsgLoopNeeded && highScoreImages.size() != 0)
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
		sprites.push_back(highScorePos->sprite);
	else
		__super::AddBackgroundToDrawingList();
}

void DMDView::ScaleSprites()
{
	// do the base class work
	__super::ScaleSprites();

	// scale the high score images
	for (auto &i : highScoreImages)
		ScaleSprite(i.sprite, 1.0f, false);
}
