// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <string.h>
#include <vector>
#include <stdio.h>
#include "Util.h"
#include "StringUtil.h"
#include "LogError.h"
#include "InstanceHandle.h"


// -----------------------------------------------------------------------
//
// sprintf variants
//


// sprintf with automatic allocation.  The caller is responsible for freeing 
// the memory with free[].  Returns the number of characters in the result
// string, excluding the trailing null.  Returns -1 on failure.
int asprintf(TCHAR **result, const TCHAR *fmt, ...)
{
	// set up the varargs list
	va_list ap;
	va_start(ap, fmt);

	// do the formatting and allocation
	int r = vasprintf(result, fmt, ap);

	// close out the varargs list
	va_end(ap);

	// return the 
	return r;
}

// vsprintf with automatic allocation.  The caller is responsible for freeing 
// the memory with free[].  Returns the number of characters in the result
// string, excluding the trailing null.  Returns -1 on failure.
int vasprintf(TCHAR **result, const TCHAR *fmt, va_list ap)
{
	// presume failure
	int r = -1;
	*result = 0;

	// figure the required buffer size; proceed if in a valid range
	int size = _vsctprintf(fmt, ap);
	if (size >= 0 && size < INT_MAX)
	{
		// allocate space for the formatted text plus trailing null
		if ((*result = new (std::nothrow) TCHAR[size + 1]) != 0)
		{
			// format the text
			r = _vsntprintf_s(*result, size + 1, _TRUNCATE, fmt, ap);
			if (r < 0 || r > size)
			{
				// failed - delete the buffer and set the error result
				delete[] *result;
				*result = 0;
				r = -1;
			}
		}
	}

	// return the result length
	return r;
}

// -----------------------------------------------------------------------
//
// Wide/Ansi and Ansi/Wide conversions
//

CSTRING WideToAnsi(const WCHAR *wstr, UINT codePage)
{
	// figure out how much space we need
	int len = WideCharToMultiByte(codePage, 0, wstr, -1, 0, 0, 0, 0);

	// set up a buffer and reserve space
	CSTRING astr;
	astr.reserve(len);
	astr.resize(len - 1);

	// do the conversion for reals this time
	WideCharToMultiByte(codePage, 0, wstr, -1, &astr[0], len, 0, 0);

	// return the string
	return astr;
}

WSTRING AnsiToWide(const CHAR *astr, UINT codePage)
{
	// figure out how much space we need
	int len = MultiByteToWideChar(codePage, 0, astr, -1, 0, 0);

	// set up a buffer and reserve space
	WSTRING wstr;
	wstr.reserve(len);
	wstr.resize(len - 1);

	// do the conversion for reals this time
	MultiByteToWideChar(codePage, 0, astr, -1, &wstr[0], len + 1);

	// return the string
	return wstr;
}


// -----------------------------------------------------------------------
// 
// String prefix/suffix comparisons
//

bool tstrStartsWith(const TCHAR *str, const TCHAR *substr)
{
	size_t sublen = _tcslen(substr);
	return _tcslen(str) >= sublen && _tcsncmp(str, substr, sublen) == 0;
}

bool tstriStartsWith(const TCHAR *str, const TCHAR *substr)
{
	size_t sublen = _tcslen(substr);
	return _tcslen(str) >= sublen && _tcsnicmp(str, substr, sublen) == 0;
}

bool tstrEndsWith(const TCHAR *str, const TCHAR *substr)
{
	size_t len = _tcslen(str);
	size_t sublen = _tcslen(substr);
	return len >= sublen && _tcscmp(str + (len - sublen), substr) == 0;
}

bool tstriEndsWith(const TCHAR *str, const TCHAR *substr)
{
	size_t len = _tcslen(str);
	size_t sublen = _tcslen(substr);
	return len >= sublen && _tcsicmp(str + (len - sublen), substr) == 0;
}

// -----------------------------------------------------------------------
//
// Type-overloaded covers for the system LoadString
//
int LoadStringT(HINSTANCE hInstance, int resourceId, LPSTR buffer, int bufferMax)
{
	return LoadStringA(hInstance, resourceId, buffer, bufferMax); 
}

int LoadStringT(HINSTANCE hInstance, int resourceId, LPWSTR buffer, int bufferMax)
{
	return LoadStringW(hInstance, resourceId, buffer, bufferMax); 
}


// Load a string from a resource, returning a pointer directly
// to the system resource data.
TSTRINGEx LoadStringT(int resourceId)
{
	const TCHAR *ptr = 0;
	int len = LoadStringT(G_hInstance, resourceId, (TCHAR *)&ptr, 0);
	if (len >= 0)
		return TSTRINGEx(ptr, len);
	else
		return MsgFmt(_T("[Resource String %d]"), resourceId).Get();
}


// -----------------------------------------------------------------------
//
// Guid parsing and formatting
//

bool ParseGuid(const TCHAR *guidString, GUID &guid)
{
	// skip leading space
	for (; _istspace(*guidString); ++guidString);

	// skip opening "{"
	if (*guidString == '{')
		++guidString;

	// parse it
	unsigned int d2, d3, a[8];
	if (_stscanf_s(guidString, _T("%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x"),
		&guid.Data1, &d2, &d3, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7]) != 11)
		return false;

	// pack the guid elements
	guid.Data2 = d2;
	guid.Data3 = d3;
	for (int i = 0; i < 8; ++i)
		guid.Data4[i] = (unsigned char)a[i];

	// success
	return true;
}

TSTRING FormatGuid(const GUID &guid)
{
	// format it
	TCHAR buf[40];
	_stprintf_s(buf, _T("%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x"),
		guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

	// return it as a string
	return TSTRING(buf);
}

// -----------------------------------------------------------------------
//
// URL parameter escaping
//
TSTRING UrlParamEncode(const TSTRING &str)
{
	TSTRING result;

	// add a single %xx encoded byte to the result
	auto AddPct = [&result](BYTE c)
	{
		static const TCHAR hex[] = _T("0123456789ABCDEF");
		result.push_back('%');
		result.push_back(hex[(c >> 4) & 0x0F]);
		result.push_back(hex[c & 0x0F]);
	};

	// scan the string
	for (auto c : str)
	{
		if (c > 127)
		{
			// non-ASCII - convert to UTF8 and encode as a series of %xx bytes
			CHAR buf[32];
			int len = WideCharToMultiByte(CP_UTF8, 0, &c, 1, buf, countof(buf), NULL, FALSE);
			for (int i = 0; i < len; ++i)
				AddPct((BYTE)buf[i]);
		}
		else if (_istalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
		{
			// unreserved character - encode as-is
			result.push_back(c);
		}
		else if (c == ' ')
		{
			// space - encode as '+'
			result.push_back('+');
		}
		else
		{
			// Add everything else as a %xx sequence.  Note that we already
			// know for certain that it's within the 0..127 range, so we can
			// safely convert it to BYTE without loss of precision.
			AddPct((BYTE)c);
		}
	}

	// return the result
	return result;
}

// -----------------------------------------------------------------------
//
// HTML escaping
//

WSTRING HtmlEscape(const WSTRING &str)
{
	std::basic_regex<TCHAR> pat(_T("[<>&]"));
	return regex_replace(str, pat, [](const std::match_results<WSTRING::const_iterator> &m)
	{
		if (m[0].first[0] == '<')
			return _T("&lt;");
		else if (m[0].first[0] == '>')
			return _T("&gt;");
		else if (m[0].first[0] == '&')
			return _T("&amp;");
		else
			return m[0].str().c_str();
	});
}

CSTRING HtmlEscape(const CSTRING &str)
{
	std::basic_regex<CHAR> pat("[<>&]");
	return regex_replace(str, pat, [](const std::match_results<CSTRING::const_iterator> &m)
	{
		if (m[0].first[0] == '<')
			return "&lt;";
		else if (m[0].first[0] == '>')
			return "&gt;";
		else if (m[0].first[0] == '&')
			return "&amp;";
		else
			return m[0].str().c_str();
	});
}

// -----------------------------------------------------------------------
//
// MsgFmt - formatted message string
// 

MsgFmt::MsgFmt(const TCHAR *fmt, ...)
{
	// format the string
	va_list ap;
	va_start(ap, fmt);
	InitV(fmt, ap);
	va_end(ap);
}

MsgFmt::MsgFmt(int resourceStringId, ...)
{
	// load the resource string
	TSTRING fmt = LoadStringT(resourceStringId);

	// format it sprintf-style
	va_list ap;
	va_start(ap, resourceStringId);
	InitV(fmt.c_str(), ap);
	va_end(ap);
}

void MsgFmt::Init(const TCHAR *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	InitV(fmt, ap);
	va_end(ap);
}

void MsgFmt::InitV(const TCHAR *fmt, va_list ap)
{
	// no buffer yet
	msg = 0;

	// try formatting the message
	if (vasprintf(&msg, fmt, ap) < 0)
	{
		// failed - use the original string as our message
		size_t alo = _tcslen(fmt) + 1;
		if ((msg = new (std::nothrow) TCHAR[alo]) != 0)
			_tcscpy_s(msg, alo, fmt);
	}
}

// -----------------------------------------------------------------------
//
// Format a fractional number
//
TSTRING FormatFraction(float value)
{
	// separate it into whole and fractional parts
	float whole;
	float frac = fabsf(modff(value, &whole));

	// if the fractional part is one of the common fractions, 
	// format it with the appropriate Unicode fraction character
	static const struct
	{
		float val;
		TCHAR ch;
	} fractionChars[] = {
		{ 0.25f, 0xBC },
		{ 0.5f,  0xBD },
		{ .75f,  0xBE }
	};
	for (auto &f : fractionChars)
	{
		if (f.val == frac)
			return whole == 0.0f ? MsgFmt(_T("%c"), f.ch).Get() : MsgFmt(_T("%d%c"), (int)whole, f.ch).Get();
	}

	// No luck with the fraction characters.  Format it as a 
	// regular floating point value.
	TCHAR buf[32];
	_stprintf_s(buf, _T("%f"), value);

	// if that used an 'E' format, return it as-is
	TCHAR *p = buf;
	for (; *p != 0; ++p)
	{
		if (*p == 'e' || *p == 'E')
			return buf;
	}

	// no 'E' - work backwards from the end to find the last
	// significant digit (not a trailing zero)
	for (; p != buf && *(p - 1) == '0'; --p);

	// if that leaves us with a trailing period, drop it as well
	if (p != buf && *(p - 1) == '.')
		--p;

	// null-terminate here
	*p = 0;

	// return the result
	return buf;
}

