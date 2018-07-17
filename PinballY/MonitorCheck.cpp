// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "Resource.h"
#include "MonitorCheck.h"

MonitorCheck::MonitorCheck(int numMonitors, DWORD wait_ms) :
	numMonitors(numMonitors),
	wait_ms(wait_ms)
{
	// Look for the nVidia driver API.  The nVidia driver reportedly
	// fools the Windows API that enumerates monitors with its own
	// cache of hardware data.  Fortunately, nVidia provides its own
	// API that lets us control this.
	nvRefresh = nullptr;
	HMODULE nvlib = LoadLibrary(_T("NvCpl"));
	if (nvlib != NULL)
		nvRefresh = (NvRefreshProc)GetProcAddress(nvlib, "NvCplRefreshConnectedDevices");
}

MonitorCheck::~MonitorCheck()
{
}

bool MonitorCheck::WaitForMonitors(const TCHAR *str)
{
	// try matching the "N monitors, M seconds" pattern
	std::basic_regex<TCHAR> pat(_T("\\s*(\\d+)\\s*monitors?\\s*[\\s,]\\s*(\\d+)\\s*seconds?\\s*"), std::regex_constants::icase);
	std::match_results<const TCHAR *> m;
	if (std::regex_match(str, m, pat))
	{
		// monitor count in group 1, seconds in group 2
		int n = _ttoi(m[1].str().c_str());
		int ms = _ttoi(m[2].str().c_str()) * 1000;

		// do the wait
		return WaitForMonitors(n, ms);
	}
	else
	{
		// invalid syntax
		LogError(EIT_Warning, MsgFmt(IDS_ERR_MONWAITSYNTAX, str));
		return false;
	}

}

bool MonitorCheck::WaitForMonitors(int numMonitors, DWORD wait_ms)
{
	// create and show the dialog
	std::unique_ptr<MonitorCheck> dlg(new MonitorCheck(numMonitors, wait_ms));

	// if the required monitors are already attached, return success
	// without even showing the dialog
	if (dlg->CountMonitors() >= numMonitors)
		return true;

	// run the dialog
	dlg->Show(ID_DLG_MONITOR_WAIT);

	// check again for the required monitor count to determine the result
	return dlg->CountMonitors() >= numMonitors;
}

int MonitorCheck::CountMonitors()
{
	// Refresh the nVidia cache, if present.  This makes it more likely
	// that we're seeing accurate information about the live video devices
	// on a system with nVideo cards.
	if (nvRefresh != nullptr)
		nvRefresh(NVREFRESH_NONINTRUSIVE);

	// Count monitors, by enumerating all attached monitors through a
	// callback that simply keeps count of each time it's called.
	struct EnumContext
	{
		EnumContext() : cnt(0) { }
		int cnt;
	} ctx;
	EnumDisplayMonitors(NULL, NULL, [](HMONITOR hMon, HDC hdc, LPRECT rc, LPARAM lparam) -> BOOL
	{
		// count the monitor in the context
		auto ctx = reinterpret_cast<EnumContext*>(lparam);
		ctx->cnt += 1;

		// continue the enumeration
		return TRUE;
	}, (LPARAM)&ctx);

	// return the count
	return ctx.cnt;
}

INT_PTR MonitorCheck::Proc(UINT message, WPARAM wParam, LPARAM lParam)
{
	INT_PTR ret;
	const int UpdateTimerId = 101;
	switch (message)
	{
	case WM_INITDIALOG:
		// do the base class initialization first
		ret = __super::Proc(message, wParam, lParam);

		// set up a timer to do another check every so often
		SetTimer(hDlg, UpdateTimerId, 250, 0);

		// note the starting time, so that we can auto-cancel when we
		// reach the maximum wait time
		startTime = GetTickCount();

		// return the result from the base class
		return ret;

	case WM_TIMER:
		if (wParam == UpdateTimerId)
		{
			// Cancel the dialog if we've reached the timeout
			if (wait_ms != INFINITE && (DWORD)(GetTickCount() - startTime) > wait_ms)
				EndDialog(hDlg, IDCANCEL);

			// Dismiss the dialog if we've reached the desired monitor count
			if (CountMonitors() >= numMonitors)
				EndDialog(hDlg, IDOK);

			// timer handled
			return 0;
		}
		break;
	}

	// use the base class handling if we didn't override it
	return __super::Proc(message, wParam, lParam);
}

