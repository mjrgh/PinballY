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
#include "MouseButtons.h"
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
	// if we don't load any media, sync the next window before we leave
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
	} syncer(this);

	// do nothing if minimized or hidden
	if (!IsWindowVisible(hWnd) || IsIconic(hWnd))
		return;

	// get the currently selected game, if any
	GameListItem *game = GameList::Get()->GetNthGame(0);

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

void SecondaryView::BeginRunningGameMode()
{
	// Clear the sprites, to free up video memory and stop any video
	// playback.  Note that we don't need any transition effects, as
	// we just hid the window entirely.
	ClearMedia();
}

void SecondaryView::EndRunningGameMode()
{
	// send a Restore Visibility command to the parent frame
	::SendMessage(GetParent(hWnd), WM_COMMAND, ID_RESTORE_VISIBILITY, 0);
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

bool SecondaryView::IsBorderlessWindowMode(HWND parent)
{
	return parent != NULL
		&& ::SendMessage(parent, PWM_ISBORDERLESS, 0, 0) != 0
		&& ::SendMessage(parent, PWM_ISFULLSCREEN, 0, 0) == 0;
}

bool SecondaryView::OnNCHitTest(POINT pt, UINT &hit)
{
	// If the parent is borderless and not in full-screen mode, simulate
	// sizing borders around the perimeter of the client area.
	if (HWND parent = GetParent(hWnd); IsBorderlessWindowMode(parent))
	{
		// If it's within the sizing border with of an edge, let 
		// the parent window handle it.  This gives us an invisible
		// sizing border that acts like a normal sizing border, but
		// is covered out to the edge by the DMD contents.

		// figure the sizing border area of the parent, based on its
		// window style, but excluding the caption area
		RECT rcFrame = { 0, 0, 0, 0 };
		DWORD dwStyle = GetWindowLong(parent, GWL_STYLE);
		DWORD dwExStyle = GetWindowLong(parent, GWL_EXSTYLE);
		AdjustWindowRectEx(&rcFrame, dwStyle & ~WS_CAPTION, FALSE, dwExStyle);

		// get my window rect 
		RECT rcWindow;
		GetWindowRect(hWnd, &rcWindow);

		// check if we're in the sizing border
		if ((pt.x >= rcWindow.left && pt.x < rcWindow.left - rcFrame.left)
			|| (pt.x < rcWindow.right && pt.x >= rcWindow.right - rcFrame.right)
			|| (pt.y >= rcWindow.top && pt.y < rcWindow.top - rcFrame.top)
			|| (pt.y < rcWindow.bottom && pt.y >= rcWindow.bottom - rcFrame.bottom))
		{
			// it's in the sizing border - let the parent window handle it
			hit = HTTRANSPARENT;
			return true;
		}
	}

	// inherit the default handling
	return __super::OnNCHitTest(pt, hit);
}

bool SecondaryView::OnMouseMove(POINT pt)
{
	// check if we're in a left-button drag operation
	if (dragButton == MouseButton::mbLeft)
	{
		// If the parent is borderless and not in full-scree mode, 
		if (HWND parent = GetParent(hWnd); parent != NULL && IsBorderlessWindowMode(parent))
		{
			// Get the delta from the initial position
			POINT delta;
			delta.x = pt.x - dragPos.x;
			delta.y = pt.y - dragPos.y;

			// move the parent window by the drag position
			RECT rc;
			GetWindowRect(parent, &rc);
			SetWindowPos(parent, 0, rc.left + delta.x, rc.top + delta.y, -1, -1, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);

			// Note that we don't need to update the drag position, as it's
			// relative to the client area, and we're moving the client area
			// in sync with each mouse position change.  That means that the
			// relative mouse never changes.
			return true;
		}
	}

	// use the default handling 
	return __super::OnMouseMove(pt);
}
