// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class StartupDialog : public OptionsPage
{
	DECLARE_DYNAMIC(StartupDialog)

public:
	StartupDialog(int dialogId);
	virtual ~StartupDialog();

	virtual void InitVarMap() override;

	struct MonVars : VarMap
	{
		MonVars(const TCHAR *configVar, int ckid, 
			int numMonEditId, int numMonSpinId,
			int waitTimeEditId, int waitTimeSpinId) :
			VarMap(configVar, ckid, ckEnable),
			numMonEditId(numMonEditId), numMonSpinId(numMonSpinId),
			waitTimeEditId(waitTimeEditId), waitTimeSpinId(waitTimeSpinId)
		{ }

		CButton ckEnable;
		CEdit edNumMon;
		CEdit edWaitTime;
		CSpinButtonCtrl spinNumMon;
		CSpinButtonCtrl spinWaitTime;

		int numMonEditId;
		int numMonSpinId;
		int waitTimeEditId;
		int waitTimeSpinId;

		struct Val
		{
			int enabled;
			int numMon;
			int waitTime;

			void LoadFromConfig();
		} val;

		virtual void ddxControl(CDataExchange *pDX) override
		{
			__super::ddxControl(pDX);
			DDX_Control(pDX, numMonEditId, edNumMon);
			DDX_Control(pDX, numMonSpinId, spinNumMon);
			DDX_Control(pDX, waitTimeEditId, edWaitTime);
			DDX_Control(pDX, waitTimeSpinId, spinWaitTime);
		}

		virtual void InitControl() override 
		{
			spinNumMon.SetRange(0, 10);
			spinWaitTime.SetRange(0, 3600);
		}

		virtual void doDDX(CDataExchange *pDX) override
		{
			DDX_Check(pDX, controlID, val.enabled);
			DDX_Text(pDX, numMonEditId, val.numMon);
			DDX_Text(pDX, waitTimeEditId, val.waitTime);
		}

		virtual void LoadConfigVar() override { val.LoadFromConfig(); }
		virtual void SaveConfigVar() override;
		virtual bool IsModifiedFromConfig() override;
	};


};

