// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <winsafer.h>
#include <Shlwapi.h>
#include <ShlObj.h>
#include <commdlg.h>
#include <Dlgs.h>
#include <math.h>
#include "../rapidxml/rapidxml.hpp"
#include "Util.h"
#include "UtilResource.h"
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

