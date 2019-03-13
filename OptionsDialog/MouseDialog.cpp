// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "MouseDialog.h"
#include "../Utilities/Config.h"

BEGIN_MESSAGE_MAP(CDragButton, CButton)
	ON_WM_LBUTTONDOWN()
END_MESSAGE_MAP()

void CDragButton::OnLButtonDown(UINT nFlags, CPoint point)
{
	GetParent()->SendMessage(WM_COMMAND, IDC_BTN_MOUSE_COORDS);
}


IMPLEMENT_DYNAMIC(MouseDialog, OptionsPage)

MouseDialog::MouseDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

MouseDialog::~MouseDialog()
{
}

void MouseDialog::InitVarMap()
{
	setCoordsBtn.SubclassDlgItem(IDC_BTN_MOUSE_COORDS, this);
	varMap.emplace_back(new CkBoxMap(_T("Mouse.HideByMoving"), IDC_CK_HIDE_BY_MOVING, false));
	varMap.emplace_back(new EditStrMap(_T("Mouse.HideCoords"), IDC_TXT_MOUSE_COORDS, _T("1920,540")));
}

BOOL MouseDialog::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (LOWORD(wParam))
	{
	case IDC_BTN_MOUSE_COORDS:
		SetCapture();
		capturingCoords = true;
		GetDlgItem(IDC_STXT_CLICK_TO_SET)->ShowWindow(SW_SHOW);
		SetCursor(LoadCursor(NULL, IDC_CROSS));
		return TRUE;
	}

	return OptionsPage::OnCommand(wParam, lParam);
}

void MouseDialog::OnMouseMove(UINT nFlags, CPoint point)
{
	if (capturingCoords)
	{
		ClientToScreen(&point);
		SetDlgItemText(IDC_TXT_MOUSE_COORDS, MsgFmt(_T("%ld,%ld"), point.x, point.y));
	}
}

void MouseDialog::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (capturingCoords)
		ReleaseCapture();
}

void MouseDialog::OnCaptureChanged(CWnd *pWnd)
{
	if (capturingCoords)
	{
		capturingCoords = false;
		GetDlgItem(IDC_STXT_CLICK_TO_SET)->ShowWindow(SW_HIDE);
	}
	OptionsPage::OnCaptureChanged(pWnd);
}

BEGIN_MESSAGE_MAP(MouseDialog, OptionsPage)
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()
