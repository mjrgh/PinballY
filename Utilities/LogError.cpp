// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Error logging

#include "stdafx.h"
#include <math.h>
#include "UtilResource.h"
#include "Util.h"
#include "StringUtil.h"
#include "LogError.h"
#include "Dialog.h"

// --------------------------------------------------------------------------
//
// Error-with-details dialog
//

class SysErrorDialog : public MessageBoxLikeDialog
{
public:
	SysErrorDialog(const TCHAR *friendly, const TCHAR *details, int bitmap_id)
		: MessageBoxLikeDialog(bitmap_id)
	{
		this->friendly = friendly;
		this->details = details;
	}

	~SysErrorDialog()
	{
		DeleteObject(brushBkg);
		DeleteObject(brush3dFace);
	}

protected:
	const TCHAR *friendly;
	const TCHAR *details;

	HBRUSH brushBkg;
	HBRUSH brush3dFace;

	virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		static HBRUSH brushBkg;
		static HBRUSH brush3dFace;
		static HBITMAP icon;

		switch (message)
		{
		case WM_INITDIALOG:
		{
			// inherit the standard handling
			MessageBoxLikeDialog::Proc(message, wParam, lParam);

			// set the friendly text and size the control to fit
			int dy = ResizeStaticToFitText(GetDlgItem(IDC_TXT_ERROR), friendly);

			// move controls below the text downward to accommodate the new size
			MoveCtlBy(IDOK, 0, dy);
			MoveCtlBy(IDC_SHOW_DETAILS, 0, dy);
			MoveCtlBy(IDC_BOTTOM_BAR, 0, dy);
			MoveCtlBy(IDC_TXT_DETAILS_LABEL, 0, dy);
			MoveCtlBy(IDC_TXT_ERRDETAIL, 0, dy);

			// increase the window height for the expanded text
			ExpandWindowBy(0, dy);

			// expand the detail text to fit its text
			ResizeStaticToFitText(GetDlgItem(IDC_TXT_ERRDETAIL), details);

			// done
			return TRUE;
		}

		case WM_COMMAND:
			if (LOWORD(wParam) == IDC_SHOW_DETAILS)
			{
				// show the detail message and label, and hide the Details button
				ShowWindow(GetDlgItem(IDC_TXT_DETAILS_LABEL), SW_SHOW);
				ShowWindow(GetDlgItem(IDC_TXT_ERRDETAIL), SW_SHOW);
				ShowWindow(GetDlgItem(IDC_SHOW_DETAILS), SW_HIDE);

				// figure the height of the newly exposed controls (plus a little margin)
				RECT rc1 = GetCtlScreenRect(GetDlgItem(IDC_TXT_DETAILS_LABEL));
				RECT rc2 = GetCtlScreenRect(GetDlgItem(IDC_TXT_ERRDETAIL));
				int dy = rc2.bottom - rc1.top + 10;

				// move the controls below these down by the newly exposed height
				MoveCtlBy(IDOK, 0, dy);
				MoveCtlBy(IDC_BOTTOM_BAR, 0, dy);

				// increase the window height
				RECT rcw;
				GetWindowRect(hDlg, &rcw);
				MoveWindow(hDlg, rcw.left, rcw.top, rcw.right - rcw.left, rcw.bottom - rcw.top + dy, TRUE);

				// done
				return TRUE;
			}
			break;
		}

		// do the default handling
		return MessageBoxLikeDialog::Proc(message, wParam, lParam);
	}

};

// --------------------------------------------------------------------------
//
// Log an error using the basic Windows message box style.
//
void LogError(ErrorIconType icon, const TCHAR *message)
{
	// figure the system icon cased on our internal icon type
	UINT mbIcon = (icon == EIT_Warning ? MB_ICONWARNING :
		icon == EIT_Information ? MB_ICONINFORMATION :
		MB_ICONERROR);

	// show a standard system message box
	MessageBoxWithIdleMsg(GetActiveWindow(), message, 
		LoadStringT(IDS_ERRDLG_CAPTION).c_str(), 
		MB_OK | MB_TASKMODAL | mbIcon);
}

// Log a "system" error.  This is for situations where the underlying error
// comes from a system API, and we don't have a way to recover from the
// specific underyling problem.  The difference between this and the basic
// error dialog is that this one breaks the error message into a "friendly" 
// part, with a (hopefully) non-technical description of the operation that
// was being attempted, and a "details" part, which contains information on
// the specific point in the code where the error occurred and the error
// code from the API, if available.  Most users find the technical details
// useless, but we don't want to go the Apple route of simply hiding that
// information entirely, since it does become useful if the user has to
// contact the developers about the problem.  So we try to have it both
// ways by separating the two parts.  The dialog reports just the friendly
// part initially, but provides a "Details" button that lets the user reveal
// the details on demand.
void LogSysError(ErrorIconType icon, const TCHAR *friendly, const TCHAR *details)
{
	// figure the image resource corresponding to the icon ID
	int bitmap_id = (icon == EIT_Warning ? IDB_WARNING : 
		icon == EIT_Information ? IDB_INFORMATION : 
		IDB_ERROR);

	// show our "system error with hidden details" dialog
	SysErrorDialog dlg(friendly, details, bitmap_id);
	dlg.Show(IDD_ERROR);
}

// --------------------------------------------------------------------------
//
// Error-with-text-box dialog
//

class ErrorWithTextDialog : public MessageBoxLikeDialog
{
public:
	ErrorWithTextDialog(
		const TCHAR *summary, const std::list<TSTRING> *errlist,
		const TCHAR *separator, int bitmap_id)
		: MessageBoxLikeDialog(bitmap_id)
	{
		this->summary = summary;
		this->errlist = errlist;
		this->separator = separator;
	}

protected:
	const TCHAR *summary;
	const std::list<TSTRING> *errlist;
	const TCHAR *separator;

	virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_INITDIALOG:
		{
			// do the basic handling
			MessageBoxLikeDialog::Proc(message, wParam, lParam);

			// expand the summary static to fit its text
			int dy = ResizeStaticToFitText(GetDlgItem(IDC_TXT_ERROR), summary);

			// move the items below the summary to accommodate the expansion
			MoveCtlBy(IDC_DETAILS, 0, dy);
			MoveCtlBy(IDOK, 0, dy);
			MoveCtlBy(IDC_BOTTOM_BAR, 0, dy);

			// expand the window to accommodate the increased height
			RECT rcw;
			GetWindowRect(hDlg, &rcw);
			MoveWindow(hDlg, rcw.left, rcw.top, rcw.right - rcw.left, rcw.bottom - rcw.top + dy, TRUE);

			// store the detail text
			TSTRING detailText;
			for (auto it : *errlist)
			{
				detailText.append(it);
				detailText.append(separator);
			}
			SetWindowText(GetDlgItem(IDC_DETAILS), detailText.c_str());

			// done
			return TRUE;
		}

		}

		// inherit the default handling
		return MessageBoxLikeDialog::Proc(message, wParam, lParam);
	}
};


// --------------------------------------------------------------------------
//
// Log an error with details in a scrolling text box
//
void LogErrorWithDetails(
	const TCHAR *summary, 
	const std::list<TSTRING> *details, 
	const TCHAR *separator, 
	ErrorIconType icon)
{
	// show the dialog
	int bitmap_id = (icon == EIT_Warning ? IDB_WARNING : 
		icon == EIT_Information ? IDB_INFORMATION :
		IDB_ERROR);
	ErrorWithTextDialog dlg(summary, details, separator, bitmap_id);
	dlg.Show(IDD_ERRORWITHTEXTBOX);
}


// --------------------------------------------------------------------------
//
// Format a stdio file error message.  'err' is an errno/_doserrno error
// code, or an equivalent code returned from a stdio-type function.
// 'msg' is a printf-style format string with a message to display.
// We'll format that message, then substitute the file system message
// for the substring '$E' in the formatted string.  We'll finally
// return the combined result.
TSTRING FileErrorMessage(int err)
{
	TSTRING result;

	// get the system error message
	TCHAR fileErrMsg[512];
	if (_tcserror_s<countof(fileErrMsg)>(fileErrMsg, err))
	{
		// couldn't get the message - use a generic "error <number>" message
		result = MsgFmt(_T("File system error %d"), err).Get();
	}
	else
	{
		// got the message - return it
		result = fileErrMsg;
	}

	// return the string object
	return result;
}


// --------------------------------------------------------------------------
// 
// Basic error handler
//

void ErrorHandler::Error(const TCHAR *msg)
{
	// display the message
	Display(EIT_Error, msg);
}

void ErrorHandler::SysError(const TCHAR *friendly, const TCHAR *details)
{
	// format a combined message
	MsgFmt msg(IDS_ERR_SYSERROR, friendly, details);

	// combine the friendly and details messages and display the result
	Display(EIT_Error, msg.Get());
}

void ErrorHandler::GroupError(ErrorIconType icon, const TCHAR *summary, const class ErrorList &geh)
{
	// start with the formatted summary
	TSTRING message = summary;

	// append the details as line items
	geh.EnumErrors([&message](const ErrorList::Item &item) -> void 
	{
		// add it to the list
		message.append(_T("\r\n"));
		message.append(item.message);
		if (item.details.length() != 0)
			message.append(MsgFmt(IDS_ERR_TECHDETAILS, item.message.c_str()));
	});

	// log the formatted message
	Display(icon, message.c_str());
}

const TSTRING ErrorHandler::GroupErrorDetailLocation() const
{
	return LoadStringT(IDS_ERRLOC_BELOW);
}

// --------------------------------------------------------------------------
//
// Interactive error handler
//

void InteractiveErrorHandler::Display(ErrorIconType icon, const TCHAR *msg)
{
	LogError(icon, msg);
}

void InteractiveErrorHandler::SysError(const TCHAR *friendly, const TCHAR *details)
{
	LogSysError(EIT_Error, friendly, details);
}

void InteractiveErrorHandler::GroupError(ErrorIconType icon, const TCHAR *summary, const ErrorList &geh)
{
	// Build a list of the details
	std::list<TSTRING> details;
	geh.EnumErrors([&details](const ErrorList::Item &item) -> void 
	{
		TSTRING txt = item.message;
		if (item.details.length() != 0)
			txt.append(MsgFmt(_T(" (%s)"), item.details.c_str()));
		details.emplace(details.end(), txt.c_str()); 
	});

	// log the error
	LogErrorWithDetails(summary, &details, geh.ErrorSeparator(), icon);
}

// --------------------------------------------------------------------------
//
// Capturing error handler
//

void CapturingErrorHandler::Display(ErrorIconType icon, const TCHAR *msg)
{
	// store the message
	errors.emplace(errors.end(), msg, _T(""));
}

void CapturingErrorHandler::SysError(const TCHAR *friendly, const TCHAR *details)
{
	errors.emplace(errors.end(), friendly, details);
}

void CapturingErrorHandler::EnumErrors(std::function<void(const Item &)> callback) const
{
	// pass each error in the list to the callback
	for (auto it : errors)
		callback(it);
}

// --------------------------------------------------------------------------
//
// Parsing error handler
//

void ParsingErrorHandler::Error(const TCHAR *msg)
{
	// count the error
	++errCount;

	// add it to the list
	errors.emplace(errors.end(), lineno, msg);
}

void ParsingErrorHandler::EnumErrors(std::function<void(const Item &)> callback) const
{
	// iterate through the error list
	for (auto it = errors.begin(); it != errors.end(); ++it)
	{
		// format this message with the line number and pass it to the callback
		Item item(MsgFmt(IDS_ERR_LINENO, it->lineno, it->msg.c_str()), _T(""));
		callback(item);
	}
}
