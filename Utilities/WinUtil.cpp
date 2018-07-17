// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "UtilResource.h"
#include <winsafer.h>
#include <Shlwapi.h>
#include <ShlObj.h>
#include <commdlg.h>
#include <Dlgs.h>
#include <math.h>
#include "../rapidxml/rapidxml.hpp"
#include "Util.h"
#include "WinUtil.h"
#include "Pointers.h"

#pragma comment(lib, "shlwapi.lib")


void ForceRectIntoWorkArea(RECT &rc, bool clip)
{
	// note the current width and height
	int cx = rc.right - rc.left;
	int cy = rc.bottom - rc.top;

	// get the nearest montior to the current area
	HMONITOR hMonitor = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);

	// retrieve the monitor info
	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	GetMonitorInfo(hMonitor, &mi);

	// Force the upper left corner into the working area.   Move
	// the window so that as much of it as possible fits into the
	// same monitor working area.
	rc.left = max(mi.rcWork.left, min(mi.rcWork.right - cx, rc.left));
	rc.top = max(mi.rcWork.top, min(mi.rcWork.bottom - cy, rc.top));

	// If we're clipping, also limit the size so that the bottom
	// right is within the work area
	if (clip)
	{
		// clipping - limit to the right bottom corner of the monitor
		rc.right = min(rc.left + cx, mi.rcWork.right);
		rc.bottom = min(rc.top + cy, mi.rcWork.bottom);
	}
	else
	{
		// not clipping - preserve the original size at the new location
		rc.right = rc.left + cx;
		rc.bottom = rc.top + cy;
	}
}

void ClipRectToWorkArea(RECT &rc, SIZE &minSize)
{
	// get the nearest montior to the current area
	HMONITOR hMonitor = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);

	// retrieve the monitor info
	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	GetMonitorInfo(hMonitor, &mi);

	// Force the lower right corner into bounds
	rc.right = min(mi.rcWork.right, rc.right);
	rc.bottom = min(mi.rcWork.bottom, rc.bottom);

	// If this reduced the size below the minimum, move the left
	// top corner as needed to bring it back up to the minimum.
	rc.left = min(rc.left, rc.right - minSize.cx);
	rc.top = min(rc.top, rc.bottom - minSize.cy);
}

bool IsWindowPosUsable(const RECT &rc, int minWidth, int minHeight)
{
	// get the nearest montior to the current area
	HMONITOR hMonitor = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);

	// retrieve the monitor info
	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	GetMonitorInfo(hMonitor, &mi);

	// Figure the intersection of the monitor's working area and
	// the window area
	RECT rcInt;
	IntersectRect(&rcInt, &rc, &mi.rcWork);

	// To pass the test, the top edge of the monitor must be within
	// the monitor work area, and the minimum height and width must
	// be showing.
	return rcInt.top == rc.top
		&& rcInt.right - rcInt.left >= minWidth
		&& rcInt.bottom - rcInt.top >= minHeight;
}

bool ValidateFullScreenLayout(const RECT &rc)
{
	// set up a context for the callback
	struct MonitorEnumCtx
	{
		MonitorEnumCtx(const RECT rc) : rc(rc), ok(false) { }
		RECT rc;
		bool ok;
	}
	ctx(rc);

	// enumerate the attached monitors
	EnumDisplayMonitors(0, 0, [](HMONITOR, HDC, LPRECT lprcMonitor, LPARAM lParam)
	{
		// get the context
		auto ctx = (MonitorEnumCtx*)lParam;

		// check for a match to the display area
		if (ctx->rc.left == lprcMonitor->left
			&& ctx->rc.top == lprcMonitor->top
			&& ctx->rc.right == lprcMonitor->right
			&& ctx->rc.bottom == lprcMonitor->bottom)
		{
			// it's a match - flag it and stop searching
			ctx->ok = true;
			return FALSE;
		}
		else
		{
			// no match yet - keep searching
			return TRUE;
		}
	}, (LPARAM)&ctx);

	// return the results
	return ctx.ok;
}

// -----------------------------------------------------------------------
//
// Safer process termination
//
void SaferTerminateProcess(HANDLE hProcess)
{
	// First, check to see if the process is still running.   
	// There's no need to kill it if it exited on its own.  
	// Give it a few milliseconds.
	if (WaitForSingleObject(hProcess, 10) != WAIT_OBJECT_0)
	{
		// The wait didn't immediately succeed, so the program is still
		// running (either that or an error happened in the wait itself,
		// in which case we don't know one way or the other about the
		// program, so we'll have to assume it's still running).
		//
		// If possible, stop the program gracefully by trying to close
		// its UI window(s).  This is always safer than killing the
		// process via TerminateProcess(), as that API can leave device
		// resources in unstable states.
		DWORD pid = GetProcessId(hProcess);
		for (int tries = 0; tries < 5; ++tries)
		{
			// Look for a main window.  If there isn't one, the graceful
			// approach won't work, so stop retrying.
			HWND hwnd = FindMainWindowForProcess(pid, nullptr);
			if (hwnd == NULL)
				break;

			// try closing this window
			::SendMessage(hwnd, WM_CLOSE, 0, 0);
		}

		// We're either out of windows or out of retries.  Check to see
		// if the program terminated on its own due to window closures.
		// If it did, great - we're done; if not, we have to resort to
		// more drastic measures.
		if (WaitForSingleObject(hProcess, 10) != WAIT_OBJECT_0)
		{
			// The next level of escalation is to try injecting a call
			// to the Win32 ExitProcess() API into the process.  We can
			// do this via a remote thread.  As long as the process in
			// question is a child process that we created ourselves, 
			// our process handle should already have the necessary
			// access rights to do this.
			DWORD tid;
			HandleHolder hThread(CreateRemoteThread(hProcess, NULL, 0,
				(LPTHREAD_START_ROUTINE)&ExitProcess, 0, 0, &tid));

			// Try to give the thread a chance to execute before we 
			// proceed, by doing a "wait" operation.  The thread 
			// shouldn't take very long to finish given that it's just
			// making the one API call to ExitProcess, so we shouldn't
			// need to give it more than a few milliseconds for its
			// own sake, but give it a bit longer anyway because there
			// are undoubtedly other things running the system that 
			// could preempt our new thread for a while.
			WaitForSingleObject(hThread, 30);
		}

		// Do one last check.  If the process is still alive, kill it
		// by fiat via TerminateProcess().  This is an undesirable last
		// resort because it can corrupt global data managed in DLLs
		// attached to the process.  But we really have no alternative
		// at this point, as we've already tried all of the safer
		// methods.
		if (WaitForSingleObject(hProcess, 10) != WAIT_OBJECT_0)
			TerminateProcess(hProcess, 0);
	}
}

// -----------------------------------------------------------------------
//
// Search for the main window for a given process ID
//
HWND FindMainWindowForProcess(DWORD pid, DWORD *pThreadId)
{
	// set up a search context
	struct EnumWindowsCtx
	{
		EnumWindowsCtx(DWORD pid) : pid(pid), tid(0), hWnd(NULL) { }
		DWORD pid;
		DWORD tid;
		HWND hWnd;
	} ctx(pid);

	// enumerate the top-level windows across the system
	EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL
	{
		// recover our search context
		auto ctx = (EnumWindowsCtx *)lParam;

		// If this is a main window, check its process ID.  A "main window"
		// is a visible window with no owner.
		if (IsWindowVisible(hWnd) && GetWindow(hWnd, GW_OWNER) == NULL)
		{
			// it's a top-level window - get its process and thread information
			DWORD winPid = 0;
			DWORD winTid = GetWindowThreadProcessId(hWnd, &winPid);
			if (winPid == ctx->pid)
			{
				// it's one of our target process's windows - use its thread
				ctx->tid = winTid;
				ctx->hWnd = hWnd;

				// stop the enumeration
				return FALSE;
			}
		}

		// it's not a match - continue the enumeration
		return TRUE;
	}, (LPARAM)&ctx);

	// pass the thread ID back to the caller if desired
	if (pThreadId != nullptr)
		*pThreadId = ctx.tid;

	// return the window handle
	return ctx.hWnd;
}

// -----------------------------------------------------------------------
//
// Windows error messages
//

WindowsErrorMessage::WindowsErrorMessage(DWORD errCode)
{
	Init(errCode);
}

WindowsErrorMessage::WindowsErrorMessage()
{
	Init(GetLastError());
}

void WindowsErrorMessage::Init(DWORD errCode)
{
	// remember the error code
	this->errCode = errCode;

	// format the message
	TCHAR *buf = NULL;
	FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&buf, 0, NULL);

	if (buf != NULL)
	{
		// strip newlines and save the result
		std::basic_regex<TCHAR> pat(_T("[\n\r]"));
		txt = std::regex_replace(buf, pat, _T(""));

		// free the local bufer
		LocalFree(buf);
	}
}

WindowsErrorMessage::~WindowsErrorMessage()
{
}

void WindowsErrorMessage::Reset()
{
	Init(GetLastError());
}


// -----------------------------------------------------------------------
//
// Program manifest reader
//


// Read a program's manifest.  The manifest is a plain text resource
// of type RT_MANIFEST, embedded within the .exe using the normal 
// Windows resource mechanism.  This finds the RT_MANIFEST resource,
// interprets it as plain ASCII text, and returns a string with the
// contents.
//
// The manifest should contain XML data that conforms to a schema
// defined in the Windows SDK docs, but we don't make any attempt
// to parse the XML or even determine that it is in fact XML.  We
// simply return the contents of the resource as plain text.
bool ProgramManifestReader::Read(const TCHAR *filename)
{
	// Internal callback for EnumResourceNames
	struct CallbackContext
	{
		CallbackContext(CSTRING &contents) : found(false), contents(contents) { }

		// contents of the manifest resource
		CSTRING &contents;

		// did we find the resource?
		bool found;
	};
	auto callback = [](HMODULE hModule, LPCTSTR lpType,	LPWSTR lpName, LONG_PTR lParam) -> BOOL
	{
		// get the context object
		auto ctx = reinterpret_cast<CallbackContext*>(lParam);

		// load the resource
		HRSRC hResInfo = FindResource(hModule, lpName, lpType);
		DWORD cbResourceSize = SizeofResource(hModule, hResInfo);
		HGLOBAL hResData = LoadResource(hModule, hResInfo);
		const BYTE *pResource = (const BYTE *)LockResource(hResData);

		// append the resource data to the string
		ctx->contents.append((const CHAR *)pResource, cbResourceSize);

		// record that we found at least one resource
		ctx->found = true;

		// continue the enumeration
		return TRUE;
	};

	// fail if the filename is null
	if (filename == nullptr)
		return FALSE;

	// load the EXE as a data file
	HMODULE hModule = LoadLibraryEx(filename, NULL, LOAD_LIBRARY_AS_DATAFILE);
	if (hModule == NULL)
		return FALSE;

	// enumerate RT_MANIFEST resources, populating our internal
	// contents string
	CallbackContext ctx(this->contents);
	EnumResourceNames(hModule, RT_MANIFEST, callback, reinterpret_cast<LONG_PTR>(&ctx));

	// we're done with the module
	FreeLibrary(hModule);

	// if we didn't find anything, return failure
	if (!ctx.found)
		return false;

	// parse the XML
	try
	{
		doc.parse<0>(contents.data());
	}
	catch (std::exception &)
	{
		return false;
	}

	// success
	return true;
}

// Read the "requested execution level" value 
ProgramManifestReader::RequestedExecutionLevel ProgramManifestReader::GetRequestedExecutionLevel()
{
	// Traverse down the tree to assembly/trustInfo/security/requestedPrivileges/
	// requestedExecutionLevel[level].  This element isn't mandatory, so there's
	// no guarantee that it will be present (and no guarantee that the XML even
	// conforms to the manifest schema, for that matter, as it's really just a
	// plain text resource embedded in the .exe).
	if (auto root = doc.first_node("assembly"); root != nullptr)
	{
		if (auto trustInfo = root->first_node("trustInfo"); trustInfo != nullptr)
		{
			if (auto security = trustInfo->first_node("security"); security != nullptr)
			{
				if (auto reqPriv = security->first_node("requestedPrivileges"); reqPriv != nullptr)
				{
					if (auto reqEx = reqPriv->first_node("requestedExecutionLevel"); reqEx != nullptr)
					{
						if (auto level = reqEx->first_attribute("level"); level != nullptr)
						{
							auto val = level->value();
							if (strcmp(val, "asInvoker") == 0)
								return AsInvoker;
							else if (strcmp(val, "highestAvailable") == 0)
								return HighestAvailable;
							else if (strcmp(val, "requireAdministrator") == 0)
								return RequireAdministrator;
						}
					}
				}
			}
		}
	}

	return Unknown;
}

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
// To implement the different behavior from the normal CreateProcess(),
// we use the undocumented __COMPAT_LEVEL=RunAsInvoker environment 
// variable to coerce the process to use "asInvoker" privilege level
// regardless of the level specified in its manifest.
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
	LPPROCESS_INFORMATION lpProcessInformation)
{
	// private copy of the environment, if we decide we need it
	std::unique_ptr<WCHAR> newEnv;

	// Get the EXE filename.  If lpApplicationName is non-null, it's
	// the filename.  Otherwise, we have to parse lpCommandLine and
	// pull out the first token.
	TSTRING exe;
	if (lpApplicationName != nullptr)
	{
		// use the application name
		exe = lpApplicationName;

		// if the filename doesn't exist, try adding .EXE
		if (!PathFileExists(exe.c_str()) && PathFileExists((exe + L".EXE").c_str()))
			exe += L".EXE";
	}
	else if (lpCommandLine != nullptr)
	{
		// There's no application name, so the program name should be
		// the first token of the command line.  Skip leading spaces
		const WCHAR *p = lpCommandLine;
		for (; iswspace(*p); ++p);

		// check for a quoted token
		if (*p == '"')
		{
			// it's quoted - scan for the matching close quote
			const WCHAR *start = ++p;
			for (; *p != 0 && *p != '"'; ++p);
			exe.assign(start, p - start);
		}
		else
		{
			// It's not quoted: scan for space delimiters.  The normal
			// CreateProcess() is documented as allowing ambiguous 
			// filenames here (with multiple unquoted spaces), so for
			// the sake of compatibility, we'll do the same.
			const WCHAR *start = p;
			while (*p != 0)
			{
				// scan for the next space
				for (; *p != 0 && !iswspace(*p); ++p);

				// check if this file exists
				exe.assign(start, p - start);
				if (PathFileExists(exe.c_str()))
					break;

				// check if this file + .EXE exists
				exe += L".EXE";
				if (PathFileExists(exe.c_str()))
					break;
			}
		}
	}

	// Get the program's requested execution level.  If it's
	// "highestAvailable" or unknown, apply the RunAsInvoker
	// coercion to try to force the program to run with our
	// privileges.
	//
	// DON'T apply the coercion if the requested level in the
	// manifest is "requireAdministrator".  The level means
	// that the program can't run properly without elevated
	// privileges, so it's better to let it fail up front if
	// we can't run it elevated than to run it in a way that
	// it specifically says it can't handle properly.
	//
	// Note that we won't automatically fail the request if
	// requireAdministrator is set.  We'll just skip the
	// RunAsInvoker coercion, and let CreateProcess() decide
	// what to do.  It's perfectly possible for to launch an
	// elevated child process if we're already elevated
	// ourselves, so that might work just fine - better to
	// delegate such things to the system than try to figure
	// it out ourselves.
	ProgramManifestReader manifest;
	auto requestedLevel = ProgramManifestReader::RequestedExecutionLevel::Unknown;
	if (manifest.Read(exe.c_str()))
		requestedLevel = manifest.GetRequestedExecutionLevel();
	if (requestedLevel == ProgramManifestReader::RequestedExecutionLevel::Unknown
		|| requestedLevel == ProgramManifestReader::RequestedExecutionLevel::HighestAvailable)
	{
		// We have an unknown level or "highest available".
		// Apply the coercion to RunAsInvoker.  To apply the
		// coercion, we have to inject __COMPAT_LAYER=RunAsInvoker
		// into the new process's environment block.  To avoid
		// confusing things in our own process environment,
		// create a local copy of the environment and make the
		// changes only in the local copy.
		//
		// Start by getting the original environment.  Use the
		// caller's custom environemnt block if they provided
		// one, otherwise use the process-wide environment.
		const WCHAR *env = lpEnvironment != nullptr ?
			static_cast<const WCHAR *>(lpEnvironment) :
			GetEnvironmentStringsW();

		// Measure the size so that we can allocate a new copy with
		// our added string.  The block is arranged with null-terminated
		// strings back-to-back, and a double null character (or an
		// empty string, if you prefer) marking the end of the whole
		// block.  So just search for the double null character.
		const WCHAR *envEnd = env;
		for (; envEnd[0] != 0 || envEnd[1] != 0; ++envEnd);

		// Allocate space for the current environment, plus our added
		// variable, plus the final null character.
		static const TCHAR compatVar[] = L"__COMPAT_LAYER=RunAsInvoker";
		static const size_t compatVarLen = countof(compatVar);
		newEnv.reset(new WCHAR[envEnd - env + 1 + compatVarLen + 1]);

		// Now copy the strings, except for any existing __COMPAT_LAYER
		// value, as we're going to replace that.  These are all in VAR=VAL 
		// format, delimited by null characters.  
		const WCHAR *src = env;
		WCHAR *dst = newEnv.get();
		for (; src[0] != 0; )
		{
			// get the length of this string, including null terminator
			size_t len = wcslen(src) + 1;

			// check for an existing __COMPAT_LAYER= variable
			if (len < 15 || _wcsnicmp(src, compatVar, 15) != 0)
			{
				// it's not __COMPAT_LAYER - copy it
				memcpy(dst, src, len * sizeof(WCHAR));

				// move the output pointer to the next write position
				dst += len;
			}

			// skip to the next string in the input
			src += len;
		}

		// Now add our __COMPAT_LAYER string
		memcpy(dst, compatVar, compatVarLen * sizeof(WCHAR));
		dst += compatVarLen;

		// add the final null character
		*dst = 0;

		// Our new environment block uses Unicode characters, so
		// let CreateProcess know about this via the flags
		dwCreationFlags |= CREATE_UNICODE_ENVIRONMENT;
	}

	// try the launch with the possibly modified environment
	return CreateProcess(lpApplicationName, lpCommandLine,
		lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags,
		newEnv.get(), lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
}

// -----------------------------------------------------------------------
//
// Browse for a folder
//
bool BrowseForFolder(TSTRING &path, HWND parent, const TCHAR *title, DWORD opts)
{
	// presume failure
	bool result = false;

	// create the dialog object
	RefPtr<IFileDialog> fd;
	RefPtr<IShellItem> psi;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fd))))
	{
		// set the "folder picker" option
		DWORD dwOptions;
		if (SUCCEEDED(fd->GetOptions(&dwOptions)))
		{
			// add the folder picker options
			dwOptions |= FOS_PICKFOLDERS | FOS_DONTADDTORECENT | FOS_NOCHANGEDIR;

			// add our own options
			if ((opts & BFF_OPT_ALLOW_MISSING_PATH) != 0)
				dwOptions &= ~(FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);

			// set the new options
			fd->SetOptions(dwOptions);
		}

		// set the title
		fd->SetTitle(title);

		// set the initial selected file
		fd->SetFileName(path.c_str());

		// if that's not empty, start in the parent folder
		if (path != _T(""))
		{
			// get the parent folder
			TCHAR parFolder[MAX_PATH];
			_tcscpy_s(parFolder, path.c_str());
			PathRemoveFileSpec(parFolder);
			RefPtr<IShellItem> parFolderItem;
			if (SUCCEEDED(SHCreateItemFromParsingName(parFolder, NULL, IID_PPV_ARGS(&parFolderItem))))
			{
				// start in this directory
				fd->SetFolder(parFolderItem);

				// set the file name to the file spec portion only
				if (const TCHAR *sl = _tcsrchr(path.c_str(), '\\'); sl != nullptr)
					fd->SetFileName(sl + 1);
			}
		}

		// show the dialog
		if (SUCCEEDED(fd->Show(NULL)))
		{
			if (SUCCEEDED(fd->GetResult(&psi)))
			{
				LPWSTR pstr;
				if (SUCCEEDED(psi->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &pstr)))
				{
					path = pstr;
					result = true;
				}
			}
		}
	}

	// return the result
	return result;
}

// -----------------------------------------------------------------------
//
// Browse for a file
//
bool BrowseForFile(TSTRING &path, HWND parent, const TCHAR *title)
{
	// presume failure
	bool result = false;

	// create the dialog object
	IFileDialog *fd = nullptr;
	IShellItem *psi = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fd))))
	{
		// set the "folder picker" option
		DWORD dwOptions;
		if (SUCCEEDED(fd->GetOptions(&dwOptions)))
			fd->SetOptions(dwOptions | FOS_DONTADDTORECENT | FOS_NOCHANGEDIR);

		// set the title
		fd->SetTitle(title);

		// get the path and file spec separately
		TCHAR folder[MAX_PATH];
		_tcscpy_s(folder, path.c_str());
		PathRemoveFileSpec(folder);
		const TCHAR *file = PathFindFileName(path.c_str());

		// set the initial filename and path
		if (folder[0] == 0)
		{
			// no folder at all - just set the filename
			fd->SetFileName(path.c_str());
		}
		else if (PathIsRelative(folder))
		{
			// relative path given - use the whole relative path as the
			// initial filename, and don't set a path at all
			fd->SetFileName(path.c_str());
		}
		else
		{
			// absolute path - set the path and folder separately
			fd->SetFileName(file);
			RefPtr<IShellItem> parFolderItem;
			if (SUCCEEDED(SHCreateItemFromParsingName(folder, NULL, IID_PPV_ARGS(&parFolderItem))))
				fd->SetFolder(parFolderItem);
		}

		// show the dialog
		if (SUCCEEDED(fd->Show(NULL)))
		{
			if (SUCCEEDED(fd->GetResult(&psi)))
			{
				LPWSTR pstr;
				if (SUCCEEDED(psi->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &pstr)))
				{
					path = pstr;
					result = true;
				}
			}
		}
	}

	// clean up
	if (fd != nullptr) fd->Release();
	if (psi != nullptr) psi->Release();

	// return the result
	return result;
}

// -----------------------------------------------------------------------
//
// Get the program registered for a given filename extension
//
bool GetProgramForExt(TSTRING &prog, const TCHAR *ext)
{
	// if the extension is null or empty, there's no program
	if (ext == nullptr || ext[0] == 0)
		return false;

	// Look up the program associated with the filename extension. 
	// AssocQueryString is meant to be able to look up the program
	// given an extension in one step, but this seems to fail with
	// .vpt (on my machine, at least).  Doing the lookup by progID
	// works, though.  So first, grab the progID for the extension,
	// which is given by the default value stored under 
	// HKEY_CLASSES_ROOT\<.extension>.
	TCHAR progId[256];
	LONG progIdLen = sizeof(progId);
	if (RegQueryValue(HKEY_CLASSES_ROOT, ext, progId, &progIdLen) != ERROR_SUCCESS)
		progId[0] = 0;

	// Now look up the associated executable.  Try the lookup by 
	// extension first - that seems to be the intended way to do 
	// this, according to the documentation, even though it doesn't
	// seem reliable.  If that fails, and we found a ProgID above,
	// try again using the ProgID.
	TCHAR buf[MAX_PATH];
	DWORD len = countof(buf);
	if (SUCCEEDED(AssocQueryString(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, ext, NULL, buf, &len))
		|| (progId[0] != 0 && SUCCEEDED(AssocQueryString(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, progId, NULL, buf, &len))))
	{
		// got it
		prog = buf;
		return true;
	}

	// no luck
	return false;
}

