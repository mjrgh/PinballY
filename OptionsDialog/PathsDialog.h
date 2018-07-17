// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include "OptionsPage.h"

class PathsDialog : public OptionsPage
{
	DECLARE_DYNAMIC(PathsDialog)

public:
	PathsDialog(int dialogId);
	virtual ~PathsDialog();

	CButton btnSelDbFolder;
	CButton btnSelMediaFolder;
	CPngImage folderIcon;

	struct FolderRadioMap : RadioStrMap
	{
		FolderRadioMap(const TCHAR *configVar, int autoButtonID, int pbyButtonID, int customButtonID,
			int editID, int browseButtonID, const TCHAR *localFolder) :
			RadioStrMap(configVar, autoButtonID, _T(""), vals, countof(vals)),
			autoButtonID(autoButtonID), pbyButtonID(pbyButtonID), customButtonID(customButtonID),
			editID(editID), browseButtonID(browseButtonID)
		{
			this->localFolder = localFolder;
			vals[0] = _T("");
			vals[1] = this->localFolder.c_str();
			vals[2] = _T("");
		}

		void SetCustomFolder(const TCHAR *s)
		{
			customFolder = s;
			vals[2] = customFolder.c_str();
		}

		virtual void SetDefault(const TCHAR *configVal) override;

		virtual void CreateExtraControls(CWnd *dlg) override
		{
			edit.SubclassDlgItem(editID, dlg);
		}

		void BrowseForFolder(HWND parent, int captionID);

		bool OnEditChange()
		{
			// get the new text
			CString str;
			edit.GetWindowText(str);

			// if it's different from the stored text, update the stored
			// text and select the Custom radio button
			if (str != customFolder.c_str())
			{
				customFolder = str;
				vals[2] = customFolder.c_str();
				intVar = 2;
				return true;
			}

			// no change
			return false;
		}

		int autoButtonID;
		int pbyButtonID;
		int customButtonID;

		int editID;
		int browseButtonID;
		CEdit edit;

		const TCHAR *vals[3];
		TSTRING localFolder;
		TSTRING customFolder;
	};

	FolderRadioMap *dbRadioMap;
	FolderRadioMap *mediaRadioMap;

	virtual void InitVarMap() override;
    virtual BOOL OnInitDialog() override;
    virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;
};

