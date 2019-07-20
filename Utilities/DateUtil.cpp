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

void DateTime::ToStructTm(tm& tm)
{
	// convert the internal FILETIME to a time_t
	ULARGE_INTEGER ull;
	ull.LowPart = ft.dwLowDateTime;
	ull.HighPart = ft.dwHighDateTime;
	time_t t = ull.QuadPart / 10000000ULL - 11644473600ULL;

	// convert the time_t to struct tm
	localtime_s(&tm, &t);
}

TSTRING DateTime::FormatLocalDateTime(DWORD dateFlags, DWORD timeFlags) const
{
	// Convert the internal UTC timestamp to a SYSTEMTIME struct
	// adjusted to the local time zone.  
	SYSTEMTIME utc, lcl;
	FileTimeToSystemTime(&ft, &utc);
	SystemTimeToTzSpecificLocalTime(NULL, &utc, &lcl);

	// format it
	TCHAR date[255], time[255];
	GetDateFormat(LOCALE_USER_DEFAULT, dateFlags, &lcl, NULL, date, countof(date));
	GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, timeFlags, &lcl, NULL, time, countof(time));
	return MsgFmt(_T("%s, %s"), date, time).Get();
}

TSTRING DateTime::FormatLocalDate(DWORD flags) const
{
	// Convert the internal UTC timestamp to a SYSTEMTIME struct
	// adjusted to the local time zone.  
	SYSTEMTIME utc, lcl;
	FileTimeToSystemTime(&ft, &utc);
	SystemTimeToTzSpecificLocalTime(NULL, &utc, &lcl);

	// format it
	TCHAR date[255];
	GetDateFormat(LOCALE_USER_DEFAULT, flags, &lcl, NULL, date, countof(date));
	return date;
}

bool DateTime::Parse(const TCHAR *str)
{
	// store a local time zone SYSTEMTIME value 
	auto Store = [this](const SYSTEMTIME &localDate)
	{
		// convert to UTC
		SYSTEMTIME utcDate;
		TzSpecificLocalTimeToSystemTime(NULL, &localDate, &utcDate);

		// convert to our internal FILETIME representation
		SystemTimeToFileTime(&utcDate, &ft);
	};

	// check for computer-style formats: YYYYMMDD-HHMMSS and the like
	typedef std::basic_regex<TCHAR> R;
	std::match_results<const TCHAR *> m;
	if (std::regex_match(str, m, R(_T("\\s*(\\d\\d\\d\\d)-?(\\d\\d)-?(\\d\\d)([:\\-]?(\\d\\d):?(\\d\\d)(:?\\d\\d)?)?\\s*"))))
	{
		// extract the fields
		SYSTEMTIME d;
		ZeroMemory(&d, sizeof(d));
		d.wYear = _ttoi(m[1].str().c_str());
		d.wMonth = _ttoi(m[2].str().c_str());
		d.wDay = _ttoi(m[3].str().c_str());
		if (m[4].matched)
		{
			d.wHour = _ttoi(m[5].str().c_str());
			d.wMinute = _ttoi(m[6].str().c_str());
			if (m[7].matched)
				d.wSecond = _ttoi(m[7].str().c_str());
		}

		// store it and return success
		Store(d);
		return true;
	}

	// parse a date
	auto ParseDate = [](const TCHAR *str, SYSTEMTIME &d, TSTRING &leftover)
	{
		static const int daysInMonth[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		auto ValidateMMDD = [](int mm, int dd, int yy)
		{
			// validate the month
			if (mm < 1 || mm > 12)
				return false;

			// validate the day
			if (dd < 1 || dd > daysInMonth[mm])
			{
				// special case: if it's a leap year, allow feb 29
				auto IsLeapYear = [](int yy)
				{
					if (yy % 4 != 0)
						return false;
					if (yy % 100 != 0)
						return true;
					if (yy % 400 != 0)
						return false;
					else
						return true;
				};
				if (!(mm == 2 && dd == 29 && IsLeapYear(yy)))
					return false;
			}

			// all is well
			return true;
		};

		// try various date formats
		std::match_results<const TCHAR *> m;
		if (std::regex_match(str, m, R(_T("\\s*(\\d\\d\\d\\d)[\\-/.,](\\d\\d?)[\\-/.,](\\d\\d?)\\b(.*)"))))
		{
			// YYYY-MM-DD
			int yy = _ttoi(m[1].str().c_str());
			int mm = _ttoi(m[2].str().c_str());
			int dd = _ttoi(m[3].str().c_str());
			if (!ValidateMMDD(mm, dd, yy))
				return false;

			// populate the fields
			d.wYear = yy;
			d.wMonth = mm;
			d.wDay = dd;
			leftover = m[4].str();
			return true;
		}
		
		// MM, DD, YY/YYYY fields in any order, with typical separators.
		if (std::regex_match(str, m, R(_T("\\s*(\\d{1,4})([\\-/.,])(\\d{1,4})\\2(\\d{1,4})\\b(.*)"))))
		{
			do
			{
				int f[3];
				f[0] = _ttoi(m[1].str().c_str());
				f[1] = _ttoi(m[3].str().c_str());
				f[2] = _ttoi(m[4].str().c_str());

				// We can only have one field (the year) that's four digits long; 
				// the others have to be one or two digits.  If there's more than
				// one such field, abandon this interpretation.
				int nYearFields = 0;
				if (m[1].str().length() > 2) ++nYearFields;
				if (m[3].str().length() > 2) ++nYearFields;
				if (m[4].str().length() > 2) ++nYearFields;
				if (nYearFields > 1)
					break;

				// Get locale format picture for the short date, to determine
				// the default order of the year, month, and day.
				int idx = 0;
				int mi = -1, di = -1, yi = -1;
				TCHAR lc[100];
				GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SSHORTDATE, lc, countof(lc));
				for (const TCHAR *p = lc; *p && idx < 3; ++p)
				{
					// check for year, month, and day signifiers
					if (*p == 'y')
						yi = idx++;
					else if (*p == 'M')
						mi = idx++;
					else if (*p == 'd')
						di = idx++;

					// skip consecutive elements of the same type
					for (TCHAR c = *p++; *p == c; ++p);
				}

				// Make sure we got all three elements.  If not, use the default
				// US MM-DD-YYYY format
				if (yi < 0 || mi < 0 || di < 0)
					mi = 0, di = 1, yi = 2;

				// If any fields from the value is over 31, it can only be the year.
				for (int i = 0; i < 3; ++i)
				{
					if (f[i] > 31 && i != yi)
					{
						// This must be the year, but it's not in the position we'd
						// have assumed based on the locale.  Treat this one as the
						// year, and renumber the other two such that we preserve
						// their relative order.
						yi = i;
						int n1, n2;
						if (i == 0)
							n1 = 1, n2 = 2;
						else if (i == 1)
							n1 = 0, n2 = 2;
						else
							n1 = 0, n2 = 1;
						if (mi < di)
							mi = n1, di = n2;
						else
							di = n1, mi = n2;

						break;
					}
				}

				// pull out the tentative fields
				int mm = f[mi];
				int dd = f[di];
				int yy = f[yi];

				// If the month field is out of range (over 12), swap it
				// with the day field.
				if (mm > 12)
					mm = f[di], dd = f[mi];

				// if the year is two digits, infer the century from the current year
				if (yy < 100)
				{
					// If we're near a turn-of-century, the date could be in the
					// next or previous century.  E.g., if it's 2018, and the year
					// is given as 86, it's probably 1986 rather than 2086.  And
					// if it's currently 2095 and the year is given as 03, it's 
					// probably 2103. Pick the possibility that's closest to the 
					// current date.
					SYSTEMTIME now;
					GetLocalTime(&now);
					int century = (now.wYear / 100) * 100;
					int y1 = century - 100 + yy, y2 = century + yy, y3 = century + 100 + yy;
					int d1 = abs(y1 - now.wYear), d2 = abs(y2 - now.wYear), d3 = abs(y3 - now.wYear);
					if (d1 < d2 && d1 < d3)
						yy = y1;
					else if (d2 < d1 && d2 < d3)
						yy = y2;
					else
						yy = y3;
				}

				// validate the result - if it fails, abandon this interpretation
				if (!ValidateMMDD(mm, dd, yy))
					break;

				// success!
				d.wYear = yy;
				d.wMonth = mm;
				d.wDay = dd;
				leftover = m[5].str();
				return true;
			}
			while (false);
		}

		// no match
		return false;
	};

	// parse a time
	auto ParseTime = [](const TCHAR *str, SYSTEMTIME &t, TSTRING &leftover)
	{
		typedef std::basic_regex<TCHAR> R;
		std::match_results<const TCHAR *> m;
		TSTRING timePart;
		if (std::regex_match(str, m, R(_T("\\s*(\\d\\d?)[:.](\\d\\d)([:.](\\d\\d))?(\\s*([aApP][mM]?))?\\b(.*)"))))
		{
			// get the hour, minute, and second
			int hh = _ttoi(m[1].str().c_str());
			int mm = _ttoi(m[2].str().c_str());
			int ss = m[3].matched ? _ttoi(m[4].str().c_str()) : 0;

			// if we have a PM suffix, add twelve hours
			if (m[5].matched && _totlower(m[6].str()[0]) == 'p')
				hh += 12;

			// validate the ranges
			if (hh > 23 || mm > 59 || ss > 59)
				return false;

			// populate the fields
			t.wHour = hh;
			t.wMinute = mm;
			t.wSecond = ss;
			t.wMilliseconds = 0;
			leftover = m[7].str();
			return true;
		}
			
		// no match
		return false;
	};

	// trim delimiters from a string
	auto TrimDelims = [](TSTRING &s)
	{
		s = std::regex_replace(s, R(_T("^[\\s.,;:@\\-]+")), _T(""));
	};

	// Try starting with a time value.
	TSTRING leftover;
	SYSTEMTIME d, t;
	ZeroMemory(&d, sizeof(d));
	ZeroMemory(&t, sizeof(t));
	if (ParseTime(str, t, leftover))
	{
		// Got a time - try parsing a date portion
		TrimDelims(leftover);
		if (ParseDate(leftover.c_str(), d, leftover))
		{
			// copy the date portion into the time object
			t.wYear = d.wYear;
			t.wMonth = d.wMonth;
			t.wDayOfWeek = d.wDayOfWeek;
			t.wDay = d.wDay;

			// store it
			Store(d);

			// success
			return true;
		}

		// We couldn't parse a date after the time, so flush this
		// interpretation.  Dates and times can look similar in some
		// formats, but we're really after a date value here, so 
		// don't allow a bare time value.
	}

	// try parsing it starting with a date value
	if (ParseDate(str, d, leftover))
	{
		// got a date - try the time portion
		TrimDelims(leftover);
		if (ParseTime(leftover.c_str(), t, leftover))
		{
			// got a time - copy the time portion into the date object
			d.wHour = t.wHour;
			d.wMinute = t.wMinute;
			d.wSecond = t.wSecond;
			d.wMilliseconds = t.wMilliseconds;
		}
		else
		{
			// there's no time portion - use midnight
			d.wHour = d.wMinute = d.wSecond = d.wMilliseconds = 0;
		}

		// store it
		Store(d);
		
		// success
		return true;
	}

	// no luck
	return false;
}
