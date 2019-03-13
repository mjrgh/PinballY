// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "MouseDialog.h"
#include "../Utilities/Config.h"

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

void MouseDialog::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (capturingCoords)
	{
		ReleaseCapture();
		capturingCoords = false;
		ClientToScreen(&point);
		SetDlgItemText(IDC_TXT_MOUSE_COORDS, MsgFmt(_T("%ld,%ld"), point.x, point.y));
	}
}

void MouseDialog::OnCaptureChanged(CWnd *pWnd)
{
	capturingCoords = false;
	OptionsPage::OnCaptureChanged(pWnd);
}

BEGIN_MESSAGE_MAP(MouseDialog, OptionsPage)
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()
