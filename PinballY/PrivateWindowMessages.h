// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Private window message definitions
//
// We define all of our internal WM_USER+n and WM_APP+n messages
// centrally here, to make it easier to keep track of them all and
// avoid any conflicts.
//
// Per Windows SDK guidelines, WM_APP+n messages are application-wide
// (that is, not specific to any window class), whereas WM_USER+n
// messages are private to a window class.  In principle, that means
// we could reuse the same WM_USER+n message ID for different purposes
// in different windows, and also that we could define the constants
// for the message IDs in the window-specific implementation files
// instead of defining them centrally in this one file.  However,
// we use a fairly deep class hierarchy, which makes distributed
// definitions of IDs problematic, since we can't make a message ID
// "private" in the C++ access control sense - at best we can make
// them "protected", meaning that a class and its derived classes
// all share the same class-specific WM_USER+n message IDs.  So
// we use a more restrictive rule than the normal Windows guidelines:
// we treat all messages in the WM_APP+n and WM_USER+n ranges as
// app-global.  And therefore, we define all of the IDs centrally
// in this one file, to minimize the chances of accidental collisions
// in ID assignments.

#pragma once
#include <Windows.h>
#include "../Utilities/ErrorIconType.h"

// BaseView messages
const UINT BVMsgAsyncSpriteLoadDone = WM_USER + 0;	// sprite loading finished

// BaseWin messages
const UINT BWMsgUpdateMenu = WM_USER + 100;			// update menu commands; wparam=HMENU, lParam=BaseWin* fromWin
const UINT BWMsgCallLambda = WM_USER + 101;         // call a lamdba on the window thread (see CallOnMainThread() in BaseWin.h)

// FrameWin messages
const UINT FWRemoveVanityShield = WM_USER + 150;    // remove the vanity shield window

// PlayfieldView messages
const UINT PFVMsgGameLoaded = WM_USER + 200;        // LPARAM = LONG_PTR(&PlayfieldView::LaunchReport)
const UINT PFVMsgGameOver = WM_USER + 201;          // LPARAM = LONG_PTR(&PlayfieldView::LaunchReport)
const UINT PFVMsgGameLaunchError = WM_USER + 202;   // LPARAM = LONG_PTR(&PlayfieldView::LaunchErrorReport)
const UINT PFVMsgGameRunBefore = WM_USER + 203;     // LPARAM = LONG_PTR(&PlayfieldView::LaunchReport)
const UINT PFVMsgGameRunAfter = WM_USER + 204;      // LPARAM = LONG_PTR(&PlayfieldView::LaunchReport)
const UINT PFVMsgCaptureDone = WM_USER + 205;       // WPARAM = LONG_PTR(&PlayfieldView::CaptureDoneReport)
const UINT PFVMsgManualGo = WM_USER + 206;          // manual start/stop event from Admin Host
const UINT PFVMsgLaunchThreadExit = WM_USER + 207;  // LPARAM = LONG_PTR(&PlayfieldView::LaunchReport)
const UINT PFVMsgShowError = WM_USER + 208;			// LPARAM = const PFVMsgShowErrorParams *params
const UINT PFVMsgShowSysError = WM_USER + 209;		// WPARAM = TCHAR *friendly, LPARAM = const TCHAR *details
const UINT PFVMsgPlayElevReqd = WM_USER + 210;      // WPARAM = TCHAR *systemName, LPARAM = game->internalID)
const UINT PFVMsgJsDebugMessage = WM_USER + 211;    // Javascript debug request received from debugger UI

// DMDView messages
const UINT DMVMsgHighScoreImage = WM_USER + 300;    // WPARAM = DWORD seqno, LPARAM = std::list<DMDView::HighScoreImage> *images

// PFVShowMessage parameters struct
class ErrorList;
struct PFVMsgShowErrorParams
{
	PFVMsgShowErrorParams(const ErrorList *errList) :
		iconType(EIT_Error), summary(nullptr), errList(errList) { }
	PFVMsgShowErrorParams(ErrorIconType iconType, const ErrorList *errList) :
		iconType(iconType), summary(nullptr), errList(errList) { }
	PFVMsgShowErrorParams(const TCHAR *summary, const ErrorList *errList = nullptr) :
		iconType(EIT_Error), summary(summary), errList(errList) { }
	PFVMsgShowErrorParams(ErrorIconType iconType, const TCHAR *summary, const ErrorList *errList = nullptr) :
		iconType(iconType), summary(summary), errList(errList) { }

	// Icon type
	ErrorIconType iconType;

	// Summary message.  This is optional; if provided, it's displayed 
	// at the top of the message box, separated visually from the error
	// list.
	const TCHAR *summary;

	// Error list.  Optional; if provided, the errors in the list are
	// displayed as separate line items in the lower part of the box,
	// beneath the summary.
	const ErrorList *errList;
};

// HighScores messages
const UINT HSMsgHighScores = WM_APP + 0;            // LPARAM = const HighScores::NotifyInfo*

// App-wide private window messages (PWM_)
const UINT PWM_ISBORDERLESS = WM_APP + 100;         // is the window borderless?  LRESULT=BOOL is-borderless
const UINT PWM_ISFULLSCREEN = WM_APP + 101;         // is the window full-screen? LRESULT=BOOL is-full-screen

// Audio Video Player app-wide messages.  These messages are sent 
// from AVP objects to their associated event windows during playback.
// These messages pass the AVP cookie in the WPARAM. 
const UINT AVPMsgFirstFrameReady = WM_APP + 200;	// first frame is ready
const UINT AVPMsgEndOfPresentation = WM_APP + 201;	// end of presentation
const UINT AVPMsgLoopNeeded = WM_APP + 202;			// end of presentation - window must initiate looping
const UINT AVPMsgSetFormat = WM_APP + 203;          // video frame format detected; LPARAM(AudioVideoPlayer::FormatDesc*)

// DirectShow media player events
const UINT DSMsgOnEvent = WM_APP + 300;             // DirectShow IMediaEvent event ready notification
	
// Private dialog subclass messages.  For dialog boxes implemented
// via the Windows dialog APIs, the Windows dialog manager defines
// the window class, and it uses certain WM_USER messages.  Some
// of these are exposed externally (DM_GETDEFID et al), but it's
// best to assume that others could be used internally within the
// dialog manager itself without being documented, or *could* be
// used in future Windows updates (so it's not even safe to try to
// determine empirically which ones are actually used currently).
// This is in keeping with Windows conventions that the window
// class owns the entire WM_USER range, and unfortunately the
// designers of Windows used the term "class" in a pre-OOP sense
// in this context, and weren't thinking in terms of subclassing.
// The recommended solution (per Microsoft) is for application
// code that needs to inject its own private messages into dialog
// boxes to use the WM_APP space.  To avoid conflicts with other
// WM_APP usages of our own, we'll reserve a range here for use
// in custom dialog boxes.
const UINT PrivateDialogMessageFirst = WM_APP + 500;
const UINT PrivateDialogMessageLast = WM_APP + 699;

