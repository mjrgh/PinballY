// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Main entrypoint for options DLL.
//

#include "stdafx.h"
#include <afxvisualmanagervs2008.h>
#include "OptionsDialogDll.h"
#include "OptionsDialogExports.h"
#include "OptionsDialog.h"
#include "../Utilities/InstanceHandle.h"
#include "../Utilities/InputManagerWithConfig.h"
#include "../Utilities/Config.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#endif


BEGIN_MESSAGE_MAP(COptionsDialogApp, CWinApp)
END_MESSAGE_MAP()

extern "C" DWORD WINAPI GetOptionsDialogVersion()
{
	return PINBALLY_OPTIONS_DIALOG_IFC_VSN;
}

extern "C" void WINAPI ShowOptionsDialog(
	const TCHAR *configFilePath,
	ConfigSaveCallback configSaveCallback,
	InitializeDialogPositionCallback initPosCallback,
	bool isAdminHostRunning,
	SetUpAdminAutoRunCallback setUpAdminAutoRunCallback,
	RECT *finalDialogRect)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	// initialize subsystems we use
	ConfigManager::Init();
	InputManagerWithConfig::Init();

	// load the configuration
	ConfigFileDesc configFileDesc = MainConfigFileDesc;
	configFileDesc.dir = configFilePath;
	ConfigManager::GetInstance()->Load(configFileDesc);

	// set up for save notifications
	class Receiver : ConfigManager::Subscriber
	{
	public:
		Receiver(ConfigSaveCallback configSaveCallback) :
			configSaveCallback(configSaveCallback)
		{
			// subscribe for config notifications
			ConfigManager::GetInstance()->Subscribe(this);
		}
		
		ConfigSaveCallback configSaveCallback;
		virtual void OnConfigPostSave(bool succeeded) override 
		{ 
			configSaveCallback(succeeded);
		}

	} receiver(configSaveCallback);

	// show the dialog
	MainOptionsDialog dlg(initPosCallback, isAdminHostRunning, setUpAdminAutoRunCallback, finalDialogRect);
	dlg.DoModal();

	// save any pending changes to the in-memory configuration
	ConfigManager::GetInstance()->SaveIfDirty();

	// shut down the subsystems we use
	ConfigManager::Shutdown();
	InputManager::Shutdown();
	CMFCVisualManager::DestroyInstance();
}


extern "C" void WINAPI Cleanup()
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
}

// application object singleton
COptionsDialogApp theApp;

COptionsDialogApp::COptionsDialogApp()
{
}

BOOL COptionsDialogApp::InitInstance()
{
	// do superclass initialization first
	BOOL result = __super::InitInstance();

	// save the instance handle in our global
	G_hInstance = m_hInstance;

	// Set up the most modern visual manager available.  This is used by
	// CMFCxxx controls; they'll adopt a rather charmingly retro Windows XP
	// style in the absence of a specific selection here.  (You'd think
	// the system theme manager settings would apply instead as the default,
	// especially since MFC is theme-aware, but you'd be wrong.  For some
	// reason they thought you'd prefer your application's look and feel
	// to be forever frozen in that most futuristic year of 2001.)
	CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerVS2008));

	// return the base class result
	return result;
}

