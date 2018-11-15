// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// FileVersionInfo - retrieve version data from an embedded VSVERSIONINFO
// resource in an EXE or DLL file.
//

#include "stdafx.h"
#include "FileVersionInfo.h"

FileVersionInfo::FileVersionInfo(const TCHAR *filename) :
	valid(false),
	llVersion(0ULL),
	versionHi(0),
	versionLo(0)
{
	// clear the version number array
	ZeroMemory(version, sizeof(version));

	// "open" file version data
	DWORD vsInfoHandle;
	DWORD vsInfoSize = GetFileVersionInfoSize(filename, &vsInfoHandle);
	if (vsInfoSize != 0)
	{
		// load the version info
		std::unique_ptr<BYTE> vsInfo(new BYTE[vsInfoSize]);
		if (GetFileVersionInfo(filename, vsInfoHandle, vsInfoSize, vsInfo.get()))
		{
			// read the strings we're interested in
			LPCTSTR nameBuf = nullptr;
			UINT nameLen = 0;
			if (VerQueryValue(vsInfo.get(), _T("\\StringFileInfo\\040904e4\\ProductName"), (LPVOID*)&nameBuf, &nameLen)
				|| VerQueryValue(vsInfo.get(), _T("\\StringFileInfo\\040904b0\\ProductName"), (LPVOID*)&nameBuf, &nameLen)
				|| VerQueryValue(vsInfo.get(), _T("\\StringFileInfo\\000004b0\\ProductName"), (LPVOID*)&nameBuf, &nameLen))
				productName.assign(nameBuf, nameLen);
			if (VerQueryValue(vsInfo.get(), _T("\\StringFileInfo\\040904e4\\Comment"), (LPVOID*)&nameBuf, &nameLen)
				|| VerQueryValue(vsInfo.get(), _T("\\StringFileInfo\\040904b0\\Comments"), (LPVOID*)&nameBuf, &nameLen)
				|| VerQueryValue(vsInfo.get(), _T("\\StringFileInfo\\000004b0\\Comments"), (LPVOID*)&nameBuf, &nameLen))
				comments.assign(nameBuf, nameLen);
			if (VerQueryValue(vsInfo.get(), _T("\\StringFileInfo\\040904e4\\LegalCopyright"), (LPVOID*)&nameBuf, &nameLen)
				|| VerQueryValue(vsInfo.get(), _T("\\StringFileInfo\\040904b0\\LegalCopyright"), (LPVOID*)&nameBuf, &nameLen)
				|| VerQueryValue(vsInfo.get(), _T("\\StringFileInfo\\000004b0\\LegalCopyright"), (LPVOID*)&nameBuf, &nameLen))
				legalCopyright.assign(nameBuf, nameLen);

			// retrieve the fixed data portion
			VS_FIXEDFILEINFO *vsFixedInfo = nullptr;
			UINT vsFixedInfoLen;
			if (!VerQueryValue(vsInfo.get(), _T("\\"), (LPVOID*)&vsFixedInfo, &vsFixedInfoLen))
				vsFixedInfo = nullptr;

			// slice and dice the version number for easier consumption by the caller
			if (vsFixedInfo != nullptr)
			{
				// make a 64-bit combined long - this format is easy to compare by magnitude to
				// see if we're before/after a given target version
				llVersion = (((ULONGLONG)vsFixedInfo->dwProductVersionMS) << 32) | vsFixedInfo->dwProductVersionLS;

				// pull out the major.minor.patch.build parts
				auto vsnPart = [this](int bitOfs) { return (int)((llVersion >> bitOfs) & 0xFFFF); };
				version[0] = vsnPart(48);
				version[1] = vsnPart(32);
				version[2] = vsnPart(16);
				version[3] = vsnPart(0);

				// format it as a string
				versionStr.Format(_T("%d.%d.%d.%d"), version[0], version[1], version[2], version[3]);

				// got it
				valid = true;
			}
		}
	}
}
