// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// 7-Zip interface

#pragma once

class ErrorHandler;
struct IInArchive;

class SevenZipArchive
{
public:
	SevenZipArchive();
	~SevenZipArchive();

	// open an archive file
	bool OpenArchive(const TCHAR *fname, ErrorHandler &eh);

	// enumerate the files in the archive
	bool EnumFiles(std::function<void(UINT32 idx, const WCHAR *path, bool isDir)> func);

	// extract the file at the given index
	bool Extract(UINT32 idx, const TCHAR *destFile, ErrorHandler &eh);

protected:
	// 7z.dll archive reader object
	RefPtr<IInArchive> archive;

	// filename
	TSTRING filename;
};

