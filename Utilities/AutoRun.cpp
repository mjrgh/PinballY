// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include <taskschd.h>
#include <lmcons.h>
#include "Pointers.h"
#include "AutoRun.h"
#include "LogError.h"
#include "UtilResource.h"
#include "WinUtil.h"
#include "ComUtil.h"

#define SECURITY_WIN32
#include <security.h>

// for task scheduler CLSIDs
#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "Secur32.lib")


// Delete any old HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run key
// from a past iteration.
static bool CleanUpRunKey(const TCHAR *desc, ErrorHandler &eh)
{
	LSTATUS err = ERROR_SUCCESS;
	auto ReturnError = [&err, &eh](const TCHAR *where)
	{
		WindowsErrorMessage sysErr(err);
		eh.SysError(LoadStringT(IDS_ERR_CLEANAUTOLAUNCHREG),
			MsgFmt(_T("%s: system error %d: %s"), where, err, sysErr.Get()));
		return false;
	};

	// open the Run key in the registry
	HKEYHolder hkey;
	HKEY rootKey = HKEY_CURRENT_USER;
	const TCHAR *keyName = _T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run");
	if ((err = RegOpenKey(HKEY_CURRENT_USER, keyName, &hkey)) == ERROR_FILE_NOT_FOUND)
	{
		// no such key - there's nothing to clean up
		return true;
	}
	else if (err != ERROR_SUCCESS)
	{
		// something else went wrong
		return ReturnError(MsgFmt(_T("Opening %s"), keyName));
	}

	// query the current value
	DWORD typ;
	DWORD len;
	err = RegQueryValueEx(hkey, desc, NULL, &typ, NULL, &len);
	if (err == ERROR_SUCCESS)
	{
		// The value is present.  Delete it.
		if ((err = RegDeleteValue(hkey, desc)) != ERROR_SUCCESS)
			return ReturnError(MsgFmt(_T("Deleting %s[%s]"), keyName, desc));
	}
	else if (err == ERROR_FILE_NOT_FOUND)
	{
		// The value isn't present - nothing to do
		return true;
	}
	else
	{
		// other error
		return ReturnError(MsgFmt(_T("Initial value query for %s[%s]"), keyName, desc));
	}

	// no errors
	return true;
}

// Set up Auto Run using Task Scheduler
bool SetUpAutoRun(bool add, const TCHAR *desc, const TCHAR *exe, const TCHAR *params, bool adminMode, ErrorHandler &eh)
{
	// error handler - log an error and return false
	LONG err;
	HRESULT hr;
	auto ReturnError = [&err, &eh](const TCHAR *where)
	{
		WindowsErrorMessage sysErr(err);
		eh.SysError(LoadStringT(IDS_ERR_SYNCAUTOLAUNCHREG),
			MsgFmt(_T("%s: system error %d: %s"), where, err, sysErr.Get()));
		return false;
	};
	auto ReturnCOMError = [&hr, &eh](const TCHAR *where)
	{
		WindowsErrorMessage sysErr(hr);
		eh.SysError(LoadStringT(IDS_ERR_SYNCAUTOLAUNCHREG),
			MsgFmt(_T("%s: HRESULT %lx, %s"), where, (long)hr, sysErr.Get()));
		return false;
	};

	// Before we set up the Task Scheduler task, remove any pre-existing Run
	// key from the registry.  Before Alpha 10, we used the registry key
	// HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Run[desc]
	// to do the auto-launch.  That key is incapable of launching Admin mode
	// programs, and seems to be unreliable even for regular user mode on
	// some systems.  But if the current system had an earlier (pre-Alpha 10)
	// version installed, and auto-launch was enabled, it'll have the Run key.
	// We obviously don't want to leave that in place: if they're switching
	// auto-launch off, we want to make sure both the task and the key are
	// gone so that the program doesn't auto-launch by any mechanism; and if
	// they're switching auto-launch on, we obviously don't want the key to
	// be there in addition to the task, as we'd get two copies of the program
	// running.  So no matter what the new auto-launch setting is, we want the
	// key gone. 
	CleanUpRunKey(desc, eh);

	// set up the task name
	BString taskName(MsgFmt(_T("%s Startup Task"), desc));

	// create a Task Service instance
	RefPtr<ITaskService> pService;
	if (!SUCCEEDED(hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pService))))
		return ReturnCOMError(_T("creating Task Scheduler service instance"));

	// connect to the task service
	VARIANTEx vEmpty;
	if (!SUCCEEDED(hr = pService->Connect(vEmpty, vEmpty, vEmpty, vEmpty)))
		return ReturnCOMError(_T("connecting to Task Scheduler service"));

	// connect to the root task folder
	RefPtr<ITaskFolder> pRootFolder; 
	WCHAR rootFolderPath[] = L"\\";
	if (!SUCCEEDED(hr = pService->GetFolder(rootFolderPath, &pRootFolder)))
		return ReturnCOMError(_T("getting Task Scheduler root task folder"));

	// delete any existing task of the same name
	if (!SUCCEEDED(hr = pRootFolder->DeleteTask(taskName, 0))
		&& hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
		return ReturnCOMError(_T("deleting Task Scheduler task"));

	// if we're not adding a new task, we're done
	if (!add)
		return true;

	// create the task builder object 
	RefPtr<ITaskDefinition> pTask;
	if (!SUCCEEDED(hr = pService->NewTask(0, &pTask)))
		return ReturnCOMError(_T("creating new Task Scheduler task builder object"));

	// get the registration info
	RefPtr<IRegistrationInfo> pRegInfo;
	if (!SUCCEEDED(hr = pTask->get_RegistrationInfo(&pRegInfo)))
		return ReturnCOMError(_T("getting Task Scheduler task registration information"));

	// set the task author
	BString author(desc);
	if (!SUCCEEDED(hr = pRegInfo->put_Author(author)))
		return ReturnCOMError(_T("setting task author"));

	// create the principal for the task
	RefPtr<IPrincipal> pPrincipal;
	if (!SUCCEEDED(hr = pTask->get_Principal(&pPrincipal)))
		return ReturnCOMError(_T("creating the task principal"));

	// set up the principal
	WCHAR principalId[] = L"Principal1";
	if (!SUCCEEDED(hr = pPrincipal->put_Id(principalId)))
		return ReturnCOMError(_T("setting task principal ID"));
	if (!SUCCEEDED(hr = pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN)))
		return ReturnCOMError(_T("setting task logon type"));

	// set admin mode if required
	if (adminMode && !SUCCEEDED(hr = pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST)))
		return ReturnCOMError(_T("setting task elevation (privilege) level"));

	// create the task settings
	RefPtr<ITaskSettings> pSettings;
	if (!SUCCEEDED(hr = pTask->get_Settings(&pSettings)))
		return ReturnCOMError(_T("creating task settings"));

	// set up the settings
	if (!SUCCEEDED(hr = pSettings->put_StartWhenAvailable(VARIANT_TRUE)))
		return ReturnCOMError(_T("setting task start-when-available value"));

	// set up the trigger condition
	RefPtr<ITriggerCollection> pTriggerCollection;
	if (!SUCCEEDED(hr = pTask->get_Triggers(&pTriggerCollection)))
		return ReturnCOMError(_T("getting task trigger collection"));

	// add the logon trigger
	RefPtr<ITrigger> pTrigger;
	RefPtr<ILogonTrigger> pLogonTrigger;
	if (!SUCCEEDED(hr = pTriggerCollection->Create(TASK_TRIGGER_LOGON, &pTrigger)))
		return ReturnCOMError(_T("creating logon trigger for task"));
	if (!SUCCEEDED(hr = pTrigger->QueryInterface(IID_PPV_ARGS(&pLogonTrigger))))
		return ReturnCOMError(_T("querying logon trigger interface"));

	// set it up
	WCHAR triggerId[] = L"LogonTrigger", delay[] = L"PT5S";
	if (!SUCCEEDED(hr = pLogonTrigger->put_Id(triggerId)))
		return ReturnCOMError(_T("setting logon trigger ID"));
	if (!SUCCEEDED(hr = pLogonTrigger->put_Delay(delay)))
		return ReturnCOMError(_T("setting logon trigger delay"));

	// set the username in the logon trigger to the current user account
	WCHAR username[UNLEN + 1];
	DWORD usernameLen = countof(username);
	if (!GetUserNameExW(NameSamCompatible, username, &usernameLen))
		return ReturnError(_T("getting current user name"));
	if (!SUCCEEDED(hr = pLogonTrigger->put_UserId(username)))
		return ReturnCOMError(_T("setting logon trigger user ID"));

	// create the action collection
	RefPtr<IActionCollection> pActionCollection;
	if (!SUCCEEDED(hr = pTask->get_Actions(&pActionCollection)))
		return ReturnCOMError(_T("creating action collection for task"));

	// create the execute action
	RefPtr<IAction> pAction;
	RefPtr<IExecAction> pExecAction;
	if (!SUCCEEDED(hr = pActionCollection->Create(TASK_ACTION_EXEC, &pAction)))
		return ReturnCOMError(_T("creating executable action"));
	if (!SUCCEEDED(hr = pAction->QueryInterface(IID_PPV_ARGS(&pExecAction))))
		return ReturnCOMError(_T("querying executable action interface"));

	// set the executable path
	BString bExe(exe);
	if (!SUCCEEDED(hr = pExecAction->put_Path(bExe)))
		return ReturnCOMError(_T("setting executable path in task"));

	// set the working directory to the folder containing the executable
	WCHAR exeDir[MAX_PATH];
	wcscpy_s(exeDir, exe);
	PathRemoveFileSpec(exeDir);
	if (!SUCCEEDED(hr = pExecAction->put_WorkingDirectory(exeDir)))
		return ReturnCOMError(_T("setting working directory in task"));

	// set the command line parameters, if present
	if (params)
	{
		BString bParams(params);
		if (!SUCCEEDED(hr = pExecAction->put_Arguments(bParams)))
			return ReturnCOMError(_T("setting command parameters in task"));
	}

	// save the task to the root folder
	RefPtr<IRegisteredTask> pRegisteredTask;
	if (!SUCCEEDED(hr = pRootFolder->RegisterTaskDefinition(
		taskName, pTask, TASK_CREATE_OR_UPDATE,
		vEmpty, vEmpty, TASK_LOGON_INTERACTIVE_TOKEN, vEmpty,
		&pRegisteredTask)))
		return ReturnCOMError(_T("registering task"));

	// success
	return true;
}

// Get the auto-launch state in Task Scheduler.  Returns true on success,
// false on failure.
bool GetAutoRunState(const TCHAR *desc, bool &exists,
	TSTRING &exe, TSTRING &params, bool &adminMode,
	ErrorHandler &eh)
{
	// set up the task name
	BString taskName(MsgFmt(_T("%s Startup Task"), desc));

	// error handler - log an error and return false
	LONG err;
	HRESULT hr;
	auto ReturnError = [&err, &eh, &taskName](const TCHAR *where)
	{
		WindowsErrorMessage sysErr(err);
		eh.SysError(MsgFmt(IDS_ERR_GETAUTOLAUNCHREG, (const wchar_t*)taskName),
			MsgFmt(_T("%s: system error %d: %s"), where, err, sysErr.Get()));
		return false;
	};
	auto ReturnCOMError = [&hr, &eh, &taskName](const TCHAR *where)
	{
		WindowsErrorMessage sysErr(hr);
		eh.SysError(MsgFmt(IDS_ERR_GETAUTOLAUNCHREG, (const wchar_t*)taskName),
			MsgFmt(_T("%s: HRESULT %lx, %s"), where, (long)hr, sysErr.Get()));
		return false;
	};

	// create a Task Service instance
	RefPtr<ITaskService> pService;
	if (!SUCCEEDED(hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pService))))
		return ReturnCOMError(_T("creating Task Scheduler service instance"));

	// connect to the task service
	VARIANTEx vEmpty;
	if (!SUCCEEDED(hr = pService->Connect(vEmpty, vEmpty, vEmpty, vEmpty)))
		return ReturnCOMError(_T("connecting to Task Scheduler service"));

	// connect to the root task folder
	RefPtr<ITaskFolder> pRootFolder;
	WCHAR rootFolderPath[] = L"\\";
	if (!SUCCEEDED(hr = pService->GetFolder(rootFolderPath, &pRootFolder)))
		return ReturnCOMError(_T("getting Task Scheduler root task folder"));

	// retrieve the task, if present
	RefPtr<IRegisteredTask> pRegisteredTask;
	if ((hr = pRootFolder->GetTask(taskName, &pRegisteredTask)) == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
	{
		// The task doesn't exist.  Fill in the results to indicate this
		// and return success
		exists = false;
		exe = _T("");
		params = _T("");
		adminMode = false;
		return true;
	}
	else if (!SUCCEEDED(hr))
		return ReturnCOMError(_T("looking up task"));

	// the task exists
	exists = true;

	// get the task definition
	RefPtr<ITaskDefinition> pTaskDef;
	if (!SUCCEEDED(hr = pRegisteredTask->get_Definition(&pTaskDef)))
		return ReturnCOMError(_T("retrieving the task definition"));

	// get the task principal, to determine the launch mode
	RefPtr<IPrincipal> pPrincipal;
	if (!SUCCEEDED(hr = pTaskDef->get_Principal(&pPrincipal)))
		return ReturnCOMError(_T("retrieving the task principal"));

	// get the run level type 
	TASK_RUNLEVEL_TYPE runLevel;
	if (!SUCCEEDED(hr = pPrincipal->get_RunLevel(&runLevel)))
		return ReturnCOMError(_T("getting task logon type"));

	// note whether it's admin mode or normal mode
	adminMode = (runLevel == TASK_RUNLEVEL_HIGHEST);

	// get the action collection
	RefPtr<IActionCollection> pActionCollection;
	if (!SUCCEEDED(hr = pTaskDef->get_Actions(&pActionCollection)))
		return ReturnCOMError(_T("retrieving action collection for task"));

	// get the execute action
	long nActions;
	if (!SUCCEEDED(hr = pActionCollection->get_Count(&nActions)))
		return ReturnCOMError(_T("getting action count"));
	for (long i = 1; i <= nActions; ++i)
	{
		RefPtr<IAction> pAction;
		RefPtr<IExecAction> pExecAction;
		if (!SUCCEEDED(hr = pActionCollection->get_Item(i, &pAction)))
			return ReturnCOMError(_T("retrieving action"));
		if (SUCCEEDED(hr = pAction->QueryInterface(IID_PPV_ARGS(&pExecAction))))
		{
			// get the executable name
			BSTR bExe = NULL;
			if (!SUCCEEDED(hr = pExecAction->get_Path(&bExe)))
				return ReturnCOMError(_T("setting executable path in task"));

			// return it
			if (bExe != NULL)
			{
				exe = bExe;
				SysFreeString(bExe);
			}

			// get the arguments
			BSTR bArgs = NULL;
			if (!SUCCEEDED(hr = pExecAction->get_Arguments(&bArgs)))
				return ReturnCOMError(_T("getting command line arguments"));

			// return the arguments
			if (bArgs != NULL)
			{
				params = bArgs;
				SysFreeString(bArgs);
			}

			// we've got everything - return success
			return true;
		}
	}

	// failed to find the command information - return those as empty
	exe = _T("");
	params = _T("");
	return true;
}
