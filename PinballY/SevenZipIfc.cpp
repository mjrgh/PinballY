// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Simple 7-Zip interface for reading archives.
//
// Important note on COM pointers:  7-Zip doesn't follow the normal
// COM convention for initializing the reference count in a newly
// created object.  The standard convention, which we follow in our
// RefPtr<> and related templates, is that a newly created object sets
// its own reference count to 1 in its constructor, on behalf of the
// caller.  7-Zip's convention is that the CALLER is responsible for
// adding the initial reference, so a constructor returns with the
// reference count set to zero.  The inconsistency with the standard
// COM convention is hideously confusing and error-prone, especially
// given that 7-Zip otherwise uses COM infrastructure and terminology,
// but we're obviously stuck with it.  So whenever we create an object
// implemented in the 7-zip code via 'new', we have to explicitly add
// the initial reference.  For the sake of NOT making the clash of
// conventions even worse, we use the 7-Zip convention WITHIN THIS
// MODULE ONLY for the COM-like interfaces we have to implement as
// callbacks to pass to 7-Zip.

#define INITGUID
#include "stdafx.h"
#include "SevenZipIfc.h"
#include "DialogResource.h"
#include "DialogWithSavedPos.h"
#include "../LZMA/CPP/7zip/Common/FileStreams.h"
#include "../LZMA/CPP/7zip/IPassword.h"
#include "../LZMA/CPP/7zip/Archive/iArchive.h"
#include "../LZMA/CPP/Windows/PropVariant.h"

// -----------------------------------------------------------------------
//
// Password dialog
//

static HRESULT RunPasswordDialog(BSTR *pbstrPassword, const TCHAR *archiveFilename, const TCHAR *entryName)
{
	class PasswordDialog : public DialogWithSavedPos
	{
	public:
		PasswordDialog(const TCHAR *archiveFilename, const TCHAR *entryName) :
			DialogWithSavedPos(_T("SevenZipPasswordDialog.Position")),
			archiveFilename(archiveFilename),
			entryName(entryName)
		{
			password[0] = 0;
		}

		const TCHAR *archiveFilename;
		const TCHAR *entryName;

		TCHAR password[512];

		virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam) override
		{
			switch (message)
			{
			case WM_INITDIALOG:
				SetDlgItemText(hDlg, IDC_TXT_ARCHIVE, archiveFilename);
				SetDlgItemText(hDlg, IDC_TXT_ARCHENTRY, entryName != nullptr ? entryName : _T(""));
				break;

			case WM_COMMAND:
				switch (LOWORD(wParam))
				{
				case IDOK:
					// Store the password before closing
					GetDlgItemText(hDlg, IDC_EDIT_PASSWORD, password, countof(password));
					break;
				}
				break;
			}

			// use the base class method
			return __super::Proc(message, wParam, lParam);
		}
	};

	// run the dialog
	PasswordDialog dlg(archiveFilename, entryName);
	dlg.Show(IDD_ARCHIVE_PASSWORD);

	// if they entered a password, return it as a BSTR
	if (dlg.password[0] != 0)
	{
		*pbstrPassword = SysAllocString(dlg.password);
		return S_OK;
	}
	else
	{
		return E_ABORT;
	}
}

// -----------------------------------------------------------------------
//
// GUIDs used in the 7z DLL
//

DEFINE_GUID(CLSID_CFormatZip,
	0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x01, 0x00, 0x00);
DEFINE_GUID(CLSID_CFormatRAR,
	0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x03, 0x00, 0x00);
DEFINE_GUID(CLSID_CFormat7z,
	0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x07, 0x00, 0x00);
DEFINE_GUID(CLSID_CFormatXz,
	0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x0C, 0x00, 0x00);


// -----------------------------------------------------------------------
//
// 7z.dll interface
//

class SevenZipDll
{
public:
	// global singleton instance
	static SevenZipDll inst;

	SevenZipDll() { }
	~SevenZipDll() { }

	// load the DLL if we haven't already
	bool Load(ErrorHandler &eh)
	{
		// if we haven't already loaded the DLL, try to do so now
		if (hdll == NULL)
		{
			// presume failure
			bool ok = false;
			const TCHAR *where = nullptr;
			TCHAR dllPath[MAX_PATH];
			do
			{
				// get the DLL path
				GetDeployedFilePath(dllPath, _T("7-Zip\\7z.dll"), _T("$(SolutionDir)\\7-Zip\\$(Platform)\\7z.dll"));

				// load the DLL
				where = _T("in LoadLibrary()");
				if ((hdll = LoadLibrary(dllPath)) == NULL)
					break;

				// bind functions
				where = _T("binding 7z.dll!CreateObject");
				if ((pCreateObj = (Func_CreateObject)GetProcAddress(hdll, "CreateObject")) == NULL)
					break;

				// success
				ok = true;

			} while (false);

			// if anything failed, unload the DLL
			if (!ok)
			{
				// grab the windows error code
				WindowsErrorMessage winErr;

				// unlock the DLL if we got that far
				if (hdll != NULL)
				{
					FreeLibrary(hdll);
					hdll = NULL;
				}

				// clear bound function pointers
				pCreateObj = NULL;

				// log the error
				eh.SysError(LoadStringT(IDS_ERR_7Z_LOAD_DLL),
					MsgFmt(_T("%s, failed %s: %s"), dllPath, where, winErr.Get()));
				return false;
			}
		}

		// success
		return true;
	}

	// create an object
	HRESULT CreateObject(const GUID *clsID, const GUID *iid, void **ppObj)
	{
		return pCreateObj(clsID, iid, ppObj);
	}

protected:
	// DLL module handle
	HMODULE hdll;

	// CreateObject function
	Func_CreateObject pCreateObj;
};

// singleton instance
SevenZipDll SevenZipDll::inst;


// -----------------------------------------------------------------------
//
// 7-Zip archive interface
//

SevenZipArchive::SevenZipArchive()
{
}

SevenZipArchive::~SevenZipArchive()
{
	if (archive != nullptr)
		archive->Close();
}

bool SevenZipArchive::OpenArchive(const TCHAR *fname, IStream *fileStream, ErrorHandler &eh)
{
	// remember the filename
	this->filename = fname;

	// initialize the DLL singleton
	SevenZipDll &dll = SevenZipDll::inst;
	if (!dll.Load(eh))
		return false;

	// determine which format to use
	const GUID *format = nullptr;
	if (tstriEndsWith(fname, _T(".zip")))
		format = &CLSID_CFormatZip;
	else if (tstriEndsWith(fname, _T(".rar")))
		format = &CLSID_CFormatRAR;
	else if (tstriEndsWith(fname, _T(".7z")))
		format = &CLSID_CFormat7z;

	// make sure we found a format
	if (format == nullptr)
	{
		eh.Error(MsgFmt(IDS_ERR_7Z_UNKNOWN_EXT, fname));
		return false;
	}

	// create the archive reader
	HRESULT hr;
	if (!SUCCEEDED(hr = dll.CreateObject(format, &IID_IInArchive, reinterpret_cast<void**>(&archive))))
	{
		eh.SysError(MsgFmt(IDS_ERR_7Z_CREATE_IINARCH, fname),
			MsgFmt(_T("7z.dll!CreateObject, error %lx"), (long)hr));
		return false;
	}
	archive->AddRef();

	// Implement 7-Zip's private IInStream interface on the COM IStream object.
	// IInStream is very simple and maps almost directly (so one wonders why
	// they didn't just use IStream in the first place!).
	class MyInFileStream : public CMyUnknownImp, public IInStream
	{
	public:
		MyInFileStream(IStream *src) : src(src, RefCounted::DoAddRef)
		{
			// Seek to the start of the stream.  The caller will typically
			// reuse the same stream for a series of archive operations: first
			// scanning the contents of the archive, then extracting selected
			// files.  The 7-zip code assumes that the archive starts at the
			// initial seek point, and doesn't reset the seek point after an
			// operation, so we have to make sure that we're at the starting
			// point on each new Open operation.
			if (src != nullptr)
			{
				LARGE_INTEGER zero = { 0, 0 };
				ULARGE_INTEGER newPos;
				src->Seek(zero, STREAM_SEEK_SET, &newPos);
			}
		}

		MY_UNKNOWN_IMP1(IInStream);

		HRESULT Seek(Int64 offset, UInt32 seekOrigin, UInt64 *pNewPos)
		{
			if (src == nullptr)
				return E_FAIL;

			LARGE_INTEGER liOffset;
			ULARGE_INTEGER newPos;
			liOffset.QuadPart = offset;
			HRESULT result = src->Seek(liOffset, seekOrigin, &newPos);

			if (pNewPos != nullptr)
				*pNewPos = newPos.QuadPart;

			return S_OK;
		}

		HRESULT Read(void *data, UInt32 size, UInt32 *processedSize)
		{
			ULONG actual = 0;
			HRESULT result = S_OK;

			if (size != 0)
				result = src->Read(data, size, &actual);

			if (processedSize != nullptr)
				*processedSize = actual;

			return SUCCEEDED(result) ? S_OK : result;
		}

	protected:
		virtual ~MyInFileStream() { }

		// for testing only
		FILE *fp;

		// underlying source stream
		RefPtr<IStream> src;
	};

	// create the 7-Zip stream reader for our COM IStream
	RefPtr<MyInFileStream> stream(new MyInFileStream(fileStream));
	stream->AddRef();

	// The Open Callback object.  The archive classes use this to ask for
	// a password for encrypted files.  We simply flag an error in
	// these cases.
	class CArchiveOpenCallback :
		public IArchiveOpenCallback,
		public ICryptoGetTextPassword,
		public CMyUnknownImp
	{
	public:
		CArchiveOpenCallback(const TCHAR *fname, ErrorHandler &eh) : fname(fname), eh(eh) { }
		TSTRING fname;
		ErrorHandler &eh;

		MY_UNKNOWN_IMP1(ICryptoGetTextPassword)

			STDMETHOD(SetTotal)(const UInt64 *files, const UInt64 *bytes) { return S_OK; }
		STDMETHOD(SetCompleted)(const UInt64 *files, const UInt64 *bytes) { return S_OK; }

		STDMETHOD(CryptoGetTextPassword)(BSTR *pbstrPassword)
		{
			return RunPasswordDialog(pbstrPassword, fname.c_str(), nullptr);
		}
	};

	// open the archive
	const UINT64 scanSize = 1 << 23;
	RefPtr<CArchiveOpenCallback> openCb(new CArchiveOpenCallback(fname, eh));
	openCb->AddRef();
	if (archive->Open(stream, &scanSize, openCb) != S_OK)
	{
		// extract the extension in upper-case
		TSTRING ext;
		if (const TCHAR *dot = _tcsrchr(fname, '.'); dot != nullptr)
		{
			ext = dot + 1;
			std::transform(ext.begin(), ext.end(), ext.begin(), ::_totupper);
		}
		else
			ext = _T("ZIP");

		// Forget the archive  reference so that we can't inadvertantly call into
		// it later - 7-Zip seems to leave it in a state where 7-Zip can crash
		// internally if we attempt to all any of its interface methods.
		archive = nullptr;

		// log the error
		eh.SysError(
			MsgFmt(IDS_ERR_7Z_OPEN_ARCH, fname, ext.c_str()),
			MsgFmt(_T("7z.dll!IInArchive::Open failed, error code %lx"), (long)hr));
		return false;
	}

	// success
	return true;
}

bool SevenZipArchive::EnumFiles(std::function<void(UINT32 idx, const WCHAR *path, bool isDir)> func)
{
	// fail if the archive isn't open
	if (archive == nullptr)
		return false;

	// count items
	UInt32 nItems = 0;
	if (!SUCCEEDED(archive->GetNumberOfItems(&nItems)))
		return false;

	// scan the files
	bool allOk = true;
	for (UInt32 i = 0; i < nItems; ++i)
	{
		// read this entry's data
		NWindows::NCOM::CPropVariant nameProp, isDirProp;
		if (SUCCEEDED(archive->GetProperty(i, kpidPath, &nameProp))
			&& nameProp.vt == VT_BSTR
			&& SUCCEEDED(archive->GetProperty(i, kpidIsDir, &isDirProp))
			&& isDirProp.vt == VT_BOOL)
		{
			// pass it to the callback
			func(i, nameProp.bstrVal, isDirProp.boolVal != 0);
		}
		else
		{
			// couldn't read this entry - note the error, but keep going
			allOk = false;
		}
	}

	// return the status indication
	return allOk;
}

bool SevenZipArchive::Extract(UINT32 idx, const TCHAR *destFile, ErrorHandler &eh)
{
	// we need an open archive to proceed
	if (archive == nullptr)
		return false;

	// set up the extraction callback object
	class ExtractCallback :
		public IArchiveExtractCallback,
		public ICryptoGetTextPassword,
		public CMyUnknownImp
	{
	public:
		ExtractCallback(SevenZipArchive *arch, const TCHAR *destFile, ErrorHandler &eh) :
			arch(arch),
			destFile(destFile),
			eh(eh),
			nErrors(0)
		{
			// clear the file time and attributes
			fileInfo.modTime.dwLowDateTime = 0;
			fileInfo.modTime.dwHighDateTime = 0;
			fileInfo.attr = INVALID_FILE_ATTRIBUTES;
		}

		MY_UNKNOWN_IMP1(ICryptoGetTextPassword)

			// IProgress
			STDMETHOD(SetTotal)(UInt64) { return S_OK; }
		STDMETHOD(SetCompleted)(const UInt64 *) { return S_OK; }

		// IArchiveExtractCallback
		STDMETHOD(GetStream)(UInt32 index, ISequentialOutStream **pOutStream, Int32 askExtractMode)
		{
			// clear any previous output streams
			*pOutStream = NULL;

			// get the name of the entry we're trying to extract
			NWindows::NCOM::CPropVariant nameProp;
			if (SUCCEEDED(arch->archive->GetProperty(index, kpidPath, &nameProp))
				&& nameProp.vt == VT_BSTR)
				entryName = nameProp.bstrVal;

			// only proceed if we're in 'extract' mode
			if (askExtractMode != NArchive::NExtract::NAskMode::kExtract)
				return S_OK;

			// get the original file attributes from the archive
			NWindows::NCOM::CPropVariant attrProp;
			if (SUCCEEDED(arch->archive->GetProperty(index, kpidAttrib, &attrProp))
				&& attrProp.vt == VT_UI4)
				fileInfo.attr = attrProp.ulVal;

			// get the original file modified timestamp from the archive
			NWindows::NCOM::CPropVariant modTimeProp;
			if (SUCCEEDED(arch->archive->GetProperty(index, kpidMTime, &modTimeProp))
				&& modTimeProp.vt == VT_FILETIME)
				fileInfo.modTime = modTimeProp.filetime;

			// create the output stream (NB - the assignment adds a reference)
			outStream = new COutFileStream();

			// open the file
			if (!outStream->Open(destFile.c_str(), CREATE_ALWAYS))
			{
				eh.Error(MsgFmt(IDS_ERR_7Z_EXTRACT_OPEN_OUTPUT, arch->filename.c_str(), destFile.c_str()));
				return E_ABORT;
			}

			// add a reference on behalf of the caller, and pass the stream back
			(*pOutStream = outStream)->AddRef();

			// success
			return S_OK;
		}

		STDMETHOD(PrepareOperation)(Int32 /*askExtractMode*/)
		{
			return S_OK;
		}

		STDMETHOD(SetOperationResult)(Int32 resultEOperationResult)
		{
			const char *detail = "other error";
			switch (resultEOperationResult)
			{
			case NArchive::NExtract::NOperationResult::kOK:
				// success
				break;

			case NArchive::NExtract::NOperationResult::kWrongPassword:
				// password error
				++nErrors;
				eh.Error(MsgFmt(IDS_ERR_7Z_WRONG_PASSWORD, arch->filename.c_str()));
				break;

			case NArchive::NExtract::NOperationResult::kUnsupportedMethod:  detail = "unsupported method"; goto genErr;
			case NArchive::NExtract::NOperationResult::kCRCError:           detail = "CRC error"; goto genErr;
			case NArchive::NExtract::NOperationResult::kDataError:          detail = "data error"; goto genErr;
			case NArchive::NExtract::NOperationResult::kUnavailable:        detail = "data unavailable"; goto genErr;
			case NArchive::NExtract::NOperationResult::kUnexpectedEnd:      detail = "unexpected end of file"; goto genErr;
			case NArchive::NExtract::NOperationResult::kDataAfterEnd:       detail = "extra data after end of file"; goto genErr;
			case NArchive::NExtract::NOperationResult::kIsNotArc:           detail = "not an archive file"; goto genErr;
			case NArchive::NExtract::NOperationResult::kHeadersError:       detail = "header error"; goto genErr;

			genErr:
				// Generic error message reporter - for technical errors that generally
				// can't be fixed by user action.  We provide details for these in the
				// usual "system error" format, since they might be useful to the
				// developers but won't be helpful to most users.
				++nErrors;
				eh.SysError(
					MsgFmt(IDS_ERR_7Z_EXTRACT_FAILED, arch->filename.c_str(), entryName.c_str(), destFile.c_str()),
					MsgFmt(_T("7z.dll extract failed: %hs"), detail));
			}

			// if we have a stream, finalize it
			if (outStream != nullptr)
			{
				// set the original modified time on the output stream
				if (fileInfo.modTime.dwHighDateTime != 0 || fileInfo.modTime.dwLowDateTime != 0)
					outStream->SetMTime(&fileInfo.modTime);

				// close the stream
				outStream->Close();

				// set the original file attributes
				if (fileInfo.attr != INVALID_FILE_ATTRIBUTES)
				{
					// check for Posix flags
					DWORD attr = fileInfo.attr;
					if ((attr & 0xF0000000) != 0)
						attr &= 0x3FFF;

					// set the attributes on the file
					SetFileAttributes(destFile.c_str(), attr);
				}

				// we're done with the stream
				outStream = nullptr;

				// if errors occurred, delete the file - we don't want to leave
				// behind an empty or corrupted file
				if (nErrors != 0)
					DeleteFile(destFile.c_str());
			}

			return S_OK;
		}

		// ICryptoGetTextPassword
		STDMETHOD(CryptoGetTextPassword)(BSTR *pbstrPassword)
		{
			return RunPasswordDialog(pbstrPassword, arch->filename.c_str(), WSTRINGToTSTRING(entryName).c_str());
		}

		// source archive object
		SevenZipArchive *arch;

		// destination file
		TSTRING destFile;

		// output stream
		RefPtr<COutFileStream> outStream;

		// name of entry being extracted
		WSTRING entryName;

		// error handler
		ErrorHandler &eh;

		// error count
		int nErrors;

		struct
		{
			FILETIME modTime;
			UInt32 attr;
		} fileInfo;
	};
	RefPtr<ExtractCallback> cb;
	cb = new ExtractCallback(this, destFile, eh);

	// set up the object indices
	UInt32 indices[] = { idx };

	// extract the item
	HRESULT hr = archive->Extract(indices, countof(indices), false, cb);
	if (!SUCCEEDED(hr))
	{
		// log a separate generic error if the callback didn't already log
		// specific errors
		if (cb->nErrors == 0)
		{
			eh.SysError(
				MsgFmt(IDS_ERR_7Z_EXTRACT_FAILED, filename.c_str(), cb->entryName.c_str(), destFile),
				MsgFmt(_T("7z.dll!IInArchive::Extract failed, HRESULT %lx"), (long)hr));
		}

		// return failure
		return false;
	}

	// check for logged errors in the extract callback
	if (cb->nErrors != 0)
		return false;

	// success
	return true;
}

