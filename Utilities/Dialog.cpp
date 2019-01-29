// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Dialog box helper

#include "stdafx.h"
#include "UtilResource.h"
#include "Dialog.h"
#include "InstanceHandle.h"
#include "WinUtil.h"

Dialog::Dialog()
{
}

Dialog::~Dialog()
{
}

void Dialog::Show(int resourceID)
{
	DialogBoxParam(
		G_hInstance,
		MAKEINTRESOURCE(resourceID),
		GetActiveWindow(),
		&Dialog::DialogProc,
		LPARAM(this));
}

void Dialog::ShowWithMessageBoxFont(int resourceID)
{
	// get the system UI parameters
	NONCLIENTMETRICSW ncm;
	ncm.cbSize = sizeof(ncm);
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSW), &ncm, 0);

	// show the dialog using the system Message Box font
	ShowWithFont(resourceID, &ncm.lfMessageFont);
}

// ShowWithFont helper: skip a null-terminated WCHAR string in the
// dialog template (including the trailing null).
static const BYTE *Skip_sz(const BYTE *p)
{
	const WORD *w = (const WORD *)p;
	for (; *w != 0; ++w);
	return (const BYTE *)(w + 1);
}

// ShowWithFont helper: skip an "sz_Or_Ord" field in the dialog
// template, which contains one of the following:
//
//   WORD 0x0000   -> just this one word
//   WORD 0xFFFF   -> two words
//   other         -> null-terminated WCHAR string
//
static const BYTE *Skip_sz_Or_Ord(const BYTE *p)
{
	// it the first WORD is 0x0000, that's all there is
	const WORD *w = (const WORD *)p;
	if (*w == 0)
		return (const BYTE *)(w + 1);

	// if the first WORD is 0xFFFF, there's exactly one more WORD 
	// following it
	if (*w == 0xffff)
		return (const BYTE *)(w + 2);

	// otherwise, it's a null-terminated WCHAR string
	return Skip_sz(p);
}

void Dialog::ShowWithFont(int resource_id, const LOGFONTW *fontDesc)
{
	// load the font
	HFONT font = CreateFontIndirect(fontDesc);

	// load the dialog template
	HRSRC hRes = FindResource(G_hInstance, MAKEINTRESOURCE(resource_id), RT_DIALOG);
	DWORD resSize = SizeofResource(G_hInstance, hRes);
	HGLOBAL hGlob = LoadResource(G_hInstance, hRes);
	const BYTE *tpl = (BYTE *)LockResource(hGlob);
	if (tpl == nullptr)
	{
		// show a hardcoded message, rather than a localizable string table
		// resource as we usually would: if the dialog resource is missing,
		// string resources might be missing as well
		MessageBox(NULL, MsgFmt(_T("Missing dialog resource %d"), resource_id), _T("Error"),
			MB_ICONERROR | MB_OK);
		return;
	}

	// parse it - start with the 'sz_Or_Ord menu' entry (string or resource ID)
	const BYTE *p = Skip_sz_Or_Ord(tpl + 26);

	// skip the windowClass field
	p = Skip_sz_Or_Ord(p);

	// skip the title field
	p = Skip_sz(p);

	// we're at the pointsize field
	const BYTE *pPointsize = p;
	const BYTE *pTypeface = p + 6;

	// get the length of the old font name
	size_t fontNameLen = (wcslen((const WCHAR *)pTypeface) + 1) * sizeof(WCHAR);

	// allocate a new copy of the structure with space for the message 
	// box font name in place of the font in the resource file
	size_t newFontNameLen = (wcslen(fontDesc->lfFaceName) + 1) * sizeof(WCHAR);
	BYTE *newTpl = new BYTE[resSize + newFontNameLen - fontNameLen];

	// copy the original template up to the font name
	memcpy(newTpl, tpl, pTypeface - tpl);

	// The dialog template wants the new font size to be specified
	// as a point size.  Work backwards from the lfHeight in the font
	// descriptor to a point size.  If lfHeight is positive, it's the
	// character cell height, which is basically the character height
	// plus the leading.  If it's negative, it's just the character
	// height.  Once we have the character height, figure the point
	// size by running the forumula described in the WinSDK docs for
	// LOGFONT.lfHeight backwards.
	LONG charHeight = fontDesc->lfHeight;
	if (charHeight != 0)
	{
		// we'll need a DC for some of the font lookup operations
		HDC dc = GetDC(GetActiveWindow());

		// check if we have a char height or cell height
		if (charHeight < 0)
		{
			// it's the char height - get the absolute value
			charHeight = -charHeight;
		}
		else
		{
			// it's the cell height - get the leading
			TEXTMETRIC tm;
			ZeroMemory(&tm, sizeof(tm));
			HGDIOBJ oldFont = SelectObject(dc, font);
			GetTextMetrics(dc, &tm);
			SelectObject(dc, oldFont);

			// deduct the leading from the cell height to get the char height
			charHeight -= tm.tmInternalLeading;
		}

		// Now we can finally figure the point size by working the WinSDK
		// lfHeight point size formula backwards.
		LONG ptSize = MulDiv(charHeight, 72, GetDeviceCaps(dc, LOGPIXELSY));
		newTpl[pPointsize - tpl] = (BYTE)ptSize;

		// done with our temp dc
		ReleaseDC(GetActiveWindow(), dc);
	}

	// store the new font name
	memcpy(newTpl + (pTypeface - tpl), fontDesc->lfFaceName, newFontNameLen);

	// copy the rest of the template
	memcpy(newTpl + (pTypeface - tpl) + newFontNameLen, pTypeface + fontNameLen,
		resSize - ((pTypeface - tpl) + fontNameLen));

	// show the dialog
	DialogBoxIndirectParam(
		G_hInstance,
		(LPCDLGTEMPLATE)newTpl,
		GetActiveWindow(),
		&Dialog::DialogProc,
		LPARAM(this));

	// release resources
	UnlockResource(hGlob);
	delete[] newTpl;
	DeleteObject(font);
}

INT_PTR CALLBACK Dialog::DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	Dialog *self;

	switch (message)
	{
	case WM_INITDIALOG:
		// The lParam has our object pointer.  Store it in the DWLP_USER
		// window long so that we can retrieve it on subsequent calls.
		SetWindowLongPtr(hDlg, DWLP_USER, lParam);

		// get the 'this' pointer
		self = (Dialog *)lParam;

		// set the window handle internally
		self->hDlg = hDlg;

		// invoke the virtual method
		return self->Proc(message, wParam, lParam);

	case WM_ENTERIDLE:
		// Forward this up to our parent window.  Note that WM_ENTERIDLE is
		// sent to the *parent* of a dialog, not the dialog itself, so 
		// we're not handling this here for the sake of the present dialog.
		// Rather, we're handling it for the sake of any nested dialogs we
		// invoke, including MessageBox dialogs.  Note that we won't get
		// this at all for nested MessageBox dialogs unless they're created
		// using our specialized cover, MessageBoxWithIdleMsg().  The point
		// of our WM_ENTERIDLE handling is to continue doing D3D rendering
		// while in a modal dialog's nested event loop; our main window
		// classes take care of that, which is why it's desirable to
		// forward the message up to our parent.
		return SendMessage(GetParent(hDlg), message, wParam, lParam);

	default:
		// For other messages, the DWLP_USER window long should have our
		// object pointer.  Retrieve it, make sure it's valid, and call
		// our virtual dialog proc method.
		self = (Dialog *)GetWindowLongPtr(hDlg, DWLP_USER);
		if (self != 0)
			return self->Proc(message, wParam, lParam);

		// it's not set - ignore the message
		return FALSE;
	}
}

INT_PTR Dialog::Proc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
		// WM_INITDIALOG should generally return true
		return TRUE;

	case WM_COMMAND:
		// close the dialog on OK or CANCEL
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return TRUE;
		}
		break;
	}

	// not handled
	return FALSE;
}

int Dialog::ResizeStaticToFitText(HWND ctl, const TCHAR *txt)
{
	// get the current layout
	const RECT rc = GetCtlScreenRect(ctl);
	const int width = rc.right - rc.left;
	const int height = rc.bottom - rc.top;

	// get the font and select it into the DC
	HFONT hfont = (HFONT)SendMessage(ctl, WM_GETFONT, 0, 0);
	HDC hdc = GetDC(hDlg);
	HFONT oldFont = (HFONT)SelectObject(hdc, hfont);

	// set the text
	SetWindowText(ctl, txt);

	// measure the text
	RECT rcTxt = rc;
	int newht = DrawText(hdc, txt, -1, &rcTxt, DT_CALCRECT | DT_TOP | DT_LEFT | DT_WORDBREAK);

	// if the height didn't increase, there's nothing to do
	if (newht < height)
		return 0;

	// increase the height to fit, keeping the width and location
	MoveWindow(ctl, rc.left, rc.top, rc.right - rc.left, newht, TRUE);

	// restore the old font
	SelectObject(hdc, oldFont);

	// return the change in height
	return newht - height;
}

// Get the client rect for a dialog control relative to the dialog window
RECT Dialog::GetCtlScreenRect(HWND ctl)
{
	RECT rc;
	GetWindowRect(ctl, &rc);
	ScreenToClient(hDlg, (POINT *)&rc);
	ScreenToClient(hDlg, ((POINT *)&rc) + 1);
	return rc;
}

void Dialog::MoveCtlBy(int ctlID, int dx, int dy)
{
	// get the control
	HWND ctl = GetDlgItem(ctlID);
	if (ctl != 0)
	{
		// get its current location
		RECT rc = GetCtlScreenRect(ctl);

		// move it to the new location
		MoveWindow(ctl, rc.left + dx, rc.top + dy, rc.right - rc.left, rc.bottom - rc.top, TRUE);
	}
}

void Dialog::ExpandWindowBy(int dx, int dy)
{
	// get the current window area
	RECT rcw;
	GetWindowRect(hDlg, &rcw);

	// expand it by the given amount
	MoveWindow(hDlg, rcw.left, rcw.top, rcw.right - rcw.left + dx, rcw.bottom - rcw.top + dy, TRUE);
}

void Dialog::FormatDlgItemText(int ctlID, ...)
{
	va_list ap;
	va_start(ap, ctlID);
	FormatWindowTextV(GetDlgItem(ctlID), ap);
	va_end(ap);
}

// --------------------------------------------------------------------------
//
// Message-box-like dialog
//

INT_PTR MessageBoxLikeDialog::Proc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
		// set the bitmap ID for the icon
		SendDlgItemMessage(hDlg, IDC_ERROR_ICON, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)icon);
		break;

	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORBTN:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORDLG:
		// use the 3D face brush for the bottom bar
		if ((HWND)lParam == GetDlgItem(IDC_BOTTOM_BAR))
			return (INT_PTR)faceBrush;

		// use the white brush for other controls
		return (INT_PTR)bkgBrush;
	}

	// return the inherited handling
	return __super::Proc(message, wParam, lParam);
}

// --------------------------------------------------------------------------
//
// Message box with checkbox
//

MessageBoxLikeDialog::MessageBoxLikeDialog(ErrorIconType icon) :
	MessageBoxLikeDialog(icon == EIT_Warning ? IDB_WARNING :
		icon == EIT_Information ? IDB_INFORMATION :
		IDB_ERROR)
{
}

MessageBoxLikeDialog::MessageBoxLikeDialog(int bitmap_id)
{
	// load the icon
	icon = (HBITMAP)LoadImage(
		G_hInstance, MAKEINTRESOURCE(bitmap_id),
		IMAGE_BITMAP, 0, 0, LR_DEFAULTSIZE);

	// create the brushes
	bkgBrush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
	faceBrush = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
}


INT_PTR MessageBoxWithCheckbox::Proc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
		// set the main message
		SetDlgItemText(hDlg, IDC_TXT_ERROR, this->message.c_str());

		// resize it to accommodate the message text
		{
			int dy = ResizeStaticToFitText(GetDlgItem(IDC_TXT_ERROR), this->message.c_str());
			MoveCtlBy(IDC_MESSAGE_CHECKBOX, 0, dy);
			MoveCtlBy(IDC_BOTTOM_BAR, 0, dy);
			MoveCtlBy(IDOK, 0, dy);
			MoveCtlBy(IDCANCEL, 0, dy);
			ExpandWindowBy(0, dy);
		}

		// set the checkbox text
		SetDlgItemText(hDlg, IDC_MESSAGE_CHECKBOX, checkboxLabel.c_str());
		break;

	case WM_COMMAND:
		// record checkbox changes
		if (LOWORD(wParam) == IDC_MESSAGE_CHECKBOX)
			isCheckboxChecked = IsDlgButtonChecked(hDlg, IDC_MESSAGE_CHECKBOX) == BST_CHECKED;
		break;

	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORBTN:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORDLG:
		// use the 3D face brush for the checkbox, which is in the bottom bar
		if ((HWND)lParam == GetDlgItem(IDC_MESSAGE_CHECKBOX))
			return (INT_PTR)faceBrush;
		break;
	}

	// return the inherited handling
	return __super::Proc(message, wParam, lParam);
}

// -----------------------------------------------------------------------
//
// MessageBox() specialization with with idle processing
//
int MessageBoxWithIdleMsg(HWND hWnd, LPCTSTR lpText, LPCTSTR lpCaption, UINT uType)
{
	// install our hook
	static HHOOK hhook;
	hhook = SetWindowsHookEx(WH_CBT, [](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT
	{
		if (nCode == HCBT_CREATEWND)
		{
			// creating a window - get the hook message parameters
			HWND hwndCreating = reinterpret_cast<HWND>(wParam);
			CBT_CREATEWND *createStruct = reinterpret_cast<CBT_CREATEWND*>(lParam);

			// Check the class name of the new window.  The MessageBox 
			// class starts with "#32770".
			TCHAR cls[256];
			GetClassName(hwndCreating, cls, countof(cls));
			if (_tcsncmp(cls, _T("#32770"), 6) == 0)
			{
				// it's the message box - clear the DS_NOIDLEMSG style
				SetWindowLong(hwndCreating, GWL_STYLE,
					GetWindowLong(hwndCreating, GWL_STYLE) & ~DS_NOIDLEMSG);
			}
		}

		// call the next hook
		return CallNextHookEx(hhook, nCode, wParam, lParam);

	}, G_hInstance, GetCurrentThreadId());

	// now run the native system message box
	int ret = MessageBox(hWnd, lpText, lpCaption, uType);

	// remove the hook
	UnhookWindowsHookEx(hhook);

	// return the MessageBox result
	return ret;
}
