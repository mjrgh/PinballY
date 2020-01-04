// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "WindowDialog.h"
#include "../Utilities/Config.h"

IMPLEMENT_DYNAMIC(WindowDialog, OptionsPage)

WindowDialog::WindowDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

WindowDialog::~WindowDialog()
{
}

void WindowDialog::InitVarMap()
{
	varMap.emplace_back(new CkBoxMap(_T("DMDWindow.KeepInFrontOfBg"), IDC_CK_DMD_IN_FRONT, false));
	varMap.emplace_back(new CkBoxMap(_T("PlayfieldWindow.TopmostDuringGameLaunch"), IDC_CK_TOPMOST_DURING_LAUNCH, false));
}

