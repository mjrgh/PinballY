// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once
#include "stdafx.h"
#include <timeapi.h>

class HiResTimer
{
public:
	HiResTimer();
	~HiResTimer();

	// Get the current time in QPC ticks.  This gets the time at
	// the finest precision scale available on the hardware.
	inline int64_t GetTime_ticks()
	{
		if (qpcAvailable)
		{
			// use the high-res performance counter
			LARGE_INTEGER t;
			QueryPerformanceCounter(&t);
			return t.QuadPart;
		}
		else
		{
			// no QPC, so use the low-res system timer instead
			return timeGetTime();
		}
	}

	// Get the current time in seconds
	inline double GetTime_seconds() { return GetTime_ticks() * tickTime_sec; }

	// Get the current time in microseconds
	inline double GetTime_us() { return GetTime_ticks() * tickTime_us; }

	// get the tick time in seconds/microseconds
	inline double GetTickTime_sec() const { return tickTime_sec; }
	inline double GetTickTime_us() const { return tickTime_us; }

protected:
	// Peformance counter clock period in seconds.  Multiply an
	// interval read from the performance counter by this factor to
	// convert from ticks to seconds.
	double tickTime_sec;

	// Performance counter clock period in microseconds.  Multiply
	// an interval by this factor to convert to microseconds.
	double tickTime_us;

	// is the QPC timer available?
	bool qpcAvailable;
};
