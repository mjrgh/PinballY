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
	varMap.emplace_back(new EditFloatPctMap(_T("Underlay.Height"), IDC_EDIT_UNDERLAY_HEIGHT, 20.7292f));
	varMap.emplace_back(new EditFloatPctMap(_T("Underlay.YOffset"), IDC_EDIT_UNDERLAY_YOFFSET, 0.0f));
	varMap.emplace_back(new EditFloatPctMap(_T("Underlay.MaxWidth"), IDC_EDIT_UNDERLAY_MAXWID, 1000.0f));
	varMap.emplace_back(new CkBoxMap(_T("Underlay.Enable"), IDC_CK_ENABLE_UNDERLAY, true));
	varMap.emplace_back(new PagingModeMap(_T("GameList.PagingMode"), IDC_CB_PAGINGMODE));
	varMap.emplace_back(new EditStrMap(_T("Wheel.AutoRepeatRate"), IDC_EDIT_AUTOREPEATRATE, _T("")));
}

void GameWheelDialog::PagingModeMap::LoadConfigVar()
{
	strVar = ConfigManager::GetInstance()->Get(configVar, _T("Default"));
}

void GameWheelDialog::PagingModeMap::SaveConfigVar()
{
	ConfigManager::GetInstance()->Set(configVar, strVar);
}

bool GameWheelDialog::PagingModeMap::IsModifiedFromConfig()
{
	auto cfgVal = ConfigManager::GetInstance()->Get(configVar, _T("Default"));
	return strVar != cfgVal;
}
