// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Secondary view

#include "stdafx.h"
#include <d3d11_1.h>
#include <gdiplus.h>
#include <DirectXMath.h>
#include "../Utilities/Config.h"
#include "SecondaryView.h"
#include "Resource.h"
#include "D3D.h"
#include "D3DWin.h"
#include "GraphicsUtil.h"
#include "Camera.h"
#include "TextDraw.h"
#include "VideoSprite.h"
#include "GameList.h"
#include "Application.h"
#include "PlayfieldView.h"
#include "MediaDropTarget.h"

using namespace DirectX;

// construction
SecondaryView::SecondaryView(int contextMenuId, const TCHAR *winConfigVarPrefix)
	: BaseView(contextMenuId, winConfigVarPrefix),
	backgroundLoader(this)
{
}

void SecondaryView::GetMediaFiles(const GameListItem *game,
	TSTRING &video, TSTRING &image, TSTRING &defaultImage)
{
	// if we have a background image type, look for a matching file
	if (auto m = GetBackgroundImageType(); m != nullptr)
		game->GetMediaItem(image, *m);

	// if we have a background video type, look for a matching file
	if (auto m = GetBackgroundVideoType(); m != nullptr)
		game->GetMediaItem(video, *m);

	// get our default image name
	TCHAR buf[MAX_PATH];
	GetDeployedFilePath(buf, GetDefaultBackgroundImage(), _T(""));
	defaultImage = buf;
}

void SecondaryView::UpdateDrawingList()
{
	// clear the list
	sprites.clear();

	// add the background images to the list
	AddBackgroundToDrawingList();
	if (incomingBackground.sprite != 0)
		sprites.push_back(incomingBackground.sprite);

	// add the video overlay
	if (videoOverlay != nullptr)
		sprites.push_back(videoOverlay);

	// add the drop effect overlay
	if (dropTargetSprite != nullptr)
		sprites.push_back(dropTargetSprite);

	// update sprite scaling
	ScaleSprites();
}

void SecondaryView::AddBackgroundToDrawingList()
{
	if (currentBackground.sprite != 0)
		sprites.push_back(currentBackground.sprite);
}

// update sprite scaling
void SecondaryView::ScaleSprites()
{
	// stretch the topper images to exactly fill the window
	ScaleSprite(currentBackground.sprite, 1.0f, false);
	ScaleSprite(incomingBackground.sprite, 1.0f, false);
	ScaleSprite(dropTargetSprite, 1.0f, true);
}

void SecondaryView::UpdateMenu(HMENU hMenu, BaseWin *fromWin)
{
	// update base class items
	__super::UpdateMenu(hMenu, fromWin);

	// update frame items via the parent
	HWND hwndParent = GetParent(hWnd);
	if (fromWin != 0 && fromWin->GetHWnd() != hwndParent)
		::SendMessage(hwndParent, BWMsgUpdateMenu, (WPARAM)hMenu, (LPARAM)this);
}

bool SecondaryView::OnCommand(int cmd, int src, HWND hwndControl)
{
	switch (cmd)
	{
	case ID_SYNC_GAME:
		SyncCurrentGame();
		return true;
	}

	// not handled
	return __super::OnCommand(cmd, src, hwndControl);
}

bool SecondaryView::OnTimer(WPARAM timer, LPARAM callback)
{
	// check for one of our timers
	switch (timer)
	{
	case animTimerID:
		// update our animation
		if (!UpdateAnimation())
		{
			// the animation is no longer running - kill the timer
			KillTimer(hWnd, animTimerID);
		}
		return true;
	}

	// not handled - inherit the default handling
	return __super::OnTimer(timer, callback);
}

bool SecondaryView::UpdateAnimation()
{
	// if we have an incoming backglass, fade it in
	if (incomingBackground.sprite != nullptr && incomingBackground.sprite->IsFadeDone())
	{
		// done - make the new background current
		currentBackground = incomingBackground;
		incomingBackground.sprite = nullptr;
		incomingBackground.game = nullptr;

		// update the drawing list for the change in sprites
		UpdateDrawingList();

		// carry out any side effects of the change
		OnChangeBackgroundImage();

		// sync the next window in the daisy chain
		SyncNextWindow();
	}

	// we're still running if there's still a sprite to fade
	return incomingBackground.sprite != nullptr;
}

void SecondaryView::SyncNextWindow()
{
	if (UINT cmd = GetNextWindowSyncCommand(); cmd != 0)
	{
		if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != nullptr)
			pfv->PostMessage(WM_COMMAND, cmd);
	}
}


void SecondaryView::OnShowHideFrameWindow(bool show)
{
	if (show)
	{
		// showing the window - sync the game display
		SyncCurrentGame();
	}
	else
	{
		// hiding - remove the backglass
		currentBackground.Clear();
		incomingBackground.Clear();
		UpdateDrawingList();

		// handle the change
		OnChangeBackgroundImage();
	}
}

void SecondaryView::SyncCurrentGame()
{
	// make sure we synchronize the next window no matter how we exit
	class Syncer
	{
	public:
		Syncer(SecondaryView *self) : self(self), loadedMedia(false) { }
		~Syncer()
		{
			if (!loadedMedia)
				self->SyncNextWindow();
		}

		SecondaryView *self;
		bool loadedMedia;
	};
	Syncer syncer(this);

	// do nothing if minimized or hidden
	if (!IsWindowVisible(hWnd) || IsIconic(hWnd))
		return;

	// Get the game to display, according to the current mode
	GameListItem *game = nullptr;
	if (Application::Get()->IsGameRunning())
	{
		// Running game mode.  Show media for the running game,
		// but only if the game is specifically designated for
		// media display in this window.
		game = GameList::Get()->GetByInternalID(Application::Get()->GetRunningGameId());
		if (!ShowMediaWhenRunning(game))
			game = nullptr;
	}
	else
	{
		// normal wheel mode - show the currently selected game
		game = GameList::Get()->GetNthGame(0);
	}

	// do nothing if there's no game
	if (game == nullptr)
		return;

	// if the new game is already the incoming game, just let that 
	// animation finish
	if (incomingBackground.sprite != nullptr && game == incomingBackground.game)
		return;

	// if there's no incoming game, and the new game is already the
	// current game, there's nothing to change
	if (incomingBackground.sprite == nullptr
		&& currentBackground.sprite != nullptr && currentBackground.game == game)
		return;

	// get the media files
	TSTRING video, image, defaultImage;
	GetMediaFiles(game, video, image, defaultImage);

	// set up to load the sprite asynchronously
	HWND hWnd = this->hWnd;
	SIZE szLayout = this->szLayout;
	auto load = [hWnd, video, image, defaultImage, szLayout](VideoSprite *sprite)
	{
		// start at zero alpha, for the cross-fade
		sprite->alpha = 0;

		// try the video first, unless videos are disabled
		Application::AsyncErrorHandler eh;
		bool ok = false;
		if (video.length() != 0 && Application::Get()->IsEnableVideo())
			ok = sprite->LoadVideo(video.c_str(), hWnd, { 1.0f, 1.0f }, eh, _T("Background Video"));

		// try the image if that didn't work
		if (!ok && image.length() != 0)
			ok = sprite->Load(image.c_str(), { 1.0f, 1.0f }, szLayout, eh);

		// load a default image if we didn't load anything custom
		if (!ok)
			sprite->Load(defaultImage.c_str(), { 1.0f, 1.0f }, szLayout, eh);
	};

	auto done = [this, game](VideoSprite *sprite)
	{
		// set the new sprite
		incomingBackground.sprite = sprite;
		incomingBackground.game = game;

		// update the drawing list for the change in sprites
		UpdateDrawingList();

		// start the fade timer, unless we have a video that's still loading
		if (sprite->GetVideoPlayer() == nullptr || sprite->GetVideoPlayer()->IsFrameReady())
			StartBackgroundCrossfade();
	};

	backgroundLoader.AsyncLoad(false, load, done);
	syncer.loadedMedia = true;
}

void SecondaryView::StartBackgroundCrossfade()
{
	// set up the crossfade
	DWORD crossFadeTime = 120;
	SetTimer(hWnd, animTimerID, animTimerInterval, 0);
	incomingBackground.sprite->StartFade(1, crossFadeTime);
}

void SecondaryView::OnEnableVideos(bool enable)
{
	// Clear the background sprites (current and incoming), then reload.
	// If video is becoming disabled, we only need to reload if we actually
	// have a video currently showing.  If video is becoming enabled, reload
	// under all circumstances.
	bool reload = false;
	auto Check = [enable, &reload](decltype(incomingBackground) &item)
	{
		// we only need to reload this item if it has a sprite loaded
		if (item.sprite != nullptr)
		{
			// reload the item if we're newly enabling video, or the item
			// is currently showing a video
			if (enable || item.sprite->GetVideoPlayer() != nullptr)
			{
				// enabling - reload unconditionally
				item.Clear();
				reload = true;
			}
		}
	};
	Check(currentBackground);
	Check(incomingBackground);

	// if necessary, reload
	if (reload)
		SyncCurrentGame();
}

void SecondaryView::ClearMedia()
{
	incomingBackground.Clear();
	currentBackground.Clear();
	OnChangeBackgroundImage();
	UpdateDrawingList();
}

void SecondaryView::BeginRunningGameMode(GameListItem *game)
{
	// Check if we're set to continue showing this window's media
	// while this game is running.
	if (ShowMediaWhenRunning(game))
	{
		// We're showing this window's media while this game is
		// running.  Bring this window into the topmost layer so
		// that stays in front of any window the game itself tries
		// to open, as "show when running" overrides whatever the
		// game shows.
		SetWindowPos(GetParent(hWnd), HWND_TOPMOST, -1, -1, -1, -1,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
	}
	else
	{
		// Show nothing in this window while the game is running.
		// Explicitly clear any media currently being displayed, to
		// free up video memory and reduce CPU load.
		ClearMedia();
	}
}

bool SecondaryView::ShowMediaWhenRunning(GameListItem *game) const
{
	// make sure there's a game
	if (game != nullptr)
	{
		// get the Show When Running data
		if (const TCHAR *s = GameList::Get()->GetShowWhenRunning(game); s != nullptr)
		{
			// scan the list for our window ID
			const TCHAR * id = ShowWhenRunningWindowId();
			size_t idLen = _tcslen(id);
			while (*s != 0)
			{
				// find the end of the current token
				const TCHAR *nxt = s;
				for (; *nxt != 0 && *nxt != ' '; ++nxt);

				// check for a "negative" token, with a "-" prefix: e.g., "-dmd" means
				// that the DMD is explicitly not shown in this game
				bool tokSense = true;
				if (*s == '-')
				{
					tokSense = false;
					++s;
				}

				// compare this token - if it's a match, this window is indeed in the
				// Show When Running list, so keep showing the media
				if (_tcsnicmp(s, id, idLen) == 0)
					return tokSense;

				// advance to the start of the next token
				for (s = nxt; *s == ' '; ++s);
			}
		}
	}

	// we didn't find this window in the Show When Running, so the default is "no"
	return false;
}

void SecondaryView::EndRunningGameMode()
{
	// send a Restore Visibility command to the parent frame
	::SendMessage(GetParent(hWnd), WM_COMMAND, ID_RESTORE_VISIBILITY, 0);

	// remove myself from the topmost layer, if I'm there
	if ((GetWindowLong(GetParent(hWnd), GWL_EXSTYLE) & WS_EX_TOPMOST) != 0)
	{
		SetWindowPos(GetParent(hWnd), HWND_NOTOPMOST, -1, -1, -1, -1,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
	}
}

bool SecondaryView::OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case AVPMsgFirstFrameReady:
		// If this is the incoming background's video player, start the
		// cross-fade for the new background.
		if (incomingBackground.sprite != nullptr && incomingBackground.sprite->GetVideoPlayerCookie() == wParam)
			StartBackgroundCrossfade();
		break;
	}

	// inherit the default handling
	return __super::OnAppMessage(msg, wParam, lParam);
}

void SecondaryView::ShowContextMenu(POINT pt)
{
	// reset the screen saver timer
	if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != nullptr)
		pfv->ResetAttractMode();

	// use the normal handling
	__super::ShowContextMenu(pt);
}

