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

	// get the file drop handle
	STGMEDIUM stg;
	stg.tymed = TYMED_HGLOBAL;
	FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	if (SUCCEEDED(pDataObj->GetData(&fmt, &stg)))
	{
		// get the file drop handle
		hDrop = (HDROP)stg.hGlobal;

		// get the number of files
		nFiles = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);

		// success
		return true;
	}
	else
	{
		// no file drop information - forget the data object and return failure
		this->pDataObj = nullptr;
		return false;
	}
}

void MediaDropTarget::FileDrop::EnumFiles(std::function<void(const TCHAR*)> func) const
{
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
