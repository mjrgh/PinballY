// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Key assigner control

#include "stdafx.h"
#include "../Utilities/KeyInput.h"
#include "../Utilities/Joystick.h"
#include "../Utilities/InputManager.h"
#include "KeyAssignCtrl.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


KeyAssignCtrl::KeyAssignCtrl()
{
}

KeyAssignCtrl::~KeyAssignCtrl()
{
}

BEGIN_MESSAGE_MAP(KeyAssignCtrl, CVCEdit)
	ON_WM_SETFOCUS()
	ON_WM_KILLFOCUS()
END_MESSAGE_MAP()

BOOL KeyAssignCtrl::PreTranslateMessage(MSG* pMsg)
{
	switch (pMsg->message)
	{
	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		SetFocus();
		return TRUE;

	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	{
		// translate extended keys
		vk = KeyInput::TranslateExtKeys(pMsg->message, pMsg->wParam, pMsg->lParam);

		// trigger a key entry event
		OnKeyEntry();

		// the key has been handled
		return TRUE;
	}

	case WM_KEYUP:
	case WM_SYSKEYUP:
		// ignore key-up events
		return TRUE;
	}

	// use the default handling
	return __super::PreTranslateMessage(pMsg);
}

void KeyAssignCtrl::OnKeyEntry()
{
	// send a parent notification
	if (m_hWnd != 0)
		GetParent()->SendMessage(WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), EN_ACCEL_SET), (LPARAM)GetSafeHwnd());
}

void KeyAssignCtrl::OnSetFocus(CWnd *pWnd)
{
	// do the base class work
	__super::OnSetFocus(pWnd);

	// subscribe for raw input messages
	JoystickManager::GetInstance()->SubscribeJoystickEvents(this);
}

void KeyAssignCtrl::OnKillFocus(CWnd *pWnd)
{
	// do the regular work
	__super::OnKillFocus(pWnd);

	// unsubscribe to raw input
	JoystickManager::GetInstance()->UnsubscribeJoystickEvents(this);
}

bool KeyAssignCtrl::OnJoystickButtonChange(
	JoystickManager::PhysicalJoystick *js, int button, bool pressed, bool foreground)
{
	// if a button was newly pressed, use it as the result
	if (pressed)
	{
		// remember the button and unit
		jsButton = button;
		jsUnit = js->logjs->index;

		// trigger a key entry event
		OnKeyEntry();

		// event handled
		return true;
	}

	// not handled
	return false;
}
