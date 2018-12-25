// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// VPinMAME-related functions

#pragma once

class GameListItem;

class VPinMAMEIfc
{
public:
	// Name of base registry key for VPinMAME saved configuration data.  
	// This is under HKEY_CURRENT_USER.
	static const TCHAR *configKey;

	// Enumerate all installed VPinMAME ROMs.  Invokes the callback
	// for each ROM found in the registry.  The callback returns
	// true to continue the enumeration, false to stop.
	static void EnumRoms(std::function<bool(const TCHAR*)> func);

	// Find the VPinMAME ROM name for the given game.  This looks at
	// the VPM configuration data in the Windows registry to find a
	// matching entry for the game.  
	//
	// If a ROM name is found, it corresponds to an extant subkey of 
	// VPM's main registry key (HKCU\Software\Freeware\Visual PinMame).
	// The subkey's values contain the VPM saved configuration data for
	// the game (or, more specifically, for the ROM the game uses).
	//
	// Returns true if a suitable match was found, false if not.
	static bool FindRom(TSTRING &romName, const GameListItem *game);

	// Get a list of installed ROMs on this machine matching a given
	// prefix.  The naming convention for VPM ROMs is "game_ver",
	// where "game" is a common prefix for all of the ROM versions
	// for a given table, and "ver" is a version ID suffix.  E.g.,
	// Funhouse has ROMs like "fh_l1", "fh_l2", etc.
	//
	// The purpose of this routine is to figure out which version(s)
	// of a particular table's ROM is/are actually being used on this
	// machine, so that we can retrieve configuration information for
	// the game matching the runtime environment when it's played.
	//
	// We consider a ROM to be installed if it has an entry in the VPM
	// saved configuration data in the registry.  That means that the
	// ROM has been loaded into the VPM runtime at some point.
	//
	// The search name string can be provided as a simple prefix
	// ("fh" for Funhouse) or as the name of a particular ROM version
	// ("fh_l3").  If a version part is included, we'll strip that out
	// and search for just the prefix part.
	//
	static void GetInstalledRomVersions(
		std::list<TSTRING> &installedRoms,
		const TCHAR *searchName);

	// Get the VPM ROM file system path
	static bool GetRomDir(TSTRING &dir);
};
