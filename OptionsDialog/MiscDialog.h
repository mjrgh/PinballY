// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class MiscDialog : public OptionsPage
{
	DECLARE_DYNAMIC(MiscDialog)

public:
	MiscDialog(int dialogId);
	virtual ~MiscDialog();

	virtual void InitVarMap() override;
};

