// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Configuration file manager

#include "stdafx.h"
#include <vector>
#include <regex>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <stdio.h>
#include <string.h>
#include "Config.h"
#include "UtilResource.h"
#include "FileUtil.h"

// global singleton
ConfigManager *ConfigManager::inst = 0;

// Standard config file descriptor
const ConfigFileDesc MainConfigFileDesc = {
	nullptr,                        // store the file in the deployment folder
	_T("Settings.txt"),				// filename
	_T("DefaultSettings.txt"),		// default settings filename
	_T("PinballY")					// friendly application name
};


void ConfigManager::Init()
{
	if (inst == 0)
		inst = new ConfigManager();
}

void ConfigManager::Shutdown()
{
	delete inst;
	inst = 0;
}

ConfigManager::ConfigManager()
{
	dirty = false;
}

ConfigManager::~ConfigManager()
{
}

bool ConfigManager::Load(const ConfigFileDesc &fileDesc)
{
	// Get the My Documents path if possible
	TCHAR fname[MAX_PATH], defsFname[MAX_PATH], dir[MAX_PATH], docs[MAX_PATH];
	if (fileDesc.appDataSubdir != nullptr
		&& SUCCEEDED(SHGetFolderPath(0, CSIDL_APPDATA, 0, SHGFP_TYPE_CURRENT, docs)))
	{
		// got it - combine it with the app-specific subfolder to get
		// the full directory path
		PathCombine(dir, docs, fileDesc.appDataSubdir);

		// combine the relative filename with the AppData subfolder
		PathCombine(fname, dir, fileDesc.filename);

		// also build the defaults file name, if provided
		if (fileDesc.defaultSettingsFilename != nullptr)
			PathCombine(defsFname, dir, fileDesc.defaultSettingsFilename);
	}
	else
	{
		// The App Data subdir is null, or we can't resolve the path, so use
		// the deployment directory.
		GetDeployedFilePath(fname, fileDesc.filename, _T(""));

		// also build the defaults file name, if provided
		if (fileDesc.defaultSettingsFilename != nullptr)
			GetDeployedFilePath(defsFname, fileDesc.defaultSettingsFilename, _T(""));
	}

	// Now pull the directory portion from the combined path.  The filename
	// might have added its own subfolder within the app folder, so we can't
	// assume the full path is 'dir'.
	_tcscpy_s(dir, fname);
	PathRemoveFileSpec(dir);

	// If the containing folder doesn't exist, try creating it
	if (!DirectoryExists(dir))
	{
		// try creating it
		if (!CreateSubDirectory(dir, docs, 0))
		{
			DWORD err = GetLastError();
			LogSysError(EIT_Warning, MsgFmt(IDS_ERR_CONFIGMKDIR, dir),
				MsgFmt(_T("CreateDirectory failed, win32 error %lx"), err));
			return false;
		}
	}

	// remember the filename and friendly app name
	this->filename = fname;

	// If the file doesn't exist, try writing to the location to make sure
	// we'll be able to save the config later.
	if (PathFileExists(fname))
	{
		// load the file
		Reload();
	}
	else
	{
		// It's okay if the file doesn't exist, since this simply represents 
		// an empty/default configuration, but make sure we can write this 
		// location to gain some confidence that we'll be able to save any
		// settings changes later.
		FILE *fp;
		int err = _tfopen_s(&fp, fname, _T("w"));
		if (err == 0)
		{
			// success - simply leave the empty file behind
			fclose(fp);

			// populate the settings with a boilerplate comment header
			contents.emplace_back(LoadStringT(IDS_CFG_COMMENT1).c_str());
			contents.emplace_back(LoadStringT(IDS_CFG_COMMENT2).c_str());
			contents.emplace_back(_T(""));
			Set(_T("UpdateTime"), _T("New"));
			dirty = true;
		}
		else
		{
			// Failed - this location must not be writable.  Log an error
			// and abort.
			LogSysError(EIT_Warning,
				MsgFmt(IDS_ERR_CONFIGWRITEDIR, fname),
				MsgFmt(_T("_tfopen(write mode) failed, error code %d"), errno));
			return false;
		}

		// If a default settings file was specified, load it.  This is a
		// separate settings file with defaults for a new installation.
		// (We use a separate file for the defaults, rather than simply
		// including an initial settings file, to better support both new
		// installations and upgrades.  A user who's upgrading an existing
		// installation will already have a settings file from their old
		// version, so we don't want to run the risk of overwriting that
		// by providing a new settings file in the distribution.  Instead,
		// we use this separate defaults file.  For a new user, we'll see
		// that there's no previous settings file, so we'll load the
		// defaults; for an upgrading user, we'll find the existing
		// settings file and load that.)
		if (fileDesc.defaultSettingsFilename != nullptr)
			LoadFrom(defsFname);
	}

	// success - even if the Reload didn't load anything, we still
	// count this as success since we didn't run into any errors
	// trying to create a new file
	return true;
}

bool ConfigManager::Reload()
{
	// load from the current file
	return LoadFrom(filename.c_str());
}

bool ConfigManager::LoadFrom(const TCHAR *filename)
{
	// Clear out any previous configuration
	contents.clear();
	vars.clear();
	arrays.clear();

	// Open the file
	long filelen;
	SilentErrorHandler seh;
	WCHAR *filebuf = ReadFileAsWStr(filename, seh, filelen, 0);
	if (filebuf != 0)
	{
		// parse it
		WCHAR *p = filebuf;
		WCHAR *fileEnd = filebuf + filelen;
		while (p < fileEnd)
		{
			// scan to the newline
			auto nl = p;
			while (nl < fileEnd && *nl != '\n' && *nl != '\r')
				++nl;

			// add this line to the contents list
			contents.emplace_back(p, nl);

			// skip CR-LF sequences
			p = (nl == fileEnd ? nl : nl + 1);
			if (p != fileEnd && *nl == '\r' && *p == '\n')
				++p;
		}

		// done with the file contents
		delete[] filebuf;

		// parse NAME=VALUE pairs
		int lineno = 1;
		for (auto it = contents.begin(); it != contents.end(); ++it, ++lineno)
		{
			// get the item
			ConfigLine &l = (*it);

			// skip leading spaces
			TSTRING s = l.text;
			auto i = s.begin();
			for (; i != s.end() && _istspace(*i); ++i);

			// skip comments and blank lines
			if (i == s.end() || *i == '#')
				continue;

			// skip lines that don't start with a symbol character
			if (!_istalpha(*i) && *i != '_' && *i != '$' && *i != '.')
			{
				LogFileWarning(lineno, _T("Invalid name symbol"));
				continue;
			}

			// take this as the start of the name and seek the end of the name
			auto name = i;
			for (; i != s.end() && *i != '=' && !_istspace(*i); ++i);
			auto nameEnd = i;

			// skip spaces
			for (; i != s.end() && _istspace(*i); ++i);

			// we need a '=' for a value
			if (*i != '=')
			{
				LogFileWarning(lineno, _T("Missing '=' in name/value pair"));
				continue;
			}

			// skip spaces after the '='
			for (++i; i != s.end() && _istspace(*i); ++i);

			// this is the value section
			auto value = i;

			// trim trailing spaces
			auto valueEnd = s.end();
			for (; valueEnd != value && _istspace(*(valueEnd - 1)); --valueEnd);

			// set the isolated name and value entries in the ConfigLine record
			l.name.assign(name, nameEnd);
			l.value.assign(value, valueEnd);

			// add an entry to the variable map as well
			AddVariable(l.name.c_str(), &l);
		}

		// we're clean after loading
		dirty = false;

		// notify subscribers
		for (auto s : subscribers)
			s->OnConfigReload();

		// success
		return true;
	}
	else
	{
		// file doesn't exist
		return false;
	}
}

bool ConfigManager::Save(bool silent)
{
	// notify subscribers
	for (auto s : subscribers)
		s->OnConfigPreSave();

	// set the update timestamp in the file
	TCHAR date[20], time[20];;
	GetDateFormatEx(LOCALE_NAME_INVARIANT, 0, 0, _T("ddd dd MMM yyyy"), date, _countof(date), 0);
	GetTimeFormatEx(LOCALE_NAME_INVARIANT, 0, 0, _T("HH:mm:ss"), time, _countof(time));
	Set(_T("UpdateTime"), ConfigLine::FormatString(_T("%s %s")), date, time);

	// open the file
	FILE *fp;
	const char *errSrc = 0;
	int err = _tfopen_s(&fp, filename.c_str(), _T("w, ccs=UTF-8"));
	if (err == 0)
	{
		// write all lines
		for (auto it = contents.begin(); it != contents.end(); ++it)
		{
			// skip erased items
			if (it->erased)
				continue;

			// convert to multibyte for the file

			// write the line and a newline
			if (_fputts(it->text.c_str(), fp) < 0
				|| _fputts(_T("\n"), fp) < 0)
			{
				errSrc = "_fputts() failed";
				err = errno;
				break;
			}
		}
	}
	else
	{
		// flag that fopen failed
		errSrc = "_tfopen(write mode) failed";
	}

	// done - close the file
	if (fclose(fp) < 0)
	{
		errSrc = "fclose() failed";
		err = errno;
	}

	// notify subscribers
	for (auto s : subscribers)
		s->OnConfigPostSave(err == 0);

	// check for error
	if (err != 0)
	{
		// if not in silent mode, report the problem
		if (!silent)
		{
			LogSysError(EIT_Warning, 
				MsgFmt(IDS_ERR_CONFIGWRITE,	filename.c_str()),
				MsgFmt(_T("%hs failed, error code %d"), errSrc, err));
		}

		// return failure
		return false;
	}
	else
	{
		// success - clear the dirty flag
		dirty = false;

		// declare victory
		return true;
	}
}

void ConfigManager::LogFileWarning(int lineno, const TCHAR *msg, ...)
{
	// format the message
	TCHAR *buf = 0;
	va_list ap;
	va_start(ap, msg);
	vasprintf(&buf, msg, ap);
	va_end(ap);

	// TO DO - log the message (somewhere - don't want to bother the user with it)

	// done with the formatted strings
	delete[] buf;
}

// Get a value
const TCHAR *ConfigManager::Get(const TCHAR *name, const TCHAR *defval) const
{
	auto it = vars.find(name);
	return it == vars.end() || it->second->erased ? defval : it->second->value.c_str();
}

// get a value as a bool
bool ConfigManager::GetBool(const TCHAR *name, bool defval) const
{
	// look up the variable; if not found, return the default value
	auto it = vars.find(name);
	if (it == vars.end() || it->second->erased)
		return defval;

	// do the conversion
	return ToBool(it->second->value.c_str());
}

bool ConfigManager::ToBool(const TCHAR *val)
{
	// treat "1", "true", "t", "yes", and "y" as true, others as false
	std::basic_regex<TCHAR> pat(_T("^\\s*(true|t|yes|y|1)"), std::regex_constants::icase);
	return std::regex_match(val, pat);
}

void ConfigManager::SetBool(const TCHAR *name, bool val)
{
	// on output, always use 1 for true and 0 for false, as this
	// representation is language-independent
	Set(name, val ? _T("1") : _T("0"));
}

// get a value as an int
int ConfigManager::GetInt(const TCHAR *name, int defval) const
{
	auto it = vars.find(name);
	return it == vars.end() || it->second->erased ? defval : ToInt(it->second->value.c_str());
}

int ConfigManager::ToInt(const TCHAR *val)
{
	return _ttoi(val);
}

// set a value as an int
void ConfigManager::Set(const TCHAR *name, int val)
{
	TCHAR buf[32];
	if (_itot_s<countof(buf)>(val, buf, 10) == 0)
		Set(name, buf);
}

// get a value as a float
float ConfigManager::GetFloat(const TCHAR *name, float defval) const
{
	auto it = vars.find(name);
	return it == vars.end() || it->second->erased ? defval : ToFloat(it->second->value.c_str());
}

float ConfigManager::ToFloat(const TCHAR *val)
{
	return static_cast<float>(_ttof(val));
}

// set a value as a float
void ConfigManager::SetFloat(const TCHAR *name, float val) 
{
	TCHAR buf[32];
	if (_stprintf_s(buf, _T("%f"), val) >= 0)
		Set(name, buf);
}

// set an array element to an int
void ConfigManager::SetArrayEle(const TCHAR *name, const TCHAR *index, int val)
{
	TCHAR buf[32];
	if (_itot_s<countof(buf)>(val, buf, 10) == 0)
		SetArrayEle(name, index, val);
}

// get a value as a RECT
RECT ConfigManager::GetRect(const TCHAR *name, RECT defval) const
{
	auto it = vars.find(name);
	return it == vars.end() || it->second->erased ? defval : ToRect(it->second->value.c_str());
}

RECT ConfigManager::ToRect(const TCHAR *val)
{
	RECT rc;
	if (_stscanf_s(val, _T("%ld,%ld,%ld,%ld"), &rc.left, &rc.top, &rc.right, &rc.bottom) == 4)
		return rc;
	else
		return { 0, 0, 0, 0 };
}

// set a value as a RECT
void ConfigManager::Set(const TCHAR *name, RECT &rc)
{
	TCHAR buf[50];
	_sntprintf_s<countof(buf)>(buf, _TRUNCATE, _T("%ld,%ld,%ld,%ld"), rc.left, rc.top, rc.right, rc.bottom);
	Set(name, buf);
}

// set an array value as a RECT
void ConfigManager::SetArrayEle(const TCHAR *name, const TCHAR *index, RECT &rc)
{
	TCHAR buf[50];
	_sntprintf_s<countof(buf)>(buf, _TRUNCATE, _T("%ld,%ld,%ld,%ld"), rc.left, rc.top, rc.right, rc.bottom);
	SetArrayEle(name, index, buf);
}

// Set to a formatted string value
void ConfigManager::Set(const TCHAR *name, ConfigLine::FormatString format, ...)
{
	va_list ap;
	va_start(ap, format);
	TCHAR *buf;
	if (vasprintf(&buf, format.format, ap) >= 0)
	{
		Set(name, buf);
		delete[] buf;
	}
	else
		Set(name, format);
}

// Add a variable to the internal variable map
void ConfigManager::AddVariable(const TCHAR *name, ConfigLine *line)
{
	// add it to the variable map
	vars.emplace(name, line);

	// check if it's an array variable
	const TCHAR *br = _tcschr(name, '[');
	if (br != 0)
	{
		// pull out the main variable name
		TSTRING main;
		main.assign(name, br - name);

		// skip the '['
		++br;

		// if it ends with ']', remove the ']'
		const TCHAR *endp = br + _tcslen(br);
		if (*(endp - 1) == ']')
			--endp;

		// pull out the index value
		TSTRING index;
		index.assign(br, endp - br);

		// Add the entry:  the arrays[] entry indexed by the
		// main name is a map, to which we want to add the
		// pair "index => full name".
		arrays[main].emplace(index, name);
	}
}

// Set a string value
void ConfigManager::Set(const TCHAR *name, const TCHAR *value)
{
	Set(vars.find(name), name, value);
}

// Set an array variable to a string
void ConfigManager::SetArrayEle(const TCHAR *name, const TCHAR *index, const TCHAR *value)
{
	// set the variable with the full name "name[index]"
	Set(MsgFmt(_T("%s[%s]"), name, index), value);
}

void ConfigManager::Set(std::unordered_map<TSTRING, ConfigLine *>::iterator it, const TCHAR *name, const TCHAR *value)
{
	// generate the new plaintext for the config file line
	TSTRING text;
	text.append(name);
	text.append(_T(" = "));
	text.append(value);

	// If we found it, set the value and rewrite the file text line with
	// the new value.  If not, insert a new line.
	if (it != vars.end())
	{
		// there's already an entry - overwrite the value and text
		it->second->text = text;
		it->second->value = value;

		// it's no longer erased
		it->second->erased = false;
	}
	else
	{
		// not found - insert a new item into the file contents
		contents.push_back(ConfigLine(text.c_str(), name, value));
		ConfigLine &l = contents.back();

		// add it to the variable map
		AddVariable(name, &l);
	}

	// mark the unsaved change
	dirty = true;
}

// create a variable if it doesn't already exist
void ConfigManager::Create(const TCHAR *name)
{
	// set the key to an empty string if it doesn't already exist or 
	// it's been erased
	auto it = vars.find(name);
	if (it == vars.end() || it->second->erased)
		Set(it, name, _T(""));
}

// delete an existing variable
void ConfigManager::Delete(const TCHAR *name)
{
	// Look up the variable.  If it's present, mark it as erased.
	// To avoid pathological situations where the same variable is
	// repeatedly set and erased, keep the config table entry; that
	// way we'll keep reusing the same entry on each cycle.
	if (auto it = vars.find(name); it != vars.end())
	{
		it->second->erased = true;
		dirty = true;
	}
}

// delete all variables matched by a callback
void ConfigManager::Delete(std::function<bool(const TSTRING &)> match)
{
	for (auto it = contents.begin(); it != contents.end(); ++it)
	{
		if (!it->erased && it->name.length() != 0 && match(it->name))
		{
			it->erased = true;
			dirty = true;
		}
	}
}

// delete an array
void ConfigManager::DeleteArray(const TCHAR *name)
{
	// look up the name in the array map
	auto arr = arrays.find(name);
	if (arr != arrays.end())
	{
		// Got it - delete all of its index values.  The entry in
		// the main 'arrays' map that we have in 'arr' is a pair 
		// with its 'second' containing a map of all of the index
		// values.  Each index value is a pair of "index => full name".
		// So just go through those pairs and delete each full name
		// element from the main variable table.
		for (auto& idx : arr->second)
			Delete(idx.second.c_str());
	}
}

// Enumerate array elements
void ConfigManager::EnumArray(
	const TCHAR *name,
	std::function<void(const TCHAR *val, const TCHAR *index, const TCHAR *fullName)> callback)
{
	// look up the array entry, with the list of index values
	auto array = arrays.find(name);
	if (array != arrays.end())
	{
		// iterate over the index values
		for (auto& ele : array->second)
		{
			// ele.first = the isolated index value
			// ele.second = the full variable name, "var[index]"
			// 
			// Look up the variable value from the main variable table,
			// which is indexed by the full name.
			auto v = vars.find(ele.second);
			callback(
				v == vars.end() ? 0 : v->second->value.c_str(),
				ele.first.c_str(), ele.second.c_str());
		}
	}
}

void ConfigManager::SetWindowPlacement(const TCHAR *name, const RECT &rcNormalPosition, int nFlags, int nShowCmd)
{
	Set(name, MsgFmt(_T("rcNormalPos(%d,%d,%d,%d),nFlags(%u),nShowCmd(%u)"),
		rcNormalPosition.left, rcNormalPosition.top, rcNormalPosition.right, rcNormalPosition.bottom,
		nFlags, nShowCmd));
}

BOOL ConfigManager::GetWindowPlacement(const TCHAR *name, RECT &rcNormalPosition, int nFlags, int nShowCmd)
{
	// try looking up the name
	const TCHAR *txt = Get(name);
	if (txt == 0)
		return FALSE;

	// set defaults for elements not in the list
	rcNormalPosition = { CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT };
	nFlags = 0;
	nShowCmd = SW_SHOWNORMAL;

	// parse the list
	std::basic_regex<TCHAR> rcPat(_T("\\s*rcNormalPos\\s*\\(\\s*(-?\\d+)\\s*,\\s*(-?\\d+)\\s*,\\s*(-?\\d+)\\s*,\\s*(-?\\d+)\\s*\\)\\s*(.*)"));
	std::basic_regex<TCHAR> flagsPat(_T("\\s*nFlags\\s*\\(\\s*(\\d+)\\s*\\)\\s*(.*)"));
	std::basic_regex<TCHAR> cmdPat(_T("\\s*nShowCmd\\s*\\(\\s*(\\d+)\\s*\\)\\s*(.*)"));
	while (*txt != 0)
	{
		std::match_results<const TCHAR *> m;
		if (std::regex_match(txt, m, rcPat))
		{
			rcNormalPosition.left = _ttoi(m[1].str().c_str());
			rcNormalPosition.top = _ttoi(m[2].str().c_str());
			rcNormalPosition.right = _ttoi(m[3].str().c_str());
			rcNormalPosition.bottom = _ttoi(m[4].str().c_str());
			txt = m[5].first;
		}
		else if (std::regex_match(txt, m, flagsPat))
		{
			nFlags = _ttoi(m[1].str().c_str());
			txt = m[2].first;
		}
		else if (std::regex_match(txt, m, cmdPat))
		{
			nShowCmd = _ttoi(m[1].str().c_str());
			txt = m[2].first;
		}
		else
		{
			// no match - fail
			return FALSE;
		}

		// skip the comma if present
		if (*txt == ',')
			++txt;
	}

	// success
	return TRUE;
}
