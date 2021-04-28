// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <ShlObj.h>
#include <Shlwapi.h>
#include <string.h>
#include <vector>
#include <stdio.h>
#include <ios>
#include <fstream>
#include <memory>
#include "FileUtil.h"
#include "UtilResource.h"
#include "WinUtil.h"

// Touch a file
bool TouchFile(const TCHAR *filename)
{
	// presume failure
	bool ok = false;

	// open the file to write attributes
	HandleHolder hFile = CreateFile(filename, FILE_WRITE_ATTRIBUTES, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile != NULL && hFile != INVALID_HANDLE_VALUE)
	{
		// get the current time, as a FILETIME value
		SYSTEMTIME st;
		FILETIME ft;
		GetSystemTime(&st);
		SystemTimeToFileTime(&st, &ft);

		// set the last write time
		if (SetFileTime(hFile, NULL, NULL, &ft))
			ok = true;
	}

	// return the result
	return ok;
}

// Create a subdirectory, including intermediate directories as needed.
BOOL CreateSubDirectory(
	const TCHAR *fullPathToCreate,
	const TCHAR *fullParentPath,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
	// if the current path exists, we're done
	if (DirectoryExists(fullPathToCreate))
		return TRUE;

	// if the path matches the parent path, fail
	if (fullParentPath != nullptr && _tcsicmp(fullPathToCreate, fullParentPath) == 0)
		return FALSE;

	// make a copy of the target path
	size_t len = _tcslen(fullPathToCreate) + 1;
	std::unique_ptr<TCHAR> p(new TCHAR[len]);
	_tcscpy_s(p.get(), len, fullPathToCreate);

	// strip the last folder element
	PathRemoveFileSpec(p.get());

	// If that's empty or the same as the parent, stop.  When we reach
	// the root folder, removing the last path element will have no
	// effect.  Assuming the target folder is valid, the root folder
	// will exist, so the recursion will have already ended when we
	// tested for the current path's existence above.  And if a parent
	// path was specified, we'll stop when we reach it.  But if no
	// parent was specified (so we're supposed to create all folders
	// back to the root folder, if needed), AND the volume is invalid,
	// we'll never find an existing folder and so we'll never stop on
	// the folder existence condition.  So we could get stuck trying
	// to create an invalid root folder without this extra test.
	// Note that the result is FALSE (failed) in this case, because
	// we reached a topmost folder that doesn't exist and that we're
	// not going to attempt to create (because that would fail).
	if (p.get()[0] == 0 || _tcscmp(p.get(), fullPathToCreate) == 0)
		return FALSE;

	// create that element; if that works, create the child folder
	return CreateSubDirectory(p.get(), fullParentPath, lpSecurityAttributes)
		&& CreateDirectory(fullPathToCreate, lpSecurityAttributes);
}

// Read the entire contents of a file as a byte array
BYTE *ReadFileAsStr(const TCHAR *filename, class ErrorHandler &handler, long &len, UINT flags)
{
	// open the file and seek to the end (to get the size)
	FILEPtrHolder fp;
	int err;
	if ((err = _tfopen_s(&fp, filename, _T("rb"))) != 0)
	{
		handler.Error(MsgFmt(IDS_ERR_OPENFILE, filename, FileErrorMessage(err).c_str()));
		return nullptr;
	}

	// get the size by seeking to the end and 'tell'ing
	fseek(fp, 0, SEEK_END);
	long fileLen = ftell(fp);

	// seek back to the beginning
	fseek(fp, 0, SEEK_SET);

	// add extra space to the allocation for special termination
	long aloLen = fileLen;
	if (flags & ReadFileAsStr_NewlineTerm)
		++aloLen;
	if (flags & ReadFileAsStr_NullTerm) 
		++aloLen;

	// allocate the buffer
	BYTE *buf = new (std::nothrow) BYTE[aloLen];
	if (buf == nullptr)
	{
		handler.Error(MsgFmt(IDS_ERR_OPENFILENOMEM,	filename, fileLen));
		return nullptr;
	}

	// read the data into the buffer
	bool ok = (fread(buf, 1, fileLen, fp) == fileLen);

	// check for error
	if (!ok)
	{
		handler.Error(MsgFmt(IDS_ERR_READFILE, filename, FileErrorMessage(errno).c_str()));
		delete[] buf;
		return nullptr;
	}

	// add special termination
	if (flags & ReadFileAsStr_NewlineTerm)
		buf[fileLen++] = '\n';
	if (flags & ReadFileAsStr_NullTerm)
		buf[fileLen++] = '\0';

	// pass the final adjusted data length back to the caller
	len = fileLen;

	// success
	return buf;
}

// -----------------------------------------------------------------------
//
// Read the entire contents of a file as a wide character array.
//
wchar_t *ReadFileAsWStr(const TCHAR *filename, class ErrorHandler &handler, 
	long &lengthInChars, UINT flags, UINT defaultMultibyteCodePage)
{
	// open the file and seek to the end (to get the size)
	FILE *fp;
	int err;
	if ((err = _tfopen_s(&fp, filename, _T("rb"))) != 0)
	{
		handler.Error(MsgFmt(IDS_ERR_OPENFILE, filename, FileErrorMessage(err).c_str()));
		return nullptr;
	}

	// get the size by seeking to the end and 'tell'ing
	fseek(fp, 0, SEEK_END);
	long fileLen = ftell(fp);

	// seek back to the beginning
	fseek(fp, 0, SEEK_SET);

	// add extra space to the allocation for special termination
	int nExtraChars = 0;
	if (flags & ReadFileAsStr_NewlineTerm)
		++nExtraChars;
	if (flags & ReadFileAsStr_NullTerm)
		++nExtraChars;

	// Allocate the buffer.  Allocate space for the actual file bytes, plus
	// space for the extra termination characters specified in the flags.
	BYTE *buf = new (std::nothrow) BYTE[fileLen + (nExtraChars * sizeof(WCHAR))];
	if (buf == 0)
	{
		handler.Error(MsgFmt(IDS_ERR_OPENFILENOMEM, filename, fileLen));
		return nullptr;
	}

	// read the data into the buffer
	bool ok = (fread(buf, 1, fileLen, fp) == fileLen);

	// done with the file
	fclose(fp);

	// check for errors
	if (!ok)
	{
		handler.Error(MsgFmt(IDS_ERR_READFILE, filename, FileErrorMessage(errno).c_str()));
		delete[] buf;
		return nullptr;
	}

	// translate from multibyte to wide characters
	auto mbToWide = [&handler, &filename, &fileLen, &lengthInChars, &buf, nExtraChars](UINT codePage, int prefixByteLen)
	{
		// deduct the prefix from the file length
		fileLen -= prefixByteLen;

		// figure the length in wide characters
		int nWideChars = MultiByteToWideChar(codePage, 0, (const char *)buf + prefixByteLen, fileLen, 0, 0);

		// allocate space
		wchar_t *wbuf = new (std::nothrow) wchar_t[nWideChars + nExtraChars];
		if (wbuf == 0)
		{
			handler.Error(MsgFmt(IDS_ERR_OPENFILENOMEM, filename, nWideChars * sizeof(wchar_t)));
			delete[] buf;
			return false;
		}

		// translate
		MultiByteToWideChar(codePage, 0, (const char *)buf + prefixByteLen, fileLen, wbuf, nWideChars);

		// switch to the new buffer
		delete[] buf;
		buf = (BYTE *)wbuf;

		// set the file length in characters
		lengthInChars = nWideChars;

		// success 
		return true;
	};

	// check for byte-order markers
	if (fileLen >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
	{
		// UTF8 - convert to wide characters
		if (!mbToWide(CP_UTF8, 3))
			return nullptr;
	}
	else if (fileLen >= 2 && buf[0] == 0xFF && (buf[1] == 0xFE || buf[1] == 0xFD))
	{
		// UTF16 Little Endian.  This is the native Windows format, so all we
		// need to do is discard the first two bytes.
		fileLen -= 2;
		memmove(buf, buf + 2, fileLen);

		// each character is two bytes
		lengthInChars = fileLen / 2;
	}
	else if (fileLen >= 2 && buf[1] == 0xFF && (buf[0] == 0xFE || buf[0] == 0xFD))
	{
		// UTF16 Big Endian.  This is the same as the Windows format except
		// that each byte pair is swapped.  Go through the buffer and swap
		// the byte pairs, skipping the first two order-marker bytes.
		BYTE *p = buf;
		fileLen -= 2;
		for (long n = fileLen ; n > 0; n -= 2, p += 2)
		{
			p[0] = p[3];
			p[1] = p[2];
		}

		// each character is two bytes
		lengthInChars = fileLen / 2;
	}
	else if (fileLen >= 4 && buf[0] == 0xFF && buf[1] == 0xFE && buf[2] == 0x00 && buf[3] == 0x00)
	{
		// UTF32 Little Endian - not implemented
		handler.Error(MsgFmt(IDS_ERR_FILECHARSET, filename, _T("UTF-32LE")));
		delete[] buf;
		return nullptr;
	}
	else if (fileLen >= 4 && buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0xFE && buf[3] == 0xFF)
	{
		// UTF32 Big Endian - not implemented
		handler.Error(MsgFmt(IDS_ERR_FILECHARSET, filename, _T("(UTF-32BE)")));
		delete[] buf;
		return nullptr;
	}
	else
	{
		// No Unicode byte order marker found, so take this to be an 
		// ordinary multibyte file, interpreting characters in the code
		// page specified by the caller.
		if (!mbToWide(defaultMultibyteCodePage, 0))
			return nullptr;
	}

	// the buffer is now in wide character format
	wchar_t *wbuf = (wchar_t *)buf;

	// add special termination
	if (flags & ReadFileAsStr_NewlineTerm)
		wbuf[lengthInChars++] = '\n';
	if (flags & ReadFileAsStr_NullTerm)
		wbuf[lengthInChars++] = '\0';

	// success
	return wbuf;
}

// -----------------------------------------------------------------------
//
// Get a module file name
//
DWORD SafeGetModuleFileName(HMODULE hModule, TCHAR *buf, DWORD buflen)
{
	// start with MAX_PATH and work from there
	for (DWORD curlen = MAX_PATH; curlen <= MAX_PATH*256; curlen *= 2)
	{
		// try it with the current buffer size
		std::unique_ptr<TCHAR> curbuf(new TCHAR[curlen]);
		DWORD result = GetModuleFileName(hModule, curbuf.get(), curlen);

		// If the return code was zero, an error occurred
		if (result == 0)
			return 0;

		// If the result is exactly 'len', it means that the buffer was
		// too short, so we have to try again.  If it's non-zero and less
		// than 'len', though, we have a winner.
		if (result < curlen)
		{
			// If the result will fit in the caller's buffer, just copy
			// it exactly as it is.
			if (result < buflen)
			{
				_tcscpy_s(buf, buflen, curbuf.get());
				return result;
			}

			// It's too big for the caller's buffer.  Try translating it
			// to a short path, and return the result.
			return GetShortPathName(curbuf.get(), buf, buflen);
		}
	}

	// we couldn't find a buffer big enough - fail
	return 0;
}

// -----------------------------------------------------------------------
//
// Get the executable file path
//
DWORD GetExeFilePath(TCHAR *buf, DWORD buflen)
{
	// get the full EXE file name
	DWORD result = SafeGetModuleFileName(NULL, buf, buflen);

	// check for error
	if (result == 0 || result == buflen)
		return result;

	// strip the filename
	PathRemoveFileSpec(buf);

	// return the result length
	return (DWORD)_tcslen(buf);
}

// stringize a macro
#define STRINGIZEX(s) _T(#s)
#define STRINGIZE(s) STRINGIZEX(s)

// -----------------------------------------------------------------------
//
// Get a deployed file path
//
void GetDeployedFilePath(TCHAR *result, const TCHAR *relFilePath, const TCHAR *devPath)
{
	// folder path containing the executable
	static TCHAR exePath[MAX_PATH];

	// Development build $(Solution) directory.  This contains a non-empty
	// path if we're running in dev mode.
	static TCHAR solDir[MAX_PATH];

	// Initialize if this is the first time through
	static bool inited = false;
	if (!inited)
	{
		// Get the executable path
		DWORD ret = GetExeFilePath(exePath, countof(exePath));
		if (ret == 0 || ret == countof(exePath))
		{
			// use the working directory as a last result
			_tcscpy_s(exePath, _T("."));
		}

		// Check for the development environment marker file
		TCHAR marker[MAX_PATH];
		PathCombine(marker, exePath, _T(".DevEnvironment"));
		if (FileExists(marker))
		{
			// it exists - the contents give the $(Solution) folder path
			FILE *fp;
			if (_tfopen_s(&fp, marker, _T("r")) == 0)
			{
				// read the first line into the solution path
				if (_fgetts(solDir, countof(solDir), fp) == 0)
					solDir[0] = 0;

				// strip trailing newlines and whitespace
				size_t len = _tcslen(solDir);
				while (len > 0 && (solDir[len - 1] == '\n' || solDir[len-1] == ' ' || solDir[len-1] == '\t'))
					solDir[--len] = 0;

				// we're done with the file
				fclose(fp);
			}
		}

		// we've completed initialization
		inited = true;
	}

	// If there's a solution path, we're in dev mode.  Otherwise we're
	// in deployment mode.
	if (solDir[0] != 0)
	{
		// Dev mode - combine the solution path, the dev tree path
		// (if there is one), and the file path.  If there's no dev
		// path, just combine the solution dir with the file path.
		if (devPath != nullptr && devPath[0] != 0)
		{
			// There's a dev path.  Check for substitution variables.
			if (_tcsstr(devPath, _T("$(")) != nullptr)
			{
				// Substitution variables are present.  Apply the 
				// substitutions.
				TSTRING tmp = regex_replace(TSTRING(devPath), std::basic_regex<TCHAR>(_T("\\$\\((\\w+)\\)")), 
					[](const std::match_results<TSTRING::const_iterator> &m) -> TSTRING
				{
					if (m[1].str() == _T("SolutionDir"))
						return solDir;
					else if (m[1].str() == _T("Configuration"))
						return STRINGIZE(BUILD_CONFIGURATION);
					else if (m[1].str() == _T("Bits"))
						return IF_32_64(_T("32"), _T("64"));
					else if (m[1].str() == _T("32"))
						return IF_32_64(_T("32"), _T(""));
					else if (m[1].str() == _T("64"))
						return IF_32_64(_T(""), _T("64"));
					else if (m[1].str() == _T("Platform"))
						return IF_32_64(_T("x86"), _T("x64"));
					else
						return m[0].str();
				});
				_tcscpy_s(result, MAX_PATH, tmp.c_str());
			}
			else
			{
				// No substutition variables.  Simply combine the paths:
				// solution dir + dev path + relative file path
				TCHAR tmp[MAX_PATH];
				PathCombine(tmp, solDir, devPath);
				PathCombine(result, tmp, relFilePath);
			}
		}
		else
		{
			// no dev path - combine solution + file
			PathCombine(result, solDir, relFilePath);
		}
	}
	else
	{
		// Deployment mode - combine the program folder and the file path
		PathCombine(result, exePath, relFilePath);
	}
}

//------------------------------------------------------------------------
//
// Search for a file matching the given root name using the provided
// list of extensions.  On entry, fname[] is set up with the root
// name.  We'll add each extension in turn until we find one that
// gives us the name of an extant file.  If we find such a file,
// we'll return true, with the full name in fname[].  If we don't
// find any files matching any of the possible names, we'll return
// false.
bool FindFileUsingExtensions(TCHAR fname[MAX_PATH], const TCHAR* const exts[], size_t nExts)
{
	// start with the root name
	size_t rootLen = _tcslen(fname);
	for (size_t i = 0; i < nExts; ++i)
	{
		const TCHAR *ext = exts[i];
		size_t extLen = _tcslen(ext);
		if (extLen + rootLen + 1 < MAX_PATH)
		{
			memcpy(fname + rootLen, ext, (extLen + 1) * sizeof(TCHAR));
			if (FileExists(fname))
				return true;
		}
	}

	// not found
	return false;
}

