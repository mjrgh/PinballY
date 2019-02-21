// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Base frame window.  This is the base class for our top-level
// windows.

#pragma once

#include <unordered_set>
#include "BaseWin.h"
#include "BaseView.h"

class ViewWin;
class VanityShieldWindow;

class FrameWin : public BaseWin
{
public:
	FrameWin(const TCHAR *configVarPrefix, int iconId, int grayIconId);

	// is the window activated?
	bool IsActivated() const { return isActivated; }

	// are we in full-screen mode?
	bool IsFullScreen() const { return fullScreenMode; }

	// is the window in borderless mode? 
	virtual bool IsBorderless() const { return borderless; }

	// handle application foreground/background switches
	virtual void OnAppActivationChange(bool activating);

	// set full-screen mode
	void SetFullScreen(bool fullScreen);

	// toggle between regular and full-screen mode
	void ToggleFullScreen();

	// set borderless mode
	void SetBorderless(bool borderless);

	// toggle between regular and borderless mode
	void ToggleBorderless();

	// Show/hide the frame window.  This updates the window's UI
	// visibility and saves the config change.
	void ShowHideFrameWindow(bool show);

	// Set the window position from Javascript
	void JsSetWindowPos(HWND hwndAfter, int x, int y, int cx, int cy, int flags);

	// Set the window state
	void JsSetWindowState(TSTRING state);

	// Restore visibility from the saved configuration settings
	void RestoreVisibility();

	// Create our window, loading the settings from the configuration
	bool CreateWin(HWND parent, int nCmdShow, const TCHAR *title);

	// get my view window
	BaseWin *GetView() const { return view; }

	// update my menu
	virtual void UpdateMenu(HMENU hMenu, BaseWin *fromWin) override;

	// Save/restore the pre-run window placement
	void SavePreRunPlacement();
	void RestorePreRunPlacement();

protected:
	virtual ~FrameWin();

	// Is this a hideable window?  If true, we'll hide the window on
	// a Minimize or Close command, instead of actually minimizing or
	// closing it.  Most of our secondary windows (backglass, DMD,
	// topper) use this behavior.
	virtual bool IsHideable() const { return false; }

	// Create my view window child.  Subclasses must override this
	// to create the appropriate view window type.
	virtual BaseView *CreateViewWin() = 0;

	// Customize the system menu.  This does nothing by default.
	// Subclasses can override this to add custom commands to the
	// window's system menu.
	virtual void CustomizeSystemMenu(HMENU);

	// Copy the given context menu to the system menu, excluding
	// the given commands.
	void CopyContextMenuToSystemMenu(HMENU contextMenu, HMENU systemMenu, 
		const std::unordered_set<UINT> &excludeCommandIDs);

	// add an item to the system menu
	void AddSystemMenu(HMENU m, int cmd, int idx);

	// get the initial window position
	virtual RECT GetCreateWindowPos(int &nCmdShow) override;

	// initialize
	virtual bool InitWin() override;

	// register my window class
	virtual const TCHAR *RegisterClass() override;

	// window proc
	virtual LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam) override;

	// window creation
	virtual bool OnCreate(CREATESTRUCT *cs) override;

	// close the window
	virtual bool OnClose() override;

	// destroy
	virtual bool OnDestroy() override;

	// window activation
	virtual bool OnNCActivate(bool active, HRGN updateRegion) override;
	virtual bool OnActivate(int waCode, int minimized, HWND hWndOther) override;

	// erase the background
	virtual bool OnEraseBkgnd(HDC) override { return 0; }

	// paint the window contents
	virtual void OnPaint(HDC hdc) override;

	// size/move
	virtual void OnResize(int width, int height) override;
	virtual void OnMove(POINT pos) override;
	virtual bool OnWindowPosChanging(WINDOWPOS *pos) override;

	// get min/max window size
	virtual bool OnGetMinMaxInfo(MINMAXINFO *mmi) override;

	// paint the custom frame caption
	void PaintCaption(HDC hdc);

	// Non-client mouse clicks
	virtual bool OnNCMouseButtonUp(int button, UINT hit, POINT pt) override;

	// handle a command
	virtual bool OnCommand(int cmd, int source, HWND hwndControl) override;
	virtual bool OnSysCommand(WPARAM wParam, LPARAM lParam) override;

	// command command handler
	bool DoCommand(int cmd);

	// We use the DWM "extend frame into client" mechanism to take
	// over the entire frame as client area and do custom drawing.
	// This means we can simply return from WM_NCCALCSIZE with the
	// parameter rectangles unchanged.
	virtual bool OnNCCalcSize(bool validateClientRects, NCCALCSIZE_PARAMS *, UINT&) override;

	// Non-client hit testing.  This handles a WM_NCHITTEST in the
	// frame.  If the caller is using custom frame drawing, it needs
	// to test for hits to the caption bar, system menu icon, and
	// sizing borders.
	virtual bool OnNCHitTest(POINT ptMouse, UINT &hit) override;

	// initialize a menu popup
	virtual bool OnInitMenuPopup(HMENU hMenu, int itemPos, bool isWinMenu) override;

	// save the current window position in the config
	void WindowPosToConfig();

	// update the view layout
	virtual void UpdateLayout();

	// private window messages (WM_USER .. WM_APP-1)
	virtual bool OnUserMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;
	
	// private app messages (WM_APP+)
	virtual bool OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

	// Figure the frame parameters.  This is called when the window is
	// created, activated, or resized, so that we can update the custom
	// frame size element calculations.
	virtual void FigureFrameParams();

	// Normal window placement and style.  When we switch to full-screen mode, 
	// we store the current window style and placement here so that we can 
	// restore them when switching back to windowed mode.
	WINDOWPLACEMENT normalWindowPlacement;
	DWORD normalWindowStyle;

	// show the system menu
	void ShowSystemMenu(int x, int y);

	// Get the bounding rectangle of the nth monitor (n >= 1), in desktop
	// window coordinates.  Returns true if the given monitor was found,
	// false if no such monitor exists.
	bool GetDisplayMonitorCoords(int n, RECT &rc);

	// "Vanity shield" window.  During window creation, if we're creating a
	// full-screen or borderless window, we'll create a separate, temporary
	// cover window first to cover the area of the new window.  This is just
	// to make the window creation process look seamless by hiding the
	// initial appearance of the window at creation, since we have to create
	// the window with borders.  You'd *think* we could just create it
	// borderless in the first place, wouldn't you?  Sadly we can't, because
	// of an apparent bug or undocumented limitation in the Windows DWM: if
	// we create a window as borderless initially, the DWM won't be able to
	// properly render the caption bar and borders if we later enable them.
	// To work around this, we have to create the window with borders and
	// then turn them off; we can't go straight to the borderless style
	// during creation.  But we *can* create a separate vanity shield
	// window that's initially borderless and that initialy fills the full
	// space if full-screen; the vanity shield only lasts as long as it
	// takes to set up the *real* window, so the vanity shield never has
	// to show borders itself, thus the DWM bug is irrelevant to it.
	RefPtr<VanityShieldWindow> vanityShield;

	// main view window
	RefPtr<BaseView> view;

	// window icons, for the active and inactive window states
	HICON icon;
	HICON grayIcon;

	// Custom frame parameters
	//
	// 'dwmExtended' tells us if we succeeded in extended the window frame 
	// into the client area, which we do at window activation time.  If this
	// is true, we take over the whole window rect as the client area and 
	// draw our custom caption, otherwise we let Windows handle the caption
	// via the normal non-caption size calculation and painting.
	//
	// 'frameBorders' gives the size of the border area we draw within the
	// client area.  In principle, we could use this to draw all of the 
	// non-client controls (caption and sizing border) in the client space,
	// but for compatibility with Windows 10, we can only safely use this
	// to draw the caption.  So the left, bottom, and right elements are
	// always zero in our implementation; only the top is actually used.
	// We nonetheless keep the whole rectangle, for greater flexibility if
	// we should later want to change this to do custom sizing frame drawing
	// for the older themes that have normal non-client-area frames.
	//
	// 'captionOfs' is the offset of the caption area from the top left
	// of the window rect, taking into account the sizing borders on the
	// top and left edges, if any.  (These are removed in maximized and
	// full-screen modes, for example.)
	//
	bool dwmExtended;			// frame extended into client with DWM
	RECT frameBorders;          // frame border widths for DWM
	POINT captionOfs;           // caption offset from top left

	// window icon size
	SIZE szIcon;

	// reactivate full-screen mode on switching the app to the foreground
	void ReactivateFullScreen();

	// current mode - windowed or full-screen
	bool fullScreenMode;

	// borderless mode in the configuration?
	bool borderless;

	// is the window currently activated?
	bool isActivated;

	// window closed
	bool closed;

	// Saved window position prior to running a game.  Some games change the
	// display configuration in such a way that the Windows virtual desktop
	// area changes size, and that can in turn cause Windows to reposition
	// our windows to force them into the new display area.  It can also
	// resize our windows by changing DPI settings.  To compensate, we save
	// the window placement here prior to each game launch, and restore it
	// when the game exits.
	WINDOWPLACEMENT preRunPlacement = { 0 };

	// configuration variables
	TSTRINGEx configVarPos;
	TSTRINGEx configVarMaximized;
	TSTRINGEx configVarMinimized;
	TSTRINGEx configVarFullScreen;
	TSTRINGEx configVarVisible;
	TSTRINGEx configVarBorderless;

	// window subclass registration
	static const TCHAR *frameWinClassName;
	static bool frameWinClassRegistered;
	static ATOM frameWinClassAtom;
};
