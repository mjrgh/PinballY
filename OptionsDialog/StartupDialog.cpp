// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "StartupDialog.h"
#include "OptionsDialog.h"
#include "../Utilities/AutoRun.h"
#include "../Utilities/Config.h"
#include "../Utilities/FileUtil.h"
#include "../Utilities/UtilResource.h"

IMPLEMENT_DYNAMIC(StartupDialog, OptionsPage)

StartupDialog::StartupDialog(int dialogId) :
	OptionsPage(dialogId)
{
	// Look up the task in Windows Task Scheduler, and use this
	// information to override whatever is in the configuration.
	bool exists;
	bool adminMode;
	TSTRING exe, params;
	if (GetAutoRunState(_T("PinballY"), exists, exe, params, adminMode, SilentErrorHandler()))
	{
		// presume we won't find a valid task
		const TCHAR *setting = _T("off");

		// if a task exists, validate the executable path
		if (exists)
		{
			// there's a task - check if it's regular or admin mode
			TCHAR actual[MAX_PATH];
			GetExeFilePath(actual, countof(actual));
			if (adminMode)
			{
				// admin mode - it should be our PinballY Admin Mode program
				PathAppend(actual, _T("PinballY Admin Mode.exe"));
				if (_tcsicmp(exe.c_str(), actual) == 0)
					setting = _T("admin");
			}
			else
			{
				// regular mode - make sure it matches our PinballY program
				PathAppend(actual, _T("PinballY.exe"));
				if (_tcsicmp(exe.c_str(), actual) == 0)
					setting = _T("on");
			}
		}

		// update the config setting to match the Task Scheduler state
		auto cfg = ConfigManager::GetInstance();
		if (_tcsicmp(cfg->Get(_T("AutoLaunch"), _T("off")), setting) != 0)
			cfg->Set(_T("AutoLaunch"), setting);
	}
}

StartupDialog::~StartupDialog()
{
}

void StartupDialog::InitVarMap()
{
	static const TCHAR *startupVals[] = { _T("off"), _T("on"), _T("admin") };

	varMap.emplace_back(autoLaunchButtons = new AutoLaunchMap(
		_T("AutoLaunch"), IDC_RB_START_MANUAL, _T("off"), startupVals, countof(startupVals)));
	varMap.emplace_back(new CkBoxMap(_T("SplashScreen"), IDC_CK_SPLASH_SCREEN, true));
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

int StartupDialog::AutoLaunchMap::ConfigToRadio()
{
	if (const TCHAR *val = ConfigManager::GetInstance()->Get(configVar, nullptr); val != nullptr)
	{
		// 0/false/no/off -> 0
		if (std::regex_match(val, std::basic_regex<TCHAR>(_T("0|false|f|no|n|off"), std::regex_constants::icase)))
			return 0;

		// 1/true/yes/on -> 1
		if (std::regex_match(val, std::basic_regex<TCHAR>(_T("1|true|t|yes|y|on"), std::regex_constants::icase)))
			return 1;

		// auto -> 2
		if (std::regex_match(val, std::basic_regex<TCHAR>(_T("admin"), std::regex_constants::icase)))
			return 2;
	}

	// not defined - use button 0 by default
	return 0;
}

void StartupDialog::AutoLaunchMap::LoadConfigVar()
{
	// get the config value as a radio button value
	intVar = ConfigToRadio();
}

bool StartupDialog::AutoLaunchMap::IsModifiedFromConfig()
{
	int cfgVal = ConfigToRadio();
	return cfgVal != intVar;
}

BOOL StartupDialog::OnCommand(WPARAM wParam, LPARAM lParam)
{
	// if switching to Admin mode, warn about UAC elevation
	if (wParam == IDC_RB_START_AUTO_ADMIN
		&& (autoLaunchButtons == nullptr || autoLaunchButtons->intVar != 2))
	{
		// Check if the Admin Host is running.  If so, there should be no UAC
		// prompt, so we don't have to issue this warning.
		if (auto par = dynamic_cast<MainOptionsDialog*>(GetParent()); par == nullptr || !par->IsAdminHostRunning())	
			MessageBox(LoadStringT(IDS_ADMIN_LAUNCH_WARNING), _T("PinballY"), MB_OK | MB_ICONINFORMATION);
	}

	// use the standard handling
	return __super::OnCommand(wParam, lParam);
}

BOOL StartupDialog::OnApply()
{
	// get the old config setting
	int oldAutoLaunch = autoLaunchButtons->ConfigToRadio();

	// do the base class work first
	if (!__super::OnApply())
		return FALSE;

	// if the auto-launch settings have changed, update the Task Scheduler entry
	if (auto a = autoLaunchButtons->intVar; a != oldAutoLaunch)
	{
		// get the main dialog
		auto mainDlg = dynamic_cast<MainOptionsDialog*>(GetParent()); 

		// get the executable file 
		TCHAR exe[MAX_PATH];
		DWORD result = SafeGetModuleFileName(NULL, exe, countof(exe));
		if (result == 0 || result == countof(exe))
		{
			LogSysError(EIT_Error, LoadStringT(IDS_ERR_SYNCAUTOLAUNCHREG),
				_T("Unable to get PinballY program file path - path is too long"));
			return OnApplyFail();
		}

		// see what we have for the new mode
		if (a == 0 || a == 1)
		{
			// Manual (0) or regular user-mode Auto (1).  We can make this 
			// change in normal user mode (without UAC elevation).  Get the 
			// program name, and add or remove the Task Scheduler task.
			if (!SetUpAutoRun(a == 1, _T("PinballY"), exe, nullptr, false, InteractiveErrorHandler()))
				return OnApplyFail();
		}
		else
		{
			// Admin Mode Auto Launch.  This change requires elevation, so we
			// can't do it from within this process directly; instead, we have
			// to launch a separate Admin mode program to do it.  If the Admin
			// Host is running, we can launch it through that without triggering
			// a UAC prompt.  Otherwise, we're in plain user mode, so we'll have
			// to do the launch via ShellExec() to trigger UAC elevation.  We
			// should have already warned the user that this will happen, so 
			// it shouldn't come as a surprise.
			if (mainDlg != nullptr && mainDlg->IsAdminHostRunning())
			{
				// The Admin Host is running, so it can launch the task
				// setup program in elevated mode on for us without any
				// UAC intervention.
				if (!mainDlg->setUpAdminAutoRunCallback())
					return OnApplyFail();
			}
			else
			{
				// The Admin Host isn't running, so we're stuck in regular
				// user mode.  We'll have to run the privileged task setup
				// program in admin mode explicitly via ShellExecute().
				// We prefer the Admin Host approach above because it
				// doesn't trigger a UAC dialog, but when we're running in
				// ordinary user mode to start with, UAC prompting is
				// appropriate for this privileged operation, in that the
				// user didn't pre-authorize us for privileged activity in
				// general.
				SHELLEXECUTEINFO ex;
				ex.cbSize = sizeof(ex);
				ex.fMask = SEE_MASK_NOCLOSEPROCESS;
				ex.hwnd = GetParent()->GetSafeHwnd();
				ex.lpVerb = _T("runas");
				ex.lpFile = exe;
				ex.lpParameters = _T(" /AutoLaunch=AdminMode");
				ex.lpDirectory = NULL;
				ex.nShow = SW_HIDE;
				ex.hInstApp = NULL;
				if (!ShellExecuteEx(&ex)) 
				{
					// get the error code
					WindowsErrorMessage err;

					// If the error was "cancelled by the user", there's no need to show
					// an error box telling the user what they know they just did.  Other
					// error codes need an explanation, though.
					if (err.GetCode() == ERROR_CANCELLED)
					{
						// canceled by user action - no need for an error message
					}
					else
					{
						// show the error
						LogSysError(EIT_Error, LoadStringT(IDS_ERR_SYNCAUTOLAUNCHREG),
							MsgFmt(_T("Unable to launch PinballY in Administrator mode: %s"), err.Get()));
					}

					return OnApplyFail();
				}

				// capture the process handle into a self-closing holder
				HandleHolder hProc = ex.hProcess;

				// wait for the subprocess to exit
				if (WaitForSingleObject(hProc, 5000) != WAIT_OBJECT_0)
				{
					LogSysError(EIT_Error, LoadStringT(IDS_ERR_SYNCAUTOLAUNCHREG),
						_T("Error waiting for task setup process, or process isn't responding"));
					return OnApplyFail();
				}

				// Check the process return code.  If it's non-zero, consider it a
				// failure.  There's no need for an error message in this case,
				// though, since the launched program will show its own error
				// dialogs as needed.
				DWORD exitCode;
				GetExitCodeProcess(hProc, &exitCode);
				if (exitCode != 0)
					return OnApplyFail();
			}
		}
	}

	// success
	return TRUE;
}

