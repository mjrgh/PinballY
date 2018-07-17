// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "StartupDialog.h"
#include "../Utilities/Config.h"

IMPLEMENT_DYNAMIC(StartupDialog, OptionsPage)

StartupDialog::StartupDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

StartupDialog::~StartupDialog()
{
}

void StartupDialog::InitVarMap()
{
	varMap.emplace_back(new CkBoxMap(_T("AutoLaunch"), IDC_CK_AUTOLAUNCH, false));
	varMap.emplace_back(new EditStrMap(_T("RunAtStartup"), IDC_EDIT_RUN_AT_STARTUP, _T("")));
	varMap.emplace_back(new EditStrMap(_T("RunAtExit"), IDC_EDIT_RUN_AT_EXIT, _T("")));
	varMap.emplace_back(new MonVars(_T("WaitForMonitors"), IDC_CK_MONITOR_WAIT, 
		IDC_EDIT_NUM_MONITORS, IDC_SPIN_NUM_MONITORS,
		IDC_EDIT_MON_WAIT_TIME, IDC_SPIN_MON_WAIT_TIME));
}

void StartupDialog::MonVars::Val::LoadFromConfig()
{
	// parse the string from the config
	TSTRING s = ConfigManager::GetInstance()->Get(_T("WaitForMonitors"), _T(""));
	std::basic_regex<TCHAR> pat(_T("\\s*(\\d+)\\s*monitors?\\s*[\\s,]\\s*(\\d+)\\s*seconds?\\s*"), std::regex_constants::icase);
	std::match_results<TSTRING::const_iterator> m;
	if (std::regex_match(s, m, pat))
	{
		// matched - enable it and store the values
		enabled = true;
		numMon = _ttoi(m[1].str().c_str());
		waitTime = _ttoi(m[2].str().c_str());
	}
	else
	{
		// the config string doesn't match the expected format; treat it
		// as disabled in the UI
		enabled = false;
		numMon = 0;
		waitTime = 0;
	}
}

void StartupDialog::MonVars::SaveConfigVar()
{
	// if it's enabled, build the "N monitors, M seconds" string
	TSTRINGEx s;
	if (val.enabled)
		s.Format(_T("%d monitors, %d seconds"), val.numMon, val.waitTime);

	// set the value in the config
	ConfigManager::GetInstance()->Set(_T("WaitForMonitors"), s.c_str());
}

bool StartupDialog::MonVars::IsModifiedFromConfig()
{
	// get the current config settings
	Val cfgVal;
	cfgVal.LoadFromConfig();

	// check if they match
	return cfgVal.enabled != val.enabled
		|| cfgVal.numMon != val.numMon
		|| cfgVal.waitTime != val.waitTime;
}

