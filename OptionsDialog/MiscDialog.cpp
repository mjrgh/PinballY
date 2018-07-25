// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "MiscDialog.h"
#include "../Utilities/Config.h"

IMPLEMENT_DYNAMIC(MiscDialog, OptionsPage)

MiscDialog::MiscDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

MiscDialog::~MiscDialog()
{
}

void MiscDialog::InitVarMap()
{
	varMap.emplace_back(new SpinIntMap(_T("GameTimeout"), IDC_EDIT_GAME_IDLE_TIME, 300, IDC_SPIN_GAME_IDLE_TIME, 0, 3600));

	static const TCHAR *instCardLocs[] = { _T("Playfield"), _T("Backglass"), _T("Topper") };
	varMap.emplace_back(new RadioStrMap(
		_T("InstructionCardLocation"), IDC_RB_INST_PF, instCardLocs[1], instCardLocs, countof(instCardLocs)));

	static const TCHAR *exitBtnOpts[] = { _T("Select"), _T("Cancel") };
	varMap.emplace_back(new RadioStrMap(
		_T("ExitMenu.ExitKeyMode"), IDC_RB_EXIT_SELECT, exitBtnOpts[0], exitBtnOpts, countof(exitBtnOpts)));

	varMap.emplace_back(new CkBoxMap(_T("ExitMenu.Enabled"), IDC_CK_ENABLE_EXIT_MENU, true));
	varMap.emplace_back(new CkBoxMap(_T("ExitMenu.ShowOperatorMenu"), IDC_CK_SHOW_OP_MENU_IN_EXIT_MENU, false));
	varMap.emplace_back(new CkBoxMap(_T("HideTaskbarDuringGame"), IDC_CK_HIDE_TASKBAR, true));
}

