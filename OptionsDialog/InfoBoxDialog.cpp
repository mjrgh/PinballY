// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "InfoBoxDialog.h"
#include "../Utilities/Config.h"
#include "../Utilities/FileUtil.h"

IMPLEMENT_DYNAMIC(InfoBoxDialog, OptionsPage)

InfoBoxDialog::InfoBoxDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

InfoBoxDialog::~InfoBoxDialog()
{
}

void InfoBoxDialog::InitVarMap()
{
	varMap.emplace_back(new CkBoxMap(_T("InfoBox.Show"), IDC_CK_SHOW_INFO_BOX, true));
	varMap.emplace_back(new CkBoxMap(_T("InfoBox.Title"), IDC_CK_INFOBOX_TITLE, true));
	varMap.emplace_back(new CkBoxMap(_T("InfoBox.GameLogo"), IDC_CK_INFOBOX_GAMELOGO, false));
	varMap.emplace_back(new CkBoxMap(_T("InfoBox.Manufacturer"), IDC_CK_INFOBOX_MANUF, true));
	varMap.emplace_back(new CkBoxMap(_T("InfoBox.ManufacturerLogo"), IDC_CK_INFOBOX_MANUF_LOGO, true));
	varMap.emplace_back(new CkBoxMap(_T("InfoBox.System"), IDC_CK_INFOBOX_SYSTEM, true));
	varMap.emplace_back(new CkBoxMap(_T("InfoBox.SystemLogo"), IDC_CK_INFOBOX_SYSTEM_LOGO, true));
	varMap.emplace_back(new CkBoxMap(_T("InfoBox.Year"), IDC_CK_INFOBOX_YEAR, true));
	varMap.emplace_back(new CkBoxMap(_T("InfoBox.TableType"), IDC_CK_INFOBOX_TABLETYPE, false));
	varMap.emplace_back(new CkBoxMap(_T("InfoBox.TableTypeAbbr"), IDC_CK_INFOBOX_TABLETYPE_ABBR, false));
	varMap.emplace_back(new CkBoxMap(_T("InfoBox.Rating"), IDC_CK_INFOBOX_RATING, true));
	varMap.emplace_back(new CkBoxMap(_T("InfoBox.TableFile"), IDC_CK_INFOBOX_TABLEFILE, false));
}
