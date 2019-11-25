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

class Sprite;
class VideoSprite;
class GameListItem;
class DMDFont;

class DMDView : public SecondaryView
{
public:
	DMDView();

	// clear media
	virtual void ClearMedia() override;

	// receive a high score update
	void OnUpdateHighScores(GameListItem *game);

	// pick a font for a generated high score screen
	static const DMDFont *PickHighScoreFont(const std::list<TSTRING> &group);
	static const DMDFont *PickHighScoreFont(const std::list<const TSTRING*> &group);

	// wait for high score image generator threads to exit
	void WaitForHighScoreThreads(DWORD timeout = INFINITE);

	// enter/exit running game mode
	virtual void BeginRunningGameMode(GameListItem *game, GameSystem *system, bool &hasVideo) override;
	virtual void EndRunningGameMode() override;

protected:
	// private application message (WM_APP to 0xBFFF)
	virtual bool OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

	// private window mesage
	virtual bool OnUserMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

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
	virtual const TCHAR *GetDefaultBackgroundImage() const override { return _T("Default DMD"); }
	virtual const TCHAR *GetDefaultBackgroundVideo() const override { return _T("Default DMD"); }
	virtual const TCHAR *GetDefaultSystemImage() const override { return _T("Default Images\\No DMD"); }
	virtual const TCHAR *GetDefaultSystemVideo() const override { return _T("Default Videos\\No DMD"); }
	virtual const TCHAR *StartupVideoName() const override { return _T("Startup Video (dmd)"); }

	// "show when running" window ID
	virtual const TCHAR *ShowWhenRunningWindowId() const override { return _T("dmd"); }

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
		// sprite type, for deferred sprite creation from a bitmap
		enum SpriteType
		{
			NoSpriteType,
			NormalSpriteType,
			DMDSpriteType
		};

		HighScoreImage(SpriteType spriteType, DWORD t) :
			spriteType(spriteType), dibits(nullptr), displayTime(t)
		{ }

		HighScoreImage(Sprite *sprite, DWORD t) : 
			spriteType(NoSpriteType), sprite(sprite), dibits(nullptr), displayTime(t)
		{ }

		HighScoreImage(SpriteType spriteType, const BITMAPINFO &bmi, BYTE *dibits, DWORD t) :
			spriteType(spriteType), dibits(dibits), displayTime(t)
		{
			memcpy(&this->bmi, &bmi, sizeof(this->bmi));
		}

		HighScoreImage(SpriteType spriteType, HBITMAP hbmp, const BITMAPINFO &bmi, const void *dibits, DWORD t) :
			spriteType(spriteType), hbmp(hbmp), dibits(dibits), displayTime(t)
		{
			memcpy(&this->bmi, &bmi, sizeof(this->bmi));
		}

		// transfer ownership of resources from another HighScoreImage object
		HighScoreImage(HighScoreImage &i) :
			spriteType(i.spriteType),
			sprite(i.sprite.Detach()), 
			hbmp(i.hbmp.Detach()), 
			dibits(i.dibits), 
			displayTime(i.displayTime)
		{
			// copy the bitmap info
			memcpy(&this->bmi, &i.bmi, sizeof(this->bmi));

			// we've taken ownership of the DIbits - clear it in the source
			i.dibits = nullptr;
		}

		~HighScoreImage()
		{
			// If we have a dibits array but no bitmap handle, the 
			// dibits array was separately allocated and we're
			// responsible for cleaning it up.  If there's a bitmap
			// handle, the dibits are owned by the bitmap and don't
			// need to be separately deleted.
			if (hbmp == NULL && dibits != nullptr)
				delete[] dibits;
		}

		// for deferred sprite creation, the type of Sprite object to create
		SpriteType spriteType;

		// image for this item
		RefPtr<Sprite> sprite;

		// We create the images in a background thread, staging them
		// initially to a DIB for later conversion to a D3D image in
		// the main thread.  The DIB information is saved here until
		// the renderer needs to display the image, at which point
		// it's converted into a sprite.
		HBITMAPHolder hbmp;
		BITMAPINFO bmi;
		const void *dibits;

		// time in milliseconds to display this item
		DWORD displayTime;
	};
	std::list<HighScoreImage> highScoreImages;

	// Set the high score image list.  When we switch to a new game, we kick
	// off a thread to generate the high score images.  We use a thread rather
	// than generating them on the main thread, because it can take long enough
	// to cause a noticeable delay in the UI if done on the main thread.  The
	// sequence number lets us determine if this is the most recent request;
	// we'll simply discard results from an older request.
	void SetHighScoreImages(DWORD seqno, std::list<HighScoreImage> *images);

	// Last high score request sequence number
	DWORD highScoreRequestSeqNo;

	// Number of outstanding high score image generator threads
	volatile DWORD nHighScoreThreads;

	// Current display position in high score image list.  This
	// is ignored when the high score image list is empty.  When
	// it's non-empty, this points to the current image being
	// displayed.
	decltype(highScoreImages)::iterator highScorePos;
};
