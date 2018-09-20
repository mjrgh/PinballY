// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
 #include "stdafx.h"
#include "resource.h"
#include "PathsDialog.h"
#include "../Utilities/Config.h"
#include "../Utilities/WinUtil.h"
#include "../Utilities/FileUtil.h"
#include "../Utilities/PBXUtil.h"

IMPLEMENT_DYNAMIC(PathsDialog, OptionsPage)

PathsDialog::PathsDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

PathsDialog::~PathsDialog()
{
}


void PathsDialog::InitVarMap()
{
	varMap.emplace_back(mediaRadioMap = new FolderRadioMap(
		_T("MediaPath"),
		IDC_RB_PBY_MEDIA_FOLDER, IDC_RB_PBX_MEDIA_FOLDER, IDC_RB_CUSTOM_MEDIA_FOLDER,
		IDC_EDIT_MEDIA_FOLDER, IDC_BTN_MEDIA_FOLDER, 
		_T("Media")));
	varMap.emplace_back(dbRadioMap = new FolderRadioMap(
		_T("TableDatabasePath"), 
		IDC_RB_PBY_DB_FOLDER, IDC_RB_PBX_DB_FOLDER, IDC_RB_CUSTOM_DB_FOLDER,
		IDC_EDIT_DB_FOLDER, IDC_BTN_DB_FOLDER, 
		_T("Databases")));
}

BOOL PathsDialog::OnInitDialog()
{
    // do the base class work
    BOOL result = __super::OnInitDialog();

	// set up the Select Folder buttons
	btnSelDbFolder.SubclassDlgItem(IDC_BTN_DB_FOLDER, this);
	btnSelMediaFolder.SubclassDlgItem(IDC_BTN_MEDIA_FOLDER, this);

	// set their icons
	folderIcon.Load(MAKEINTRESOURCE(IDB_FOLDER_ICON));
	btnSelDbFolder.SetBitmap(folderIcon);
	btnSelMediaFolder.SetBitmap(folderIcon);

	// return the base class result
    return result;
}

void PathsDialog::FolderRadioMap::SetDefault(const TCHAR *configVal)
{
	// select the "custom" radio button
	intVar = 2;
	
	// store the custom path in the slot for the button value
	customFolder = configVal;
	vals[2] = customFolder.c_str();

	// store the path in the edit control as well
	edit.SetWindowText(configVal);
}

void PathsDialog::FolderRadioMap::BrowseForFolder(HWND parent, int captionID)
{
	TSTRING path = customFolder;
	if (::BrowseForFolder(path, parent, LoadStringT(captionID)))
	{
		// clear the old custom folder, to force a UI update
		customFolder = _T("");

		// set the new window text - this will trigger a change
		// notification, which will trigger updating the radio
		// button and internal field values
		edit.SetWindowText(path.c_str());
	}
}

BOOL PathsDialog::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (wParam)
	{
	case MAKEWPARAM(IDC_BTN_DB_FOLDER, BN_CLICKED):
		// "Browse for a database folder" button click
		dbRadioMap->BrowseForFolder(::GetParent(GetSafeHwnd()), IDS_BROWSE_DB_FOLDER_CAPTION);
		break;

	case MAKEWPARAM(IDC_BTN_MEDIA_FOLDER, BN_CLICKED):
		// "Browse for a media folder" button click
		mediaRadioMap->BrowseForFolder(::GetParent(GetSafeHwnd()), IDS_BROWSE_MEDIA_FOLDER_CAPTION);
		break;

	case MAKEWPARAM(IDC_EDIT_DB_FOLDER, EN_CHANGE):
		// change to the custom db folder - sync the radio button
		if (dbRadioMap->OnEditChange())
			UpdateData(false);
		break;

	case MAKEWPARAM(IDC_EDIT_MEDIA_FOLDER, EN_CHANGE):
		// change to the custom media folder - sync the radio button
		if (mediaRadioMap->OnEditChange())
			UpdateData(false);
		break;

	case MAKEWPARAM(IDC_RB_PBX_DB_FOLDER, BN_CLICKED):
	case MAKEWPARAM(IDC_RB_PBX_MEDIA_FOLDER, BN_CLICKED):
		// if PinballX isn't installed, warn that these might not work properly
		if (GetPinballXPath(true) == nullptr)
			::MessageBox(GetParent()->GetSafeHwnd(), LoadStringT(IDS_WARN_NO_PBX_PATH), LoadStringT(IDS_WARN_CAPTION), 
				MB_ICONWARNING | MB_OK);

		// but let them do it anyway - they might be doing this in anticipation
		// of installing PinballX later
		break;
	}
	
	// do the normal work
    return __super::OnCommand(wParam, lParam);
}

