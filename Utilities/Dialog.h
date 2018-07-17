// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Dialog box helper class

#pragma once
#include "Util.h"
#include "StringUtil.h"
#include "ErrorIconType.h"

class Dialog
{
public:
	Dialog();
	virtual ~Dialog();

	// show the dialog
	virtual void Show(int resourceID);

	// Show the dialog, replacing the font specified in the dialog resource
	// with the system Message Box font.
	virtual void ShowWithMessageBoxFont(int resource_id);

	// Show the dialog, replacing the font specified in the dialog resource
	// with the specified dynamically loaded font.
	virtual void ShowWithFont(int resource_id, const LOGFONTW *fontDesc);

protected:
	// dialog box procedure - static entrypoint
	static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

	// dialog box procedure - virtual method entrypoint
	virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam);

	// Get the client rectangle of a control in global (screen) 
	// coordinates.
	RECT GetCtlScreenRect(HWND ctl);

	// get a dialog item
	HWND GetDlgItem(int ctlID) { return ::GetDlgItem(hDlg, ctlID); }

	// Resize a static text element vertically so that it's tall enough
	// to fit its text contents.  Returns the change in height from the
	// original control height.
	int ResizeStaticToFitText(HWND ctl, const TCHAR *txt);

	// Move a control by the given distance
	void MoveCtlBy(int ctlID, int dx, int dy);

	// Expand the window by the given delta
	void ExpandWindowBy(int dx, int dy);

	// Dialog window handle
	HWND hDlg;
};

// --------------------------------------------------------------------------
//
// System message box with continued D3D rendering.
//
// This works like the normal MessageBox() API, but hooks into the dialog
// creation to remove the DS_NOIDLEMSG flag that (for whatever reason) the
// standard message box dialog sets.  That flag prevents idle processing
// while the dialog is showing, which in turn prevents our D3D windows
// from rendering.  So in most cases, it's better to use this version when
// you need a system message box.
//

int MessageBoxWithIdleMsg(HWND hWnd, LPCTSTR lpText, LPCTSTR lpCaption, UINT uType);


// --------------------------------------------------------------------------
// 
// Message Box-like dialogs.  These dialogs have the following fixed
// resource IDs:
//
//    IDOK           - OK/Close button
//    IDC_TXT_ERROR  - main error text
//    IDC_ERROR_ICON - error icon
//    IDC_BOTTOM_BAR - bottom bar containing the buttons
//


class MessageBoxLikeDialog : public Dialog
{
public:
	MessageBoxLikeDialog(ErrorIconType icon);
	MessageBoxLikeDialog(int bitmap_id);

	virtual void Show(int resource_id)
	{
		ShowWithMessageBoxFont(resource_id);
	}

	virtual ~MessageBoxLikeDialog()
	{
		DeleteObject(icon);
		DeleteObject(bkgBrush);
		DeleteObject(faceBrush);
	}

protected:
	virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam);

	// error/warning icon
	HBITMAP icon;

	// background color brush - uses the system window background color
	HBRUSH bkgBrush;

	// 3D face brush, per system parameters
	HBRUSH faceBrush;
};


// --------------------------------------------------------------------------
//
// Message box with checkbox.  This simulates a message box, but adds a
// check box at the bottom.  These are usually used for a message like
// "Don't show me this message again".
//
class MessageBoxWithCheckbox : public MessageBoxLikeDialog
{
public:
	MessageBoxWithCheckbox(ErrorIconType icon, const TCHAR *message, const TCHAR *checkboxLabel) :
		MessageBoxLikeDialog(icon),
		message(message), checkboxLabel(checkboxLabel), isCheckboxChecked(false)
	{
	}

	MessageBoxWithCheckbox(int iconID, const TCHAR *message, const TCHAR *checkboxLabel) : 
		MessageBoxLikeDialog(iconID), 
		message(message), checkboxLabel(checkboxLabel), isCheckboxChecked(false)
	{
	}

	// is the checkbox checked?
	bool IsCheckboxChecked() const { return isCheckboxChecked; }

protected:
	virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam);

	// main text message
	TSTRING message;

	// checkbox label, set at construction
	TSTRING checkboxLabel;	

	// is the checkbox checked?
	bool isCheckboxChecked;
};

