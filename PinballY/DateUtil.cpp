// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "DateUtil.h"

// create a new DateTime representing the current time
DateTime::DateTime()
{
	SYSTEMTIME st;
	GetSystemTime(&st);
	SystemTimeToFileTime(&st, &ft);
}

// create a new DateTime representing a time in YYYYMMDDHHMMSS format
DateTime::DateTime(const TCHAR *str)
{
	SYSTEMTIME st;
	ZeroMemory(&ft, sizeof(ft));
	ZeroMemory(&st, sizeof(st));
	if (str != nullptr && _stscanf_s(str, _T("%04hd%02hd%02hd%02hd%02hd%02hd"),
		&st.wYear, &st.wMonth, &st.wDay, &st.wHour, &st.wMinute, &st.wSecond) == 6)
		SystemTimeToFileTime(&st, &ft);
}

TSTRING DateTime::ToString() const
{
	SYSTEMTIME st;
	FileTimeToSystemTime(&ft, &st);

	TCHAR buf[128];
	_stprintf_s(buf, _T("%04d%02d%02d%02d%02d%02d"),
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

	return buf;
}

TSTRING DateTime::FormatLocalTime(DWORD flags) const
{
	// adjust to the local time zone
	FILETIME lft;
	FileTimeToLocalFileTime(&ft, &lft);

	// get it as a SYSTEMTIME
	SYSTEMTIME st;
	FileTimeToSystemTime(&lft, &st);

	// format it
	TCHAR date[255], time[255];
	GetDateFormat(LOCALE_USER_DEFAULT, DATE_LONGDATE, &st, NULL, date, countof(date));
	GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, flags, &st, NULL, time, countof(time));
	return MsgFmt(_T("%s, %s"), date, time).Get();
}
