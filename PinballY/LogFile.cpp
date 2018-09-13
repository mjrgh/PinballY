// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//

#include "stdafx.h"
#include <io.h>
#include <fcntl.h>
#include "LogFile.h"
#include "DateUtil.h"
#include "VersionInfo.h"

// statics
LogFile *LogFile::inst = nullptr;

LogFile::LogFile() :
	enabledFeatures(BaseLogging),
	tempFeatures(0)
{
	// build the filename - <program folder>\PinballY.log
	TCHAR fname[MAX_PATH];
	GetExeFilePath(fname, countof(fname));
	PathAppend(fname, _T("PinballY.log"));

	// open the file, overwriting any existing copy
	h = CreateFile(fname, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	// the start of the file counts as preceded by an infinite
	// amount of blank space, for group separator purposes
	nNewlines = 2;

	// write the starting time
	WriteTimestamp(_T("Session started\nPinballY %hs, build %d (%s, %hs)\n\n"), 
		G_VersionInfo.fullVerWithStat, G_VersionInfo.buildNo, 
		IF_32_64(_T("x86"), _T("x64")), G_VersionInfo.date);
}

LogFile::~LogFile()
{
	Group();
	WriteTimestamp(_T("PinballY session ending\n\n"));
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

void LogFile::InitConfig()
{
	// subscribe to config events
	ConfigManager::GetInstance()->Subscribe(this);

	// load the current configuration
	OnConfigReload();
}

void LogFile::OnConfigReload()
{
	// clear the feature enable mask, except for base logging (which
	// is always enabled)
	enabledFeatures = BaseLogging;

	// set the feature enable bits according to the config
	auto cfg = ConfigManager::GetInstance();
	static const struct
	{
		const TCHAR *cfgVar;
		DWORD flag;
		bool defval;
	} vars[] =
	{
		{ _T("Log.MediaFiles"),   MediaFileLogging, false },
		{ _T("Log.SystemSetup"),  SystemSetupLogging, false },
		{ _T("Log.MediaCapture"), CaptureLogging, true },
		{ _T("Log.TableLaunch"),  TableLaunchLogging, false },
		{ _T("Log.RealDMD"),      DmdLogging, true },
		{ _T("Log.DOF"),          DofLogging, true },
	};
	for (auto &v : vars)
	{
		if (cfg->GetBool(v.cfgVar, v.defval))
			enabledFeatures |= v.flag;
	}
}

void LogFile::Write(const TCHAR *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	WriteV(false, BaseLogging, fmt, ap);
	va_end(ap);
}

void LogFile::Write(DWORD feature, const TCHAR *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	WriteV(false, feature, fmt, ap);
	va_end(ap);
}

void LogFile::WriteTimestamp(const TCHAR *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	WriteV(true, BaseLogging, fmt, ap);
	va_end(ap);
}

void LogFile::WriteTimestamp(DWORD feature, const TCHAR *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	WriteV(true, feature, fmt, ap);
	va_end(ap);
}

void LogFile::WriteV(bool timestamp, DWORD features, const TCHAR *fmt, va_list ap)
{
	if (h != NULL && h != INVALID_HANDLE_VALUE 
		&& ((enabledFeatures | tempFeatures) & features) != 0)
	{
		// write the timestamp if desired
		if (timestamp)
		{
			DateTime d;
			WriteStr(d.FormatLocalDateTime().c_str());
			WriteStr(_T(": "));
		}

		// format the message and write it out
		TSTRINGEx s;
		s.FormatV(fmt, ap);
		WriteStr(s.c_str());
	}
}

void LogFile::WriteStr(const TCHAR *s)
{
	// convert to single-byte characters and write it out
	WriteStrA(TCHARToAnsi(s).c_str());
}

void LogFile::WriteStrA(const CHAR *s)
{
	// convert C-style newlines to DOS-style CR-LF sequences
	static std::basic_regex<CHAR> nl("\r\n|\n\r|\n");
	CSTRING c = std::regex_replace(s, nl, "\r\n");

	// hold the lock while writing
	CriticalSectionLocker locker(lock);

	// write the file
	DWORD bytesWritten = 0;
	WriteFile(h, c.c_str(), (DWORD)c.length(), &bytesWritten, NULL);

	// Count ending newlines.  Note that all newlines in the string
	// should be in DOS CR-LF format, so we shouldn't need to worry
	// about other formats at this point.
	for (const CHAR *p = c.c_str(); *p != 0; ++p)
	{
		char cur = *p;
		char nxt = *(p + 1);
		if ((cur == '\r' && nxt == '\n'))
		{
			++nNewlines;
			++p;
		}
		else
			nNewlines = 0;
	}
}

void LogFile::Group(DWORD feature)
{
	// proceed only if the feature bits are enabled
	if (h != NULL && h != INVALID_HANDLE_VALUE
		&& ((enabledFeatures | tempFeatures) & feature) != 0)
	{
		// if the last output didn't end with a newline, add two newlines
		// (one to end the previous line, and another to form a blank line);
		// if the last output ended with one newline, add one more for the
		// blank line; and if we have more than two newlines, we don't
		// need to add anything more.
		if (nNewlines == 0)
			WriteStrA("\n\n");
		else if (nNewlines == 1)
			WriteStrA("\n");
	}
}

void LogFile::EnableTempFeature(DWORD feature)
{
	CriticalSectionLocker locker(lock);
	tempFeatures |= feature;
}

void LogFile::WithdrawTempFeature(DWORD feature)
{
	CriticalSectionLocker locker(lock);
	tempFeatures &= ~feature;
}
