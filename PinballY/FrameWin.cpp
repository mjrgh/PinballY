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

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Uxtheme.lib")

// config variable names
namespace ConfigVars
{
	static const TCHAR *FullScreen = _T("FullScreen");
	static const TCHAR *WindowPos = _T("Position");
	static const TCHAR *WindowMaximized = _T("Maximized");
	static const TCHAR *WindowMinimized = _T("Minimized");
	static const TCHAR *WindowVisible = _T("Visible");
}

// statics
bool FrameWin::frameWinClassRegistered = false;
const TCHAR *FrameWin::frameWinClassName = _T("PinballY.FrameWinClass");
ATOM FrameWin::frameWinClassAtom = 0;

FrameWin::FrameWin(const TCHAR *configVarPrefix, int iconId, int grayIconId) : BaseWin(0)
{
	// generate the config var names
	configVarPos = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::WindowPos);
	configVarFullScreen = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::FullScreen);
	configVarMinimized = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::WindowMinimized);
	configVarMaximized = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::WindowMaximized);
	configVarVisible = MsgFmt(_T("%s.%s"), configVarPrefix, ConfigVars::WindowVisible);
	
	// clear variables
	isActivated = false;
	normalWindowPlacement.length = 0;
	dwmExtended = false;

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
	fullScreenMode = false;

	// If a full-screen window was requested, look up the display by index.
	// If the lookup fails, revert to the default coordinates.
	bool cfgFullScreen = ConfigManager::GetInstance()->GetInt(configVarFullScreen) != 0;
	if (cfgFullScreen)
	{
		// get the normal screen area
		RECT rc = ConfigManager::GetInstance()->GetRect(configVarPos, pos);

		// find the monitor containing this area
		HMONITOR hmon;
		MONITORINFO mi = { sizeof(mi) };
		if ((hmon = MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) != 0 && GetMonitorInfo(hmon, &mi))
		{
			// got it - use full-screen mode within this monitor's display area
			fullScreenMode = true;
			pos = mi.rcMonitor;
		}
	}

	// if we didn't end up in full-screen mode, look for a window location
	// in the config instead
	if (!fullScreenMode)
	{
		// get the stored window location
		RECT rc = ConfigManager::GetInstance()->GetRect(configVarPos, pos);

		// get the maximized and minimized states
		if (ConfigManager::GetInstance()->GetInt(configVarMaximized, 0))
			nCmdShow = SW_MAXIMIZE;
		else if (ConfigManager::GetInstance()->GetInt(configVarMinimized, 0))
			nCmdShow = SW_MINIMIZE;

		// see if we read a non-default location
		if (rc.left != CW_USEDEFAULT || rc.right != CW_USEDEFAULT || rc.right != 0 || rc.bottom != 0)
		{
			// make sure it's within the virtual screen bounds
			RECT vsrc;
			vsrc.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
			vsrc.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
			vsrc.right = vsrc.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
			vsrc.bottom = vsrc.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
			if (rc.right < rc.left + 50)
				rc.right = rc.left + 50;						// minimum width
			if (rc.bottom < rc.top + 50)
				rc.bottom = rc.top + 50;						// minimum height
			if (rc.left != CW_USEDEFAULT && rc.left < vsrc.left)
			{
				rc.right = vsrc.left + (rc.right - rc.left);    // maintain the current width
				rc.left = vsrc.left;							// force to the left edge of the virtual screen
			}
			if (rc.top != CW_USEDEFAULT && rc.top < vsrc.top)
			{
				rc.bottom = vsrc.top + (rc.bottom - rc.top);	// maintain the current height
				rc.top = vsrc.top;								// force to the top edge of the virtual screen
			}
			if (rc.left != CW_USEDEFAULT && rc.right != 0 && rc.right > vsrc.right)
			{
				rc.left = vsrc.right - (rc.right - rc.left);	// maintain the current width
				rc.right = vsrc.right;							// force to the right edge of the virtual screen
			}
			if (rc.top != CW_USEDEFAULT && rc.bottom != 0 && rc.bottom > vsrc.bottom)
			{
				rc.top = vsrc.bottom - (rc.bottom - rc.top);	// maintain the current height
				rc.bottom = vsrc.bottom;						// force to the bottom edge of the virtual screen
			}

			// apply the new size
			pos = rc;
		}
	}

	// return the position
	return pos;
}

// initialize the window
bool FrameWin::InitWin()
{
	// do the base class work
	if (!__super::InitWin())
		return false;

	// create my view
	view.Attach(CreateViewWin());
	if (view == 0)
		return false;

	// switch to full-screen style if initially in full-screen mode
	if (fullScreenMode)
	{
		// set styles for full-screen mode
		SetWindowLong(hWnd, GWL_STYLE,
			(GetWindowLong(hWnd, GWL_STYLE) & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);

		// refigure the frame borders
		SetWindowPos(
			hWnd, NULL, -1, -1, -1, -1,
			SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOMOVE | SWP_FRAMECHANGED);

	}

	// If we're not starting in full-screen mode, initialize the system menu.
	// There's no need to do this in full-screen mode because there's no system
	// menu at all in FS mode.
	if (!fullScreenMode)
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
		| WS_SIZEBOX | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;

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

	// the view controls some of the state, so have it make further updates
	if (view != fromWin)
		view->UpdateMenu(hMenu, this);
}

void FrameWin::CopyContextMenuToSystemMenu(HMENU contextMenu, HMENU systemMenu,
	const std::unordered_set<UINT> &excludeCommandIDs)
{
	// if there's no system menu, there's nothing to do
	if (systemMenu == 0)
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

void FrameWin::ToggleFullScreen()
{
	// get our current window style
	DWORD style = GetWindowLong(hWnd, GWL_STYLE);

	// check the current mode
	if (!fullScreenMode)
	{
		// We're currently in windowed mode - switch to full screen.
		// Remember the current window placement so that we can restore it if we
		// later switch back to windowed mode.  Then determine which monitor the
		// window currently occupies, using the primary monitor by default.
		normalWindowStyle = style;
		normalWindowPlacement.length = sizeof(normalWindowPlacement);
		MONITORINFO mi = { sizeof(mi) };
		if (GetWindowPlacement(hWnd, &normalWindowPlacement)
			&& GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &mi))
		{
			// we're now in full-screen mode
			fullScreenMode = true;

			// switch to a borderless popup window
			SetWindowLong(hWnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);

			// fill the monitor
			SetWindowPos(
				hWnd, HWND_TOP,
				mi.rcMonitor.left, mi.rcMonitor.top,
				mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

			// update the config
			ConfigManager::GetInstance()->Set(configVarFullScreen, 1);
		}
	}
	else
	{
		// We're currently in full screen mode - switch to windowed mode.
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
			RECT rc;
			GetWindowRect(hWnd, &rc);
			InsetRect(&rc, 32, 64);
			SetWindowPos(
				hWnd, HWND_TOP,
				rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_DRAWFRAME);
		}

		// update the config to remove the full-screen mode
		ConfigManager::GetInstance()->Set(configVarFullScreen, 0);

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

	// if the window can be hidden, hide it on minmize or close
	if (WORD sc = LOWORD(wParam); (sc == SC_MINIMIZE || sc == SC_CLOSE) && IsHideable())
	{
		ShowHideFrameWindow(false);
		return true;
	}

	// inherit the default handling
	return __super::OnSysCommand(wParam, lParam);
}

bool FrameWin::DoCommand(int cmd)
{
	switch (cmd)
	{
	case ID_ABOUT:
		if (PlayfieldView *pfv = Application::Get()->GetPlayfieldView(); pfv != 0)
			pfv->SendMessage(WM_COMMAND, ID_ABOUT);
		return true;

	case ID_EXIT:
		if (PlayfieldWin *pfw = Application::Get()->GetPlayfieldWin(); pfw != 0)
			pfw->PostMessage(WM_CLOSE);
		return true;

	case ID_HIDE:
		ShowHideFrameWindow(false);
		return true;

	case ID_FULL_SCREEN:
		ToggleFullScreen();
		return true;

	case ID_OPTIONS:
		// send these to the playfield view
		if (PlayfieldView *pfv = Application::Get()->GetPlayfieldView(); pfv != 0)
			pfv->SendMessage(WM_COMMAND, cmd);
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
		// forward these to the child view
		if (view != 0)
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

	// explicitly recalculate the frame
	RECT rc;
	GetWindowRect(hWnd, &rc);
	SetWindowPos(hWnd, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_FRAMECHANGED);

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
	static const LRESULT hitTests[3][3] =
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

