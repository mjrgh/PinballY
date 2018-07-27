// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "MenuDialog.h"
#include "../Utilities/Config.h"

IMPLEMENT_DYNAMIC(MenuDialog, OptionsPage)

MenuDialog::MenuDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

MenuDialog::~MenuDialog()
{
}

void MenuDialog::InitVarMap()
{
	static const TCHAR *exitBtnOpts[] = { _T("Select"), _T("Cancel") };
	varMap.emplace_back(new RadioStrMap(
		_T("ExitMenu.ExitKeyMode"), IDC_RB_EXIT_SELECT, exitBtnOpts[0], exitBtnOpts, countof(exitBtnOpts)));

	varMap.emplace_back(new CkBoxMap(_T("ExitMenu.Enabled"), IDC_CK_ENABLE_EXIT_MENU, true));
	varMap.emplace_back(new CkBoxMap(_T("ExitMenu.ShowOperatorMenu"), IDC_CK_SHOW_OP_MENU_IN_EXIT_MENU, false));
}

