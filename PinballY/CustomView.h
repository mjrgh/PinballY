// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Custom Window view window
// This is a child window that serves as the D3D drawing surface for
// a user-created Javascript Custom Window.

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
#include "JavascriptEngine.h"

class Sprite;
class VideoSprite;
class TextureShader;
class GameListItem;
class CustomWin;

class CustomView : public SecondaryView
{
public:
	CustomView(JsValueRef jsobj, const TCHAR *configVarPrefix);

	// Get a custom view by serial number
	static CustomView *GetBySerial(int n);

	// Call a callback for each custom view.  Stops when the callback
	// return false.  Returns the result of the last callback, or true
	// if no callbacks are invoked.
	static bool ForEachCustomView(std::function<bool(CustomView*)> f);

	// Synchronize the next custom view.  This processes an ID_SYNC_USERDEFINED
	// command (sent to the main playfield window) to carry out our sequential,
	// one-window-at-a-time media sync process.  This searches the list of
	// custom views for the next window that hasn't been synchronized yet on
	// the current update, and initiates its media load.  When the media have
	// finished loading, that window will send a new ID_SYNC_USERDEFINED command
	// to the main window to initiate syncing in the next custom view.  The
	// "loop" ends when all of the custom views are marked as synchronized.
	static void SyncNextCustomView();

	// Synchronize all of the custom views at once.  The main window calls
	// this to carry out media sync on all of the custom views at once, when
	// the "simultaneous sync" mode is selected in the user options settings.
	// Some users with faster machines prefer for all media to load at once,
	// since the sequential loading causes a perceptible delay as the various
	// windows refresh one at a time.  The point of the sequential sync is to
	// avoid overloading the CPU with a bunch of video loads all at once,
	// which can stall video playback by saturating the CPU and disk.  But
	// some machines are fast enough to handle the high load smoothly, and
	// even on those that aren't, some users prefer a brief playback stall
	// to the domino effect of loading one window at a time.
	static void SyncAllCustomViews();

	// Receive notification that a media synchronization pass (after a new
	// game has been selected in the UI) is starting.  This clears all of our
	// internal media sync flags so that we know that all of the windows
	// have yet to be synchronized on this pass.
	static void OnBeginMediaSync();

	// Set the media types for our background media
	void SetBackgroundImageMediaType(const MediaType *mt) { backgroundImageType = mt; }
	void SetBackgroundVideoMediaType(const MediaType *mt) { backgroundVideoType = mt; }

	// Set media item names
	void SetDefaultBackgroundImage(const TCHAR *name) { defaultBackgroundImage = name; }
	void SetDefaultBackgroundVideo(const TCHAR *name) { defaultBackgroundVideo = name; }
	void SetStartupVideoName(const TCHAR *name) { startupVideoName = name; }

	// Set the Show Media When Running key
	void SetShowMediaWhenRunningId(const TCHAR *id) { showMediaWhenRunningId = id; }

	// Get/set Show Media When Running flag
	JsValueRef JsGetShowMediaWhenRunningFlag() const;
	void JsSetShowMediaWhenRunningFlag(JsValueRef flag);

	// Get the background media info
	virtual const MediaType *GetBackgroundImageType() const override { return backgroundImageType; };
	virtual const MediaType *GetBackgroundVideoType() const override { return backgroundVideoType; }

	// Does this window allow media capture?
	void SetMediaCapturable(bool f) { isMediaCapturable = f; }
	bool IsMediaCapturable() const { return isMediaCapturable; }

protected:
	virtual ~CustomView() override;

	// update a menu on open
	virtual void UpdateMenu(HMENU hMenu, BaseWin *fromWin) override;

	// The custom views go last, but there can be more than one.  So when we
	// finish loading the media in one, we have to move on to the next one.
	// Do this by sending another ID_SYNC_USERDEFINED command.  The command
	// processor will scan the list of custom views to find the next one in
	// need of synchronization on this pass.  Note that this might appear to
	// form an infinite loop of ID_SYNC_USERDEFINED commands, but the loop
	// actually terminates simply enough, when our static method that handles
	// the commnad (SyncNextCustomView()) discovers that all of the windows
	// in the list have been synced already.  At that point it'll simply
	// return without initiating a new sync, so no new command will be sent,
	// and the loop terminates.
	virtual UINT GetNextWindowSyncCommand() const override { return ID_SYNC_USERDEFINED; }

	// Media sync flag.  This keeps track of which custom views have been
	// synchronized so far on the current game selection change.
	bool mediaSyncFlag = false;

	// Get default media names
	virtual const TCHAR *GetDefaultBackgroundImage() const override { return defaultBackgroundImage.c_str(); }
	virtual const TCHAR *GetDefaultBackgroundVideo() const override { return defaultBackgroundVideo.c_str(); }
	virtual const TCHAR *GetDefaultSystemImage() const override { return _T("Default Images\\No Custom Window Media"); }
	virtual const TCHAR *GetDefaultSystemVideo() const override { return _T("Default Videos\\No Custom Window Media"); }
	virtual const TCHAR *StartupVideoName() const override { return startupVideoName.c_str(); }

	// Show media in this window when a game is running?  We override this
	// to allow customization via Javascript, rather than using the normal
	// settings scheme used by the built-in window types.
	virtual bool ShowMediaWhenRunning(GameListItem *game, GameSystem *system) const override;

	// Show Media When Running flag, set by javascript.  This has three
	// states: true -> show media when running, false -> don't show media
	// when running, undefined -> use the Show When Running ID mechanism
	// common to other secondary windows
	enum { SM_SHOW, SM_NOSHOW, SM_UNDEF } showMediaWhenRunningFlag = SM_UNDEF;

	// "Show when running" window ID.  This isn't used for custom windows,
	// since we override ShowMediaWhenRunning() to use a different test.
	virtual const TCHAR *ShowWhenRunningWindowId() const override { return showMediaWhenRunningId.c_str(); }

	// Show Media When Running ID key, set when creating the custom window
	TSTRING showMediaWhenRunningId;

	// The Javascript object associated with the window
	JsValueRef jsobj;

	// media files names
	TSTRING defaultBackgroundImage = _T("Default Custom");
	TSTRING defaultBackgroundVideo = _T("Default Custom");
	TSTRING startupVideoName;

	// Media types.  These are null by default, meaning that the system won't
	// automatically load background media into the window on game selection
	// changes.  Javascript can change these as desired.
	const MediaType *backgroundImageType = nullptr;
	const MediaType *backgroundVideoType = nullptr;

	// Does this window allow media capture?
	bool isMediaCapturable = false;

	// Original label for "Hide <This Window>" command in our context menu,
	// from the resource file.  We keep the original text so that we can update
	// the display text each time the menu pops up, to match the current
	// window title.
	TSTRING origHideThisWindowMenuLabel;
};
