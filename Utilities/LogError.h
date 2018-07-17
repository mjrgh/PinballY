// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once

#include <list>
#include <functional>
#include "Util.h"
#include "StringUtil.h"
#include "Dialog.h"
#include "ErrorIconType.h"

// Basic error logging.  This is essentially equivalent to the system
// default message box.
void LogError(ErrorIconType icon, const TCHAR *message);

// Error logging for internal or system errors.
//
// 'friendly' is a general, user-friendly message describing in general 
// terms what went wrong, without going into technical details.  This
// message shouldn't use terms that a non-technical user would find
// meaningless, such as internal subsystem names or Windows error code
// numbers.  It should simply describe the general nature of the problem.
//
// 'details' provides the technical details for debugging and support
// purposes.  This can contain printf-style format codes (filled in
// via the varargs that follow) and should contain the gory details
// that the 'friendly' message omits, to help a developer identify
// the underlying cause of the error.
//
void LogSysError(ErrorIconType icon, const TCHAR *friendly, const TCHAR *details);

// Show an error dialog with a summary message and details in a text
// box.  This is similar to the system error logger, but (a) it doesn't
// initially hide the details, and (b) it uses a scrolling text box for
// the details.  This is useful for cases where the details are intended
// for direct user consumption, and where the number of detail messages 
// is inherently unpredictable.  File input parsing is the canonical
// example: parsing a file might produce many errors, but the operation
// of loading the file is conceptually a single unit of work from the
// user's perspective, so we want to group all of the errors into a
// single UI event rather than presenting one popup alert after 
// another.  And the detail errors in this case are useful for the
// user to review, since they should tell the user what needs to be
// changed to correct the problem.  (In contrast, raw system errors 
// from D3D COM interfaces wouldn't typicaly point a non-technical
// user to a solution, so they're not worth displaying in the UI in
// many cases.)
//
// The list gives the line item detail messages to display in a
// scrolling text box.  The summary message is formatted with printf 
// semantics and is displayed at the top of the dialog.
//
void LogErrorWithDetails(
	const TCHAR *summary, 
	const std::list<TSTRING> *details,
	const TCHAR *separator,
	ErrorIconType icon = EIT_Error);

// Retrieve the file system error for the given errno/_doserrno code
TSTRING FileErrorMessage(int err);

// Error handling interface.  In some cases, we want to show error
// messages interactively; sometimes we don't want to show anything
// in the UI but want to log them for later review; and sometimes
// we want to simply ignore errors altogether.  This provides a
// generic interface for handling different cases.  Routines that
// need to be able to report different errors can accept an object
// with the generic interface, and callers can provide suitable
// implementations.
class ErrorHandler
{
public:
	// Report a simple error message.
	//
	// This is for errors where the entire message is suitable for
	// presentation to non-technical users, such as errors due to
	// invalid user actions, syntax errors in user input, etc.  
	//
	// By default, we format the message and call Display().
	virtual void Error(const TCHAR *msg);

	// Report a system error.  Uses the same interface as
	// LogSysError(), but handles the actual presentation according
	// to the concrete subclass's policies.  'friendly' is a
	// non-technical summary of the error and any suggested
	// remedies.  'details' contains technical details of the
	// error, such as the specific operation being attempted and
	// resulting system error codes.  'details' is specified as a
	// printf-style format string that's formatted with the varargs
	// parameters that follow.
	//
	// This is for errors from system components such as Windows
	// or D3D, where a system call failed and returned an error
	// code that we don't interpret ourselves but want to make
	// available to the user for debugging purposes.  The key feature
	// of this routine is that it allows for technical details that
	// can be hidden in the UI unless the user specifically asks for
	// them.
	//
	// By default, this formats the message by combining the friendly
	// and detailed messages into a single string, then calls Display()
	// on the result.  Subclasses can override to do more sophisticated
	// handling as desired.
	virtual void SysError(const TCHAR *friendly, const TCHAR *details);

	// Log a parsing error.  This logs an error where a single
	// conceptual operation might generate many individual errors,
	// such as when parsing text input.  We log the whole group of
	// errors as a unit to allow for a less intrusive UI than
	// presenting a long series of alert boxes.
	virtual void GroupError(ErrorIconType icon, const TCHAR *summary, const class ErrorList &geh);

	// Get a description of the location of the error details that
	// are displayed by this error handler in a GroupError() message.
	// This should be a simple prepositional phrase that can be 
	// inserted into a message string advising the user of where
	// the detail message list can be found.  The standard UI shows
	// the list in a scrollable text box at the bottom of the dialog,
	// so the default description is "below".  A subclass that sends
	// the details to a log file might say "in the log file 
	// (<filename>)".
	virtual const TSTRING GroupErrorDetailLocation() const;

protected:
	// Display a formatted error
	virtual void Display(ErrorIconType icon, const TCHAR *msg) = 0;
};

// Interactive message handler.  This handler displays basic errors
// via a system message box, system errors via the system error dialog,
// and parsing errors via the detail text box dialog.
class InteractiveErrorHandler : public ErrorHandler
{
public:
	virtual void SysError(const TCHAR *friendly, const TCHAR *details);
	virtual void GroupError(ErrorIconType icon, const TCHAR *summary, const class ErrorList &geh);

protected:
	// display the mesage
	virtual void Display(ErrorIconType icon, const TCHAR *msg);
};

// Silent message handler.  This handler simply discards errors.
class SilentErrorHandler : public ErrorHandler
{
public:
	virtual void SysError(const TCHAR *, const TCHAR *) { }
	virtual void Error(const TCHAR *) { }
	virtual void GroupError(ErrorIconType icon, const TCHAR *, const class ErrorList &) { }
	virtual void Display(ErrorIconType icon, const TCHAR *) { }
};

// Group error list.  This is a generic interface for error
// loggers that capture multiple messages for display in a single
// UI action (e.g., in a scrolling text box within a dialog).
class ErrorList
{
public:
	virtual ~ErrorList() { }

	// error list item
	struct Item
	{
		Item(const TCHAR *message, const TCHAR *details)
		{
			this->message = message;
			this->details = (details != 0 ? details : _T(""));
		}
		TSTRING message;
		TSTRING details;
	};

	// enumerate the errors
	virtual void EnumErrors(std::function<void(const struct Item &)>) const = 0;

	// count the errors
	virtual size_t CountErrors() const = 0;

	// separator to use between messages in the display text box
	virtual const TCHAR *ErrorSeparator() const { return _T("\r\n\r\n"); }
};

class SimpleErrorList : public ErrorList
{
public:
	virtual size_t CountErrors() const override { return items.size(); }
	virtual void EnumErrors(std::function<void(const struct Item &)> cb) const override
	{
		for (auto i : items)
			cb(i);
	}
	std::list<Item> items;

	void Add(const ErrorList *errorList)
	{
		errorList->EnumErrors([this](const Item &item) { items.emplace_back(item); });
	}
};

// Error list with several group error lists
class MultiErrorList : public ErrorList
{
public:
	// Add an error list to the group.  The summary message is included
	// included in the overall summary if the list has any errors.
	void Add(const ErrorList *errorList)
	{
		errorLists.emplace(errorLists.end(), errorList);
	}

	// Report the error through the error handler.  If there are any
	// errors in any of the lists, we report them, constructing a summary
	// message that combines the summaries for all of the lists.  We
	// don't show any message if there are no errors.  We return true if
	// any errors were reported, false if not.
	bool Report(ErrorIconType icon, ErrorHandler &eh, const TCHAR *summary)
	{
		// if we found any errors, make our report
		if (CountErrors() != 0)
		{
			eh.GroupError(icon, summary, *this);
			return true;
		}
		else
			return false;
	}

	// enumerate the errors
	virtual void EnumErrors(std::function<void(const Item &)> callback) const
	{
		// enumerate the errors in each error list
		for (auto it: errorLists)
			it.errorList->EnumErrors(callback);
	}
	
	// count errors
	virtual size_t CountErrors() const
	{
		size_t tot = 0;
		for (auto it = errorLists.begin(); it != errorLists.end(); ++it)
			tot += it->errorList->CountErrors();
		return tot;
	}

protected:
	struct Sublist
	{
		Sublist(const ErrorList *errorList) { this->errorList = errorList; }
		const ErrorList *errorList;
	};

	// list of error handlers
	std::list<Sublist> errorLists;
};

// Capturing error handler.  This captures errors for later display
// through another error handler.
class CapturingErrorHandler : public ErrorHandler, public ErrorList
{
public:
	virtual void SysError(const TCHAR *friendly, const TCHAR *details);

	// enumerate the errors
	virtual void EnumErrors(std::function<void(const Item &)>) const;
	virtual size_t CountErrors() const { return errors.size(); }

protected:
	virtual void Display(ErrorIconType icon, const TCHAR *msg);

	// captured error list
	std::list<Item> errors;
};

// Parsing error logger.  This collects a list of error messages,
// with associated line number locations, for processes that involve
// parsing multi-line text input.  The key feature is that this type
// of process can generate a whole series of errors for what is
// conceptually a single unit of work from the user's perspective,
// such as loading a script.  We don't want to force the user to
// click through a long series of error messages, but at the same
// time we don't want to force the user to keep coming back to find
// new errors one at a time.  The solution is to collect all of the
// errors in the whole process and present them as a unit: that way
// we can report the overall failure in an understandable way ("The
// script file can't be loaded because it contains syntax errors")
// while also presenting the whole list of individual errors as a
// group, such as in a scrolling text box or in a log file.
class ParsingErrorHandler : public ErrorList
{
public:
	ParsingErrorHandler() : lineno(0), errCount(0) { }

	// enumerate the errors
	virtual void EnumErrors(std::function<void(const Item &)>) const;
	virtual size_t CountErrors() const { return errCount; }
	virtual const TCHAR *ErrorSeparator() const { return _T("\r\n"); }

	// current line number - the caller should update this as
	// parsing proceeds through the file
	int lineno;

	// error count - incremented each time Error is called
	size_t errCount;
	 
	// error list entry
	struct Err
	{
		Err(int lineno, const TCHAR *msg)
		{
			this->lineno = lineno;
			this->msg = msg;
		}
		int lineno;
		TSTRING msg;
	};

	// error list
	std::list<Err> errors;

	// Report an error.  Formats the message using printf
	// semantics, then adds it to the error list with the
	// curent line number.
	void Error(const TCHAR *msg);
};
