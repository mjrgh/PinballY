// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// KeyAssignCtrl - key assignment control
//
// This implements an input control similar to CMFCAcceleratorKeyAssignCtrl,
// but this version is specialized for pinball simulator keys rather than
// normal Windows accelerators.  The differences are:
//
// - The modifier keys (Shift, Alt, Ctrl) count as mappable keys in their
//   own right, rather than as modifiers for chords
//
// - Because the modifier keys are mappable as command keys, we don't allow
//   chords (e.g., Ctrl+A or Shift+Ctrl+X); only individual keys can be
//   mapped
//
// - The left/right instances of the modifier keys are treated as distinct;
//   e.g., Left Shift and Right Shift can be assigned to separate commands
//
// - Certain other extended keys that Windows doesn't distinguish from the
//   basic equivalents are mapped to our own "VKE_xxx" codes, defined in
//   KeyInput.h.  E.g., Windows folds Keypad Enter into VK_RETURN, but we
//   distinguish it as VKE_NUMPAD_ENTER.  
//

#pragma once
#include <afxacceleratorkeyassignctrl.h>
#include <afxcontrolbarutil.h>
#include <afxacceleratorkey.h>
#include "VCEdit.h"
#include "../Utilities/Joystick.h"


// Game-style key assignment control.  Customizes the 
class KeyAssignCtrl : public CVCEdit, public JoystickManager::JoystickEventReceiver
{
public:
	KeyAssignCtrl();
	virtual ~KeyAssignCtrl();

	virtual BOOL PreTranslateMessage(MSG* pMsg);

	// Parent notifications.  These are custom codes sent to the
	// parent window on special accelerator key events.  These are
	// sent via WM_COMMAND messages in imitation of the EN_xxx 
	// notifications that a regular edit control uses.  These are
	// sent as
	//
	//   WM_COMMAND, MAKEWPARAM(dialog control ID, EN_xxx), (LPARAM)hWnd
	//
	// EN_ACCEL_SET   - set a new accelerator key
	//
	static const USHORT EN_ACCEL_SET = 0xF100;

	// Get the assigned keyboard key, as a virtual key (VK_) value. 
	// Returns -1 if no key was assigned.
	int GetKey() const { return vk; }

	// Get the assigned joystick button data.  If a joystick button
	// was pressed, fills in 'unit' with the unit number, as defined
	// by the JoystickManager class, and returns the pressed button
	// (numbered from 0).  If a joystick button wasn't entered, we
	// return -1.
	int GetJS(int &unit)
	{
		unit = jsUnit;
		return jsButton;
	}

	// reset
	void Reset(const TCHAR *initText)
	{
		// clear the key and button entries
		vk = -1;
		jsButton = -1;
		jsUnit = -1;

		// set the initial window text, and select it all
		SetWindowText(initText != 0 ? initText : _T(""));
		SetSel(0, -1, FALSE);
	}

	// Joystick input subscription handling
	virtual bool OnJoystickButtonChange(
		JoystickManager::PhysicalJoystick *js, 
		int button, bool pressed, bool foreground) override;


protected:
	// Keystroke entered.  This is called when the user 
	// completes key entry by pressing a valid accelerator
	// key.  The default sends an EN_ACCEL_SET notification
	// to the parent (via a WM_COMMAND message).
	virtual void OnKeyEntry();

	// Virtual key (VK_) or extended virtual key (VKE_)
	// code for the entered keyboard key, or -1 if no key 
	// has been pressed yet
	int vk;

	// joystick button pressed, nubered from 0, or -1 if no
	// button has been pressed
	int jsButton;

	// joystick unit number
	int jsUnit;

protected:
	DECLARE_MESSAGE_MAP()
	afx_msg void OnSetFocus(CWnd *pWnd);
	afx_msg void OnKillFocus(CWnd *pWnd);
};
