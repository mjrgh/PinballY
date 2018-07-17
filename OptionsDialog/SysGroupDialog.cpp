// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "SysGroupDialog.h"
#include "SystemDialog.h"
#include "OptionsDialog.h"
#include "../Utilities/Config.h"

IMPLEMENT_DYNAMIC(SysGroupDialog, OptionsPage)

SysGroupDialog::SysGroupDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

SysGroupDialog::~SysGroupDialog()
{
}

void SysGroupDialog::MarkForDeletion(SystemDialog *sysDlg)
{
	// add this to my deletion list
	systemsPendingDeletion.push_back(sysDlg->GetSysNum());

	// flag the unsaved change
	SetDirty();
}

BOOL SysGroupDialog::OnApply()
{
	// commit any pending deletions
	for (auto sysNum : systemsPendingDeletion)
	{
		// delete our main config variable ("SystemN")
		auto cfg = ConfigManager::GetInstance();
		cfg->Delete(MsgFmt(_T("System%d"), sysNum));

		// delete all of the sub vars ("SystemN.xxx")
		MsgFmt varPrefix(_T("System%d."), sysNum);
		cfg->Delete([&varPrefix](const TSTRING &name) {
			return tstrStartsWith(name.c_str(), varPrefix.Get());
		});
	}

	// inherit the default handling
	return __super::OnApply();
}

BOOL SysGroupDialog::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (LOWORD(wParam))
	{
	case IDC_BTN_NEW_SYSTEM:
		if (auto parDlg = dynamic_cast<MainOptionsDialog*>(GetParent()); parDlg != nullptr)
			parDlg->AddNewSystem();
		break;
	}

	// invoke the default handler
	return __super::OnCommand(wParam, lParam);
}

