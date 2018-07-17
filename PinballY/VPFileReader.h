// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

class VPFileReader
{
public:
	VPFileReader();
	~VPFileReader();

	HRESULT Read(const WCHAR *filename, bool getScript);

	// Version loaded from the file
	INT32 fileVersion;

	// Embedded table metadata
	std::unique_ptr<WCHAR> tableName;
	std::unique_ptr<WCHAR> tableVersion;
	std::unique_ptr<WCHAR> releaseDate;
	std::unique_ptr<WCHAR> authorName;
	std::unique_ptr<WCHAR> authorEmail;
	std::unique_ptr<WCHAR> authorWebSite;
	std::unique_ptr<WCHAR> blurb;
	std::unique_ptr<WCHAR> description;
	std::unique_ptr<WCHAR> rules;

	// Script
	std::unique_ptr<CHAR> script;

	// File protection
	struct FileProtection
	{
		static const UINT32 DISABLE_TABLE_SAVE = 0x00000001;
		static const UINT32 DISABLE_SCRIPT_EDITING = 0x00000002;
		static const UINT32 DISABLE_OPEN_MANAGERS = 0x00000004;
		static const UINT32 DISABLE_CUTCOPYPASTE = 0x00000008;
		static const UINT32 DISABLE_TABLEVIEW = 0x00000010;
		static const UINT32 DISABLE_TABLE_SAVEPROT = 0x00000020;
		static const UINT32 DISABLE_DEBUGGER = 0x00000040;
		static const UINT32 DISABLE_EVERYTHING = 0x80000000;

		static const int PASSWORD_LENGTH = 16;
		static const int CIPHER_LENGTH = PASSWORD_LENGTH + 8;

		UINT32  fileversion;
		UINT32  size;
		BYTE    paraphrase[CIPHER_LENGTH];
		UINT32  flags;
		INT32   keyversion;
		INT32   reserved[2];
	} protection;

};
