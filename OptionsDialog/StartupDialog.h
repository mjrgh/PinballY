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

protected:
	virtual void InitVarMap() override;

	BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;

	BOOL OnApply() override;
		
	struct MonVars : VarMap
	{
		MonVars(const TCHAR *configVar, int ckid, 
			int numMonEditId, int numMonSpinId,
			int waitTimeEditId, int waitTimeSpinId,
			int addedWaitEditId, int addedWaitSpinId) :
			VarMap(configVar, ckid, ckEnable),
			numMonEditId(numMonEditId), numMonSpinId(numMonSpinId),
			waitTimeEditId(waitTimeEditId), waitTimeSpinId(waitTimeSpinId),
			addedWaitEditId(addedWaitEditId), addedWaitSpinId(addedWaitSpinId)
		{ }

		CButton ckEnable;
		CEdit edNumMon;
		CEdit edWaitTime;
		CEdit edAddedWait;
		CSpinButtonCtrl spinNumMon;
		CSpinButtonCtrl spinWaitTime;
		CSpinButtonCtrl spinAddedWait;

		int numMonEditId;
		int numMonSpinId;
		int waitTimeEditId;
		int waitTimeSpinId;
		int addedWaitEditId;
		int addedWaitSpinId;

		struct Val
		{
			int enabled;
			int numMon;
			int waitTime;
			int addedWait;

			void LoadFromConfig();
		} val;

		virtual void ddxControl(CDataExchange *pDX) override
		{
			__super::ddxControl(pDX);
			DDX_Control(pDX, numMonEditId, edNumMon);
			DDX_Control(pDX, numMonSpinId, spinNumMon);
			DDX_Control(pDX, waitTimeEditId, edWaitTime);
			DDX_Control(pDX, waitTimeSpinId, spinWaitTime);
			DDX_Control(pDX, addedWaitEditId, edAddedWait);
			DDX_Control(pDX, addedWaitSpinId, spinAddedWait);
		}

		virtual void InitControl() override 
		{
			spinNumMon.SetRange(0, 10);
			spinWaitTime.SetRange(0, 3600);
			spinAddedWait.SetRange(0, 3600);
		}

		virtual void doDDX(CDataExchange *pDX) override
		{
			DDX_Check(pDX, controlID, val.enabled);
			DDX_Text(pDX, numMonEditId, val.numMon);
			DDX_Text(pDX, waitTimeEditId, val.waitTime);
			DDX_Text(pDX, addedWaitEditId, val.addedWait);
		}

		virtual void LoadConfigVar() override { val.LoadFromConfig(); }
		virtual void SaveConfigVar() override;
		virtual bool IsModifiedFromConfig() override;
	};

	class AutoLaunchMap : public RadioStrMap
	{
	public:
		AutoLaunchMap(const TCHAR *configVar, int controlID, const TCHAR *defVal, const TCHAR *const *vals, size_t nVals) :
			RadioStrMap(configVar, controlID, defVal, vals, nVals) { }

		virtual void LoadConfigVar() override;
		virtual bool IsModifiedFromConfig() override;
		int ConfigToRadio();
	};

	AutoLaunchMap *autoLaunchButtons;
	SpinIntMap *autoLaunchDelay;
};

