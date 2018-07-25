// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Log file interface.  The log file is global to the app, so there's
// one singleton instance.
//
#pragma once

class LogFile
{
public:
	// Initialize - creates the global singleton
	static void Init();

	// Shut down
	static void Shutdown();

	// get the global singleton instance
	static LogFile *Get() { return inst; }

	// write a message
	void Write(const TCHAR *fmt, ...);

	// get the underlying OS file handle
	HANDLE GetFileHandle() const { return h; }

protected:
	LogFile();
	~LogFile();

	// OS file handle
	HandleHolder h;

	// global singleton instance
	static LogFile *inst;
};
