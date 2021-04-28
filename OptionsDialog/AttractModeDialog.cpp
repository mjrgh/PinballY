// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "../Utilities/Config.h"
#include "AttractModeDialog.h"

IMPLEMENT_DYNAMIC(AttractModeDialog, OptionsPage)

AttractModeDialog::AttractModeDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

AttractModeDialog::~AttractModeDialog()
{
}

void AttractModeDialog::InitVarMap()
{
	// set up the basic controls
	varMap.emplace_back(new CkBoxMap(_T("AttractMode.Enabled"), IDC_CK_ATTRACT_ENABLED, true));
	varMap.emplace_back(new SpinIntMap(_T("AttractMode.IdleTime"), IDC_EDIT_ATTRACT_IDLE_TIME, 180, IDC_SPIN_ATTRACT_IDLE_TIME, 0, 3600));
	varMap.emplace_back(new SpinIntMap(_T("AttractMode.SwitchTime"), IDC_EDIT_ATTRACT_SWITCH_TIME, 10, IDC_SPIN_ATTRACT_SWITCH_TIME, 0, 3600));
	varMap.emplace_back(new SpinIntMap(_T("AttractMode.StatusLine.UpdateTime"), IDC_EDIT_ATTRACT_MSG_TIME, 1000, IDC_SPIN_ATTRACT_MSG_TIME, 0, 3600));
	varMap.emplace_back(new StatusMessageMap(_T("AttractMode.StatusLine.Messages"), IDC_EDIT_ATTRACT_STATUS_MSG, _T("")));
	varMap.emplace_back(new CkBoxMap(_T("AttractMode.Mute"), IDC_CK_MUTE_ATTRACT_MODE, true));
	varMap.emplace_back(new CkBoxMap(_T("AttractMode.HideWheelImages"), IDC_CK_HIDE_WHEEL_IMAGES, true));
	varMap.emplace_back(new CkBoxMap(_T("AttractMode.HideInfoBox"), IDC_CK_HIDE_INFO_BOX, true));
}

