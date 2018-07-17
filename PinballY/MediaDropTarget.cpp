// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <Shellapi.h>
#include <ShlObj.h>
#include "MediaDropTarget.h"
#include "GameList.h"
#include "Application.h"
#include "PlayfieldView.h"

MediaDropTarget::MediaDropTarget(const MediaType *imageType, const MediaType *videoType) :
	lastDropEffect(DROPEFFECT_NONE),
	imageType(imageType),
	videoType(videoType)
{
}

MediaDropTarget::~MediaDropTarget()
{
}

HRESULT MediaDropTarget::QueryInterface(REFIID iid, LPVOID *ppUnk)
{
	*ppUnk = NULL;
	if (iid == IID_IUnknown)
		*ppUnk = static_cast<IUnknown*>(this);
	else if (iid == IID_IDropTarget)
		*ppUnk = static_cast<IDropTarget*>(this);
	else
		return E_NOINTERFACE;

	return S_OK;
}

// process files in the drop through a callback
static void ProcessFileDrop(IDataObject *pDataObj, std::function<void(const TCHAR*)> func)
{
	// check for a file drop
	STGMEDIUM stg;
	stg.tymed = TYMED_HGLOBAL;
	FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	if (SUCCEEDED(pDataObj->GetData(&fmt, &stg)))
	{
		// get the file drop descriptor handle (HDROP)
		HDROP hDrop = (HDROP)stg.hGlobal;

		// process the files
		UINT nFiles = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);
		for (UINT i = 0; i < nFiles; ++i)
		{
			// get this file's name length
			if (UINT len = DragQueryFile(hDrop, i, NULL, 0); len != 0)
			{
				// allocate space and retrieve the filename
				std::unique_ptr<TCHAR> fname(new TCHAR[++len]);
				DragQueryFile(hDrop, i, fname.get(), len);

				// process the file through the callback
				func(fname.get());
			}
		}
	}
}

HRESULT MediaDropTarget::DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
	// presume we won't accept the drop
	lastDropEffect = DROPEFFECT_NONE;

	// all drops are processed through the playfield view, so we
	// can't proceed unless that's available
	auto pfv = Application::Get()->GetPlayfieldView();
	if (pfv == nullptr)
	{
		*pdwEffect = DROPEFFECT_NONE;
		return S_OK;
	}

	// check for a file drop
	FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	if (SUCCEEDED(pDataObj->QueryGetData(&fmt)))
	{
		// scan the files to determine if any are acceptable
		int nFilesAccepted = 0;
		ProcessFileDrop(pDataObj, [this, pfv, &nFilesAccepted](const TCHAR *fname)
		{
			// try processing it through the playfield view
			if (pfv->CanDropFile(fname, this))
				++nFilesAccepted;
		});

		// if we accepted any files, set the 'copy' effect
		if (nFilesAccepted != 0)
			lastDropEffect = DROPEFFECT_COPY;
	}

	// success
	*pdwEffect = lastDropEffect;
	return S_OK;
}

HRESULT MediaDropTarget::DragLeave()
{
	return S_OK;
}

HRESULT MediaDropTarget::DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
	*pdwEffect = lastDropEffect;
	return S_OK;
}

HRESULT MediaDropTarget::Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
	// no files processed yet
	int nFilesProcessed = 0;
	*pdwEffect = DROPEFFECT_NONE;

	// all drops are processed through the playfield view, so we
	// can't proceed unless that's available
	auto pfv = Application::Get()->GetPlayfieldView();
	if (pfv == nullptr)
		return S_OK;

	// begin the file drop operation
	pfv->BeginFileDrop();

	// process the dropped files
	ProcessFileDrop(pDataObj, [this, pfv, &nFilesProcessed](const TCHAR *fname)
	{
		// try processing it through the playfield view
		if (pfv->DropFile(fname, this))
			++nFilesProcessed;
	});

	// end the drop operation
	pfv->EndFileDrop();

	// if we dropped any files, set the drop effect to 'copy' for the
	// caller's benefit, in case they want to show different feedback
	// for different results
	if (nFilesProcessed != 0)
		*pdwEffect = DROPEFFECT_COPY;

	// success
	return S_OK;
}
