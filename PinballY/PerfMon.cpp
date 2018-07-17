// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <Pdh.h>
#include <PdhMsg.h>
#include <timeapi.h>
#include <d3d11_1.h>
#include <DirectXMath.h>
#include "PerfMon.h"

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "winmm.lib")

PerfMon::PerfMon(float rolling_period_seconds)
{
	// clear the master frame counter
	nFrames = 0;

	// start at the first rolling timer
	curRolling = 0;

	// set all start times to now plus a staggered start offset
	int64_t now = timer.GetTime_ticks();
	cur.t0 = now;
	int64_t stagger = int64_t(rolling_period_seconds / timer.GetTickTime_sec() / countof(rolling));
	for (int i = 0; i < countof(rolling); ++i)
		rolling[i].t0 = now + i*stagger;

	// remember the rolling average time
	this->rolling_period_sec = rolling_period_seconds;

	// initialize a CPU performance query
	hCpuQuery = 0;
	hCpuCounter = 0;
	nCpuCores = 0;
	if (PdhOpenQuery(nullptr, 0, &hCpuQuery) == ERROR_SUCCESS)
	{
		// Add a counter for the overall CPU 
		PdhAddCounter(
			hCpuQuery, _T("\\Processor(_Total)\\% Processor Time"), 0,
			&hCpuCounter);

		// Count the CPUs by querying "Processor" instances.  First see how much
		// buffer space we need to supply to the query call.
		DWORD listBufSize = 0, instBufSize = 0;
		if (PdhEnumObjectItems(nullptr, nullptr, _T("Processor"),
			0, &listBufSize, 0, &instBufSize, PERF_DETAIL_WIZARD, FALSE)
			== PDH_MORE_DATA)
		{
			// Allocate buffers and query for real this time.
			TCHAR *listBuf = new TCHAR[listBufSize + 1];
			TCHAR *instBuf = new TCHAR[instBufSize + 1];
			if (PdhEnumObjectItems(nullptr, nullptr, _T("Processor"),
				listBuf, &listBufSize, instBuf, &instBufSize,
				PERF_DETAIL_WIZARD, FALSE)
				== ERROR_SUCCESS)
			{
				// Set up a counter for each CPU core.  The instance buffer
				// contains a list of null-terminated strings, packed one
				// after another in the buffer, with the CPU IDs.
				TCHAR *p = instBuf;
				while (*p != 0 && nCpuCores < countof(hCoreCounter))
				{
					// skip the "_Total" entry, as we call that out separately
					if (_tcscmp(p, _T("_Total")) != 0)
					{
						// add the "% Processor Time" counter for this CPU
						TCHAR counterName[128];
						_stprintf_s(
							counterName,
							_T("\\Processor(%s)\\%% Processor Time"), p);
						if (PdhAddCounter(hCpuQuery, counterName, 0,
							&hCoreCounter[nCpuCores]) != ERROR_SUCCESS)
							break;

						// count it and skip to the next
						++nCpuCores;
					}

					// skip to the next string
					p += _tcslen(p) + 1;
				}
			}

			// done with the buffers
			delete[] listBuf;
			delete[] instBuf;
		}
	}
}

PerfMon::~PerfMon()
{
	// close our performance query handle
	if (hCpuQuery != 0)
		PdhCloseQuery(hCpuQuery);
}

bool PerfMon::GetCurFPS(float &result, float min_time_sec)
{
	// get the elapsed time
	int64_t now = timer.GetTime_ticks();
	float dt = float((now - cur.t0) * timer.GetTickTime_sec());

	// if the minimum time hasn't elapsed yet, return failure
	if (dt < min_time_sec)
		return false;

	// figure the FPS rate - elapsed frames divided by elapsed time
	result = float(int32_t(nFrames - cur.n0)) / dt;

	// reset the counter
	cur.n0 = nFrames;
	cur.t0 = now;

	// return success
	return true;
}

float PerfMon::GetRollingFPS()
{
	// get the elapsed time on the current rolling timer
	int64_t now = timer.GetTime_ticks();
	float dt = float((now - rolling[curRolling].t0) * timer.GetTickTime_sec());

	// if this rolling window has been going on for quite long enough,
	// move to the next one
	if (dt > rolling_period_sec)
	{
		// reset the start time for the current one to now
		rolling[curRolling].n0 = nFrames;
		rolling[curRolling].t0 = now;

		// advance to the next one
		if (++curRolling >= countof(rolling))
			curRolling = 0;
	}

	// return the average for the current period
	return float((nFrames - rolling[curRolling].n0) / dt);
}

bool PerfMon::GetCPUMetrics(CPUMetrics &metrics)
{
	// we can only proceed if we have 
	if (hCpuQuery == 0)
		return false;

	// collect a counter snapshot
	PdhCollectQueryData(hCpuQuery);

	// set the CPU count in the results
	metrics.nCpus = nCpuCores;

	// query the overall CPU load
	PDH_FMT_COUNTERVALUE value;
	PdhGetFormattedCounterValue(hCpuCounter, PDH_FMT_LONG, nullptr, &value);
	metrics.cpuLoad = value.longValue;

	// query each CPU
	for (int i = 0; i < nCpuCores; ++i)
	{
		PdhGetFormattedCounterValue(hCoreCounter[i], PDH_FMT_LONG, nullptr, &value);
		metrics.coreLoad[i] = value.longValue;
	}

	// success
	return true;
}
