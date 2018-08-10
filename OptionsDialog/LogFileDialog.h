// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class LogFileDialog : public OptionsPage
{
	DECLARE_DYNAMIC(LogFileDialog)

public:
	LogFileDialog(int dialogId);
	virtual ~LogFileDialog();

	virtual void InitVarMap() override;

protected:
	virtual BOOL OnInitDialog() override;
	virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT *pResult) override;

	// log file path
	TSTRING logFilePath;
};
