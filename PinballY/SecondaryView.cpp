// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Secondary view

#include "stdafx.h"
#include <d3d11_1.h>
#include <gdiplus.h>
#include <DirectXMath.h>
#include "../Utilities/Config.h"
#include "../Utilities/GraphicsUtil.h"
#include "SecondaryView.h"
#include "Resource.h"
#include "D3D.h"
#include "D3DWin.h"
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
	TSTRING &video, TSTRING &image, TSTRING &defaultVideo, TSTRING &defaultImage)
{
	// if we have a background image type, look for a matching file
	if (auto m = GetBackgroundImageType(); m != nullptr)
		GetBackgroundImageMedia(game, m, image);

	// if we have a background video type, look for a matching file
	if (auto m = GetBackgroundVideoType(); m != nullptr)
		GetBackgroundVideoMedia(game, m, video);

	// get our default video and image files
	if (auto gl = GameList::Get(); gl != nullptr)
	{
		TCHAR buf[MAX_PATH];

		const TCHAR * systemMediaDir = (system != nullptr ? game->system->mediaDir.c_str() : nullptr);

		if (systemMediaDir && gl->FindGlobalVideoFile(buf, systemMediaDir, GetDefaultSystemVideo()))
				defaultVideo = buf;
		else if (gl->FindGlobalVideoFile(buf, _T("Videos"), GetDefaultBackgroundVideo()))
			defaultVideo = buf;

		if (systemMediaDir && gl->FindGlobalImageFile(buf, systemMediaDir, GetDefaultSystemImage()))
			defaultImage = buf;
		else if (gl->FindGlobalImageFile(buf, _T("Images"), GetDefaultBackgroundImage()))
			defaultImage = buf;
	}
}

void SecondaryView::GetBackgroundImageMedia(const GameListItem *game, const MediaType *mtype, TSTRING &image)
{
	game->GetMediaItem(image, *mtype);
}

void SecondaryView::GetBackgroundVideoMedia(const GameListItem *game, const MediaType *mtype, TSTRING &video)
{
	game->GetMediaItem(video, *mtype);
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
	auto gl = GameList::Get();
	GameListItem *game = nullptr;
	if (Application::Get()->IsGameRunning())
	{
		// Running game mode.  Show media for the running game,
		// but only if the game is specifically designated for
		// media display in this window.
		game = gl->GetByInternalID(Application::Get()->GetRunningGameId());
		auto system = gl->GetSystem(Application::Get()->GetRunningGameSystem());
		if (!ShowMediaWhenRunning(game, system))
			game = nullptr;
	}
	else
	{
		// normal wheel mode - show the currently selected game
		game = gl->GetNthGame(0);
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

	// get the audio volume for the game
	int volPct = gl->GetAudioVolume(game);

	// combine it with the global video volume setting
	volPct = volPct * Application::Get()->GetVideoVolume() / 100;

	// get the media files
	TSTRING video, image, defaultVideo, defaultImage;
	GetMediaFiles(game, video, image, defaultVideo, defaultImage);

	// note whether or not videos are enabled
	bool videosEnabled = Application::Get()->IsEnableVideo();

	// If there's no incoming game, and the new media file matches the media
	// file for the current sprite, leave the current one as-is.  This can
	// happen when both the current and incoming games use the same default
	// background.  Only bother with this in the case of video; for images,
	// a re-load won't be visible.  With videos, a re-load would start the
	// video over from the beginning, so it's nicer to leave it running
	// uninterrupted.
	bool isSameVideo = false;
	if (videosEnabled
		&& incomingBackground.sprite == nullptr
		&& currentBackground.sprite != nullptr
		&& currentBackground.sprite->GetVideoPlayer() != nullptr)
	{
		// get the current video path
		if (const TCHAR *oldPath = currentBackground.sprite->GetVideoPlayer()->GetMediaPath(); oldPath != nullptr)
		{
			// figure out which new video we're going to use
			const TCHAR *newPath = nullptr;
			if (video.length() != 0)
				newPath = video.c_str();
			else if (image.length() != 0)
				newPath = nullptr;  // we're using the image, not a video
			else if (defaultVideo.length() != 0)
				newPath = defaultVideo.c_str();

			// check if they're the same
			if (newPath != nullptr && _tcsicmp(newPath, oldPath) == 0)
				isSameVideo = true;
		}
	}

	// now load the new video or image if it's not the same as the old one
	if (!isSameVideo)
	{
		// set up to load the sprite asynchronously
		HWND hWnd = this->hWnd;
		SIZE szLayout = this->szLayout;
		auto load = [hWnd, video, image, defaultImage, defaultVideo, szLayout, videosEnabled, volPct](VideoSprite *sprite)
		{
			// start at zero alpha, for the cross-fade
			sprite->alpha = 0;

			// try the video first, unless videos are disabled
			Application::AsyncErrorHandler eh;
			bool ok = false;
			if (video.length() != 0 && videosEnabled)
				ok = sprite->LoadVideo(video.c_str(), hWnd, { 1.0f, 1.0f }, eh, _T("Background Video"), true, volPct);

			// try the image if that didn't work
			if (!ok && image.length() != 0)
			{
				// try loading the image
				CapturingErrorHandler ceh;
				if (!(ok = sprite->Load(image.c_str(), { 1.0f, 1.0f }, szLayout, ceh)))
				{
					// if this is an SWF file, log the error specially
					ImageFileDesc desc;
					if (GetImageFileInfo(image.c_str(), desc) && desc.imageType == ImageFileDesc::SWF)
						eh.FlashError(ceh);
					else
						eh.GroupError(EIT_Error, nullptr, ceh);
				}
			}

			// try the default video if we still don't have anything
			if (!ok && videosEnabled && defaultVideo.length() != 0)
				ok = sprite->LoadVideo(defaultVideo.c_str(), hWnd, { 1.0f, 1.0f }, eh, _T("Default background video"), volPct);

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

void SecondaryView::ApplyWorkingAudioVolume(int volPct)
{
	auto Update = [volPct](decltype(incomingBackground) &item)
	{
		if (item.sprite != nullptr && item.sprite->IsVideo())
		{
			if (auto vp = item.sprite->GetVideoPlayer(); vp != nullptr)
				vp->SetVolume(volPct);
		}
	};
	Update(currentBackground);
	Update(incomingBackground);
}

void SecondaryView::ClearMedia()
{
	incomingBackground.Clear();
	currentBackground.Clear();
	OnChangeBackgroundImage();
	UpdateDrawingList();
}

void SecondaryView::BeginRunningGameMode(GameListItem *game, GameSystem *system, bool &hasVideo)
{
	// assume we're going to freeze updates in the window while 
	// the game is running
	bool freeze = true;

	// presume we don't have a video to play in the background
	hasVideo = false;

	// Check if we're set to continue showing this window's media
	// while this game is running.
	if (ShowMediaWhenRunning(game, system))
	{
		// We're showing this window's media while this game is
		// running.  Bring this window into the topmost layer so
		// that stays in front of any window the game itself tries
		// to open, as "show when running" overrides whatever the
		// game shows.
		SetWindowPos(GetParent(hWnd), HWND_TOPMOST, -1, -1, -1, -1,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);

		// Determine if we're showing (or are about to show) a video
		// as the background.  If so, we'll want full-speed frame
		// updates.  For static images, we can freeze the background.
		if (incomingBackground.sprite != nullptr)
			hasVideo = incomingBackground.sprite->IsVideo();
		else if (currentBackground.sprite != nullptr)
			hasVideo = currentBackground.sprite->IsVideo();

		// don't freze playback if we have a video
		if (hasVideo)
			freeze = false;
	}
	else
	{
		// Show nothing in this window while the game is running.
		// Explicitly clear any media currently being displayed, to
		// free up video memory and reduce CPU load.
		ClearMedia();
	}

	// check if we decided to freeze the background
	if (freeze)
	{
		// force a final update before we freeze background rendering, to
		// blank the window
		InvalidateRect(hWnd, NULL, false);

		// freeze updates
		freezeBackgroundRendering = true;
	}
}

bool SecondaryView::ShowMediaWhenRunning(GameListItem *game, GameSystem *system) const
{
	// my window ID
	const TCHAR *id = ShowWhenRunningWindowId();
	size_t idLen = _tcslen(id);

	// Test a space-delimited Show When Running list.  If the window ID
	// was found in the list, sets 'show' to true if the ID was found
	// positively or false if the ID was negated with a hyphen prefix,
	// and returns true.  Returns false if the window wasn't found, in
	// which case the next setting in the hierarchy (game, system,
	// global) should be used.
	bool show = false;
	auto Test = [id, idLen, &show](const TCHAR *p)
	{
		// scan the list
		if (p != nullptr)
		{
			// scan the list for our window ID
			while (*p != 0)
			{
				// find the end of the current token
				const TCHAR *nxt = p;
				for (; *nxt != 0 && !_istspace(*nxt); ++nxt);

				// check for a "negative" token, with a "-" prefix: e.g., "-dmd" means
				// that the DMD is explicitly not shown in this game
				bool tokSense = true;
				if (*p == '-')
				{
					tokSense = false;
					++p;
				}

				// compare this token - if it's a match, this window is indeed in the
				// Show When Running list, so keep showing the media
				if (_tcsnicmp(p, id, idLen) == 0)
				{
					// it's a match - use the token sense and return success
					show = tokSense;
					return true;
				}

				// advance to the start of the next token
				for (p = nxt; _istspace(*p); ++p);
			}
		}

		// not found
		return false;
	};

	// Start with the individual setting for the game
	if (game != nullptr && Test(GameList::Get()->GetShowWhenRunning(game)))
		return show;

	// The game doesn't have an individual setting, so test the system's 
	// Keep Open list
	if (system != nullptr && Test(system->keepOpen.c_str()))
		return show;

	// There's no per-game or per-system setting, so use the global setting
	if (Test(ConfigManager::GetInstance()->Get(_T("ShowWindowsWhileRunning"))))
		return show;

	// there's no setting for this window at any level; the default is to 
	// blank the window
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

	// restore idle-time background updates
	freezeBackgroundRendering = false;
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

