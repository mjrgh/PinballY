// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// String Utilities

#pragma once
#include <Windows.h>
#include <string>
#include <tchar.h>
#include <varargs.h>
#include <vector>
#include <iterator>
#include <regex>
#include <functional>
#include "InstanceHandle.h"

// TSTRING - std::string version using current system-wide TCHAR type
typedef std::basic_string<TCHAR, std::char_traits<TCHAR>> TSTRING;

// CSTRING - std::string version using single-byte characters
typedef std::basic_string<CHAR, std::char_traits<CHAR>> CSTRING;

// WSTRING - std::string version using explicit wide characters
typedef std::basic_string<WCHAR, std::char_traits<WCHAR>> WSTRING;

// Convert a wide string to ANSI multibyte and vice versa
CSTRING WideToAnsiCnt(const WCHAR *wstr, int len, UINT codePage = CP_ACP);
CSTRING WideToAnsi(const WCHAR *wstr, UINT codePage = CP_ACP);
WSTRING AnsiToWideCnt(const CHAR *astr, int len, UINT codePage = CP_ACP);
WSTRING AnsiToWide(const CHAR *astr, UINT codePage = CP_ACP);

#define WSTRINGToCSTRING(/*const WSTRING& */ wstr) WideToAnsi((wstr).c_str())
#define CSTRINGToWSTRING(/*const CSTRING& */ cstr) AnsiToWide((cstr).c_str())
#ifdef UNICODE
#define WideToTSTRING(/*const WCHAR* */ wstr) TSTRING(wstr)
#define TCHARToWide(/*const TCHAR* */ tstr)   TSTRING(tstr)
#define AnsiToTSTRING(/*const CHAR* */ astr)  AnsiToWide(astr)
#define TCHARToAnsi(/*const TCHAR* */ tstr)   WideToAnsi(tstr)
#define TCHARToWCHAR(/*const TCHAR* */ tstr) (tstr)
#define WCHARToTCHAR(/*const WCHAR* */ wstr) (wstr)
#define TCHARToCCHAR(/*const TCHAR* */) tstr) (WideToAnsi(tstr).c_str())
#define CHARToTCHAR(/*const CHAR* */ cstr) (AnsiToWide(cstr).c_str())
#define TSTRINGToWSTRING(/*const TSTRING& */ tstr) (tstr)
#define TSTRINGToCSTRING(/*const TSTRING& */ tstr) WideToAnsi((tstr).c_str())
#define CSTRINGToTSTRING(/*const CSTRING& */ cstr) AnsiToWide((cstr).c_str())
#define WSTRINGToTSTRING(/*const WSTRING& */ wstr) (wstr)
#else
#define WideToTSTRING(/*const WCHAR* */ wstr) WideToAnsi(wstr)
#define TCHARToWide(/*const TCHAR* */ tstr)   AnsiToWide(tstr)
#define TSTRINGtoWSTRING(/*const TSTRING& */ tstr) AnsiToWide((tstr).c_str())
#define TSTRINGToCSTRING(/*const TSTRING& */ tstr) (tstr)
#define CSTRINGToTSTRING(/*const TSTRING& */ cstr) (cstr)
#define WSTRINGToTSTRING(/*const WSTRING& */ wstr) AnsiToWide((wstr).c_str())
#define AnsiToTSTRING(/*const CHAR* */ astr)  TSTRING(astr)
#define TCHARToAnsi(/*const TCHAR* */ tstr)   TSTRING(tstr)
#define WCHARToTCHAR(/*const WCHAR* */ wstr) (AnsiToWide(wstr).c_str())
#define CHARToTCHAR(/*const CHAR* */ cstr) (cstr)
#define TCHARToWCHAR(/*const TCHAR* */ tstr) (AnsiToWide(tstr).c_str())
#define TCHARToCCHAR(/*const TCHAR* */) tstr) (tstr)
#endif

// sprintf/vsprintf with automatic allocation.  The caller is responsible for 
// freeing the returned memory buffer with free[].  Returns the number of 
// characters in the result string, excluding the trailing null.  Returns 
// -1 on failure.
int asprintf(TCHAR **result, const TCHAR *fmt, ...);

// vsprintf with automatic allocation.  The caller is responsible for freeing 
// the memory with free[].  Returns the number of characters in the result
// string, excluding the trailing null.  Returns -1 on failure.
template<typename T>
int vasprintf(T **result, const T *fmt, va_list ap)
{
	// presume failure
	int r = -1;
	*result = 0;

	// figure the required buffer size; proceed if in a valid range
	int size;
	if constexpr (sizeof(T) == 1)
		size = _vscprintf(fmt, ap);
	else
		size = _vsctprintf(fmt, ap);

	if (size >= 0 && size < INT_MAX)
	{
		// allocate space for the formatted text plus trailing null
		if ((*result = new (std::nothrow) T[static_cast<size_t>(size) + 1]) != 0)
		{
			// format the text
			if constexpr (sizeof(T) == 1)
				r = _vsnprintf_s(*result, static_cast<size_t>(size) + 1, _TRUNCATE, fmt, ap);
			else
				r = _vsntprintf_s(*result, static_cast<size_t>(size) + 1, _TRUNCATE, fmt, ap);

			if (r < 0 || r > size)
			{
				// failed - delete the buffer and set the error result
				delete[] * result;
				*result = 0;
				r = -1;
			}
		}
	}

	// return the result length
	return r;
}



// Overloaded cover for the Ansi and Unicode string loader functions, so
// that our template loader function can access the right one by type 
// rather than by name.
int LoadStringT(HINSTANCE hInstance, int resourceId, LPSTR buffer, int bufferMax);
int LoadStringT(HINSTANCE hInstance, int resourceId, LPWSTR buffer, int bufferMax);

// Load a string from a resource.  On success, sets *str to the
// loaded string, and returns true.  Returns false on failure and
// sets the string to empty.
template<class S> bool LoadStringT(S *str, int resourceId)
{
	// Get a pointer directly to the resource data
	const typename S::value_type *p = nullptr;
	int len = LoadStringT(G_hInstance, resourceId, (typename S::value_type *)&p, 0);

	// assign it to the <S> string
	if (len >= 0 && p != 0)
	{
		str->assign(p, len);
		return true;
	}
	else
		return false;
}

// Split a string at a delimiter, returning a list.  S is one of
// the basic_string<chartype>-derived types - TSTRING, TSTRINGEx, etc.
template<class S>
std::list<S> StrSplit(const typename S::value_type *str, typename S::value_type delim)
{
	std::list<S> lst;
	for (const typename S::value_type *p = str; *p != 0; ++p)
	{
		if (*p == delim)
		{
			lst.emplace_back(str, p - str);
			str = p + 1;
		}
	}
	lst.emplace_back(str);
	return lst;
}

// Trim a string of leading and trailing spaces
template<class S> S TrimString(const typename S::value_type *str)
{
	// find the first non-space character
	auto start = str;
	while (*start == ' ' || *start == '\t')
		++start;

	// find the last non-space character
	auto p = start, end = start;
	for ( ; *p != 0 ; ++p)
	{
		if (*p != ' ' && *p != '\t')
			end = p;
	}

	// return a string from start to end inclusive
	return S(start, end - start + 1);
}

// Extended string class.  This can be used to extend the basic
// string, TSTRING, and WSTRING classes with some extra methods.
template<class S> 
class StringEx : public S
{
public:
	StringEx() : S() { }
	StringEx(const typename S::value_type *s) : S(s) { }
	StringEx(const S &s) : S(s) { }
	StringEx(const typename S::value_type *s, size_t len) : S(s, len) { }
	StringEx(class MsgFmt &m);

	// load from a resource
	void Load(int resourceId) { LoadStringT(this, resourceId); }

	bool StartsWith(const typename S::value_type *prefix)
	{
		S sprefix(prefix);
		size_t prefixLen = sprefix.length();
		return this->length() >= prefixLen
			&& memcmp(prefix, this->c_str(), prefixLen * sizeof(S::value_type)) == 0;
	}

	StringEx &Format(const typename S::value_type *format, ...)
	{
		// call the varargs version
		va_list ap;
		va_start(ap, format);
		StringEx &ret = FormatV(format, ap);
		va_end(ap);
		return ret;
	}

	StringEx &FormatV(const typename S::value_type *format, va_list ap)
	{
		// Try formatting the string.  If it succeeds, use the formatted
		// text, otherwise use the template string.
		typename S::value_type *buf = 0;	
		if (vasprintf<S::value_type>(&buf, format, ap) >= 0)
			this->assign(buf);
		else
			this->assign(format);

		// free the temp buffer
		delete[] buf;

		// return 'this'
		return *this;
	}

	std::list<StringEx<S>> Split(typename S::value_type delim) { return StrSplit<StringEx<S>>(this->c_str(), delim); }

	operator const typename S::value_type*() { return this->c_str(); }
};

typedef StringEx<CSTRING> CSTRINGEx;
typedef StringEx<TSTRING> TSTRINGEx;
typedef StringEx<WSTRING> WSTRINGEx;

// Load a string from a resource
TSTRINGEx LoadStringT(int resourceId);

// Test a string to see if it starts/ends with a given substring.  The
// "i" versions are case-insensitive.
bool tstrStartsWith(const TCHAR *str, const TCHAR *substr);
bool tstriStartsWith(const TCHAR *str, const TCHAR *substr);
bool tstrEndsWith(const TCHAR *str, const TCHAR *substr);
bool tstriEndsWith(const TCHAR *str, const TCHAR *substr);

// Formatted string object.  This is a convenient way to format a string
// for assignment to a string value or for a function argument.  Use 
// printf-style formatting in the constructor.  This yields an object
// with an internal string value that can be obtained with a cast to
// the string pointer type.
class MsgFmt
{
public:
	// format from a string constant format string
	MsgFmt(const TCHAR *fmt, ...);

	// format from a resource string
	MsgFmt(int resourceStringId, ...);

	~MsgFmt() { delete msg; }

	// get the message string
	const TCHAR *Get() const { return msg != 0 ? msg : _T("[Null]"); }
	operator const TCHAR *() const { return Get(); }

protected:
	// initialize
	void Init(const TCHAR *fmt, ...);
	void InitV(const TCHAR *fmt, va_list ap);

	// message string
	TCHAR *msg;
};

template<class S>
StringEx<S>::StringEx(MsgFmt &m) : S(m.Get()) { }

// Format a fraction, using Unicode fraction characters.  This
// formats a float with a fractional part using "vulgar fraction"
// characters for the common fractions (1/2, 1/4, 3/4).  For
// other fractions, we use a floating point format instead.
TSTRING FormatFraction(float value);


// -------------------------------------------------------------------------
//
// Safe, buffer-length-limited versions of some stdlib string.h functions
//

template<size_t size> TCHAR* _tcschr_s(TCHAR (&buf)[size], int c)
{
	// scan for the target character, stopping at the end of the buffer
	// or the first nul character
	for (TCHAR *p = buf, *endp = buf + size; p < endp && *p != 0; ++p)
	{
		// check for a match to the target character
		if (*p == c)
			return p;
	}

	// not found - return null pointer
	return nullptr;
}

// -------------------------------------------------------------------------
//
// BSTR holder.  This is a simple cover class that automatically manages
// the SysXxxString storage for the object's lifetime.
//

class BString
{
public:
	BString() { bstr = nullptr; }
	BString(const CHAR *src) { _Set(src); }
	BString(const WCHAR *src) { _Set(src); }

	~BString() { SysFreeString(bstr); }

	operator BSTR() { return bstr; }
	BSTR* operator&() { return &bstr; }

	BString& operator =(const CHAR *src) { Set(src); return *this; }
	BString& operator =(const WCHAR *src) { Set(src); return *this; }

protected:
	void Clear()
	{
		if (bstr != NULL)
		{
			SysFreeString(bstr);
			bstr = NULL;
		}
	}

	void Set(const CHAR *src) { Clear(); _Set(src); }
	void Set(const WCHAR *src) { Clear(); _Set(src); }

	void _Set(const WCHAR *src) { bstr = SysAllocString(src); }
	void _Set(const CHAR *src) { bstr = SysAllocString(AnsiToWide(src).c_str()); }
		
	// the underlying BSTR
	BSTR bstr;
};

// array of BSTR
class BStringArray
{
public:
	BStringArray(UINT n) : n(n), bstrs(new BSTR[n]) { ZeroMemory(bstrs, n * sizeof(BSTR)); }
	~BStringArray()
	{
		for (UINT i = 0; i < n; ++i)
		{
			if (bstrs[i] != nullptr)
			{
				SysFreeString(bstrs[i]);
				bstrs[i] = nullptr;
			}
		}
		delete[] bstrs;
	}

	UINT n;
	BSTR* operator&() { return bstrs; }

	class Cover
	{
	public:
		Cover(BSTR &bstr) : bstr(bstr) { }
		operator BSTR() { return bstr; }
		BSTR operator =(const WCHAR *src) 
		{
			if (bstr != nullptr)
				SysFreeString(bstr);
			bstr = SysAllocString(src);
		}

		BSTR &bstr;
	};
	Cover operator[](UINT i) { return Cover(bstrs[i]); }

	BSTR *bstrs;
};


// -------------------------------------------------------------------------
// Parse a UUID/GUID string.  We accept the following formats:
//
// 123e4567-e89b-12d3-a456-426655440000
// {123e4567-e89b-12d3-a456-426655440000}
// 
// Returns true on success, false if the input string wasn't
// in a valid format.
bool ParseGuid(const TCHAR *guidString, size_t guidStringLen, GUID &guid);
inline bool ParseGuid(const TCHAR *guidString, GUID &guid) { return ParseGuid(guidString, _tcslen(guidString), guid); }

// Format a GUID.  Generates the GUID in the standard hex
// format.  Just the bare GUID string is returned, without
// braces or other surrounding marks.
TSTRING FormatGuid(const GUID &guid);

// -------------------------------------------------------------------------
// 
// HTML escapes.  Converts markup characters & < > to &xxx equivalents.
//
WSTRING HtmlEscape(const WSTRING &str);
CSTRING HtmlEscape(const CSTRING &str);

// Javascript escapes.  Converts \, ", ', \n, \r, \b to \\ sequences.
TSTRING JavascriptEscape(const TSTRING &str);

// URL parameter encoding.  Converts special characters to %xx sequences,
// and replaces spaces with '+'s.
TSTRING UrlParamEncode(const TSTRING &str);


// -------------------------------------------------------------------------
//
// Regex replace with callback
//
// The callback function has this form:
//
//  [captures](const std::match_results<StringType::const_iterator> &m) -> StringType
//
// where StringType is the suitable TSTRING, CSTRING, or WSTRING type
// matching the source string type.
//

template<typename BidirIt, typename Traits, typename CharT, typename UnaryFunction>
std::basic_string<CharT> regex_replace(BidirIt first, BidirIt last,
	const std::basic_regex<CharT, Traits>& re, UnaryFunction f)
{
	std::basic_string<CharT> s;

	typename std::match_results<BidirIt>::difference_type
		positionOfLastMatch = 0;
	auto endOfLastMatch = first;

	auto callback = [&](const std::match_results<BidirIt>& match)
	{
		auto positionOfThisMatch = match.position(0);
		auto diff = positionOfThisMatch - positionOfLastMatch;

		auto startOfThisMatch = endOfLastMatch;
		std::advance(startOfThisMatch, diff);

		s.append(endOfLastMatch, startOfThisMatch);
		s.append(f(match));

		auto lengthOfMatch = match.length(0);

		positionOfLastMatch = positionOfThisMatch + lengthOfMatch;

		endOfLastMatch = startOfThisMatch;
		std::advance(endOfLastMatch, lengthOfMatch);
	};

	std::regex_iterator<typename std::basic_string<CharT>::const_iterator> begin(first, last, re), end;
	std::for_each(begin, end, callback);

	s.append(endOfLastMatch, last);

	return s;
}

template<class Traits, class CharT, class UnaryFunction>
std::basic_string<CharT> regex_replace(
	const std::basic_string<CharT>& s,
	const std::basic_regex<CharT, Traits>& re, 
	UnaryFunction f)
{
	return regex_replace(s.cbegin(), s.cend(), re, f);
}

template<class Traits, class CharT, class UnaryFunction>
std::basic_string<CharT> regex_replace(
	const CharT *s,
	const std::basic_regex<CharT, Traits>& re,
	UnaryFunction f)
{
	return regex_replace(std::basic_string<CharT>(s), re, f);
}
