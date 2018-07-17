// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class SystemDialog;

class SysGroupDialog : public OptionsPage
{
	DECLARE_DYNAMIC(SysGroupDialog)

public:
	SysGroupDialog(int dialogId);
	virtual ~SysGroupDialog();

	virtual void InitVarMap() override { }

	// Apply changes.  This commits any pending system deletions.
	BOOL OnApply();

	// Mark a system for deletion.  This doesn't actually carry out
	// the deletion; it just adds the system to an internal list.  The
	// deletion is carried out the user clicks "Apply" or "OK".
	void MarkForDeletion(SystemDialog *sysDlg);

protected:
	// command handler
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;

	// Internal list of systems marked for deletion.  Systems are
	// identified by system number (the N in the "SystemN.xxx" 
	// config variables).
	std::list<int> systemsPendingDeletion;
};
