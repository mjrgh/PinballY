// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class CaptureDialog : public OptionsPage
{
	DECLARE_DYNAMIC(CaptureDialog)

public:
	CaptureDialog(int dialogId);
	virtual ~CaptureDialog();

	virtual void InitVarMap() override;

protected:
	DECLARE_MESSAGE_MAP()

	struct ManualStartButtonMap : VarMap
	{
		ManualStartButtonMap(const TCHAR *configVar, int controlID) :
			VarMap(configVar, controlID, combo) { }

		// settings values as they appear in the config file
		static const TCHAR *buttonNames[];

		CComboBox combo;
		int intVar;

		virtual void doDDX(CDataExchange *pDX) override { DDX_CBIndex(pDX, controlID, intVar); }

		virtual void LoadConfigVar() override;
		virtual void SaveConfigVar() override;
		virtual bool IsModifiedFromConfig() override;
	};
};
