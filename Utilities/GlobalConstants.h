// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Miscellaneous constants shared among sub-projects

#pragma once

namespace PinballY
{
	namespace Constants
	{
		// Maximum number of systems.  This is the number of SystemN 
		// entries we scan for in the configuration.  This limit is
		// arbitrary, and we can trivially raise it if necessary, but
		// we don't want to make it ridiculously large because of the
		// time cost of scanning through empty entries.
		const int MaxSystemNum = 100;
	}
}
