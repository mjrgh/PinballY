// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Performance monitoring

#pragma once

#include "stdafx.h"
#include <Pdh.h>
#include "HiResTimer.h"


class PerfMon
{
public:
	PerfMon(float rolling_period_seconds);
	~PerfMon();

	// count a frame
	inline void CountFrame() { ++nFrames; }

	// Get the instantaneous frames per second.  If the minimum time has
	// elapsed, resets the cycle, fills in result with the current FPS
	// rate, and returns true.  If the minimum time hasn't elapsed, simply
	// returns false.
	bool GetCurFPS(float &result, float min_time_sec);

	// Get the current rolling average frames per second.  This
	// returns the current average and does internal housekeeping
	// on the internal counters.
	float GetRollingFPS();

	// CPU metrics object
	struct CPUMetrics
	{
		int cpuLoad;		// overall percentage CPU load
		int nCpus;			// number of CPUs/cores
		int coreLoad[16];	// percentage load on each individual core
	};

	// Get CPU performance metrics
	bool GetCPUMetrics(CPUMetrics &metrics);

protected:
	// Hi-res timer
	HiResTimer timer;

	// Master frame counter.  We increment this one counter on each
	// frame.  The various sub-counters note the current master counter
	// at the start of their cycles, then use the delta from the master
	// counter to the start count to determine the number of frames in
	// that cycle.  This lets us minimize the performance impact of the
	// counter itself by only incrementing one variable.
	int64_t nFrames;

	// counter structure
	struct Counter
	{
		Counter() { n0 = 0; t0 = 0; }
		int64_t n0;			// starting master frame count
		int64_t t0;			// start time
	};

	// Current frame count and start time.  This is for determining the
	// short term (essentially instantaneous) rate.  We reset this each
	// time we sample it.
	Counter cur;

	// Rolling average counters.  We keep a ring of these with staggered
	// start times.  At any given time, one of these is current.  When
	// the current counter reaches the maximum time window, we reset it
	// and move to the next in the ring, making it current.  This lets
	// us maintain an average over a fairly consistent trailing period.
	Counter rolling[8];
	int curRolling;

	// time in seconds for the rolling average
	float rolling_period_sec;

	// CPU performance query handle
	HQUERY hCpuQuery;

	// overall CPU performance counter
	HCOUNTER hCpuCounter;

	// per-core CPU performance counters
	int nCpuCores;
	HCOUNTER hCoreCounter[16];
};
