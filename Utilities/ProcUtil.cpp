// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <winsafer.h>
#include <Shlwapi.h>
#include "UtilResource.h"
#include "FileUtil.h"
#include "WinUtil.h"
#include "ProcUtil.h"


// -----------------------------------------------------------------------
//
// Run a child process, capturing stdout to a text buffer
//
bool CreateProcessCaptureStdout(
	const TCHAR *exe, const TCHAR *params, DWORD timeout,
	std::function<void(const BYTE*, long len)> onSuccess,
	std::function<void(const TCHAR*)> onError)
{
	// Set up an "inheritable handle" security attributes struct,
	// for creating the stdin and stdout/stderr handles for the
	// child process.  These need to be inheritable so that we 
	// can open the files and pass the handles to the child.
	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	// open the NUL file as stdin for the child
	HandleHolder hStdIn = CreateFile(_T("NUL"), GENERIC_READ, 0, &sa, OPEN_EXISTING, 0, NULL);

	// create a temp file to capture the stdout and stderr output
	HandleHolder hStdOut;
	TSTRING fnameStdOut;
	TCHAR tmpPath[MAX_PATH] = _T("<no temp path>"), tmpName[MAX_PATH] = _T("<no temp name>");
	GetTempPath(countof(tmpPath), tmpPath);
	GetTempFileName(tmpPath, _T("PBYTmp"), 0, tmpName);
	hStdOut = CreateFile(tmpName, GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	// log an error if that failed, but continue with the capture
	if (hStdOut == NULL)
	{
		// log the error
		WindowsErrorMessage err;
		onError(MsgFmt(_T("Unable to create temp file %s for output from child process (error %d, %s)"),
			tmpName, err.GetCode(), err.Get()));
		return false;
	}

	// set up the startup info for the console program
	STARTUPINFO sinfo;
	ZeroMemory(&sinfo, sizeof(sinfo));
	sinfo.cb = sizeof(sinfo);
	sinfo.dwFlags = STARTF_USESTDHANDLES;
	sinfo.hStdInput = hStdIn;
	sinfo.hStdOutput = hStdOut;
	sinfo.hStdError = hStdOut;

	// arrange to delete the temp file before we return
	struct TempFileDeleter
	{
		TempFileDeleter(HandleHolder &hFile, const TCHAR *fname) : hFile(hFile), fname(fname) { }
		HandleHolder &hFile;
		const TCHAR *fname;

		~TempFileDeleter()
		{
			hFile = NULL;
			DeleteFile(fname);
		}
	};
	TempFileDeleter tmpFileDeleter(hStdOut, tmpName);

	// get the folder containing the program to use as the working directory
	TCHAR folder[MAX_PATH];
	_tcscpy_s(folder, exe);
	PathRemoveFileSpec(folder);

	// Launch the program.  Use CREATE_NO_WINDOW to run it invisibly,
	// so that we don't get UI cruft from flashing a console window
	// onto the screen briefly.
	PROCESS_INFORMATION pinfo;
	ZeroMemory(&pinfo, sizeof(pinfo));
	TSTRING cmdline(params);
	if (!CreateProcess(exe, cmdline.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW,
		NULL, folder, &sinfo, &pinfo))
	{
		WindowsErrorMessage err;
		onError(MsgFmt(_T("Unable to create process %s (error %d, %s)"), exe, err.GetCode(), err.Get()));
		return false;
	}

	// close our copies of the file handles
	hStdIn = NULL;
	hStdOut = NULL;

	// arrange to close the handles when we're done
	HandleHolder hProc = pinfo.hProcess;
	CloseHandle(pinfo.hThread);

	// Wait for the program to finish, or until the timeout expires
	if (WaitForSingleObject(pinfo.hProcess, timeout) == WAIT_OBJECT_0)
	{
		class ReadErrorHandler : public ErrorHandler
		{
		public:
			ReadErrorHandler(decltype(onError) &onError) : onError(onError) { }
			decltype(onError) onError;

		protected:
			virtual void Display(ErrorIconType icon, const TCHAR *msg) override { onError(msg); }
		};
		ReadErrorHandler eh(onError);

		// Success - read the results
		long len = 0;
		std::unique_ptr<BYTE> buf(ReadFileAsStr(tmpName, eh, len, 0));
		if (buf == nullptr)
			return false;

		// send the result to the callback
		onSuccess(buf.get(), len);
	}
	else
	{
		// The process seems to be stuck.  Kill it so that we don't
		// leave a zombie process hanging around.
		SaferTerminateProcess(pinfo.hProcess);

		// Notify the callback of the failure
		onError(MsgFmt(_T("Child process %s not responding; terminating"), exe));
		return false;
	}

	// success
	return true;
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
	auto callback = [](HMODULE hModule, LPCTSTR lpType, LPWSTR lpName, LONG_PTR lParam) -> BOOL
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

