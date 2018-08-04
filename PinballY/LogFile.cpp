// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//

#include "stdafx.h"
#include <io.h>
#include <fcntl.h>
#include "LogFile.h"
#include "DateUtil.h"

// statics
LogFile *LogFile::inst = nullptr;

LogFile::LogFile()
{
	// build the filename - <program folder>\PinballY.log
	TCHAR fname[MAX_PATH];
	GetExeFilePath(fname, countof(fname));
	PathAppend(fname, _T("PinballY.log"));

	// Open it with an inheritable handle.  This allows passing the handle
	// to a child process as stdout/stderr, to redirect the output from the
	// child process in the log file.
	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;
	h = CreateFile(fname, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, &sa,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	// write the starting time
	if (h != INVALID_HANDLE_VALUE)
	{
		DateTime d;
		Write(_T("PinballY session started %s\n\n"), d.FormatLocalDateTime().c_str());
	}
}

LogFile::~LogFile()
{
}

// initialize
void LogFile::Init()
{
	if (inst == nullptr)
		inst = new LogFile();
}

// terminate
void LogFile::Shutdown()
{
	if (inst != nullptr)
	{
		delete inst;
		inst = nullptr;
	}
}

void LogFile::Write(const TCHAR *fmt, ...)
{
	if (h != NULL && h != INVALID_HANDLE_VALUE)
	{
		// set up the varargs list
		va_list ap;
		va_start(ap, fmt);

		// format the message
		TSTRINGEx s;
		s.FormatV(fmt, ap);

		// done with the varargs
		va_end(ap);

		// convert the formatted text to single-byte characters, and convert
		// C-style \n newlines to DOS-style \r\n newlines
		static std::basic_regex<CHAR> nl("\n");
		CSTRING c = std::regex_replace(TSTRINGToCSTRING(s), nl, "\r\n");

		// write to the file in single-byte characters
		DWORD bytesWritten = 0;
		WriteFile(h, c.c_str(), (DWORD)c.length(), &bytesWritten, NULL);
	}
}
