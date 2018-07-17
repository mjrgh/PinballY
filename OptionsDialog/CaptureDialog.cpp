// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "CaptureDialog.h"
#include "../Utilities/Config.h"

IMPLEMENT_DYNAMIC(CaptureDialog, OptionsPage)

CaptureDialog::CaptureDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

CaptureDialog::~CaptureDialog()
{
}

void CaptureDialog::InitVarMap()
{
	// set up the basic controls
	varMap.emplace_back(new SpinIntMap(_T("Capture.StartupDelay"), IDC_EDIT_CAP_STARTUP_DELAY, 10, IDC_SPIN_CAP_STARTUP_DELAY, 0, 120));
	varMap.emplace_back(new SpinIntMap(_T("Capture.PlayfieldVideoTime"), IDC_EDIT_CAP_PF_TIME, 30, IDC_SPIN_CAP_PF_TIME, 1, 120));
	varMap.emplace_back(new SpinIntMap(_T("Capture.BackglassVideoTime"), IDC_EDIT_CAP_BG_TIME, 30, IDC_SPIN_CAP_BG_TIME, 1, 120));
	varMap.emplace_back(new SpinIntMap(_T("Capture.DMDVideoTime"), IDC_EDIT_CAP_DMD_TIME, 30, IDC_SPIN_CAP_DMD_TIME, 1, 120));
	varMap.emplace_back(new SpinIntMap(_T("Capture.TopperVideoTime"), IDC_EDIT_CAP_TOPPER_TIME, 30, IDC_SPIN_CAP_TOPPER_TIME, 1, 120));
}

