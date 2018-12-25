// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "VPinMAMEIfc.h"
#include "GameList.h"
#include "Application.h"
#include "HighScores.h"
#include "DOFClient.h"

// statics
const TCHAR *VPinMAMEIfc::configKey = _T("Software\\Freeware\\Visual PinMame");

// Enumerate installed VPM ROMs
void VPinMAMEIfc::EnumRoms(std::function<bool(const TCHAR*)> func)
{
	HKEYHolder hkeyVPM;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, configKey, 0, KEY_ENUMERATE_SUB_KEYS, &hkeyVPM) == ERROR_SUCCESS)
	{
		// Scan all subkeys.  The subkeys are all ROM names, except
		// for some special keys ("default", "globals").  ROM key 
		// names generally follow the pattern "game_ver", but the
		// "_ver" suffix isn't always present.  Note that it's 
		// possible to have ROM versions both with and without the 
		// suffix for the same game: e.g., we might find keys for 
		// both "xenon" and "xenon_l1".
		for (DWORD i = 0; ; ++i)
		{
			// get the next key; stop if there are no more keys
			TCHAR buf[128];
			if (RegEnumKey(hkeyVPM, i, buf, countof(buf)) != ERROR_SUCCESS)
				break;

			// check for the special names that aren't for ROMs
			if (_tcscmp(buf, _T("default")) == 0
				|| _tcscmp(buf, _T("global")) == 0)
				continue;
			
			// pass it to the callback; if it returns false, stop the
			// enumeration
			if (!func(buf))
				break;
		}
	}
}

// Check for a prefix match to a ROM name
bool PrefixMatch(const TCHAR *prefix, size_t prefixLen, const TCHAR *rom, size_t romLen)
{
	return romLen > prefixLen
		&& rom[prefixLen] == '_'
		&& _tcsnicmp(prefix, rom, prefixLen) == 0;
}

// Find the VPM ROM for a given game
bool VPinMAMEIfc::FindRom(TSTRING &romName, const GameListItem *game)
{
	// Try to determine the ROM name to use for the game:
	//
	// - If there's an explicit ROM setting in the game database
	//   entry, use that
	//
	// - Otherwise, try to get the NVRAM file for the game.  If
	//   there's an exact match in the VPinMAME ROM records, we'll
	//   use that.
	//
	// - Otherwise, match based on the DOF ROM name.  The DOF ROM
	//   is usually the version-independent base name (e.g., "fh"
	//   for Funhouse) whereas VPM uses the exact name stored in
	//   the game, which usually has a version suffix ("fh_l3").
	//   This makes the DOF entries the least exact.  In most
	//   cases that's beside the point because most users will
	//   only have ever installed a single ROM version, hence the
	//   inexact match will still give us a unique result when we
	//   actually compare it against what's in the registry.  But
	//   in the rare cases where the user has run more than one
	//   version of a ROM, it's better to use one of the other
	//   methods first so that we have a chance of matching the
	//   right one of the several possible versions.
	//
	TSTRING targetName, nvramPath, nvramName;
	const TCHAR *dofRom;
	if (game->rom.length() != 0)
	{
		// We have an exact name from the game.  Use it instead
		// of any of our heuristics.  This lets the user easily
		// override our guesswork whenever the guesswork gets it
		// wrong.
		targetName = game->rom;
	}
	else if (Application::Get()->highScores->GetNvramFile(nvramPath, nvramName, game))
	{
		// We got an NVRAM file.  For a VPM game, the NVRAM file has
		// the same name as the ROM, except that the NVRAM file adds
		// a ".nv" extension.  Remove the extension and use the result
		// as the ROM name.
		const TCHAR *p = nvramName.c_str();
		const TCHAR *dot = _tcsrchr(p, '.');
		if (dot != nullptr)
			targetName.assign(p, dot - p);
		else
			targetName.assign(p);
	}
	else if (DOFClient::Get() != nullptr && (dofRom = DOFClient::Get()->GetRomForTable(game)) != nullptr)
	{
		// We found a name from the DOF config.  This is usually just
		// the game name prefix, without the version suffix, so it
		// probably won't exactly match the VPM key stored in the
		// registry.  Our search allows for this by treating "x" as
		// a match to "x" OR "x_*".  So as long as the actual entry
		// in the registry uses this prefix, we'll find it; and as
		// long as the user has only installed/played one version 
		// of this game's ROM, it'll give us the correct match.
		//
		// The only snag is that we might match the wrong version
		// if the user has two or more versions of the ROM 
		// installed, since our wildcard rule will match all of
		// them equally well and will just pick one arbitrarily.
		// That's why we use the DOF rule as the last resort: it's
		// the least selective and most likely to pick the wrong
		// version in cases where there are multiple versions.
		// Fortunately, the harm is minimal; we just get the VPM
		// DMD settings for the "other" version.  And it can be
		// easily fixed by manual override, by filling in the 
		// actual ROM name for this game in the game database.
		targetName = dofRom;
	}
	else
	{
		// We came up empty on the ROM name, so we can't provide
		// a VPinMAME key.
		return false;
	}

	// Scan for matching ROMs
	bool found = false;
	EnumRoms([&found, &targetName, &romName](const TCHAR *cur)
	{
		// check for an exact match
		if (_tcsicmp(cur, targetName.c_str()) == 0)
		{
			// got it - we can stop searching, since we're not going
			// to find anything stronger than an exact match
			romName = cur;
			found = true;
			return false;
		}

		// check for a prefix match
		if (PrefixMatch(targetName.c_str(), targetName.length(), cur, _tcslen(cur)))
		{
			// It's a partial match up to the version suffix.  Stash
			// it as the best match so far, but keep searching, since
			// there might still be an exact match yet to be found,
			// which would override this partial match.
			romName = cur;
			found = true;
		}

		// continue the enumeration
		return true;
	});

	// indicate whether or not we found anything
	return found;
}

void VPinMAMEIfc::GetInstalledRomVersions(
	std::list<TSTRING> &installedRoms,
	const TCHAR *searchName)
{
	// note if searchName contains a suffix
	size_t searchNameLen = _tcslen(searchName);
	const TCHAR *und = _tcschr(searchName, '_');
	size_t prefixLen = und != nullptr ? und - searchName : 0;

	// Enumerate the ROMs
	EnumRoms([searchName, searchNameLen, prefixLen, &installedRoms](const TCHAR *cur)
	{
		size_t curLen = _tcslen(cur);

		// check for matches
		if (_tcsicmp(searchName, cur) == 0
			|| PrefixMatch(searchName, searchNameLen, cur, curLen)
			|| (prefixLen != 0 && curLen == prefixLen && _tcsnicmp(searchName, cur, curLen) == 0)
			|| (prefixLen != 0 && PrefixMatch(searchName, prefixLen, cur, curLen)))
			installedRoms.push_back(cur);

		// continue the enumeration
		return true;
	});
}

bool VPinMAMEIfc::GetRomDir(TSTRING &dir)
{
	// Look up the global VPinMAME NVRAM path in the registry.  This
	// is the path that usually applies to all Visual Pinball ROM-based
	// games, regardless of which VP version they're using, since VPM's
	// design as a COM object forces all VP versions to share a common
	// VPM installation.
	const TCHAR *keyPath = _T("Software\\Freeware\\Visual PinMame\\globals");
	HKEYHolder hkey;
	if (RegOpenKey(HKEY_CURRENT_USER, keyPath, &hkey) == ERROR_SUCCESS
		|| RegOpenKey(HKEY_LOCAL_MACHINE, keyPath, &hkey) == ERROR_SUCCESS)
	{
		// read the nvram_directory value
		DWORD typ;
		TCHAR val[MAX_PATH];
		DWORD len = sizeof(val);
		if (RegQueryValueEx(hkey, _T("rompath"), 0, &typ, (BYTE*)val, &len) == ERROR_SUCCESS && typ == REG_SZ)
		{
			dir = val;
			return true;
		}
	}

	return false;
}
