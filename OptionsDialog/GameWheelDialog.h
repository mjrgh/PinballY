// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class GameWheelDialog : public OptionsPage
{
	DECLARE_DYNAMIC(GameWheelDialog)

public:
	GameWheelDialog(int dialogId);
	virtual ~GameWheelDialog();

	virtual void InitVarMap() override;

	struct PagingModeMap : VarMap
	{
		PagingModeMap(const TCHAR *configVar, int controlID) :
			VarMap(configVar, controlID, combo) { }

		CComboBox combo;
		CString strVar;

		virtual void doDDX(CDataExchange *pDX) override { DDX_CBString(pDX, controlID, strVar); }

		virtual void LoadConfigVar() override;
		virtual void SaveConfigVar() override;
		virtual bool IsModifiedFromConfig() override;
	};
};

