// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Visual Pinball file reader
//
// This parses a VP file (.vpt or .vpx) to retrieve the embedded "Table 
// Information" metadata (table name, author, etc) that can be entered
// in the VP editor.  Not all authors bother to provide the metadata,
// but many do, so this can be helpful in identifying a table when the
// filename doesn't make it clear.
//
// We can optionally also retrieve the table's script text.  The script
// text can be useful for recovering other information about the table,
// such as the internal game ID it passes to the controller.  (The ID
// is the name of the ROM when VPinMAME is used, or a DOF config ID
// for non-ROM tables.)
//
// VP uses OLE Structured Storage as its main wrapper format, and uses
// a bunch of ad hoc formats within the Structured Storage streams.
// There's no particular rhyme or reason to the various formats; you 
// just have to look at the code to see what it's doing.  For the Table
// Information metadata, these are simply a bunch of strings that are
// each stored in a particular named stream within a particular named
// storage.  (A "storage" is analgous to a directory, and a "stream" is
// analgous to a file; a Structured Storage is basically a mini file
// system within a file.)  The script text is all stored in a single
// contiguous byte block within the "table data" stream, which is a
// monolithic stream containing a series of (essentially) FOURCC 
// chunks.  Fortunately, the FOURCC format is self-describing enough
// that we can scan through it without having to actually understand
// or parse any of the items we don't care about; we can scan through 
// the stream looking for the couple of chunks we want to extract, and
// just skip the rest.
//
// Despite the complexity of the format, it's actually pretty fast to
// scan through it if you only want to extra specific items.  On my
// development machine, a typical table read (with script, even!)
// only takes about 5ms.  This is fast enough to do on-demand in UI
// code.
//

#include "stdafx.h"
#include "VPFileReader.h"
#include "../Utilities/WinCryptUtil.h"

// File "tag" maker macro.  A tag is a four-character code
// packed into four bytes in the FOURCC fashion.  'TAG(ABCD)'
// gets this in UINT32 format for simple comparisons.  (This
// way of packing the code also makes it 'switch'-compatible
// for efficient lookup.)
#define TAG(X) ((UINT32)((#X[0])|(#X[1]<<8)|(#X[2]<<16)|(#X[3]<<24)))

VPFileReader::VPFileReader() :
	fileVersion(0)
{
	ZeroMemory(&protection, sizeof(protection));
}

VPFileReader::~VPFileReader()
{
}

HRESULT VPFileReader::Read(const WCHAR *filename, bool getScript)
{
	// make sure we have a non-null filename
	if (filename == nullptr)
		return E_POINTER;

	// VP's underlying raw storage format is OLE Structured Storage.  Open 
	// the file as an IStorage.
	RefPtr<IStorage> stg;
	HRESULT hr = StgOpenStorage(filename, NULL, STGM_TRANSACTED | STGM_READ | STGM_SHARE_EXCLUSIVE, NULL, 0, &stg);
	if (FAILED(hr))
		return hr;

	// Set up a cryptography context, in case we need to decrypt a locked file
	HCRYPTPROVHolder hcp;
	if (!CryptAcquireContext(&hcp, NULL, NULL, PROV_RSA_FULL,
		CRYPT_VERIFYCONTEXT | CRYPT_NEWKEYSET | CRYPT_SILENT))
		return HRESULT_FROM_WIN32(GetLastError());

	// Initialize an MD5 hasher for the password decryption
	static const BYTE HASH_INIT_VECTOR[] = "Visual Pinball";
	HCRYPTHASHHolder hchkey;
	if (!CryptCreateHash(hcp, CALG_MD5, NULL, 0, &hchkey)
		|| !CryptHashData(hchkey, HASH_INIT_VECTOR, 14, 0))
		return HRESULT_FROM_WIN32(GetLastError());

	// Read the Table Info stream
	RefPtr<IStorage> infoStg;
	if (SUCCEEDED(hr = stg->OpenStorage(L"TableInfo", NULL, STGM_DIRECT | STGM_READ | STGM_SHARE_EXCLUSIVE, NULL, 0, &infoStg)))
	{
		auto ReadValue = [&infoStg](const WCHAR *name, std::unique_ptr<WCHAR> &value)
		{
			// open the stream by name
			RefPtr<IStream> stream;
			HRESULT hr = infoStg->OpenStream(name, NULL, STGM_DIRECT | STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &stream);
			if (FAILED(hr))
				return hr;

			// get the content size
			STATSTG ss;
			stream->Stat(&ss, STATFLAG_NONAME);
			DWORD byteLen = ss.cbSize.LowPart;
			DWORD charLen = byteLen / sizeof(WCHAR);

			// allocate a buffer
			value.reset(new WCHAR[charLen + 1]);

			// read it
			ULONG read;
			if (FAILED(hr = stream->Read(value.get(), byteLen, &read)))
				return hr;

			// null-terminate it
			value.get()[charLen] = 0;

			// success
			return S_OK;
		};

		// Read the values
		ReadValue(L"TableName", tableName);
		ReadValue(L"TableVersion", tableVersion);
		ReadValue(L"ReleaseDate", releaseDate);
		ReadValue(L"AuthorName", authorName);
		ReadValue(L"AuthorEmail", authorEmail);
		ReadValue(L"AuthorWebSite", authorWebSite);
		ReadValue(L"TableBlurb", blurb);
		ReadValue(L"TableDescription", description);
		ReadValue(L"Rules", rules);
	}

	// open the main "Game" substorage
	RefPtr<IStorage> dataStg;
	if (FAILED(hr = stg->OpenStorage(L"GameStg", NULL, STGM_DIRECT | STGM_READ | STGM_SHARE_EXCLUSIVE, NULL, 0, &dataStg)))
		return hr;

	// open the Version stream
	RefPtr<IStream> versionStream;
	HCRYPTKEYHolder hPasswordKey;
	if (SUCCEEDED(hr = dataStg->OpenStream(L"Version", NULL, STGM_DIRECT | STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &versionStream)))
	{
		// read the version data
		ULONG read;
		if (SUCCEEDED(hr = versionStream->Read(&fileVersion, sizeof(fileVersion), &read)))
		{
			// Initialize the password decryption key according to the file version
			CryptDeriveKey(hcp, CALG_RC2, hchkey,
				(fileVersion == 600) ? CRYPT_EXPORTABLE : (CRYPT_EXPORTABLE | 0x00280000),
				&hPasswordKey);
		}
	}

	// open the Game Data stream
	RefPtr<IStream> gameStream;
	if (FAILED(hr = dataStg->OpenStream(L"GameData", NULL, STGM_DIRECT | STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &gameStream)))
		return hr;

	// if we don't need any of the data items, we're done
	if (!getScript)
		return S_OK;

	// read records
	for (bool done = false; !done; )
	{
		// read the record size and tag
		INT32 recLen;
		INT32 tag;
		ULONG read;
		if (FAILED(hr = gameStream->Read(&recLen, sizeof(recLen), &read))
			|| FAILED(hr = gameStream->Read(&tag, sizeof(tag), &read)))
			return hr;

		// the nominal record length includes the FOURCC tag, so deduct
		// that from the remaining data, as we've read it now
		recLen -= 4;

		// check for tags we're interested in
		switch (tag)
		{
		case TAG(CODE):
			// CODE is just an empty tag not stored in the usual format;
			// a size prefix comes next, then the text.  Read the size.
			if (FAILED(hr = gameStream->Read(&recLen, sizeof(recLen), &read)))
				return hr;

			// allocate space
			script.reset(new CHAR[recLen + 1]);

			// read the data
			if (FAILED(hr = gameStream->Read(script.get(), recLen, &read)))
				return hr;

			// null-terminate it
			script.get()[recLen] = 0;

			// if necessary, decrypt the script
			if ((protection.flags & (FileProtection::DISABLE_EVERYTHING | FileProtection::DISABLE_SCRIPT_EDITING)) != 0)
			{
				DWORD cryptLen = recLen;
				CryptDecrypt(hPasswordKey, NULL, TRUE, 0, (BYTE*)script.get(), &cryptLen);
			}

			// done
			break;

		case TAG(SECB):
			// security data
			if (FAILED(hr = gameStream->Read(&protection, sizeof(protection), &read)))
				return hr;
			break;

		case TAG(ENDB):
			done = true;
			break;

		default:
			// skip the record
			{
				LARGE_INTEGER skip;
				skip.HighPart = 0;
				skip.LowPart = recLen;
				if (FAILED(hr = gameStream->Seek(skip, STREAM_SEEK_CUR, NULL)))
					return hr;
			}
			break;
		}
	}

	// success
	return S_OK;
}
