// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once
#include "../Utilities/Config.h"

class OptionsPage : public CPropertyPageEx
{
	DECLARE_DYNAMIC(OptionsPage)

public:
	OptionsPage(int dialogId);
	virtual ~OptionsPage();

	// do we have uncomitted changes?
	bool IsDirty() const { return m_bIsDirty; }

protected:
	// timer IDs
	static const int DirtyCheckTimerId = 101;

	// timer handler
	afx_msg void OnTimer(UINT_PTR id);

	// set up the VarMap entries
	virtual void InitVarMap() { }

	// initialize the dialog
	virtual BOOL OnInitDialog() override;

	// do data exchange
	virtual void DoDataExchange(CDataExchange *pDX) override;

	// apply changes
	BOOL OnApply();
		
	// Set the dirty (modified) flag
	void SetDirty(BOOL dirty = TRUE);

	// Command handler
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;
	
	// Dirty flag
	bool m_bIsDirty;

	// Check if we're modified from the configuration
	virtual bool IsModFromConfig();

	DECLARE_MESSAGE_MAP()

	// Control <-> Member variable <-> Config variable mapping.
	//
	// This is an extension of the MFC "DDX" scheme that adds our config
	// file variables to the mix.  It also greatly simplifies setup by
	// eliminating the need to declare separate variables for the value
	// and CWnd object for every dialog control.  Instead, we just have
	// to create a VarMap instance per dialog control in InitVarMap(),
	// and everything else proceeds automatically using the list.
	struct VarMap
	{
		VarMap(const TCHAR *configVar, int controlID, CWnd &controlWnd) :
			configVar(configVar), controlID(controlID), controlWnd(controlWnd) { }

		virtual ~VarMap() { }

		const CString configVar;   // config variable name
		int controlID;             // dialog control ID
		CWnd &controlWnd;          // control CWnd object

		// Create extra controls.  This creates any controls that we
		// maintain directly, rather than through DDX.  Controls are
		// usually created via SubclassDlgItem().
		virtual void CreateExtraControls(CWnd *dlg) { }

		// initialize the control(s)
		virtual void InitControl() { }

		// set up the control mapping
		virtual void ddxControl(CDataExchange *pDX) { DDX_Control(pDX, controlID, controlWnd); }

		// do the DDX value exchange (DDX_Check, DDX_Text, etc)
		virtual void doDDX(CDataExchange *pDX) = 0;

		// load/save the config variable
		virtual void LoadConfigVar() = 0;
		virtual void SaveConfigVar() = 0;

		// test if the control value differs from the config value
		virtual bool IsModifiedFromConfig() = 0;
	};

	// Checkbox <-> bool
	struct CkBoxMap : VarMap
	{
		CkBoxMap(const TCHAR *configVar, int controlID, bool defVal) :
			VarMap(configVar, controlID, ckbox), defVal(defVal) { }
		int intVar;
		bool defVal;
		CButton ckbox;
		virtual void doDDX(CDataExchange *pDX) { DDX_Check(pDX, controlID, intVar); }
		virtual void LoadConfigVar() { intVar = ConfigManager::GetInstance()->GetBool(configVar, defVal); }
		virtual void SaveConfigVar() { ConfigManager::GetInstance()->SetBool(configVar, intVar); }
		virtual bool IsModifiedFromConfig() override;
	};

	// Edit box <-> string
	struct EditStrMap : VarMap
	{
		EditStrMap(const TCHAR *configVar, int controlID, const TCHAR *defVal) :
			VarMap(configVar, controlID, edit), defVal(defVal) { }
		CString strVar;
		CString defVal;
		CEdit edit;
		virtual void doDDX(CDataExchange *pDX) { DDX_Text(pDX, controlID, strVar); }
		virtual void LoadConfigVar() { strVar = FromConfig(ConfigManager::GetInstance()->Get(configVar, defVal)).c_str(); }
		virtual void SaveConfigVar() { ConfigManager::GetInstance()->Set(configVar, ToConfig(strVar).c_str()); }
		virtual bool IsModifiedFromConfig() override;

		virtual TSTRING FromConfig(const TCHAR *str) { return str; }
		virtual TSTRING ToConfig(const TCHAR *str) { return str; }
	};

	// Edit box <-> status line message.  This is specialized to
	// convert between the config file's "|" delimited format and
	// the one-message-per-line format we use in the text box.
	struct StatusMessageMap : EditStrMap
	{
		StatusMessageMap(const TCHAR *configVar, int controlID, const TCHAR *defVal) :
			EditStrMap(configVar, controlID, defVal) { }

		virtual TSTRING FromConfig(const TCHAR *str) override;
		virtual TSTRING ToConfig(const TCHAR *str) override;
	};

	// Edit box <-> integer value
	struct EditIntMap : VarMap
	{
		EditIntMap(const TCHAR *configVar, int controlID, int defVal) :
			VarMap(configVar, controlID, edit) { }
		int intVar;
		int defVal;
		CEdit edit;
		virtual void doDDX(CDataExchange *pDX) { DDX_Text(pDX, controlID, intVar); }
		virtual void LoadConfigVar() override { intVar = ConfigManager::GetInstance()->GetInt(configVar, defVal); }
		virtual void SaveConfigVar() override { ConfigManager::GetInstance()->Set(configVar, intVar); }
		virtual bool IsModifiedFromConfig() override;
	};
	
	// Edit box <-> float value
	struct EditFloatMap : VarMap
	{
		EditFloatMap(const TCHAR *configVar, int controlID, float defVal) :
			VarMap(configVar, controlID, edit) { }
		float floatVar;
		float defVal;
		CEdit edit;
		virtual void doDDX(CDataExchange *pDX);
		virtual void LoadConfigVar() { floatVar = ConfigManager::GetInstance()->GetFloat(configVar, defVal); }
		virtual void SaveConfigVar() { ConfigManager::GetInstance()->SetFloat(configVar, floatVar); }
		virtual bool IsModifiedFromConfig() override;
	};

	// Edit box with spin button <-> int value
	struct SpinIntMap : EditIntMap
	{
		SpinIntMap(const TCHAR *configVar, int editControlID, int defVal, int spinControlID, int minVal, int maxVal) :
			EditIntMap(configVar, editControlID, defVal),
			spinControlID(spinControlID), 
			minVal(minVal),
			maxVal(maxVal)
		{ }

		virtual void InitControl() override { spinBtn.SetRange(minVal, maxVal); }

		virtual void ddxControl(CDataExchange *pDX)
		{
			__super::ddxControl(pDX);
			DDX_Control(pDX, spinControlID, spinBtn);
		}

		int spinControlID;
		int minVal, maxVal;
		CSpinButtonCtrl spinBtn;
	};

	// config variable map
	std::list<std::unique_ptr<VarMap>> varMap;

	// Radio button group <-> string
	struct RadioStrMap : VarMap
	{
		RadioStrMap(const TCHAR *configVar, int controlID, const TCHAR *defVal, const TCHAR *const *vals, size_t nVals) :
			VarMap(configVar, controlID, radio), defVal(defVal), vals(vals), nVals(nVals) { }
		int intVar;
		TSTRING defVal;
		CButton radio;
		const TCHAR *const *vals;
		size_t nVals;
		virtual void doDDX(CDataExchange *pDX) { DDX_Radio(pDX, controlID, intVar); }

		virtual void LoadConfigVar() override;
		virtual void SaveConfigVar() override;
		virtual bool IsModifiedFromConfig() override;

		// Set the default radio button index (0..n) for a config value.
		// The "default default" is the first button, but subclasses can
		// override as needed.
		virtual void SetDefault(const TCHAR *configVal) { intVar = 0; }
	};
};
