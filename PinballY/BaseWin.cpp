// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <dwmapi.h>
#include "../Utilities/GraphicsUtil.h"
#include "BaseWin.h"
#include "Resource.h"
#include "MouseButtons.h"
#include "Application.h"
#include "DShowAudioPlayer.h"

// window class registration
bool BaseWin::baseWinClassRegistered = FALSE;
ATOM BaseWin::baseWinClassAtom;
const TCHAR *BaseWin::baseWinClassName = _T("PinballY.BaseWin");

BaseWin::BaseWin(int contextMenuId) : contextMenuId(contextMenuId)
{
	isNcActive = false;
	hWnd = 0;
	curMsg = 0;
	szClient = { 100, 100 };
	hContextMenu = 0;
}

BaseWin::~BaseWin()
{
	// delete the context menu
	if (hContextMenu != 0)
		DestroyMenu(hContextMenu);

	// delete the context menu bitmaps
	for (auto hbmp : menuBitmaps)
		DeleteObject(hbmp);
}

// Register my window class
const TCHAR *BaseWin::RegisterClass()
{
	if (!baseWinClassRegistered)
	{
		// set up our class descriptor
		WNDCLASSEX wcex;
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = StaticWndProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = sizeof(LONG_PTR);
		wcex.hInstance = G_hInstance;
		wcex.hIcon = 0;
		wcex.hIconSm = 0;
		wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
		wcex.hbrBackground = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
		wcex.lpszMenuName = 0;
		wcex.lpszClassName = baseWinClassName;

		// register the class
		baseWinClassAtom = RegisterClassEx(&wcex);

		// we're now registered
		baseWinClassRegistered = true;
	}

	return baseWinClassName;
}

// Create our window
bool BaseWin::Create(HWND parent, const TCHAR *title, DWORD style, int nCmdShow)
{
	// register my class if we haven't already
	RegisterClass();

	// get the initial window position
	RECT rc = GetCreateWindowPos(nCmdShow);

	// create the UI window
	hWnd = CreateWindow(RegisterClass(), title, style,
		rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
		parent, NULL, G_hInstance, this);

	if (hWnd == NULL)
	{
		LogSysError(EIT_Error, LoadStringT(IDS_ERR_CREATEWIN),
			MsgFmt(_T("BaseWin::CreateWindow() failed, Win32 error %d"), GetLastError()));
		return false;
	}

	// load our context menu
	if (contextMenuId != 0)
		hContextMenu = LoadMenu(G_hInstance, MAKEINTRESOURCE(contextMenuId));

	// do class-specific initialization
	if (!InitWin())
	{
		// failed - destroy the window and abort
		DestroyWindow(hWnd);
		hWnd = NULL;
		return false;
	}

	// show the window and do initial drawing
	InitShowWin(nCmdShow);

	// success
	return true;
}

void BaseWin::InitShowWin(int nCmdShow)
{
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
}

LRESULT BaseWin::SendMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
	return hWnd != 0 ? ::SendMessage(hWnd, message, wParam, lParam) : 0;
}

void BaseWin::PostMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (hWnd != 0)
		::PostMessage(hWnd, message, wParam, lParam);
}

LRESULT CALLBACK BaseWin::StaticWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	// WM_NCCREATE is typically our very first message received, so we
	// haven't had a chance to set up our 'self' pointer via SetWindowLong 
	// yet.  Fortunately, we have another way to get 'self' during window
	// creation, specifically from the LPARAM we passed to CreateWindow,
	// That gets passed to us here via the CREATESTRUCT pointer that
	// WM_NCCREATE sends us via our lParam.
	BaseWin *self;
	if (message == WM_NCCREATE)
	{
		// get our 'self' parameter
		const CREATESTRUCT *cs = (const CREATESTRUCT *)lParam;
		self = (BaseWin *)cs->lpCreateParams;

		// add a reference on behalf of the window
		self->AddRef();

		// store our 'this' pointer in the window extra data slot
		SetWindowLongPtr(hWnd, 0, LONG_PTR(self));

		// store our hWnd internally
		self->hWnd = hWnd;
	}
	else
	{
		// 'self' should already be stashed in window long #0
		self = (BaseWin *)GetWindowLongPtr(hWnd, 0);
	}

	// if we have a 'self' pointer, dispatch through it
	if (self != nullptr)
	{
		// stack a message descriptor
		struct Stacker
		{
			// on creation, set up our internal curMsg, stack the old one, and make
			// out internal curMsg the active one
			Stacker(BaseWin *self, UINT message, WPARAM wParam, LPARAM lParam)
				: self(self), curMsg(message, wParam, lParam), prvMsg(self->curMsg)
			{
				self->curMsg = &curMsg;
			}

			// when we go out of scope, restore the stacked curMsg
			~Stacker() { self->curMsg = prvMsg; }
			
			BaseWin *self;		// this window object
			CurMsg curMsg;		// the current message
			CurMsg *prvMsg;		// the stacked prior message
		}
		curMsg(self, message, wParam, lParam);
		
		// dispatch through the virtual window proc
		return self->WndProc(message, wParam, lParam);
	}

	// no 'self' pointer is available; use the default system handling
	return DefWindowProc(hWnd, message, wParam, lParam);
}


LRESULT BaseWin::WndProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_PAINT:
		{
			// begin the paint cycle
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);

			// do the window-specific painting
			OnPaint(hdc);

			// end the paint cycle
			EndPaint(hWnd, &ps);
		}
		return 0;

	case WM_ERASEBKGND:
		// Assume that if the override handles the message, it will erase
		// the background, so set lResult to true going in.  The override
		// can always change that if it wants to do something unusual.
		curMsg->lResult = TRUE;
		if (OnEraseBkgnd((HDC)wParam))
			return curMsg->lResult;
		break;

	case WM_INPUT:
		// process it through our virtual
		OnRawInput(GET_RAWINPUT_CODE_WPARAM(wParam), (HRAWINPUT)lParam);

		// Note that it's mandatory that we also call DefWindowProc,
		// because that's where the system releases the input buffer
		// resources.  Proceed to the default handling.
		break;

	case WM_KEYDOWN:
	case WM_KEYUP:
		if (OnKeyEvent(message, wParam, lParam))
			return curMsg->lResult;
		break;

	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
		if (OnSysKeyEvent(message, wParam, lParam))
			return curMsg->lResult;
		break;

	case WM_SYSCHAR:
		if (OnSysChar(wParam, lParam))
			return curMsg->lResult;
		break;

	case WM_HOTKEY:
		if (OnHotkey((int)wParam, LOWORD(lParam), HIWORD(lParam)))
			return curMsg->lResult;
		break;

	case WM_LBUTTONDOWN:
		if (OnMouseButtonDown(MouseButton::mbLeft, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_LBUTTONUP:
		if (OnMouseButtonUp(MouseButton::mbLeft, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_MBUTTONDOWN:
		if (OnMouseButtonDown(MouseButton::mbMiddle, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_MBUTTONUP:
		if (OnMouseButtonUp(MouseButton::mbMiddle, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_RBUTTONDOWN:
		if (OnMouseButtonDown(MouseButton::mbRight, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_RBUTTONUP:
		if (OnMouseButtonUp(MouseButton::mbRight, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_XBUTTONDOWN:
		if (OnMouseButtonDown(
			(HIWORD(wParam) & XBUTTON1) != 0 ? MouseButton::mbX1 : MouseButton::mbX2,
			{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_XBUTTONUP:
		if (OnMouseButtonUp(
			(HIWORD(wParam) & XBUTTON1) != 0 ? MouseButton::mbX1 : MouseButton::mbX2,
			{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_MOUSEMOVE:
		if (OnMouseMove({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_MOUSEWHEEL:
		if (OnMouseWheel(MouseButton::mbWheel, float((INT16)HIWORD(wParam)) / WHEEL_DELTA))
			return curMsg->lResult;
		break;

	case WM_MOUSEHWHEEL:
		if (OnMouseWheel(MouseButton::mbHWheel, float((INT16)HIWORD(wParam)) / WHEEL_DELTA))
			return curMsg->lResult;
		break;

	case WM_SETCURSOR:
		if (OnSetMouseCursor(reinterpret_cast<HWND>(wParam), LOWORD(lParam), HIWORD(lParam)))
			return curMsg->lResult;
		break;

	case WM_NCLBUTTONDOWN:
		if (OnNCMouseButtonDown(MouseButton::mbLeft, (UINT)wParam, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_NCLBUTTONUP:
		if (OnNCMouseButtonUp(MouseButton::mbLeft, (UINT)wParam, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_NCMBUTTONDOWN:
		if (OnNCMouseButtonDown(MouseButton::mbMiddle, (UINT)wParam, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_NCMBUTTONUP:
		if (OnNCMouseButtonUp(MouseButton::mbMiddle, (UINT)wParam, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_NCRBUTTONDOWN:
		if (OnNCMouseButtonDown(MouseButton::mbRight, (UINT)wParam, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_NCRBUTTONUP:
		if (OnNCMouseButtonUp(MouseButton::mbRight, (UINT)wParam, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_NCXBUTTONDOWN:
		if (OnNCMouseButtonDown(HIWORD(wParam) == XBUTTON1 ? MouseButton::mbX1 : MouseButton::mbX2,
			(UINT)LOWORD(wParam), { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_NCXBUTTONUP:
		if (OnNCMouseButtonUp(HIWORD(wParam) == XBUTTON1 ? MouseButton::mbX1 : MouseButton::mbX2,
			(UINT)LOWORD(wParam), { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			return curMsg->lResult;
		break;

	case WM_ACTIVATE:
		if (OnActivate(LOWORD(wParam), HIWORD(wParam) != 0, (HWND)lParam))
			return curMsg->lResult;
		break;

	case WM_ACTIVATEAPP:
		if (OnActivateApp((BOOL)wParam, (DWORD)lParam))
			return curMsg->lResult;
		break;

	case WM_COMMAND:
		if (OnCommand(LOWORD(wParam), HIWORD(wParam), (HWND)lParam))
			return curMsg->lResult;
		break;

	case WM_SYSCOMMAND:
		if (OnSysCommand(wParam, lParam))
			return curMsg->lResult;
		break;

	case WM_TIMER:
		if (OnTimer(wParam, lParam))
			return curMsg->lResult;
		break;

	case WM_MOVE:
		OnMove({ (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) });
		break;

	case WM_SIZE:
		// Resize the window.  Note that we get one of these during the initial 
		// window setup, before we have a chance to set the 'self' object, so 'self'
		// might be null.
		switch (wParam)
		{
		case SIZE_MAXIMIZED:
		case SIZE_RESTORED:
			// we've been maximized or restored - update with the new size
			OnResize(LOWORD(lParam), HIWORD(lParam));
			break;

		case SIZE_MAXHIDE:
		case SIZE_MAXSHOW:
			// *another* window has been maximized or restored, so our size isn't
			// affected; ignore this
			break;

		case SIZE_MINIMIZED:
			// we've been minimized
			OnMinimize();
			break;

		default:
			break;
		}
		break;

	case WM_WINDOWPOSCHANGING:
		if (OnWindowPosChanging(reinterpret_cast<WINDOWPOS*>(lParam)))
			return curMsg->lResult;
		break;

	case WM_WINDOWPOSCHANGED:
		if (OnWindowPosChanged(reinterpret_cast<WINDOWPOS*>(lParam)))
			return curMsg->lResult;
		break;

	case WM_GETMINMAXINFO:
		if (OnGetMinMaxInfo(reinterpret_cast<MINMAXINFO*>(lParam)))
			return curMsg->lResult;
		break;

	case WM_CREATE:
		if (OnCreate(reinterpret_cast<CREATESTRUCT*>(lParam)))
			return curMsg->lResult;
		break;

	case WM_CLOSE:
		if (OnClose())
			return curMsg->lResult;
		break;

	case WM_DESTROY:
		if (OnDestroy())
			return curMsg->lResult;
		break;

	case WM_NCDESTROY:
		if (OnNCDestroy())
			return curMsg->lResult;

		// OnNCDestroy() normally releases our self-reference, which can have
		// the side effect of deleting the object.  So do the system default
		// work and return immediately, to be sure we don't try to dereference
		// 'this' after this point.
		return DefWindowProc(hWnd, message, wParam, lParam);

	case WM_NCHITTEST:
		{
			UINT hit = HTNOWHERE;
			if (OnNCHitTest({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }, hit))
				return hit;
		}
		break;

	case WM_NCCALCSIZE:
		{
			UINT wvrFlags = 0;
			if (OnNCCalcSize(wParam != 0, (NCCALCSIZE_PARAMS *)lParam, wvrFlags))
				return wvrFlags;
		}
		break;

	case WM_NCACTIVATE:
		// This message normally must return TRUE.  FALSE means that the system
		// shouldn't do default drawing for inactive title bars; that's not
		// recommended (per the SDK doc), so we don't expose a return value
		// explicitly through the override.  The override can still set a return
		// value via this->curMsg->lResult for extraordinary situations.
		curMsg->lResult = TRUE;
		if (OnNCActivate(wParam != 0, (HRGN)lParam))
			return curMsg->lResult;
		break;

	case WM_INITMENUPOPUP:
		if (OnInitMenuPopup((HMENU)wParam, LOWORD(lParam), HIWORD(lParam) != 0))
			return curMsg->lResult;
		break;

	case WM_INPUT_DEVICE_CHANGE:
		OnRawInputDeviceChange((USHORT)wParam, (HANDLE)lParam);
		break;

	case WM_ENTERIDLE:
		if (OnEnterIdle((int)wParam, (HWND)lParam))
			return curMsg->lResult;
		break;

	case WM_DPICHANGED:
		OnDpiChanged(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<LPCRECT>(lParam));
		break;

	default:
		// check for private window class and private application messages
		if (message >= WM_USER && message < WM_APP)
		{
			if (OnUserMessage(message, wParam, lParam))
				return curMsg->lResult;
		}
		else if (message >= WM_APP && message < 0xBFFF)
		{
			if (OnAppMessage(message, wParam, lParam))
				return curMsg->lResult;
		}
		break;
	}

	// The message wasn't handled by one of our overrides, so use
	// the system default handling.  If we're using DWM extensions,
	// and the DWM handler intercepted the message, use its result.
	// Otherwise use the system default window proc.
	return curMsg->dwmHandled ?
		curMsg->dwmResult : 
		DefWindowProc(hWnd, message, wParam, lParam);
}

bool BaseWin::OnActivateApp(BOOL activating, DWORD otherThreadId)
{
	// notify the application object
	Application::Get()->OnActivateApp(this, activating, otherThreadId);

	// let the default system processing proceed
	return false;
}

bool BaseWin::OnNCDestroy()
{
	// forget our C++ object
	SetWindowLongPtr(hWnd, 0, 0);

	// the window handle is no longer valid - clear it
	hWnd = NULL;

	// remove the reference we keep on behalf of the system window (this might
	// delete 'self', so do this last)
	Release();

	// allow the default system handling to proceed
	return false;
}

bool BaseWin::OnUserMessage(UINT message, WPARAM wParam, LPARAM lParam) 
{
	switch (message)
	{
	case BWMsgUpdateMenu:
		UpdateMenu((HMENU)wParam, (BaseWin *)lParam);
		return true;

	case BWMsgCallLambda:
		// "Lambda Callback".  This takes a pointer to a std::function<void()>
		// in the LPARAM, and simply invokes it.  The main use of this is to
		// let a background thread do some work on the main UI thread, which
		// is sometimes a convenient way to synchronize access to resources 
		// across threads.
		curMsg->lResult = (*(std::function<LRESULT()>*)lParam)();
		return true;
	}

	// not handled
	return false;
}

bool BaseWin::OnAppMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case DSMsgOnEvent:
		// DirectShow event ready.  Call the DirectShow object to process
		// the event.
		DShowAudioPlayer::OnEvent(lParam);
		return true;
	}

	// not handled
	return false;
}

void BaseWin::LoadMenuIcon(int cmd, int resid)
{
	// load the image
	HBITMAP bmp = LoadPNG(resid);
	if (bmp != NULL)
	{
		// set the bitmap in the menu
		MENUITEMINFO mii;
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_BITMAP;
		mii.hbmpItem = bmp;
		SetMenuItemInfo(hContextMenu, cmd, FALSE, &mii);

		// add the bitmap to our cleanup list
		menuBitmaps.push_back(bmp);
	}
}

void BaseWin::LoadMenuCheckIcons(int cmd, int residUnchecked, int residChecked)
{
	// load the images
	HBITMAP bmpUnchecked = LoadPNG(residUnchecked);
	HBITMAP bmpChecked = LoadPNG(residChecked);
	if (bmpUnchecked != NULL && bmpChecked != NULL)
	{
		// set the bitmap in the menu
		MENUITEMINFO mii;
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_CHECKMARKS;
		mii.hbmpUnchecked = bmpUnchecked;
		mii.hbmpChecked = bmpChecked;
		SetMenuItemInfo(hContextMenu, cmd, FALSE, &mii);
	}

	// add the bitmaps to our deletion list
	if (bmpUnchecked != NULL)
		menuBitmaps.push_back(bmpUnchecked);
	if (bmpChecked != NULL)
		menuBitmaps.push_back(bmpChecked);
}

void BaseWin::ShowContextMenu(POINT pt)
{
	// translate the LPARAM to a POINT in screen coordinates
	ClientToScreen(hWnd, &pt);

	// get the menu
	HMENU m = GetSubMenu(hContextMenu, 0);

	// update menu item status
	UpdateMenu(m, this);

	// show the menu
	TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hWnd, 0);
}

bool BaseWin::OnEnterIdle(int /*code*/, HWND /*hwndSrc*/)
{
	// process D3D updates as long as we're idle
	MSG msg;
	while (!PeekMessage(&msg, NULL, 0, 0, FALSE))
		D3DView::RenderAll();

	// consider it handled
	return true;
}

void BaseWin::OnDpiChanged(int dpiX, int dpiY, LPCRECT prcNewPos)
{
	// resize the window based on the suggested size from Windows
	SetWindowPos(hWnd, NULL, prcNewPos->left, prcNewPos->top,
		prcNewPos->right - prcNewPos->left, prcNewPos->bottom - prcNewPos->top,
		SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
}
