// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Log file interface.  The log file is global to the app, so there's
// one singleton instance.
//
#pragma once
#include "../Utilities/Config.h"
#include "../Utilities/LogError.h"

class LogFile : public ConfigManager::Subscriber
{
public:
	// Initialize - creates the global singleton
	static void Init();

	// initialize with config settings
	void InitConfig();

	// Shut down
	static void Shutdown();

	// get the global singleton instance
	static LogFile *Get() { return inst; }

	// Feature flags.  These allow messages to be conditionally
	// displayed according to which features are enabled.
	static const DWORD BaseLogging        = 0x00000001;   // basic logging; always enabled
	static const DWORD MediaFileLogging   = 0x00000002;   // media file setup
	static const DWORD SystemSetupLogging = 0x00000004;   // system setup and table search
	static const DWORD CaptureLogging     = 0x00000008;   // media capture
	static const DWORD TableLaunchLogging = 0x00000010;   // table launch
	static const DWORD DmdLogging         = 0x00000020;   // DMD setup
	static const DWORD DofLogging         = 0x00000040;   // DOF
	static const DWORD JSLogging          = 0x00000080;   // Javascript
	static const DWORD MediaDropLogging   = 0x00000100;   // media file drag-and-drop operations
	static const DWORD HiScoreLogging     = 0x00000200;   // high score retrieval

	// Is a feature enabled?
	bool IsFeatureEnabled(DWORD feature) { return ((enabledFeatures | tempFeatures) & feature) != 0; }

	// Enable a feature temporarily.  This doesn't affect the global
	// settings; it just enables a feature for the duration of the 
	// session, or until the temporary override is withdrawn.
	void EnableTempFeature(DWORD feature);

	// Withdraw a temporary feature.  This reverses the effect of
	// EnableTempFeature() on the given feature.  This doesn't affect
	// the persistent settings.  It also doesn't disable a feature
	// that's enabled in the persistent settings; it only removes
	// the additional temp enabling.
	void WithdrawTempFeature(DWORD feature);

	// write a message
	void Write(const TCHAR *fmt, ...);

	// Write a message if logging for the given feature is enabled.
	//
	// (This AND's the feature flag against the current enable mask,
	// and writes the message only if the result is nonzero.  Note
	// that this means you can pass an OR combination of feature bits
	// if desired; the message is written if any of the bits are set
	// in the enable mask.)
	void Write(DWORD feature, const TCHAR *fmt, ...);

	// write a message with a timestamp
	void WriteTimestamp(const TCHAR *fmt, ...);
	void WriteTimestamp(DWORD feature, const TCHAR *fmt, ...);

	// common write handler with formatting, feature flag testing,
	// and optional timestamps
	void WriteV(bool timestamp, DWORD features, const TCHAR *fmt, va_list ap);

	// basic string writer
	void WriteStr(const TCHAR *str);
	void WriteStrA(const CHAR *str);

	// Start a group.  This adds a blank line if any non-empty lines
	// have been written since the last group start.  If the feature
	// mask is given, the group is only started if the feature bit is
	// set in the feature enable mask.
	void Group(DWORD feature = BaseLogging);

protected:
	LogFile();
	~LogFile();

	// config file notifications
	virtual void OnConfigReload() override;

	// critical section for writing
	CriticalSection lock;

	// OS file handle for the log file
	HandleHolder h;

	// Feature enable mask.  This is a bitwise combination of feature
	// flags determining which features are enabled for logging.
	DWORD enabledFeatures;

	// Temporarily enabled features.  This allows callers to temporarily
	// enable additional log features dynamically, without changing the
	// persistent settings.
	DWORD tempFeatures;

	// global singleton instance
	static LogFile *inst;

	// Number of consecutive newlines at end of output.  We keep track
	// of this for the Group() function, so that we know how many 
	// newlines we need to add to ensure there's a blank line at the
	// end of the output.
	int nNewlines;
};


// Error handler that captures directly to the log file
class LogFileErrorHandler : public ErrorHandler
{
public:
	LogFileErrorHandler(const TCHAR *prefixMessage = _T(""), DWORD featureMask = LogFile::BaseLogging) : 
		prefixMessage(prefixMessage), featureMask(featureMask){ }

	virtual void Display(ErrorIconType icon, const TCHAR *msg)
	{
		LogFile::Get()->Write(featureMask, _T("%s%s\n"), prefixMessage.c_str(), msg);
	}

	TSTRING prefixMessage;
	DWORD featureMask;
};

