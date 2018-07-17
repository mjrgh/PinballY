// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "StatuslineDialog.h"
#include "../Utilities/Config.h"

IMPLEMENT_DYNAMIC(StatuslineDialog, OptionsPage)

StatuslineDialog::StatuslineDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

StatuslineDialog::~StatuslineDialog()
{
}

void StatuslineDialog::InitVarMap()
{
	varMap.emplace_back(new StatusMessageMap(_T("UpperStatus.Messages"), IDC_EDIT_UPPER_STATUS, _T("")));
	varMap.emplace_back(new SpinIntMap(_T("UpperStatus.UpdateTime"), IDC_EDIT_UPPER_STATUS_TIME, 2513, IDC_SPIN_UPPER_STATUS_TIME, 100, 10000));
	varMap.emplace_back(new StatusMessageMap(_T("LowerStatus.Messages"), IDC_EDIT_LOWER_STATUS, _T("")));
	varMap.emplace_back(new SpinIntMap(_T("LowerStatus.UpdateTime"), IDC_EDIT_LOWER_STATUS_TIME, 2233, IDC_SPIN_LOWER_STATUS_TIME, 100, 10000));
}

