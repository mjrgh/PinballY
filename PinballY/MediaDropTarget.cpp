// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <Shellapi.h>
#include <ShlObj.h>
#include "MediaDropTarget.h"
#include "GameList.h"
#include "BaseView.h"

MediaDropTarget::MediaDropTarget(BaseView *view) :
	lastDropEffect(DROPEFFECT_NONE)
{
	// explicitly assign the view pointer, to count the reference
	this->view = view;

	// register the drop target with Windows
	RegisterDragDrop(view->GetHWnd(), this);
}

MediaDropTarget::~MediaDropTarget()
{
}

void MediaDropTarget::OnDestroyWindow()
{
	RevokeDragDrop(view->GetHWnd());
}

const MediaType *MediaDropTarget::GetBackgroundImageType() const
{
	return view->GetBackgroundImageType();
}

const MediaType *MediaDropTarget::GetBackgroundVideoType() const
{
	return view->GetBackgroundVideoType();
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

bool MediaDropTarget::FileDrop::Init(IDataObject *pDataObj)
{
	// clear fields
	Clear();

	// remember the data object
	this->pDataObj = pDataObj;

	// Try getting a simple file drop handle (HDROP).  This is used for Windows
	// desktop shell drag-drop operations.
	STGMEDIUM stg;
	stg.tymed = TYMED_HGLOBAL;
	FORMATETC fmt_hdrop = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	if (SUCCEEDED(pDataObj->GetData(&fmt_hdrop, &stg)))
	{
		// get the file drop handle
		hDrop = (HDROP)stg.hGlobal;

		// get the number of files
		nFiles = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);

		// success
		return true;
	}

	// Try CFSTR_FILEDESCRIPTOR.  This is used by most other applications to
	// perform file-like transfers.  In particular, browsers use this when
	// dragging objects such as images.
	auto CF_FILEDESCRIPTOR = static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_FILEDESCRIPTOR));
	FORMATETC fmt_fd = { CF_FILEDESCRIPTOR, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	if (SUCCEEDED(pDataObj->GetData(&fmt_fd, &stg)))
	{
		// lock the global data object
		auto fgd = static_cast<FILEGROUPDESCRIPTOR*>(GlobalLock(stg.hGlobal));

		// remember the global handle and the number of files
		nFiles = fgd->cItems;

		// make a copy of the group descriptor and the array of file descriptors
		// it contains
		size_t fgdSize = sizeof(FILEGROUPDESCRIPTOR) + (nFiles - 1) * sizeof(fgd->fgd[0]);
		fileGroupDesc.reset(static_cast<FILEGROUPDESCRIPTOR*>(malloc(fgdSize)));
		memcpy(fileGroupDesc.get(), fgd, fgdSize);

		// done with the data object for now
		GlobalUnlock(stg.hGlobal);

		// success
		return true;
	}

	// no file drop information - forget the data object and return failure
	this->pDataObj = nullptr;
	return false;
}

void MediaDropTarget::FileDrop::EnumFiles(std::function<void(const TCHAR*, IStream*)> func)
{
	for (UINT i = 0; i < nFiles; ++i)
	{
		// get this file descriptor
		auto fileDesc = &fileGroupDesc->fgd[i];

		// check the transfer type
		if (hDrop != NULL)
		{
			// HDROP transfer
			// get this file's name length
			if (UINT len = DragQueryFile(hDrop, i, NULL, 0); len != 0)
			{
				// allocate space and retrieve the filename
				std::unique_ptr<TCHAR> fname(new TCHAR[++len]);
				DragQueryFile(hDrop, i, fname.get(), len);

				// create a stream on the file
				RefPtr<IStream> stream;
				SHCreateStreamOnFileEx(fname.get(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream);

				// process the file through the callback
				func(fname.get(), stream);
			}
		}
		else if (fileGroupDesc.get() != nullptr)
		{
			// CFSTR_FILEDESCRIPTOR transfer
			// Retrieve the file contents for this item.  The API doc says that the
			// file contents can be provided as an HGLOBAL, IStream, or IStorage.
			// I'm not going to bother with the IStorage case beacuse I don't think
			// it'll actually occur in the wild with any drag-and-drop sources that
			// would be used in this context.  I expect we'll only get drops from
			// the Windows desktop and from browsers.  The desktop doesn't use this
			// mechanism to begin with (it uses HDROP transfers), and browsers will
			// always (as far as I've seen) treat their downloads as blobs, so they
			// can be expected to use HGLOBAL or IStream transfers.  I suppose that
			// a browser trying to be fancy could provide an IStorage interface to
			// a ZIP download or something like that, but I doubt it; we can always
			// revisit IStorage if it's needed for some useful use case.
			STGMEDIUM stg;
			stg.tymed = TYMED_ISTREAM;
			auto CF_FILECONTENTS = static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_FILECONTENTS));
			FORMATETC fmt = { CF_FILECONTENTS, NULL, DVASPECT_CONTENT, static_cast<LONG>(i), TYMED_ISTREAM | TYMED_HGLOBAL };
			if (SUCCEEDED(pDataObj->GetData(&fmt, &stg)))
			{
				// set up reference-counting on the IUknown
				RefPtr<IUnknown> stgRefPtr(stg.pUnkForRelease);

				// check the type and get the IStream
				RefPtr<IStream> stream;
				if ((stg.tymed & TYMED_ISTREAM) != 0)
				{
					// there's already an IStream in the clipboard data, so just use that
					stream = stg.pstm;
				}
				else if ((stg.tymed & TYMED_HGLOBAL) != 0)
				{
					// If the file size in the descriptor is zero, use the size of the
					// HGLOBAL data block instead.  The SDK documentation ponits out that
					// the HGLOBAL size is unreliable because the allocation size might be
					// rounded to an allocation unit size, but in practice, some programs
					// send us file descriptors with the size (and all of the other fields)
					// zeroed out.  It seems that our only option is to use the memory
					// block size, imprecise though it might be.
					UINT64 fileSize = (fileDesc->nFileSizeHigh | fileDesc->nFileSizeLow) != 0 ?
						(static_cast<UINT64>(fileDesc->nFileSizeHigh) << 32) | fileDesc->nFileSizeLow :
						GlobalSize(stg.hGlobal);

					// Create a memory stream on the contents of the HGLOBAL.
					// Note that memory streams only accept a 32-bit size for the data, so this
					// won't work if the object is over 4GB.  Hopefully any program that sends
					// us very large objects would send us an IStream instead, but if there
					// actually does exist some pathological case where we get a transfer over
					// 4GB as an HGLOBAL, we'll just have to ignore it.
					//
					// Note that there's a COM function that creates an IStream directly on an
					// HGLOBAL (CreateStreamOnHGlobal).  That might seem more direct, and it
					// might even be more efficient for large objects, because presumably it
					// doesn't need to make a private copy of the data.  (Although the SDK
					// documentation insists that memory streams have inherently better
					// performance and should always be used over CreateStreamOnHGGlobal,
					// which Microsoft seems to consider a mistake they'd like people to stop
					// using.)  Apart from the SDK's advice that memory streams are always
					// better, we have an important "correctness" reason to use memory streams:
					// there's no way to prevent the originating program from deleting the
					// HGLOBAL out from under us after we return from the present function,
					// and we're going to supply an IStream to the caller that they might
					// hold onto indefinitely.  An IStream from CreateStreamOnHGlobal is
					// dependent upon the HGLOBAL staying around as long as the IStream is
					// around, but it has no way to enforce that, so it's a dangling-pointer
					// crash just waiting to happen.  Better to use the more modern and more
					// correctly designed memory-stream approach.
					if (fileSize <= UINT32_MAX)
					{
						// lock the HGLOBAL 
						if (auto p = static_cast<BYTE*>(GlobalLock(stg.hGlobal)); p != nullptr)
						{
							// create the memory stream
							stream = SHCreateMemStream(p, static_cast<UINT>(fileSize));

							// we're done with the HGLOBAL
							GlobalUnlock(stg.hGlobal);
						}
					}
				}

				// process the file through the callback
				if (stream != nullptr)
					func(fileGroupDesc->fgd[i].cFileName, stream);
			}
		}
	}
}

HRESULT MediaDropTarget::DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
	// presume we won't accept the drop
	lastDropEffect = DROPEFFECT_NONE;

	// get the drop information; if successful, pass it to the view for processing
	if (fileDrop.Init(pDataObj))
		view->ShowDropTargets(fileDrop, ScreenToView(pt), &lastDropEffect);

	// success
	*pdwEffect = lastDropEffect;
	return S_OK;
}

HRESULT MediaDropTarget::DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
	// if there's a data object, pass it to the view for processing
	if (fileDrop.IsValid())
		view->UpdateDropTargets(fileDrop, ScreenToView(pt), &lastDropEffect);

	// set the drop effect
	*pdwEffect = lastDropEffect;

	// done
	return S_OK;
}

HRESULT MediaDropTarget::DragLeave()
{
	// remove the drop target info from the window
	if (fileDrop.IsValid())
		view->RemoveDropTargets();

	// clear the file drop object
	fileDrop.Clear();

	// done
	return S_OK;
}

HRESULT MediaDropTarget::Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
	// presume failure
	*pdwEffect = DROPEFFECT_NONE;

	// refresh the drop object
	if (fileDrop.Init(pDataObj))
	{
		// pass it to the view for processing
		*pdwEffect = lastDropEffect;
		view->DoMediaDrop(fileDrop, ScreenToView(pt), pdwEffect);
	}

	// remove any drop area visual effects from the target window
	view->RemoveDropTargets();

	// success
	return S_OK;
}

POINT MediaDropTarget::ScreenToView(POINTL ptl) const
{
	// convert to a regular POINT
	POINT pt = { ptl.x, ptl.y };

	// convert to client coordinates
	ScreenToClient(view->GetHWnd(), &pt);

	// adjust for mirroring
	RECT rc;
	GetClientRect(view->GetHWnd(), &rc);
	if (view->IsMirrorHorz())
		pt.x = rc.right - pt.x;
	if (view->IsMirrorVert())
		pt.y = rc.bottom - pt.y;

	// adjust for rotation
	switch (view->GetRotation())
	{
	default:
		return pt;

	case 90:
		// NB - the POINT qualifier is a compiler bug workaround.  It might
		// look redundant, which it is, but don't remove it until you can
		// verify that Microsoft has fixed the compiler bug.
		// https://developercommunity.visualstudio.com/content/problem/449167/return-with-struct-list-initializer-computes-wrong.html
		return POINT { rc.bottom - pt.y, rc.left + pt.x };

	case 180:
		return POINT { rc.right - pt.x, rc.bottom - pt.y };

	case 270:
		return POINT { rc.top + pt.y, rc.right - pt.x };
	}
}
