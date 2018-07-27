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

void GameLaunchDialog::InitVarMap()
{
	varMap.emplace_back(new SpinIntMap(_T("GameTimeout"), IDC_EDIT_GAME_IDLE_TIME, 300, IDC_SPIN_GAME_IDLE_TIME, 0, 3600));
	varMap.emplace_back(new CkBoxMap(_T("HideTaskbarDuringGame"), IDC_CK_HIDE_TASKBAR, true));
}

