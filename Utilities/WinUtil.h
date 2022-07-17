// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Windows utility functions

#pragma once
#include "StringUtil.h"

// -----------------------------------------------------------------------
//
// Format window text.  This retrieves the current window text for
// the window, uses it as a sprintf-style format string to format the
// provided varargs values, then sets the window text to the result.
//
void FormatWindowText(HWND hwnd, ...);
void FormatWindowTextV(HWND hwnd, va_list ap);

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
// A "better" SetForegroundWindow.  This uses a tricky procedure that
// Microsoft recommends to bring the process to the foreground more
// reliably than the basic Win32 SetForegroundWindow() function, involving
// attaching to the current foreground window's thread's input state.
// This should generally be used in place of SetForegroundWindow(), since
// it has the same intended effect, but just tends to have that effect
// more reliably than the base SFW().
//
void BetterSetForegroundWindow(HWND hwndActive, HWND hwndFocus);

// Is our process the foreground application?  This checks the process
// ID of the current system-wide foreground window to determine if the
// window belongs to the current process.
bool IsForegroundProcess();


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

// -----------------------------------------------------------------------
//
// Registry key handler holder
//
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
// Resource locker
//
class ResourceLocker
{
public:
	ResourceLocker(int id, const TCHAR *rt) : hres(NULL), hglobal(NULL), data(nullptr), sz(0)
	{
		// find the resource
		if ((hres = FindResource(G_hInstance, MAKEINTRESOURCE(id), rt)) != NULL)
		{
			// load it
			if ((hglobal = LoadResource(G_hInstance, hres)) != NULL)
			{
				// get its size and lock it
				sz = SizeofResource(G_hInstance, hres);
				data = LockResource(hglobal);
			}
		}
	}

	~ResourceLocker()
	{
		if (hres != NULL)
			UnlockResource(hres);
	}

	// get the data pointer; null if the resource wasn't found
	const void *GetData() const { return data; }

	// get the data size in bytes
	DWORD GetSize() const { return sz; }

	// get the resource handle
	HRSRC GetHRes() const { return hres; }

	// get the HGLOBAL handle
	HGLOBAL GetHGlobal() const { return hglobal; }

protected:
	HRSRC hres;          // resource handle
	HGLOBAL hglobal;     // hglobal with loaded resource
	const void *data;    // pointer to data
	DWORD sz;            // size of the data
};


// -----------------------------------------------------------------------
//
// HGLOBAL locker.  Creates and locks an HGLOBAL memory object.
//
class HGlobalLocker
{
public:
	// allocate and lock an HGLOBAL
	HGlobalLocker(UINT flags, SIZE_T size) : p(nullptr)
	{
		if ((h = GlobalAlloc(flags, size)) != NULL)
			p = GlobalLock(h);
	}

	~HGlobalLocker()
	{
		if (h != NULL)
		{
			if (p != nullptr)
				GlobalUnlock(h);
			GlobalFree(h);
		}
	}

	// get the handle
	HGLOBAL GetHandle() const { return h; }

	// get the locked buffer
	void *GetBuf() const { return p; }

protected:
	// object handle
	HGLOBAL h;

	// locked buffer pointer
	void *p;
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

#pragma warning(push)
#pragma warning(disable:6201)
		SubAuthority[0] = firstSubAuthority;
		SubAuthority[1] = secondSubAuthority;
#pragma warning(pop)
	}

	BYTE GetAuthority() const { return IdentifierAuthority.Value[5]; }
	DWORD GetFirstSubAuthority() const { return SubAuthority[0]; }
	DWORD GetSecondSubAuthority() const { 
		// NB: The SDK headers define the SubAuthority array using
		// the type-unsafe C idiom of the "overallocated struct",
		// where SubAuthority is declared as an array with one element,
		// but it's the last thing in the struct, so the run-time
		// size of the array can be expanded beyond one element by
		// allocating extra space when malloc'ing the struct.
		// There's no way to tell the compiler we're doing this,
		// though, so the compiler (properly) generates a warning
		// about out-of-bounds access if we reference SubAuthority[1].
		// We can suppress the warning by obtaining a pointer to the
		// base of the array before indexing it, by writing
		// (&SubAuthority[0]).  While this might seem like we're just
		// "shutting up the compiler", this is arguably also a more
		// semantically correct way to express the access, given that
		// SubAuthority[] isn't truly a one-element array at all;
		// it's just declared that way because the language lacks
		// syntax to express a struct containing an embedded array of
		// varying size.  The SubAuthority member in the struct is
		// more properly interpreted as the address of the zeroeth
		// element of a dynamically allocated array.  Explicitly
		// converting the address expression to a pointer before
		// indexing it makes this interpretation explicit.  This 
		// slight rewrite does also happen to "shut up the compiler",
		// but it's not because it's a hack, rather because it
		// expresses the meaning more accurately than the one-element
		// array declaration from the SDK header does.  It's the
		// one-element array declaration that's actually dishonest
		// here.  Note also that this access isn't type-safe, in
		// that the compiler can't statically check that the struct
		// was actually allocated with enough space for a two-element
		// version of the array, so the caller is responsible for
		// verifying the size before access by checking the
		// SubAuthorityCount member.
		return (&SubAuthority[0])[1]; 
	}

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

