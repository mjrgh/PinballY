// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class DOFDialog : public OptionsPage
{
	DECLARE_DYNAMIC(DOFDialog)

public:
	DOFDialog(int dialogId);
	virtual ~DOFDialog();

	virtual void InitVarMap() override;
};

