// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// FileVersionInfo - retrieve version data from an embedded VSVERSIONINFO
// resource in an EXE or DLL file.
//
#pragma once

#include "StringUtil.h"

class FileVersionInfo
{
public:
	FileVersionInfo(const TCHAR *exename);

	// Did we successfully read the version data?
	bool valid;

	// version number, in 64-bit format, for easy comparison
	ULONGLONG llVersion;

	// version number in 4-part notation, major.minor.patch.build
	WORD version[4];

	// as a string, in dotted notation
	TSTRINGEx versionStr;

	// version number, in original 32:32 format
	DWORD versionHi;
	DWORD versionLo;

	// strings from the file version data
	TSTRING productName;
	TSTRING comments;
	TSTRING legalCopyright;

protected:
};
