// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once
#include "OptionsPage.h"


class SystemDialog : public OptionsPage
{
	DECLARE_DYNAMIC(SystemDialog)

public:
	SystemDialog(int dialogId, int sysNum, bool isNew = false);
	virtual ~SystemDialog();

	virtual void InitVarMap() override;

	// get the system number
	int GetSysNum() const { return sysNum; }

protected:
	// initialize
	virtual BOOL OnInitDialog() override;

	// command handler
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;

	// apply changes
	virtual BOOL OnApply() override;

	// Check if we're modified from the configuration
	virtual bool IsModFromConfig() override;

	// browse for a subfolder given a base path
	void BrowseSubfolder(int editID, const TCHAR *parent);

	// browse for a full folder path
	void BrowseFolder(int editID);

	// browse for an EXE file
	void BrowseExe();

	// browse for a file
	void BrowseFile(int editID);

	// process a change to the system class selection
	void OnSysClassChange();

	// get the edit control for a given variable mapper
	CEdit *GetEditVarMap(int editID);

	// delete the system from the configuration
	void DeleteSystem();

	// system number in config variables ("SystemN.xxx")
	int sysNum;

	// folder icon for the browse buttons
	CPngImage folderIcon;

	// browse buttons
	CButton btnMediaFolder;
	CButton btnDbFolder;
	CButton btnExe;
	CButton btnTableFolder;
	CButton btnNvramFolder;

	// Is this a new system (that is, newly created since the
	// last config file Apply)?
	bool isNew;


	// Var mapper for the System Class combo.  The combo control is
	// set up in the dialog resource with user-friendly names for the
	// system classes.  These must be in a fixed order, since we
	// map the combo list index to the internal config file class
	// names ("VP", "VPX", "FP", etc).
	struct SysClassMap : VarMap
	{
		SysClassMap(const TCHAR *configVar, int controlID) :
			VarMap(configVar, controlID, combo) { }

		// Config file names for the system classes, in combo list 
		// index order.  The prepopulated combo data in the dialog
		// resource must match the order of this array.
		static const TCHAR *configClasses[];

		// default extensions by system class
		static const TCHAR *defExts[];

		CComboBox combo;
		int intVar;

		virtual void doDDX(CDataExchange *pDX) override { DDX_CBIndex(pDX, controlID, intVar); }

		virtual void LoadConfigVar() override;
		virtual void SaveConfigVar() override;
		virtual bool IsModifiedFromConfig() override;
	};
	SysClassMap *sysClassMap;
};

