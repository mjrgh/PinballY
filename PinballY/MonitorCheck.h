// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Monitor Checker.  Waits for Windows to recognize a given number
// of monitors attached to the system.  This can be used to delay
// the initial presentation of the UI until all of the required
// monitors are on-line.  This is useful when the program is 
// launched at system boot time, since Windows launches startup
// programs fairly early in the boot process, even before all
// monitors have finished powering up and connecting.  TVs in
// particular can be relatively slow to come online.  It can be
// problematic to show the UI before all monitors are ready, 
// because it might not be possible to restore window positions
// faithfully when the desktop layout is incomplete.
//
// This class displays a simple dialog if a wait is necessary,
// to let the user know why there's a delay and to allow manual
// cancellation if desired.

#pragma once

class MonitorCheck : public Dialog
{
public:
	~MonitorCheck();

	// Wait for the given number of monitors to come online, with
	// the given maximum wait time.  Returns true if all monitors
	// are available, false if not.  The wait time is in 
	// milliseconds; use INFINITE to wait forever.
	static bool WaitForMonitors(int numMonitors, DWORD max_wait_ms, DWORD extra_wait_ms);

	// Wait for monitors using the config file "WaitForMonitors"
	// string format.  This allows the following formats:
	//
	//  <N>
	//  <N> monitors
	//  <N> monitors, <S> seconds
	//  <N> monitors, <ms> ms|milliseconds
	//  <N> monitors, forever
	//
	// For the formats that don't specify a wait time, we use a
	// default wait time of 90 seconds.
	//
	static bool WaitForMonitors(const TCHAR *configString, DWORD extra_wait_ms);

protected:
	MonitorCheck(int numMonitors, DWORD max_wait_ms, DWORD extra_wait_ms);

	// dialog proc 
	virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam) override;

	// Count monitors currently in the system
	int CountMonitors();

	// Target monitor count
	int numMonitors;

	// Maximum waiting time, in milliseconds
	DWORD max_wait_ms;

	// Extra waiting time after the last monitor has checked in
	DWORD extra_wait_ms;

	// Starting time of current phase, as a GetTickCount() value
	DWORD startTime;

	// Current phase
	enum WaitPhase
	{
		MonitorWait,    // waiting for monitors to come online
		ExtraWait       // extra wait after monitors are online
	};
	WaitPhase phase = MonitorWait;

	// Pointer to NVidia DLL entrypoint to refresh the video device
	// cache.  We use this on systems with NVidia cards to get more
	// accurate information about the instantaneous physical device
	// layout.
	const DWORD NVREFRESH_NONINTRUSIVE = 1;
	typedef BOOL(APIENTRY *NvRefreshProc)(IN DWORD flags);
	NvRefreshProc nvRefresh;
};
