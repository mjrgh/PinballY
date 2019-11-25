// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Secondary view.  This is a common base class for the view child
// of our secondary windows (backglass, DMD, topper).  These all
// work roughly the same way, in that they all display a background
// image or video and mostly defer event handling to the main
// playfield view.  

#pragma once
#include "BaseView.h"
#include "GameList.h"
#include "Sprite.h"
#include "VideoSprite.h"

struct MediaType;

class SecondaryView : public BaseView
{
public:
	SecondaryView(int contextMenuId, const TCHAR *winConfigVarPrefix);

	// sync with the current selection in the global game list
	void SyncCurrentGame();

	// update our menu
	virtual void UpdateMenu(HMENU hMenu, BaseWin *fromWin) override;

	// clear media
	virtual void ClearMedia();

	// Begin/end running game mode
	virtual void BeginRunningGameMode(GameListItem *game, GameSystem *system, bool &hasVideo);
	virtual void EndRunningGameMode();

	// frame window is being shown/hidden
	virtual void OnShowHideFrameWindow(bool show) override;

	// change video enabling status
	virtual void OnEnableVideos(bool enable) override;

	// apply a working audio level to playing media
	void ApplyWorkingAudioVolume(int volPct);

protected:
	// Get the next window to update during a game transition.
	// We update the windows one at a time to spread out the extra 
	// load caused by starting videos.  Rather than switching to
	// the media for a new game in every window at the same time,
	// we switch windows one at a time, with each window notifying
	// the next in line after its video has started.  This gets
	// the command for the next window in line.  This returns a
	// command of the form ID_SYNC_window; this command is sent
	// to the main playfield view for dispatch.
	virtual UINT GetNextWindowSyncCommand() const = 0;

	// Should we continue to show media in this window when we
	// start running a game?  This is a per-game setting, stored
	// in the game stats database, since the desirability is a
	// function of the particular game's ability to display its
	// own graphics in the area this window represents.  To some
	// extent, it's a function of the player system: for example,
	// Pinball FX3 doesn't provide animated backglass graphics, so
	// most people will want to let the PBY video continue to play
	// in the backglass for every FX3 game.  But it can also vary
	// per game, especially in VP, where some games provide
	// backglass and DMD graphics and some don't.
	bool ShowMediaWhenRunning(GameListItem *game, GameSystem *system) const;

	// window ID for "Show When Running" column in the game stats
	virtual const TCHAR *ShowWhenRunningWindowId() const = 0;

	// send the sync command to the next window
	void SyncNextWindow();

	// Handle a change of current background image
	virtual void OnChangeBackgroundImage() { }

	// get my default background image/video name
	virtual const TCHAR *GetDefaultBackgroundImage() const = 0;
	virtual const TCHAR *GetDefaultBackgroundVideo() const = 0;

	// get my default system image/video name
	virtual const TCHAR *GetDefaultSystemImage() const = 0;
	virtual const TCHAR *GetDefaultSystemVideo() const = 0;

	// Get the media files for the background for the given game
	virtual void GetMediaFiles(const GameListItem *game,
		TSTRING &video, TSTRING &image, TSTRING &defaultVideo, TSTRING &defaultImage);

	// Get an individual media item.  These get the main items only, 
	// not the default files.
	virtual void GetBackgroundImageMedia(const GameListItem *game, const MediaType *mtype, TSTRING &image);
	virtual void GetBackgroundVideoMedia(const GameListItem *game, const MediaType *mtype, TSTRING &video);

	// start a cross-fade for an incoming background image
	void StartBackgroundCrossfade();

	// update our sprite drawing list
	virtual void UpdateDrawingList() override;
	
	// Add the main background image to the drawing list
	virtual void AddBackgroundToDrawingList();

	// update sprite scaling
	virtual void ScaleSprites() override;

	// Update the animation.  Returns true if the animation is still
	// running after this call, false if not.
	virtual bool UpdateAnimation();

	// process a command
	virtual bool OnCommand(int cmd, int source, HWND hwndControl) override;

	// process a timer
	virtual bool OnTimer(WPARAM timer, LPARAM callback) override;

	// private application message (WM_APP to 0xBFFF)
	virtual bool OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

	// context menu display
	virtual void ShowContextMenu(POINT pt) override;

	// Timer IDs
	static const int animTimerID = 101;     // animation timer

	// Animation timer interval (milliseconds)
	static const DWORD animTimerInterval = 15;

	// Current and incoming background images.  We keep the two so that 
	// we can animate a cross-fade when switching to a new image.
	struct
	{
		void Clear()
		{
			game = nullptr;
			sprite = nullptr;
		}
		GameListItem *game;				// game list item
		RefPtr<VideoSprite> sprite;		// sprite
	}
	currentBackground, incomingBackground;

	// async loader
	AsyncSpriteLoader backgroundLoader;
};
