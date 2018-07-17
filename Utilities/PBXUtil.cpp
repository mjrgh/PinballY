// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <Shlwapi.h>
#include "PBXUtil.h"
#include "StringUtil.h"
#include "WinUtil.h"

// PinballX install path.  Initialized on the first call
// to GetPinballXPath().
static bool pinballXPathSet = false;
static TSTRING pinballXPath;

// Get the PinballX path
const TCHAR *GetPinballXPath()
{
	// if we haven't resolved the path yet, do so now
	if (!pinballXPathSet)
	{
		// PinballX's main registry footprint seems to be its uninstall key,
		// under HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall.
		// Unfortunately, the specific subkey is variable, because it's the
		// usual MS Setup release GUID, meaning every update has a unique
		// random subkey name.  (Random in the sense that it's generated
		// randomly when the MSI for a given release is built; it's stable
		// and permanent for a given release.)  So we have to search all of 
		// the subkeys for one that has a pointer to PinballX.exe.  The 
		// pointer we're looking for is the DisplayIcon value, which should
		// contain the full path to the PinballX.exe application file.
		HKEYHolder hkey;
		std::basic_regex<TCHAR> exePat(_T(".*\\\\pinballx\\.exe$"), std::regex_constants::icase);
		if (RegOpenKey(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"), &hkey) == ERROR_SUCCESS)
		{
			// enumerate subkeys
			for (DWORD index = 0; ; ++index)
			{
				TCHAR name[256];
				DWORD namelen = countof(name);
				if (RegEnumKeyEx(hkey, index, name, &namelen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
				{
					// query the DisplayIcon value of this key
					HKEYHolder hSubkey;
					DWORD typ;
					TCHAR val[MAX_PATH];
					DWORD vallen = sizeof(val);
					if (RegOpenKey(hkey, name, &hSubkey) == ERROR_SUCCESS
						&& RegQueryValueEx(hSubkey, _T("DisplayIcon"), NULL, &typ, (BYTE*)val, &vallen) == ERROR_SUCCESS
						&& std::regex_match(val, exePat))
					{
						// DisplayIcon should be set to the full path and filename
						// of the PinballX executable.  Remove the filename part to
						// get the path - that should be the PBX install folder.
						PathRemoveFileSpec(val);
						pinballXPath = val;

						// stop searching
						break;
					}
				}
			}
		}

		// we've now done the search
		pinballXPathSet = true;
	}

	// Return the stored path, or null if the path is empty
	return pinballXPath.length() != 0 ? pinballXPath.c_str() : nullptr;
}

