// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Configuration file manager

#pragma once


#include "stdafx.h"
#include <stdarg.h>
#include <list>
#include <vector>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <functional>

// Configuration file description.  This specifies the file system
// location and name of the file.
struct ConfigFileDesc
{
	// "Application Data" subdirectory.  If this is non-null, the config
	// file is located in this subfolder of the standard Windows "Application 
	// Data" folder.  If this is null, or we can't resolve the Application Data
	// folder path, we'll use the deployment directory (the folder where the
	// program .EXE is installed) as the root folder.
	const TCHAR *appDataSubdir; 
	
	// File name, relative to the root folder set by appDataSubdir.
	const TCHAR *filename;

	// Default file.  If the file named above doesn't exist, we'll load
	// defaults from this file, read-only.  Any updates will be written back
	// to the file named above.  This allows a default settings file to be
	// included in the distribution without risk of overwriting the user's
	// customized settings file when updating to a new version.  The
	// filename for the defaults file is relative to the same root folder
	// as the "real" file above.
	const TCHAR *defaultSettingsFilename;

	// Application friendly name.  This is the name of the application for
	// presentation in the UI.  We use this to add a comment at the start of
	// the generated file if we have to create a new file from scratch, so
	// that the user knows where the file came from.
	const TCHAR *appFriendlyName;
};

// Standard config file descriptor for the main application.
// The file descriptor is external to the configuration manager 
// object so that other applications can reuse the class but store 
// their data in separate files.
extern const ConfigFileDesc MainConfigFileDesc;


// Config file line
class ConfigLine
{
public:
	ConfigLine(const TCHAR *text) 
		: text(text) { erased = false; }
	template<typename iter> ConfigLine(iter first, iter end)
		: text(first, end) { erased = false; }
	ConfigLine(const TCHAR *text, const TCHAR *name, const TCHAR *value) :
		text(text), name(name), value(value) { erased = false; }
	ConfigLine(const ConfigLine &src) :
		text(src.text), name(src.name), value(src.value) { erased = false; }

	struct FormatString
	{
		FormatString(const TCHAR *format) : format(format) { }
		const TCHAR *format;
	};

	// create from a sprintf-style format string
	ConfigLine(FormatString format, ...)
	{
		// format the message
		va_list ap;
		va_start(ap, format);
		TCHAR *buf;
		if (vasprintf(&buf, format.format, ap) >= 0)
		{
			// success - store and release the formatted text
			text = buf;
			delete[] buf;
		}
		else
		{
			// failed - use the raw format string
			text = format.format;
		}
		va_end(ap);

		// active line
		erased = false;
	}

	// text
	TSTRING text;

	// isolated name and value strings
	TSTRING name;
	TSTRING value;

	// Flag: this variable has been erased.  This line won't be saved
	// to the file.
	bool erased;
};

// Configuration manager
class ConfigManager
{
public:
	// Manage the global singleton
	static void Init();
	static void Shutdown();
	static ConfigManager *GetInstance() { return inst; }

	// Load the configuration file.  Returns true on success, false on failure.
	// If no configuration exists, creates a default configuration.  Displays
	// explanatory messages on errors.
	//
	// If the file doesn't exist, we'll try creating an empty file as a test
	// to make sure the location is writable, to gain confidence that we'll be
	// able to save any changes later.  If the test write succeeds, we don't
	// consider the absence of an existing file to be an error, so we don't
	// show any error or warning messages about it.  We simply proceed as 
	// though we had an empty file.
	bool Load(const ConfigFileDesc &fileDesc);

	// Get the filename
	const TCHAR *GetFilename() const { return filename.c_str(); }

	// Reload the current configuration file.  Returns true if the config
	// file exists, false if not.  A false return isn't an error; it simply
	// means that we're using defaults for all settings.
	bool Reload();

	// Save the configuration back to the original file.  If 'silent' is true,
	// we don't show any messages on error; otherwise, we display error dialogs
	// describing any error conditions.
	bool Save(bool silent = false);

	// Save if we have unsaved changes.  Returns true on success, false on
	// error.  If there are no changes to save, we simply return true.
	bool SaveIfDirty(bool silent = false)
	{
		return dirty ? Save() : true;
	}

	// Do we have unsaved changes?
	bool IsDirty() const { return dirty; }

	// Update subscriber.  This registers an object to notify on certain
	// config change events.
	class Subscriber
	{
	public:
		// Configuration file has been reloaded
		virtual void OnConfigReload() { }

		// Configuration file pre/post save events
		virtual void OnConfigPreSave() { }
		virtual void OnConfigPostSave(bool succeeded) { }

		virtual ~Subscriber()
		{
			// automatically unsubscribe on destruction
			ConfigManager *cfg = ConfigManager::GetInstance();
			if (cfg != 0)
				cfg->Unsubscribe(this);
		}
	};

	// Subscribe an object for notifications.
	void Subscribe(Subscriber *sub) { subscribers.push_back(sub); }
	void Unsubscribe(Subscriber *sub) { subscribers.remove(sub); }

	// get a variable, returning a default value (which itself defaults to
	// null) if the key isn't present
	const TCHAR *Get(const TCHAR *name, const TCHAR *defval = 0) const;

	// Get variables in various datatypes.  These versions return a default
	// value if the variable isn't present, with no error or warning.
	bool GetBool(const TCHAR *name, bool defval = false) const;
	int GetInt(const TCHAR *name, int defval = 0) const;
	float GetFloat(const TCHAR *name, float defval = 0.0f) const;
	COLORREF GetColor(const TCHAR *name, COLORREF defval = RGB(0, 0, 0)) const;
	RECT GetRect(const TCHAR *name, RECT defval = { 0, 0, 0, 0 }) const;

	// Convert from string to the various datatypes
	static const TCHAR *ToStr(const TCHAR *val) { return val; }
	static bool ToBool(const TCHAR *val);
	static int ToInt(const TCHAR *val);
	static float ToFloat(const TCHAR *val);
	static COLORREF ToColor(const TCHAR *val, COLORREF defval);
	static RECT ToRect(const TCHAR *val);

	// set a variable in various formats
	void Set(const TCHAR *name, const TCHAR *value);
	void Set(const TCHAR *name, int value);
	void Set(const TCHAR *name, RECT &rc);
	void SetFloat(const TCHAR *name, float value);
	void SetBool(const TCHAR *name, bool value);
	void SetColor(const TCHAR *name, COLORREF value);

	// set to a formatted string value
	void Set(const TCHAR *name, ConfigLine::FormatString format, ...);

	// Get/set a window placement value.
	BOOL GetWindowPlacement(const TCHAR *name, RECT &rcNormalPosition, int nFlags, int nShowCmd);
	void SetWindowPlacement(const TCHAR *name, const RECT &rcNormalPosition, int nFlags, int nShowCmd);

	// Set an array variable
	void SetArrayEle(const TCHAR *name, const TCHAR *index, const TCHAR *value);
	void SetArrayEle(const TCHAR *name, const TCHAR *index, int value);
	void SetArrayEle(const TCHAR *name, const TCHAR *index, RECT &rc);

	// Enumerate the variables in an array
	void EnumArray(const TCHAR *name, std::function<void(const TCHAR *val, const TCHAR *index, const TCHAR *fullName)> callback);

	// Create a variable.  If the variable doesn't exist, this creates
	// it with an empty string as the value.  This has no effect if the
	// variable already exists.
	void Create(const TCHAR *name);

	// Delete a variable
	void Delete(const TCHAR *name);

	// Delete all variables matched by a given callback
	void Delete(std::function<bool(const TSTRING &)> match);

	// Delete an array variable.  This deletes all instances
	// matching "name[*]".
	void DeleteArray(const TCHAR *name);

protected:
	// We use a global singleton, so the instance is managed internally
	ConfigManager();
	~ConfigManager();

	// global singleton instance
	static ConfigManager *inst;

	// load from a specific filename
	bool LoadFrom(const TCHAR *filename);

	// internal set with a variable entry already looked up
	void Set(std::unordered_map<TSTRING, ConfigLine *>::iterator it, const TCHAR *name, const TCHAR *value);

	// log a warning about syntax errors reading the file
	void LogFileWarning(int lineno, const TCHAR *msg, ...);

	// add a variable to our map
	void AddVariable(const TCHAR *name, ConfigLine *line);

	// filename
	TSTRING filename;

	// lines read from the file
	std::list<ConfigLine> contents;

	// hash of variables found among the file contents list
	std::unordered_map<TSTRING, ConfigLine *> vars;

	// Hash of array variables.  An array variable is specified with
	// a line of the form "key[index]=value", where "index" is an
	// arbitrary string.  These lines are indexed in the main variable
	// map keyed by their full "key[index]" strings, and they also appear
	// in this separate map keyed with just the "key" part.  That gives
	// us a map containing all of the "index" strings.  The second-level
	// map contains pairs of "index" -> "key[index]", which can then be
	// used to look up the main variable entry elements.  This map
	// doesn't actually contain any values or pointers to values, to
	// minimize redundant data that we have to keep in sync.  It's just
	// an index to make it fast to find all elements of an array.
	//
	// Note that our rules for modifying the main variable list make it
	// fairly easy to keep this in sync.  We never rename an existing
	// variable, and we never remove a variable from the list (we can
	// delete an item, but that merely marks it as deleted and leaves 
	// the list entry intact).  So the only operation that can alter
	// the main variable map, and hence the only operation that can
	// alter this map, is adding a new variable.
	std::unordered_map<TSTRING, std::unordered_map<TSTRING, TSTRING>> arrays;

	// do we have unsaved changes?
	bool dirty;

	// Notification subscribers
	std::list<Subscriber *> subscribers;
};
