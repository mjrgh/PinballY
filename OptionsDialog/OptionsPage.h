// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once
#include <afxcolorbutton.h>
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
	virtual BOOL OnApply() override;

	// Handle OnApply failure.  OnApply overrides can call this before
	// returning to re-mark the page as dirty and try to select it in
	// the UI, to direct the user's attention to the locus of the 
	// failure.  If 'ctl' is non-null, we'll set focus on the control
	// after switching back to the page.
	BOOL OnApplyFail(HWND ctl = NULL);
	BOOL OnApplyFail(CWnd *pWnd) { return OnApplyFail(pWnd->GetSafeHwnd()); }
		
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

	// config variable map
	std::list<std::unique_ptr<VarMap>> varMap;
	


	// 
	// Specialized control mappings
	//

	// Checkbox <-> bool
	struct CkBoxMap : VarMap
	{
		CkBoxMap(const TCHAR *configVar, int controlID, bool defVal) :
			VarMap(configVar, controlID, ckbox), defVal(defVal) { }
		int intVar;
		bool defVal;
		CButton ckbox;
		virtual void doDDX(CDataExchange *pDX) override { DDX_Check(pDX, controlID, intVar); }
		virtual void LoadConfigVar() override { intVar = ConfigManager::GetInstance()->GetBool(configVar, defVal); }
		virtual void SaveConfigVar() override { ConfigManager::GetInstance()->SetBool(configVar, intVar); }
		virtual bool IsModifiedFromConfig() override;
	};

	// Checkbox <-> enumerated value
	struct CkBoxEnumMap : CkBoxMap
	{
		CkBoxEnumMap(const TCHAR *configVar, int controlID,
			const TCHAR *uncheckedVal, const TCHAR *checkedVal, bool defVal) :
			CkBoxMap(configVar, controlID, defVal), uncheckedVal(uncheckedVal), checkedVal(checkedVal) { }
		virtual void LoadConfigVar() override { intVar = GetConfigVar(); }
		virtual void SaveConfigVar() override;
		virtual bool GetConfigVar();
		virtual bool IsModifiedFromConfig() override;

		// Config file values to store for the checkbox settings
		TSTRING uncheckedVal;
		TSTRING checkedVal;
	};

	// Edit box <-> string
	struct EditStrMap : VarMap
	{
		EditStrMap(const TCHAR *configVar, int controlID, const TCHAR *defVal) :
			VarMap(configVar, controlID, edit), defVal(defVal) { }
		CString strVar;
		CString defVal;
		CEdit edit;
		virtual void doDDX(CDataExchange *pDX) override { DDX_Text(pDX, controlID, strVar); }
		virtual void LoadConfigVar() override { strVar = FromConfig(ConfigManager::GetInstance()->Get(configVar, defVal)).c_str(); }
		virtual void SaveConfigVar() override { ConfigManager::GetInstance()->Set(configVar, ToConfig(strVar).c_str()); }
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
			VarMap(configVar, controlID, edit), defVal(defVal) { }

		int intVar;
		int defVal;
		CEdit edit;
		virtual void doDDX(CDataExchange *pDX) override { DDX_Text(pDX, controlID, intVar); }
		virtual void LoadConfigVar() override { intVar = ConfigManager::GetInstance()->GetInt(configVar, defVal); }
		virtual void SaveConfigVar() override { ConfigManager::GetInstance()->Set(configVar, intVar); }
		virtual bool IsModifiedFromConfig() override;
	};
	
	// Edit box <-> float value
	struct EditFloatMap : VarMap
	{
		EditFloatMap(const TCHAR *configVar, int controlID, float defVal) :
			VarMap(configVar, controlID, edit),	defVal(defVal) { }
		float floatVar;
		float defVal;
		CEdit edit;
		virtual void doDDX(CDataExchange *pDX) override;
		virtual void LoadConfigVar() override { floatVar = ConfigManager::GetInstance()->GetFloat(configVar, defVal); }
		virtual void SaveConfigVar() override { ConfigManager::GetInstance()->SetFloat(configVar, floatVar); }
		virtual bool IsModifiedFromConfig() override;
	};

	// Edit box <-> percentage value with float
	struct EditFloatPctMap : EditFloatMap
	{
		EditFloatPctMap(const TCHAR *configVar, int controlID, float defVal) :
			EditFloatMap(configVar, controlID, defVal) { }

		// get as a string, with the "%"
		TSTRING GetAsStr();

		virtual void SaveConfigVar() override { ConfigManager::GetInstance()->Set(configVar, GetAsStr().c_str()); }
		virtual void doDDX(CDataExchange *pDX) override;
	};

	// Edit box with spin button <-> int value
	//
	// Note: make sure the following properties are set in the spin control
	// to attach it properly to its edit box:
	//
	//  Alignment = Right Align
	//  Auto Buddy = True
	//  Set Buddy Integer = True
	//  
	struct SpinIntMap : EditIntMap
	{
		SpinIntMap(const TCHAR *configVar, int editControlID, int defVal, int spinControlID, int minVal, int maxVal) :
			EditIntMap(configVar, editControlID, defVal),
			spinControlID(spinControlID), 
			minVal(minVal),
			maxVal(maxVal)
		{ }

		virtual void InitControl() override { spinBtn.SetRange(minVal, maxVal); }

		virtual void ddxControl(CDataExchange *pDX) override
		{
			__super::ddxControl(pDX);
			DDX_Control(pDX, spinControlID, spinBtn);
		}

		int spinControlID;
		int minVal, maxVal;
		CSpinButtonCtrl spinBtn;
	};

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
		virtual void doDDX(CDataExchange *pDX) override { DDX_Radio(pDX, controlID, intVar); }

		virtual void LoadConfigVar() override;
		virtual void SaveConfigVar() override;
		virtual bool IsModifiedFromConfig() override;

		// Set the default radio button index (0..n) for a config value.
		// The "default default" is the first button, but subclasses can
		// override as needed.
		virtual void SetDefault(const TCHAR *configVal) { intVar = 0; }
	};

	// Checkbox variable mapping for "Keep Window Open" checkboxes.
	// These are peculiar in that we use a group of checkboxes to
	// represent the value of a single config variable.  The config
	// variable value contains keywords with the checkbox states:
	//
	//    ShowWindowsWhileRunning = bg -dmd instcard
	//
	// The containing dialog must call our static OnApply() from its
	// OnApply() method.  We'll scan its var map for our instances,
	// and update the corresponding config variable.
	//
	// For the tri-state checkbox, we can customize the drawing to
	// show the states using our special graphics that help clarify
	// the On/Off/Default settings.  To use this, the containing 
	// dialog must intercept WM_NOTIFY messages of type NM_CUSTOMDRAW
	// and pass them to our OnCustomDraw() handler.
	//
	struct KeepWindowCkMap : CkBoxMap
	{
		KeepWindowCkMap(const TCHAR *configVar, const TCHAR *windowID, int controlID, bool triState);
		~KeepWindowCkMap();

		// apply changes - the containing dialog must call this from
		// its OnApply()
		static void OnApply(std::list<std::unique_ptr<VarMap>> &varMap);

		// handle custom drawing for a tri-state checkbox
		static LRESULT OnCustomDraw(CWnd *dlg, NMHDR *pnmhdr);

		// ID string for the window ("bg", "dmd", "topper", "instcard")
		TSTRING windowID;

		// is this a tri-state checkbox (on, off, indeterminate = "inherit default")
		bool triState;

		// bitmap for tri-state checkbox
		static Gdiplus::Bitmap *bmpKeepWinCkbox;
		static int bmpRefs;

		virtual void LoadConfigVar() override;
		virtual void SaveConfigVar() override;
		virtual bool IsModifiedFromConfig() override;

		void InitConfigVal();
		int configVal;
	};

	// Color button mapper
	struct ColorButtonMap : VarMap
	{
		ColorButtonMap(const TCHAR *configVar, int controlID, COLORREF defVal) :
			VarMap(configVar, controlID, button), defVal(defVal) { }

		COLORREF defVal;

		// compact color button - omits the drop arrow to keep it smaller
		class CompactColorButton : public CMFCColorButton
		{
		public:
			virtual void OnDraw(CDC *pDC, const CRect &rc, UINT uiState) override;
			virtual void OnDrawFocusRect(CDC* pDC, const CRect& rectClient) override;
			virtual void OnDrawBorder(CDC*, CRect&, UINT) override { }
		};

		CompactColorButton button;

		virtual void InitControl() override;
		virtual void doDDX(CDataExchange *pDX) override;
		virtual void LoadConfigVar() override;
		virtual void SaveConfigVar() override;
		virtual bool IsModifiedFromConfig() override;
	};
};
