// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "DOFDialog.h"
#include "../Utilities/Config.h"

IMPLEMENT_DYNAMIC(DOFDialog, OptionsPage)

DOFDialog::DOFDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

DOFDialog::~DOFDialog()
{
}

void DOFDialog::InitVarMap()
{
	varMap.emplace_back(new CkBoxMap(_T("DOF.Enable"), IDC_CK_DOF_ENABLED, true));
}
