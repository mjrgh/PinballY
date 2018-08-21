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
// Program manifest reader
//

class ProgramManifestReader
{
public:
	ProgramManifestReader() { }

	// Read a program's manifest.  Populates the internal XML 
	// document from the manifest.  Returns true on success, 
	// false on failure.
	bool Read(const TCHAR *filename);

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


