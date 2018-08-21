// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class InfoBoxDialog : public OptionsPage
{
	DECLARE_DYNAMIC(InfoBoxDialog)

public:
	InfoBoxDialog(int dialogId);
	virtual ~InfoBoxDialog();

	virtual void InitVarMap() override;

protected:
	// log file path
	TSTRING logFilePath;
};
