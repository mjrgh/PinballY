// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Basic window class

#pragma once

#include "PrivateWindowMessages.h"

// Call a lambda function on a window's message handler thread. 
// This can be used with the HWND for any BaseWin-derived window
// to invoke the given lambda on the main UI thread. (More 
// precisely, the lambda is called on the thread that created the 
// window.  Our windows are always created on the main UI thread,
// so it's the same thing).  
//
// The lambda is of type std::function<LRESULT(void)>.  The call 
// to CallOnMainThread() returns the LRESULT returned by the 
// lambda.  Write the lambda like this:
//
//    CallOnMainThread(hwnd, [captures]() -> LRESULT {
//        // lambda body here
//        return result;
//    });
//
// This can be used to control access to  shared variables or 
// resources used in background threads, by encapsulating the 
// resource access in a little snippet of code (the lambda) that's 
// guaranteed to execute on the main thread.  Forcing all of the
// code that accesses a given resource onto one thread avoids the
// need for any other concurrency controls on the resource by
// forcing the resource access to be purely single-threaded.
//
// The function is invoked inline as a blocking SendMessage() call,
// so there are no complications with object lifetime; it acts pretty
// much like a normal call to the lambda, except that it happens in
// the specified window's message thread.
//
// It's generally faster to use a more granular locking mechanism
// (e.g., a Critical Section) when possible.  The SendMessage() call
// forces the background thread to wait for the main thread in all
// cases, whereas a resource-specific lock will only block the 
// background thread when the main thread is holding the same lock.
// Too much use of CallOnMainThread() can defeat the purpose of
// using a separate thread at all by effectively single-threading
// most of the work.  (In fact, it can be worse than doing it all
// on a single thread, because of the added overhead of the message
// calls.)
//
// This is implemented as a macro to make the type casting a bit
// "safer", at least in the sense of making it less error-prone to 
// write calls to the macro.  What makes it error-prone is the need
// to cast the lambda pointer to an LVALUE to pass to SendMessage().
// Pointer-to-int casts are bad practice in general, and the fact
// that this is a lambda pointer makes it even worse, but there's
// no way around this with SendMessage().  But the real hazard isn't
// the fact of the cast-to-LPARAM; it's getting that cast right at
// all.  Due to C++ automatic conversions, a simple cast from the
// lambda to (LPARAM) won't work, as C++ will want to convert the
// lambda to a different pointer type instead.  We need to
// explicitly cast the lambda to our std::function type first,
// then take the address of the resulting object and cast that
// pointer to the LPARAM.  That's a bit tedious and error-prone 
// to write out every time, thus the macro.
//
#define CallOnMainThread(hwnd, lambda) \
    ::SendMessage((hwnd), BWMsgCallLambda, 0, \
        (LPARAM)&static_cast<std::function<LRESULT(void)>>(lambda))

class BaseWin : public RefCounted
{
public:
	BaseWin(int contextMenuId);

	// create our system window
	bool Create(HWND parent, const TCHAR *title, DWORD style, int nCmdShow);

	// system window handle
	HWND GetHWnd() const { return hWnd; }

	// get my context menu
	HMENU GetContextMenu() const { return this->hContextMenu; }

	// send/post a message to the window
	LRESULT SendMessage(UINT message, WPARAM wParam = 0, LPARAM lParam = 0);
	void PostMessage(UINT message, WPARAM wParam = 0, LPARAM lParam = 0);

	// Update menu command item checkmarks and item-enabled status for 
	// the current UI state.  'fromWin' indicates which window is
	// initiating the request; in cases where parents or children need
	// to call each other to update items whose state is contained
	// in another window, this prevents recursion back into the 
	// original caller.  Subclasses must override.  
	virtual void UpdateMenu(HMENU hMenu, BaseWin *fromWin) = 0;

	// is the window active, as of the last WM_NCACTIVATE?
	bool IsNcActive() const { return isNcActive; }

protected:
	// Initialize the window.  Returns true on success, false on failure.
	virtual bool InitWin() { return true; }

	// Initially show the window.  The default shows the window with the
	// given mode and does an initial update.
	virtual void InitShowWin(int nCmdShow);

	// get the initial window position for creation
	virtual RECT GetCreateWindowPos(int &nCmdShow) { return { 0, 0, 1, 1 }; }

	// load the menu icons
	void LoadMenuIcon(int cmd, int resid);
	void LoadMenuCheckIcons(int cmd, int residUnchecked, int residChecked);

	// Show the context menu.  'pt' is the location of the mouse cursor
	// in screen coordinates at the time of the right-click.
	virtual void ShowContextMenu(POINT pt);

	// Virtual window proc.  Subclasses override this to process
	// system messages sent to the window.
	virtual LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam);

	// Message handler convention:  each message handler returns a bool
	// indicating whether or not the message "overrides" the system
	// default window proc.  True means that the handler fully handled
	// the message, overriding the default window proc.  False means
	// that the handler didn't override the default window proc: that
	// is, it either didn't anything at all, or it did some work in
	// addition to the default handling, but still wants the default
	// handling to be applied as well.
	//
	// Some message handlers use void returns.  These are handlers 
	// where either the system default window proc does nothing, or
	// where it must always be invoked.  We use void returns for these 
	// to clarify that the override won't be able to control whether or 
	// not the system handler is invoked.  If for some reason you really 
	// need to change this behavior for a particular message (which you 
	// shouldn't, since if you did we'd have used the bool return 
	// convention in the first place!), you'll have to override the
	// whole virtual WndProc() and handle that message ID case.
	//
	// For convenience, our message handlers decode the WPARAM/LPARAM
	// values into the proper types they represent.  Handlers can get at
	// the original parameters via this->curMsg.
	//
	// The Windows convention for most system messages to return 0 
	// from the window proc if the message is handled.  However, the
	// return value is used to convey information back to the caller
	// for some messages.  In cases where the result is always used,
	// we'll generally pass an LRESULT* or similar out variable for
	// the handler to fill in.  In cases where a return value is only
	// rarely needed, we'll instead return the default 0, but the
	// handler can return a more specific value by storing it in
	// this->curMsg->lResult.

	// Current message that we're processing.  This is only valid
	// during message processing.  The window proc "stacks" this in
	// recursive calls.
	struct CurMsg
	{
		CurMsg(UINT msg, WPARAM wParam, LPARAM lParam)
			: msg(msg), wParam(wParam), lParam(lParam), 
			dwmHandled(false), dwmResult(0),
			lResult(0)
		{ }

		// raw message parameters
		UINT msg;
		WPARAM wParam;
		LPARAM lParam;

		// DWM result, for windows using DWM extensions
		bool dwmHandled;
		LRESULT dwmResult;

		// window proc result to return
		LRESULT lResult;
	};
	CurMsg *curMsg;

	// Window creation
	virtual bool OnCreate(CREATESTRUCT *cs) { return false; }

	// process WM_ACTIVATE
	virtual bool OnActivate(int waCode, int minimized, HWND hWndOther) { return false; }

	// process WM_ACTIVATEAPP
	virtual bool OnActivateApp(BOOL activating, DWORD otherThreadId);

	// Process WM_CLOSE.  If you want to prevent the window from closing,
	// override this to return true: that will prevent calling the default
	// window proc, which is where the window is actually closed if we
	// want closing to proceed. 
	virtual bool OnClose() { return false; }

	// process WM_DESTROY and WM_NCDESTROY
	virtual bool OnDestroy() { return false; }
	virtual bool OnNCDestroy();

	// process WM_PAINT
	virtual void OnPaint(HDC) { }

	// non-client hit test
	virtual bool OnNCHitTest(POINT pt, UINT &hitResult) { return false; }

	// non-client size calculation
	virtual bool OnNCCalcSize(bool validateClientRects, NCCALCSIZE_PARAMS *params, UINT &wvrFlagsResult)
		{ return false; }
	
	// non-client activation
	virtual bool OnNCActivate(bool active, HRGN updateRegion)
	{
		// note the new active status
		isNcActive = active;

		// use the system default handling
		return false;
	}

	// get min/max window size
	virtual bool OnGetMinMaxInfo(MINMAXINFO *mmi) { return false; }

	// Handle a WM_SIZE event
	virtual void OnResize(int width, int height) { szClient = { width, height }; }

	// Handle WM_MOVE
	virtual void OnMove(POINT pt) { }

	// Handle a WM_SIZE event for minimizing the window
	virtual void OnMinimize() { }

	// Window position is about to change
	virtual bool OnWindowPosChanging(WINDOWPOS *) { return false; }

	// Window position has just changed
	virtual bool OnWindowPosChanged(WINDOWPOS *) { return false; }

	// Handle a DPI change event
	virtual void OnDpiChanged(int dpiX, int dpiY, LPCRECT prcNewPos);

	// Process WM_ERASEBKGND.  By default, we set curMsg->lResult to TRUE
	// before calling this, since the only reason you'd normally want to
	// override this is to do whatever erasing needs to be done.  It's
	// unusual to return FALSE from this message without invoking the
	// default window proc, since that leaves the window marked as
	// un-erased, which typically gets Windows stuck in a loop calling
	// this message handler with no result.  But if for some reason you
	// need to bypass the system default window proc AND return FALSE
	// from the window proc, you can do so via this->curMsg->lResult.
	virtual bool OnEraseBkgnd(HDC hdc) { return false; }

	// process WM_KEYDOWN and WM_KEYUP
	virtual bool OnKeyEvent(UINT message, WPARAM wParam, LPARAM lParam) { return false; }

	// process WM_SYSKEYDOWN and WM_SYSKEYUP
	virtual bool OnSysKeyEvent(UINT message, WPARAM wParam, LPARAM lParam) { return false; }

	// process WM_SYSCHAR
	virtual bool OnSysChar(WPARAM wParam, LPARAM lParam) { return false; }

	// process WM_HOTKEY
	virtual bool OnHotkey(int id, WORD mod, int vkey) { return false; }

	// Mouse events.  'button' is one of the MouseButton::mbXxx button codes.
	virtual bool OnMouseButtonDown(int button, POINT pt) { return false; }
	virtual bool OnMouseButtonUp(int button, POINT pt) { return false; }
	virtual bool OnMouseMove(POINT pt) { return false; }

	// Non-client mouse clicks
	virtual bool OnNCMouseButtonDown(int button, UINT hit, POINT pt) { return false; }
	virtual bool OnNCMouseButtonUp(int button, UINT hit, POINT pt) { return false; }

	// Process WM_MOUSEWHEEL.  'button' is MouseButton::mbWheel for the
	// normal wheel, mbHWheel for the horizontal wheel.
	virtual bool OnMouseWheel(int button, float delta) { return false; }

	// Process WM_INITMENUPOPUP
	virtual bool OnInitMenuPopup(HMENU hMenu, int itemPos, bool isWinMenu) { return false; }

	// Process a raw input event (WM_INPUT)
	virtual void OnRawInput(UINT rawInputCode, HRAWINPUT hRawInput) { }

	// Process a raw input device change event (WM_INPUT_DEVICE_CHANGED)
	virtual void OnRawInputDeviceChange(USHORT what, HANDLE hDevice) { }

	// Process a command.  'cmd' is the command code from the low word of
	// the WPARAM, and 'source' is the message source code from the high
	// word (0 for a menu, 1 for an accelerator, or the control notification
	// code for a control message).  hwndControl is the control's window
	// handle if it's a control notification, or NULL if the source is a
	// menu or accelerator.
	virtual bool OnCommand(int cmd, int source, HWND hwndControl) { return false; }

	// process a system command
	virtual bool OnSysCommand(WPARAM wParam, LPARAM lParam) { return false; }

	// process a timer
	virtual bool OnTimer(WPARAM timer, LPARAM callback) { return false; }

	// private window class messages (WM_USER to WM_APP-1)
	virtual bool OnUserMessage(UINT msg, WPARAM wParam, LPARAM lParam);

	// private application message (WM_APP to 0xBFFF)
	virtual bool OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam);

	// Handle an ENTER IDLE message.  The 'code' value is one of:
	//
	//   MSGF_DIALOGBOX  -> a dialog box is being displayed; hwndSrc is the dialog window
	//   MSGF_MENU       -> a menu is being displayed; hwndSrc is the menu's containing window
	//
	// The default handler calls the D3DView renderer as long as we're
	// idling, to continue updates in D3D windows.
	virtual bool OnEnterIdle(int code, HWND hwndSrc);

	// Win32 window proc callback
	static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	// Register my window class, returning the class name
	virtual const TCHAR *RegisterClass();

	// have we registered the base window class?
	static bool baseWinClassRegistered;

	// window class name and class atom
	static const TCHAR *baseWinClassName;
	static ATOM baseWinClassAtom;

	// system window handle
	HWND hWnd;

	// Context menu
	HMENU hContextMenu;

	// context menu resource ID
	int contextMenuId;

	// Context menu image bitmaps.  We keep a list of these so that we
	// can delete them when we destroy the window.
	std::list<HBITMAP> menuBitmaps;

	// window client area on last resize
	SIZE szClient;

	// is the window active as of the last WM_NCACTIVATE?
	bool isNcActive;

	virtual ~BaseWin();
};
