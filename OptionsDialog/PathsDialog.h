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
			// set the local folder path - this is a relative path that's taken
			// to be relative to the PinballY install folder by default
			this->localFolder = localFolder;
			
			// set the PinballX path - this uses the substitution variable
			// [PinballX] (which expands to the PBX install folder) plus the
			// local folder name
			pbxFolder = _T("[PinballX]\\");
			pbxFolder += localFolder;

			vals[0] = this->localFolder.c_str();
			vals[1] = pbxFolder.c_str();
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

		virtual void SaveConfigVar() override
		{
			// get the string value
			const TCHAR *strVal = intVar >= 0 && (size_t)intVar < nVals ? vals[intVar] : defVal.c_str();

			// if it's empty, use "." as the default
			if (std::regex_match(strVal, std::basic_regex<TCHAR>(_T("\\s*"))))
				strVal = _T(".");

			// set it
			ConfigManager::GetInstance()->Set(configVar, strVal);
		}


		void BrowseForFolder(HWND parent, int captionID);

		bool OnEditChange()
		{
			// get the new text
			CString str;
			edit.GetWindowText(str);

			// if it's different from the stored text, update the stored
			// text and select the Custom radio button
			if (_tcsicmp(str, customFolder.c_str()) != 0)
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
		TSTRINGEx pbxFolder;
		TSTRING customFolder;
	};

	FolderRadioMap *dbRadioMap;
	FolderRadioMap *mediaRadioMap;

	virtual void InitVarMap() override;
    virtual BOOL OnInitDialog() override;
    virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;
};

