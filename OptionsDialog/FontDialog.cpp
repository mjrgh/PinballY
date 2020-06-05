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
	varMap.emplace_back(new FontVarMap(&allFonts, _T("LaunchStatusFont"), IDC_CB_LAUNCHMSG_FONT, IDC_CB_LAUNCHMSG_FONT_PTS, IDC_CB_LAUNCHMSG_FONT_WT));
	varMap.emplace_back(new FontVarMap(&allFonts, _T("TTHighScoreFont"), IDC_CB_TTHISCORE_FONT, IDC_CB_TTHISCORE_FONT_PTS, IDC_CB_TTHISCORE_FONT_WT));

	varMap.emplace_back(new ColorButtonMap(_T("MenuTextColor"), IDC_CLR_MENUS, RGB(0xff, 0xff, 0xff)));
	varMap.emplace_back(new ColorButtonMap(_T("MenuBackgroundColor"), IDC_CLR_MENUBKG, RGB(0x00, 0x00, 0x00)));
	varMap.emplace_back(new ColorButtonMap(_T("MenuHiliteColor"), IDC_CLR_MENUHILITE, RGB(0x40, 0xA0, 0xFF)));
	varMap.emplace_back(new ColorButtonMap(_T("MenuGroupTextColor"), IDC_CLR_MENUGROUPTEXT, RGB(0x00, 0xFF, 0xFF)));
	varMap.emplace_back(new ColorButtonMap(_T("MenuHeaderColor"), IDC_CLR_MENUHDRS, RGB(0xff, 0xff, 0xff)));
	varMap.emplace_back(new ColorButtonMap(_T("PopupTitleColor"), IDC_CLR_POPUPTITLES, RGB(0xff, 0xff, 0xff)));
	varMap.emplace_back(new ColorButtonMap(_T("PopupTextColor"), IDC_CLR_POPUPS, RGB(0xff, 0xff, 0xff)));
	varMap.emplace_back(new ColorButtonMap(_T("PopupBackgroundColor"), IDC_CLR_POPUPBKG, RGB(0x00, 0x00, 0x00)));
	varMap.emplace_back(new ColorButtonMap(_T("PopupSmallTextColor"), IDC_CLR_POPUPSMALL, RGB(0xff, 0xff, 0xff)));
	varMap.emplace_back(new ColorButtonMap(_T("PopupDetailTextColor"), IDC_CLR_POPUPDETAIL, RGB(0xA0, 0xA0, 0xA0)));
	varMap.emplace_back(new ColorButtonMap(_T("MediaDetailTextColor"), IDC_CLR_MEDIADETAIL, RGB(0xff, 0xff, 0xff)));
	varMap.emplace_back(new ColorButtonMap(_T("WheelTitleColor"), IDC_CLR_WHEELTITLES, RGB(0xff, 0xff, 0xff)));
	varMap.emplace_back(new ColorButtonMap(_T("WheelTitleShadow"), IDC_CLR_WHEELTITLESHADOW, RGB(0x00, 0x00, 0x00)));
	varMap.emplace_back(new ColorButtonMap(_T("HiScoreTextColor"), IDC_CLR_HISCORES, RGB(0xff, 0xff, 0xff)));
	varMap.emplace_back(new ColorButtonMap(_T("InfoBoxTitleColor"), IDC_CLR_INFOBOXTITLES, RGB(0xff, 0xff, 0xff)));
	varMap.emplace_back(new ColorButtonMap(_T("InfoBoxTextColor"), IDC_CLR_INFOBOXTEXT, RGB(0xff, 0xff, 0xff)));
	varMap.emplace_back(new ColorButtonMap(_T("InfoBoxBackgroundColor"), IDC_CLR_INFOBOXBKG, RGB(0x00, 0x00, 0x00)));
	varMap.emplace_back(new ColorButtonMap(_T("InfoBoxDetailTextColor"), IDC_CLR_INFOBOXDETAILS, RGB(0xC0, 0xC0, 0xC0)));
	varMap.emplace_back(new ColorButtonMap(_T("StatusLineTextColor"), IDC_CLR_STATUSLINETEXT, RGB(0xff, 0xff, 0xff)));
	varMap.emplace_back(new ColorButtonMap(_T("StatusLineShadowColor"), IDC_CLR_STATUSLINESHADOW, RGB(0x00, 0x00, 0x00)));
	varMap.emplace_back(new ColorButtonMap(_T("CreditsTextColor"), IDC_CLR_CREDITSTEXT, RGB(0xff, 0xff, 0xff)));
	varMap.emplace_back(new ColorButtonMap(_T("LaunchStatusTextColor"), IDC_CLR_LAUNCHMSGTEXT, RGB(0xff, 0xff, 0xff)));
	varMap.emplace_back(new ColorButtonMap(_T("LaunchStatusBackgroundColor"), IDC_CLR_LAUNCHMSGBKG, RGB(0x1E, 0x1E, 0x1E)));
	varMap.emplace_back(new ColorButtonMap(_T("TTHighScoreTextColor"), IDC_CLR_TTHISCORETEXT, RGB(0x00, 0x00, 0x00)));
}
