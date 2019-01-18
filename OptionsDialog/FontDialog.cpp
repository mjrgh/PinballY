// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "../Utilities/Config.h"
#include "FontDialog.h"

IMPLEMENT_DYNAMIC(FontDialog, OptionsPage)

FontDialog::FontDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

FontDialog::~FontDialog()
{
}

void FontDialog::InitVarMap()
{
	CFontPreviewCombo::InitFonts(allFonts, this, 16, _T("abcABC"));

	varMap.emplace_back(new FontComboMap(&allFonts, _T("DefaultFontFamily"), IDC_CB_DEFAULT_FONT, _T("*")));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("MenuFont"), IDC_CB_MENU_FONT, IDC_CB_MENU_FONT_PTS, IDC_CB_MENU_FONT_WT));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("MenuHeaderFont"), IDC_CB_MENUHDR_FONT, IDC_CB_MENUHDR_FONT_PTS, IDC_CB_MENUHDR_FONT_WT));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("PopupTitleFont"), IDC_CB_POPUPTITLE_FONT, IDC_CB_POPUPTITLE_FONT_PTS, IDC_CB_POPUPTITLE_FONT_WT));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("PopupFont"), IDC_CB_POPUP_FONT, IDC_CB_POPUP_FONT_PTS, IDC_CB_POPUP_FONT_WT));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("PopupSmallerFont"), IDC_CB_POPUPSMALLER_FONT, IDC_CB_POPUPSMALLER_FONT_PTS, IDC_CB_POPUPSMALLER_FONT_WT));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("PopupDetailFont"), IDC_CB_POPUPDETAIL_FONT, IDC_CB_POPUPDETAIL_FONT_PTS, IDC_CB_POPUPDETAIL_FONT_WT));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("MediaDetailFont"), IDC_CB_MEDIADETAIL_FONT, IDC_CB_MEDIADETAIL_FONT_PTS, IDC_CB_MEDIADETAIL_FONT_WT));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("WheelFont"), IDC_CB_WHEEL_FONT, IDC_CB_WHEEL_FONT_PTS, IDC_CB_WHEEL_FONT_WT));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("HighScoreFont"), IDC_CB_HISCORE_FONT, IDC_CB_HISCORE_FONT_PTS, IDC_CB_HISCORE_FONT_WT));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("InfoBoxTitleFont"), IDC_CB_INFOBOXTITLE_FONT, IDC_CB_INFOBOXTITLE_FONT_PTS, IDC_CB_INFOBOXTITLE_FONT_WT));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("InfoBoxFont"), IDC_CB_INFOBOX_FONT, IDC_CB_INFOBOX_FONT_PTS, IDC_CB_INFOBOX_FONT_WT));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("InfoBoxDetailFont"), IDC_CB_INFOBOXDETAIL_FONT, IDC_CB_INFOBOXDETAIL_FONT_PTS, IDC_CB_INFOBOXDETAIL_FONT_WT));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("StatusFont"), IDC_CB_STATUS_FONT, IDC_CB_STATUS_FONT_PTS, IDC_CB_STATUS_FONT_WT));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("CreditsFont"), IDC_CB_CREDITS_FONT, IDC_CB_CREDITS_FONT_PTS, IDC_CB_CREDITS_FONT_WT));
}
