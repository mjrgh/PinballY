// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Windows utility functions

#pragma once
#include "../rapidxml/rapidxml.hpp"
#include "StringUtil.h"

// -----------------------------------------------------------------------
//
// Miscellaneous window layout functions
//

// Force a rectangle into a valid monitor working area.  This can be
// used to move a window into a viewable display area if any portion
// of the window currently lies outside the work area.  The result 
// rectangle is chosen to be as close as possible to original window
// rectangle, but with the upper left corner of the rect within the 
// bounds of a monitor's work area, and with as much of the rest of 
// the rect as possible also within the same monitor's work area. 
//
// If 'clip' is true, after moving the window as described above, we
// then resize the window if necessary to keep the bottom right corner
// within the work area.  If 'clip' is false, the window size isn't
// changed, even if the bottom and/or right extend out of the work area.
void ForceRectIntoWorkArea(RECT &rc, bool clip);

// Clip a rectangle to a valid monitor working area.  This is similar
// to ForceRectIntoWorkArea, but rather than trying to move the window,
// we resize it if the lower right is out of bounds.  If this takes
// the size below the given minimum size, we then move the upper left
// as needed to return to the minimum size.
void ClipRectToWorkArea(RECT &rc, SIZE &minSize);

// Determine if a window location is "usable", meaning that enough
// of the window is within a visible monitor area that the user can
// move the window if desired.  The minimum requirement is that some
// part of the window's caption bar is within a monitor work area,
// since you can usually move a window to a better location as long
// as you can reach its caption bar with the mouse.  We go a little
// further by letting the caller specify a minimum height and width
// that must be visible.  To pass our test, the visible area of the
// window must include the top of the window (since that's where the
// caption bar starts) plus the specified minimum height, and must
// include the specified minimum width.
bool IsWindowPosUsable(const RECT &rc, int minWidth, int minHeight);

// Validate a proposed full-screen location.  This can be used when
// restoring a window location from a saved configuration, and the
// location is flagged as a full-screen setup.  We'll search for a
// monitor that matches the given coordinates, and return true if
// we find a match, false if not.  A false return usually indicates
// that the monitor setup has changed since the configuration was
// saved, so it's no longer appropriate to show a full-screen layout
// at the saved location.  The window should usually simply be 
// restored at a default location on the main monitor instead.
bool ValidateFullScreenLayout(const RECT &rc);


// -----------------------------------------------------------------------
//
// Miscellaneous process management functions
//

// Search for the main window for a process, given the process ID.
// This searches through the top-level desktop windows, looking for
// a "main window" (defined as a visible window with no owner) that
// belongs to the target process.  If we find such a window, we
// fill in *pThreadId (if pThreadId isn't null) with the window's
// thread ID, and we return the window handle.  Returns NULL if no
// matching window is found.
HWND FindMainWindowForProcess(DWORD pid, DWORD *pThreadId);

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


// -----------------------------------------------------------------------
//
// Handle holder.  This is a simple RAII object that closes a handle
// upon going out of scope.
//
struct HandleHolder
{
	HandleHolder() : h(NULL) { }
	HandleHolder(HANDLE h) : h(h) { }
	~HandleHolder() { Clear(); }
	HANDLE h;

	operator HANDLE() const { return h; }
	HANDLE* operator&() { return &h; }
	void operator=(HANDLE h) 
	{
		Clear();
		this->h = h;
	}

	void Clear()
	{
		if (h != NULL && h != INVALID_HANDLE_VALUE)
			CloseHandle(h);
	
		h = NULL;
	}

	bool operator==(HANDLE h) { return this->h == h; }

	// detach the handle from this holder
	HANDLE Detach()
	{
		HANDLE ret = h;
		h = NULL;
		return ret;
	}
};

// Registry key handler holder
struct HKEYHolder
{
	HKEYHolder() : hkey(NULL) { }
	HKEYHolder(HKEY hkey) : hkey(hkey) { }
	~HKEYHolder() { Clear(); }

	HKEY hkey;

	operator HKEY() const { return hkey; }
	HKEY* operator&() { return &hkey; }
	void operator=(HKEY hkey)
	{
		Clear();
		this->hkey = hkey;
	}

	void Clear()
	{
		if (hkey != NULL)
			RegCloseKey(hkey);

		hkey = NULL;
	}

	bool operator==(HKEY hkey) { return this->hkey == hkey; }

	HKEY Detach()
	{
		HKEY ret = hkey;
		hkey = NULL;
		return ret;
	}
};

// -----------------------------------------------------------------------
//
// Windows system error code message retriever.  This gets the
// message text for a system error code as returned by GetLastError().
//
class WindowsErrorMessage
{
public:
	WindowsErrorMessage();						// initialize with GetLastError()
	WindowsErrorMessage(DWORD errorCode);		// initialize with the specified error code
	~WindowsErrorMessage();

	const TCHAR *Get() const { return txt.c_str(); }
	DWORD GetCode() const { return errCode; }

	// reset to the current error code
	void Reset();

	// reset to a specific error code
	void Reset(DWORD err) { Init(err); }
	
protected:
	void Init(DWORD err);

	DWORD errCode;
	TSTRING txt;
};

// -----------------------------------------------------------------------
//
// Critical section object.  This can be used as a lightweight resource
// lock to protect a resource against concurrent access by multiple
// threads.  A critical section is held exclusively by one thread at
// a time; other threads trying to acquire the lock will wait until
// the current thread releases it.  Recursive acquisition on the same
// thread is allowed (and counted, so each lock must have a matching 
// unlock).
//
// Critical sections are best for short sections of code that need to
// access a shared resource BRIEFLY (e.g., a member variable in an 
// object).  They shouldn't be used for locks held for long periods,
// as the wait on acquisition is normally infinite and there's no way
// to include a critical section in a multi-object wait (which would
// be needed if you wanted to allow canceling a wait via a separate
// signal, say).  These also aren't great for objects with a lot of
// contention among threads; Windows has better syncrhonization
// objects for situations requiring queued waits or priorities or
// the like.
//
// As with all resource locking, be careful of deadlocks when holding
// multiple locks.
//
class CriticalSection
{
public:
	CriticalSection() { InitializeCriticalSection(&cs); }
	~CriticalSection() { DeleteCriticalSection(&cs); }

	operator CRITICAL_SECTION*() { return &cs; }

protected:
	CRITICAL_SECTION cs;
};

// Critical section locker.  Uses RAII to guarantee the lock
// is released when the object goes out of scope.
class CriticalSectionLocker
{
public:
	CriticalSectionLocker(CRITICAL_SECTION *cs) : cs(cs) { EnterCriticalSection(cs); }
	~CriticalSectionLocker() { Unlock(); }

	void Unlock()
	{
		if (cs != nullptr)
		{
			LeaveCriticalSection(cs);
			cs = nullptr;
		}
	}

	void Lock(CRITICAL_SECTION *cs)
	{
		// Release any previous lock.  Do this first, so that the caller
		// doesn't have to worry about deadlocks due to lock acquisition
		// ordering.
		Unlock();

		// acquire the new lock
		this->cs = cs;
		EnterCriticalSection(cs);
	}

protected:
	CRITICAL_SECTION *cs;
};

// -----------------------------------------------------------------------
//
// "Well-known" SID helper for standard Windows SIDs
//
struct WellKnownSid : SID
{
public:

	WellKnownSid(BYTE authority,
		DWORD firstSubAuthority,
		DWORD secondSubAuthority = 0)
	{
		::ZeroMemory(this, sizeof(WellKnownSid));

		Revision = SID_REVISION;
		SubAuthorityCount = (0 != secondSubAuthority ? 2 : 1);
		IdentifierAuthority.Value[5] = authority;
		SubAuthority[0] = firstSubAuthority;
		SubAuthority[1] = secondSubAuthority;
	}

	BYTE GetAuthority() const { return IdentifierAuthority.Value[5]; }
	DWORD GetFirstSubAuthority() const { return SubAuthority[0]; }
	DWORD GetSecondSubAuthority() const { return SubAuthority[1]; }

	static WellKnownSid Everyone()
	{
		return WellKnownSid(WorldAuthority, SECURITY_WORLD_RID);
	}

	static WellKnownSid Administrators()
	{
		return WellKnownSid(NtAuthority, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS);
	}

	static WellKnownSid Users()
	{
		return WellKnownSid(NtAuthority, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS);
	}

	enum WellKnownAuthorities
	{
		NullAuthority,
		WorldAuthority,
		LocalAuthority,
		CreatorAuthority,
		NonUniqueAuthority,
		NtAuthority,
		MandatoryLabelAuthority = 16
	};

private:

	DWORD m_secondSubAuthority;
};

// -----------------------------------------------------------------------
//
// Get the program (.exe file) registered for a filename extension.
// Returns true if a program is successfully found.
//
bool GetProgramForExt(TSTRING &prog, const TCHAR *ext);

// -----------------------------------------------------------------------
//
// Browse for a folder/file using the new-style file dialogs
//
bool BrowseForFolder(TSTRING &selectedPath, HWND parent, const TCHAR *title, DWORD options = 0);
bool BrowseForFile(TSTRING &path, HWND parent, const TCHAR *title);

// BrowseForFolder options
#define BFF_OPT_ALLOW_MISSING_PATH    0x00000001   // allow selecting a folder that doesn't exist

