// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "GameLaunchDialog.h"
#include "../Utilities/Config.h"

IMPLEMENT_DYNAMIC(GameLaunchDialog, OptionsPage)

GameLaunchDialog::GameLaunchDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

GameLaunchDialog::~GameLaunchDialog()
{
}

static const TCHAR *showWindowsVar = _T("ShowWindowsWhileRunning");

void GameLaunchDialog::InitVarMap()
{
	varMap.emplace_back(new SpinIntMap(_T("GameTimeout"), IDC_EDIT_GAME_IDLE_TIME, 300, IDC_SPIN_GAME_IDLE_TIME, 0, 3600));
	varMap.emplace_back(new CkBoxMap(_T("HideTaskbarDuringGame"), IDC_CK_HIDE_TASKBAR, true));
	varMap.emplace_back(new KeepWindowCkMap(showWindowsVar, _T("bg"), IDC_CK_SHOW_WHEN_RUNNING_BG, false));
	varMap.emplace_back(new KeepWindowCkMap(showWindowsVar, _T("dmd"), IDC_CK_SHOW_WHEN_RUNNING_DMD, false));
	varMap.emplace_back(new KeepWindowCkMap(showWindowsVar, _T("realdmd"), IDC_CK_SHOW_WHEN_RUNNING_REALDMD, false));
	varMap.emplace_back(new KeepWindowCkMap(showWindowsVar, _T("topper"), IDC_CK_SHOW_WHEN_RUNNING_TOPPER, false));
	varMap.emplace_back(new KeepWindowCkMap(showWindowsVar, _T("instcard"), IDC_CK_SHOW_WHEN_RUNNING_INSTCARD, false));
}

BOOL GameLaunchDialog::OnApply()
{
	// do the base class work
	__super::OnApply();

	// update the ShowWindowsWhileRunning config value
	KeepWindowCkMap::OnApply(varMap);

	// changes accepted
	return TRUE;
}

