// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Process utility functions

#pragma once
#include "../rapidxml/rapidxml.hpp"
#include "StringUtil.h"

class ErrorHandler;


// -----------------------------------------------------------------------
//
// Search for the main window for a process, given the process ID.
// This searches through the top-level desktop windows, looking for
// a "main window" (defined as a visible window with no owner) that
// belongs to the target process.  If we find such a window, we
// fill in *pThreadId (if pThreadId isn't null) with the window's
// thread ID, and we return the window handle.  Returns NULL if no
// matching window is found.
HWND FindMainWindowForProcess(DWORD pid, DWORD *pThreadId);


// -----------------------------------------------------------------------
//
// Safer process termination.  This tries to terminate the process
// by means safer than the TerminateProcess() API: first by trying
// to close all of its windows, then by trying to inject a call to
// ExitProcess() into the process via a remote thread.  If those
// attempts fail, we fall back on TerminateProcess() to make sure
// it's ultimately killed dead, but we use that as a last resort
// because of the potential system instability that API can cause.
void SaferTerminateProcess(HANDLE hProcess);

// -----------------------------------------------------------------------
//
// Terminate a process via it's process name rather than via the
// process HANDLE.  This should have the same effect as the built-in
// TerminateProcess() call, however terminating the process by name
// seems to work in cases where terminating the process via it's
// HANDLE does not.  This is provided as an alternative for those
// cases where niether SaferTerminateProcess nor TerminateProcess
// manage to kill the process.
void TerminateProcessByName(const WCHAR *filename);

// -----------------------------------------------------------------------
//
// Create a merged environment block for CreateProcess().  This creates
// a WCHAR block containing all of the environment variables from a given
// old environment, merged with the variables provided by the caller. 
// Caller variables replace any from the old environment.
//
// In each variation the old environment block can be provided by the
// caller, or it can be inherited from the current process environment
// block by specifying nullptr for oldEnv.
// 

// provide the variables as an array of strings, in NAME=VAL format
void CreateMergedEnvironment(std::unique_ptr<WCHAR> &merged,
	const WCHAR *const *newVars, size_t nNewVars, 
	const WCHAR *oldEnv = nullptr);

// provide the variables as a list of string pointers
void CreateMergedEnvironment(std::unique_ptr<WCHAR> &merged,
	const std::list<const WCHAR*> &newVars,
	const WCHAR *oldEnv = nullptr);

// Provide the variables as a flattened list of NAME=VALUE pairs delimited
// by semicolons.  To use a literal semicolon within a VALUE section, use
// a stuttered semicolon (;;).
void CreateMergedEnvironment(std::unique_ptr<WCHAR> &merged, const TCHAR *vars);

// -----------------------------------------------------------------------
//
// Parse a command line, using the same algorithm as CreateProcess(), to
// determine the full path of the application file to be launched.
//
// Important:  The rules for this are not what you'd probably think, if
// you haven't read the details before.  Read the SDK documentation for
// the lpApplicationName and lpCommandLine parameters to CreateProcess()
// for a detailed accounting of the algorithm.
//
// On return, appName is populated with the application name portion of
// the command line.  If we can find an extant file matching the name,
// we return true, and appName contains the fully qualified name with
// path and extension, even if those aren't actually specified in the
// command line text.  If we can't find a matching file, we return
// false; appName still has our best guess at the token, but we don't
// attempt to add any path or extension information that isn't explicitly
// specified in the command text in this case, so appName might have
// a relative path or no path at all.
//
bool GetAppNameFromCommandLine(TSTRING &appName, const TCHAR *cmdLine);

// -----------------------------------------------------------------------
//
// Program manifest reader
//

class ProgramManifestReader
{
public:
	ProgramManifestReader() { }

	// Read a program's manifest.  Populates the internal XML 
	// document from the manifest.  Returns true on success, 
	// false on failure. 
	//
	// failIfMissing controls the return value if we successfully
	// load the program file, but it doesn't contain a manifest
	// resource.  If failIfMissing is true, we'll return failure
	// in this case; this is the default, since the caller is
	// usually only interested in knowing whether a manifest was
	// successfully loaded, and a missing manifest obviously can't
	// be loaded.  But if failIfMissing is false, we'll return
	// true if all else goes well but there simply isn't a
	// manifest resource embedded in the program.  The caller
	// can determine that the manifest is missing in this case by
	// checking to see if the text is empty.
	bool Read(const TCHAR *filename, bool failIfMissing = true);

	// is the text empty?
	bool IsEmpty() const { return contents.length() == 0; }

	// The manifest as a parsed XML document.  This is populated
	// on a successful call to Read().
	rapidxml::xml_document<char> doc;

	// Requested Execution Level constants.  These correspond to the valid
	// values of "level" in the <requestedExecutionLevel level="XXX"> tag
	// in a program manifest, in the manifest XML schema defined in the 
	// Windows SDK docs.
	enum RequestedExecutionLevel
	{
		Unknown,
		AsInvoker,
		HighestAvailable,
		RequireAdministrator
	};

	// retrieve the requested execution level from the parsed manifest
	RequestedExecutionLevel GetRequestedExecutionLevel();

protected:
	// Plain text contents of the manifest.  Note that this is
	// modified by the parsing process, since rapidxml parses
	// the text in situ.
	CSTRING contents;
};


// -----------------------------------------------------------------------
//
// Create a process, wait for it to finish, and capture its stdout and
// stderr output to a text buffer.  This should be used only for non-
// interactive processes that normally run in console windows.
//
bool CreateProcessCaptureStdout(
	const TCHAR *exe, const TCHAR *params, DWORD timeout,
	std::function<void(const BYTE* stdoutContents, long len)> onSuccess,
	std::function<void(const TCHAR *msg)> onError);


// -----------------------------------------------------------------------
//
// CreateProcessAsInvoker() - create process with the current process's
// privilege elevation level, if possible.
//
// This routine *mostly* works like the normal CreateProcess(), with
// one exception: if the program to run requests the "highestAvailable" 
// privilege level in its manifest, we interpret this to mean the level
// of the invoker process (i.e., the current process) rather than the
// highest privilege level available to the current user account.  The
// normal CreateProcess() does the opposite.
//
// The difference comes into play when our process isn't elevated, but
// the current user account is in the Administrators group, and thus
// *could* run a program elevated.  In that case, CreateProcess() will
// fail when presented with a "highestAvailable" program, because it
// interprets "highestAvailable" as meaning the user's Admin privileges,
// and it can't launch an elevated child process from a non-elevated 
// parent process.  In contrast, we succeed by interpreting "highest
// available" as our own user-level privileges, and launching the child
// in user mode.
//
// We work exactly the same way as CreateProcess() for "asInvoker" 
// programs, which always run at the same level as their parent, and 
// for "requireAdministrator" programs, which can't run at all without 
// elevation.
//
// Here's the full set of possible cases:
//
// - If we're running un-elevated:
//
//   - Launching an app marked "asInvoker" or "highestAvailable" will
//     succeed, with the child running un-elevated just like us
//
//   - Launching an app marked "requireAdministrator" fails with
//     ERROR_ELEVATION_REQUIRED
//
// - If we're running elevated: all launches succeed, with the child
//   running elevated just like us
//
// The interface is the same as CreateProcess().  A non-zero return
// indicates success and zero indicates failure; error details can
// be obtained from GetLastError().
//
BOOL CreateProcessAsInvoker(
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFO lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation);


