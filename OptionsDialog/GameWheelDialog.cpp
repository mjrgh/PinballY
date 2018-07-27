// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "GameWheelDialog.h"
#include "../Utilities/Config.h"

IMPLEMENT_DYNAMIC(GameWheelDialog, OptionsPage)

GameWheelDialog::GameWheelDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

GameWheelDialog::~GameWheelDialog()
{
}

void GameWheelDialog::InitVarMap()
{
	varMap.emplace_back(new CkBoxMap(_T("GameList.HideUnconfigured"), IDC_CK_HIDE_UNCONFIG, false));
}
