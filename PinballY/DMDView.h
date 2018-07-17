// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// DMD view window
// This is a child window that serves as the D3D drawing surface for
// the DMD window.

#pragma once

#include "../Utilities/Config.h"
#include "D3D.h"
#include "D3DWin.h"
#include "D3DView.h"
#include "Camera.h"
#include "TextDraw.h"
#include "PerfMon.h"
#include "BaseView.h"
#include "SecondaryView.h"
#include "BorderlessSecondaryView.h"

class Sprite;
class VideoSprite;
class GameListItem;
class DMDFont;

class DMDView : public BorderlessSecondaryView
{
public:
	DMDView();

	// clear media
	virtual void ClearMedia() override;

	// receive a high score update
	void OnUpdateHighScores(GameListItem *game);

	// pick a font for a generated high score screen
	static const DMDFont *PickHighScoreFont(const std::list<const TSTRING*> &group);

protected:
	// private application message (WM_APP to 0xBFFF)
	virtual bool OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

	// timers
	virtual bool OnTimer(WPARAM timer, LPARAM callback) override;

	// timer IDs
	static const int StartHighScoreTimerID = 200;  // start the high score slide show
	static const int NextHighScoreTimerID = 201;   // advance to next high score image

	// get the next window for game syncing
	virtual UINT GetNextWindowSyncCommand() const override { return ID_SYNC_TOPPER; }

	// Get the background media info
	virtual const MediaType *GetBackgroundImageType() const override;
	virtual const MediaType *GetBackgroundVideoType() const override;
	virtual const TCHAR *GetDefaultBackgroundImage() const override;

	// handle a change of background image
	virtual void OnChangeBackgroundImage() override;

	// add the main background image to the drawing list
	virtual void AddBackgroundToDrawingList();

	// scale sprites
	virtual void ScaleSprites() override;

	// generate high score images for the current game
	void GenerateHighScoreImages();

	// clear out the high score images
	void ClearHighScoreImages();

	// start the high score slideshow
	void StartHighScorePlayback();

	// high-score graphics list
	struct HighScoreImage
	{
		HighScoreImage(Sprite *sprite, DWORD t) : sprite(sprite), displayTime(t) { }

		// image for this item
		RefPtr<Sprite> sprite;

		// time in milliseconds to display this item
		DWORD displayTime;
	};
	std::list<HighScoreImage> highScoreImages;

	// Current display position in high score image list.  This
	// is ignored when the high score image list is empty.  When
	// it's non-empty, this points to the current image being
	// displayed.
	decltype(highScoreImages)::const_iterator highScorePos;
};
