// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Date utility functions

#pragma once
#include <ctime>

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

	// Parse from flexible input formats.  This tries to infer
	// the format from the text.  On success, populates the DateTime
	// object with the new date and returns true; on failure, leaves
	// the DateTime value unchanged and returns false.
	bool Parse(const TCHAR *str);

	// is the date valid?
	bool IsValid() const { return ft.dwHighDateTime != 0 || ft.dwLowDateTime != 0; }

	// get the value in YYYYMMDDHHMMSS format
	TSTRING ToString() const;

	// Get the FILETIME value as an INT64.  This represents the number of
	// 100ns intervals since the FILETIME epoch, Jaunary 1, 1601 00:00:00 UTC.
	FILETIME GetFileTime() const { return ft; }

	// get the value as a Variant DATE value
	DATE ToVariantDate() const
	{
		SYSTEMTIME st;
		DATE d;
		FileTimeToSystemTime(&ft, &st);
		SystemTimeToVariantTime(&st, &d);
		return d;
	}

	// get the value as a C sys/time.h struct tm
	void ToStructTm(tm& tm);

	// Get the value in human-readable Date-and-time format, in the local
	// time zone, using the Windows localization.  
	//
	// The date flags are the values defined for Win32 GetDateFormatEx():
	//
	//  DATE_LONGDATE           - use the localized long date format
	//  DATE_SHORTDATE          - use the localized short date format
	//
	// The time flags are the values defined for Win32 GetTimeFormatEx():
	//
	//  TIME_NOMINUTESORSECONDS - omit minutes and seconds
	//  TIME_NOSECONDS          - omits seconds
	//  TIME_NOTIMEMARKER       - omit the time portion marker
	//  TIME_FORCE24HOURFORMAT  - use 24-hour format (overrides locale defaults)
	//
	TSTRING FormatLocalDateTime(DWORD dateFlags = DATE_LONGDATE, DWORD timeFlags = 0) const;

	// Get the value in human-readable Date format (the date only,
	// without the time of day), in the local time zone, using the
	// Windows localization.
	//
	// The flags are the values defined for Win32 GetDateFormatEx():
	//
	//  DATE_LONGDATE           - use the localized long date format
	//  DATE_SHORTDATE          - use the localized short date format
	//
	TSTRING FormatLocalDate(DWORD dateFlags = DATE_LONGDATE) const;

protected:
	// timestamp this date represents, as a FILETIME value
	FILETIME ft;
};

