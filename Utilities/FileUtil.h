// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// File Utilities

#pragma once
#include <Windows.h>
#include <string>
#include <varargs.h>
#include <vector>
#include <iterator>

// -----------------------------------------------------------------------
//
// Does a file exist?
//
template<typename CHARTYPE>
bool FileExists(const CHARTYPE *filename)
{
	DWORD dwAttrib = 0;
	if constexpr (std::is_same<CHARTYPE, CHAR>::value)
		dwAttrib = GetFileAttributesA(filename);
	else if constexpr (std::is_same<CHARTYPE, WCHAR>::value)
		dwAttrib = GetFileAttributesW(filename);

	return (dwAttrib != INVALID_FILE_ATTRIBUTES
		&& !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

//
// Does a directory exist?
//
inline bool DirectoryExists(const TCHAR *filename)
{
	DWORD dwAttrib = GetFileAttributes(filename);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES
		&& (dwAttrib & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

// -----------------------------------------------------------------------
// 
// "Touch" a file - set the modified date to the current time
//
bool TouchFile(const TCHAR *filename);

// -----------------------------------------------------------------------
//
// Create a subdirectory, including all intermediate directories
// as needed, but stopping at the given parent.  If the parent
// is null, we'll create the entire path.
//
// For example, CreateSubDirectory("x:\\a\\b\\c", "x:\\a", 0)
// creates x:\a\b and x:\a\b\c as needed, but doesn't create x:\a
// if it doesn't exist.
BOOL CreateSubDirectory(
	const TCHAR *fullPathToCreate,
	const TCHAR *fullParentPath,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes);

// -----------------------------------------------------------------------
//
// FILE* holder.  This ensures that the FILE* is fclose()'d when
// the holder goes out of scope.
//
struct FILEPtrHolder 
{
	FILEPtrHolder() : fp(nullptr) { }
	FILEPtrHolder(FILE *fp) : fp(fp) { }

	~FILEPtrHolder() { fclose(); }

	int fclose()
	{
		int ret = 0;
		if (fp != nullptr)
		{
			ret = ::fclose(fp);
			fp = nullptr;
		}
		return ret;
	}

	// release the FILE* to a new owner
	FILE *release()
	{
		FILE *ret = fp;
		fp = nullptr;
		return ret;
	}

	// the underlying FILE*
	FILE *fp;

	// for convenience, the struct can be used as though it were a FILE* 
	operator FILE* () { return fp; }
	FILE** operator& () { return &fp; }
};


// -----------------------------------------------------------------------
//
// Read a file into a newly allocated byte array.
//
// The file is read in binary mode, so DOS CR-LF (\r\n) newline 
// sequences will be passed through literally.  The caller should 
// treat each \r\n pair as a single newline if interpreting the data 
// as plain text.  (The easiest way to do this is just to treat both 
// \r and \n individually as whitespace, but some callers might want 
// to keep a line counter, or might treat blank lines as meaningful, 
// in which case it's important to recognize the \r\n pairs as 
// units.)
//
// On success, we return a newly allocated byte array, and fills in
// 'len' with the length of the file in bytes.  The caller is 
// responsible for freeing the returned buffer when done via 
// delete[].  We return null on failure.
BYTE *ReadFileAsStr(
	const TCHAR *filename, 
	class ErrorHandler &handler, 
	long &len,
	UINT flags);

// Read as wide string.  This checks for byte order markers and
// converts as needed.  
//
// If no byte order markers are found, we take the file to use 
// multibyte characters in the default code page specified.  The 
// default is CP_ACP, the local default Windows ANSI code page, 
// which depends on the localization settings on the current 
// machine.  This is appropriate for user-supplied content, as
// whatever other programs the user used to create the content
// will generally also work in terms of the localized default
// code page.  For our own content that we include in the program
// distribution, use the known code page for the pre-generated
// content.  In most cases, this should be 1252 (Windows Latin 1,
// aka "Western Europe", the default code page used in US and 
// Western European locales).
wchar_t *ReadFileAsWStr(
	const TCHAR *filename,
	class ErrorHandler &handler,
	long &characterLength,
	UINT flags,
	UINT defaultMultibyteCodePage = CP_ACP);

// flag: add newline termination to ReadFileAsStr result
#define ReadFileAsStr_NewlineTerm   0x0001

// flag: add null terminate to ReadFileAsStr result
#define ReadFileAsStr_NullTerm      0x0002

// Simple reader class
class BinaryReader
{
public:
	BinaryReader() : buf(nullptr), p(nullptr), buflen(0) { }
	~BinaryReader() { delete[] buf; }

	bool Load(const TCHAR *filename, ErrorHandler &handler)
	{
		p = buf = ReadFileAsStr(filename, handler, buflen, 0);
		return buf != 0;
	}

	template<class T> bool Read(T &val)
	{
		// make sure we have enough data left to read the value
		long rem = buflen - (long)(p - buf);
		if (rem < sizeof(val))
			return false;

		// copy the value 
		memcpy(&val, p, sizeof(val));

		// advance the read pointer
		p += sizeof(val);

		// success
		return true;
	}

	template<class T> bool Read(T *val, int cnt)
	{
		for (int i = 0; i < cnt; ++i)
		{
			if (!Read(val[i]))
				return false;
		}
		return true;
	}

protected:
	BYTE *buf;
	long buflen;
	BYTE *p;
};


// -----------------------------------------------------------------------
// 
// Safe version of GetModuleFileName() (safer, at least).  The standard
// Windows GetModuleFileName() API has a peculiar interface, in that it
// requires the caller to allocate a buffer, but doesn't provide any
// directly way to query the amount of space needed.  Windows has a
// pretty uniform convention for this type of API, where the API tells
// you the amount of space required if the provided buffer is too small.
// This API doesn't do that; it just tells you whether or not the buffer
// was big enough, forcing the caller to play 20 Questions with the API
// by passing in successively bigger buffers until you guess right.
// But that's not the only peculiarity.  Most of the APIs that return
// file system paths limit themselves to MAX_PATH results, so you can
// just pass in a MAX_PATH buffer and rest assured that the result will
// fit, assuming it's valid at all.
//
// This safer version of the interface does two things to help.  First,
// it plays the 20 Questions game so that you don't have to.  It queries
// the path with an internally allocated buffer, expanding the buffer on
// each "insufficient buffer" error result until it's either big enough
// or the buffer gets unreasonably large.  (Which we define as 33280 
// characters, or MAX_PATH*256, because the Windows API docs say that 
// real results can never be bigger than "slightly over" 32K characters.
// "Slightly over" is another peculiarity - even the Microsoft people
// seem fuzzy on the details of this one.)  Second, if the result from
// GetModuleFileName() does turn out to exceed the provided buffer
// space, we'll try to translate it to a "short path" equivalent using 
// GetShortPathName().
//
// It *should* be safe to pass in a buffer of length MAX_PATH, since
// the whole point of GetShortPathName() is to get a path that's
// compatible with the double-legacy APIs - that is, we're not not 
// just talking about the APIs that can only handle MAX_PATH paths, 
// which are "legacy" from the perspective of the extremely long "\\?\"
// filename rules; we're talking about the *really* old APIs that were
// contrained to 8.3 filenames.
//
// On success, the return value is the length in characters of the 
// resulting path, excluding the terminating null character.  This 
// will be non-zero and less than 'buflen'.  On error, the return value
// is zero.  A return of exactly 'buflen' means that a long path came
// back from GetModuleFileName(), and the provided buffer was
// insufficient for GetShortPathName().  This should be unlikely in
// typical setups, but the API docs leave open the possibility that 
// it can happen in situations where the underlying file system doesn't
// support short paths, such as some types of network shares.
//
DWORD SafeGetModuleFileName(HMODULE hModule, TCHAR *buf, DWORD buflen);


// -----------------------------------------------------------------------
//
// Get the full path to the currently running executable, stripped of 
// the file name.  On success, returns the length of the path, excluding
// the terminating null.  If the result is too large for the buffer,
// returns 'buflen'.  Returns zero on error.
//
DWORD GetExeFilePath(TCHAR *buf, DWORD buflen);

// -----------------------------------------------------------------------
//
// Deployment path lookup:  get a file path relative to the deployment
// directory.  This finds a file that's part of the release set and is
// thus installed in the program folder.
//
// The result is returned in the provided buffer 'result', which must
// be at least MAX_PATH characters long.
//
// 'relativeFilename' is the relative filename of the path.  In a full
// deployment, this is relative to the root program folder.
//
// 'devSysPath' is an additional path fragment that's used only if we're
// running in the build environment.  This is a path relative to the 
// Visual Studio "Solution" folder - the root of the folder tree
// containing the .sln file for the overall project.
//
// If we're running in an end-user deployment environment, the result is
// the absolute path to the program folder (where the running .EXE is
// located) appended with the relative filename path.  If we're running
// in a build environment, the result is the Visual Studion $(Solution)
// folder, appended with the 'devSysPath' value, appended with the
// relative filename value.
//
// This routine has two main purposes.  The first is to make access to
// deployed files independent of the current working directory setting.
// Windows normally sets the working directory to the folder containing
// the program's EXE file at launch, but this isn't guaranteed: users
// can override it when launching from the desktop via a shortcut; we
// can be launched by another program, which can specify the initial
// working directory when creating the process; and we can change it
// ourselves in the course of our work.  We therefore shouldn't assume
// that a particular working directory is in effect at any given time.
// This routine allows a caller to get the full path to a deployed file
// without having to check the current working path setting.
//
// The second purpose is to allow for differences in the directory
// structures of the development and deployment environments.  A full
// deployment contains the program executables, along with various data
// files.  Some of the data files are derived mechanically during the
// build process, and others are simply copied from the source tree.  In
// a developer setup, though, we might want not want to create a full
// replica of the deployment environment on every build, since doing so
// would cost time and disk space to replicate files that already exist
// in other parts of the project directory structure.  The extra copying
// is especially redundant because a developer will typically need to
// build and test several platform/configuration combinations.
//
// To deal with these directory layout differences, this function looks
// for a file called .DevEnvironment in the folder containing the .EXE.
// This file must be mechanically generated by each executable during
// the build process, and (obviously) it should NOT be included in the
// file set for an end-user deployment.  The file, if present, contains
// the Visual Studio $(SolutionDir) value for the project, which we use
// to get the root solution path.  We then combine the solution path,
// the dev path argument, and the relative file path argument to yield
// the final path name.
//
// For example, suppose that we deploy some help files in
// <program folder>\Help, but in the development system, we generate 
// them in $(SolutionDir)\Project\Derived\Help.  You'd pass
// _T("Help\\Filename") as the relative filename path, and 
// _T("Project\\Derived") as the dev path.  The dev path would only
// be used if we determine that we're running in the folder where the
// program was last actually built.
//
// 
// Substitution variables:  devSysPath can be specified as a complete
// path instead of an add-on path, by using one or more substitution
// variables within the path.  If any substitution variables are used,
// when we're running under the build system, relativeFilename is
// completely ignored and devSysPath is used as the entire name.
//
//  $(SolutionDir)   -> solution folder, absolute fully qualified path
//  $(Configuration) -> Debug, Release
//  $(Bits)          -> "32" or "64", per build configuration
//  $(32)            -> "32" for a 32-bit build, empty otherwise
//  $(64)            -> "64" for a 64-bit build, empty otherwise
//  $(Platform)      -> "x86" for 32-bit build, "x64" for 64-bit build
//
void GetDeployedFilePath(
	TCHAR *result /* must be >= MAX_PATH characters long */, 
	const TCHAR *relativeFilename, const TCHAR *devSysPath);

// Search for a file matching the given root name using the provided
// list of extensions.  On entry, fname[] is set up with the root
// name.  We'll add each extension in turn until we find one that
// gives us the name of an extant file.  If we find such a file,
// we'll return true, with the full name in fname[].  If we don't
// find any files matching any of the possible names, we'll return
// false.
bool FindFileUsingExtensions(TCHAR fname[MAX_PATH], const TCHAR* const exts[], size_t nExts);

