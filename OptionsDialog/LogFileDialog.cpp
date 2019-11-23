// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#include "stdafx.h"
#include "resource.h"
#include "LogFileDialog.h"
#include "../Utilities/Config.h"
#include "../Utilities/FileUtil.h"

IMPLEMENT_DYNAMIC(LogFileDialog, OptionsPage)

LogFileDialog::LogFileDialog(int dialogId) :
	OptionsPage(dialogId)
{
}

LogFileDialog::~LogFileDialog()
{
}

void LogFileDialog::InitVarMap()
{
	varMap.emplace_back(new CkBoxMap(_T("Log.MediaFiles"), IDC_CK_LOG_MEDIA, false));
	varMap.emplace_back(new CkBoxMap(_T("Log.SystemSetup"), IDC_CK_LOG_SYSTEM_SETUP, false));
	varMap.emplace_back(new CkBoxMap(_T("Log.MediaCapture"), IDC_CK_LOG_MEDIA_CAPTURE, true));
	varMap.emplace_back(new CkBoxMap(_T("Log.TableLaunch"), IDC_CK_LOG_TABLE_LAUNCH, false));
	varMap.emplace_back(new CkBoxMap(_T("Log.RealDMD"), IDC_CK_LOG_DMD, true));
	varMap.emplace_back(new CkBoxMap(_T("Log.DOF"), IDC_CK_LOG_DOF, true));
	varMap.emplace_back(new CkBoxMap(_T("Log.Javascript"), IDC_CK_LOG_JAVASCRIPT, true));
	varMap.emplace_back(new CkBoxMap(_T("Log.MediaDrop"), IDC_CK_LOG_MEDIA_DROP, true));
	varMap.emplace_back(new CkBoxMap(_T("Log.HighScoreRetrieval"), IDC_CK_LOG_HIGHSCORES, true));
	varMap.emplace_back(new CkBoxMap(_T("Log.WindowLayoutSetup"), IDC_CK_LOG_WINDOWLAYOUT, false));
}

BOOL LogFileDialog::OnInitDialog()
{
	// build the log file path:  <program folder>\PinballY.log
	TCHAR fname[MAX_PATH];
	GetExeFilePath(fname, countof(fname));
	PathAppend(fname, _T("PinballY.log"));
	logFilePath = fname;

	// show the log file name in the link
	SetDlgItemText(IDC_LNK_LOGFILE, MsgFmt(_T("<a>%s</a>"), fname));

	// do the base class work
	return __super::OnInitDialog();
}

BOOL LogFileDialog::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT *pResult)
{
	// check for notifications from the log file link
	auto nm = reinterpret_cast<NMHDR*>(lParam);
	if (nm->idFrom == IDC_LNK_LOGFILE && (nm->code == NM_CLICK || nm->code == NM_RETURN))
	{
		// open the log file
		ShellExecute(GetParent()->GetSafeHwnd(), _T("open"), _T("notepad.exe"), logFilePath.c_str(), NULL, SW_SHOW);
	}

	// do the base class work
	return __super::OnNotify(wParam, lParam, pResult);
}

