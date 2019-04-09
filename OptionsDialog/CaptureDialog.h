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

	// audio device dropdown
	struct AudioDeviceMap : VarMap
	{
		AudioDeviceMap(const TCHAR *configVar, int controlId) :
			VarMap(configVar, controlId, combo)
		{ }

		CString val;
		CComboBox combo;

		// on initialization, populate the combo list with the available audio input devices
		virtual void InitControl() override;

		virtual void doDDX(CDataExchange *pDX) override { DDX_Text(pDX, controlID, val); }

		virtual void LoadConfigVar() override
		{
			val = ConfigManager::GetInstance()->Get(configVar, _T(""));
		}

		virtual void SaveConfigVar() override
		{
			// the zeroth list item is always "Default", which is rendered in
			// the config as an empty string
			const TCHAR *s = (combo.GetCurSel() == 0 ? _T("") : val.GetString());
			ConfigManager::GetInstance()->Set(configVar, s);
		}

		virtual bool IsModifiedFromConfig() override
		{
			// the zeroth list item is always "Default", which is rendered in
			// the config as an empty string
			const TCHAR *s = (combo.GetCurSel() == 0 ? _T("") : val.GetString());
			auto t = ConfigManager::GetInstance()->Get(configVar, _T(""));
			return _tcscmp(t, s) != 0;
		}
	};

protected:
	DECLARE_MESSAGE_MAP()
	afx_msg void OnClickAudioHelp(NMHDR *pNMHDR, LRESULT *pResult);

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
