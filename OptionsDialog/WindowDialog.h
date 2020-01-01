// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class WindowDialog : public OptionsPage
{
	DECLARE_DYNAMIC(WindowDialog)

public:
	WindowDialog(int dialogId);
	virtual ~WindowDialog();

	virtual void InitVarMap() override;
};
