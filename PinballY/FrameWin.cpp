// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <Dwmapi.h>
#include <vssym32.h>
#include <VersionHelpers.h>
#include "../Utilities/Config.h"
#include "PlayfieldView.h"
#include "Resource.h"
#include "PlayfieldWin.h"
#include "Application.h"
#include "MouseButtons.h"
#include "LogFile.h"

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Uxtheme.lib")

// -----------------------------------------------------------------------
//
// Vanity shield window.  This is a temporary window that we create to
// cover up the final window area when creating a borderless or full-screen
// window, to hide the "normal window" appearance of the window that we 
// have to give it briefly during the creation process in order to work
// around a DWM bug.
//
class VanityShieldWindow : public BaseWin
{
public:
	VanityShieldWindow(RECT &rc) : BaseWin(0), rc(rc) { }
	virtual void UpdateMenu(HMENU, BaseWin *) override { }
	virtual RECT GetCreateWindowPos(int &nCmdShow) override { return rc; }
	virtual bool OnEraseBkgnd(HDC hdc)
	{
		RECT rcClient;
		GetClientRect(hWnd, &rcClient);
		FillRect(hdc, &rcClient, GetStockBrush(BLACK_BRUSH));
		return true;
	}

	// initial window position
	RECT rc;
};


// -----------------------------------------------------------------------
//
// Frame window
//

// config variable names
namespace ConfigVars
{
	static const TCHAR *FullScreen = _T("FullScreen");
	static const TCHAR *WindowPos = _T("Position");
	static const TCHAR *FSWindowPos = _T("FullScreenPosition");
	static const TCHAR *WindowMaximized = _T("Maximized");
	static const TCHAR *WindowMinimized = _T("Minimized");
	static const TCHAR *WindowVisible = _T("Visible");
	static const TCHAR *WindowBorderless = _T("Borderless");
	static const TCHAR *FullScreenRestoreMethod = _T("Startup.FullScreenRestoreMethod");
}

// statics
bool FrameWin::frameWinClassRegistered = false;
const TCHAR *FrameWin::frameWinClassName = _T("PinballY.FrameWinClass");
ATOM FrameWin::frameWinClassAtom = 0;

FrameWin::FrameWin(const TCHAR *configVarPrefix, const TCHAR *logDesc, int iconId, int grayIconId) : BaseWin(0)
{
	// remember the name for the log file
	this->logDesc = logDesc;

	// generate the config var names
	configVarPos = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::WindowPos);
	configVarFSPos = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::FSWindowPos);
	configVarFullScreen = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::FullScreen);
	configVarMinimized = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::WindowMinimized);
	configVarMaximized = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::WindowMaximized);
	configVarVisible = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::WindowVisible);
	configVarBorderless = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::WindowBorderless);
	
	// clear variables
	isActivated = false;
	normalWindowPlacement.length = 0;
	dwmExtended = false;
	borderless = false;

	// get the standard system caption height
	int cyCaption = GetSystemMetrics(SM_CYCAPTION);
	cyCaption = (cyCaption >= 23 ? 24 : 16);

	// figure the icon size based on the caption
	szIcon.cx = szIcon.cy = cyCaption;

	// load the icons
	icon = (HICON)LoadImage(G_hInstance, MAKEINTRESOURCE(iconId),
		IMAGE_ICON, cyCaption, cyCaption, LR_SHARED | LR_LOADTRANSPARENT);
	grayIcon = (HICON)LoadImage(G_hInstance, MAKEINTRESOURCE(grayIconId),
		IMAGE_ICON, cyCaption, cyCaption, LR_SHARED | LR_LOADTRANSPARENT);
}

FrameWin::~FrameWin()
{
}

// Register my window class
const TCHAR *FrameWin::RegisterClass()
{
	if (!frameWinClassRegistered)
	{
		// set up our class descriptor
		WNDCLASSEX wcex;
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = StaticWndProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = sizeof(LONG_PTR);
		wcex.hInstance = G_hInstance;
		wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
		wcex.lpszMenuName = 0;
		wcex.lpszClassName = frameWinClassName;

		// Use a black brush for the background.  This is critical for
		// DwmExtendFrameIntoClient(), because DWM keys the normal frame
		// drawing to this brush.  If this is anything other than the
		// stock black brush, DWM won't draw the frame controls properly.
		wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

		// Set the icons in the class registration.  Note that we don't
		// use the previously loaded icon objects for these, since we
		// loaded that at a custom size, and we want the standard icon
		// size for the class icons.
		wcex.hIcon = LoadIcon(G_hInstance, MAKEINTRESOURCE(IDI_MAINICON));
		wcex.hIconSm = LoadIcon(G_hInstance, MAKEINTRESOURCE(IDI_MAINICON));

		// register the class
		frameWinClassAtom = RegisterClassEx(&wcex);

		// we're not registered
		frameWinClassRegistered = true;
	}

	// return the window class
	return frameWinClassName;
}

RECT FrameWin::GetCreateWindowPos(int &nCmdShow)
{
	// set up with default window coordinates as a fallback in case we
	// don't find a saved location in the config
	RECT pos;
	SetRect(&pos, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0);

	// Get the full screen mode flag.  We won't actually reinstate this
	// until we've finished creating the window, to work around a mystery
	// DWM bug with custom caption drawing that's triggered if we start
	// with full-screen styles (specifically, without caption and sizing
	// borders).
	fullScreenMode = ConfigManager::GetInstance()->GetBool(configVarFullScreen);
	
	// Also note the borderless state.  As with full-screen mode, we can't
	// go borderless initially, because we'll trigger a DWM bug with frame
	// drawing if we don't let DWM set up the initial window with border
	// and caption styles enabled.
	bool borderless = ConfigManager::GetInstance()->GetBool(configVarBorderless);

	// get the stored window location
	auto cfg = ConfigManager::GetInstance();
	RECT rc = cfg->GetRect(configVarPos, pos);

	// get the maximized and minimized states
	if (cfg->GetInt(configVarMaximized, 0))
		nCmdShow = SW_MAXIMIZE;
	else if (cfg->GetInt(configVarMinimized, 0))
		nCmdShow = SW_MINIMIZE;

	// log the restored settings
	LogFile::Get()->Group(LogFile::WindowLayoutLogging);
	LogFile::Get()->Write(LogFile::WindowLayoutLogging,
		_T("Window layout setup: initializing %s window\n")
		_T("  Normal position (when not maximized or full-screen): Left,top = %d, %d; Right,bottom = %d, %d; Size = %d x %d\n")
		_T("  Full screen mode = %hs\n")
		_T("  Borderless = %hs\n")
		_T("  Show Mode = %d (%hs)\n"),
		logDesc.c_str(),
		rc.left, rc.top, rc.right, rc.bottom, rc.right - rc.left, rc.bottom - rc.top,
		fullScreenMode ? "Yes" : "No",
		borderless ? "Yes" : "No",
		nCmdShow,
		nCmdShow == SW_SHOW ? "SW_SHOW" :
		nCmdShow == SW_SHOWNORMAL ? "SW_SHOWNORMAL" :
		nCmdShow == SW_SHOWDEFAULT ? "SW_SHOWDEFAULT" :
		nCmdShow == SW_MAXIMIZE ? "SW_MAXIMIZE" :
		nCmdShow == SW_SHOWMAXIMIZED ? "SW_SHOWMAXIMIZED" :
		nCmdShow == SW_MINIMIZE ? "SW_MINIMIZE" :
		nCmdShow == SW_SHOWMINIMIZED ? "SW_SHOWMINIMIZED" :
		nCmdShow == SW_HIDE ? "SW_HIDE" :
		"SW_other");

	// check if we read a non-default position
	if (rc.left != CW_USEDEFAULT && rc.right != CW_USEDEFAULT)
	{
		// if desired, make sure it's within the visible desktop area
		if (cfg->GetBool(_T("Startup.ForceWindowsIntoView"), true)
			&& !IsWindowPosUsable(rc, 50, 50))
		{
			// set a minimum usable size
			if (rc.right < rc.left + 50)
			{
				rc.right = rc.left + 50;
				LogFile::Get()->Write(LogFile::WindowLayoutLogging, _T("  ! Width too small, adjusting to %d\n"), rc.right - rc.left);
			}
			if (rc.bottom < rc.top + 50)
			{
				rc.bottom = rc.top + 50;
				LogFile::Get()->Write(LogFile::WindowLayoutLogging, _T("  ! Height too small, adjusting to %d\n"), rc.bottom - rc.top);
			}

			// force it into the desktop work area
			RECT origRc = rc;
			ForceRectIntoWorkArea(rc, false);

			// log any change
			if (rc.left != origRc.left || rc.right != origRc.right || rc.top != origRc.top || rc.bottom != origRc.bottom)
			{
				LogFile::Get()->Write(LogFile::WindowLayoutLogging,
					_T("  ! Position is outside usable window area, forcing into view; new area = %d, %d, %d, %d (size %d x %d)\n"),
					rc.left, rc.top, rc.right, rc.bottom, rc.right - rc.left, rc.bottom - rc.top);
			}
		}

		// apply the saved position
		pos = rc;
	}
	else
	{
		LogFile::Get()->Write(LogFile::WindowLayoutLogging,
			_T("  Note: left/top = %d = CW_USEDEFAULT means Windows chooses the position\n"), CW_USEDEFAULT);
	}

	// If the saved window setup is borderless or full-screen, create a
	// "vanity shield" window covering the creation area, to hide the
	// window caption and border structure that Windows will draw during
	// the creation process, until we change the frame properties.  The
	// momentary appearance of the borders is 
	if ((borderless || fullScreenMode) 
		&& (nCmdShow != SW_MINIMIZE && nCmdShow != SW_SHOWMINIMIZED && nCmdShow != SW_SHOWMINNOACTIVE && nCmdShow != SW_HIDE))
	{
		// start with the same position as the window itself
		RECT rcVanity = pos;

		// get the full-screen area, if desired
		if (fullScreenMode)
			GetFullScreenRestorePosition(&rcVanity, &pos);

		// create the window
		vanityShield.Attach(new VanityShieldWindow(rcVanity));
		vanityShield->Create(NULL, _T("PinballY"), WS_POPUP | WS_CLIPSIBLINGS, SW_SHOW);
	}

	// return the position
	return pos;
}

// Get the full-screen restore position
bool FrameWin::GetFullScreenRestorePosition(RECT *fullScreenPos, const RECT *preFullScreenPos)
{
	// get the full-screen restore method from the settings
	auto cfg = ConfigManager::GetInstance();
	const TCHAR *method = cfg->Get(ConfigVars::FullScreenRestoreMethod, _T(""));

	LogFile::Get()->Group(LogFile::WindowLayoutLogging);
	LogFile::Get()->Write(LogFile::WindowLayoutLogging,
		_T("Window layout setup: getting full-screen restore position for %s\n"),
		logDesc.c_str());
		
	// parse the method string
	static const std::basic_regex<TCHAR> pixPat(_T("(pix(el)?\\s+)?coord(inate)?s?"), std::regex_constants::icase);
	if (std::regex_match(method, pixPat))
	{
		// Pixel Coordinates method.  This restores the exact full-screen
		// position last saved, without trying to map to a monitor.

		// get the stored full-screen location
		RECT fsrc = cfg->GetRect(configVarFSPos);
		LogFile::Get()->Write(LogFile::WindowLayoutLogging,
			_T(". using Pixel Coordinates method per settings; %s = %d, %d, %d, %d (%d x %d)\n"),
			configVarFSPos.c_str(), fsrc.left, fsrc.top, fsrc.right, fsrc.bottom,
			fsrc.right - fsrc.left, fsrc.bottom - fsrc.top);

		// if a position was stored, use it
		if (fsrc.left != fsrc.right && fsrc.bottom != fsrc.top)
		{
			*fullScreenPos = fsrc;
			return true;
		}

		// missing or empty position - log it and fall through to the
		// Nearest Monitor method
		LogFile::Get()->Write(LogFile::WindowLayoutLogging,
			_T(". note: saved full-screen position is missing; falling back on Nearest Monitor method\n"));
	}

	// If we didn't choose a different position already, fall back
	// on the Nearest Monitor method.
	//
	// Nearest Monitor uses the full display area of the monitor 
	// containing the PRE-full-screen position of the window (that is,
	// the position of the window as it was when the user applied the
	// FULL SCREEN command).  This essentially simulates the effect
	// of the user performing a new FULL SCREEN command on the restored
	// (pre-full-screen) position.  This is the default because it
	// adapts automatically to changes in desktop layout and screen
	// resolution.  Whatever the desktop looks like right now, we'll
	// pick the full area of a monitor as the new window area.
	RECT logrc;
	if (preFullScreenPos != nullptr)
		logrc = *preFullScreenPos;
	else
		GetWindowRect(hWnd, &logrc);
	LogFile::Get()->Write(LogFile::WindowLayoutLogging,
		_T(". using Nearest Monitor method, based on %hs (%d, %d, %d, %d)\n"),
		preFullScreenPos != nullptr ? "stored pre-full-screen position" : "current live window position",
		logrc.left, logrc.top, logrc.right, logrc.bottom);

	// find the monitor containing the rectangle or window, as applicable
	MONITORINFO mi = { sizeof(mi) };
	HMONITOR hMonitor = preFullScreenPos != nullptr ?
		MonitorFromRect(preFullScreenPos, MONITOR_DEFAULTTONEAREST) :
		MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);

	// get the monitor descriptor
	if (GetMonitorInfo(hMonitor, &mi))
	{
		// got it 
		LogFile::Get()->Write(LogFile::WindowLayoutLogging,
			_T(". monitor area is %d, %d, %d, %d (%d x %d)\n"),
			mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom,
			mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top);

		// use the full monitor screen area from the descriptor
		*fullScreenPos = mi.rcMonitor;
		return true;
	}

	// no full-screen position is available
	LogFile::Get()->Write(LogFile::WindowLayoutLogging, _T(". failed - unable to determine full-screen position\n"));
	return false;
}

// initialize the window
bool FrameWin::InitWin()
{
	// do the base class work
	if (!__super::InitWin())
		return false;

	// create my view
	view.Attach(CreateViewWin());
	if (view == nullptr)
		return false;

	// Turn off window transition animations if we have a vanity shield.  
	// The whole point of the vanity shield is to hide the initial window
	// placement sequence behind a cloak of darkness.  Allowing transition
	// animations can actually make things worse, because our cloak might
	// be lifted midway through an animation: so we'll have the screen go
	// black, then get a flash of the desktop around the edges of the real
	// window as it animates out to full size.
	if (vanityShield != nullptr)
	{
		int value = 1;
		DwmSetWindowAttribute(hWnd, DWMWA_TRANSITIONS_FORCEDISABLED, &value, sizeof(value));
	}

	// If we're starting in full-screen mode, post a command to self to
	// switch to full screen mode, and do the rest of the initialization
	// as a normal window.  Initializing the window in full-screen mode
	// causes a weird redraw problem in our custom frame area after
	// returning to normal window mode, for reasons I haven't been able
	// to determine.  To all appearances, the window is identical either
	// way in all of the API attributes I can see, so my best guess is
	// that there's something that sticks in the internal DWM structs
	// for the record at window creation time, that can't be cleared up
	// with any of the later changes we make when switching from full-
	// screen to windowed.  (Weirdly, minimizing and then restoring the
	// window will un-stick whatever's stuck, but that's not a viable
	// workaround.)  The solution seems to be to defer our full-screen
	// style switching until after window creation has been completed.
	if (fullScreenMode)
	{
		fullScreenMode = false;
		PostMessage(WM_COMMAND, ID_FULL_SCREEN_INIT);
	}

	// For the same reason as full-screen mode, it doesn't seem to work
	// to initialize the window in borderless mode.  The DWM seems to
	// have a problem with drawing the title bar later on if we don't
	// show a title bar initially.  So if this isn't a permanently
	// borderless window, always start in bordered mode and switch to
	// borderless via a posted command.   Note that we can distinguish
	// between switchable windows and permanently borderless by setting
	// our internal 'borderless' flag to false and checking what
	// IsBorderless() says: if we get a true result from IsBorderless(),
	// we know that a subclass is making it permanently borderless.
	borderless = false;
	if (!IsBorderless() && ConfigManager::GetInstance()->GetBool(configVarBorderless))
		PostMessage(WM_COMMAND, ID_WINDOW_BORDERS_INIT);

	// If there's a vanity shield, remove it as soon as we finish with
	// the FULL SCREEN and TOGGLE BORDERS commands.  The vanity shield is 
	// specifically to cover up the initial redraws with the window in its
	// half-formed state, so it's no longer needed once we're finished 
	// setting up the full set of window styles.
	if (vanityShield != nullptr)
		PostMessage(FWRemoveVanityShield);

	// customize the system menu
	CustomizeSystemMenu(GetSystemMenu(hWnd, FALSE));

	// update the frame layout
    FigureFrameParams();
	UpdateLayout();

	// success
	return true;
}

// Create our system window
bool FrameWin::CreateWin(HWND parent, int nCmdShow, const TCHAR *title)
{
	// figure the normal style
	normalWindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU
		| WS_SIZEBOX | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_CLIPSIBLINGS;

	// if it's initially hidden, change the show command to SW_HIDE
	bool visible = ConfigManager::GetInstance()->GetInt(configVarVisible, 1) != 0;
	if (!visible)
		nCmdShow = SW_HIDE;

	// create the window
	return Create(parent, title, normalWindowStyle, nCmdShow);
}

void FrameWin::UpdateMenu(HMENU hMenu, BaseWin *fromWin)
{
	// update full-screen mode
	CheckMenuItem(hMenu, ID_FULL_SCREEN, MF_BYCOMMAND | (fullScreenMode ? MF_CHECKED : MF_UNCHECKED));

	// udpate "Show Window Borders" 
	CheckMenuItem(hMenu, ID_WINDOW_BORDERS, MF_BYCOMMAND | (!IsBorderless() ? MF_CHECKED : MF_UNCHECKED));

	// the view controls some of the state, so have it make further updates
	if (view != fromWin)
		view->UpdateMenu(hMenu, this);
}

void FrameWin::CopyContextMenuToSystemMenu(HMENU contextMenu, HMENU systemMenu,
	const std::unordered_set<UINT> &excludeCommandIDs)
{
	// if there's no system menu, there's nothing to do
	if (systemMenu == NULL)
		return;

	// get the first submenu of the context menu, as that's the actual
	// context menu popup
	contextMenu = GetSubMenu(contextMenu, 0);

	// Test the menu to see if our custom items are already
	// present.  Assume that if we're copied the first item,
	// we've copied all of them.  If we find that we've already
	// copied the items, don't do so again.
	MENUITEMINFO mii;
	mii.cbSize = sizeof(mii);
	mii.fMask = MIIM_FTYPE | MIIM_ID;
	if (GetMenuItemInfo(contextMenu, 0, TRUE, &mii)
		&& GetMenuItemInfo(systemMenu, mii.wID, FALSE, &mii))
		return;

	// Copy the items from the context menu
	bool sepPending = false;
	int n = GetMenuItemCount(contextMenu);
	int idx = 0;
	for (int i = 0; i < n; ++i)
	{
		// get this item
		mii.fMask = MIIM_ID | MIIM_FTYPE;
		if (GetMenuItemInfo(contextMenu, i, TRUE, &mii))
		{
			// check what we have
			if ((mii.fType & MFT_SEPARATOR) != 0)
			{
				// It's a separator.  Don't add it yet; just flag its
				// presence.  If we add any more commands, we'll add the
				// separator before adding the next command.
				sepPending = true;
			}
			else if (excludeCommandIDs.find(mii.wID) != excludeCommandIDs.end())
			{
				// This command is in the exclusion set - omit it
			}
			else
			{
				// It's a command, and it's not excluded, so add it.  If
				// we have a pending separator, add that first.
				if (sepPending)
				{
					AddSystemMenu(systemMenu, -1, idx++);
					sepPending = false;
				}

				// add the command item
				AddSystemMenu(systemMenu, mii.wID, idx++);
			}
		}
	}

	// If we added any commands, add a separator after the last one, to
	// separate our commands from the default items already in the menu.
	// Note that our last addition can't be a separator, because we only
	// separators just before commands, so we can be sure that the last
	// thing we added is a command as long as we added anything at all.
	if (idx != 0)
		AddSystemMenu(systemMenu, -1, idx++);

	// set the shortcut keys in the menu
	if (PlayfieldView *pfv = Application::Get()->GetPlayfieldView(); pfv != 0)
		pfv->UpdateMenuKeys(systemMenu);

}

void FrameWin::UpdateLayout()
{
	// resize the view window 
	if (view != 0)
	{
		// get the client area
		RECT rc;
		GetClientRect(hWnd, &rc);

		// adjust for the host framing
		rc.left += frameBorders.left;
		rc.top += frameBorders.top;
		rc.right -= frameBorders.right;
		rc.bottom -= frameBorders.bottom;

		// move the view
		SetWindowPos(view->GetHWnd(), NULL, rc.left, rc.top,
			rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
	}
}

void FrameWin::JsSetWindowPos(HWND hwndAfter, int x, int y, int cx, int cy, int flags)
{
	// if we're currently in full-screen mode, exit full-screen mode
	if (fullScreenMode)
		ToggleFullScreen();

	// if we're currently maximized or minimized, restore
	if (IsIconic(hWnd) || IsMaximized(hWnd))
		SendMessage(WM_SYSCOMMAND, SC_RESTORE);

	// set the position
	SetWindowPos(hWnd, hwndAfter, x, y, cx, cy, static_cast<UINT>(flags));
}

void FrameWin::JsSetWindowState(TSTRING state)
{
	// if we're currently in full-screen mode, exit full-screen mode
	if (fullScreenMode)
		ToggleFullScreen();

	// check for special state changes - min, max, restore
	std::transform(state.begin(), state.end(), state.begin(), ::_totlower);
	if (state == _T("min"))
	{
		// minimize
		SendMessage(WM_SYSCOMMAND, SC_MINIMIZE);
	}
	else if (state == _T("max"))
	{
		// maximize
		SendMessage(WM_SYSCOMMAND, SC_MAXIMIZE);
	}
	else if (state == _T("restore"))
	{
		// restore
		SendMessage(WM_SYSCOMMAND, SC_RESTORE);
	}
}

void FrameWin::ShowHideFrameWindow(bool show)
{
	// save the new state in the configuration
	ConfigManager::GetInstance()->Set(configVarVisible, show ? 1 : 0);

	// hide or show the window
	ShowWindow(hWnd, show ? SW_SHOW : SW_HIDE);

	// notify the view
	view->OnShowHideFrameWindow(show);
}

void FrameWin::RestoreVisibility()
{
	if (ConfigManager::GetInstance()->GetInt(configVarVisible, 1) != 0)
		ShowWindow(hWnd, SW_SHOWNOACTIVATE);
}

void FrameWin::SetBorderless(bool borderless)
{
	if (this->borderless != borderless)
		ToggleBorderless();
}

void FrameWin::ToggleBorderless(bool initing)
{
	// invert the state
	borderless = !borderless;

	// update the config
	ConfigManager::GetInstance()->SetBool(configVarBorderless, borderless);

	// refigure the window frame and caption layout
	FigureFrameParams();

	// redo the internal client layout
	UpdateLayout();

	// make sure the frame is redrawn
	SetWindowPos(hWnd, NULL, -1, -1, -1, -1,
		SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOSIZE | SWP_FRAMECHANGED);
}

void FrameWin::SetFullScreen(bool fullScreen)
{
	if (fullScreenMode != fullScreen)
		ToggleFullScreen();
}

void FrameWin::ToggleFullScreen(bool initing)
{
	// get our current window style
	DWORD style = GetWindowLong(hWnd, GWL_STYLE);

	// check the current mode
	if (!fullScreenMode)
	{
		// remember the original windowed position, so that we can restore the 
		// same position if we switch back to windowed mode later.
		normalWindowStyle = style;
		normalWindowPlacement.length = sizeof(normalWindowPlacement);
		if (!GetWindowPlacement(hWnd, &normalWindowPlacement))
		{
			// that failed - flag that the placement is invalid by zeroing
			// the length field
			normalWindowPlacement.length = 0;

			// log the error
			LogFile::Get()->Group();
			LogFile::Get()->Write(
				_T("Setting full-screen mode for %s: ")
				_T("no Window Placement information is available to save as the original position;\n")
				_T("the window might be at a different position when exiting full-screen mode\n\n"),
				logDesc.c_str());
		}

		// Figure the full-screen position.  If we're initializing,
		// use the saved position information; otherwise expand the
		// window to fill the current monitor it occupies.xs
		RECT rcFull;
		bool fsok = true;
		if (initing)
		{
			// startup mode - figure the full-screen position based
			// on the option settings
			fsok = GetFullScreenRestorePosition(&rcFull, nullptr);
		}
		else
		{
			// regular interactive switch to full-screen mode - use the
			// current window position
			MONITORINFO mi = { sizeof(mi) };
			if (GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &mi))
			{
				// got it - take over the whole viewing area of the selected monitor
				rcFull = mi.rcMonitor;
				fsok = true;
			}
		}

		// if we successfully retrieved a full-screen position, apply it
		if (fsok)
		{
			// we're now in full-screen mode
			fullScreenMode = true;

			// switch to a borderless popup window
			SetWindowLong(hWnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);

			// fill the monitor
			SetWindowPos(
				hWnd, HWND_TOP,
				rcFull.left, rcFull.top, 
				rcFull.right - rcFull.left, rcFull.bottom - rcFull.top,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

			// update the config with the new full-screen status
			ConfigManager::GetInstance()->SetBool(configVarFullScreen, true);

			// save the pixel coordinates of the new full-screen position, 
			// in case we want to restore this in the next session based 
			// on the exact coordinates
			ConfigManager::GetInstance()->Set(configVarFSPos, rcFull);

			// log it if in setup mode
			if (initing)
			{
				LogFile::Get()->Group(LogFile::WindowLayoutLogging);
				LogFile::Get()->Write(LogFile::WindowLayoutLogging,
					_T("Window setup: %s: Setting window to full-screen mode at %d, %d, %d, %d (size %d x %d)\n"),
					logDesc.c_str(),
					rcFull.left, rcFull.top, rcFull.right, rcFull.bottom,
					rcFull.right - rcFull.left, rcFull.bottom - rcFull.top);
			}
		}
		else
		{
			// unable to get monitor info - log an error
			LogFile::Get()->Group();
			LogFile::Get()->Write(
				_T("Setting full-screen mode for %s window: ")
				_T("unable to determine full-screen position\n"),
				logDesc.c_str());
		}
	}
	else
	{
		// We're currently in full screen mode - switch to windowed mode
		fullScreenMode = false;

		// Switch to an overlapped window
		SetWindowLong(hWnd, GWL_STYLE, normalWindowStyle | WS_VISIBLE);

		// If we have a previous position, restore it.  Otherwise simply shrink 
		// down a bit from the current size.
		if (normalWindowPlacement.length != 0)
		{
			// restore the old window position
			SetWindowPlacement(hWnd, &normalWindowPlacement);
		}
		else
		{
			// no saved window placement is available; keep at the current
			// position with a slight inset on each side so that it's clear
			// that it's no longer in full-screen mode
			RECT rc;
			GetWindowRect(hWnd, &rc);
			InsetRect(&rc, 32, 64);
			SetWindowPos(
				hWnd, HWND_TOP,
				rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		}

		// update the config to remove the full-screen mode
		ConfigManager::GetInstance()->SetBool(configVarFullScreen, false);

		// Re-build the system menu if necessary.  If we launch in
		// full-screen mode, we won't build the system menu initially
		// because there will be no system menu to build in a window
		// that doesn't have a caption bar.  So we'll have to build it
		// the first time we come out of FS mode.
		CustomizeSystemMenu(GetSystemMenu(hWnd, FALSE));
	}

	// refigure the window frame and caption layout
	FigureFrameParams();

	// redo the internal client layout
	UpdateLayout();

	// make sure the frame is redrawn
	SetWindowPos(hWnd, NULL, -1, -1, -1, -1,
		SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOSIZE | SWP_FRAMECHANGED);
}

// Handle an application foreground/background switch
void FrameWin::OnAppActivationChange(bool activating)
{
	// If we're in full-screen mode, do some extra work
	if (fullScreenMode)
	{
		// If the application is activating, explicitly restore full-screen mode
		// to re-trigger Windows side effects of full-screen sizing, such as 
		// hiding the taskbar.
		//
		// If the application is switching to the background, move full-screen
		// windows to the bottom of the Z order.
		if (activating)
			ReactivateFullScreen();
		else
			SetWindowPos(hWnd, HWND_BOTTOM, -1, -1, -1, -1, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
}

// Reactivate full-screen mode.  This is called whenever the application switches
// to the foreground (we're notified of this via WM_APPACTICVATE, which we handle
// in the Application object).  We reset the window placement to fill our primary
// monitor.  
//
// This is necessary because Windows does some special work in SetWindowPos that it
// *doesn't* do when the same window comes to the foreground with the same placement
// already set.  For example, if we're positioned on a "secondary" monitor (not the
// one designated as the primary desktop monitor in the Windows screen resolution
// control panel), Windows won't hide the taskbar on that monitor on an app switch.
//
// I consider it a Windows bug that we have to do this.  The side effects (like the
// taskbar hiding) should be part of the window state, not just momentary effects
// of calling a particular API.  But whatever you want to call it, we have to live
// with it, and this seems to be the way to live with it.
void FrameWin::ReactivateFullScreen()
{
	// proceed if we're in full screen mode and we have a valid normal window placement
	if (fullScreenMode && normalWindowPlacement.length != 0)
	{
		// get the monitor containing our normal window placement area
		MONITORINFO mi = { sizeof(mi) };
		if (GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &mi))
		{
			// Set it momentarily to a smaller size, so that the next SetWindowPos
			// actually has some work to do - Windows will ignore it otherwise.  The
			// additional size change doesn't seem to discernible as a discrete UI
			// change on the actual video display, so this won't cause any visual
			// hiccups, but it does make Windows do the extra work we want it to do
			// on the second SetWindowPos() below.
			SetWindowPos(
				hWnd, HWND_TOP,
				mi.rcMonitor.left, mi.rcMonitor.top,
				mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top - 1,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

			// set the window placement to fill the monitor
			SetWindowPos(
				hWnd, HWND_TOP,
				mi.rcMonitor.left, mi.rcMonitor.top,
				mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		}
	}
}

// Add our custom items to the system menu.  
void FrameWin::CustomizeSystemMenu(HMENU m)
{
	// Copy our context menu to the system menu, excluding the "Hide"
	// and "Exit commands, since these are both redundant with the
	// "Close" command on the standard system menu.
	std::unordered_set<UINT> exclude;
	exclude.emplace(ID_HIDE);
	exclude.emplace(ID_EXIT);
	CopyContextMenuToSystemMenu(view->GetContextMenu(), m, exclude);
}

bool FrameWin::OnActivate(int waCode, int minimized, HWND hWndOther)
{
	// adjust the frame borders
	FigureFrameParams();

	// check the activation state
	switch (waCode)
	{
	case WA_ACTIVE:
	case WA_CLICKACTIVE:
		// set focus on the view
		if (view != 0)
			SetFocus(view->GetHWnd());

		// handled
		return true;
	}

	// use the base class handling
	return __super::OnActivate(waCode, minimized, hWndOther);
}


// Closing only hides the window
bool FrameWin::OnClose()
{
	// if this is a hideable window, hide it instead of actually closing it
	if (IsHideable())
	{
		// hide the window
		ShowHideFrameWindow(false);

		// skip the default system processing
		return true;
	}

	// otherwise use the default handling
	return __super::OnClose();
}

bool FrameWin::OnDestroy()
{
	// Destroy the vanity window if it's still around.  It *shouldn't*
	// be, since we should have removed it as soon as our own window
	// was fully initialized, but it's conceivable that we prematurely
	// aborted the window creation process due to error or user
	// cancellation.
	if (vanityShield != nullptr)
	{
		HWND vanityHWnd = vanityShield->GetHWnd();
		vanityShield = nullptr;
		DestroyWindow(vanityHWnd);
	}

	// do the base class work
	return __super::OnDestroy();
}

bool FrameWin::OnCommand(int cmd, int source, HWND hwndControl)
{
	return DoCommand(cmd) || __super::OnCommand(cmd, source, hwndControl);
}

bool FrameWin::OnSysCommand(WPARAM wParam, LPARAM lParam)
{
	// Run it through the regular command handler first, to
	// process custom commands we add to the system menu.
	if (DoCommand(LOWORD(wParam)))
		return true;

	// if the window can be hidden, hide it on minimize or close, unhide on restore
	if (WORD sc = LOWORD(wParam); (sc == SC_MINIMIZE || sc == SC_CLOSE) && IsHideable())
	{
		ShowHideFrameWindow(false);
		return true;
	}
	else if (sc == SC_RESTORE && IsHideable())
	{
		ShowHideFrameWindow(true);
	}

	// inherit the default handling
	return __super::OnSysCommand(wParam, lParam);
}

bool FrameWin::DoCommand(int cmd)
{
	switch (cmd)
	{
	case ID_ABOUT:
	case ID_HELP:
	case ID_OPTIONS:
		// forward to the main playfield view
		if (PlayfieldView *pfv = Application::Get()->GetPlayfieldView(); pfv != nullptr)
			pfv->SendMessage(WM_COMMAND, cmd);
		return true;

	case ID_EXIT:
		// close the main playfield window
		if (PlayfieldWin *pfw = Application::Get()->GetPlayfieldWin(); pfw != nullptr)
			pfw->PostMessage(WM_CLOSE);
		return true;

	case ID_HIDE:
		ShowHideFrameWindow(false);
		return true;

	case ID_FULL_SCREEN:
		ToggleFullScreen();
		return true;

	case ID_FULL_SCREEN_INIT:
		ToggleFullScreen(true);
		return true;

	case ID_WINDOW_BORDERS:
		ToggleBorderless();
		return true;

	case ID_WINDOW_BORDERS_INIT:
		ToggleBorderless(true);
		return true;

	case ID_VIEW_BACKGLASS:
		Application::Get()->ShowWindow(Application::Get()->GetBackglassWin());
		return true;

	case ID_VIEW_DMD:
		Application::Get()->ShowWindow(Application::Get()->GetDMDWin());
        return true;

    case ID_VIEW_TOPPER:
        Application::Get()->ShowWindow(Application::Get()->GetTopperWin());
        return true;

    case ID_VIEW_INSTCARD:
        Application::Get()->ShowWindow(Application::Get()->GetInstCardWin());
        return true;

	case ID_VIEW_PLAYFIELD:
		Application::Get()->ShowWindow(Application::Get()->GetPlayfieldWin());
		return true;

	case ID_FPS:
	case ID_ROTATE_CW:
	case ID_ROTATE_CCW:
		// forward these to our child view
		if (view != nullptr)
            view->SendMessage(WM_COMMAND, cmd);
		return true;

	case ID_RESTORE_VISIBILITY:
		RestoreVisibility();
		return true;
	}

	// not handled
	return false;
}

bool FrameWin::OnInitMenuPopup(HMENU hMenu, int itemPos, bool isWinMenu)
{
	// If it's the system menu, have the child view update the menu
	// item status.
	if (hMenu == GetSystemMenu(hWnd, FALSE) && view != nullptr)
		view->UpdateMenu(hMenu, this);

	// reset the attract mode timer in the main window
	if (auto pfv = Application::Get()->GetPlayfieldView(); pfv != nullptr)
		pfv->ResetAttractMode();

	// inherit the default handling
	return __super::OnInitMenuPopup(hMenu, itemPos, isWinMenu);
}


bool FrameWin::OnWindowPosChanging(WINDOWPOS *pos)
{
	// if we're changing the Z order, and we have a vanity shield, make
	// sure we stay behind the vanity shield
	if ((pos->flags & SWP_NOZORDER) == 0 && vanityShield != nullptr)
		pos->hwndInsertAfter = vanityShield->GetHWnd();

	// inherit the default processing
	return __super::OnWindowPosChanging(pos);
}

void FrameWin::OnMove(POINT pos)
{
	// do the base class work
	__super::OnMove(pos);

	// save position changes to the config
	WindowPosToConfig();
}

void FrameWin::OnResize(int width, int height)
{
	// do the base class work
	__super::OnResize(width, height);

	// store the new size
	WindowPosToConfig();

	// make sure we redraw
	InvalidateRect(hWnd, 0, TRUE);

	// update the frame layout
	UpdateLayout();
}

bool FrameWin::OnGetMinMaxInfo(MINMAXINFO *mmi)
{
	mmi->ptMinTrackSize.x = 200;
	mmi->ptMinTrackSize.y = 200;
	return true;
}

bool FrameWin::OnCreate(CREATESTRUCT *cs)
{
	// do the base class work
	__super::OnCreate(cs);

	// Explicitly recalculate the frame
	RECT rc;
	GetWindowRect(hWnd, &rc);
	SetWindowPos(hWnd, HWND_TOP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_FRAMECHANGED);

	// allow the system handler to proceed
	return false;
}

bool FrameWin::OnNCActivate(bool active, HRGN updateRgn)
{
	// do the base class work
	bool ret = __super::OnNCActivate(active, updateRgn);

	// set our internal activation flag
	isActivated = active;

	// invalidate the caption rect so that we redraw it with the new status
	if (frameBorders.top != 0)
	{
		RECT rc;
		GetClientRect(hWnd, &rc);
		rc.bottom = frameBorders.top;
		InvalidateRect(hWnd, &rc, FALSE);
	}

	// udpate our application foreground status
	Application::Get()->CheckForegroundStatus();

	// return the base class result
	return ret;
}

bool FrameWin::OnNCMouseButtonUp(int button, UINT hit, POINT pt)
{
	// show the system menu if right-clicking in the non-client area
	if (button == MouseButton::mbRight)
		ShowSystemMenu(pt.x, pt.y);

	// run the default handling as well
	return __super::OnNCMouseButtonUp(button, hit, pt);
}


LRESULT FrameWin::WndProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	// Run the message through the DWM handler, in case we're extending
	// the frame into the client area.  (The host decides whether to do
	// that; if it does, this call is required, and if it doesn't, this
	// call will have no effect but will be harmless.)
	curMsg->dwmHandled = DwmDefWindowProc(hWnd, message, wParam, lParam, &curMsg->dwmResult);

	// do some special DWM-related handling for certain messages
	switch (message)
	{
	case WM_NCHITTEST:
	case WM_NCMOUSELEAVE:
		// If DWM claimed the message, don't do any more processing
		if (curMsg->dwmHandled)
			return curMsg->dwmResult;
		break;
	}

	// inherit the base class handling
	return __super::WndProc(message, wParam, lParam);
}

// -----------------------------------------------------------------------
//
// Non-client frame customization
//
// For visual styling, we do some very slight customization of the
// window frame, using the Windows DWM API.  Here's the basic idea:
//
// - We use WM_NCCALCSIZE in the window proc to make the entire
//   window area "client" space.  This removes all "non-client"
//   space from the window, which makes the entire window area
//   available for regular painting.
//
// - We then use DwmExtendFrameIntoClientArea() to extend the 
//   system window frame drawing into the client area by the
//   normal frame width.  This essentially reverses the effect
//   of making the whole window into client area by giving part
//   of the client area to DWM to draw the frame controls.
//
// - Calling DwmExtendFrameIntoClient() has the side effect that
//   it makes the system draw the normal sizing borders, caption
//   bar background, and caption bar buttons (minimize, maximize,
//   close) when handling WM_ERASEBKGND.  However, it DOESN'T
//   draw the title text or window/system menu icon.
//
// - In our WM_PAINT handler, we draw the title bar text and the
//   window/system menu icon.  We have to do this because the
//   system won't do it automatically thanks to our call to
//   DwmExtendFrameIntoClient().  This is actually the whole
//   point of doing the customization in the first place: we want
//   to draw a larger than normal icon, and draw the window text
//   at the left side.
//
// - As documented in the Win API docs, we have to call
//   DwmDefWindowProc() in our message handler.  This processes
//   hits on the min/max/close buttons.
//
// - In our WM_NCHITTEST handler, we have to do additional testing
//   for hits to the caption bar, sizing borders, and system menu
//   icon area.  DwmDefWindowProc() doesn't handle those.
//
// - CUSTOMIZATION NOTE:  To change this to completely customize
//   ALL frame drawing, set all MARGINS elements to zero in the call
//   to DwmExtendFrameIntoClient.  This will make the system frame
//   zero width all around, which will prevent any of the controls
//   from being drawn.  We'll have to draw everything in this case,
//   including the sizing border background colors, the caption bar
//   background, and the min/max/close boxes.  We'll also have to
//   do all mouse tracking for the min/max/close boxes, since the 
//   normal system controls that DwmDefWindowProc() is meant to
//   track for us won't exist.
//
void FrameWin::FigureFrameParams()
{
	if (IsFullScreen())
	{
		// full screen mode - there are no frame controls in this mode
		SetRect(&frameBorders, 0, 0, 0, 0);
		captionOfs.x = captionOfs.y = 0;
	}
	else if (IsBorderless())
	{
		// borderless mode - omit the frame controls
		SetRect(&frameBorders, 0, 0, 0, 0);
		captionOfs.x = captionOfs.y = 0;
	}
	else
	{
		// figure the normal caption and border area, by adjusting
		// an empty client rectangle to a window rect
		DWORD dwStyle = GetWindowLong(hWnd, GWL_STYLE);
		DWORD dwExStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
		SetRect(&frameBorders, 0, 0, 0, 0);
		AdjustWindowRectEx(&frameBorders, dwStyle, FALSE, dwExStyle);

		// the left and top will be adjusted in the negative direction, so
		// negate these to get the sizes
		frameBorders.left *= -1;
		frameBorders.top *= -1;

		// For compatibility with Windows 10, we have to use 0 margins on
		// the left, right, and bottom.  We're still allowed to inset the
		// top for an extended title bar area, but we have to leave all of
		// the other margins at 0.  Windows 10 uses a new scheme for the
		// sizing borders, where the borders are drawn *outside* of the
		// window rect instead of inside it.  (Actually, *mostly* outside.
		// There's still a 1-pixel border inset into the window rect.)
		// Now, you'd think that the Windows 10 AdjustWindowRectEx() would
		// take that into account and tell us that the sizing borders are 
		// 1 pixel wide, but you'd be wrong!  It reports the size of the
		// outset, without telling us that it's an outset.  So we have to
		// explicitly override that result and fix the borders at 0.
		frameBorders.left = 0;
		frameBorders.right = 0;
		frameBorders.bottom = 0;

		// figure the inset for just the borders, sans caption
		RECT rcBorders;
		SetRect(&rcBorders, 0, 0, 0, 0);
		AdjustWindowRectEx(&rcBorders, dwStyle & ~WS_CAPTION, FALSE, dwExStyle);

		// figure the caption area
		captionOfs.x = -rcBorders.left;
		captionOfs.y = -rcBorders.top;
	}

	// adjust the caption offset for the icon
	captionOfs.x += szIcon.cx;

	// adjust the caption offset for a margin between icon and title
	captionOfs.x += 4;

	// set the frame margins in the DWM
    MARGINS frameMargins = { frameBorders.left, frameBorders.right, frameBorders.top, frameBorders.bottom };
	dwmExtended = SUCCEEDED(DwmExtendFrameIntoClientArea(hWnd, &frameMargins));

	// check if the DWM extension succeeded
	if (dwmExtended)
	{
		// Success.  We'll now do our own custom drawing for the caption
		// bar title and icon, so turn them off in the window manager.
		SetWindowThemeNonClientAttributes(hWnd,
			WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON | WTNCA_NOSYSMENU,
			WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON | WTNCA_NOSYSMENU);
	}
	else
	{
		// DWM frame extension failed.  Our client area is simply the
		// normal client area with the default NC framing, so turn off
		// all frame borders.
		frameBorders = { 0, 0, 0, 0 };
		captionOfs = { 0, 0 };
	}

	// If we're in borderless mode, set a null window region, to defeat
	// the rounded corners in the Windows 7 standard window style.  The
	// rounded corners are designed for the standard frame border, and
	// don't look right when there's no frame, but Windows applies them
	// unconditionally to all windows, standard frame or no.  So we have
	// to remove them explicitly when we don't want them.  The rounded
	// corners in Win 7 are implemented via a window region that creates
	// a transparent area around the rounded corners, so we can remove
	// them by setting a null window region, which makes the entire
	// window rectangle opaque again.
	if (IsBorderless())
		SetWindowRgn(hWnd, NULL, false);
}

bool FrameWin::OnNCCalcSize(bool validateClientRects, NCCALCSIZE_PARAMS *p, UINT &wvrFlagsResult)
{
	// If we're borderless, claim the entire window rect as client
	// area.  We can do this simply by returning the rectangles as
	// passed in from Windows.
	if (IsBorderless())
		return true;

	// if we're not using DWM frame extension, use the normal system
	// default handling to draw the normal frame caption and borders
	if (!dwmExtended)
		return false;

	// In the validateClientRects case, fix up the frame to reflect
	// the frame incursion we requested from DWM.
	if (validateClientRects)
	{
		// get the original proposed window rect
		RECT rcOrig = p->rgrc[0];

		// Get the standard sizes first.  We have to let the system 
		// calculate the initial values to accommodate the differences
		// in handling in Windows 7, 8, and 10.
		DefWindowProc(hWnd, WM_NCCALCSIZE, (WPARAM)validateClientRects, (LPARAM)p);

		// extend the client area all the way to the top edge
		p->rgrc[0].top = rcOrig.top;

		// we've handled it
		return true;
	}

	// use the default handling
	return false;
}

// Because we're doing custom framing, we also need to do our
// own non-client hit testing.
bool FrameWin::OnNCHitTest(POINT ptMouse, UINT &hit)
{
	// get the window rect 
	RECT rcWindow;
	GetWindowRect(hWnd, &rcWindow);

	// Figure the sizing border area based on the window style.  Note 
	// that we want only the border here, so exclude the 'caption' style
	// from the query.  The result (rcFrame) is the window rect for an
	// imaginary 0x0 client rect, so the elements of rcFrame aren't the
	// border widths per se, they're the *difference from zero*.  That
	// means that top and left will be the negative border widths, and
	// bottom and right will be the positive border widths.
	RECT rcFrame = { 0, 0, 0, 0 };
	DWORD dwStyle = GetWindowLong(hWnd, GWL_STYLE);
	DWORD dwExStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
	AdjustWindowRectEx(&rcFrame, dwStyle & ~WS_CAPTION, FALSE, dwExStyle);

	// Check if we're in the top or bottom border area
	USHORT uRow = 1;
	if (ptMouse.y >= rcWindow.top && ptMouse.y < rcWindow.top - rcFrame.top)
		uRow = 0;
	else if (ptMouse.y < rcWindow.bottom && ptMouse.y >= rcWindow.bottom - rcFrame.bottom)
		uRow = 2;

	// Check if we're in the left or right border area
	USHORT uCol = 1;
	if (ptMouse.x >= rcWindow.left && ptMouse.x < rcWindow.left - rcFrame.left)
		uCol = 0;
	else if (ptMouse.x < rcWindow.right && ptMouse.x >= rcWindow.right - rcFrame.right)
		uCol = 2;

	// Now use the combination of top/bottom and left/right to see which
	// specific border zone we're in
	static const UINT hitTests[3][3] =
	{
		{ HTTOPLEFT,    HTTOP,     HTTOPRIGHT },
		{ HTLEFT,       HTNOWHERE, HTRIGHT },
		{ HTBOTTOMLEFT, HTBOTTOM,  HTBOTTOMRIGHT },
	};
	hit = hitTests[uRow][uCol];

	// if it's not in the border, check the caption
	if (hit == HTNOWHERE && ptMouse.y >= rcWindow.top && ptMouse.y < rcWindow.top + frameBorders.top)
	{
		// it's in the caption - check if it's in the system menu
		hit = ptMouse.x >= rcWindow.left && ptMouse.x < rcWindow.left + szIcon.cx
			? HTSYSMENU : HTCAPTION;
	}

	// if we found a hit other than "nowhere", consider it handled
	return hit != HTNOWHERE;
}

// -----------------------------------------------------------------------
// 
// Paint the client area
//
void FrameWin::OnPaint(HDC hdc)
{
	// paint the caption area if using DWM mode
	if (dwmExtended && !IsBorderless())
		PaintCaption(hdc);
}


//
// Paint our custom caption
//
void FrameWin::PaintCaption(HDC hdc)
{
	// get the client area
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);

	// get the normal window theme data
	HTHEME hTheme = OpenThemeData(NULL, L"CompositedWindow::Window");
	if (hTheme != 0)
	{
		// get the window title
		TCHAR title[256];
		GetWindowTextW(hWnd, title, countof(title));

		// create a painting DC
		HDC hdcPaint = CreateCompatibleDC(hdc);
		if (hdcPaint != 0)
		{
			// get the caption area
			int cx = rcClient.right - rcClient.left - frameBorders.left - frameBorders.right;
			int cy = frameBorders.top - captionOfs.y;

			// Set up the BITMAPINFO for drawing text.  biHeight is
			// negative, because DrawThemeTextEx() requires top-to-bottom
			// orientation for the bitmap.
			// order.
			BITMAPINFO dib = { 0 };
			dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			dib.bmiHeader.biWidth = cx;
			dib.bmiHeader.biHeight = -cy;
			dib.bmiHeader.biPlanes = 1;
			dib.bmiHeader.biBitCount = 32;
			dib.bmiHeader.biCompression = BI_RGB;

			// create the DIB for the bitmap
			HBITMAP hbm = CreateDIBSection(hdc, &dib, DIB_RGB_COLORS, NULL, NULL, 0);
			if (hbm != 0)
			{
				HBITMAP hbmOld = (HBITMAP)SelectObject(hdcPaint, hbm);

				// Set up the theme drawing options
				DTTOPTS DttOpts = { sizeof(DTTOPTS) };
				DttOpts.dwFlags = DTT_COMPOSITED | DTT_GLOWSIZE;
				DttOpts.iGlowSize = 15;

				// Select the theme font
				LOGFONT lgFont;
				HFONT hFont = NULL;
				HFONT hFontOld = NULL;
				if (SUCCEEDED(GetThemeSysFont(hTheme, TMT_CAPTIONFONT, &lgFont)))
				{
					hFont = CreateFontIndirect(&lgFont);
					hFontOld = (HFONT)SelectObject(hdcPaint, hFont);
				}

				// Draw the caption
				RECT rcPaint = rcClient;
				rcPaint.bottom = rcPaint.top + cy;
				DrawThemeTextEx(hTheme,
					hdcPaint,
					0, 0,
					title,
					-1,
					DT_LEFT | DT_VCENTER | DT_WORD_ELLIPSIS,
					&rcPaint,
					&DttOpts);

				// Blt text to the frame
				BitBlt(hdc, captionOfs.x, captionOfs.y, cx, cy, hdcPaint, 0, 0, SRCCOPY);

				// clean up the temporary DC
                if (hFontOld != NULL)
                {
                    SelectObject(hdcPaint, hFontOld);
                    DeleteObject(hFont);
                }
                SelectObject(hdcPaint, hbmOld);
				DeleteObject(hbm);
			}

			// done with the painting DC
			DeleteDC(hdcPaint);
		}

		// done with the theme data
		CloseThemeData(hTheme);
	}

	// draw the icon
	int iconOfs = IsMaximized(hWnd) ? GetSystemMetrics(SM_CXDLGFRAME) : 0;
	DrawIconEx(hdc, iconOfs + 2, iconOfs + (frameBorders.top - iconOfs - szIcon.cy) / 2,
		GetActiveWindow() == hWnd ? icon : grayIcon,
		szIcon.cx, szIcon.cy, iconOfs, 0, DI_NORMAL);
}


// -----------------------------------------------------------------------
//
// Show the system menu
//
void FrameWin::ShowSystemMenu(int x, int y)
{
	// get the system menu
	HMENU m = GetSystemMenu(hWnd, FALSE);

	// update our commands via the child
	if (view != 0)
		view->UpdateMenu(m, this);

	// enable all of the system commands
	MENUITEMINFO mii;
	mii.cbSize = sizeof(MENUITEMINFO);
	mii.fMask = MIIM_STATE;
	mii.fType = 0;
	mii.fState = MF_ENABLED;
	SetMenuItemInfo(m, SC_RESTORE, FALSE, &mii);
	SetMenuItemInfo(m, SC_SIZE, FALSE, &mii);
	SetMenuItemInfo(m, SC_MOVE, FALSE, &mii);
	SetMenuItemInfo(m, SC_MAXIMIZE, FALSE, &mii);
	SetMenuItemInfo(m, SC_MINIMIZE, FALSE, &mii);

	// get the current window placement so we can update the system commands
	WINDOWPLACEMENT wp;
	GetWindowPlacement(hWnd, &wp);

	// gray commands that need graying
	mii.fState = MF_GRAYED;
	switch (wp.showCmd)
	{
	case SW_SHOWMAXIMIZED:
		SetMenuItemInfo(m, SC_SIZE, FALSE, &mii);
		SetMenuItemInfo(m, SC_MOVE, FALSE, &mii);
		SetMenuItemInfo(m, SC_MAXIMIZE, FALSE, &mii);
		break;

	case SW_SHOWMINIMIZED:
		SetMenuItemInfo(m, SC_MINIMIZE, FALSE, &mii);
		SetMenuDefaultItem(m, SC_RESTORE, FALSE);
		break;

	case SW_SHOWNORMAL:
		SetMenuItemInfo(m, SC_RESTORE, FALSE, &mii);
		SetMenuDefaultItem(m, SC_CLOSE, FALSE);
		break;
	}

	// track it
	LPARAM cmd = TrackPopupMenu(m,
		TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
		x, y, 0, hWnd, 0);

	// check what we got
	switch (cmd)
	{
	case 0:
		// canceled/error - no command to process
		break;

	case SC_RESTORE:
	case SC_SIZE:
	case SC_MOVE:
	case SC_MAXIMIZE:
	case SC_MINIMIZE:
	case SC_CLOSE:
		// system command - process via WM_SYSCOMMAND
		PostMessage(WM_SYSCOMMAND, cmd);
		break;

	default:
		// Anything else is one of our custom commands.  Process it
		// through the player view child window.  (If it's actually
		// a command we handle, the view will forward it back to us,
		// so we don't need to check which one it is here.)
		if (view != 0)
			view->PostMessage(WM_COMMAND, cmd);
		break;
	}
}

// Save the (non-full-screen) window position in the config 
void FrameWin::WindowPosToConfig()
{
	// only proceed if in windowed mode
	if (!fullScreenMode)
	{
		// windowed mode - save WindowPos
		RECT rc;
		GetWindowRect(hWnd, &rc);

		// only save it if it's actually different from the config
		ConfigManager *cm = ConfigManager::GetInstance();
		if (rc != cm->GetRect(configVarPos)	
			|| cm->GetInt(configVarMaximized) != IsZoomed(hWnd)
			|| cm->GetInt(configVarMinimized) != IsIconic(hWnd))
		{
			// store the new position rect, unless it's maximized or minimized
			if (!IsZoomed(hWnd) && !IsIconic(hWnd))
				cm->Set(configVarPos, rc);

			// note maximized and minimized modes
			cm->Set(configVarMaximized, IsZoomed(hWnd));
			cm->Set(configVarMinimized, IsIconic(hWnd));
		}
	}
}

// Get the bounding rectangle of the nth monitor (n >= 1), in desktop
// window coordinates.  Returns true if the given monitor was found,
// false if no such monitor exists.
bool FrameWin::GetDisplayMonitorCoords(int n, RECT &rc)
{
	// enumeration callback struct
	struct EnumDisplayMonitorsCtx
	{
		EnumDisplayMonitorsCtx(int target, RECT &rc) : rc(rc)
		{
			this->target = target;
			cur = 0;
			found = false;
		}
		RECT &rc;		// rectangle of discovered monitor
		int target;		// target monitor number (>= 1)
		int cur;		// current enumeration monitor number
		bool found;		// did we find a match?
	} ctx(n, rc);

	// enumerate monitors
	EnumDisplayMonitors(
		0, 0,
		[](HMONITOR, HDC, LPRECT lprcMonitor, LPARAM lparam)->BOOL
	{
		// get the context
		EnumDisplayMonitorsCtx *ctx = (EnumDisplayMonitorsCtx *)lparam;

		// count it
		ctx->cur++;

		// if it's the one we're looking for, we're done
		if (ctx->cur == ctx->target)
		{
			// it's the one - note its bounds
			ctx->rc = *lprcMonitor;
			ctx->found = true;

			// no need to search any further
			return FALSE;
		}

		// these aren't the droids... keep searching
		return TRUE;
	}, LPARAM(&ctx));

	// return the result
	return ctx.found;
}

void FrameWin::AddSystemMenu(HMENU m, int cmd, int idx)
{
	// set up the basic menu item descriptor struct
	MENUITEMINFO mii = { sizeof(mii) };

	// cmd == -1 means add a separator
	if (cmd == -1)
	{
		// add a separator
		mii.fMask = MIIM_FTYPE;
		mii.fType = MFT_SEPARATOR;
		InsertMenuItem(m, idx, TRUE, &mii);
	}
	else if (view != 0)
	{
		// find the command on our regular menu to get its text
		TCHAR buf[256];
		mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP | MIIM_CHECKMARKS;
		mii.dwTypeData = buf;
		mii.cch = countof(buf);
		if (GetMenuItemInfo(view->GetContextMenu(), cmd, FALSE, &mii))
		{
			// add the item to our menu
			mii.fType = MFT_STRING;
			mii.wID = cmd;
			InsertMenuItem(m, idx, TRUE, &mii);
		}
	}
}

// private window messages (WM_USER .. WM_APP-1)
bool FrameWin::OnUserMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case FWRemoveVanityShield:
		// close the vanity shield window if present
		if (vanityShield != nullptr)
		{
			// The order here is somewhat important to avoid drawing glitches
			// (and avoiding drawing glitches is the whole point of the vanity
			// shield window, so it would be a shame to let glitches happen
			// during its removal).  First, forget the vanity shield, so that
			// our WM_WINDOWPOSCHANGING handler won't think the vanity shield
			// is still present while rearranging things during the vanity
			// window destruction.
			HWND vanityHWnd = vanityShield->GetHWnd();
			vanityShield = nullptr;

			// now flush the desktop window manager, which will sync window
			// drawing with the monitor refresh cycle
			DwmFlush();

			// and finally, remove the vanity window
			DestroyWindow(vanityHWnd);

			// restore normal window transition animations, which we disabled
			// until the vanity shield was removed
			int value = 0;
			DwmSetWindowAttribute(hWnd, DWMWA_TRANSITIONS_FORCEDISABLED, &value, sizeof(value));
		}
		return true;
	}

	// inherit the default handling
	return __super::OnUserMessage(msg, wParam, lParam);
}


// private app messages (WM_APP+)
bool FrameWin::OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case PWM_ISBORDERLESS:
		// borderless mode query
		curMsg->lResult = IsBorderless();
		return true;

	case PWM_ISFULLSCREEN:
		// full screen mode query
		curMsg->lResult = IsFullScreen();
		return true;
	}

	// inherit default handling
	return __super::OnAppMessage(msg, wParam, lParam);
}

void FrameWin::SavePreRunPlacement()
{
	// get the current placement data
	preRunPlacement.length = sizeof(preRunPlacement);
	if (GetWindowPlacement(hWnd, &preRunPlacement))
	{
		// if the window is hidden, keep it hidden when restored
		if (!IsWindowVisible(hWnd))
			preRunPlacement.showCmd = SW_HIDE;
	}
	else
	{
		// failed to get the placement - flag that the placement
		// information is invalid by zeroing the length
		preRunPlacement.length = 0;
	}
}

void FrameWin::RestorePreRunPlacement()
{
	// if we have valid placement data, apply it
	if (preRunPlacement.length != 0)
	{
		// restore the saved placement
		SetWindowPlacement(hWnd, &preRunPlacement);

		// clear it so that we don't try to use it again
		preRunPlacement.length = 0;
	}
}
