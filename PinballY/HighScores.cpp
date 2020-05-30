// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "../Utilities/Config.h"
#include "../Utilities/ProcUtil.h"
#include "HighScores.h"
#include "GameList.h"
#include "Application.h"
#include "PlayfieldView.h"
#include "DOFClient.h"
#include "LogFile.h"

#include "../Utilities/std_filesystem.h"
namespace fs = std::filesystem;

HighScores::HighScores() :
	inited(false)
{
}

HighScores::~HighScores()
{
	// make sure the initialization thread finishes before we
	// delete the object
	if (hInitThread != NULL)
		WaitForSingleObject(hInitThread, INFINITE);
}

bool HighScores::Init()
{
	// file parser thread
	struct ThreadContext
	{
		ThreadContext(HighScores *self, HWND hwndPlayfieldView) :
			self(self), hwndPlayfieldView(hwndPlayfieldView) { }

		HighScores *self;
		HWND hwndPlayfieldView;
	};
	auto InitThreadMain = [](LPVOID lParam) -> DWORD
	{
		// get the 'self' pointer
		std::unique_ptr<ThreadContext> ctx(static_cast<ThreadContext*>(lParam));
		auto self = ctx->self;

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
			if (RegQueryValueEx(hkey, _T("nvram_directory"), 0, &typ, (BYTE*)val, &len) == ERROR_SUCCESS
				&& typ == REG_SZ)
				self->vpmNvramPath = val;

			// log it
			LogFile::Get()->Write(LogFile::HiScoreLogging,
				_T("High score retrieval (init): VPinMAME NVRAM path is %s\n"), val);
		}
		else
		{
			LogFile::Get()->Write(LogFile::HiScoreLogging,
				_T("High score retrieval (init): VPinMAME registry entry not found\n"));
		}

		// find the PINemHi.ini file path
		TCHAR pehIniFile[MAX_PATH];
		GetDeployedFilePath(pehIniFile, _T("PINemHi\\PINemHi.ini"), _T(""));
		self->iniFileName = pehIniFile;

		LogFile::Get()->Write(LogFile::HiScoreLogging,
			_T("High score retrieval (init): PinEMHi .ini file path is %s\n"), pehIniFile);

		// Load the file, ignoring errors, and normalizing it with a newline
		// at the end of the last line.
		long len;
		CapturingErrorHandler iniErr;
		self->iniData.reset(ReadFileAsStr(pehIniFile, iniErr, len,
			ReadFileAsStr_NullTerm | ReadFileAsStr_NewlineTerm));

		// if we found the file, process it
		if (self->iniData != nullptr)
		{
			LogFile::Get()->Write(LogFile::HiScoreLogging, 
				_T("High score retrieval (init): PinEMHi ini data loaded successfully\n"));

			// build the line index
			BYTE *start = self->iniData.get();
			for (BYTE *p = start; *p != 0; )
			{
				// if we're at a newline, store a line pointer
				if (*p == '\n' || *p == '\r')
				{
					// add the line pointer
					self->iniLines.push_back((char *)start);

					// skip CR/LF or LF/CR pairs
					BYTE *eol = p;
					if ((*p == '\r' && *(p + 1) == '\n') || (*p == '\n' && *(p + 1) == '\r'))
						++p;
					++p;

					// end the line at the first newline character
					*eol = 0;

					// this is the start of the next line
					start = p;
				}
				else
				{
					// not a newline - keep scanning
					++p;
				}
			}

			// Now scan the file
			std::regex commentPat("\\s*//.*");
			std::regex sectPat("\\s*\\[(.*)\\]\\s*");
			std::regex pairPat("([^\\s=][^=]*)=(.*)");
			std::regex vsnPat("(\\s+\\([^\\)]+\\))+$|[.,:\\(\\)]");
			CSTRING section;
			for (size_t i = 0, nLines = self->iniLines.size(); i < nLines; ++i)
			{
				// get the line pointer
				const char *p = self->iniLines[i];

				// skip comments
				if (std::regex_match(p, commentPat))
					continue;

				// check for a section marker
				std::match_results<const char*> m;
				if (std::regex_match(p, m, sectPat))
				{
					// note the new section and keep going
					section = m[1].str();
					continue;
				}

				// check for a name/value pair definition
				if (std::regex_match(p, m, pairPat))
				{
					// pull out the name and value strings
					CSTRING &name = m[1].str();
					CSTRING &val = m[2].str();

					// check which section we're in
					if (section == "romfind")
					{
						// [romfind] section.  This contains a list of "Friendly Name=file.nv"
						// definitions for ROMs.  The exact use for these isn't entirely clear
						// to me, but the default INI file says they're to help HyperPin and
						// PinballX figure the file name given the ROM name.  So I'm assuming
						// that some people populate their PBX database files with ROM names
						// using the "Friendly Name" strings listed here.  For easy migration,
						// we'll try to do the same thing.  So we'll compile a map of these
						// for lookup when asked to resolve a ROM name.  Use the lower-case
						// version of the name in the index to be more forgiving (I don't
						// think there's any benefit to exact-case matching here).
						std::transform(name.begin(), name.end(), name.begin(), ::tolower);
						self->romFind.emplace(name, val);

						// Get the root name, minus any version suffix, and minus most
						// punctuation
						CSTRING rootName = std::regex_replace(name, vsnPat, "");

						// find or add a fuzzy ROM lookup entry
						auto it = self->fuzzyRomFind.find(rootName);
						if (it == self->fuzzyRomFind.end())
							it = self->fuzzyRomFind.emplace(
								std::piecewise_construct,
								std::forward_as_tuple(rootName),
								std::forward_as_tuple(rootName.c_str())).first;

						// add this NVRAM file to the lookup entry's list
						it->second.nvFiles.push_back(val);
					}
					else if (section == "paths")
					{
						// [paths] section.  This contains the current folder paths
						// where PINemHi.exe will look for the NVRAM files per system.
						if (name == "VP")
							self->vpPath.Set("VP", val, i);
						else if (name == "FP")
							self->fpPath.Set("FP", val, i);

						LogFile::Get()->Write(LogFile::HiScoreLogging,
							_T("High score retrieval (init): path for %hs is %hs\n"),
							name.c_str(), val.c_str());
					}
				}
			}
		}
		else
		{
			LogFile::Get()->Write(LogFile::HiScoreLogging, 
				_T("High score retrieval (init): PinEMHi ini data not loaded\n"));
			iniErr.EnumErrors([](const ErrorList::Item &err) {
				LogFile::Get()->Write(LogFile::HiScoreLogging,
					_T("+ %s%s%s\n"),
					err.message.c_str(),
					err.details.length() != 0 ? _T(": ") : _T(""),
					err.details.c_str());
			});
		}

		// initialization is complete
		self->inited = true;

		// notify the main window that initialization is finished
		if (IsWindow(ctx->hwndPlayfieldView))
		{
			NotifyInfo ni(Initialized, nullptr, nullptr);
			ni.status = NotifyInfo::Success;
			::SendMessage(ctx->hwndPlayfieldView, HSMsgHighScores, 0, reinterpret_cast<LPARAM>(&ni));
		}

		// thread return value (not used)
		return 0;
	};

	// Run the initialization in a background thread, as it can take a
	// few seconds to complete in a debug build.  (The time-consuming
	// part is the bigram set construction for the ~2400 friendly ROM
	// names in the default PINEmHi config file.  We pre-build a bigram
	// set for each entry so that lookups are fast later.  The time to
	// build these adds up with so many entries.  It only takes about
	// 50ms in a release build, so we really could just do it inline,
	// but I got tired of waiting for the 5-second debug-build startup
	// delay in my own testing work.)
	DWORD tid;
	ThreadContext *ctx = new ThreadContext(this, Application::Get()->GetPlayfieldView()->GetHWnd());
	hInitThread = CreateThread(NULL, 0, InitThreadMain, ctx, 0, &tid);

	// if the thread startup failed, clean up
	if (hInitThread == NULL)
	{
		delete ctx;
		return false;
	}

	// success
	return true;
}

bool HighScores::IsInited()
{
	return inited;
}

bool HighScores::GetNvramFile(TSTRING &nvramPath, TSTRING &nvramFile, const GameListItem *game)
{
	// We can't proceed if initialization hasn't finished yet
	if (!IsInited())
		return false;

	// We can't proceed unless we have a valid game and system
	if (game == nullptr || game->system == nullptr)
		return false;

	LogFile::Get()->Write(LogFile::HiScoreLogging,
		_T("High score retrieval: determining NVRAM path for %s\n"), game->title.c_str());

	// check if the current file is populated and the file exists
	auto Valid = [&nvramPath, &nvramFile]()
	{
		// if the filename is empty, it's not valid
		if (nvramFile.length() == 0)
			return false;
		
		// build the full path and check if the file exists
		TCHAR path[MAX_PATH];
		PathCombine(path, nvramPath.c_str(), nvramFile.c_str());
		return FileExists(path);
	};

    // The NVRAM file arrangement varies by system
    const TSTRING &sysClass = game->system->systemClass;
    if (sysClass == _T("VP") || sysClass == _T("VPX"))
	{
		LogFile::Get()->Write(LogFile::HiScoreLogging, _T("+ Game is VP/VPX\n"));

		// Visual Pinball uses VPinMAME NVRAM files.  These are normally
		// located in the global VPinMAME NVRAM folder, which we can find
		// via the VPM config keys in the registry.  However, the system
		// entry in the config is allowed to override these with an 
		// explicit path setting.  So use the path from the system if
		// present, otherwise use the global path.  If the system path
		// is in relative format, combine it with the system's working
		// folder.
		if (game->system->nvramPath.length() == 0)
		{
			// no explicit path is specified - use the VPM NVRAM path
			nvramPath = vpmNvramPath;

			LogFile::Get()->Write(LogFile::HiScoreLogging, 
				_T("+ No explicit NVRAM setting in game; using VPinMAME NVRAM path = %s\n"), nvramPath.c_str());
		}
		else if (PathIsRelative(game->system->nvramPath.c_str()))
		{
			// it's a relative path - combine it with the system's working path
			TCHAR buf[MAX_PATH];
			PathCombine(buf, game->system->workingPath.c_str(), game->system->nvramPath.c_str());
			nvramPath = buf;

			LogFile::Get()->Write(LogFile::HiScoreLogging,
				_T("+ Game has relative NVRAM path; expanding to full path = %s\n"), nvramPath.c_str());
		}
		else
		{
			// it's an absolute path - use it exactly as given
			nvramPath = game->system->nvramPath;

			LogFile::Get()->Write(LogFile::HiScoreLogging,
				_T("+ Game has absolute NVRAM path; using path specified = %s\n"), nvramPath.c_str());

		}

		// Start with the explicit ROM setting in the game database 
		// entry.  If that's defined, it takes precedence, because it's
		// expressly set by the user and thus allows the user to override
		// any other heuristics we come up with.
		if (game->rom.length() != 0)
		{
			LogFile::Get()->Write(LogFile::HiScoreLogging,
				_T("+ Game has ROM explicitly specified in database = %ws\n"), game->rom.c_str());

			// We found an explicit ROM setting in the game database.
			nvramFile = game->rom;

			// If the file doesn't exist, check to see if it matches a
			// friendly ROM name from the [romfind] list.  If so, substitute
			// the associated .nv file.
			if (!Valid())
			{
				// look up the lower-cased name in the friendly ROM list
				CSTRING key = TSTRINGToCSTRING(nvramFile);
				std::transform(key.begin(), key.end(), key.begin(), ::_tolower);
				if (auto it = romFind.find(key); it != romFind.end())
					nvramFile = CSTRINGToTSTRING(it->second);

				LogFile::Get()->Write(LogFile::HiScoreLogging,
					_T("+ Specified ROM file doesn't exist; substituting .nv file = %s\n"), nvramFile.c_str());
			}
		}

		// If we don't have a valid result yet, the next stop is the ROM
		// that we matched for the table from the DOF config, if available.
		if (!Valid() && DOFClient::Get() != nullptr)
		{
			LogFile::Get()->Write(LogFile::HiScoreLogging,
				_T("+ No ROM file found that way; looking in DOF config\n"));

			if (const TCHAR *rom = DOFClient::Get()->GetRomForTable(game); rom != nullptr && rom[0] != 0)
			{
				// We found a DOF ROM.  But this isn't quite good enough to
				// pick a High Score NVRAM file, because the ROMs in the DOF
				// config are generally the "family" name rather than the
				// specific version.  For example, the DOF ROM entry for The
				// Addams Family is usually "taf", but the actual ROM will
				// be something like "taf_l1", "taf_l2", "taf_l3"...  The
				// suffix is a version number.  There's no formal structure
				// to the naming, but the longstanding convention is to use
				// "_" to delimit the version suffix, and it's consistent
				// enough that DOF uses this as a hardcoded assumption.  So
				// we will too.  
				//
				// So: starting with the DOF name, look in the NVRAM folder
				// for files of the form "<DOF name>_<suffix>.nv".  There
				// might even be a versionless file "<DOF name>.nv", so
				// count that as well.
				nvramFile = rom;
				TSTRING fileFound;
				int nFound = 0;
				static const std::basic_regex<TCHAR> dofNamePat(
					_T("(.+)(_([a-z0-9]+))?\\.nv$"), std::regex_constants::icase);

				LogFile::Get()->Write(LogFile::HiScoreLogging,
					_T("+ Guessing based on DOF ROM name = %s; scanning for matching files\n"), nvramFile.c_str());

				std::error_code ec;
				for (auto &file : fs::directory_iterator(nvramPath, ec))
				{
					// if it matches the pattern, stash it and count it
					TSTRING fname = file.path().filename();
					std::match_results<TSTRING::const_iterator> m;
					if (std::regex_match(fname, m, dofNamePat)
						&& _tcsicmp(m[1].str().c_str(), nvramFile.c_str()) == 0)
					{
						fileFound = fname;
						++nFound;

						LogFile::Get()->Write(LogFile::HiScoreLogging,
							_T("++ Found %s as possible NVRAM match\n"), fname.c_str());
					}
				}

				// If we found a unique matching file, take it as the result.
				// If multiple files exist, the user must have run multiple
				// versions of the ROM on this PC, so they might have multiple
				// versions of the table still installed, so we can't just
				// we have no way to guess which ROM version goes with which
				// table (and thus which ROM version goes with this table).
				if (nFound == 1)
				{
					nvramFile = fileFound;

					LogFile::Get()->Write(LogFile::HiScoreLogging,
						_T("++ Exactly one match found - using it (%s)\n"), nvramFile.c_str());
				}
				else if (nFound == 0)
				{
					LogFile::Get()->Write(LogFile::HiScoreLogging,
						_T("++ Zero matches found, keeping %s\n"), nvramFile.c_str());
				}
				else
				{
					LogFile::Get()->Write(LogFile::HiScoreLogging,
						_T("Multiple matches found - this is ambiguous, so keeping %s\n"), nvramFile.c_str());
				}
			}
		}

		// If we still don't have a valid result, try fuzzy matching the
		// game's title to the PINEmHi list of "friendly" ROM names.  The
		// friendly ROM names generally use the full title of the table,
		// plus some version information.  We build a table during startup
		// of just the title part of each [romfind] entry, so this makes a 
		// good basis for matching to the game title from the database.
		// The snag here is that most [romfind] titles are connected to
		// multiple ROM versions, so a title match alone won't tell us
		// which version we're using.  In most cases, we can resolve that
		// by going out to the NVRAM folder and checking to see which
		// files actually exist.  Most users will only use one version
		// of a ROM for a given game, so we'll probably only find one
		// matching file.  When that happens, we can reasonably assume
		// that the one matching file is the right one.
		if (!Valid())
		{
			LogFile::Get()->Write(LogFile::HiScoreLogging,
				_T("+ Still no match; trying a fuzzy match on the friendly ROM names\n"));

			// Retrieve the list of .nv files for the best matching
			// title in the [romfind] section.  Each [romfind] table
			// is typically associated with multiple ROM versions, so
			// this will give us a list of NVRAM files corresponding
			// to the title.
			std::list<TSTRING> nvList;
			if (GetAllNvramFiles(nvList, game->title.c_str()))
			{
				// Now scan through the associated NVRAM files to
				// see which ones actually exist.  An NVRAM file
				// should only exist for games that the user has
				// actually played.
				int n = 0;
				const TCHAR *fileFound = nullptr;
				for (auto const &nv : nvList)
				{
					// build the full file name
					TCHAR path[MAX_PATH];
					PathCombine(path, nvramPath.c_str(), nv.c_str());

					// check if the file exists
					if (FileExists(path))
					{
						fileFound = nv.c_str();
						++n;

						LogFile::Get()->Write(LogFile::HiScoreLogging,
							_T("++ Found a fuzzy match: %s\n"), nv.c_str());
					}
				}

				// If we found exactly one existing file, it must
				// be the unique version of the ROM that the user
				// has ever played on this PC, so it must be the 
				// one of interest for high score purposes.  If we
				// find more than one matching file, though, the
				// user must have multiple versions of this table
				// installed, so it's not safe to guess which NVRAM
				// file goes with which table file - we'll return
				// "not found" in this case and rely on the user to
				// resolve the conflict by setting the ROM name
				// explicitly in the database entry for the table.
				if (n == 1)
				{
					nvramFile = fileFound;

					LogFile::Get()->Write(LogFile::HiScoreLogging,
						_T("++ Found exactly one match - using it (%s)\n"), nvramFile.c_str());
				}
				else if (n == 0)
				{
					LogFile::Get()->Write(LogFile::HiScoreLogging,
						_T("++ No fuzzy matches found\n"));
				}
				else
				{
					LogFile::Get()->Write(LogFile::HiScoreLogging,
						_T("++ Multiple fuzzy matches found; this is ambiguous, so we can't use any of them\n"));
				}
			}
		}

		// VPinMAME ROM files are stored as .zip files, so the ROM name
		// in the config might refer to the zip file instead of just the
		// base name.  Strip any .zip suffix.
		std::basic_regex<TCHAR> zipPat(_T("\\.zip$"), std::regex_constants::icase);
		nvramFile = std::regex_replace(nvramFile, zipPat, _T(""));

		// if the name isn't empty and doesn't end in .nv, add the .nv suffix
		if (nvramFile.length() != 0 && !tstriEndsWith(nvramFile.c_str(), _T(".nv")))
		{
			nvramFile += _T(".nv");

			LogFile::Get()->Write(LogFile::HiScoreLogging,
				_T("+ The name so far doesn't end in .nv, so we're adding that -> %s\n"), nvramFile.c_str());
		}
	}
	else if (sysClass == _T("FP"))
	{
		LogFile::Get()->Write(LogFile::HiScoreLogging, _T("+ Game is FP\n"));
	
		// Future Pinball normally places its NVRAM files in the fpRAM
		// subfolder of the install directory.  Use that unless a path
		// is explicitly specified in the system config.
		if (game->system->nvramPath.length() != 0)
		{
			LogFile::Get()->Write(LogFile::HiScoreLogging, 
				_T("+ Explicit NVRAM path found for system = %s\n"), game->system->nvramPath.c_str());

			// There's an explicit config file setting - use it.  If it's
			// relative, combine it with the system's working path; otherwise
			// just use it exactly as given.
			if (PathIsRelative(game->system->nvramPath.c_str()))
			{
				TCHAR buf[MAX_PATH];
				PathCombine(buf, game->system->workingPath.c_str(), game->system->nvramPath.c_str());
				nvramPath = buf;

				LogFile::Get()->Write(LogFile::HiScoreLogging, 
					_T("+ Relative path specified; expanded to %s\n"), nvramPath.c_str());
			}
			else
			{
				// it's absolute - use it as-is
				nvramPath = game->system->nvramPath;

				LogFile::Get()->Write(LogFile::HiScoreLogging, 
					_T("+ Path is abolute, using as is, %s\n"), nvramPath.c_str());
			}
		}
		else
		{
			// the config doesn't specify a path, so use the default 
			// path <Future Pinball>\fpRAM
			TCHAR buf[MAX_PATH];
			PathCombine(buf, game->system->workingPath.c_str(), _T("fpRAM"));
			nvramPath = buf;

			LogFile::Get()->Write(LogFile::HiScoreLogging,
				_T("+ No path specified in system config; using default = %s\n"), nvramPath.c_str());
		}

		// The .fpram file is just the name of the game's .fp file with 
		// the extension replaced with ".fpram".  Start with the game's
		// filename from the configuration, stripped of the .fp suffix 
		// if present, then append ".fpram".
		std::basic_regex<TCHAR> extPat(_T("\\.fp$"), std::regex_constants::icase);
		nvramFile = std::regex_replace(game->filename, extPat, _T(""));
		nvramFile += _T(".fpram");

		LogFile::Get()->Write(LogFile::HiScoreLogging,
			_T("+ Final NVRAM file is %s\n"), nvramFile.c_str());

	}

	// the result is only valid if the file exists
	return Valid();
}

bool HighScores::GetAllNvramFiles(std::list<TSTRING> &nvList, const TCHAR *gameTitle)
{
	// Start with the title converted to lower-case and stripped of most
	// punctuation.
	TSTRING title = gameTitle;
	std::transform(title.begin(), title.end(), title.begin(), ::_totlower);
	std::basic_regex<TCHAR> punctPat(_T("[.,:\\(\\)]"));
	title = std::regex_replace(title, punctPat, _T(""));

	// get its bigram set
	DiceCoefficient::BigramSet<CHAR> bigrams;
	DiceCoefficient::BuildBigramSet(bigrams, TSTRINGToCSTRING(title).c_str());

	// search for the best match in the [romfind] list
	float bestScore = 0.0f;
	const FuzzyRomEntry *bestMatch = nullptr;
	for (auto const &f : fuzzyRomFind)
	{
		float score = DiceCoefficient::DiceCoefficient(bigrams, f.second.bigrams);
		if (score > bestScore)
		{
			bestScore = score;
			bestMatch = &f.second;
		}
	}

	// if we found a good enough match, return its NVRAM list
	if (bestScore > 0.7f)
	{
		// pass back the list
		for (auto const &f : bestMatch->nvFiles)
			nvList.emplace_back(CSTRINGToTSTRING(f));

		// success
		return true;
	}

	// no sufficiently strong matches were found
	return false;
}

bool HighScores::GetVersion(HWND hwndNotify, NotifyContext *notifyContext)
{
	// enqueue a version request, with the "-v" option
	TSTRING empty;
	EnqueueThread(new NVRAMThread(_T(" -v"), ProgramVersionQuery,
		nullptr, empty, empty, this, nullptr, hwndNotify, notifyContext));

	// success
	return true;
}

bool HighScores::GetScores(GameListItem *game, HWND hwndNotify, NotifyContext *notifyContext)
{
	// wrap the notify context in a unique_ptr to ensure it's disposed of
	std::unique_ptr<NotifyContext> notifyContextPtr(notifyContext);

	// try PINemHi first
	if (GetScoresFromNVRAM(game, hwndNotify, notifyContextPtr))
		return true;

	// try our ad hoc scores file if that failed
	if (GetScoresFromFile(game, hwndNotify, notifyContextPtr))
		return true;

	// no scores found
	return false;
}


bool HighScores::GetScoresFromNVRAM(GameListItem *game, HWND hwndNotify, std::unique_ptr<NotifyContext> &notifyContext)
{
	// We can't proceed if initialization hasn't finished yet
	if (!IsInited())
		return false;

	// If the game doesn't have a system, we can't proceed
	if (game == nullptr || game->system == nullptr)
		return false;

	// Get the NVRAM file; fail if we can't identify one
	TSTRING nvramPath, nvramFile;
	if (!GetNvramFile(nvramPath, nvramFile, game))
		return false;

    // get the PINemHi.ini file path entry for the system
    const TSTRING sysClass = game->system->systemClass;
    PathEntry *pathEntry = sysClass == _T("VP") || sysClass == _T("VPX") ? &vpPath :
		sysClass == _T("FP") ? &fpPath :
		nullptr;

	// we need a path entry to proceed
	if (pathEntry == nullptr)
		return false;

	// The PINemHi convention is to end the NVRAM path with a '\'
	if (!tstrEndsWith(nvramPath.c_str(), _T("\\")))
		nvramPath.append(_T("\\"));

	// Enqueue the request.  The command line is simply the name of the 
	// NVRAM file, but note that PINemHi seems to require the command line
	// to be constructed with a space before the first token.
	EnqueueThread(new NVRAMThread(
		MsgFmt(_T(" %s"), nvramFile.c_str()), HighScoreQuery,
		game, nvramPath, nvramFile, this, pathEntry, hwndNotify, notifyContext.release()));

	// the request was successfully submitted
	return true;
}

bool HighScores::GetScoresFromFile(GameListItem *game, HWND hwndNotify, std::unique_ptr<NotifyContext> &notifyContext)
{
	// try resolving the game's table file
	GameListItem::ResolvedFile rf;
	game->ResolveFile(rf);

	// look for a file with the same base name, with the extension replaced
	// with .pinballyHighScores
	std::basic_regex<TCHAR> pat(_T("\\.[^.\\\\/:]+$"));
	TSTRING filename = std::regex_replace(rf.path, pat, _T(".pinballyHighScores"));
	if (!FileExists(filename.c_str()))
		return false;

	// Enqueue a thread to read the file.  Note that there's no performance
	// reason that this is necessary, since this should be a small text file
	// that we can load almost instantly.  The only reason to do this in a
	// thread is that we *do* have to use a thread for the NVRAM reading,
	// since that's a little less than instantaneous given that it requires
	// launching the PINemHi subprocess.  And since we have to do that work
	// asynchronously, the whole mechanism for receiving the results has to
	// be designed to work asynchronously, via a message callback from the
	// worker thread.  The caller thus expects the request to return without
	// having completed.  To avoid surprises, then, we need the file reader
	// to work the same way.  That means we have to create a background
	// thread that sends the results to the main thread via a message call.
	// As long as we need the thread anyway for the results transfer, we
	// might as well do the file reading work there, too, just in case we
	// ever encounter a file that's slower to read for some reason (network
	// drive, floppy disk, who knows?).  That gives us the benefit of
	// robustness against slow devices, practically for free, since we
	// needed the background thread anyway.
	EnqueueThread(new FileThread(this, HighScoreQuery, game, hwndNotify, notifyContext.release(), filename.c_str()));

	// the request was successfully submitted
	return true;
}

void HighScores::EnqueueThread(Thread *thread)
{
	// hold the thread lock while manipulating the queue
	CriticalSectionLocker lock(threadLock);

	// add the new thread
	threadQueue.emplace_back(thread);

	// If this is the only queue in the thread, launch it immediately.
	// If there was already a thread in the queue, it's still running,
	// so it will take care of starting the next thread when it exits.
	if (threadQueue.size() == 1)
		LaunchNextThread(nullptr);
}

void HighScores::LaunchNextThread(Thread *exitingThread)
{
	// hold the thread lock while working
	CriticalSectionLocker lock(threadLock);

	// if a thread is exiting, remove it from the queue
	if (exitingThread != nullptr)
		threadQueue.remove(exitingThread);

	// get the next thread out of the queue and launch it
	while (threadQueue.size() != 0)
	{
		// get the next thread on the queue
		Thread *thread = threadQueue.front();

		// launch it
		DWORD tid;
		HandleHolder hThread(CreateThread(NULL, 0, &Thread::SMain, thread, 0, &tid));

		// If that succeeded, we're done - simply return and let the
		// thread launch the next thread in the queue.
		if (hThread != NULL)
			return;

		// The thread launch failed, so this request can't be
		// carried out after all.  Send a notification reply
		// to the caller to let them know that the request is
		// finished (unsuccessfully).
		NotifyInfo ni(thread->queryType, thread->game, thread->notifyContext.get());
		ni.status = NotifyInfo::ThreadLaunchFailed;
		::SendMessage(thread->hwndNotify, HSMsgHighScores, 0, reinterpret_cast<LPARAM>(&ni));

		// failed - discard this thread and try the next one
		delete threadQueue.front();
		threadQueue.pop_front();
	}
}

DWORD HighScores::Thread::SMain(LPVOID param)
{
	// For debugging purposes, make sure we're the only PinEMHi thread
	// running.  We can't launch multiple instances of PinEMHi concurrently
	// because we have to pass some information to it through its .ini 
	// file, which is a global resource.  The thread queue mechanism
	// *should* serialize PinEMHi launches naturally by its very design,
	// so we don't have to do anything here to do that; but let's just
	// verify that it's working as expected.
	static ULONG threadCounter = 0;
	InterlockedIncrement(&threadCounter);
	if (threadCounter != 1)
		OutputDebugString(_T("Warning! Multiple concurrent high score threads detected!\n"));

	// get a unique pointer to the thread, so that we delete it on exit
	std::unique_ptr<Thread> self(reinterpret_cast<Thread*>(param));

	// run the thread main entrypoint
	self->Main();

	// we're now down with the PinEMHi launch portion of our job - un-count
	// the concurrent process launcher
	InterlockedDecrement(&threadCounter);
	if (threadCounter != 0)
		OutputDebugString(_T("Warning! High score background thread counter is not zero at thread exit\n"));

	// before exiting, launch the next thread
	self->hs->LaunchNextThread(self.get());

	// done
	return 0;
}

HighScores::NVRAMThread::NVRAMThread(
	const TCHAR *cmdline, QueryType queryType,
	GameListItem *game, const TSTRING &nvramPath, const TSTRING &nvramFile,
	HighScores *hs, PathEntry *pathEntry, HWND hwndNotify, NotifyContext *notifyContext) :
	Thread(hs, queryType, game, hwndNotify, notifyContext),
	cmdline(cmdline),
	nvramPath(nvramPath),
	nvramFile(nvramFile),
	pathEntry(pathEntry)
{
}

void HighScores::NVRAMThread::Main()
{
	// Set up the results object to send to the notifier window.
	// We'll send a notification whether we succeed or fail.
	NotifyInfo ni(queryType, game, notifyContext.get());

	// send the result message to the notification window
	auto SendResult = [&ni, this](NotifyInfo::Status status)
	{
		ni.status = status;
		SendMessage(hwndNotify, HSMsgHighScores, 0, reinterpret_cast<LPARAM>(&ni));
	};

	// Check to see if the current INI file path matches the one we
	// inferred for this game.  If not, rewrite the INI file with the
	// new path.  We do this for two reasons: first, so that the user
	// doesn't have to manually configure this INI file when setting
	// up the system, and second, so that it's possible to use
	// different NVRAM paths for different VP/FP versions.  PINemHi
	// doesn't contemplate the possibility of multiple versions, as
	// it just has one path per system, but this makes it possible
	// by fixing up the INI file before each run.  Fortunately, it's
	// not likely that we'll be rewriting the file a lot in practice,
	// as the typical setup has just one FP version installed and
	// shares a single VPM installation across all VP versions
	// (which is pretty much a requirement given that VPM is a COM
	// object with a single global binding).
	//
	// Note that the pathEntry object is inside the shared HighScores
	// object, so it might seem like we should hold the thread lock
	// here.  We don't actually have to do that, though, because we
	// only mess with the pathEntry objects within the launcher
	// threads, and our thread dispatch setup ensures that only one
	// of these threads is running at a time.
	//
	// By the same token, the file itself is a shared resource among
	// the launcher threads, since every invocation of PINemHi will
	// read the file.  So we can't have one thread updating the file 
	// while another thread is launching PINemHi.  That's actually the
	// larger reason that we serialize execution of the launcher
	// threads: going one at a time ensures that each PINemHi instance
	// reads the version of the INI file that we prepared for it in 
	// the same thread and eliminates any confusion about the order
	// of events.
	//
	// If there's no path entry, it means that we're running PINemHi
	// for a generic query (to get the program version number, for
	// example), so there's no INI file entry to check or patch.
	CSTRING nvramPathC;
	if (pathEntry != nullptr && pathEntry->path != (nvramPathC = TSTRINGToCSTRING(nvramPath)))
	{
		// we need to update the INI file
		LogFile::Get()->Write(LogFile::HiScoreLogging,
			_T("High score retrieval: opening PinEMHi INI file for update\n"));

		// open the INI file
		FILEPtrHolder fp;
		if (int err = _tfopen_s(&fp, hs->iniFileName.c_str(), _T("w")); err == 0)
		{
			// write the contents
			for (size_t i = 0; i < hs->iniLines.size(); ++i)
			{
				// if this is the line we're updating, update it;
				// otherwise just copy the original
				if (i == pathEntry->lineNo)
					fprintf(fp, "%s=%s\n", pathEntry->name.c_str(), nvramPathC.c_str());
				else
					fprintf(fp, "%s\n", hs->iniLines[i]);
			}

			// if this entry didn't originally have an entry, add one for it
			if (pathEntry->lineNo == -1)
				fprintf(fp, "[paths]\n%s=%s\n", pathEntry->name.c_str(), nvramPathC.c_str());

			// done with the file
			fp.fclose();

			// remember the new path - this reflects the new file status
			pathEntry->path = nvramPathC;
		}
		else
		{
			// failure
			LogFile::Get()->Write(LogFile::HiScoreLogging,
				_T("+ error opening PinEMHi INI file for update: %s\n"),
				FileErrorMessage(err).c_str());
			SendResult(NotifyInfo::IniFileUpdateFailed);
			return;
		}
	}

	// get the PINemHi folder and executable name
	TCHAR folder[MAX_PATH], exe[MAX_PATH];
	GetDeployedFilePath(folder, _T("PINemHi"), _T(""));
	GetDeployedFilePath(exe, _T("PINemHi\\PINemHi.exe"), _T(""));

	// create a pipe for reading the results
	HandleHolder hReadPipe, hWritePipe;
	if (!CreatePipe(&hReadPipe, &hWritePipe, NULL, 0))
	{
		SendResult(NotifyInfo::CreatePipeFailed);
		return;
	}

	// set up the startup info for the console program
	STARTUPINFO sinfo;
	ZeroMemory(&sinfo, sizeof(sinfo));
	sinfo.cb = sizeof(sinfo);
	sinfo.dwFlags = STARTF_USESTDHANDLES;
	sinfo.hStdInput = INVALID_HANDLE_VALUE;
	sinfo.hStdOutput = hWritePipe;
	sinfo.hStdError = hWritePipe;

	// don't let the child inherit our end of the stdout pipe
	SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(hWritePipe, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

	// log the command line
	LogFile::Get()->Write(LogFile::HiScoreLogging, 
		_T("PinEMHi command line: \"%s\" %s\n"), exe, cmdline.c_str());

	// Launch the program.  Use CREATE_NO_WINDOW to run it invisibly,
	// so that we don't get UI cruft from flashing a console window
	// onto the screen briefly.
	PROCESS_INFORMATION pinfo;
	ZeroMemory(&pinfo, sizeof(pinfo));
	if (!CreateProcess(exe, cmdline.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW,
		NULL, folder, &sinfo, &pinfo))
	{
		// log the error
		WindowsErrorMessage errmsg;
		LogFile::Get()->Write(_T("PinEMHi process launch failed: %s"), errmsg.Get());

		// notify the caller and abort
		SendResult(NotifyInfo::ProcessLaunchFailed);
		return;
	}

	// Close our copy of the write end of the pipe.  The child process has
	// its own inherited handle now; we need to close our end so that Windows
	// knows no one is going to write anything more to the pipe after the
	// child process exits; if we kept our end of the handle open, it would
	// make our ReadFile wait forever, since Windows would hold out hope
	// that someone might one day write to the open handle.
	hWritePipe = NULL;

	// Wait for the program to finish - not too long, as it should do its 
	// work and exit almost immediately.  Ideally it should take just a
	// few tens of milliseconds to run, but it could take longer just to
	// launch if the system is busy, so give it a few seconds.
	if (WaitForSingleObject(pinfo.hProcess, 7500) == WAIT_OBJECT_0)
	{
		// Success - read the results.  In some cases (about 10% of the time
		// in my testing), ReadFile hangs indefinitely on this read after we've
		// already read the last available byte.  It's clearly a race condition
		// of some kind, but I can't tell what I'm doing wrong.  The main "rule"
		// here is that we have to close our write handle on the pipe: once all
		// write handles on the pipe are closed, Windows knows that no further
		// data can be written to the pipe, hence ReadFile() should return
		// immediately with zero bytes if the pipe buffer is empty.  Maybe 
		// PinEMHi doesn't always close *its* end of the pipe, but I don't think 
		// that should matter, because Windows should force the handle closed 
		// when the process exits.  Which we know it has, because we wouldn't
		// have gotten past the Wait on the process handle above otherwise.
		// The hack solution is to add the PeekNamedPipe(), which tests that
		// the buffer has bytes available before attempting the read.
		CHAR buf[4096];
		DWORD nBytes;
		while (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &nBytes, NULL) && nBytes != 0
			&& ReadFile(hReadPipe, buf, sizeof(buf), &nBytes, NULL) && nBytes != 0)
		{
			buf[nBytes] = 0;
			ni.results.append(AnsiToTSTRING(buf));
		}

		// results are from PINemHi
		ni.source = NotifyInfo::Source::PINemHi;

		// log the results
		LogFile::Get()->Write(LogFile::HiScoreLogging,
			_T("PinEMHi completed successfully; results:\n>>>\n%s\n>>>\n"), ni.results.c_str());

		// Notify the callback window of the result
		SendResult(NotifyInfo::Success);
	}
	else
	{
		// Timed out - the PinEMHi child process seems to be stuck
		LogFile::Get()->Write(LogFile::HiScoreLogging,
			_T("!! PinEMHi process wait timed out; killing process\n"));

		// Kill it so that we don't leave a zombie process hanging around
		SaferTerminateProcess(pinfo.hProcess);

		// Notify the callback of the failure
		SendResult(NotifyInfo::NoReplyFromProcess);
	}

	// close the process handles
	CloseHandle(pinfo.hThread);
	CloseHandle(pinfo.hProcess);
}

void HighScores::FileThread::Main()
{
	// Set up the results object to send to the notifier window.
	// We'll send a notification whether we succeed or fail.
	NotifyInfo ni(queryType, game, notifyContext.get());

	// send the result message to the notification window
	auto SendResult = [&ni, this](NotifyInfo::Status status)
	{
		ni.status = status;
		SendMessage(hwndNotify, HSMsgHighScores, 0, reinterpret_cast<LPARAM>(&ni));
	};

	// try reading the file
	long len;
	std::unique_ptr<BYTE> b(ReadFileAsStr(filename.c_str(), SilentErrorHandler(), len, 0));
	if (b == nullptr || len > INT_MAX)
	{
		// failed
		SendResult(NotifyInfo::Status::FileReadFailed);
		return;
	}

	// pass back the results as a TSTRING
	ni.results = AnsiToWideCnt(reinterpret_cast<const CHAR*>(b.get()), static_cast<int>(len));

	// indicate that the results came from a file
	ni.source = NotifyInfo::Source::File;

	// send the successful results
	SendResult(NotifyInfo::Status::Success);
}

HighScores::NotifyInfo::NotifyInfo(QueryType queryType, GameListItem *game, NotifyContext *notifyContext) :
	status(Success),
	queryType(queryType),
	gameID(game != nullptr ? game->internalID : 0),
	context(notifyContext)
{
}
