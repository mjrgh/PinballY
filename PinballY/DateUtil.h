// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Date utility functions

#pragma once

class DateTime
{
public:
	// create a new DateTime representing the current time
	DateTime();

	// create a new DateTime representing the given time, in YYYYMMDDHHMMSS format
	DateTime(const TCHAR *d);

	// create from a FILETIME
	DateTime(const FILETIME &ft) : ft(ft) { }

	// create from a SYSTEMTIME
	DateTime(const SYSTEMTIME &st) { SystemTimeToFileTime(&st, &ft); }

	// create from a Variant DATE value
	DateTime(DATE variantTime)
	{
		SYSTEMTIME st;
		VariantTimeToSystemTime(variantTime, &st);
		SystemTimeToFileTime(&st, &ft);
	}

	// is the date valid?
	bool IsValid() const { return ft.dwHighDateTime != 0 || ft.dwLowDateTime != 0; }

	// get the value in YYYYMMDDHHMMSS format
	TSTRING ToString() const;

	// get the value as a Variant DATE value
	DATE ToVariantDate() const
	{
		SYSTEMTIME st;
		DATE d;
		FileTimeToSystemTime(&ft, &st);
		SystemTimeToVariantTime(&st, &d);
		return d;
	}

	// Get the value in human-readable format, in local time.  The
	// flags are the values defined for Win32 GetTimeFormatEx():
	//
	//  TIME_NOMINUTESORSECONDS - omit minutes and seconds
	//  TIME_NOSECONDS          - omits seconds
	//  TIME_NOTIMEMARKER       - omit the time portion marker
	//  TIME_FORCE24HOURFORMAT  - use 24-hour format (overrides locale defaults)
	//
	TSTRING FormatLocalTime(DWORD flags = 0) const;

protected:
	// timestamp this date represents, as a FILETIME value
	FILETIME ft;
};

