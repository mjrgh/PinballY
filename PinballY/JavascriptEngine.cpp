// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//

#include "stdafx.h"
#include "../Utilities/FileUtil.h"
#include "../Utilities/ComUtil.h"
#include "../Utilities/DateUtil.h"
#include "JavascriptEngine.h"
#include "LogFile.h"
#include "DialogResource.h"

#include <filesystem>
namespace fs = std::experimental::filesystem;


#pragma comment(lib, "ChakraCore.lib")
#pragma comment(lib, "ChakraCore.Debugger.Service.lib")
#pragma comment(lib, "ChakraCore.Debugger.ProtocolHandler.lib")
#pragma comment(lib, "ChakraCore.Debugger.Protocol.lib")

// statics
JavascriptEngine *JavascriptEngine::inst;
double JavascriptEngine::Task::nextId = 1.0;

JavascriptEngine::JavascriptEngine()
{
}

bool JavascriptEngine::Init(ErrorHandler &eh, const MessageWindow &messageWindow, DebugOptions *debug)
{
	// if there's already a singleton, there's nothing to do
	if (inst != nullptr)
		return true;

	// create and initialize the global singleton
	inst = new JavascriptEngine();
	return inst->InitInstance(eh, messageWindow, debug);
}

bool JavascriptEngine::InitInstance(ErrorHandler &eh, const MessageWindow &messageWindow, DebugOptions *debug)
{
	JsErrorCode err;
	auto Error = [&err, &eh](const TCHAR *where)
	{
		MsgFmt details(_T("%s failed: %s"), where, JsErrorToString(err));
		eh.SysError(LoadStringT(IDS_ERR_JSINIT), details);
		LogFile::Get()->Write(LogFile::JSLogging, _T(". Javascript engine initialization error: %s\n"), details.Get());
		return false;
	};

	// save the message window options
	this->messageWindow = messageWindow;

	// set up attributes
	DWORD attrs = JsRuntimeAttributeEnableExperimentalFeatures | JsRuntimeAttributeEnableIdleProcessing;

	// add debugger attributes
	if (debug != nullptr && debug->enable)
		attrs |= JsRuntimeAttributeDispatchSetExceptionsToDebugger;
		
	// Create the runtime object - this represents a thread of execution,
	// heap, garbage collector, and compiler.
	if ((err = JsCreateRuntime(static_cast<JsRuntimeAttributes>(attrs), nullptr, &runtime)) != JsNoError)
		return Error(_T("JsCreateRuntime"));

	// Create the execution context - this represents the "global" object
	// at the root of the javascript namespace.
	if ((err = JsCreateContext(runtime, &ctx)) != JsNoError)
		return Error(_T("JsCreateContext"));

	// make the context current
	if ((err = JsSetCurrentContext(ctx)) != JsNoError)
		return Error(_T("JsSetCurrentContext"));

	// set the Promise continuation callback
	if ((err = JsSetPromiseContinuationCallback(&PromiseContinuationCallback, this)) != JsNoError)
		return Error(_T("JsSetPromiseContinuationCallback"));

	// Enable debugging if desired
	if (debug != nullptr && debug->enable)
	{
		// create the debug service
		if ((err = JsDebugServiceCreate(&debugService, debug->serviceName.c_str(), debug->serviceDesc.c_str(), 
			debug->favIcon, debug->favIconSize)) != JsNoError)
			return Error(_T("JsDebugServiceCreate"));

		// create the debugger protocol handler
		if ((err = JsDebugProtocolHandlerCreate(runtime, &debugProtocolHandler)) != JsNoError)
			return Error(_T("JsDebugProtocolHandlerCreate"));

		// register the protocol handler
		debugServiceName = debug->serviceName;
		if ((err = JsDebugServiceRegisterHandler(debugService, debugServiceName.c_str(), debugProtocolHandler, true)) != JsNoError)
			return Error(_T("JsDebugServiceRegisterHandler"));

		// listen for connections
		debugPort = debug->port;
		if ((err = JsDebugServiceListen(debugService, debugPort)) != JsNoError)
			return Error(_T("JsDebugServiceListen"));

		// if we're set to stop in the first user code executed, set the module load pause flag
		if (debug->initBreak == DebugOptions::UserCode)
			debugInitBreakPending = true;

		// if we're launching under the debugger, wait for the debugger to connect 
		// before proceeding
		if (debug->waitForDebugger)
		{
			// Set up a thread to show a message box while waiting
			class ConnectDialog : public RefCounted, public Dialog
			{
			public:
				ConnectDialog(DebugOptions &opts) : opts(opts) { }

				DebugOptions opts;

				static DWORD CALLBACK Main(LPVOID lParam)
				{
					// get 'this' into a ref pointer; the main thread already added
					// our reference, so we just need to release it on return
					RefPtr<ConnectDialog> self(static_cast<ConnectDialog*>(lParam));

					// Wait a few seconds before showing the message box, so that we
					// don't annoyingly flash the dialog up briefly if the connection
					// is fast, as it usually will be when launching as a child process.
					// If the event is signaled before we time out, it means that the
					// connection was established and we can skip the dialog.
					if (WaitForSingleObject(self->event, 2500) == WAIT_OBJECT_0)
						return 0;

					// show the dialog
					self->ShowWithMessageBoxFont(IDD_JS_DEBUG_WAIT);

					// exit after the dialog is dismissed
					return 0;
				}

				void Close()
				{
					// set the event, to cancel any wait loop
					SetEvent(event);

					// close the dialog, if opened
					if (hDlg != NULL)
						SendMessage(hDlg, WM_COMMAND, IDOK, 0);
				}

				virtual INT_PTR Proc(UINT message, WPARAM wParam, LPARAM lParam) override
				{
					switch (message)
					{
					case WM_INITDIALOG:
						FormatDlgItemText(IDC_TXT_PORT, opts.port);
						break;

					case WM_COMMAND:
						switch (LOWORD(wParam))
						{
						case IDOK:
							// OK - this is a message from self that the connection
							// successfully completed.  Simply dismiss the dialog.
							EndDialog(hDlg, IDOK);
							return 0;

						case IDCANCEL:
							// Cancel - this is the Quit button.  Abort the process.
							TerminateProcess(GetCurrentProcess(), 0);
							EndDialog(hDlg, IDCANCEL);
							return 0;
						}
					}

					return __super::Proc(message, wParam, lParam);
				}

				HandleHolder event = CreateEvent(NULL, FALSE, FALSE, NULL);
				DWORD tid = 0;
			};
			RefPtr<ConnectDialog> dlg(new ConnectDialog(debugOptions));

			// add a reference count on behalf of the thread, and fire off the thread
			dlg->AddRef();
			CreateThread(NULL, 0, &dlg->Main, dlg.Get(), 0, &dlg->tid);

			// wait for the debugger
			err = JsDebugProtocolHandlerWaitForDebugger(debugProtocolHandler);

			// no matter what happened, we're done with the dialog now
			dlg->Close();

			// check the connection result
			if (err != JsNoError)
				return Error(_T("JsDebugProtocolHandlerWaitForDebugger"));
		}

		// Set a callback to receive notification when an incoming network message
		// arrives.  The socket is read on a background thread, and incoming requests
		// are queued for processing on the main thread.  The callback posts a private
		// window message to our message window to notify it.  That event handler in
		// turn calls the protocol handler to read and process the queued messages.
		// The main window event loop runs on the main UI/script thread, of course, 
		// so this ensures that the message processing happens on the script thread.
		// The point of the cross-thread window message post is just to ensure that
		// we wake up and process the incoming requests quickly, rather than waiting
		// until we come back into the Javascript engine for some other reason.
		this->debugOptions = *debug;
		if ((err = JsDebugProtocolHandlerSetCommandQueueCallback(debugProtocolHandler, [](void *state) 
		{
			// notify the message window
			auto js = static_cast<JavascriptEngine*>(state);
			PostMessage(js->messageWindow.hwnd, js->messageWindow.debugEventMessageId, 0, 0);

		}, this)) != JsNoError)
			return Error(_T("JsDebugProtocolHandlerSetCommandQueueCallback"));

		// set up for the initial break mode
		switch (debugOptions.initBreak)
		{
		case DebugOptions::SystemCode:
			// The first break will be in the system startup code.  There's nothing 
			// extra to do; just let the debugger take control at the first break,
			// which will naturally happen when we load the first system script.
			break;

		case DebugOptions::UserCode:
			// Don't break until we reach the first user script.  We don't want the
			// debugger to take control when we enter the initial system code, so 
			// insert a "go" command into the command queue so that we start running 
			// again after the initial break.  We'll set up a new break later when
			// we're about to load the first user script.
			JsDebugProtocolHandlerSendRequest(debugProtocolHandler, "Debugger.go");
			break;

		case DebugOptions::None:
			// We don't want to stop at all, at least not interactively.  We *do*
			// still want an initial break, to allow the debugger to process setup
			// commands, but we don't want to stay in the debugger interactively.
			// So queue a "deferred go", which will take effect when the debugger
			// is about to enter interactive mode.
			JsDebugProtocolHandlerSendRequest(debugProtocolHandler, "Debugger.deferredGo");
			break;
		}
	}

	// Set up the module import host callbacks.  Note the catch-22
	// mentioned in the ChakraCore documentation: we have to do this
	// before importing the root module, but we need a module record
	// to set the callbacks; so we need a fake module record.
	JsModuleRecord fakeModRec;
	JsInitializeModuleRecord(nullptr, nullptr, &fakeModRec);

	// set the callbacks
	if ((err = JsSetModuleHostInfo(fakeModRec, JsModuleHostInfo_FetchImportedModuleCallback, &FetchImportedModule)) != JsNoError)
		return Error(_T("JsSetModuleHostInfo(FetchImportedModuleCallback)"));
	if ((err = JsSetModuleHostInfo(fakeModRec, JsModuleHostInfo_FetchImportedModuleFromScriptCallback, &FetchImportedModuleFromScript)) != JsNoError)
		return Error(_T("JsSetModuleHostInfo(FetchImportedModuleFromScriptCallback)"));
	if ((err = JsSetModuleHostInfo(fakeModRec, JsModuleHostInfo_NotifyModuleReadyCallback, &NotifyModuleReadyCallback)) != JsNoError)
		return Error(_T("JsSetModuleHostInfo(NotifyModuleReadyCallback)"));

	// initialize special values
	JsGetNullValue(&nullVal);
	JsGetUndefinedValue(&undefVal);
	JsIntToNumber(0, &zeroVal);
	JsGetFalseValue(&falseVal);
	JsGetTrueValue(&trueVal);
	JsGetGlobalObject(&globalObj);

	// Look up properties we reference
	JsCreatePropertyId("dispatchEvent", 13, &dispatchEventProp);

	// Initialize our internal symbol properties, which we use for private
	// properties of some of our objects.
	JsValueRef symName, symbol;
	JsPointerToString(L"Thunk", 5, &symName);
	JsCreateSymbol(symName, &symbol);
	JsAddRef(symbol, nullptr);
	JsGetPropertyIdFromSymbol(symbol, &callbackPropertyId);
	
	JsPointerToString(L"xref", 4, &symName);
	JsCreateSymbol(symName, &symbol);
	JsAddRef(symbol, nullptr);
	JsGetPropertyIdFromSymbol(symbol, &xrefPropertyId);

	// define system functions
	if (!DefineObjPropFunc(globalObj, "global", "_defineInternalType", &JavascriptEngine::DllImportDefineInternalType, this, eh)
		|| !DefineObjPropFunc(globalObj, "global", "createAutomationObject", &JavascriptEngine::CreateAutomationObject, this, eh)
		|| !DefineObjPropFunc(globalObj, "Variant", "Variant", &VariantData::Create, this, eh))
		return false;

	// add Variant prototype methods
	const TCHAR *where;
	if ((err = GetProp(Variant_class, globalObj, "Variant", where)) != JsNoError
		|| (err = GetProp(Variant_proto, Variant_class, "prototype", where)) != JsNoError
		|| (err = AddGetterSetter(Variant_proto, "vt", &VariantData::GetVt, this, &VariantData::SetVt, this, where)) != JsNoError
		|| (err = AddGetterSetter(Variant_proto, "value", &VariantData::GetValue, this, &VariantData::SetValue, this, where)) != JsNoError
		|| (err = AddGetterSetter(Variant_proto, "date", &VariantData::GetDate, this, &VariantData::SetDate, this, where)) != JsNoError
		|| (err = AddGetterSetter(Variant_proto, "boolVal", &VariantData::GetBool, this, &VariantData::SetBool, this, where)) != JsNoError
		|| (err = AddGetterSetter(Variant_proto, "bstrVal", &VariantData::GetBSTR, this, &VariantData::SetBSTR, this, where)) != JsNoError
		|| (err = AddGetterSetter(Variant_proto, "cyVal", &VariantData::GetCY, this, &VariantData::SetCY, this, where)) != JsNoError
		|| (err = AddGetterSetter(Variant_proto, "decVal", &VariantData::GetDecimal, this, &VariantData::SetDecimal, this, where)) != JsNoError
		|| (err = VariantData::AddNumGetSet<CHAR, VT_I1, &VARIANT::cVal>(this, "cVal", where)) != JsNoError
		|| (err = VariantData::AddNumGetSet<BYTE, VT_UI1, &VARIANT::bVal>(this, "bVal", where)) != JsNoError
		|| (err = VariantData::AddNumGetSet<SHORT, VT_I2, &VARIANT::iVal>(this, "iVal", where)) != JsNoError
		|| (err = VariantData::AddNumGetSet<USHORT, VT_UI2, &VARIANT::uiVal>(this, "uiVal", where)) != JsNoError
		|| (err = VariantData::AddNumGetSet<INT, VT_INT, &VARIANT::intVal>(this, "intVal", where)) != JsNoError
		|| (err = VariantData::AddNumGetSet<UINT, VT_UINT, &VARIANT::uintVal>(this, "uintVal", where)) != JsNoError
		|| (err = VariantData::AddNumGetSet<LONG, VT_I4, &VARIANT::lVal>(this, "lVal", where)) != JsNoError
		|| (err = VariantData::AddNumGetSet<SCODE, VT_ERROR, &VARIANT::scode>(this, "scode", where)) != JsNoError
		|| (err = VariantData::AddNumGetSet<ULONG, VT_UI4, &VARIANT::ulVal>(this, "ulVal", where)) != JsNoError
		|| (err = VariantData::AddNumGetSet<FLOAT, VT_R4, &VARIANT::fltVal>(this, "fltVal", where)) != JsNoError
		|| (err = VariantData::AddNumGetSet<DOUBLE, VT_R8, &VARIANT::dblVal>(this, "dblVal", where)) != JsNoError)
		return Error(MsgFmt(_T("initializing Variant prototype functions: %s"), where));

	// add references to the variant objects to keep them around
	JsAddRef(Variant_class, nullptr);
	JsAddRef(Variant_proto, nullptr);

	// set up the idle task
	AddTask(new IdleTask());

	// success
	inited = true;
	return true;
}

void JavascriptEngine::Terminate()
{
	// release the the global singleton
	if (inst != nullptr)
	{
		inst->Release();
		inst = nullptr;
	}
}

JavascriptEngine::~JavascriptEngine()
{
	// Explicitly clear the task queue.  Tasks can hold references to
	// Javascript objects, so we need to delete remaining task queue items
	// while the engine is still valid.
	taskQueue.clear();

	// Likewise, dispose of all native type cache entries, as these 
	// hold Javascript object references.
	nativeTypeCache.clear();

	// shut down the debug service
	if (debugProtocolHandler != nullptr)
	{
		JsDebugServiceUnregisterHandler(debugService, debugServiceName.c_str());
		JsDebugProtocolHandlerDestroy(debugProtocolHandler);
	}
	if (debugService != nullptr)
	{
		JsDebugServiceClose(debugService);
		JsDebugServiceDestroy(debugService);
	}
	
	// shut down the javascript runtime
	JsSetCurrentContext(JS_INVALID_REFERENCE);
	JsDisposeRuntime(runtime);
}

void JavascriptEngine::DebugConsoleLog(const TCHAR *type, const TCHAR *msg)
{
	if (debugProtocolHandler != nullptr)
	{
		JsValueRef argv[1];
		JsPointerToString(msg, _tcslen(msg), &argv[0]);
		JsDebugConsoleAPIEvent(debugProtocolHandler, TCHARToAnsi(type).c_str(), argv, countof(argv));
	}
}

bool JavascriptEngine::LoadModule(const TCHAR *url, ErrorHandler &eh)
{
	JsErrorCode err;
	auto Error = [&err, &eh](const TCHAR *where)
	{
		MsgFmt details(_T("%s failed: %s"), where, JsErrorToString(err));
		eh.SysError(LoadStringT(IDS_ERR_JSLOADMOD), details);
		LogFile::Get()->Write(LogFile::JSLogging, _T("[Javascript] Module load error: %s\n"), details.Get());
		return false;
	};

	// create a module record
	JsModuleRecord record;
	if ((err = FetchImportedModuleCommon(nullptr, L"[System]", TCHARToWide(url), &record)) != JsNoError)
		return Error(_T("Fetching main module"));

	// success
	return true;
}

void JavascriptEngine::OnDebugMessageQueued()
{
	// process queued commands
	if (debugProtocolHandler != nullptr)
	{
		JavascriptScope jsc;
		JsDebugProtocolHandlerProcessCommandQueue(debugProtocolHandler);
	}
}

bool JavascriptEngine::EvalScript(const WCHAR *scriptText, const WCHAR *url, JsValueRef *returnVal, ErrorHandler &eh)
{
	// we're entering Javascript scope
	JavascriptScope jsc;

	JsErrorCode err;
	auto Error = [&err, &eh](const TCHAR *where)
	{
		MsgFmt details(_T("%s failed: %s"), where, JsErrorToString(err));
		eh.SysError(LoadStringT(IDS_ERR_JSRUN), details);
		LogFile::Get()->Write(LogFile::JSLogging, _T("[Javascript] Script error: %s\n"), details.Get());
		return false;
	};

	// create a cookie to represent the script
	auto const *cookie = &sourceCookies.emplace_back(TCHARToWide(url));

	// run the script
	if ((err = JsRunScript(scriptText, reinterpret_cast<JsSourceContext>(cookie), TCHARToWCHAR(url), returnVal)) != JsNoError 
		&& err != JsErrorScriptException && err != JsErrorScriptCompile)
		return Error(_T("JsRunScript"));

	// check for thrown exceptions
	bool isExc = false;
	if ((err = JsHasException(&isExc)) != JsNoError)
		return Error(_T("JsHasException"));

	if (isExc)
	{
		// log the exception
		if ((err = LogAndClearException(&eh, IDS_ERR_JSRUN)) != JsNoError)
			return false;
	}

	// success
	return true;
}

JsErrorCode JavascriptEngine::LogAndClearException(ErrorHandler *eh, int msgid)
{
	JsErrorCode err;
	auto Error = [&err, eh, msgid](const TCHAR *where)
	{
		// format the technical details
		MsgFmt details(_T("%s failed: %s"), where, JsErrorToString(err));

		// log the error through the error handler, if provided
		if (eh != nullptr)
			eh->SysError(LoadStringT(msgid), details);

		// log it to the log file as well
		LogFile::Get()->Write(LogFile::JSLogging, _T("[Javascript] Script execution error: %s\n"), details.Get());
		return err;
	};

	// retrieve the exception with metadata
	JsValueRef md;
	if ((err = JsGetAndClearExceptionWithMetadata(&md)) != JsNoError)
		return Error(_T("JsGetAndClearExceptionWithMetadata"));

	// detailed error message retrieving exception metadata properties
	const TCHAR *where;
	auto ExcError = [&err, &where, &eh, &Error](const TCHAR *prop)
	{
		return Error(MsgFmt(_T("%s, getting property from exception metadata"), where));
	};

	// retrieve the exception metadata
	int lineno, colno;
	JsValueRef exc;
	TSTRING msg, url, source;
	if ((err = GetProp(lineno, md, "line", where)) != JsNoError)
		return ExcError(_T("line"));
	if ((err = GetProp(colno, md, "column", where)) != JsNoError)
		return ExcError(_T("column"));
	if ((err = GetProp(source, md, "source", where)) != JsNoError)
		return ExcError(_T("exception.source"));
	if ((err = GetProp(url, md, "url", where)) != JsNoError)
		return ExcError(_T("url"));
	if ((err = GetProp(exc, md, "exception", where)) != JsNoError)
		return ExcError(_T("exception"));

	// try getting the exception's .message property
	if ((err = GetProp(msg, exc, "message", where)) != JsNoError)
	{
		// no good - try just converting the exception to a string value
		JsValueRef excAsStr;
		if ((err = JsConvertValueToString(exc, &excAsStr)) == JsNoError)
		{
			const wchar_t *p;
			size_t len;
			JsStringToPointer(excAsStr, &p, &len);
			msg.assign(p, len);
		}
		else
		{
			// just show a generic error
			msg = _T("<no exception message available>");
		}
	}

	// Try getting a stack trace from exception.stack.  If this value is
	// present, we can convert it to string for a printable stack trace.
	// This is often more helpful than just the immediate error location,
	// which is all we can get from the metadata.
	JsValueRef stackObj;
	JsValueType stackType;
	TSTRING stack;
	if (GetProp(stackObj, exc, "stack", where) == JsNoError
		&& JsGetValueType(stackObj, &stackType) == JsNoError
		&& stackType != JsUndefined)
		GetProp(stack, exc, "stack", where);

	// report the scripting error through the error handler, if provided
	if (eh != nullptr)
		eh->Error(MsgFmt(IDS_ERR_JSEXC, msg.c_str(), url.c_str(), lineno + 1, colno + 1));

	// Log the error to the log file.  If a stack trace is available, it incorporates
	// the error message and source location, so we just need to show that; otherwise,
	// show the other metadata explicitly.
	LogFile::Get()->Group(LogFile::JSLogging);
	if (stack.length() != 0)
	{
		// the stack trace has all we need to know
		LogFile::Get()->Write(_T("[Javascript]: Uncaught exception:\n%s\n\n"), stack.c_str());
	}
	else
	{
		// no stack trace; log the metadata manually
		LogFile::Get()->Write(LogFile::JSLogging,
			_T("[Javascript] Uncaught exception: %s\n")
			_T("In %s (line %d, col %d)\n")
			_T("Source code: %s\n\n"),
			msg.c_str(), url.c_str(), lineno + 1, colno + 1, source.c_str());
	}

	// success
	return JsNoError;
}

void JavascriptEngine::CallException::Log(const TCHAR *logFileDesc, ErrorHandler *eh)
{
	// log the CallException details
	LogFile::Get()->Write(LogFile::JSLogging, _T("%s: %hs\n"), logFileDesc != nullptr ? logFileDesc : _T("Javascript error"), what());

	// log the Javascript exception details, if available
	auto js = JavascriptEngine::Get();
	if (js->HasException())
		js->LogAndClearException(eh);
}

bool JavascriptEngine::IsFalsy(JsValueRef val) const
{
	JsValueRef boolval;
	bool b;
	return (JsConvertValueToBoolean(val, &boolval) != JsNoError
		|| JsBooleanToBool(boolval, &b) != JsNoError
		|| !b);
}

JsErrorCode JavascriptEngine::ToString(TSTRING &s, const JsValueRef &val)
{
	// convert the value to string
	JsValueRef sval;
	JsErrorCode err;
	if ((err = JsConvertValueToString(val, &sval)) != JsNoError)
		return err;

	// retrieve the string buffer
	const wchar_t *pstr;
	size_t len;
	if ((err = JsStringToPointer(sval, &pstr, &len)) != JsNoError)
		return err;

	// convert to a TSTRING
	WSTRING wstr(pstr, len);
	s = WSTRINGToTSTRING(wstr);

	// success
	return JsNoError;
}

JsErrorCode JavascriptEngine::ToInt(int &i, const JsValueRef &val)
{
	// convert to numeric
	JsErrorCode err;
	JsValueRef numval;
	if ((err = JsConvertValueToNumber(val, &numval)) != JsNoError)
		return err;

	// convert to native int
	return JsNumberToInt(numval, &i);
}

JsErrorCode JavascriptEngine::ToDouble(double &d, const JsValueRef &val)
{
	// convert to numeric
	JsErrorCode err;
	JsValueRef numval;
	if ((err = JsConvertValueToNumber(val, &numval)) != JsNoError)
		return err;

	// convert to native int
	return JsNumberToDouble(numval, &d);
}

JsErrorCode JavascriptEngine::ToFloat(float &f, const JsValueRef &val)
{
	double d;
	JsErrorCode err;
	if ((err = ToDouble(d, val)) != JsNoError)
		return err;

	f = static_cast<float>(d);
	return JsNoError;
}

JsErrorCode JavascriptEngine::ToBool(bool &b, const JsValueRef &val)
{
	// convert to boolean
	JsErrorCode err;
	JsValueRef boolval;
	if ((err = JsConvertValueToBoolean(val, &boolval)) != JsNoError)
		return err;

	// convert to native bool
	return JsBooleanToBool(boolval, &b);
}

JsErrorCode JavascriptEngine::VariantDateToJsDate(DATE date, JsValueRef &result)
{
	// Variant Dates are extremely tricky to work with because of poor
	// design choices Microsoft made when defining the type.  (That's
	// not an opinion; see the Microsoft system blogs if you want an
	// accounting of the design flaws.)  Fortunately, Windows provides
	// an API that encpasulates all of the tricky handling and converts
	// to SYSTEMTIME, which is a perfectly sane date representation.
	SYSTEMTIME st;
	VariantTimeToSystemTime(date, &st);

	// SYSTEMTIME has a calendar date structure, whereas Javascript's
	// Date representation is a linear offset from an epoch.  So the
	// next step is to convert the SYSTEMTIME to a similar linear time
	// format.  The Windows format fitting that description is FILETIME.
	// (There's no loss of fidelity in this conversion.)
	FILETIME ft;
	SystemTimeToFileTime(&st, &ft);

	// And finally, convert the FILETIME to a Javascript Date, which is
	// a straightforward conversion between linear offset formulas.
	return FileTimeToJsDate(ft, result);
}

JsErrorCode JavascriptEngine::DateTimeToJsDate(const DateTime &date, JsValueRef &jsval)
{
	// convert from the internal FILETIME representation
	return FileTimeToJsDate(date.GetFileTime(), jsval);
}

JsErrorCode JavascriptEngine::FileTimeToJsDate(const FILETIME &ft, JsValueRef &jsval)
{
	// DateTime represents a date as a Windows FILETIME, which is the number
	// of 100-nanosecond intervals since January 1, 1601 00:00:00 UTC, as a
	// 64-bit int.  Javascript dates are the number of milliseconds since
	// January 1, 1970 00:00:00 UTC.  If we ignore leap seconds, it's fairly
	// easy to convert between these, since they're both linear time since
	// an epoch:
	//
	// Step 1: Make the units match.  Convert from nanoseconds since the 
	// FILETIME epoch to milliseconds.  1ms = 1000000ns = 10000*100ns = 10000hns
	// ("hns" for "hundreds of nanoseconds").
	INT64 hnsSinceFtEpoch = static_cast<INT64>((static_cast<UINT64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime);
	INT64 msSinceFtEpoch = hnsSinceFtEpoch / 10000;

	// Step 2: Rebase from the FILETIME epoch to the Unix epoch.  The
	// FILETIME epoch is 11644473600000 milliseconds before the Unix epoch.
	const INT64 unixEpochMinusFiletimeEpoch = 11644473600000;
	INT64 msSinceUnixEpoch = msSinceFtEpoch - unixEpochMinusFiletimeEpoch;

	// Now create a Javascript Date value representing this number of 
	// milliseconds since the Unix epoch.  This is straightforward, as the
	// js Date constructor accepts exactly this format as an argument.
	auto js = Get();
	JsErrorCode err;
	const TCHAR *where;
	JsValueRef dateFunc, ms, argv[2];
	if ((err = js->GetProp(dateFunc, js->globalObj, "Date", where)) != JsNoError
		|| (err = JsDoubleToNumber(static_cast<double>(msSinceUnixEpoch), &ms)) != JsNoError
		|| (argv[0] = js->globalObj, argv[1] = ms, (err = JsConstructObject(dateFunc, argv, 2, &jsval)) != JsNoError))
		return err;

	// success
	return JsNoError;
}

JsErrorCode JavascriptEngine::JsDateToVariantDate(JsValueRef jsval, DATE &date)
{
	// Our goal is to convert to Variant Date format, which is another
	// units-since-an-epoch format.  But we won't attempt the conversion
	// with our own arithmetic, because Variant Dates are notoriously
	// difficult to handle correctly.  (The format is poorly defined
	// in several ways that makes its arithmetic non-linear.  Not least
	// of which is that Variant Dates are expressed in "local time", 
	// whatever that happens to be on the local system, which adds
	// complexity due to daylight time.)  Fortunately, there's a system
	// API, SystemTimeToVariantTime(), that encapsulates all of the
	// special handling required and produces a Variant Date given a 
	// UTC value in the perfectly sane SYSTEMTIME format.  So we do
	// this conversion in three easy steps: convert Javascript Date to
	// Windows FILETIME, which is a straightforward linear conversion
	// from one offset-from-epoch representation to another; convert
	// the FILETIME to a SYSTEMTIME, which is another system API that
	// just works; and then convert that to the evil target format.

	// Start by converting the Javascript Date to a FILETIME
	FILETIME ft;
	JsErrorCode err;
	if ((err = JsDateToFileTime(jsval, ft)) != JsNoError)
		return err;

	// Now convert the FILETIME to SYSTEMTIME
	SYSTEMTIME st;
	FileTimeToSystemTime(&ft, &st);

	// And finally, convert the SYSTEMTIME to Variant Date
	SystemTimeToVariantTime(&st, &date);

	// success
	return JsNoError;
}

JsErrorCode JavascriptEngine::JsDateToFileTime(JsValueRef jsval, FILETIME &ft)
{
	// Date.prototype.valueOf returns the primitive date value, as a
	// Javascript Number (== C++ double) representing the number of
	// milliseconds since the Unix epoch.
	auto js = inst;
	JsErrorCode err;
	JsValueRef valueOfFunc, value;
	double msSinceUnixEpoch;
	const TCHAR *where;
	if ((err = js->GetProp(valueOfFunc, jsval, "valueOf", where)) != JsNoError
		|| (err = JsCallFunction(valueOfFunc, &jsval, 1, &value)) != JsNoError
		|| (err = JsNumberToDouble(value, &msSinceUnixEpoch)) != JsNoError)
		return err;

	// Javascript Date and FILETIME are both offset-from-epoch formats,
	// but they have different epochs and are in different units.  So
	// the first step is to convert from "milliseconds since the Unix
	// epoch" (January 1, 1970, 00:00:00 GMT), as Javascript Dates would
	// have it, to "milliseconds since the FILETIME epoch" (January 1,
	// 1601, 00:00:00 UTC).  Note that the two epochs are fixed points
	// in universal time, so converting between the two reference points
	// is just a matter of adding/subtracting the interval between them,
	// which is 11644473600000 milliseconds.  (Not counting leap seconds,
	// which Javascript Dates by policy ignore.  The situation appears
	// to be more complicated on Windows, where leap seconds will be
	// incorporated into conversions between FILETIME and SYSTEMTIME
	// in post-10/2018 Windows 10 updates.  So we might start seeing
	// some disagreement between human-readable local time values in
	// the two systems, by single-digit seconds, at some point.)
	const INT64 unixEpochMinusFiletimeEpoch = 11644473600000;
	INT64 msSinceFiletimeEpoch = static_cast<INT64>(msSinceUnixEpoch) + unixEpochMinusFiletimeEpoch;

	// The one other difference with FILETIMEs is that they're in units
	// of 100ns, vs Javascript Date's milliseconds.  There are 10000
	// 100ns intervals in 1ms.  ("hns" = "hundreds of nanoseconds".)
	INT64 hnsSinceFiletimeEpoch = msSinceFiletimeEpoch * 10000;

	// We conceptually have a FILETIME value now: the number of 100ns
	// intervals since the FILETIME epoch, as a 64-bit integer.  All
	// that's left is to decompose our flat 64-bit integer into the
	// high and low 32-bit halves that go in the FILETIME struct.
	ft.dwHighDateTime = static_cast<DWORD>(hnsSinceFiletimeEpoch >> 32);
	ft.dwLowDateTime = static_cast<DWORD>(hnsSinceFiletimeEpoch & 0xFFFFFFFF);

	// success
	return JsNoError;
}

JsErrorCode JavascriptEngine::JsDateToDateTime(JsValueRef jsval, DateTime &date)
{
	DATE vardate;
	if (auto err = JsDateToVariantDate(jsval, vardate); err != JsNoError)
		return err;

	date = DateTime(date);
	return JsNoError;
}

JsValueRef JavascriptEngine::Throw(JsErrorCode err)
{
	// create an exception message
	MsgFmt msg(IDS_ERR_JSERR, JsErrorToString(err));
	JsValueRef str;
	JsPointerToString(msg.Get(), _tcslen(msg.Get()), &str);

	// create an exception object for the error
	JsValueRef exc;
	JsCreateError(str, &exc);

	// set the error state
	JsSetException(exc);

	// return 'undefined'
	return undefVal;
}

JsValueRef JavascriptEngine::Throw(JsErrorCode err, const TCHAR *cbName)
{
	// for script execution errors, log the error
	if (err == JsErrorScriptException)
		LogAndClearException();

	// create an exception message
	MsgFmt msg(IDS_ERR_JSCB, JsErrorToString(err), cbName);
	JsValueRef str;
	JsPointerToString(msg.Get(), _tcslen(msg.Get()), &str);

	// create an exception object for the error
	JsValueRef exc;
	JsCreateError(str, &exc);

	// set the error state
	JsSetException(exc);

	// return 'undefined'
	return undefVal;
}

JsValueRef JavascriptEngine::Throw(const TCHAR *errorMessage)
{
	// create the JS string
	JsValueRef str;
	JsPointerToString(errorMessage, _tcslen(errorMessage), &str);

	// create an exception object for the error
	JsValueRef exc;
	JsCreateError(str, &exc);

	// set the error state
	JsSetException(exc);

	// return 'undefined'
	return undefVal;
}

JsValueRef JavascriptEngine::ThrowSimple(const char *msg)
{
	JsValueRef str, exc;
	JsCreateString(msg, strlen(msg), &str);
	JsCreateError(str, &exc);
	JsSetException(exc);

	JsValueRef undef;
	JsGetUndefinedValue(&undef);
	return undef;
}

bool JavascriptEngine::HasException()
{
	bool exc = false;
	return JsHasException(&exc) == JsNoError && exc;
}

JsErrorCode JavascriptEngine::GetProp(int &intval, JsValueRef obj, const CHAR *prop, const TCHAR* &where)
{
	// look up the property in JsValueRef format
	JsErrorCode err;
	JsValueRef val;
	if ((err = GetProp(val, obj, prop, where)) != JsNoError)
		return err;

	// get the integer value
	JsValueRef numval;
	if ((err = JsConvertValueToNumber(val, &numval)) != JsNoError)
	{
		where = _T("JsConvertValueToNumber");
		return err;
	}
	if ((err = JsNumberToInt(numval, &intval)) != JsNoError)
	{
		where = _T("JsNumberToInt");
		return err;
	}

	// success
	return JsNoError;
}

JsErrorCode JavascriptEngine::GetProp(double &dblval, JsValueRef obj, const CHAR *prop, const TCHAR* &where)
{
	// look up the property in JsValueRef format
	JsErrorCode err;
	JsValueRef val;
	if ((err = GetProp(val, obj, prop, where)) != JsNoError)
		return err;

	// get the integer value
	JsValueRef numval;
	if ((err = JsConvertValueToNumber(val, &numval)) != JsNoError)
	{
		where = _T("JsConvertValueToNumber");
		return err;
	}
	if ((err = JsNumberToDouble(numval, &dblval)) != JsNoError)
	{
		where = _T("JsNumberToDouble");
		return err;
	}

	// success
	return JsNoError;
}

JsErrorCode JavascriptEngine::GetProp(TSTRING &strval, JsValueRef obj, const CHAR *prop, const TCHAR* &where)
{
	// look up the property in JsValueRef format
	JsErrorCode err;
	JsValueRef val;
	if ((err = GetProp(val, obj, prop, where)) != JsNoError)
		return err;

	// convert to string
	JsValueRef jstrval;
	if ((err = JsConvertValueToString(val, &jstrval)) != JsNoError)
	{
		where = _T("JsConvertValueToString");
		return err;
	}

	// retrieve the string buffer
	const wchar_t *pstr;
	size_t len;
	if ((err = JsStringToPointer(jstrval, &pstr, &len)) != JsNoError)
	{
		where = _T("JsStringToPointer");
		return err;
	}

	// convert to a TSTRING
	WSTRING wstr(pstr, len);
	strval = WSTRINGToTSTRING(wstr);

	// success
	return JsNoError;
}

JsErrorCode JavascriptEngine::GetProp(JsValueRef &val, JsValueRef obj, const CHAR *propName, const TCHAR* &where)
{
	// create the property ID
	JsErrorCode err;
	JsPropertyIdRef propId;
	if ((err = JsCreatePropertyId(propName, strlen(propName), &propId)) != JsNoError)
	{
		where = _T("JsCreatePropertyId");
		return err;
	}

	// retrieve the property value
	if ((err = JsGetProperty(obj, propId, &val)) != JsNoError)
	{
		where = _T("JsGetProperty");
		return err;
	}

	// success
	return JsNoError;
}

bool JavascriptEngine::CreateObj(JsValueRef &obj)
{
	if (JsErrorCode err = JsCreateObject(&obj); err != JsNoError)
		return Throw(err, _T("JsCreateObj")), false;

	return true;
}

bool JavascriptEngine::CreateObjWithProto(JsValueRef &obj, JsValueRef proto)
{
	JsErrorCode err;
	if ((err = JsCreateObject(&obj)) != JsNoError
		|| (err = JsSetPrototype(obj, proto)) != JsNoError)
		return Throw(err, _T("CreateObjWithProto")), false;

	return true;
}

bool JavascriptEngine::CreateArray(JsValueRef &arr)
{
	if (JsErrorCode err = JsCreateArray(0, &arr); err != JsNoError)
		return Throw(err, _T("JsCreateArray")), false;

	return true;
}

JsErrorCode JavascriptEngine::ArrayPush(JsValueRef &arr, JsValueRef ele)
{
	JsPropertyIdRef propkey;
	JsValueRef propval;
	JsErrorCode err;
	if ((err = JsCreatePropertyId("push", 4, &propkey)) != JsNoError
		|| (err = JsGetProperty(arr, propkey, &propval)) != JsNoError)
		return err;

	JsValueRef argv[2] = { arr, ele };
	JsValueRef result;
	if ((err = JsCallFunction(propval, argv, countof(argv), &result)) != JsNoError)
		return err;

	return JsNoError;
}

bool JavascriptEngine::SetProp(JsValueRef obj, const CHAR *prop, JsValueRef val)
{
	JsErrorCode err;
	JsPropertyIdRef propkey;
	if ((err = JsCreatePropertyId(prop, strlen(prop), &propkey)) != JsNoError
		|| (err = JsSetProperty(obj, propkey, val, true)) != JsNoError)
		return Throw(err, _T("SetProp")), false;

	return true;
}

bool JavascriptEngine::SetProp(JsValueRef obj, const CHAR *prop, int val)
{
	JsErrorCode err;
	JsValueRef jsval;
	if ((err = JsIntToNumber(val, &jsval)) != JsNoError)
		return Throw(err, _T("SetProp(int)")), false;
	return SetProp(obj, prop, jsval);
}

bool JavascriptEngine::SetProp(JsValueRef obj, const CHAR *prop, bool val)
{
	JsErrorCode err;
	JsValueRef jsval;
	if ((err = JsBoolToBoolean(val, &jsval)) != JsNoError)
		return Throw(err, _T("SetProp(bool)")), false;
	return SetProp(obj, prop, jsval);
}

bool JavascriptEngine::SetProp(JsValueRef obj, const CHAR *prop, double val)
{
	JsErrorCode err;
	JsValueRef jsval;
	if ((err = JsDoubleToNumber(val, &jsval)) != JsNoError)
		return Throw(err, _T("SetProp(double)")), false;
	return SetProp(obj, prop, jsval);
}

bool JavascriptEngine::SetProp(JsValueRef obj, const CHAR *prop, const WCHAR *val)
{
	JsErrorCode err;
	JsValueRef jsval;
	if ((err = JsPointerToString(val, wcslen(val), &jsval)) != JsNoError)
		return Throw(err, _T("SetProp(int)")), false;
	return SetProp(obj, prop, jsval);
}

bool JavascriptEngine::SetProp(JsValueRef obj, const CHAR *prop, const WSTRING &val)
{
	JsErrorCode err;
	JsValueRef jsval;
	if ((err = JsPointerToString(val.c_str(), val.length(), &jsval)) != JsNoError)
		return Throw(err, _T("SetProp(int)")), false;
	return SetProp(obj, prop, jsval);
}

JsErrorCode JavascriptEngine::SetReadonlyProp(JsValueRef object, const CHAR *propName, JsValueRef propVal, const TCHAR* &where)
{
	JsErrorCode err = JsNoError;
	auto Check = [&err, &where](JsErrorCode errIn, const TCHAR *msg)
	{
		if (errIn != JsNoError)
		{
			err = errIn;
			where = msg;
			return false;
		}
		return true;
	};

	// The basic operation to create a readonly property is
	//
	//   Object.defineProperty(<object>, <propertyName>, { value: <value>, enumerable: true })
	//
	// The absence of a 'writable' makes it read-only.
	//
	JsValueRef descriptor;
	JsValueRef propNameStr;
	bool result;
	if (!Check(JsCreateObject(&descriptor), _T("JsCreateObject(property descriptor)"))

		|| !Check(JsCreateString("value", 5, &propNameStr), _T("JsCreateString(value)"))
		|| !Check(JsObjectSetProperty(descriptor, propNameStr, propVal, true), _T("JsObjectSetProperty(value)"))

		|| !Check(JsCreateString("enumerable", 10, &propNameStr), _T("JsCreateString(enumerable)"))
		|| !Check(JsObjectSetProperty(descriptor, propNameStr, trueVal, true), _T("JsObjectSetProperty(enumerable)"))

		|| !Check(JsCreateString(propName, strlen(propName), &propNameStr), _T("JsCreateString(propName)"))
		|| !Check(JsObjectDefineProperty(object, propNameStr, descriptor, &result), _T("JsObjectDefineProperty")))
		return err;

	return JsNoError;
}

JsErrorCode JavascriptEngine::AddGetterSetter(JsValueRef object, const CHAR *propName,
	JsNativeFunction getter, void *getterCtx,
	JsNativeFunction setter, void *setterCtx,
	const TCHAR* &where)
{
	JsValueRef jsGetter = JS_INVALID_REFERENCE, jsSetter = JS_INVALID_REFERENCE;
	JsErrorCode err;
	if (getter != nullptr && (err = JsCreateFunction(getter, getterCtx, &jsGetter)) != JsNoError)
	{
		where = _T("creating native getter function");
		return err;
	}

	if (setter != nullptr && (err = JsCreateFunction(setter, setterCtx, &jsSetter)) != JsNoError)
	{
		where = _T("creating native setter function");
		return err;
	}

	return AddGetterSetter(object, propName, jsGetter, jsSetter, where);
}


JsErrorCode JavascriptEngine::AddGetterSetter(JsValueRef object, const CHAR *propName, JsValueRef getter, JsValueRef setter, const TCHAR* &where)
{
	JsErrorCode err = JsNoError;
	auto Check = [&err, &where](JsErrorCode errIn, const TCHAR *msg)
	{
		if (errIn != JsNoError)
		{
			err = errIn;
			where = msg;
			return false;
		}
		return true;
	};

	// The basic operation to create a getter/setter is
	//
	//   Object.defineProperty(<object>, <propertyName>, { get: <getter>, set: <setter>, enumerable: true })
	//
	JsValueRef desc;
	JsValueRef propstr;
	if (!Check(JsCreateObject(&desc), _T("CreateObject"))

		|| !Check(JsCreateString("enumerable", 10, &propstr), _T("CreateString(enumerable)"))
		|| !Check(JsObjectSetProperty(desc, propstr, trueVal, true), _T("SetProp(enumerable)")))
		return err;

	if (getter != JS_INVALID_REFERENCE)
	{
		if (!Check(JsCreateString("get", 3, &propstr), _T("CreateString(get)"))
			|| !Check(JsObjectSetProperty(desc, propstr, getter, true), _T("SetProp(get)")))
			return err;
	}

	if (setter != JS_INVALID_REFERENCE)
	{
		if (!Check(JsCreateString("set", 3, &propstr), _T("CreateString(set)"))
			|| !Check(JsObjectSetProperty(desc, propstr, setter, true), _T("SetProp(set)")))
			return err;
	}

	bool ok;
	if (!Check(JsCreateString(propName, strlen(propName), &propstr), _T("CreateString(propName)"))
		|| !Check(JsObjectDefineProperty(object, propstr, desc, &ok), _T("ObjectDefineProperty()")))
		return err;

	return JsNoError;
}

bool JavascriptEngine::DefineGlobalFunc(const CHAR *name, NativeFunctionBinderBase *func, ErrorHandler &eh)
{
	JsErrorCode err;
	auto Error = [name, &err, &eh](const TCHAR *where)
	{
		eh.SysError(LoadStringT(IDS_ERR_JSINITHOST), MsgFmt(_T("Setting up native function callback for global.%hs: %s failed: %s"),
			name, where, JsErrorToString(err)));
		return false;
	};

	// define the object property
	return DefineObjPropFunc(globalObj, "global", name, func, eh);
}

bool JavascriptEngine::DefineObjPropFunc(JsValueRef obj, const CHAR *objName, const CHAR *propName, NativeFunctionBinderBase *func, ErrorHandler &eh)
{
	// set the name in the binder object
	func->callbackName = propName;

	// define the property
	return DefineObjPropFunc(obj, objName, propName, &NativeFunctionBinderBase::SInvoke, func, eh);
}

bool JavascriptEngine::DefineGetterSetter(JsValueRef obj, const CHAR *objName, const CHAR *propName,
	NativeFunctionBinderBase *getter, NativeFunctionBinderBase *setter, ErrorHandler &eh)
{
	JsErrorCode err;
	auto Error = [objName, propName, &err, &eh](const TCHAR *where)
	{
		eh.SysError(LoadStringT(IDS_ERR_JSINITHOST), MsgFmt(_T("Setting up native getter/setter for %hs.%hs: %s failed: %s"),
			objName, propName, where, JsErrorToString(err)));
		return false;
	};

	// set up the native functions
	auto Init = [&eh, &err, &Error, objName, propName](JsValueRef &jsfunc, NativeFunctionBinderBase *func, CSTRING prefix)
	{
		// skip it if this one isn't used
		if (func == nullptr)
		{
			jsfunc = JS_INVALID_REFERENCE;
			return true;
		}

		// set the name
		func->callbackName = prefix + propName;

		// create the function name value
		JsValueRef nameval;
		MsgFmt name(_T("%hs.%hs"), objName, propName);
		JsErrorCode err;
		if ((err = JsPointerToString(name.Get(), wcslen(name.Get()), &nameval)) != JsNoError)
			return Error(_T("JsPointerToString"));

		// create the native function wrapper
		if ((err = JsCreateNamedFunction(nameval, &NativeFunctionBinderBase::SInvoke, func, &jsfunc)) != JsNoError)
			return Error(_T("JsCreateFunction"));

		// success
		return true;
	};

	JsValueRef jsGetter, jsSetter;
	if (!Init(jsGetter, getter, "get_") || !Init(jsSetter, setter, "set_"))
		return false;

	// add the getter/setter
	const TCHAR *where;
	if ((err = AddGetterSetter(obj, propName, jsGetter, jsSetter, where)) != JsNoError)
		return Error(where);

	// success
	return true;
}


bool JavascriptEngine::DefineObjPropFunc(JsValueRef obj, const CHAR *objName, const CHAR *propName, JsNativeFunction func, void *context, ErrorHandler &eh)
{
	JsErrorCode err;
	auto Error = [objName, propName, &err, &eh](const TCHAR *where)
	{
		eh.SysError(LoadStringT(IDS_ERR_JSINITHOST), MsgFmt(_T("Setting up native function callback for %hs.%hs: %s failed: %s"),
			objName, propName, where, JsErrorToString(err)));
		return false;
	};

	// create the property by name
	JsPropertyIdRef propId;
	if ((err = JsCreatePropertyId(propName, strlen(propName), &propId)) != JsNoError)
		return Error(_T("JsCreatePropertyId"));

	// create the function name value
	JsValueRef nameval;
	MsgFmt name(_T("%hs.%hs"), objName, propName);
	if ((err = JsPointerToString(name.Get(), wcslen(name.Get()), &nameval)) != JsNoError)
		return Error(_T("JsPointerToString"));

	// create the native function wrapper
	JsValueRef funcval;
	if ((err = JsCreateNamedFunction(nameval, func, context, &funcval)) != JsNoError)
		return Error(_T("JsCreateFunction"));

	// set the object property
	if ((err = JsSetProperty(obj, propId, funcval, true)) != JsNoError)
		return Error(_T("JsSetProperty"));

	// success
	return true;
}

void CALLBACK JavascriptEngine::PromiseContinuationCallback(JsValueRef task, void *ctx)
{
	// add the task
	reinterpret_cast<JavascriptEngine*>(ctx)->AddTask(new PromiseTask(task));
}

void JavascriptEngine::AddTask(Task *task)
{
	// add the task
	taskQueue.emplace_back(task);

	// update the message window timer, if affected
	UpdateTaskTimer();
}

void JavascriptEngine::UpdateTaskTimer()
{
	if (IsTaskPending())
	{
		// get the next scheduled task time
		ULONGLONG tNext = GetNextTaskTime();

		// figure the elapsed time to the next task time
		ULONGLONG tNow = GetTickCount64();
		ULONGLONG dt64 = tNext <= tNow ? 0 : tNext - tNow;

		// The window timer can only hold a UINT, so limit the interval
		// to UINT_MAX.  This will result in a premature timer event, 
		// but that won't result in any incorrect behavior because the
		// event processor won't run any tasks that aren't actually ready
		// at that point; and it won't cause excessive performance impact,
		// because the premature events along the way will only occur
		// once every 49.7 days.  So we'll do an unnecessary queue scan
		// every 49.7 days until the actual event occurs.
		UINT dt = (UINT)(dt64 > UINT_MAX ? UINT_MAX : dt64);

		// schedule a timer event
		SetTimer(messageWindow.hwnd, messageWindow.timerId, dt, NULL);
	}
	else
	{
		// no tasks are pending
		KillTimer(messageWindow.hwnd, messageWindow.timerId);
	}
}

void JavascriptEngine::EnumTasks(std::function<bool(Task*)> func)
{
	for (auto &task : taskQueue)
	{
		if (!func(task.get()))
			break;
	}
}

ULONGLONG JavascriptEngine::GetNextTaskTime()
{
	// Start with a time so far in the future that it will never occur.
	// Since we use 64-bit millisecond timestamps, there's truly zero 
	// chance of a rollover ever occurring.  It's a cliche at this point
	// to laugh when someone claims a limit in a computer program will
	// never be breached, but in this case it's actually realistic to
	// say so.  64 bits worth of milliseconds is 584 million years.
	// I'm going to go out on a limb and say that the probability that
	// a Windows system will ever go that long between reboots isn't 
	// just very small, it's actually zero.
	ULONGLONG nextReadyTime = MAXULONGLONG;

	// Now scan the queue looking for tasks with earlier ready times.
	for (auto const& task : taskQueue)
	{
		if (task->readyTime < nextReadyTime)
			nextReadyTime = task->readyTime;
	}

	// return the earliest next ready time we found
	return nextReadyTime;
}

bool JavascriptEngine::RunTasks() 
{
	// no tasks have been executed yet
	bool tasksExecuted = false;

	// only process tasks when we're not in a recursive Javascript invocation
	if (inJavascript == 0)
	{
		// count the tasks as entering Javascript scope
		JavascriptScope jsc;

		// scan the task queue
		for (auto it = taskQueue.begin(); it != taskQueue.end(); )
		{
			// remember the next task, in case we remove this one
			auto nxt = it;
			nxt++;

			// get the task object
			Task *task = it->get();

			// presume we'll keep the task
			bool keep = true;

			// check the task status
			if (task->canceled)
			{
				// The task has been canceled.  Simply delete it from the
				// queue without invoking it.
				keep = false;
			}
			else if (GetTickCount64() >= task->readyTime)
			{
				// The task is ready to run.  Execute it.
				keep = task->Execute();

				// note that at least one task has been executed
				tasksExecuted = true;
			}

			// If we're not keeping the task, remove it
			if (!keep)
				taskQueue.erase(it);

			// advance to the next task
			it = nxt;
		}
	}

	// update the task timer
	UpdateTaskTimer();

	// return the task execution status
	return tasksExecuted;
}

bool JavascriptEngine::EventTask::Execute()
{
	// get the 'global' object for 'this'
	JsValueRef global, result;
	JsGetGlobalObject(&global);

	// invoke the function
	JsCallFunction(func, &global, 1, &result);

	// if any errors occurred, log them to the log file but
	// otherwise ignore them
	bool exc = false;
	if (JsHasException(&exc) != JsNoError && exc)
		JavascriptEngine::Get()->LogAndClearException();

	// presume that this is a one-shot task that won't be rescheduled;
	// subclasses can override as needed
	return false;
}

const TCHAR *JavascriptEngine::JsErrorToString(JsErrorCode err)
{
	switch (err)
	{
	case JsNoError:                            return _T("JsNoError");

	// JsErrorCategoryUsage
	case JsErrorCategoryUsage:                 return _T("JsErrorCategoryUsage");
	case JsErrorInvalidArgument:               return _T("JsErrorInvalidArgument");
	case JsErrorNullArgument:                  return _T("JsErrorNullArgument");
	case JsErrorNoCurrentContext:              return _T("JsErrorNoCurrentContext");
	case JsErrorInExceptionState:              return _T("JsErrorInExceptionState");
	case JsErrorNotImplemented:                return _T("JsErrorNotImplemented");
	case JsErrorWrongThread:                   return _T("JsErrorWrongThread");
	case JsErrorRuntimeInUse:                  return _T("JsErrorRuntimeInUse");
	case JsErrorBadSerializedScript:           return _T("JsErrorBadSerializedScript");
	case JsErrorInDisabledState:               return _T("JsErrorInDisabledState");
	case JsErrorCannotDisableExecution:        return _T("JsErrorCannotDisableExecution");
	case JsErrorHeapEnumInProgress:            return _T("JsErrorHeapEnumInProgress");
	case JsErrorArgumentNotObject:             return _T("JsErrorArgumentNotObject");
	case JsErrorInProfileCallback:             return _T("JsErrorInProfileCallback");
	case JsErrorInThreadServiceCallback:       return _T("JsErrorInThreadServiceCallback");
	case JsErrorCannotSerializeDebugScript:    return _T("JsErrorCannotSerializeDebugScript");
	case JsErrorAlreadyDebuggingContext:       return _T("JsErrorAlreadyDebuggingContext");
	case JsErrorAlreadyProfilingContext:       return _T("JsErrorAlreadyProfilingContext");
	case JsErrorIdleNotEnabled:                return _T("JsErrorIdleNotEnabled");
	case JsCannotSetProjectionEnqueueCallback: return _T("JsCannotSetProjectionEnqueueCallback");
	case JsErrorCannotStartProjection:         return _T("JsErrorCannotStartProjection");
	case JsErrorInObjectBeforeCollectCallback: return _T("JsErrorInObjectBeforeCollectCallback");
	case JsErrorObjectNotInspectable:          return _T("JsErrorObjectNotInspectable");
	case JsErrorPropertyNotSymbol:             return _T("JsErrorPropertyNotSymbol");
	case JsErrorPropertyNotString:             return _T("JsErrorPropertyNotString");
	case JsErrorInvalidContext:                return _T("JsErrorInvalidContext");
	case JsInvalidModuleHostInfoKind:          return _T("JsInvalidModuleHostInfoKind");
	case JsErrorModuleParsed:                  return _T("JsErrorModuleParsed");

	// JsErrorCategoryEngine
	case JsErrorCategoryEngine:                return _T("JsErrorCategoryEngine");
	case JsErrorOutOfMemory:                   return _T("JsErrorOutOfMemory");
	case JsErrorBadFPUState:                   return _T("JsErrorBadFPUState");

	// JsErrorCategoryScript
	case JsErrorCategoryScript:                return _T("JsErrorCategoryScript");
	case JsErrorScriptException:               return _T("JsErrorScriptException");
	case JsErrorScriptCompile:                 return _T("JsErrorScriptCompile");
	case JsErrorScriptTerminated:              return _T("JsErrorScriptTerminated");
	case JsErrorScriptEvalDisabled:            return _T("JsErrorScriptEvalDisabled");

	// JsErrorCategoryFatal
	case JsErrorCategoryFatal:                 return _T("JsErrorCategoryFatal");
	case JsErrorFatal:                         return _T("JsErrorFatal");
	case JsErrorWrongRuntime:                  return _T("JsErrorWrongRuntime");

	// JsErrorCategoryDiagError
	case JsErrorCategoryDiagError:             return _T("JsErrorCategoryDiagError");
	case JsErrorDiagAlreadyInDebugMode:        return _T("JsErrorDiagAlreadyInDebugMode");
	case JsErrorDiagNotInDebugMode:            return _T("JsErrorDiagNotInDebugMode");
	case JsErrorDiagNotAtBreak:                return _T("JsErrorDiagNotAtBreak");
	case JsErrorDiagInvalidHandle:             return _T("JsErrorDiagInvalidHandle");
	case JsErrorDiagObjectNotFound:            return _T("JsErrorDiagObjectNotFound");
	case JsErrorDiagUnableToPerformAction:     return _T("JsErrorDiagUnableToPerformAction");

	default:								   return _T("(unknown)");
	}
}

// Module import callbacks
JsErrorCode CHAKRA_CALLBACK JavascriptEngine::FetchImportedModule(
	JsModuleRecord referencingModule,
	JsValueRef specifier,
	JsModuleRecord *dependentModuleRecord)
{
	// get the module host info
	ModuleHostInfo *hostInfo = nullptr;
	JsErrorCode err;
	if ((err = JsGetModuleHostInfo(referencingModule, JsModuleHostInfo_HostDefined, (void **)&hostInfo)) != JsNoError)
		return err;

	// make sure we have host info; this should never happen, but just to be sure...
	if (hostInfo == nullptr)
	{
		// weird - log it
		JsValueRef strval;
		const wchar_t *pstr = _T("<unknown>");
		size_t len = 9;
		if (JsConvertValueToString(specifier, &strval) != JsNoError
			|| JsStringToPointer(strval, &pstr, &len) != JsNoError)
			pstr = _T("<unknown>"), len = 9;

		LogFile::Get()->Write(_T("[Javascript] FetchImportedModule callback: missing host information trying to load module %.*ws\n"), (int)len, pstr);
		return JsErrorFatal;
	}

	// call the common handler
	return inst->FetchImportedModuleCommon(referencingModule, hostInfo->path, specifier, dependentModuleRecord);
}

JsErrorCode CHAKRA_CALLBACK JavascriptEngine::FetchImportedModuleFromScript(
	JsSourceContext referencingSourceContext,
	JsValueRef specifier,
	JsModuleRecord *dependentModuleRecord)
{
	// get the source cookie struct
	const SourceCookie *cookie = reinterpret_cast<SourceCookie*>(referencingSourceContext);

	// call the common handler
	return inst->FetchImportedModuleCommon(nullptr, cookie->file, specifier, dependentModuleRecord);
}

JsErrorCode JavascriptEngine::FetchImportedModuleCommon(
	JsModuleRecord referencingModule,
	const WSTRING &referencingSourcePath,
	JsValueRef specifier,
	JsModuleRecord *dependentModuleRecord)
{
	// convert the value to string
	JsErrorCode err;
	JsValueRef strspec;
	if ((err = JsConvertValueToString(specifier, &strspec)) != JsNoError)
		return err;

	// retrieve the string
	const wchar_t *pstr;
	size_t len;
	if ((err = JsStringToPointer(strspec, &pstr, &len)) != JsNoError)
		return err;

	// proceed with the string specifier
	return FetchImportedModuleCommon(referencingModule, referencingSourcePath, WSTRING(pstr, len), dependentModuleRecord);
}

JsErrorCode JavascriptEngine::FetchImportedModuleCommon(
	JsModuleRecord referencingModule,
	const WSTRING &referencingSourcePath,
	const WSTRING &specifier,
	JsModuleRecord *dependentModuleRecord)
{
	// get the normalized filename
	JsErrorCode err;
	WSTRING fname;
	if ((err = GetModuleSource(fname, specifier, referencingSourcePath)) != JsNoError)
		return err;

	// get the file URL
	WSTRING fileUrl = GetFileUrl(fname.c_str());

	// use the canonicalized URL as the key
	WSTRING key = fileUrl;

	// look it up 
	if (auto it = modules.find(key); it != modules.end())
	{
		// found it - return the previously loaded module
		*dependentModuleRecord = it->second.module;
		return JsNoError;
	}

	// convert the normalized specifier to a js value to pass back to the engine
	JsValueRef normalizedSpecifier;
	JsPointerToString(fname.c_str(), fname.length(), &normalizedSpecifier);

	// create the new module record
	if ((err = JsInitializeModuleRecord(referencingModule, normalizedSpecifier, dependentModuleRecord)) != JsNoError)
		return err;

	// set the URL in the module record
	JsValueRef url;
	JsPointerToString(fileUrl.c_str(), fileUrl.length(), &url);
	JsSetModuleHostInfo(*dependentModuleRecord, JsModuleHostInfo_Url, url);

	// store the module record in our table
	auto it = modules.emplace(std::piecewise_construct,
		std::forward_as_tuple(key),
		std::forward_as_tuple(fname, *dependentModuleRecord));

	// set the host info in the new module record to the new ModuleHostInfo record
	ModuleHostInfo *hostInfo = &(it.first->second);
	JsSetModuleHostInfo(*dependentModuleRecord, JsModuleHostInfo_HostDefined, hostInfo);

	// add a task to load and parse the module
	AddTask(new ModuleParseTask(*dependentModuleRecord, fname));

	// success
	return JsNoError;
}

JsErrorCode CHAKRA_CALLBACK JavascriptEngine::NotifyModuleReadyCallback(
	JsModuleRecord referencingModule,
	JsValueRef exceptionVar)
{
	// get the module info
	JsErrorCode err;
	ModuleHostInfo *hostInfo = nullptr;
	if ((err = JsGetModuleHostInfo(referencingModule, JsModuleHostInfo_HostDefined, (void **)&hostInfo)) != JsNoError)
		return err;

	// make sure we have host info; this should never happen, but just to be sure...
	if (hostInfo == nullptr)
	{
		LogFile::Get()->Write(_T("[Javascript] FetchImportedModule callback - missing host info\n"));
		return JsErrorFatal;
	}

	// check for exceptions
	JsValueType excType;
	if (exceptionVar != JS_INVALID_REFERENCE 
		&& JsGetValueType(exceptionVar, &excType) != JsNoError
		&& !(excType == JsUndefined || excType == JsNull))
	{
		// set this exception in the engine
		JsSetException(exceptionVar);

		// get the path from the module info
		LogFile::Get()->Write(LogFile::JSLogging, _T("[Javascript] NotifyModuleReadyCallback exception: module %s\n"), hostInfo->path.c_str());

		// log the exception data
		inst->LogAndClearException();
	}
	else
	{
		// queue a task to load the module
		inst->AddTask(new ModuleEvalTask(referencingModule, hostInfo->path.c_str()));
	}

	// success
	return JsNoError;
}

bool JavascriptEngine::ModuleParseTask::Execute()
{
	// remove the file:/// URL prefix if present
	const WCHAR *path = this->path.c_str();
	if (wcsncmp(path, L"file:///", 8) == 0)
		path += 8;

	// load the script
	LogFile::Get()->Write(LogFile::JSLogging, _T("[Javascript] Loading module from file %ws\n"), path);
	long len;
	LogFileErrorHandler eh(_T(". "));
	std::unique_ptr<WCHAR> contents(ReadFileAsWStr(WCHARToTCHAR(path), eh, len, 0));
	if (contents == nullptr)
	{
		LogFile::Get()->Write(LogFile::JSLogging, _T(". Error loading %ws\n"), path);
		return false;
	}

	// Allocate a cookie.  This is required to give the module a source
	// context, which ChakraCore uses to identify module source locations
	// for functions defined therein, in stack traces and other debugging.
	// (CC doesn't use the path we store here; the cookie is an opaque
	// identifier as far as it's concerned.  The cookie's only purpose to
	// CC is to serve as a unique identifier for the module; CC uses the
	// cookie as a key into its own internal table to associate the source 
	// code with the module record, where the URL is actually stored.  The
	// URL it uses is the one we added to the module record previously.)
	auto const *cookie = &inst->sourceCookies.emplace_back(path);

	// Parse the source.  Note that the source memory is provided as BYTEs,
	// but the file length we have is in WCHARs, so we need to multiply for
	// the parser's sake.
	JsValueRef exc = JS_INVALID_REFERENCE;
	JsErrorCode err = JsParseModuleSource(module, reinterpret_cast<JsSourceContext>(cookie),
		(BYTE*)contents.get(), len*sizeof(WCHAR), 
		JsParseModuleSourceFlags_DataIsUTF16LE, &exc);

	// check for errors
	if (exc != JS_INVALID_REFERENCE)
	{
		// We have an exception object.  Log the error.
		JsSetException(exc);
		inst->LogAndClearException();

		// Logging the exception clears it, so set it again for the benefit
		// of the engine.
		JsSetException(exc);
	}
	else if (err == JsErrorScriptException || err == JsErrorScriptCompile)
	{
		// Script compile or execution error - these generally set an
		// exception in the Javascript context.
		LogFile::Get()->Write(LogFile::JSLogging, 
			_T("[Javascript] Error loading module %ws\n"), this->path.c_str());
		inst->LogAndClearException();
	}
	else if (err != JsNoError)
	{
		// There's an engine error code with no exception object.  Log it
		// and throw the engine error code.
		LogFile::Get()->Write(LogFile::JSLogging, 
			_T("[Javascript] Error loading module %ws: %s\n"), 
			this->path.c_str(), JsErrorToString(err));

		// throw the engine error code
		inst->Throw(err, _T("ModuleParseTask"));
	}

	// this is a one shot - don't reschedule
	return false;
}

bool JavascriptEngine::ModuleEvalTask::Execute()
{
	// If we're in debug mode, and this is the first module, do a "step into"
	// to trigger our initial pause.
	if (inst->debugInitBreakPending)
	{
		// the break is no longer pending
		inst->debugInitBreakPending = false;

		// Generate a Step Into command.  (We don't just do a "pause", because
		// the debugger sees a stack frame for "Execute this whole module".  The
		// weird thing about this state is that the debugger UI will make it look
		// like we're at the first executable line of the module, but we're not 
		// really: we're actually in some invisible wrapper that's about to call
		// into the module.  So it's much more intuitive to step through that
		// invisible wrapper and actually position the internal frame state in
		// the engine so that it's inside the module, so that the UI appearance
		// matches up with reality.)
		JsDebugProtocolHandlerSendRequest(inst->debugProtocolHandler, "Debugger.stepInto");
	}

	// evaluate the module
	JsValueRef result;
	JsErrorCode err = JsModuleEvaluation(module, &result);

	// log any error
	if (err == JsErrorScriptException || err == JsErrorScriptCompile)
	{
		LogFile::Get()->Write(LogFile::JSLogging, _T("[Javascript] Error executing module %s\n"), path.c_str());
		inst->LogAndClearException();
	}
	else if (err != JsNoError)
	{
		LogFile::Get()->Write(LogFile::JSLogging, _T("[Javascript] Module evaluation failed for %s: %s\n"), path.c_str(), JsErrorToString(err));
	}

	// this is a one shot - don't reschedule
	return false;
}

WSTRING JavascriptEngine::GetFileUrl(const WCHAR *path)
{
	// Get the URL, for error messages and debugging.  VS Code requires file:/// URLs
	// with regular Windows absolute file paths with drive letters and backslashes.
	// VS Code also cares about EXACT capitalization of the file - it won't match a 
	// file system file against one of our URLs unless the case matches exactly.  So
	// do a file system lookup on the file to get its exact path.
	WSTRING url = L"file:///";

	// if the path already starts with file:///, remove that part
	if (wcsncmp(path, L"file:///", 8) == 0)
		path += 8;

	// Try opening the file, so that we can get its "final" filename from the
	// handle.  The final filename actually looks up the file system entry and
	// gets the stored capitalization in all path elements.  This is important
	// when using the VS Code debugger, because it matches exact capitalization
	// on filenames.  If we tell it we have file C:\foo\bar.js, and the actual
	// file system entry is C:\Foo\Bar.js, VS will consider our module to be a
	// separate entity from the file, so it won't match source breakpoints the
	// user set in the actual file.
	if (HandleHolder hfile(CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
		hfile.h != NULL && hfile.h != INVALID_HANDLE_VALUE)
	{
		// we were able to open the file - get the canonical name
		WCHAR buf[4096];
		WCHAR *bufp = buf;
		GetFinalPathNameByHandleW(hfile, buf, countof(buf), FILE_NAME_NORMALIZED);

		// That API uses the \\?\ prefix in most cases.  If present, skip it.
		if (wcsncmp(L"\\\\?\\", buf, 4) == 0)
			bufp += 4;

		// use this as the final name
		url += bufp;
	}
	else
	{
		// unable to open the file - just use the canonicalized name
		url += path;
	}

	// return the result
	return url;
}

JsErrorCode JavascriptEngine::GetModuleSource(
	WSTRING &filename, const WSTRING &specifier, const WSTRING &referencingSourceFile)
{
	// if there's a file: scheme prefix, strip it
	const WCHAR *p = specifier.c_str();
	if (specifier.length() > 5 && _wcsnicmp(p, _T("file:"), 5) == 0)
	{
		// strip up to 3 '/' prefix characters
		p += 5;
		for (int nStripped = 0; nStripped < 3 && *p == '/'; ++nStripped, ++p);
	}

	// if it's already an absolute path, use it as given
	if (!PathIsRelativeW(p))
	{
		filename = p;
		return JsNoError;
	}

	// It's a relative path, so take it as relative to the referencing
	// script/module's path.  Start by getting the folder containing the
	// parent script.
	WCHAR path[MAX_PATH];
	wcscpy_s(path, referencingSourceFile.c_str());
	PathRemoveFileSpecW(path);

	// add the relative path
	PathAppendW(path, p);

	// return the file URL for this path
	filename = GetFileUrl(path);
	return JsNoError;
}

// --------------------------------------------------------------------------
//
// Create an external object
//
JsErrorCode JavascriptEngine::CreateExternalObject(JsValueRef &jsobj, ExternalObject *obj, JsFinalizeCallback finalize)
{
	// try creating the external object
	if (JsErrorCode err = JsCreateExternalObject(obj, finalize, &jsobj); err != JsNoError)
	{
		// failed - destroy the external object
		delete obj;
		return err;
	}

	// success
	return JsNoError;
}

JsErrorCode JavascriptEngine::CreateExternalObjectWithPrototype(JsValueRef &jsobj, JsValueRef prototype,
	ExternalObject *obj, JsFinalizeCallback finalize)
{
	// try creating the external object
	if (JsErrorCode err = JsCreateExternalObjectWithPrototype(obj, finalize, prototype, &jsobj); err != JsNoError)
	{
		// failed - destroy the external object
		delete obj;
		return err;
	}

	// success
	return JsNoError;
}



// --------------------------------------------------------------------------
//
// Type signature parser.  This is a utility class for parsing signatures
// generated by the DLL import type parser.
//
class JavascriptEngine::SigParser
{
public:
	// initialize from a full signature
	SigParser(const WCHAR *sig, size_t len) : sig(sig, len) { }
	SigParser(const WCHAR *sig, const WCHAR *sigEnd) : sig(sig, sigEnd - sig) { }
	SigParser(const WSTRING &sig) : sig(sig) { }
	SigParser(const std::wstring_view &sig) : sig(sig) { }

	// type signature
	std::wstring_view sig;
	const WCHAR *sigEnd() const { return sig.data() + sig.length(); }

	// find the end of a type element
	static const WCHAR *EndOfArg(const WCHAR *p, const WCHAR *sigEnd)
	{
		// skip to the next space
		int level = 0;
		for (; p < sigEnd; ++p)
		{
			// count nesting levels
			switch (*p)
			{
			case '(':
			case '{':
				++level;
				break;

			case ')':
			case '}':
				// If we're at the outer level, we must have started parsing
				// from within this nesting level, so it counts as the end relative
				// to our starting position.
				if (level == 0)
					return p;

				// leave the level
				--level;
				break;

			case ' ':
				// Separator.  If we're not in a nested type, this is the end
				// of the current type, so return this position.  Otherwise this
				// separator is part of the current type, so just keep going.
				if (level == 0)
					return p;
			}
		}

		// we reached the end of the signature without finding a separator
		return sigEnd;
	}
};


// --------------------------------------------------------------------------
//
// DllImport marshaller classes
//

// Parameters the current CPU architecture calling conventions
static const size_t argSlotSize = IF_32_64(4, 8);   // size in bytes of a generic argument slot
static const size_t stackAlign = IF_32_64(4, 16);   // stack pointer alignment size in bytes
static const size_t minArgSlots = IF_32_64(0, 4);   // minimum stack slots allocated for arguments

// Base class for marshallers
class JavascriptEngine::Marshaller
{
public:
	Marshaller(SigParser *sig) : sig(sig), p(sig->sig.data()) { }

	Marshaller(const Marshaller &m) : sig(m.sig), p(m.sig->sig.data()) { }

	// signature
	SigParser *sig;

	// current position in signature
	const WCHAR *p;

	// flag: the current type being processed was marked 'const'
	bool isConst = false;

	// error flag
	bool error = false;

	// process a signature
	virtual bool Marshall()
	{
		// no errors yet
		error = false;

		// process the signature
		for (const TCHAR *end = sig->sigEnd(); p < end && !error; NextArg())
			MarshallValue();

		// return true on success, false if there's an error
		return !error;
	}

	// Process the entries in a struct type.  The callback returns false on error, 
	// in which case we stop the iteration and return false from the overall function.
	bool MarshallStructMembers(std::function<bool(WSTRING &memberName, WSTRING &memberSig)> cb)
	{
		// get the end of the overall signature
		const WCHAR *end = sig->sigEnd();

		// if we're at the opening '{', reset the bounds to the contents of the braces
		if (p < end && *p == '{')
		{
			end = EndOfArg(p);
			p += 3;
		}

		// scan members
		for (; p < end && *p != '}'; NextArg())
		{
			// scan the member tag name
			const WCHAR *nameStart = p;
			for (; p < end && *p != ';'; ++p);
			WSTRING memberName(nameStart, p - nameStart);

			// skip the ';'
			if (p < end) 
				++p;

			// scan the type signature, but leave 'p' pointing to the signature
			WSTRING memberSig(p, EndOfArg(p) - p);

			// invoke the callback
			if (!cb(memberName, memberSig))
				return false;
		}

		// success
		return true;
	}

	// Figure the size of a struct/union in the native representation.  flexErrorMsg
	// is an error message to show if a flex array is found and the size of the 
	// unspecified dimension can't be inferred from jsval.
	size_t SizeofStruct(JsValueRef jsval, const TCHAR *flexErrorMsg);
	size_t SizeofUnion(JsValueRef jsval, const TCHAR *flexErrorMsg);

	const double MaxIntInDouble = static_cast<double>(1LL << DBL_MANT_DIG);

	// marshall the value at the current signature position
	virtual void MarshallValue()
	{
		// check for a const qualifier
		isConst = false;
		if (*p == '%')
		{
			// note it, then skip it to get to the actual type
			isConst = true;
			++p;
		}

		// process the type code
		switch (*p)
		{
		case '*': return DoPointer();
		case '&': return DoReference();
		case 'b': return DoBool();
		case 'B': return DoBSTR();
		case 'c': return DoInt8();
		case 'C': return DoUInt8();
		case 's': return DoInt16();
		case 'S': return DoUInt16();
		case 'i': return DoInt32();
		case 'I': return DoUInt32();
		case 'l': return DoInt64();
		case 'L': return DoUInt64();
		case 'z': return DoSizeT();
		case 'Z': return DoSizeT();
		case 'p': return DoIntPtr();
		case 'P': return DoUIntPtr();
		case 'f': return DoFloat();
		case 'd': return DoDouble();
		case 'H': return DoHandle();
		case 'h': return DoWinHandle();
		case 't': return DoString();
		case 'T': return DoString();
		case 'G': return DoGuid();
		case 'v': return DoVoid();
		case 'V': return DoVariant();
		case '@': return DoTypeRef();
		case '{':
			if (p[1] == 'S')
				DoStruct();
			else if (p[1] == 'U')
				DoUnion();
			else if (p[1] == 'I')
				DoInterface();
			else
				Error(MsgFmt(_T("Internal dllImport error: unknown composite type code '%c' in siguature %.*s"), 
					p[1], static_cast<int>(sig->sig.length()), sig->sig.data()));
			break;

		case '(': return DoFunction();
		case '[': return DoArray();

		default:
			Error(MsgFmt(_T("Internal dllImport error: unknown type code '%c' in signature %.*s"), 
				*p, static_cast<int>(sig->sig.length()), sig->sig.data()));
			break;
		}
	}

	// reference to named subtype
	virtual void DoTypeRef()
	{
		// get the bounds of the name - the name is delimited by the next non-symbol character
		const WCHAR *name = ++p;
		p = EndOfArg();

		// look up the name in the type table
		std::wstring_view reftype;
		if (!inst->LookUpNativeType(name, p - name, reftype))
			return;

		// push a sub-parser for the type, and marshall the value using the nested parser
		struct SigStacker
		{
			SigStacker(Marshaller *self, const std::wstring_view &reftype) : 
				self(self), 
				p(self->p),
				parentSig(self->sig),
				subSig(reftype)
			{
				self->sig = &subSig; 
				self->p = subSig.sig.data();
			}
			~SigStacker() 
			{ 
				self->sig = parentSig; 
				self->p = p;
			}

			Marshaller *self;
			SigParser *parentSig;
			SigParser subSig;
			const WCHAR *p;
		};
		SigStacker stacker(this, reftype);
		MarshallValue();
	}

	// process individual int types
	virtual void DoBool() { AnyInt32(); }
	virtual void DoInt8() { AnyInt32(); }
	virtual void DoUInt8() { AnyInt32(); }
	virtual void DoInt16() { AnyInt32(); }
	virtual void DoUInt16() { AnyInt32(); }
	virtual void DoInt32() { AnyInt32(); }
	virtual void DoUInt32() { AnyInt32(); }
	virtual void DoInt64() { AnyInt64(); }
	virtual void DoUInt64() { AnyInt64(); }
	virtual void DoSizeT() { IF_32_64(AnyInt32(), AnyInt64()); }
	virtual void DoSSizeT() { IF_32_64(AnyInt32(), AnyInt64()); }
	virtual void DoPtrdiffT() { IF_32_64(AnyInt32(), AnyInt64()); }

	// process any int type up to 32/64 bits
	virtual void AnyInt32() { }
	virtual void AnyInt64() { }

	// INT_PTR types
	virtual void DoIntPtr() { }
	virtual void DoUIntPtr() { }

	// process float types
	virtual void DoFloat() { }
	virtual void DoDouble() { }

	// handle types
	virtual void DoHandle() { IF_32_64(AnyInt32(), AnyInt64()); }
	virtual void DoWinHandle() { DoHandle(); }

	// variants
	virtual void DoVariant() { }

	// BSTRs
	virtual void DoBSTR() { }

	// process a pointer/reference type
	virtual void DoPointer() { }
	virtual void DoReference() { DoPointer(); }

	// process an array type
	virtual void DoArray() { }

	// process a struct/union type
	virtual void DoStruct() { }
	virtual void DoUnion() { }

	// process an interface type
	virtual void DoInterface() { }

	// process a string type
	virtual void DoString() { }

	// process a GUID type
	virtual void DoGuid() { }

	// process a void type
	virtual void DoVoid() { }

	// process a function type
	virtual void DoFunction() { }

	// Throw an error.  This doesn't actually "throw" in the C++ exception sense;
	// it just sets the exception in the Javascript engine, and sets our internal
	// error flag.
	void Error(const TCHAR *msg)
	{
		error = true;
		if (!inst->HasException())
			inst->Throw(msg);
	}

	// Throw an error from an engine error code
	void Error(JsErrorCode err, const TCHAR *msg)
	{
		error = true;
		if (!inst->HasException())
			inst->Throw(err, msg);
	}

	// advance p to the next argument slot
	void NextArg()
	{
		// find the end of the current argument
		const WCHAR *p = EndOfArg();

		// advance to the start of the next argument
		for (const TCHAR *end = sig->sigEnd(); p < end && *p == ' '; ++p);

		// save the result
		this->p = p;
	};

	const WCHAR *EndOfArg() const { return EndOfArg(p, sig->sigEnd()); }
	const WCHAR *EndOfArg(const WCHAR *p) const { return EndOfArg(p, sig->sigEnd()); }

	// find the end of the current argument slot; does not advance p
	static const WCHAR *EndOfArg(const WCHAR *p, const WCHAR *sigEnd)
	{
		return SigParser::EndOfArg(p, sigEnd);
	}

	// Get the length of a Javascript array value
	int GetArrayLength(JsValueRef jsval)
	{
		// read the length property
		int len;
		const TCHAR *where;
		if (JsErrorCode err = inst->GetProp(len, jsval, "length", where); err != JsNoError)
		{
			Error(err, MsgFmt(_T("dllImport: getting length of array argument"), where));
			return -1;
		}

		// make sure the length is non-negative
		return max(len, 0);
	}

	// Parse one array dimension.  Advances p to the character after the ']'.
	static bool ParseArrayDim(const WCHAR* &p, const WCHAR *endp, int &dim, bool &empty)
	{
		bool overflow = false;

		// skip the opening '['
		if (p < endp && *p == '[')
			++p;

		// check for an empty dimension
		if (p < endp && *p == ']')
		{
			++p;
			dim = 0;
			empty = true;
			return true;
		}

		// parse the dimension
		int acc = 0;
		for (; p < endp && *p >= '0' && *p <= '9'; ++p)
		{
			if (acc > INT_MAX / 10)
				overflow = true;

			acc *= 10;
			int dig = static_cast<int>(*p - '0');

			if (acc > INT_MAX - dig)
				overflow = true;

			acc += dig;
		}

		// ensure it ends with ']'
		if (p >= endp || *p != ']')
			return false;

		// skip the ']'
		++p;

		// return the results
		dim = acc;
		empty = false;
		return !overflow;
	}

	// Get the array dimension for an argument value.  This can be
	// used to figure the actual size needed for an indeterminate array
	// argument or struct element.
	bool GetActualArrayDim(JsValueRef jsval, int &dim, size_t eleSize)
	{
		// check if we have a concrete argument to infer the size from
		if (jsval != JS_INVALID_REFERENCE)
		{
			// check the argument type
			JsValueType type;
			JsErrorCode err;
			if ((err = JsGetValueType(jsval, &type)) != JsNoError)
			{
				Error(err, _T("dllImport: getting type of struct member array"));
				return false;
			}

			switch (type)
			{
			case JsArray:
				// Javascript array object.  We'll interpret each element of the JS array
				// as an instance of the type underlying the flex dimension.  Examples:
				//
				//    struct { int foo[]; }      -> JS array elements will be mapped to ints
				//    struct { int foo[][5]; }   -> JS array elements will be mapped to int[5] arrays
				//
				// Regardless of the underlying type, this means that the length of the JS
				// array gives us the flex dimension.
				if (int i = GetArrayLength(jsval); i < 0)
					return false;
				else
					dim = i;
				break;

			case JsTypedArray:
				// Javascript typed array.  For the simple case of a single-dimensional
				// native array, the typed array length maps to the single flex dimension.
				// If the native type is an array of arrays, though, the JS typed array
				// can't provide the aggregates the way it can for a regular JS array.
				// Instead, we'll map this to the flattened native array.
				//
				// E.g., if we have struct { int foo[][5]; }, this flattens to int foo[X*5]
				// for some X.  We'll assume that the JS typed array is in this flattened
				// format, so we'll work backwards from the typed array size to figure X,
				// the free flex dimension.
				{
					unsigned int arrayByteLength;
					if ((err = JsGetTypedArrayInfo(jsval, nullptr, nullptr, nullptr, &arrayByteLength)) != JsNoError)
					{
						Error(err, _T("dllImport: getting typed array information"));
						return false;
					}

					unsigned int neles = static_cast<unsigned int>(arrayByteLength / eleSize);
					if (neles > static_cast<unsigned int>(INT_MAX))
					{
						Error(_T("dllImport: typed array is too large"));
						return false;
					}

					dim = static_cast<int>(neles);
					break;
				}
				break;

			case JsUndefined:
			case JsNull:
				// null or undefined - treat this as an abstract array
				// with zero size, as though we didn't have a value at all
				break;

			default:
				// other types can't be mapped to arrays
				Error(_T("invalid type for struct array element"));
				return false;
			}
		}
		else
		{
			// There's no concrete argument to apply.  The abstract size of a
			// flex array is simply zero.  When such an array is used in a
			// struct, it's a placeholder for additional elements allocated
			// by the caller, but it doesn't contribute to sizeof(the struct).
			dim = 0;
		}

		// success
		return true;
	}

};

// Generic size counter
class JavascriptEngine::MarshallSizer : public Marshaller
{
public:
	MarshallSizer(SigParser *sig) : Marshaller(sig) { }

	// Get the concrete value for the current item being sized, if available
	virtual JsValueRef GetCurVal() = 0;

	// Add a value of the given byte size to the total
	virtual void Add(size_t bytes, size_t align = 0, int nItems = 1) = 0;
	virtual void AddStruct(size_t bytes, size_t align = 0, int nItems = 1) = 0;

	// process individual int types
	virtual void DoBool() override { Add(sizeof(bool)); }
	virtual void DoInt8() override { Add(1); }
	virtual void DoUInt8() override { Add(1); }
	virtual void DoInt16() override { Add(2); }
	virtual void DoUInt16() override { Add(2); }
	virtual void DoInt32() override { Add(4); }
	virtual void DoUInt32() override { Add(4); }
	virtual void DoInt64() override { Add(8); }
	virtual void DoUInt64() override { Add(8); }
	virtual void DoSizeT() override { Add(IF_32_64(4, 8)); }
	virtual void DoSSizeT() override { Add(IF_32_64(4, 8)); }
	virtual void DoPtrdiffT() override { Add(IF_32_64(4, 8)); }

	// INT_PTR types
	virtual void DoIntPtr() override { Add(IF_32_64(4, 8)); }
	virtual void DoUIntPtr() override { Add(IF_32_64(4, 8)); }

	// process float types
	virtual void DoFloat() override { Add(4); }
	virtual void DoDouble() override { Add(8); }

	// handle types
	virtual void DoHandle() override { Add(IF_32_64(4, 8)); }

	// process a pointer type
	virtual void DoPointer() override { Add(IF_32_64(4, 8)); }

	// process a string type
	virtual void DoString() override { Add(IF_32_64(4, 8)); }

	// process a GUID type
	virtual void DoGuid() override { Add(16); }

	// process a VARIANT type
	virtual void DoVariant() override { Add(sizeof(VARIANT), __alignof(VARIANT)); }

	// process a BSTR type
	virtual void DoBSTR() override { Add(sizeof(BSTR)); }

	// process a struct type
	virtual void DoStruct() override;

	// process an interface type
	virtual void DoInterface() override { Error(_T("dllImport: interface types cannot be passed by value")); }

	// process a union type
	virtual void DoUnion() override;

	// process an array
	virtual void DoArray() override = 0;

	// process a void type
	virtual void DoVoid() override { /* void types have zero size */ }

	// process a function type
	virtual void DoFunction() override { Error(_T("dllImport: function types cannot be passed by value")); }
};

// Basic sizer.  This simply adds up the sizes for the measured types without
// regard to alignment.  This is useful mostly to get the size of single type.
class JavascriptEngine::MarshallBasicSizer : public MarshallSizer
{
public:
	MarshallBasicSizer(SigParser *sig, JsValueRef jsval = JS_INVALID_REFERENCE) : 
		MarshallSizer(sig),	jsval(jsval)
	{ }

	virtual void Add(size_t bytes, size_t align = 0, int nItems = 1) 
	{ 
		// add the size to the total
		size += bytes * nItems; 

		// the default alignment is the type's size
		if (align == 0) 
			align = bytes;

		// use the largest alignment we've seen so far
		if (align > this->align)
			this->align = align;
	}

	virtual void AddStruct(size_t bytes, size_t align = 0, int nItems = 1) { Add(bytes, align, nItems); }

	virtual void DoArray() override
	{
		// Figure the dimension
		int dim;
		bool isEmpty;
		if (!ParseArrayDim(p, sig->sigEnd(), dim, isEmpty))
			return;

		// note if we found a flexible array dimension
		if (isEmpty)
			flex = true;

		// Figure the size of the underlying type.  Flex arrays aren't 
		// allowed beyond the first dimension of a multi-dimensional array,
		// so we don't have to pass an actual value to measure.
		SigParser subsig(p, EndOfArg());
		MarshallBasicSizer sizer(&subsig, JS_INVALID_REFERENCE);
		sizer.MarshallValue();

		// flex sub-arrays are invalid
		if (sizer.flex)
		{
			Error(_T("Invalid indeterminate dimension in sub-array"));
			return;
		}

		// if we have a flex array, figure the concrete dimension from the
		// actual value, if possible
		if (isEmpty)
		{
			if (!GetActualArrayDim(GetCurVal(), dim, sizer.size))
				return;
		}

		// add n elements of the underlying type
		Add(sizer.size, sizer.align, dim);
	}

	virtual void DoFunction() override
	{
		// we can't take the size of a function
		Error(_T("dllImport: attempting to take the size of a native function; this is an invalid operation"));
	}

	virtual void DoInterface() override { /* interfaces have zero size */ }

	virtual JsValueRef GetCurVal() override { return jsval; }

	// concrete value being sized, if available
	JsValueRef jsval;

	// computed size of type
	size_t size = 0;

	// alignment of the type
	size_t align = 0;

	// a flexible array was encountered
	bool flex = false;
};

// Common base for structs and unions
class JavascriptEngine::MarshallStructOrUnionSizer : public MarshallBasicSizer
{
public:
	MarshallStructOrUnionSizer(SigParser *sig, JsValueRef jsval) : MarshallBasicSizer(sig, jsval) { }

	// Offset of the last item marshalled.  For a struct, this is the aligned
	// offset of the last item.  For a union, this is always zero, since union
	// members are all overlaid on the same memory.
	size_t lastItemOfs = 0;

	// size of the last item
	size_t lastItemSize = 0;

	// total size of the struct, including padding
	size_t size = 0;

	// alignment
	size_t align = 0;

	// flex member error detected and reported
	bool flexError = false;

	// current property name and type signature
	WSTRING curProp;
	WSTRING curPropType;

	// the current value is the property value
	virtual JsValueRef GetCurVal() override 
	{ 
		// if there's an object and a current property, retrieve the property
		JsValueRef curval = JS_INVALID_REFERENCE;
		if (jsval != JS_INVALID_REFERENCE && curProp.length() != 0)
		{
			const TCHAR *where;
			if (JsErrorCode err = inst->GetProp(curval, jsval, WSTRINGToCSTRING(curProp).c_str(), where); err != JsNoError)
				Error(err, MsgFmt(_T("dllImport: measuring struct/union size: %s"), where));
		}

		return curval;
	}

	virtual bool Marshall() override
	{
		return MarshallStructMembers([this](WSTRING &memberName, WSTRING &memberSig)
		{
			// remember the current element name and type
			curProp = memberName;
			curPropType = memberSig;

			// A flex array is only allowed as the last element, so if we already 
			// have a flex array element, we can't have another member following it
			if (flex && !flexError)
			{
				Error(_T("dllImport: an unspecified array dimension can only be used in the last member of a struct"));
				flexError = true;
			}

			// marshall the value
			MarshallValue();

			// continue the iteration
			return true;
		});

		return !error;
	}

	virtual void MarshallValue() override
	{
		// If there's a ';' between here and the end of the argument,
		// we're looking at a field name.  Skip it to get to the type
		// code.
		if (*p != '{')
		{
			const WCHAR *q = p, *endp = EndOfArg();
			for (; q < endp && *q != ';' && *q != ' '; ++q);

			if (q < endp && *q == ';')
				p = q + 1;
		}

		// now do the regular marshalling
		__super::MarshallValue();
	}
};

// Struct size counter
class JavascriptEngine::MarshallStructSizer : public MarshallStructOrUnionSizer
{
public:
	MarshallStructSizer(SigParser *sig, JsValueRef jsval) : MarshallStructOrUnionSizer(sig, jsval) { }

	virtual void Add(size_t itemBytes, size_t itemAlign = 0, int nItems = 1) override
	{
		// if the alignment was unspecified, the type's own size is its natural alignment
		if (itemAlign == 0)
			itemAlign = itemBytes;

		// add padding to bring the current offset up to the required alignment
		ofs = ((ofs + itemAlign - 1)/itemAlign) * itemAlign;

		// remember the size and offset of the last item
		lastItemOfs = ofs;
		lastItemSize = itemBytes * nItems;

		// add this item or array of items
		ofs += itemBytes * nItems;

		// the overall struct alignment is the largest alignment of any individual item
		align = max(align, itemAlign);

		// figure the overall size, taking into account padding for alignment
		size = ((ofs + align - 1)/align) * align;
	}

	virtual void AddStruct(size_t itemBytes, size_t itemAlign = 0, int nItems = 1) override { Add(itemBytes, itemAlign, nItems); }

	// Next offset.  This is the offset of the next byte after the last item
	// added, without any padding for alignment.
	size_t ofs = 0;
};

// Union size counter
class JavascriptEngine::MarshallUnionSizer : public MarshallStructOrUnionSizer
{
public:
	MarshallUnionSizer(SigParser *sig, JsValueRef jsval) : MarshallStructOrUnionSizer(sig, jsval) { }

	virtual void Add(size_t itemBytes, size_t itemAlign, int nItems = 1) override
	{
		// use the size of the type as its default alignment
		if (itemAlign == 0)
			itemAlign = itemBytes;

		// remember the size of the last item
		lastItemSize = itemBytes * nItems;

		// the size of a union is the largest of the individual item sizes
		size = max(size, itemBytes * nItems);

		// the alignemnt is the largest of any individual item alignment
		align = max(align, itemAlign);
	}

	virtual void AddStruct(size_t itemBytes, size_t itemAlign = 0, int nItems = 1) override { Add(itemBytes, itemAlign, nItems); }
};

void JavascriptEngine::MarshallSizer::DoStruct()
{
	// measure the struct size
	SigParser subsig(p + 3, EndOfArg() - 1);
	MarshallStructSizer s(&subsig, GetCurVal());
	s.Marshall();

	// add it to our overall size
	AddStruct(s.size, s.align, 1);
}

void JavascriptEngine::MarshallSizer::DoUnion()
{
	// measure the union size
	SigParser subsig(p + 3, EndOfArg() - 1);
	MarshallUnionSizer s(&subsig, GetCurVal());
	s.Marshall();

	// add it to our overall size
	AddStruct(s.size, s.align, 1);
}

size_t JavascriptEngine::Marshaller::SizeofStruct(JsValueRef jsval, const TCHAR *flexErrorMsg)
{
	// expand references
	std::wstring_view refsig(p, EndOfArg() - p);
	if (*p == '@' && !inst->LookUpNativeType(p + 1, refsig.length() - 1, refsig))
		return 0;

	// measure the struct size
	SigParser subsig(refsig.data() + 3, refsig.data() + refsig.length() - 1);
	MarshallStructSizer s(&subsig, jsval);
	s.Marshall();

	// check for flex errors
	if (s.flex && flexErrorMsg != nullptr)
		Error(flexErrorMsg);

	// return the struct size
	return s.size;
}

size_t JavascriptEngine::Marshaller::SizeofUnion(JsValueRef jsval, const TCHAR *flexErrorMsg)
{
	// expand references
	std::wstring_view refsig(p, EndOfArg() - p);
	if (*p == '@' && !inst->LookUpNativeType(p + 1, refsig.length() - 1, refsig))
		return 0;

	// measure the struct size
	SigParser subsig(refsig.data() + 3, refsig.data() + refsig.length() - 1);
	MarshallUnionSizer s(&subsig, jsval);
	s.Marshall();

	// check for flex errors
	if (s.flex && flexErrorMsg != nullptr)
		Error(flexErrorMsg);

	// return the union size
	return s.size;
};

// Base class for marshalling values to native code
class JavascriptEngine::MarshallToNative : public Marshaller
{
public:
	MarshallToNative(SigParser *sig) : Marshaller(sig) { }

	MarshallToNative(const Marshaller &m) : Marshaller(m) { }

	// Store a value in newly allocated space
	template<typename T> void Store(T val)
	{
		if (void *p = Alloc(sizeof(val)); p != nullptr)
			*static_cast<T*>(p) = val;
	}

	// get the next value to marshall
	virtual JsValueRef GetNextVal() = 0;

	// "unget" the next value, if possible.  This applies to an argument
	// vector marshaller, allowing the same source argument to be used in
	// multiple native slots, as in IID_PPV_ARGS.
	virtual void UngetVal() { }

	// are we marshalling arguments?
	virtual bool IsArgvMarshaller() const { return false; }

	// allocate storage for a native value
	virtual void *Alloc(size_t size, int nItems = 1) = 0;

	// allocate storage for a native struct
	virtual void *AllocStruct(size_t size, int nItems = 1) { return Alloc(size, nItems); }

	virtual void DoBool() override { Store(static_cast<bool>(GetBool(GetNextVal()))); }
	virtual void DoInt8() override { Store(static_cast<INT8>(GetInt(GetNextVal(), INT8_MIN, INT8_MAX))); }
	virtual void DoUInt8() override { Store(static_cast<UINT8>(GetInt(GetNextVal(), 0, UINT8_MAX))); }
	virtual void DoInt16() override { Store(static_cast<INT16>(GetInt(GetNextVal(), INT16_MIN, INT16_MAX))); }
	virtual void DoUInt16() override { Store(static_cast<UINT16>(GetInt(GetNextVal(), 0, UINT16_MAX))); }
	virtual void DoInt32() override { Store(static_cast<INT32>(GetInt(GetNextVal(), INT32_MIN, INT32_MAX))); }
	virtual void DoUInt32() override { Store(static_cast<UINT32>(GetInt(GetNextVal(), 0, UINT32_MAX))); }
	virtual void DoInt64() override { Store(static_cast<INT64>(GetInt64(GetNextVal(), true))); }
	virtual void DoUInt64() override { Store(static_cast<UINT64>(GetInt64(GetNextVal(), false))); }
	virtual void DoIntPtr() override { Store(static_cast<INT_PTR>(IF_32_64(GetInt(GetNextVal(), INT32_MIN, INT32_MAX), GetInt64(GetNextVal(), true)))); }
	virtual void DoUIntPtr() override { Store(static_cast<INT_PTR>(IF_32_64(GetInt(GetNextVal(), 0, UINT32_MAX), GetInt64(GetNextVal(), false)))); }
	virtual void DoSizeT() override { Store(static_cast<SIZE_T>(IF_32_64(GetInt(GetNextVal(), 0, UINT32_MAX), GetInt64(GetNextVal(), false)))); }
	virtual void DoSSizeT() override { Store(static_cast<SSIZE_T>(IF_32_64(GetInt(GetNextVal(), INT32_MIN, INT32_MAX), GetInt64(GetNextVal(), true)))); }
	virtual void DoPtrdiffT() override { Store(static_cast<ptrdiff_t>(IF_32_64(GetInt(GetNextVal(), INT32_MIN, INT32_MAX), GetInt64(GetNextVal(), true)))); }
	virtual void DoFloat() override { Store(GetFloat(GetNextVal())); }
	virtual void DoDouble() override { Store(GetDouble(GetNextVal())); }
	virtual void DoHandle() override { Store(GetHandle(GetNextVal())); }

	virtual void DoArray() override = 0;

	virtual void DoVoid() override { Error(_T("dllImport: 'void' arguments are invalid")); }

	// Common handler for DoArray.  This figures the required storage size
	// for the array, allocates it via this->Alloc(), and marshalls the array
	// value into the storage area.
	void DoArrayCommon(JsValueRef jsval);

	virtual void DoString() override
	{
		// get the value to marshall, and get its type
		JsValueRef jsval = GetNextVal();
		JsValueType type;
		JsErrorCode err = JsGetValueType(jsval, &type);
		if (err != JsNoError)
		{
			Error(err, _T("dllImport: getting string argument type"));
			return;
		}

		// Let's check the type
		switch (type)
		{
		case JsNull:
		case JsUndefined:
			// pass a null pointer
			Store(nullptr);
			break;

		case JsArrayBuffer:
			// Array buffer.  This is an opaque byte array type that a caller
			// can use for a reference to any type.  Pass the callee the underlying
			// byte buffer.
			{
				ChakraBytePtr buffer = nullptr;
				unsigned int bufferLen;
				JsErrorCode err = JsGetArrayBufferStorage(jsval, &buffer, &bufferLen);
				if (err != JsNoError)
					Error(err, _T("dllImport: retrieving ArrayBuffer storage pointer"));

				// store the native pointer
				Store(buffer);
			}
			break;

		case JsTypedArray:
			// Typed array.  Make sure the array type matches the native string
			// type, then pass the callee the underlying byte/short array.
			{
				// get the typed array information
				ChakraBytePtr buf = nullptr;
				unsigned int buflen;
				JsTypedArrayType arrType;
				JsErrorCode err = JsGetTypedArrayStorage(jsval, &buf, &buflen, &arrType, nullptr);
				if (err != JsNoError)
				{
					Error(err, _T("DlImport: Getting typed array type for pointer argument"));
					return;
				}

				// the underlying type must exactly match the native type
				bool typeOk = false;
				switch (*p)
				{
				case 't':  typeOk = (arrType == JsArrayTypeInt8 || arrType == JsArrayTypeUint8); break;
				case 'T':  typeOk = (arrType == JsArrayTypeInt16 || arrType == JsArrayTypeUint16); break;
				}

				if (!typeOk)
				{
					Error(_T("dllImport: Javascript typed array type doesn't match native string argument type"));
					return;
				}

				// use the array buffer as the native pointer
				Store(buf);
			}
			break;

		default:
			// Other type.  Convert it to a Javascript string, then make a local
			// copy as a string of the appropriate type, and pass the native callee
			// a pointer to the local string copy.  There's no way to pass changes
			// to the string back to Javascript in this case.
			{
				// get the value as a string
				JsValueRef strval;
				JsErrorCode err;
				if ((err = JsConvertValueToString(jsval, &strval)) != JsNoError)
				{
					Error(err, _T("dllImport: converting argument to string"));
					return;
				}

				// retrieve the string pointer
				const wchar_t *strp;
				size_t len;
				if ((err = JsStringToPointer(strval, &strp, &len)) != JsNoError)
				{
					Error(err, _T("dllImport: retrieving string pointer"));
					return;
				}

				// convert it to the appropriate character string type
				switch (*p)
				{
				case 'T':
					// Unicode string
					wstrings.emplace_back(strp, len);
					Store(wstrings.back().c_str());
					break;

				case 't':
					// ANSI string
					if (len > static_cast<size_t>(INT_MAX))
					{
						Error(_T("dllImport: string is too long to convert to ANSI"));
						return;
					}
					cstrings.emplace_back(WideToAnsiCnt(strp, static_cast<INT>(len)));
					Store(cstrings.back().c_str());
					break;

				default:
					Error(MsgFmt(_T("dllImport: internal error: string type ID expected in signature %.*s, found '%c'"),
						static_cast<int>(sig->sig.length()), sig->sig.data(), *p));
					break;
				}
			}
			break;
		}
	}

	virtual void DoGuid() override
	{
		// we always marshall GUIDs from string values
		JsValueRef jsval = GetNextVal(), strval;
		JsErrorCode err;
		const wchar_t *p;
		size_t len;
		if ((err = JsConvertValueToString(jsval, &strval)) != JsNoError
			|| (err = JsStringToPointer(strval, &p, &len)) != JsNoError)
			return Error(err, _T("dllImport: getting string argument for GUID parameter"));

		// convert the string to a GUID
		GUID guid;
		if (!ParseGuid(p, len, guid))
			return Error(err, _T("dllImport: invalid GUID"));

		// store it
		Store(guid);
	}

	virtual void DoVariant() override
	{
		VARIANT v;
		VariantInit(&v);
		VariantData::CopyFromJavascript(&v, GetNextVal());
		Store(v);
	}

	virtual void DoBSTR() override
	{
		// get the javascript value
		JsValueRef jsval = GetNextVal();

		// if it's already a BSTR value, pass it directly
		if (auto obj = NativeTypeWrapper::Recover<NativeTypeWrapper>(jsval, nullptr); obj != nullptr)
		{
			if (obj->sig == L"B")
			{
				Store(*reinterpret_cast<BSTR*>(obj->data));
				return;
			}
		}

		// For anything else, get its Javascript string value (converting
		// to string if necessary), and create a temporary BSTR out of it.
		JsErrorCode err;
		JsValueRef jsstr;
		const WCHAR *p;
		size_t len;
		if ((err = JsConvertValueToString(jsval, &jsstr)) != JsNoError
			|| (err = JsStringToPointer(jsstr, &p, &len)) != JsNoError)
			return Error(err, _T("dllImport: converting argument to BSTR"));

		if (len > static_cast<size_t>(UINT_MAX))
			return Error(_T("dllImport: string argument is too long to convert to BSTR"));

		// create a BSTR and store it in the native slot
		BSTR bstr = SysAllocStringLen(p, static_cast<UINT>(len));
		Store(bstr);

		// schedule the native BSTR for cleanup
		ScheduleBSTRCleanup(bstr);
	}

	// schedule BSTR cleanup for a temporary BSTR passed to native code
	virtual void ScheduleBSTRCleanup(BSTR bstr) = 0;

	// locally allocated string copies, to be cleaned up on return
	std::list<WSTRING> wstrings;
	std::list<CSTRING> cstrings;

	virtual void DoPointer() override;

	virtual void DoFunction() override
	{
		// functions can't be passed by value, only by reference
		Error(_T("dllImport: functions can't be passed by value (pointer required)"));
	}

	virtual void DoStruct() override;
	virtual void DoUnion() override;

	virtual void DoInterface() override
	{
		Error(_T("dllImport: interfaces can't be passed by value (pointer required)"));
	}

	// get a boolean value
	bool GetBool(JsValueRef v)
	{
		// convert to a JS boolean
		JsErrorCode err;
		JsValueRef boolVal;
		if ((err = JsConvertValueToBoolean(v, &boolVal)) != JsNoError)
		{
			Error(err, _T("dllImport: marshalling bool argument"));
			return false;
		}

		// convert to a C bool
		bool b;
		if ((err = JsBooleanToBool(boolVal, &b)) != JsNoError)
		{
			Error(err, _T("dllImport: marshalling bool argument"));
			return false;
		}

		return b;
	}

	// get a numeric value as a double 
	double GetDouble(JsValueRef v)
	{
		// if it's an INT64 or UINT64, convert it to number explicitly
		JsValueType type;
		if (JsGetValueType(v, &type) != JsNoError && type == JsObject)
		{
			// check for Int64
			if (auto obj = XInt64Data<INT64>::Recover<XInt64Data<INT64>>(v, nullptr); obj != nullptr)
			{
				// range-check the signed value
				if (obj->i < static_cast<INT64>(-MaxIntInDouble) || obj->i > static_cast<INT64>(MaxIntInDouble))
					Error(_T("dllImport: Int64 value is out of range for conversion to Number"));

				// cast it and return the result
				return static_cast<double>(obj->i);
			}

			// check for Uint64
			if (auto obj = XInt64Data<UINT64>::Recover<XInt64Data<UINT64>>(v, nullptr); obj != nullptr)
			{
				// range-check the unsigned value
				if (obj->i > static_cast<UINT64>(MaxIntInDouble))
					Error(_T("dllImport: Int64 value is out of range for conversion to Number"));

				// cast it and return the result
				return static_cast<double>(obj->i);
			}
		}

		// convert to numeric if necessary
		JsErrorCode err;
		JsValueRef numVal;
		if ((err = JsConvertValueToNumber(v, &numVal)) != JsNoError)
		{
			Error(err, _T("dllImport: marshalling integer argument"));
			return std::numeric_limits<double>::quiet_NaN();
		}

		// Retrieve the double value.  Javascript represents all numbers as
		// doubles internally, so no conversion is required to convert to a
		// native C++ double and there's no need for range checking.
		double d;
		if ((err = JsNumberToDouble(numVal, &d)) != JsNoError)
		{
			Error(err, _T("dllImport: marshalling integer argument"));
			return std::numeric_limits<double>::quiet_NaN();
		}

		// return it
		return d;
	};

	// get a float value
	float GetFloat(JsValueRef v)
	{
		// get the double value from javascript
		double d = GetDouble(v);

		// check the range
		if (d < -FLT_MAX || d > FLT_MAX)
		{
			Error(_T("dllImport: single-precision float argument value out of range"));
			return NAN;
		}

		// return it
		return static_cast<float>(d);
	};

	// get an integer value (up to 32 bits)
	double GetInt(JsValueRef v, double minVal, double maxVal)
	{
		// get the original double value from javascript
		double d = GetDouble(v);

		// check the range
		if (d < minVal || d > maxVal)
		{
			Error(_T("dllImport: integer argument value out of range"));
			return 0;
		}

		// return it as a double, so that the caller can do the appropriate sign
		// extension in the conversion process
		return d;
	};

	// get a 64-bit integer value
	INT64 GetInt64(JsValueRef v, bool isSigned)
	{
		// check the value type
		JsErrorCode err;
		JsValueType t;
		if ((err = JsGetValueType(v, &t)) != JsNoError)
		{
			Error(err, _T("dllImport: JsGetValueType failed converting 64-bit integer argument"));
			return 0;
		}

		// if it's a numeric value, convert it from the JS double representation
		if (t == JsNumber)
		{
			// Numeric type - get the double value
			double d = GetDouble(v);

			// check the range
			if (isSigned ? (d < (double)INT64_MIN || d >(double)INT64_MAX) : (d < 0 || d >(double)UINT64_MAX))
			{
				Error(_T("dllImport: 64-bit integer argument out of range"));
				return 0;
			}

			// Return the value reinterpreted as a 
			if (isSigned)
				return static_cast<INT64>(static_cast<UINT64>(d));
			else
				return static_cast<INT64>(d);
		}

		// if it's an Int64 or Uint64 object, convert it
		if (t == JsObject)
		{
			if (auto obj = XInt64Data<INT64>::Recover<XInt64Data<INT64>>(v, nullptr); obj != nullptr)
			{
				// if the caller wants an unsigned result, it's an error if the value is negative
				if (!isSigned && obj->i < 0)
					Error(_T("dllImport: 64-bit unsigned integer argument value is negative"));
				return obj->i;
			}
			if (auto obj = XInt64Data<UINT64>::Recover<XInt64Data<UINT64>>(v, nullptr); obj != nullptr)
			{
				// if the caller wants a signed result, range-check the unsigned value
				if (isSigned && obj->i > static_cast<UINT64>(INT64_MAX))
					Error(_T("dllImport: 64-bit signed integer argument out of range"));
				return obj->i;
			}
		}

		// otherwise, interpret it as a string value
		JsValueRef strval;
		if ((err = JsConvertValueToString(v, &strval)) != JsNoError)
		{
			Error(err, _T("dllImport: converting 64-bit integer argument value to string"));
			return 0;
		}

		// parse the string
		if (isSigned)
		{
			INT64 i;
			XInt64Data<INT64>::ParseString(v, i);
			return i;
		}
		else
		{
			UINT64 i;
			XInt64Data<UINT64>::ParseString(v, i);
			return static_cast<INT64>(i);
		}
	};

	HANDLE GetHandle(JsValueRef v)
	{
		// check the value type
		JsErrorCode err;
		JsValueType t;
		if ((err = JsGetValueType(v, &t)) != JsNoError)
		{
			Error(err, _T("dllImport: JsGetValueType failed converting HANDLE argument"));
			return NULL;
		}

		switch (t)
		{
		case JsNull:
		case JsUndefined:
			// treat null/undefined as a null handle
			return NULL;

		case JsNumber:
			// interpret a number into an int, and convert that to a handle
			{
				double d;
				JsNumberToDouble(v, &d);
				return reinterpret_cast<HANDLE>(static_cast<INT_PTR>(d));
			}

		case JsObject:
			// if it's an external HandleData object, use that handle; otherwise it's an error
			{
				auto h = JavascriptEngine::HandleData::Recover<HandleData>(v, _T("dllImport: converting HANDLE argument"));
				return h != nullptr ? h->h : NULL;
			}

		default:
			Error(err, _T("dllImport: invalid value for HANDLE argument"));
			return NULL;
		}
	};
};

// Count argument slots
class JavascriptEngine::MarshallStackArgSizer : public MarshallSizer
{
public:
	MarshallStackArgSizer(SigParser *sig, JsValueRef *argv, int argc, int firstArg) :
		MarshallSizer(sig),
		jsArgv(argv), jsArgc(argc), jsArgCur(firstArg)
	{ }

	virtual JsValueRef GetCurVal() { return jsArgCur < jsArgc ? jsArgv[jsArgCur] : inst->undefVal; }

	virtual bool Marshall()
	{
		// Check the return type.  If it's a struct/union BY VALUE, AND
		// its size is more than 8 bytes, it requires special handling in 
		// the argument vector.  On both x86 and x64, the Microsoft compiler
		// inserts a hidden first argument into the parameters that contains
		// a pointer to an instance of the struct allocated by the caller.
		// A function prototype like this:
		//
		//    struct foo func(actual_args...)
		//
		// is effectively rewritten like this:
		//
		//    struct foo *func(struct foo *<unnamed>, actual_args...)
		//
		// Note that the 8-byte limit applies to both x86 and x64.  In both
		// cases, a struct that can be packed into 8 bytes is returned using
		// the platform's standard return registers for a 64-bit scalar
		// (EDX:EAX for x86, RAX for x64), so the hidden parameter isn't
		// required.
		if (*p == '@' && (p[1] == 'S' || p[1] == 'U'))
		{
			const TCHAR *flexErr = _T("dllImport: struct with unspecified array dimension can't be used as a return value");
			size_t size = p[1] == 'S' ? SizeofStruct(JS_INVALID_REFERENCE, flexErr) : SizeofUnion(JS_INVALID_REFERENCE, flexErr);
			if (size > 8)
			{
				hiddenStructArg = true;
				Add(sizeof(void *));
			}
		}

		// skip the return value entry
		NextArg();

		// do the rest of the marshalling normally
		return __super::Marshall();
	}

	virtual void Add(size_t itemBytes, size_t itemAlign = 0, int nItems = 1) override
	{
		// figure the number of slots required, widening to the slot size
		size_t slotsPerItem = (itemBytes + argSlotSize - 1) / argSlotSize;

		// add the number of stack slots required
		nSlots += slotsPerItem * nItems;
	}

	virtual void AddStruct(size_t itemBytes, size_t itemAlign = 0, int nItems = 1) override
	{
		// array arguments are always passed by reference 
		if (nItems > 1)
			return Add(argSlotSize);

#if defined(_M_IX86)
		// x86 mode.  Arbitrary structs can be passed on the stack.  Simply use
		// the standard stack allocation.
		return Add(itemBytes, itemAlign);

#elif defined(_M_X64)
		// x64 mode.  A struct can be passed inline on the stack only if it fits
		// in a single 64-bit (8-byte) stack slot.  All other structs must be
		// passed by reference.
		return itemBytes < argSlotSize ? Add(itemBytes, itemAlign) : Add(argSlotSize);

#else
#error This platform is not supported - add an #elif case for it here
#endif
	}

	// structs, unions, functions, and void can't be passed by value
	virtual void DoFunction() override { Error(_T("dllImport: function by value parameters are not supported (pointer type required)")); }
	virtual void DoVoid() override { Error(_T("dllImport: 'void' is not a valid parameter type")); }

	// array-by-value decays to a pointer to the underlying type
	virtual void DoArray() override { Add(sizeof(void*)); }

	// argument array from the Javascript caller
	JsValueRef *jsArgv;
	int jsArgc;
	int jsArgCur;

	// Number of stack slots required for the native argument vector.  This
	// counts the actual stack usage based on item size.  For example, a struct
	// that requires 16 bytes will take up 4 slots in 32-bit mode.
	size_t nSlots = 0;

	// is there a hidden first argument for a return-by-value struct?
	bool hiddenStructArg = false;
};


// Variant argument sizer.  This counts the number of arguments to
// an IDispatch function taking a VARIANGARG array.
class JavascriptEngine::MarshallVariantArgSizer : public MarshallSizer
{
public:
	MarshallVariantArgSizer(SigParser *sig) : MarshallSizer(sig)
	{ }

	virtual bool Marshall()
	{
		// skip the return value entry
		NextArg();

		// do the rest of the marshalling normally
		return __super::Marshall();
	}

	virtual JsValueRef GetCurVal() override { return JS_INVALID_REFERENCE; }

	virtual void Add(size_t itemBytes, size_t itemAlign = 0, int nItems = 1) override { ++nSlots; }
	virtual void AddStruct(size_t itemBytes, size_t itemAlign = 0, int nItems = 1) override { ++nSlots; }
	virtual void DoArray() override { ++nSlots; }

	// structs, unions, functions, and void can't be passed by value
	virtual void DoFunction() override { Error(_T("dllImport: function by value parameters are not supported (pointer type required)")); }
	virtual void DoVoid() override { Error(_T("dllImport: 'void' is not a valid parameter type")); }

	// Number of stack slots required for the native argument vector.  This
	// counts the actual stack usage based on item size.  For example, a struct
	// that requires 16 bytes will take up 4 slots in 32-bit mode.
	int nSlots = 0;
};



// marshall arguments to the native argument vector in the stack
class JavascriptEngine::MarshallToNativeArgv : public MarshallToNative
{
public:
	MarshallToNativeArgv(SigParser *sig,
		arg_t *nativeArgArray, JsValueRef *argvIn, int argcIn, int firstDllArg) :
		MarshallToNative(sig),
		nativeArgArray(nativeArgArray), argOut(nativeArgArray), 
		argvIn(argvIn), argcIn(argcIn), argInCur(firstDllArg), firstDllArg(firstDllArg)		
	{ }

	// we're an argument marshaller
	virtual bool IsArgvMarshaller() const override { return true; }

	// Flag: we allocated a hidden struct/union for a by-value return
	JsValueRef structByValueReturn = JS_INVALID_REFERENCE;

	// pointer to and byte size of the native data of the by-value return struct
	void *structByValueReturnPtr = nullptr;
	size_t structByValueReturnSize = 0;

	virtual bool Marshall()
	{
		// If the return type is a struct/union BY VALUE, we need to allocate
		// a native wrapper Javascript object as the return value.
		//
		// There's an additional wrinkle.  If the struct fits in 8 bytes, the
		// native code will pack the result struct into the return register(s) 
		// (EDX:EAX or RAX).  If not, the native code expects us to allocate
		// a temp struct (which we're going to do in any case - that's the
		// JS native wrapper we just mentioned above) and insert an extra,
		// hidden first argument containing a pointer to the temp struct. 
		// For structs <= 8 bytes, no temp pointer is required because of the
		// register return convention.
		if (*p == '@' && (p[1] == 'S' || p[1] == 'U'))
		{
			// create the native object
			NativeTypeWrapper *wrapper = nullptr;
			SigParser subsig(p, EndOfArg());
			structByValueReturn = inst->CreateNativeObject(&subsig, nullptr, &wrapper);

			// figure the size of the returned struct
			structByValueReturnSize = (p[1] == 'S' ? SizeofStruct(JS_INVALID_REFERENCE, nullptr) : SizeofUnion(JS_INVALID_REFERENCE, nullptr));

			// If wrapper object creation failed, allocate temp space for the return 
			// struct.  (This is just an extra bit of protection against crashing with 
			// a write to a null pointer or an uninitialized pointer.  We shouldn't
			// actually make it far enough to to the write, though, because the failure
			// to create the wrapper should have recorded a marshaller error that will
			// cause the overall argv marshalling to fail, which should prevent us 
			// from reaching the native call.)
			void *data = wrapper != nullptr ? wrapper->data : inst->marshallerContext->Alloc(structByValueReturnSize);

			// if the struct size is over 8 bytes, add the hidden argument
			if (structByValueReturnSize > 8)
			{
				// pass a pointer to the native storage as the hidden first argument
				*static_cast<void**>(Alloc(sizeof(void *))) = wrapper->data;
			}
			else
			{
				// The callee will return the struct contents in EDX:EAX/RAX.
				// We'll have to manually copy the contents from the registers 
				// after the callee returns.  Save the pointer to the struct
				// memory for that later copying.
				structByValueReturnPtr = wrapper->data;
			}
		}

		// skip the return value entry
		NextArg();

		// do the rest of the marshalling normally
		return __super::Marshall();
	}

	virtual void DoVariant() override
	{
		// Don't allow passing variants by value.  The cleanup implications
		// are too messy - variants can contain resources that have to be
		// freed when the variant is destroyed, and a variant on the stack
		// can be beyond our control on return for some calling conventions.
		Error(_T("VARIANT cannot be passed as an argument by value"));
	}

	virtual void ScheduleBSTRCleanup(BSTR bstr) override
	{
		// temporary BSTRs created as stack arguments marshalling must
		// be deleted on return
		class BSTRCleanupItem : public MarshallerContext::CleanupItem 
		{
		public:
			BSTRCleanupItem(BSTR bstr) : bstr(bstr) { }
			~BSTRCleanupItem() { if (bstr != nullptr) SysFreeString(bstr); }
			BSTR bstr;
		};
		inst->marshallerContext->AddCleanupItem(new BSTRCleanupItem(bstr));
	}

	virtual void DoArray() override
	{
		// In an argument list, an array decays to a pointer to the
		// underlying type.  This only goes one deep, so scan to the
		// first ']', and process the rest as though it were a pointer
		// to the underlying type.
		for (const TCHAR *end = sig->sigEnd(); p < end && *p != ']'; ++p);

		// Note that we leave p parked at the ']', because DoPointer
		// expects to be lined at the pointer signifier, usually * or &,
		// but ] works too.
		DoPointer();
	}

	// Get and consume the next Javascript input argument.  Returns 'undefined' 
	// if we're past the last argument.
	virtual JsValueRef GetNextVal() override
	{
		return (argInCur < argcIn ? argvIn[argInCur++] : inst->undefVal);
	}

	JsValueRef GetCurVal()
	{
		return (argInCur < argcIn ? argvIn[argInCur] : inst->undefVal);
	}

	virtual void UngetVal() override { --argInCur; }

	// allocate storage
	void *Alloc(size_t size, int nItems = 1) override
	{
		// arrays are always allocated by reference
		if (nItems > 1)
			return AllocStructByRef(size, nItems);

		// the storage comes from the next argument vector slot
		void *p = argOut;

		// consume the required number of slots
		argOut += (size + argSlotSize - 1) / argSlotSize;

		// return the argument slot
		return p;
	}

	// allocate storage for an inline struct
	void *AllocStruct(size_t size, int nItems = 1)
	{
		// All array arguments are passed by reference
		if (nItems > 1)
			return AllocStructByRef(size, nItems);

#if defined(_M_IX86)
		// x86 mode.  Arbitrary structs can be passed on the stack.  Simply use
		// the standard stack allocation.
		return Alloc(size);

#elif defined(_M_X64)
		// x64 mode.  A struct can be passed inline on the stack only if it fits
		// in a single 64-bit (8-byte) stack slot.  All other structs must be
		// passed by reference.
		return size < argSlotSize ? Alloc(size) : AllocStructByRef(size);

#else
#error This platform is not supported - add an #elif case for it here
#endif
	}

	void *AllocStructByRef(size_t size, int nItems = 1)
	{
		// allocate space for a local copy that we can pass by reference
		void *p = inst->marshallerContext->Alloc(size * nItems);

		// store a pointer to the newly allocated memory as the inline value
		*static_cast<void**>(Alloc(sizeof(void*))) = p;

		// return the allocated copy area for the caller to populate
		return p;
	}

	// native argument array
	arg_t *nativeArgArray;

	// current argument array output slot
	arg_t *argOut;

	// Javascript argument vector
	JsValueRef *argvIn;
	int firstDllArg;
	int argcIn;

	// current input argument index
	int argInCur;
};

class JavascriptEngine::MarshallToNativeArray : public MarshallToNative
{
public:
	MarshallToNativeArray(SigParser *sig, JsValueRef jsArray,
		void *nativeArray, size_t eleSize, int nEles) :
		MarshallToNative(sig),
		jsArray(jsArray),
		nativeArray(static_cast<BYTE*>(nativeArray)), eleSize(eleSize), nEles(nEles)
	{ }

	virtual void MarshallValue() override
	{
		// marshall each array element
		for (int i = 0; i < nEles; ++i)
		{
			// reset to the start of the signature
			p = sig->sig.data();

			// marshall this value
			__super::MarshallValue();
		}
	}

	// Don't allow marshalling arrays of BSTR or VARIANT to native code
	virtual void DoBSTR() override { Error(_T("Array of BSTR cannot be passed to native code")); }
	virtual void DoVariant() override { Error(_T("Array of VARIANT cannot be passed to native code")); }
	virtual void ScheduleBSTRCleanup(BSTR) { /* not necessary because of DoBSTR() prohibition */ }

	// get the next value
	virtual JsValueRef GetNextVal() override
	{
		// get the index as a javascript value
		JsValueRef jsIdx;
		JsIntToNumber(idxIn++, &jsIdx);

		// retrieve the indexed element
		JsValueRef val;
		JsErrorCode err = JsGetIndexedProperty(jsArray, jsIdx, &val);
		if (err != JsNoError)
		{
			Error(err, _T("dllImport: indexing argument array"));
			return inst->nullVal;
		}

		// return the value
		return val;
	}

	// store a value
	virtual void *Alloc(size_t size, int nItems = 1)
	{
		void *ret = nullptr;
		if (idxOut + nItems <= nEles)
		{
			ret = nativeArray + (idxOut * eleSize);
			idxOut += nItems;
		}
		return ret;
	}

	virtual void DoArray()
	{
		// An array within an array isn't possible, as we collapse adjacent
		// array dimensions into a C-style square array.
		Error(_T("dllImport: array of array not supported"));
	}

	// javascript array
	JsValueRef jsArray;

	// current input element
	int idxIn = 0;

	// current output element
	int idxOut = 0;

	// native array: pointer, element size, number of elements
	BYTE *nativeArray;
	size_t eleSize;
	int nEles;
};

// Marshall a reference into a native representation.  This allocates local storage
// in the native stack for the referenced value and copies the referenced value into
// the allocated space.
class JavascriptEngine::MarshallToNativeByReference : public MarshallToNative
{
public:
	MarshallToNativeByReference(SigParser *sig, JsValueRef jsval) :
		MarshallToNative(sig), jsval(jsval)
	{
		// get the value type
		if (JsGetValueType(jsval, &jstype) != JsNoError)
			jstype = JsUndefined;
	}

	virtual void MarshallValue() override
	{
		// if we've already marshalled this object, return the existing pointer
		auto &map = inst->marshallerContext->byRefMarshalledObjects;
		if (auto it = map.find(jsval); it != map.end())
		{
			pointer = it->second;
			return;
		}

		// do the normal work
		__super::MarshallValue();

		// store the pointer in the map, in case we encounter this object again
		// during this call
		map.emplace(jsval, pointer);
	}

	// Pointer to the native storage for the referenced value.  This is set
	// by Marshall() to the allocated storage.
	void *pointer = nullptr;

	virtual void DoArray() override { DoArrayCommon(jsval); }

	// allocate native storage
	virtual void *Alloc(size_t size, int nItems = 1) override { return pointer = inst->marshallerContext->Alloc(size * nItems); }

	// get the next value
	virtual JsValueRef GetNextVal() override { return jsval; }

	// no BSTR cleanup is necessary when passing by reference
	virtual void ScheduleBSTRCleanup(BSTR) override { }

	// the javascript source value we're marshalling
	JsValueRef jsval;
	JsValueType jstype;
};

// Marshall a value into a native struct or union
class JavascriptEngine::MarshallToNativeStruct : public MarshallToNative
{
public:
	MarshallToNativeStruct(SigParser *sig, JsValueRef jsval, void *pointer, size_t size) :
		MarshallToNative(sig),
		structSizer(sig, jsval),
		jsval(jsval), pointer(static_cast<BYTE*>(pointer)), size(size)
	{ }

	virtual void DoArray() override { DoArrayCommon(propval); }

	virtual bool Marshall() override
	{
		// get the value we're converting, and get its type
		JsValueType jstype;
		JsErrorCode err;
		if ((err = JsGetValueType(jsval, &jstype)) != JsNoError)
		{
			Error(err, _T("dllImport: getting value type for struct argument"));
			return false;
		}

		// we can't dereference null or undefined
		if (jstype == JsNull || jstype == JsUndefined)
		{
			Error(err, _T("dllImport: null or missing value for struct argument"));
			return false;
		}

		// we can only convert object types
		if (jstype != JsObject)
		{
			Error(err, _T("dllImport: object required for struct argument"));
			return false;
		}

		// check for a native struct
		if (auto obj = NativeTypeWrapper::Recover<NativeTypeWrapper>(jsval, nullptr); obj != nullptr)
		{
			// the type signature has to match exactly
			if (obj->sig == sig->sig)
			{
				Error(_T("dllImport: wrong struct/union type for argument"));
				return false;
			}

			// copy the data from the source object
			memcpy(pointer, obj->data, size);

			// success
			return true;
		}

		// visit each member
		MarshallStructMembers([this](WSTRING &memberName, WSTRING &memberSig)
		{
			// Marshall the field into the sizer.  This will set the "last offset"
			// field in the sizer to the current field offset, taking into account
			// its alignment.
			structSizer.MarshallValue();
			structSizer.NextArg();

			// look up the property in the object
			JsErrorCode err;
			JsPropertyIdRef propId;
			if ((err = JsGetPropertyIdFromName(memberName.c_str(), &propId)) != JsNoError)
			{
				Error(err, _T("dllImport: looking up property name for struct conversion"));
				return false;
			}

			// check if the object has the property
			bool hasProp;
			if ((err = JsHasProperty(jsval, propId, &hasProp)) == JsNoError && hasProp)
			{
				// retrieve the property value
				if ((err = JsGetProperty(jsval, propId, &propval)) != JsNoError)
				{
					Error(err, _T("dllImport: retrieving property value for struct conversion"));
					return false;
				}

				// marshall the current value
				MarshallValue();
			}
			else if (memberName == L"cbSize")
			{
				const WCHAR *tp = p;
				if (*tp == '%')
					++tp;

				// 'cbSize' is special.  A property named cbSize that doesn't have a
				// Javascript in value specified will be automatically filled in with
				// the size of the containing struct.  The caller can override this
				// for the whole struct by simply using a different name for the
				// element, and can override it for an individual instance by specifying
				// a value in the Javsacript object passed in.  Further, cbSize is only
				// special if its type is some kind of integer type..
				switch (*tp)
				{
				case 's':
				case 'S':
				case 'i':
				case 'I':
				case 'l':
				case 'L':
				case 'z':
				case 'Z':
				case 'P':
					// Convert the size to a Javascript number, then marshall it
					// into the struct field.  This is a little roundabout, as we
					// have the size as an integer already, but this lets us reuse
					// the code to do the appropriate conversion to the native type.
					JsDoubleToNumber(static_cast<double>(size), &propval);
					MarshallValue();
					break;
				}
			}

			return true;
		});

		// success
		return true;
	}

	// Don't allow marshalling structs containing BSTR or VARIANT, as it's
	// too hard to figure out what to do about cleanup.
	virtual void DoBSTR() override { Error(_T("Array of BSTR cannot be passed to native code")); }
	virtual void DoVariant() override { Error(_T("Array of VARIANT cannot be passed to native code")); }
	virtual void ScheduleBSTRCleanup(BSTR) { /* not necessary because of DoBSTR() prohibition */ }

	// the next marshalling value is the current property value
	virtual JsValueRef GetNextVal() override { return propval; }

	// the next allocation goes into the current native slot
	virtual void *Alloc(size_t size, int nItems = 1) override { return pointer + structSizer.lastItemOfs; }

	// javascript object value that we're marshalling into a native representation
	JsValueRef jsval;

	// current property value being visited
	JsValueRef propval = JS_INVALID_REFERENCE;

	// struct sizer - we walk this through the fields as we populate the struct, to
	// keep track of the current slot offset in the struct
	MarshallStructSizer structSizer;

	// pointer to the native memory
	BYTE *pointer;

	// size of the native memory area (the full struct size)
	size_t size;
};

class JavascriptEngine::MarshallToNativeUnion : public MarshallToNativeStruct
{
public:
	MarshallToNativeUnion(SigParser *sig, JsValueRef jsval, void *pointer, size_t size) :
		MarshallToNativeStruct(sig, jsval, pointer, size)
	{ }

	// union members always go at the start of the shared memory area
	virtual void *Alloc(size_t size, int nItems = 1) override { return pointer; }
};

void JavascriptEngine::MarshallToNative::DoArrayCommon(JsValueRef jsval)
{
	// Parse the array dimension.  Note that this only parses the first 
	// dimension; if there are multiple dimensions, the others will fall
	// out of the recursive measurement of the underlying type size.
	int dim;
	bool isEmpty;
	if (!ParseArrayDim(p, sig->sigEnd(), dim, isEmpty))
		return;

	// figure the size of the underlying type
	SigParser subsig(p, EndOfArg());
	MarshallBasicSizer sizer(&subsig, jsval);
	sizer.MarshallValue();

	// indeterminate size isn't allowed in a sub-array
	if (sizer.flex)
	{
		Error(_T("dllImport: sub-array with indeterminate dimension is invalid"));
		return;
	}

	if (sizer.size != 0)
	{
		// if we have a flex dimension, figure the actual size
		if (isEmpty)
		{
			if (!GetActualArrayDim(jsval, dim, sizer.size))
				return;
		}

		// marshall the native array
		SigParser subsig(p, EndOfArg());
		MarshallToNativeArray ma(&subsig, jsval, Alloc(sizer.size, dim), sizer.size, dim);
		ma.MarshallValue();
	}
}

void JavascriptEngine::MarshallToNative::DoPointer()
{
	// get the javascript argument and its type
	JsErrorCode err;
	JsValueRef jsval = GetNextVal();
	JsValueType jstype;
	if (JsGetValueType(jsval, &jstype) != JsNoError)
		jstype = JsNull;

	// get the referenced type
	const WCHAR *tp = p + 1;
	if (tp < sig->sigEnd() && *tp == '%')
		++tp;

	// check the type
	switch (jstype)
	{
	case JsNull:
	case JsUndefined:
		// A null or missing/undefined value passed as a pointer type maps
		// to a native null pointer.  Nulls can't be used for references.
		if (*p == '&')
			return Error(_T("dllImport: null or missing value is invalid for a reference ('&') type"));

		// store the null
		Store<void*>(nullptr);
		break;

	case JsString:
		// If the underlying type is an 8- or 16-bit int, convert the string
		// to a character buffer of the appropriate type, with null termination,
		// and pass a pointer to the buffer.
		{
			// get the string's contents
			const wchar_t *p;
			size_t len;
			if ((err = JsStringToPointer(jsval, &p, &len)) != JsNoError)
				return Error(err, _T("dllImport: getting argument string text"));

			// convert it to a buffer of the appropriate underlying type
			void *pointer = nullptr;
			switch (*tp)
			{
			case 'c':
			case 'C':
				// Pointer to int8.  Marshall as a pointer to a null-terminated
				// buffer of ANSI characters.
				if (len > static_cast<size_t>(INT_MAX))
					return Error(_T("dllImport: string is too long to convert to ANSI"));

				pointer = inst->marshallerContext->Alloc(len + 1);
				WideCharToMultiByte(CP_ACP, 0, p, static_cast<int>(len), static_cast<LPSTR>(pointer), static_cast<int>(len + 1), NULL, NULL);
				Store(pointer);
				break;

			case 's':
			case 'S':
				// Pointer to int16.  Marshall as a pointer to a null-terminated
				// buffer of Unicode characters.
				pointer = inst->marshallerContext->Alloc(sizeof(WCHAR)*(len + 1));
				memcpy(pointer, p, len * sizeof(WCHAR));
				static_cast<LPWSTR>(pointer)[len] = 0;
				Store(pointer);
				break;

			case 'G':
				// GUID.  Parse the string into a temporary GUID struct, and pass a
				// pointer to the temp struct.
				pointer = inst->marshallerContext->Alloc(sizeof(GUID));
				if (!ParseGuid(p, len, *static_cast<GUID*>(pointer)))
					return Error(_T("dllImport: invalid GUID string"));
				Store(pointer);
				break;

			default:
				// strings can't be passed for other reference types
				Error(_T("dllImport: string argument can only be used for char and wchar pointers"));
				break;
			}
		}
		break;

	case JsArrayBuffer:
		// Array buffer object.  This is a Javascript object containing an array
		// of bytes.  This JS type is specifically for interchange with native 
		// code under control of the JS code, so simply pass a pointer to the
		// JS buffer storage.
		{
			ChakraBytePtr buffer = nullptr;
			unsigned int bufferLen;
			if ((err = JsGetArrayBufferStorage(jsval, &buffer, &bufferLen)) != JsNoError)
				Error(err, _T("dllImport: retrieving ArrayBuffer storage pointer"));

			// store the native pointer
			Store(buffer);
		}
		break;

	case JsArray:
		// Javascript array.  Allocate an array of N of the underlying native
		// type, where N is the length of the array, and convert the values from
		// the Javascript array into the native array.
		{
			// get the length
			int len = GetArrayLength(jsval);

			// if it's not empty, marshall the array; otherwise use a null pointer
			void *pointer = nullptr;
			if (len > 0)
			{
				// measure the size of the underlying type
				SigParser subsig(p + 1, EndOfArg());
				MarshallBasicSizer sizer(&subsig, jsval);
				sizer.MarshallValue();

				// allocate temporary storage for the array copy
				void *pointer = inst->marshallerContext->Alloc(sizer.size * len);

				// marshall the array values into the native array
				MarshallToNativeArray ma(&subsig, jsval, pointer, sizer.size, len);
				ma.MarshallValue();

				// store the temporary array pointer as the result
				Store(pointer);
			}
		}
		break;

	case JsTypedArray:
		// Typed array.  Ensure the type matches, and if so, pass a direct pointer
		// to the array buffer.
		{
			// get the typed array information
			ChakraBytePtr buf = nullptr;
			unsigned int buflen;
			JsTypedArrayType arrType;
			if ((err = JsGetTypedArrayStorage(jsval, &buf, &buflen, &arrType, nullptr)) != JsNoError)
			{
				Error(err, _T("DlImport: Getting typed array type for pointer argument"));
				return;
			}

			// the underlying type must exactly match the native type
			bool typeOk = false;
			switch (*tp)
			{
			case 'c':  typeOk = arrType == JsArrayTypeInt8; break;
			case 'C':  typeOk = arrType == JsArrayTypeUint8; break;
			case 's':  typeOk = arrType == JsArrayTypeInt16; break;
			case 'S':  typeOk = arrType == JsArrayTypeUint16; break;
			case 'i':  typeOk = arrType == JsArrayTypeInt32; break;
			case 'I':  typeOk = arrType == JsArrayTypeUint32; break;
			case 'f':  typeOk = arrType == JsArrayTypeFloat32; break;
			case 'd':  typeOk = arrType == JsArrayTypeFloat64; break;
			}

			if (!typeOk)
			{
				Error(_T("dllImport: Javascript typed array type doesn't match native pointer argument type"));
				return;
			}

			// use the array buffer as the native pointer
			Store(buf);
		}
		break;

	case JsFunction:
		// function
		{
			// a function can only be passed in a function pointer slot
			if (*tp != '(')
			{
				Error(_T("dllImport: function argument value can only be used for a function pointer parameter"));
				return;
			}

			// look for a callback thunk
			JsValueRef thunk = JS_INVALID_REFERENCE;
			bool hasThunk;
			if ((err = JsHasOwnProperty(jsval, inst->callbackPropertyId, &hasThunk)) != JsNoError
				|| (hasThunk && (err = JsGetProperty(jsval, inst->callbackPropertyId, &thunk)) != JsNoError))
			{
				Error(err, _T("dllImport: getting callback function thunk"));
				return;
			}

			// recover the thunk or create a new one
			JavascriptCallbackWrapper *wrapper = nullptr;
			if (hasThunk)
			{
				// retrieve and validate the external data
				const TCHAR *where = nullptr;
				if (!JavascriptCallbackWrapper::Recover<JavascriptCallbackWrapper>(thunk, where))
				{
					Error(err, MsgFmt(_T("dllImport: recovering callback function thunk data: %s"), where));
					return;
				}
			}
			else
			{
				// create a new thunk
				SigParser subsig(tp + 1, EndOfArg(tp) - 1);
				if ((err = CreateExternalObject(thunk, wrapper = new JavascriptCallbackWrapper(jsval, &subsig))) != JsNoError)
				{
					Error(err, _T("dllImport: creating callback function thunk external object"));
					return;
				}

				// Cross-reference the function and the external thunk object.  This
				// will keep the thunk alive as long as the function is alive, and
				// vice versa, ensuring they're collected as a pair.
				if ((err = JsSetProperty(thunk, inst->callbackPropertyId, jsval, true)) != JsNoError
					|| (err = JsSetProperty(jsval, inst->callbackPropertyId, thunk, true)) != JsNoError)
				{
					Error(err, _T("dllImport: setting callback function/thunk cross-references"));
					return;
				}
			}

			// Pass the thunk from the wrapper to the native code to use as the callback
			Store(wrapper->thunk);
		}
		break;

	case JsObject:
		{
			SigParser toSig(p + 1, EndOfArg(p + 1));
			if (auto nativeObj = NativeTypeWrapper::Recover<NativeTypeWrapper>(jsval, nullptr); nativeObj != nullptr)
			{
				// Native data.  The native object must be a pointer type, and its
				// underlying type must be compatible with the referenced type.
				// Alternatively, if the native object is of the same type as the
				// pointer reference, we'll automatically infer that the address
				// of the native object was intended.
				const WSTRING &nativeSig = nativeObj->sig;
				bool isPtr = IsPointerType(nativeSig.c_str());
				bool isArray = IsArrayType(nativeSig.c_str());
				const WCHAR *nativeType = SkipPointerOrArrayQual(nativeObj->sig.c_str());
				SigParser fromSig(nativeType, nativeSig.c_str() + nativeSig.length());
				if (IsPointerConversionValid(&fromSig, &toSig))
				{
					// Check if it's a pointer type, an array type, or value type
					if (isPtr)
					{
						// pointer type - 'data' points to the pointer value to pass
						Store(*reinterpret_cast<void**>(nativeObj->data));
					}
					else if (isArray)
					{
						// array type - 'data' points to the base of the array
						Store(nativeObj->data);
					}
					else
					{
						// value type - 'data' is the pointer to the value
						Store(nativeObj->data);
					}
				}
				else
					Error(_T("Incompatible pointer type conversion"));
			}
			else if (auto nativePtr = NativePointerData::Recover<NativePointerData>(jsval, nullptr); nativePtr != nullptr)
			{
				// Native pointer.  Check that it's compatible with the underlying type.
				// Note that the signature stored in a NativePointer type is the signature
				// of the referenced type, so there's no pointer qualifier to remove.
				SigParser fromSig(nativePtr->sig);
				if (IsPointerConversionValid(&fromSig, &toSig))
				{
					// If the FROM signature is a pointer to a pointer to a COM interface,
					// assume this is an OUT parameter that will overwrite any current
					// interface pointer.  Release the old pointer before the call to
					// maintain the integrity of the reference count.
					if (fromSig.sig.substr(0, 3) == L"*@I")
					{
						auto pUnk = reinterpret_cast<IUnknown**>(nativePtr->ptr);
						if (*pUnk != nullptr)
						{
							(*pUnk)->Release();
							*pUnk = nullptr;
						}
					}

					// store the pointer
					Store(nativePtr->ptr);
				}
				else
					Error(_T("Incompatible pointer type conversion"));
			}
			else if (auto comObj = COMImportData::Recover<COMImportData>(jsval, nullptr); comObj != nullptr)
			{
				// COM object.
				//
				// If the underlying type is "pointer to GUID", automatically pass the GUID
				// of the interface instead of the interface pointer.
				//
				// Otherwise, pass the interface pointer.
				//
				if (*tp == 'G')
				{
					// they want the GUID, not the interface pointer
					void *pointer = inst->marshallerContext->Alloc(sizeof(GUID));
					if (!ParseGuid(comObj->guid.c_str(), *static_cast<GUID*>(pointer)))
						return Error(MsgFmt(_T("Invalid GUID \"%s\" in COMPointer"), comObj->guid.c_str()));
					Store(pointer);

					// SPECIAL CASE: Check for the IID_PPV_ARGS() format.  In C++, the macro
					// IID_PPV_ARGS(p) expands to "__uuidof(p), (void**)&p", simplifying the
					// syntax for the extremely common case in COM where you have to pass the
					// IID of an interface you want to retrieve and the address of the variable
					// that will receive the interface pointer.  We simulate this when passing
					// arguments by not consuming the COM pointer when we see a REFIID,void**
					// argument pair and a COM pointer object is passed for the REFIID argument.
					if (IsArgvMarshaller())
					{
						// advance to the next argument
						const WCHAR *pnxt = EndOfArg(p), *endp = sig->sigEnd();
						for (; pnxt < endp && *pnxt == ' '; ++pnxt);

						// if it's a void**, reuse the current argument for it
						if (pnxt + 2 < endp && pnxt[0] == '*' && pnxt[1] == '*' && pnxt[2] == 'v')
							UngetVal();
					}
				}
				else
				{
					// Validate that we have a valid conversion from the COM interface pointer
					// type.  COM objects always have pointer type signatures, so skip the pointer
					// qualifier '*' to get to the underlying interface type.
					//
					// Special case 1: if we're passing a COM pointer to a void** parameter, 
					// assume that this is a standard COM interface OUT param, so pass the 
					// address of the COM pointer.
					//
					// Special case 2: if we're passing a IFoo* to an IFoo** parameter, do the
					// same thing as for a void**.  We'll likewise assume that this is an OUT
					// param returning a COM pointer of the same type.
					//
					SigParser fromSig(comObj->sig.data() + 1, comObj->sig.length() - 1);
					if (IsPointerConversionValid(&fromSig, &toSig))
					{
						// compatible COM interface pointer - pass the pointer
						Store(comObj->pUnknown);
					}
					else if (toSig.sig == L"*v" || toSig.sig == comObj->sig)
					{
						// void** or <my interface type>**.  Take this to be an OUT pointer 
						// receiving a new interface pointer, so pass the address of our
						// internal IUnknown pointer.
						Store(&comObj->pUnknown);

						// If we currently have a non-null interface, release it and clear the
						// pointer.  We always have to assume that a void** is a strictly OUT
						// variable that the callee will overwrite, so our counted reference
						// would be lost if we didn't clear the variable here.
						if (comObj->pUnknown != nullptr)
						{
							comObj->pUnknown->Release();
							comObj->pUnknown = nullptr;
						}
					}
					else
						Error(_T("Incompatible pointer type conversion"));
				}
			}
			else if (auto v = VariantData::Recover<VariantData>(jsval, nullptr); v != nullptr)
			{
				if (toSig.sig == L"V")
					Store(&v->v);
				else
					Error(_T("Incompatible pointer type conversion"));
			}
			else
			{
				// marshall other object types by reference
				goto by_ref;
			}
		}
		break;

	default:
	by_ref:
		// anything else gets marshalled by reference
		{
			SigParser subsig(p + 1, EndOfArg());
			MarshallToNativeByReference mbr(&subsig, jsval);
			mbr.MarshallValue();
			Store(mbr.pointer);
		}
		break;
	}
}

void JavascriptEngine::MarshallToNative::DoStruct()
{
	// get the value
	JsValueRef jsval = GetNextVal();

	// allocate space
	size_t size = SizeofStruct(jsval, nullptr);
	void *pointer = AllocStruct(size);

	// marshall through a struct marshaller
	SigParser subsig(p + 3, EndOfArg() - 1);
	MarshallToNativeStruct ms(&subsig, jsval, pointer, size);
	ms.Marshall();
}

void JavascriptEngine::MarshallToNative::DoUnion()
{
	// get the value
	JsValueRef jsval = GetNextVal();

	// allocate space
	size_t size = SizeofUnion(jsval, nullptr);
	void *pointer = AllocStruct(size);

	// marshall through a struct marshaller
	SigParser subsig(p + 3, EndOfArg() - 1);
	MarshallToNativeUnion mu(&subsig, jsval, pointer, size);
	mu.Marshall();
}

class JavascriptEngine::MarshallFromNative : public Marshaller
{
public:
	MarshallFromNative(SigParser *sig) : Marshaller(sig) { }

	MarshallFromNative(Marshaller &m) : Marshaller(m) { }

	bool Check(JsErrorCode err)
	{
		if (err != JsNoError)
		{
			Error(err, _T("dllImport: converting native value to Javascript"));
			error = true;
		}
		return !error;
	}
};

class JavascriptEngine::MarshallFromNativeValue : public MarshallFromNative
{
public:
	MarshallFromNativeValue(SigParser *sig, void *valp) :
		MarshallFromNative(sig), valp(valp)
	{ }

	// Javascript result value
	JsValueRef jsval = JS_INVALID_REFERENCE;

	virtual void DoBool() override { Check(JsBoolToBoolean(*reinterpret_cast<const bool*>(valp), &jsval)); }
	virtual void DoInt8() override { Check(JsIntToNumber(*reinterpret_cast<const INT8*>(valp), &jsval)); }
	virtual void DoUInt8() override { Check(JsIntToNumber(*reinterpret_cast<const UINT8*>(valp), &jsval)); }
	virtual void DoInt16() override { Check(JsIntToNumber(*reinterpret_cast<const INT16*>(valp), &jsval)); }
	virtual void DoUInt16() override { Check(JsIntToNumber(*reinterpret_cast<const UINT16*>(valp), &jsval)); }
	virtual void DoInt32() override { Check(JsIntToNumber(*reinterpret_cast<const INT32*>(valp), &jsval)); }
	virtual void DoUInt32() override { Check(JsIntToNumber(*reinterpret_cast<const UINT32*>(valp), &jsval)); }
	virtual void DoIntPtr() override { IF_32_64(DoInt32(), DoInt64()); }
	virtual void DoUIntPtr() override { IF_32_64(DoUInt32(), DoUInt64()); }
	virtual void DoSSizeT() override { IF_32_64(DoInt32(), DoInt64()); }
	virtual void DoPtrdiffT() override { IF_32_64(DoInt32(), DoInt64()); }
	virtual void DoSizeT() override { IF_32_64(DoUInt32(), DoUInt64()); }

	virtual void DoInt64() override 
	{
		// use our Int64 type to represent the result
		Check(XInt64Data<INT64>::CreateFromInt(*reinterpret_cast<const INT64*>(valp), jsval));
	}

	virtual void DoUInt64() override
	{
		// use our Uint64 type to represent the result
		Check(XInt64Data<UINT64>::CreateFromInt(*reinterpret_cast<const UINT64*>(valp), jsval));
	}

	virtual void DoString() override
	{
		// skip any const qualifier in the underlying type
		const WCHAR *tp = p;
		bool isConst = false;
		if (*tp == '%')
		{
			isConst = true;
			++tp;
		}

		// make sure it's a recognized string type
		if (*tp != 'T' && *tp != 't')
		{
			Error(_T("dllImport: unrecognized string type code in type signature"));
			return;
		}

		// the value is a WCHAR* or CHAR* pointer
		void *ptr = *static_cast<void* const*>(valp);

		// Figure the size of the underlying character type.  Note that this
		// isn't the length of the string - it's just the size of the first
		// character cell.  A C string pointer is really just a pointer to a 
		// character type; it's string-ness comes from that actually being
		// the first element of an array of character cells, and from the
		// convention that the contents of the array will be terminated by
		// a null character.  As far as the pointer type is concerned, it's
		// just a pointer to a character.
		size_t size = *tp == 'T' ? sizeof(WCHAR) : sizeof(CHAR);

		// create a native pointer type, recording the string type
		SigParser subsig(p, EndOfArg(p));
		Check(NativePointerData::Create(ptr, size, &subsig, *tp, &jsval));
	}

	virtual void DoGuid() override
	{
		// get the GUID
		const GUID *pguid = static_cast<const GUID*>(valp);

		// convert it to a string
		TSTRING str = FormatGuid(*pguid);

		// return it as a Javascript string
		Check(JsPointerToString(str.c_str(), str.length(), &jsval));
	}

	virtual void DoVariant() override
	{
		Check(VariantData::CreateFromNative(static_cast<const VARIANT*>(valp), jsval));
	}

	virtual void DoBSTR() override
	{
		// create a Javascript string from the BSTR
		BSTR bstr = *static_cast<BSTR*>(valp);
		Check(JsPointerToString(bstr, SysStringLen(bstr), &jsval));
	}

	virtual void DoFloat() override { Check(JsDoubleToNumber(*reinterpret_cast<const float*>(valp), &jsval)); }
	virtual void DoDouble() override { Check(JsDoubleToNumber(*reinterpret_cast<const double*>(valp), &jsval)); }

	virtual void DoHandle() override
	{
		// HANDLE values are 64 bits on x64, so we can't reliably convert
		// these to and from Javascript Number values.  Wrap it in an 
		// external data object to ensure we preserve all bits.
		Check(CreateExternalObjectWithPrototype(jsval, inst->HANDLE_proto, new HandleData(*reinterpret_cast<const HANDLE*>(valp))));
	}

	virtual void DoWinHandle() override
	{
		Check(CreateExternalObjectWithPrototype(jsval, inst->HWND_proto, new HWNDData(*reinterpret_cast<const HWND*>(valp))));
	}

	virtual void DoFunction() override { Error(_T("dllImport: function can't be returned by value (pointer required)")); }

	void DoPointerToFunction(const WCHAR *funcSig)
	{
		// get the native function pointer
		FARPROC procAddr = *const_cast<FARPROC*>(reinterpret_cast<const FARPROC*>(valp));

		// a null native function pointer translate to a null javascript result
		if (procAddr == nullptr)
		{
			jsval = inst->nullVal;
			return;
		}

		// wrap the function pointer in an external data object
		JsValueRef extObj;
		Check(CreateExternalObject(extObj,
			new DllImportData(procAddr, TSTRING(_T("[Return/OUT value from DLL invocation]")), TSTRING(_T("[Anonymous]")))));

		// The external object isn't by itself callbable from Javascript;
		// it has to go through our special system _call() function.  We now 
		// have to wrap the function in a lambda that calls <dllImport>.call() 
		// with the function object and its signature.

		// create a javascript string argument for the signature
		JsValueRef funcSigVal;
		JsErrorCode err;
		if ((err = JsPointerToString(funcSig, EndOfArg(funcSig) - funcSig, &funcSigVal)) != JsNoError)
		{
			Error(err, _T("dllImport: JsPointerToString(native callback signature"));
			return;
		}
		
		// Get this._bindExt
		const TCHAR *where;
		JsValueRef bindExt;
		if ((err = inst->GetProp(bindExt, inst->dllImportObject, "_bindExt", where)) != JsNoError)
		{
			Error(err, MsgFmt(_T("dllImport: getting this._bindExt(): %s"), where));
			return;
		}

		// Call DllImport._bindExt(this, extObj, funcsig).  This return value from that function
		// is the wrapped function pointer.
		JsValueRef bindArgv[3] = { inst->dllImportObject, extObj, funcSigVal };
		if ((err = JsCallFunction(bindExt, bindArgv, static_cast<unsigned short>(countof(bindArgv)), &jsval)) != JsNoError)
		{
			Error(err, _T("dllImport: JsCallFunction(this._bindExt())"));
			return;
		}
	}

	virtual void DoVoid() override { jsval = inst->undefVal; }

	virtual void DoPointer() override 
	{
		// if the pointer is a local object, we don't have to do anything with it
		void *ptr = *static_cast<void* const*>(valp);
		if (inst->marshallerContext->IsLocal(ptr))
			return;

		// skip any const qualifier in the underlying type, as we're marshalling
		// the *pointer* here
		const WCHAR *tp = p + 1;
		if (*tp == '%')
			++tp;

		// check the underlying type
		switch (*tp)
		{
		case '(':
			// Pointer to function.  This is special: we marshall it back as
			// though it were a bound DLL export.
			DoPointerToFunction(tp);
			break;

		default:
			// This is a pointer returned or passed by reference from the native 
			// callee.  Javascript doesn't have its own way to represent a native 
			// pointer, so wrap this in an external data object.
			{
				// get the size of the dereferenced type
				SigParser subsig(tp, EndOfArg(tp));
				MarshallBasicSizer sizer(&subsig, JS_INVALID_REFERENCE);
				sizer.MarshallValue();

				// create the native pointer wrapper
				Check(NativePointerData::Create(ptr, sizer.size, &subsig, 0, &jsval));
			}
			break;
		}
	}

	virtual void DoStruct() override;
	virtual void DoUnion() override;
	virtual void DoArray() override;
	
	virtual void DoInterface() override { Error(_T("dllImport: interface can't be returned by value (pointer required)")); }

	// pointer to native value we're marshalling
	void *valp;
};


void JavascriptEngine::MarshallFromNativeValue::DoStruct()
{
	// marshall the native struct
	NativeTypeWrapper *obj = nullptr;
	SigParser subsig(p, EndOfArg(p));
	jsval = inst->CreateNativeObject(&subsig, valp);
}

void JavascriptEngine::MarshallFromNativeValue::DoUnion()
{
	// marshall the native union
	NativeTypeWrapper *obj = nullptr;
	SigParser subsig(p, EndOfArg(p));
	jsval = inst->CreateNativeObject(&subsig, valp);
}

void JavascriptEngine::MarshallFromNativeValue::DoArray()
{
	NativeTypeWrapper *obj = nullptr;
	SigParser subsig(p, EndOfArg(p));
	jsval = inst->CreateNativeObject(&subsig, valp);
}

// --------------------------------------------------------------------------
//
// DllImport implementation
//

bool JavascriptEngine::BindDllImportCallbacks(ErrorHandler &eh)
{
	JsErrorCode err;
	const TCHAR *subwhere = nullptr;
	auto Error = [&err, &eh](const TCHAR *where)
	{
		eh.SysError(LoadStringT(IDS_ERR_JSINIT),
			MsgFmt(_T("Binding dllImport callbacks: %s: %s"), where, JsErrorToString(err)));
		return false;
	};

	// look up dllImport object, which should be defined by the system
	// scripts as a property of the global object
	if ((err = GetProp(dllImportObject, globalObj, "dllImport", subwhere)) != JsNoError)
		return Error(subwhere);

	// set up the bindings for prototype methods and class methods
	if (!DefineObjPropFunc(dllImportObject, "dllImport", "_bind", &JavascriptEngine::DllImportBind, this, eh)
		|| !DefineObjPropFunc(dllImportObject, "dllImport", "_sizeof", &JavascriptEngine::DllImportSizeof, this, eh)
		|| !DefineObjPropFunc(dllImportObject, "dllImport", "_create", &JavascriptEngine::DllImportCreate, this, eh)
		|| !DefineObjPropFunc(dllImportObject, "dllImport", "_call", &JavascriptEngine::DllImportCall, this, eh)
		|| !DefineObjPropFunc(dllImportObject, "dllImport", "_invokeAutomationMethod", &JavascriptEngine::InvokeAutomationMethod, this, eh))
		return false;

	// find the COMPoinpter class and prototype
	if ((err = GetProp(COMPointer_class, globalObj, "COMPointer", subwhere)) != JsNoError
		|| (err = GetProp(COMPointer_proto, COMPointer_class, "prototype", subwhere)) != JsNoError)
		return Error(subwhere);
	
	// add a reference on each
	JsAddRef(COMPointer_class, nullptr);
	JsAddRef(COMPointer_proto, nullptr);

	// add methods
	if (!DefineObjPropFunc(COMPointer_class, "COMPointer", "isNull", &COMImportData::IsNull, this, eh)
		|| !DefineObjPropFunc(COMPointer_class, "COMPointer", "clear", &COMImportData::Clear, this, eh))
		return false;

	// find the HANDLE object's prototype
	JsValueRef classObj;
	if ((err = GetProp(classObj, globalObj, "HANDLE", subwhere)) != JsNoError
		|| (err = GetProp(HANDLE_proto, classObj, "prototype", subwhere)) != JsNoError)
		return Error(subwhere);

	// set up HANDLE.prototype function bindings
	if (!DefineObjPropFunc(HANDLE_proto, "HANDLE", "toString", &HandleData::ToString, this, eh)
		|| !DefineObjPropFunc(HANDLE_proto, "HANDLE", "toNumber", &HandleData::ToNumber, this, eh)
		|| !DefineObjPropFunc(HANDLE_proto, "HANDLE", "toUint64", &HandleData::ToUInt64, this, eh)
		|| !DefineObjPropFunc(classObj, "HANDLE", "_new", &HandleData::CreateWithNew, this, eh))
		return false;

	// save a reference on the handle prototype, as we're hanging onto it
	JsAddRef(HANDLE_proto, nullptr);

	// find the HWND object's prototype
	if ((err = GetProp(classObj, globalObj, "HWND", subwhere)) != JsNoError
		|| (err = GetProp(HWND_proto, classObj, "prototype", subwhere)) != JsNoError)
		return Error(subwhere);

	// set up prototype methods and properties
	auto SetSpecialHWND = [this, classObj, &err, &subwhere, &Error](const CHAR *name, HWND hwnd)
	{
		JsValueRef val;
		if ((err = HWNDData::CreateFromNative(hwnd, val)) != JsNoError
			|| (err = SetReadonlyProp(classObj, name, val, subwhere)) != JsNoError)
			return Error(_T("Creating special HWND window property"));

		return true;
	};
	if (!DefineObjPropFunc(classObj, "HWND", "_new", &HWNDData::CreateWithNew, this, eh)
		|| !DefineObjMethod(HWND_proto, "HWND", "isVisible", &HWNDData::IsVisible, this, eh)
		|| !DefineObjMethod(HWND_proto, "HWND", "getWindowPos", &HWNDData::GetWindowPos, this, eh)
		|| !SetSpecialHWND("BOTTOM", HWND_BOTTOM)
		|| !SetSpecialHWND("NOTOPMOST", HWND_NOTOPMOST)
		|| !SetSpecialHWND("TOP", HWND_TOP)
		|| !SetSpecialHWND("TOPMOST", HWND_TOPMOST))
		return false;

	// save a reference on it
	JsAddRef(HWND_proto, nullptr);

	// find the NativeObject object's prototype
	if ((err = GetProp(classObj, globalObj, "NativeObject", subwhere)) != JsNoError
		|| (err = GetProp(NativeObject_proto, classObj, "prototype", subwhere)) != JsNoError)
		return Error(subwhere);

	// set up NativeObject methods
	if (!DefineObjPropFunc(classObj, "NativeObject", "addressOf", &NativeTypeWrapper::AddressOf, this, eh))
		return false;

	// find the NativePointer object's prototype
	if ((err = GetProp(classObj, globalObj, "NativePointer", subwhere)) != JsNoError
		|| (err = GetProp(NativePointer_proto, classObj, "prototype", subwhere)) != JsNoError)
		return Error(subwhere);

	auto AddGetter = [this, &Error](JsValueRef obj, const char *propName, JsNativeFunction func, void *ctx)
	{
		JsValueRef getter;
		JsErrorCode err;
		const TCHAR *where;
		if ((err = JsCreateFunction(func, ctx, &getter)) != JsNoError)
			return Error(_T("JsCreateFunction(getter)"));

		if ((err = AddGetterSetter(obj, propName, getter, JS_INVALID_REFERENCE, where)) != JsNoError)
			return Error(where);

		return true;
	};

	// set up NativePointer.prototype function bindings
	if (!DefineObjPropFunc(NativePointer_proto, "NativePointer", "toString", &NativePointerData::ToString, this, eh)
		|| !DefineObjPropFunc(NativePointer_proto, "NativePointer", "toStringZ", &NativePointerData::ToStringZ, this, eh)
		|| !DefineObjPropFunc(NativePointer_proto, "NativePointer", "toNumber", &NativePointerData::ToNumber, this, eh)
		|| !DefineObjPropFunc(NativePointer_proto, "NativePointer", "toUint64", &NativePointerData::ToUInt64, this, eh)
		|| !DefineObjPropFunc(NativePointer_proto, "NativePointer", "toArrayBuffer", &NativePointerData::ToArrayBuffer, this, eh)
		|| !DefineObjPropFunc(NativePointer_proto, "NativePointer", "toArray", &NativePointerData::ToArray, this, eh)
		|| !DefineObjPropFunc(NativePointer_proto, "NativePointer", "_to", &NativePointerData::To, this, eh)
		|| !AddGetter(NativePointer_proto, "at", &NativePointerData::At, this)
		|| !DefineObjPropFunc(NativePointer_proto, "NativePointer", "isNull", &NativePointerData::IsNull, this, eh)
		|| !DefineObjPropFunc(classObj, "NativePointer", "fromNumber", &NativePointerData::FromNumber, this, eh))
		return false;

	// save a reference on the handle prototype, as we're hanging onto it
	JsAddRef(NativePointer_proto, nullptr);

	// set up the INT64 native type
	if ((err = GetProp(classObj, globalObj, "Int64", subwhere)) != JsNoError
		|| (err = GetProp(Int64_proto, classObj, "prototype", subwhere)) != JsNoError)
		return Error(subwhere);

	if (!DefineObjPropFunc(Int64_proto, "Int64", "toString", &XInt64Data<INT64>::ToString, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "toObject", &XInt64Data<INT64>::ToObject, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "toNumber", &XInt64Data<INT64>::ToNumber, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "negate", &XInt64Data<INT64>::Negate, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "add", &XInt64Data<INT64>::Add, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "sub", &XInt64Data<INT64>::Subtract, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "mul", &XInt64Data<INT64>::Multiply, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "div", &XInt64Data<INT64>::Divide, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "mod", &XInt64Data<INT64>::Modulo, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "and", &XInt64Data<INT64>::And, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "or", &XInt64Data<INT64>::Or, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "not", &XInt64Data<INT64>::Not, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "shl", &XInt64Data<INT64>::Shl, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "ashr", &XInt64Data<INT64>::Ashr, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "lshr", &XInt64Data<INT64>::Lshr, this, eh)
		|| !DefineObjPropFunc(Int64_proto, "Int64", "compare", &XInt64Data<INT64>::Compare, this, eh)
		|| !DefineObjPropFunc(classObj, "Int64", "_new", &XInt64Data<INT64>::Create, this, eh))
		return false;

	JsAddRef(Int64_proto, nullptr);

	// set up the UINT64 native type
	if ((err = GetProp(classObj, globalObj, "Uint64", subwhere)) != JsNoError
		|| (err = GetProp(UInt64_proto, classObj, "prototype", subwhere)) != JsNoError)
		return Error(subwhere);

	if (!DefineObjPropFunc(UInt64_proto, "Uint64", "toString", &XInt64Data<UINT64>::ToString, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "Uint64", "toObject", &XInt64Data<UINT64>::ToObject, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "Uint64", "toNumber", &XInt64Data<UINT64>::ToNumber, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "UInt64", "negate", &XInt64Data<UINT64>::Negate, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "Uint64", "add", &XInt64Data<UINT64>::Add, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "Uint64", "sub", &XInt64Data<UINT64>::Subtract, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "Uint64", "mul", &XInt64Data<UINT64>::Multiply, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "Uint64", "div", &XInt64Data<UINT64>::Divide, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "Uint64", "mod", &XInt64Data<UINT64>::Modulo, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "Uint64", "and", &XInt64Data<UINT64>::And, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "Uint64", "or", &XInt64Data<UINT64>::Or, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "Uint64", "not", &XInt64Data<UINT64>::Not, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "Uint64", "shl", &XInt64Data<UINT64>::Shl, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "Uint64", "ashr", &XInt64Data<UINT64>::Ashr, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "Uint64", "lshr", &XInt64Data<UINT64>::Lshr, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "Uint64", "compare", &XInt64Data<UINT64>::Compare, this, eh)
		|| !DefineObjPropFunc(classObj, "UInt64", "_new", &XInt64Data<UINT64>::Create, this, eh))
		return false;

	JsAddRef(Int64_proto, nullptr);

	// success
	return true;
}

// DllImportBind - this is set up in the Javascript as dllImport.bind_(), an
// internal method that bind() calls to get the native function pointer.  Javascript
// doesn't have a type that can represent a native FARPROC directly, so we use an
// external object instead.  Our DllImport code on the Javascript then wraps that in
// a lambda that calls DllImportCall with the external object as a parameter.
JsValueRef JavascriptEngine::DllImportBind(TSTRING dllName, TSTRING funcName)
{
	// normalize the DLL name to use as the HMODULE map key
	TSTRING key = dllName;
	std::transform(key.begin(), key.end(), key.begin(), ::_totupper);

	// if this key isn't already in the table, load the DLL
	HMODULE hmod = NULL;
	if (auto it = dllHandles.find(key); it != dllHandles.end())
	{
		// got it - reuse the existing module handle
		hmod = it->second;
	}
	else
	{ 
		// not found - try loading the DLL
		hmod = LoadLibrary(dllName.c_str());
		if (hmod == NULL)
		{
			WindowsErrorMessage winErr;
			Throw(MsgFmt(_T("dllImport.bind: Error loading DLL %s: %s"), dllName.c_str(), winErr.Get()));
			return nullVal;
		}

		// add it to the table
		dllHandles.emplace(key, hmod).first;
	}

	// look up the proc address
	FARPROC addr = GetProcAddress(hmod, TSTRINGToCSTRING(funcName).c_str());
	if (addr == NULL)
	{
		WindowsErrorMessage winErr;
		Throw(MsgFmt(_T("dllImport.bind: Error binding %s!%s: %s"), dllName.c_str(), funcName.c_str(), winErr.Get()));
		return nullVal;
	}

	// create an external object with a DllImportData object to represent the result
	JsValueRef ret;
	if (JsErrorCode err = CreateExternalObject(ret, new DllImportData(addr, dllName, funcName)); err != JsNoError)
	{
		Throw(err, _T("dllImport.bind"));
		return nullVal;
	}

	// return the result
	return ret;
}

// assembler glue functions for DLL calls
#if defined(_M_X64)
extern "C" UINT64 DllCallGlue64_RAX(FARPROC func, const void *args, size_t nArgBytes);
extern "C" __m128 DllCallGlue64_XMM0(FARPROC func, const void *args, size_t nArgBytes);
#endif

// DllImportSizeof is set up in the Javascript as dllImport._sizeof(), an 
// internal method of the DllImport object.  This can be used to retrieve the
// size of a native data structure defined via the Javascript-side C parser.
//
// Since the value is returned to Javascript, we return it as a double rather
// than size_t.  Returning as size_t is problematic because size_t is 64 bits
// on x64, which can exceed the integer precision of a double.  So explicitly
// convert it after checking for overflow.
double JavascriptEngine::DllImportSizeof(WSTRING typeInfo)
{
	// measure the size
	SigParser sig(typeInfo);
	MarshallBasicSizer sizer(&sig);
	sizer.Marshall();

	// check for overflow
	if (sizer.size > (1ULL << DBL_MANT_DIG))
	{
		ThrowSimple("dllImport.sizeof: size overflows Javascript Number");
		return 0.0;
	}

	// return the result
	return static_cast<double>(sizer.size);
}

// DllImportCreate is set up in the Javascript as dllImport._create(), an 
// internal method of the DllImport object.  This creates an instance of a
// native type, for use in calls to native code.
JsValueRef JavascriptEngine::DllImportCreate(WSTRING typeInfo)
{
	SigParser sig(typeInfo);
	return CreateNativeObject(&sig);
}

// DllImportDefineInternalType = Javascript _defineInternalType.  This is a
// global function that the Javascript-side C type parser calls this on each
// successful struct, union, and interface type definition.  This allows us
// to store type definitions for reference during marshalling between native
// and Javascript types.
void JavascriptEngine::DllImportDefineInternalType(WSTRING name, WSTRING typeInfo)
{
	nativeTypeMap.emplace(name, typeInfo);
}

bool JavascriptEngine::LookUpNativeType(const WSTRING &s, std::wstring_view &sig, bool silent)
{
	if (auto it = nativeTypeMap.find(s); it != nativeTypeMap.end())
	{
		sig = it->second;
		return true;
	}

	if (!silent)
		Throw(MsgFmt(_T("Undefined type reference @%s"), s.c_str()));
	
	return false;
}


// DllImportCall is set up in the Javascript as dllImport._call(), an internal
// method of the dllImport object.  When the Javascript caller calls the lambda
// returned from bind(), the lambda invokes dllImport.call(), which in turn 
// invokes _call() (which is how we're exposed to Javascript) as:
//
//   dllImport._call(nativeFunc, signature, ...args)
//
// Javascript this: the DllImport instance that was used to create the binding with bind()
//
// nativeFunc: the external object we created for the DLL entrypoint in DllImportBind
//
// signature: a compact pre-parsed version of the function signature
//
// args: the javascript arguments to the call
//
//
// Alternatively, for COM interface imports, the binding is called like this:
//
//   dllImport._call(comObject, vtableIndex, functionSignature, ...args)
//
// In this case, we get the native function pointer from the vtable in the COM 
// object pointer, using the vtable index.  The additional vtable index argument
// is required to select the appropriate function from the interface method
// vector.
//
JsValueRef JavascriptEngine::DllImportCall(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsErrorCode err;

	// we need at least two arguments ('this', native function, signature)
	if (argc < 3)
		return inst->Throw(_T("dllImport.call: missing arguments"));

	// get the javascript this pointer
	int ai = 0;
	auto jsthis = argv[ai++];

	// set up a temporary allocator for the marshallers
	MarshallerContext tempAlloc;

	// Get the native function object
	FARPROC funcPtr = nullptr;
	if (auto dllFuncObj = DllImportData::Recover<DllImportData>(argv[ai], nullptr); dllFuncObj != nullptr)
	{
		// DLL import function pointer - get the proc address from the import object
		funcPtr = dllFuncObj->procAddr;
		++ai;
	}
	else if (auto comObj = COMImportData::Recover<COMImportData>(argv[ai], nullptr); comObj != nullptr)
	{
		// COM import - make sure the underlying COM object has been populated
		if (comObj->pUnknown == nullptr)
			return inst->Throw(_T("dllImport.call: COM object pointer is null"));
		
		// get the proc address from the vtable
		int vtableIndex;
		++ai;
		if ((err = JsNumberToInt(argv[ai++], &vtableIndex)) != JsNoError)
			return inst->Throw(err, _T("dllImport.call: getting COM object vtable index"));

		// sanity-check it
		if (vtableIndex < 0 || vtableIndex >= comObj->vtableCount)
			return inst->Throw(_T("dllImport.call: COM object vtable index out of range"));

		// get the proc address from the vtable
		funcPtr = comObj->GetVTable()[vtableIndex];
	}
	else
		return inst->Throw(_T("dllImport.call: invalid native function object"));

	// get the function signature, as a string
	const WCHAR *sigStr;
	size_t sigLen;
	if ((err = JsStringToPointer(argv[ai++], &sigStr, &sigLen)) != JsNoError)
		return inst->Throw(err, _T("dllImport.call"));
		
	// Set up a signature parser for the function, and a sub-parser with just
	// the return value + argument vector portion.  That is, the part inside
	// the parentheses, skipping the calling convention prefix:
	//
	//   (<callingConv><returnType> <arg1> <arg2> ...)
	//
	SigParser funcSig(sigStr, sigLen);
	SigParser argvSig(sigStr + 2, Marshaller::EndOfArg(sigStr, sigStr + sigLen) - 1);

	// the rest of the Javascript arguments are the arguments to pass to the DLL
	int firstDllArg = ai;

	// Get the calling convention.  This is the first letter of the first token:
	// S[__stdcall], C[__cdecl], F[__fastcall], T[__thiscall], V[__vectorcall]
	WCHAR callConv = sigStr[1];

	// the return value type starts immediately after the calling convention
	const WCHAR *retType = sigStr + 2;

	// Set up a stack argument sizer to measure how much stack space we need
	// for the native copies of the arguments.  The first type in the function 
	// signature is the return type, so skip that.
	MarshallStackArgSizer stackSizer(&argvSig, argv, argc, firstDllArg);

	// the remaining items in the signature are the argument types - size them
	if (!stackSizer.Marshall())
		return inst->undefVal;

	// Figure the required native argument array size
	size_t argArraySize = max(stackSizer.nSlots, minArgSlots) * argSlotSize;

	// round up to the next higher alignment boundary
	argArraySize = ((argArraySize + stackAlign - 1) / stackAlign) * stackAlign;

	// allocate the argument array
	arg_t *argArray = static_cast<arg_t*>(alloca(argArraySize));

	// Zero the argument array memory.  Our stack marshaller performs type
	// conversions to the formal parameter types of the arguments, but some
	// stack slots might be wider than their respective formals, so some
	// type of widening operation has to be performed.  There are three
	// possible ways to do this:
	//
	//  (1) zero-extend from the formal type to the stack slot type
	//  (2) sign-extend from the formal type to the stack slot type
	//  (3) leave the high-order bytes in the stack slot uninitialized (random)
	//
	// Now, none of this is specified in any C standards, since it simply
	// shouldn't matter!  A callee shouldn't have any awareness that a stack
	// slot might be wider than the formal type that's passed in it, and
	// shouldn't even think about what's in those other bytes of the slot.
	// But we know from sad experience that there are many, many, many
	// examples of code that accidentally relies on the ad hoc behavior of
	// the compiler or other components.  So it will be safest if we do the
	// same thing that a Microsoft compiler would do here.
	//
	// So: the answer is that, empirically, the Microsoft compiler always 
	// does (1), zero-extend the value.  The easiest thing for us would
	// have been (3), leave it random, but fortunately (1) is almost as
	// easy.  We can satisfy this simply by initializing all bytes of our
	// stack array to zero.  The stack marshaller won't write anything to
	// unused high-order bytes when marshalling values into a slot, so
	// those bytes will remain zero, exactly as though the marshaller had
	// explicitly zero-extended each value it wrote out to the full slot
	// size.  The only downside is that this is slightly less efficient
	// than doing the zero-extension in place during the marshalling, in
	// that we have to do this extra write pass over the stack array,
	// which can end up being completely unnecessary if all of the formal
	// types are wide enough to fill the slots with actual data.  But the
	// cost of determining that is far higher than the cost of doing the
	// possibly redundant writes, not to mention more error-prone, so
	// let's just do the easy dumb thing.
	ZeroMemory(argArray, argArraySize);

	// marshall the arguments into the native stack
	MarshallToNativeArgv argPacker(&argvSig, argArray, argv, argc, firstDllArg);
	if (!argPacker.Marshall() || inst->HasException())
	{
		// marshalling failed or threw a JS error - fail
		return inst->undefVal;
	}

	// All return types can fit into 64 bits.  Note that this is only
	// because the __m128 vector types aren't supported.  If we want to
	// add support for __m128 types, we'd have to either expand this to
	// an __m128, or add a separate __m128 local for those values only.
	// A separate local would reduce unnecessary extra instructions and 
	// byte copying, especially in 32-bit mode, at the cost of added
	// code complexity (the complexity is why we don't have a separate
	// 32-bit local for 32-bit mode).
	UINT64 rawret;

	// Call the DLL function
#if defined(_M_IX86)

	// use the appropriate calling convention
	switch (callConv)
	{
	case 'S':
	case 'C':
		// __stdcall: arguments on stack; return in EDX:EAX; callee pops arguments
		// __cdecl: arguments on back; return in EDX:EAX; caller pops arguments
		__asm {
			; copy arguments into the stack
			mov ecx, argArraySize
			sub esp, ecx
			mov edi, esp
			mov esi, argArray
			shr ecx, 2; divide by 4 to get DWORD size
			rep movsd

			; call the function
			mov eax, funcPtr
			call eax

			; store the result
			mov dword ptr rawret, eax
			mov dword ptr rawret[4], edx

			; caller removes arguments if using __cdecl; for all others, the callee has
			; already removed them for us
			cmp callConv, 'C'
			jne $1

			; __cdecl->remove our arguments
			add esp, argArraySize
			$1 :
		}
		break;

	case 'F':
		return inst->Throw(_T("dllImport.call: __fastcall calling convention not supported"));

	case 'T':
		return inst->Throw(_T("dllImport.call: __thiscall calling convention not supported"));

	case 'V':
		return inst->Throw(_T("dllImport.call: __vectorcall calling convention not supported"));

	default:
		return inst->Throw(_T("dllImport.call: unknown calling convention in function signature"));
	}

#elif defined(_M_X64)

	// The Microsoft x64 convention passes the first four arguments in
	// registers, and passes the rest on the stack.  There are also four
	// "shadow" stack slots (64 bits each) where the first four arguments
	// would go if they were pushed onto the stack; these are allocated
	// by the caller for the callee's use, but aren't populated.  So our
	// stack is basically right already from the alloca(), but we still
	// need to populate the registers.  We need some assembly code to
	// make that happen.  The 64-bit compiler doesn't support inline
	// assembly, so we have to farm this out to some .asm glue.  
	//
	// The return value in x64 mode comes back in RAX for all integer 
	// types, pointer types, and by-value structs that can be packed into
	// 8 bytes; and in XMM0 for float, double, and 128-bit vector types.
	// Our glue routine doesn't mess with the return registers, so the
	// return value we get will be in whatever register the callee left
	// it in.  To retrieve the proper value through C++, we therefore
	// have to call a glue function with a return type that uses the
	// same register.  We don't need functions for every type - just
	// for two representative types that 
	switch (*retType)
	{
	case 'f':
	case 'd':
		// float/double - these come back in XMM0

		// NOTE: __m128 types aren't currently supported.  Those types are
		// returned in XMM0, so if you're adding support for those types, add
		// a case or cases for them here to enable them as return values.

		// call the XMM0 alias for the glue function, so that the compiler knows
		// that the value is returned in XMM0
		rawret = DllCallGlue64_XMM0(funcPtr, argArray, argArraySize).m128_u64[0];
		break;

	default:
		// all other types come back in RAX - call the RAX version of the glue function
		rawret = DllCallGlue64_RAX(funcPtr, argArray, argArraySize);
		break;
	}

#else
#error Processor architecture not supported.  Add the appropriate code here to build for this target.
#endif

	// Marshall the return value back to Javascript:
	//
	// - If the return value is a "struct by value" type, and it required
	//   allocating a hidden struct pointer argument, the return value is
	//   the allocated object.  We created this as a native type wrapper
    //   for the struct type, so we return the corresponding js object.
	//
	// - Otherwise, the return value is contained in the return register
	//   (rawRet).  Marshall that back to Javascript.
	//
	if (argPacker.structByValueReturn != JS_INVALID_REFERENCE)
	{
		// We have a by-value struct return.  We already allocated a 
		// javascript to hold the result.  
		//
		// If the struct size is over 8 bytes, we sent the callee a pointer
		// to that allocated return struct to use as the space for the struct,
		// so the struct has already been populated in place by the callee.
		// 
		// If the struct size is <= 8 bytes, though, the callee returned the
		// struct's contents packed into the return registers.  In this case,
		// we have to manually copy the return register into our struct
		// memory.
		if (argPacker.structByValueReturnSize <= 8)
			memcpy(argPacker.structByValueReturnPtr, &rawret, argPacker.structByValueReturnSize);

		// The return value is the pre-allocated struct.
		return argPacker.structByValueReturn;
	}
	else
	{
		// Marshall the raw return value to javascript and return the result
		MarshallFromNativeValue marshallRetVal(&argvSig, &rawret);
		marshallRetVal.MarshallValue();
		return marshallRetVal.jsval;
	}
}


// -----------------------------------------------------------------------
//
// Native HANDLE type
//

JsErrorCode JavascriptEngine::HandleData::CreateFromNative(HANDLE h, JsValueRef &jsval)
{
	return CreateExternalObjectWithPrototype(jsval, inst->HANDLE_proto, new HandleData(h));
}

HANDLE JavascriptEngine::HandleData::FromJavascript(JsValueRef jsval)
{
	// if the value is another HANDLE object, use the same underlying handle value
	if (auto handleObj = Recover<HandleData>(jsval, nullptr); handleObj != nullptr)
		return handleObj->h;

	// otherwise, reinterpret a numeric value (possibly 64-bit) as a handle
	return reinterpret_cast<HANDLE>(XInt64Data<UINT64>::FromJavascript(jsval));
}

JsValueRef CALLBACK JavascriptEngine::HandleData::CreateWithNew(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	// if we have an argument, interpret it into a HANDLE value
	HANDLE h = NULL;
	if (argc >= 2)
		h = FromJavascript(argv[1]);

	// create the handle
	JsValueRef retval;
	if (JsErrorCode err = CreateFromNative(h, retval); err != JsNoError)
		inst->Throw(err, _T("new HANDLE()"));

	// return the new HANDLE js object
	return retval;
}

JsValueRef CALLBACK JavascriptEngine::HandleData::ToUInt64(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = HandleData::Recover<HandleData>(argv[0], _T("HANDLE.toUint64()")); self != nullptr)
	{
		// create the Uint64 object
		XInt64Data<UINT64>::CreateFromInt(reinterpret_cast<UINT64>(self->h), ret);
	}
	return ret;
}

JsValueRef CALLBACK JavascriptEngine::HandleData::ToString(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = HandleData::Recover<HandleData>(argv[0], _T("HANDLE.toString()")); self != nullptr)
	{
		WCHAR buf[40];
		swprintf_s(buf, L"0x%p", self->h);
		JsPointerToString(buf, wcslen(buf), &ret);
	}
	return ret;
}

JsValueRef CALLBACK JavascriptEngine::HandleData::ToNumber(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = HandleData::Recover<HandleData>(argv[0], _T("HANDLE.toNumber()")); self != nullptr)
	{
		double d = static_cast<double>(reinterpret_cast<UINT_PTR>(self->h));
		JsDoubleToNumber(d, &ret);
		if (d > static_cast<double>(1LL << DBL_MANT_DIG))
		{
			JsValueRef msg, exc;
			const WCHAR *txt = L"Value out of range";
			JsPointerToString(txt, wcslen(txt), &msg);
			JsCreateError(msg, &exc);
			JsSetException(exc);
		}
	}
	return ret;
}

// -----------------------------------------------------------------------
//
// Native HWND type
//

HWND JavascriptEngine::ToNativeConverter<HWND>::Conv(JsValueRef val, bool &ok, const CSTRING &name) const
{
	return HWNDData::FromJavascript(val);
}

JsErrorCode JavascriptEngine::NewHWNDObj(JsValueRef &jsval, HWND h, const TCHAR* &where)
{
	where = _T("Creating HWND object");
	return HWNDData::CreateFromNative(h, jsval);
}

JsErrorCode JavascriptEngine::HWNDData::CreateFromNative(HWND h, JsValueRef &jsval)
{
	return CreateExternalObjectWithPrototype(jsval, inst->HWND_proto, new HWNDData(h));
}

HWND JavascriptEngine::HWNDData::FromJavascript(JsValueRef jsval)
{
	// If the value is a HANDLE object, use the same underlying handle value. 
	// Note that we can coerce any HANDLE value to an HWND.
	if (auto handleObj = Recover<HandleData>(jsval, nullptr); handleObj != nullptr)
		return static_cast<HWND>(handleObj->h);

	// otherwise, reinterpret a numeric value (possibly 64-bit) as a handle
	return reinterpret_cast<HWND>(XInt64Data<UINT64>::FromJavascript(jsval));
}

JsValueRef CALLBACK JavascriptEngine::HWNDData::CreateWithNew(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	// if we have an argument, interpret it into an HWND value
	HWND h = NULL;
	if (argc >= 2)
		h = FromJavascript(argv[1]);

	// create the handle
	JsValueRef retval;
	if (JsErrorCode err = CreateFromNative(h, retval); err != JsNoError)
		inst->Throw(err, _T("new HWND()"));

	// return the new HANDLE js object
	return retval;
}

bool JavascriptEngine::HWNDData::IsVisible(JavascriptEngine *js, JsValueRef self)
{
	if (auto hwndObj = Recover<HWNDData>(self, _T("HWND.IsVisible")); hwndObj != nullptr)
		return ::IsWindowVisible(hwndObj->hwnd());

	return js->undefVal;
}

JsValueRef JavascriptEngine::HWNDData::GetWindowPos(JavascriptEngine *js, JsValueRef self)
{
	if (auto hwndObj = Recover<HWNDData>(self, _T("HWND.IsVisible")); hwndObj != nullptr)
	{
		// get the window position - frame and client
		HWND hwnd = hwndObj->hwnd();
		RECT rcWin, rcClient;
		::GetWindowRect(hwnd, &rcWin);
		::GetClientRect(hwnd, &rcClient);

		// get the min/max state
		BOOL isMin = ::IsIconic(hwnd);
		BOOL isMax = ::IsMaximized(hwnd);

		// set up the return object:
		//
		// { windowRect: { rect }, clientRect: { rect }, isMinimized: bool, isMaximized: bool }
		// 
		// where rect is { left: int, top: int, right: int, bottom: int }
		//
		JsErrorCode err;
		JsPropertyIdRef propkey;
		JsValueRef retval, propval;
		auto MakeRect = [](const RECT &rc, JsValueRef &jsrc)
		{
			JsErrorCode err;
			JsValueRef numval;
			JsPropertyIdRef propkey;
			if ((err = JsCreateObject(&jsrc)) != JsNoError
				|| (err = JsIntToNumber(rc.left, &numval)) != JsNoError
				|| (err = JsCreatePropertyId("left", 4, &propkey)) != JsNoError
				|| (err = JsSetProperty(jsrc, propkey, numval, true)) != JsNoError
				|| (err = JsIntToNumber(rc.right, &numval)) != JsNoError
				|| (err = JsCreatePropertyId("right", 5, &propkey)) != JsNoError
				|| (err = JsSetProperty(jsrc, propkey, numval, true)) != JsNoError
				|| (err = JsIntToNumber(rc.top, &numval)) != JsNoError
				|| (err = JsCreatePropertyId("top", 3, &propkey)) != JsNoError
				|| (err = JsSetProperty(jsrc, propkey, numval, true)) != JsNoError
				|| (err = JsIntToNumber(rc.bottom, &numval)) != JsNoError
				|| (err = JsCreatePropertyId("bottom", 6, &propkey)) != JsNoError
				|| (err = JsSetProperty(jsrc, propkey, numval, true)) != JsNoError)
				return err;

			return JsNoError;
		};

		if ((err = JsCreateObject(&retval)) != JsNoError
			|| (err = MakeRect(rcWin, propval)) != JsNoError
			|| (err = JsCreatePropertyId("windowRect", 10, &propkey)) != JsNoError
			|| (err = JsSetProperty(retval, propkey, propval, true)) != JsNoError
			|| (err = MakeRect(rcClient, propval)) != JsNoError
			|| (err = JsCreatePropertyId("clientRect", 10, &propkey)) != JsNoError
			|| (err = JsSetProperty(retval, propkey, propval, true)) != JsNoError
			|| (err = JsBoolToBoolean(isMax, &propval)) != JsNoError
			|| (err = JsCreatePropertyId("maximized", 9, &propkey)) != JsNoError
			|| (err = JsSetProperty(retval, propkey, propval, true)) != JsNoError
			|| (err = JsBoolToBoolean(isMin, &propval)) != JsNoError
			|| (err = JsCreatePropertyId("minimized", 9, &propkey)) != JsNoError
			|| (err = JsSetProperty(retval, propkey, propval, true)) != JsNoError)
			return js->Throw(err, _T("HWND.getWindowPos"));

		return retval;
	}

	return js->undefVal;
}

// -----------------------------------------------------------------------
//
// Native pointer type
//

JavascriptEngine::NativePointerData::NativePointerData(void *ptr, size_t size, SigParser *sig, WCHAR stringType) :
	ptr(ptr), size(size), sig(sig->sig), stringType(stringType)
{
	// add me to the native pointer map, to keep the underlying native
	// data block we reference alive in dead object scans
	inst->nativePointerMap.emplace(this, static_cast<BYTE*>(ptr));
}

JavascriptEngine::NativePointerData::~NativePointerData()
{
	// remove me from the native pointer map
	inst->nativePointerMap.erase(this);

	// any objects we referenced might now be unreachable, so scheule a scan
	inst->ScheduleDeadObjectScan();
}


JsErrorCode JavascriptEngine::NativePointerData::Create(
	void *ptr, size_t size, SigParser *sig, WCHAR stringType, JsValueRef *jsval)
{
	// if the native value is a null pointer, return Javascript null
	if (ptr == nullptr)
	{
		*jsval = inst->nullVal;
		return JsNoError;
	}

	// create the external object
	JsErrorCode err;
	if ((err = CreateExternalObjectWithPrototype(*jsval, inst->NativePointer_proto,
		new NativePointerData(ptr, size, sig, stringType))) != JsNoError)
		return err;

	// make sure the length fits
	if (size > (1ULL << DBL_MANT_DIG))
	{
		ThrowSimple("NativePointer: object is too large (byte size exceeds Javascript Number capacity)");
		return JsNoError;
	}

	// set the length property to the byte length
	JsValueRef lengthVal;
	const TCHAR *where = _T("JsIntToNumber(length)");
	if ((err = JsDoubleToNumber(static_cast<double>(size), &lengthVal)) != JsNoError
		|| (err = inst->SetReadonlyProp(*jsval, "length", lengthVal, where)) != JsNoError)
		return err;

	// success
	return JsNoError;
}

JsValueRef CALLBACK JavascriptEngine::NativePointerData::ToString(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = NativePointerData::Recover<NativePointerData>(argv[0], _T("NativePointer.toString()")); self != nullptr)
	{
		// If it's a native string pointer type, use toStringZ.  Note
		// that we explicitly DO NOT pass any additional arguments to
		// toStringZ, as toString doesn't accept any options.
		if (self->stringType != 0)
			return ToStringZ(callee, isConstructCall, argv, 1, ctx);

		// It's not a string type.  Use a descriptive representation, a la
		// Javascript's generic Object.toString() format of "[Object Class]"
		{
			WCHAR buf[40];
			swprintf_s(buf, L"0x%p[%Iu bytes]", self->ptr, self->size);
			JsPointerToString(buf, wcslen(buf), &ret);
		}
	}
	return ret;
}

JsValueRef CALLBACK JavascriptEngine::NativePointerData::ToStringZ(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = NativePointerData::Recover<NativePointerData>(argv[0], _T("NativePointer.toString()")); self != nullptr)
	{
		// set default options
		int maxLength = -1;
		int length = -1;
		UINT codePage = CP_ACP;

		// check for options
		if (argc >= 2)
		{
			// options.codePage = code page number or "utf8"
			JsPropertyIdRef propid;
			JsValueRef propval, numval;
			JsValueType proptype;
			double d;
			if (JsCreatePropertyId("codePage", 8, &propid) == JsNoError
				&& JsGetProperty(argv[1], propid, &propval) == JsNoError
				&& JsGetValueType(propval, &proptype) == JsNoError)
			{
				if (proptype == JsNumber)
				{
					JsNumberToDouble(propval, &d);
					codePage = static_cast<UINT>(d);
				}
				else if (proptype == JsString)
				{
					const wchar_t *p;
					size_t len;
					JsStringToPointer(propval, &p, &len);
					if (len == 4 && _wcsnicmp(p, L"utf8", 4) == 0)
						codePage = CP_UTF8;
					else
						return ThrowSimple("NativePointer.toStringZ(): invalid codePage option");
				}
				else
					return ThrowSimple("NativePointer.toStringZ(): invalid codePage option");
			}

			// options.maxLength = number
			if (JsCreatePropertyId("maxLength", 9, &propid) == JsNoError
				&& JsGetProperty(argv[1], propid, &propval) == JsNoError
				&& JsConvertValueToNumber(propval, &numval) == JsNoError
				&& JsNumberToDouble(propval, &d) == JsNoError)
			{
				if (d > static_cast<double>(INT_MAX))
					return ThrowSimple("NativePointer.toStringZ(): maxLength is out of range");

				maxLength = static_cast<int>(d);
			}

			// options.length = number
			if (JsCreatePropertyId("length", 6, &propid) == JsNoError
				&& JsGetProperty(argv[1], propid, &propval) == JsNoError
				&& JsConvertValueToNumber(propval, &numval) == JsNoError
				&& JsNumberToDouble(propval, &d) == JsNoError)
			{
				if (d > static_cast<double>(INT_MAX))
					return ThrowSimple("NativePointer.toStringZ(): length is out of range");

				length = static_cast<int>(d);
			}
		}

		// skip any const qualification
		const WCHAR *p = self->sig.c_str();
		if (*p == '%')
			++p;

		// check what type we have
		switch (*p)
		{
		case 'c':
		case 'C':
			// 8-bit integer types.  Take it to represent a single-byte character
			// string.
			{
				WCHAR *wstr = 0;
				__try
				{
					// get the 8-bit character source string
					auto cstr = static_cast<const CHAR*>(self->ptr);

					// If an exact length wasn't specified in the options, search for
					// a null byte, up to the maximum length limit.
					if (length < 0)
					{
						size_t srcLength = maxLength >= 0 ? strnlen_s(cstr, maxLength) : strlen(cstr);
						if (srcLength > static_cast<size_t>(INT_MAX))
							return ThrowSimple("NativePointer.toStringZ(): length is out of range");

						length = static_cast<int>(srcLength);
					}

					// apply the maximum length if specified, even if a length was specified
					if (maxLength >= 0 && length > maxLength)
						length = maxLength;

					// figure out how much space we need
					int wlen = MultiByteToWideChar(codePage, 0, cstr, length, 0, 0);
					wstr = new WCHAR[wlen];

					// do the actual conversion
					MultiByteToWideChar(codePage, 0, cstr, length, wstr, wlen);

					// create the javacode string
					JsPointerToString(wstr, wlen, &ret);
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					ThrowSimple("Memory at native pointer is unreadable, or string is unterminated");
				}

				delete[] wstr;
			}
			break;

		case 's':
		case 'S':
			// 16-bit integer types.  Take it to represent a Unicode character string.
			__try
			{
				// get the 16-bit character source string
				auto wstr = static_cast<const WCHAR*>(self->ptr);

				// If an exact length wasn't specified, search for a null byte, up to 
				// the maximum length limit.
				if (length < 0)
				{
					size_t srcLength = maxLength >= 0 ? wcsnlen_s(wstr, maxLength) : wcslen(wstr);
					if (srcLength > static_cast<size_t>(INT_MAX))
						return ThrowSimple("Native string is too long");

					length = static_cast<int>(srcLength);
				}

				// apply the maximum length if specified, even if a length was specified
				if (maxLength >= 0 && length > maxLength)
					length = maxLength;

				// create the Javascript result
				JsPointerToString(wstr, length, &ret);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return ThrowSimple("Memory at native pointer is unreadable, or string is unterminated");
			}
			break;

		default:
			// other types can't be interpreted as strings
			return ThrowSimple("Native pointer does not point to a string type");
			break;
		}
	}

	return ret;
}

JsValueRef CALLBACK JavascriptEngine::NativePointerData::ToNumber(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = NativePointerData::Recover<NativePointerData>(argv[0], _T("NativePointer.toNumber()")); self != nullptr)
	{
		double d = static_cast<double>(reinterpret_cast<UINT_PTR>(self->ptr));
		JsDoubleToNumber(d, &ret);
		if (d > static_cast<double>(1LL << DBL_MANT_DIG))
		{
			JsValueRef msg, exc;
			const WCHAR *txt = L"Value out of range";
			JsPointerToString(txt, wcslen(txt), &msg);
			JsCreateError(msg, &exc);
			JsSetException(exc);
		}
	}
	return ret;
}

JsValueRef CALLBACK JavascriptEngine::NativePointerData::ToUInt64(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = NativePointerData::Recover<NativePointerData>(argv[0], _T("NativePointer.toUint64()")); self != nullptr)
	{
		// create the Uint64 object
		XInt64Data<UINT64>::CreateFromInt(reinterpret_cast<UINT64>(self->ptr), ret);
	}
	return ret;
}

JsValueRef CALLBACK JavascriptEngine::NativePointerData::FromNumber(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	// get the number as an int64 value
	UINT64 i = argc >= 2 ? XInt64Data<UINT64>::FromJavascript(argv[1]) : 0;

	// create a void* pointer for the value
	JsValueRef jsval;
	SigParser sig(_T("v"), 1);
	if (JsErrorCode err = Create(reinterpret_cast<void*>(i), 0, &sig, 0, &jsval); err != JsNoError)
		inst->Throw(err, _T("NativePointer.fromNumber"));

	// return the object
	return jsval;
}

JsValueRef CALLBACK JavascriptEngine::NativePointerData::ToArrayBuffer(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	auto js = JavascriptEngine::Get();
	if (auto self = NativePointerData::Recover<NativePointerData>(argv[0], _T("NativePointer.toArrayBuffer()")); self != nullptr)
	{
		// make sure it fits - we only get an 'unsigned int' for the byte size from JS,
		// which is smaller than size_t on x64
		if (self->size > UINT_MAX)
			return js->Throw(_T("NativePointer.toArrayBuffer(): native array is too large"));

		// create the array buffer
		JsErrorCode err = JsCreateExternalArrayBuffer(self->ptr, static_cast<unsigned int>(self->size), nullptr, nullptr, &ret);
		if (err != JsNoError)
			return js->Throw(err, _T("NativePointer.toArrayBuffer(), creating ArrayBuffer object"));

		// Add a cross-reference from the ArrayBuffer to the pointer.  This will 
		// the pointer alive as long as the ArrayBuffer object is alive, which will
		// in turn keep the underlying native storage alive, since the dead object
		// scanner will see our pointer into the native storage.
		if ((err = JsSetProperty(ret, inst->xrefPropertyId, argv[0], true)) != JsNoError)
			return js->Throw(err, _T("NativePointer.toArrayBuffer(), setting xref"));
	}
	return ret;
}

JsValueRef CALLBACK JavascriptEngine::NativePointerData::To(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	// get the new pointer type
	JsErrorCode err;
	const wchar_t *p = nullptr;
	size_t len = 0;
	if (argc >= 2)
	{
		JsValueRef str;
		if ((err = JsConvertValueToString(argv[1], &str)) != JsNoError
			|| (err = JsStringToPointer(str, &p, &len)) != JsNoError)
			return inst->Throw(err, _T("NativePointer.to"));
	}

	// make sure we got a non-empty string
	if (p == 0 || len == 0)
		return inst->Throw(_T("NativePointer.to: new type missing"));

	// get our native pointer
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = NativePointerData::Recover<NativePointerData>(argv[0], _T("NativePointer.to()")); self != nullptr)
	{
		// if the new type a pointer type, create another pointer object;
		// otherwise create a regular native object
		if (*p == '*')
		{
			// get the size of the new dereferenced type
			SigParser sig(p + 1, SigParser::EndOfArg(p + 1, p + len));
			MarshallBasicSizer sizer(&sig, JS_INVALID_REFERENCE);
			sizer.MarshallValue();

			// create the pointer
			if ((err = NativePointerData::Create(self->ptr, sizer.size, &sig, 0, &ret)) != JsNoError)
				return inst->Throw(err, _T("NativePointer.to"));
		}
		else
		{
			// create a main signature parser with all of our reference types
			SigParser sig(p, len);
			ret = inst->CreateNativeObject(&sig, &self->ptr);
		}
	}

	return ret;

}


JsValueRef CALLBACK JavascriptEngine::NativePointerData::ToArray(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	// get the desired number of elements
	JsErrorCode err;
	double nEles = 1.0;
	if (argc >= 2)
	{
		JsValueRef num;
		if ((err = JsConvertValueToNumber(argv[1], &num)) != JsNoError
			|| (err = JsNumberToDouble(num, &nEles)) != JsNoError)
			return inst->Throw(err, _T("NativePointer.toArray()"));

		if (nEles < 1 || nEles > SIZE_MAX)
			return inst->Throw(_T("NativePointer.toArray(): array dimension is out of range"));
	}

	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = NativePointerData::Recover<NativePointerData>(argv[0], _T("NativePointer.toArrayBuffer()")); self != nullptr)
	{
		// create a signature for an array of nEles of our referenced type
		WSTRINGEx arraySig;
		arraySig.Format(_T("[%Iu]%s"), static_cast<size_t>(nEles), self->sig.c_str());
		SigParser sigprs(arraySig);
		ret = inst->CreateNativeObject(&sigprs, self->ptr);
	}

	return ret;
}

JsValueRef CALLBACK JavascriptEngine::NativePointerData::IsNull(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = NativePointerData::Recover<NativePointerData>(argv[0], _T("NativePointer.isNull()")); self != nullptr)
		JsBoolToBoolean(self->ptr == nullptr, &ret);

	return ret;
}


JsValueRef CALLBACK JavascriptEngine::NativePointerData::At(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = NativePointerData::Recover<NativePointerData>(argv[0], _T("NativePointer.at()")); self != nullptr)
	{
		// we can't dereference a null pointer
		if (self->ptr == nullptr)
			return inst->Throw(_T("Attempting to derefeference a null native pointer (pointer.at())"));

		// we can't dereference a void pointer
		if (self->size == 0 || self->sig == L"v" || self->sig == L"%v")
			return inst->Throw(_T("Native pointer to 'void' can't be dereferenced (pointer.at())"));

		// test the memory area to make sure it's valid
		if (!self->TestAt(self->ptr, self->size))
			return inst->Throw(_T("Bad native pointer dereference: referenced memory location is invalid or inaccessible (pointer.at())"));

		// looks good - create the native data wrapper
		SigParser sig(self->sig);
		ret = inst->CreateNativeObject(&sig, self->ptr);
	}
	return ret;
}

bool JavascriptEngine::NativePointerData::TestAt(void *ptr, size_t size)
{
	__try
	{
		// Try reading from the memory.  Declare the referenced memory as
		// volatile so that the compiler doesn't try to optimize away the
		// references.
		volatile BYTE a, b;
		volatile BYTE *p = static_cast<volatile BYTE*>(ptr);
		a = p[0];
		b = (size != 0 ? p[size - 1] : 0);

		// If the referenced type isn't const ('%' prefix), try writing back
		// the same values to make sure the memory is writable.
		if (sig[0] != '%')
		{
			p[0] = a;
			if (size != 0)
				p[size - 1] = b;
		}

		// passed
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

// -----------------------------------------------------------------------
//
// Native 64-bit integer types
//

template<typename T>
T JavascriptEngine::XInt64Data<T>::FromJavascript(JsValueRef jsval)
{
	// get the argument type
	JsValueType type;
	JsErrorCode err = JsGetValueType(jsval, &type);
	if (err != JsNoError)
	{
		inst->Throw(err, _T("Int64 new"));
		return 0;
	}

	switch (type)
	{
	case JsUndefined:
	case JsNull:
		// use 0 as the value of these
		return 0;

	case JsNumber:
		// number - a native double type
		{
			// get the double
			double d;
			JsNumberToDouble(jsval, &d);

			// range-check it
			bool overflow;
			if (std::is_signed<T>::value)
				overflow = d < static_cast<double>(INT64_MIN) || d > static_cast<double>(INT64_MAX);
			else
				overflow = d < 0.0 || d > static_cast<double>(UINT64_MAX);
			
			if (overflow)
			{
				inst->Throw(_T("Int64 math overflow converting number operand"));
				return 0;
			}

			// use this as the value
			return static_cast<T>(d);
		}
		break;

	case JsObject:
		// we can construct from other 64-bit types
		{
			// check for an external Int64 or Uint64 object
			void *extdata;
			if ((err = JsGetExternalData(jsval, &extdata)) == JsNoError)
			{
				if (auto b = XInt64Data<INT64>::Recover<XInt64Data<INT64>>(extdata, nullptr); b != nullptr)
					return static_cast<T>(b->i);
				else if (auto b = XInt64Data<UINT64>::Recover<XInt64Data<UINT64>>(extdata, nullptr); b != nullptr)
					return static_cast<T>(b->i);
			}

			// Check for "high" and "low" properties.  If the properties are
			// present, interpret them as numbers, and build the result out
			// of the two values.
			JsPropertyIdRef highId, lowId;
			JsValueRef highVal, lowVal, highNum, lowNum;
			double high, low;
			bool hasProp;
			if (JsCreatePropertyId("high", 4, &highId) == JsNoError
				&& JsHasProperty(jsval, highId, &hasProp) == JsNoError && hasProp
				&& JsCreatePropertyId("low", 3, &lowId) == JsNoError
				&& JsHasProperty(jsval, lowId, &hasProp) == JsNoError && hasProp
				&& JsGetProperty(jsval, highId, &highVal) == JsNoError
				&& JsConvertValueToNumber(highVal, &highNum) == JsNoError
				&& JsNumberToDouble(highNum, &high) == JsNoError
				&& JsGetProperty(jsval, lowId, &lowVal) == JsNoError
				&& JsConvertValueToNumber(lowVal, &lowNum) == JsNoError
				&& JsNumberToDouble(lowNum, &low) == JsNoError)
			{
				return (static_cast<T>(high) << 32) | static_cast<DWORD>(low);
			}

			// use the default string handling
			goto use_string;
		}
		break;

	default:
	use_string:
		// try parsing anything else as a string
		{
			T i = 0;
			if (!ParseString(jsval, i))
				return 0;
			return i;
		}
	}
}

template<typename T>
JsValueRef JavascriptEngine::XInt64Data<T>::Create(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	T i = 0;
	if (argc > 1)
		i = FromJavascript(argv[1]);

	// create the value
	CreateFromInt(i, ret);
	return ret;
}

template<typename T>
bool JavascriptEngine::XInt64Data<T>::ParseString(JsValueRef jsval, T &val)
{
	// start with a zero value
	val = 0;

	// convert the value to string
	JsErrorCode err;
	JsValueRef strval;
	if ((err = JsConvertValueToString(jsval, &strval)) != JsNoError)
	{
		inst->Throw(err, _T("Int64 parse string"));
		return false;
	}

	// retrieve the string
	const WCHAR *p, *pEnd;
	size_t len;
	if ((err = JsStringToPointer(strval, &p, &len)) != JsNoError)
	{
		inst->Throw(err, _T("Int64 parse string"));
		return false;
	}
	pEnd = p + len;

	// skip leading spaces
	for (; iswspace(*p) && p < pEnd; ++p);

	// check for a negative or positive
	bool neg = false;
	while (p < pEnd)
	{
		if (*p == '+')
			++p;
		else if (*p == '-')
			neg = !neg, ++p;
		else
			break;
	}

	// check the radix
	int radix = 10;
	if (p + 1 < pEnd && p[0] == '0' && p[1] == 'x')
	{
		radix = 16;
		p += 2;
	}
	else if (p + 1 < pEnd && p[0] == '0' && p[1] == 'b')
	{
		radix = 2;
		p += 2;
	}
	else if (p < pEnd && p[0] == '0')
	{
		radix = 8;
		++p;
	}

	// accumulate digits
	T acc = 0;
	for (; p < pEnd; ++p)
	{
		int dig = 0;
		if (*p < '0')
			break;
		if (radix == 2)
		{
			if (*p <= '1')
				dig = *p - '0';
			else
				break;
		}
		else if (radix == 8)
		{
			if (*p <= '7')
				dig = *p - '0';
			else
				break;
		}
		else if (radix == 10)
		{
			if (*p <= '9')
				dig = *p - '0';
			else
				break;
		}
		else
		{
			if (*p <= '9')
				dig = *p - '0';
			else if (*p >= 'a' && *p <= 'f')
				dig = *p - 'a' + 10;
			else if (*p >= 'A' && *p <= 'F')
				dig = *p - 'A' + 10;
			else
				break;
		}

		acc *= radix;
		acc += dig;
	}

	// apply the sign
	if constexpr (std::is_signed<T>::value)
	{
		if (neg)
			acc = -acc;
	}

	// return the result
	val = acc;
	return true;
}

template<typename T> 
JsErrorCode JavascriptEngine::XInt64Data<T>::CreateFromInt(T val, JsValueRef &jsval)
{
	// wrap it in a javsacript external object
	JsErrorCode err;
	if ((err = CreateExternalObjectWithPrototype(jsval, 
		std::is_signed<T>::value ? inst->Int64_proto : inst->UInt64_proto,
		new XInt64Data<T>(val))) != JsNoError)
		inst->Throw(err, _T("Int64 math: creating result"));

	// return the result
	return err;
}

template<typename T>
JsValueRef JavascriptEngine::XInt64Data<T>::ToString(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	JsErrorCode err;
	if (auto self = XInt64Data<T>::Recover<XInt64Data<T>>(argv[0], _T("Int64.toString()")); self != nullptr)
	{
		// get the value
		T i = self->i;

		// get the radix argument, if present
		int radix = 10;
		if (argc >= 2)
		{
			JsValueRef radixval;
			double dradix;
			if ((err = JsConvertValueToNumber(argv[1], &radixval)) == JsNoError
				&& (err = JsNumberToDouble(radixval, &dradix)) == JsNoError
				&& dradix >= 2.0 && dradix <= 36.0)
				radix = static_cast<int>(dradix);
		}

		// Set up a result buffer.  In the longest case, we'll have 64 digits
		// (bits) plus a sign.
		WCHAR buf[70];
		WCHAR *endp = buf + countof(buf);
		WCHAR *p = endp;

		// To properly handle the case of the smallest negative signed value, do the
		// actual digit iterations using an unsigned container.  We definitely won't
		// have a sign after taking the absolute value, so this will work whether the
		// underlying value is signed or unsigned.
		UINT64 ui;

		// if the value is negative, start with a sign
		bool neg = false;
		if constexpr (std::is_signed<T>::value)
		{
			if (i < 0)
			{
				neg = true;
				ui = static_cast<UINT64>(-i);
			}
			else
				ui = static_cast<UINT64>(i);
		}
		else
			ui = i;

		// work backwards through the digits from the least significant end
		do
		{
			int digit = static_cast<int>(ui % radix);
			if (digit <= 9)
				*--p = static_cast<WCHAR>(digit + '0');
			else
				*--p = static_cast<WCHAR>(digit - 10 + 'A');

			ui /= radix;

		} while (ui != 0);

		// add the sign
		if (neg)
			*--p = '-';

		// return the string
		err = JsPointerToString(p, endp - p, &ret);
		if (err != JsNoError)
			inst->Throw(err, _T("Int64.toString()"));
	}
	return ret;
}

template<typename T>
JsValueRef JavascriptEngine::XInt64Data<T>::ToObject(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = XInt64Data<T>::Recover<XInt64Data<T>>(argv[0], _T("Int64.toObject()")); self != nullptr)
	{
		// figure the high and low parts as raw 32-bit UINT32's
		UINT32 hi = static_cast<UINT32>((self->i >> 32) & 0xFFFFFFFF);
		UINT32 lo = static_cast<UINT32>(self->i & 0xFFFFFFFF);

		// convert to double, retaining the sign for the high part only
		double dhi = std::is_signed<T>::value ? static_cast<double>(static_cast<INT32>(hi)) : static_cast<double>(hi);
		double dlo = static_cast<double>(lo);

		JsErrorCode err;
		JsPropertyIdRef prop;
		JsValueRef num;
		if ((err = JsCreateObject(&ret)) != JsNoError
			|| (err = JsCreatePropertyId("high", 4, &prop)) != JsNoError
			|| (err = JsDoubleToNumber(dhi, &num)) != JsNoError
			|| (err = JsSetProperty(ret, prop, num, true)) != JsNoError
			|| (err = JsCreatePropertyId("low", 3, &prop)) != JsNoError
			|| (err = JsDoubleToNumber(dlo, &num)) != JsNoError
			|| (err = JsSetProperty(ret, prop, num, true)) != JsNoError)
			inst->Throw(err, _T("Int64.toObject"));
	}
	return ret;
}

template<typename T>
JsValueRef JavascriptEngine::XInt64Data<T>::ToNumber(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = XInt64Data<T>::Recover<XInt64Data<T>>(argv[0], _T("Int64.toNumber()")); self != nullptr)
	{
		// make sure it fits in a double without loss of precision
		static const INT64 MAX = 1i64 << DBL_MANT_DIG;
		bool overflow;
		if (std::is_signed<T>::value)
			overflow = self->i > static_cast<UINT64>(MAX);
		else
			overflow = self->i < -MAX || self->i > MAX;

		if (overflow)
			inst->Throw(_T("Int64.toNumber: value out of range"));

		// store the result
		JsErrorCode err = JsDoubleToNumber(static_cast<double>(self->i), &ret);
		if (err != JsNoError)
			inst->Throw(err, _T("Int64.toNumber"));
	}
	return ret;
}

template<typename T>
JsValueRef JavascriptEngine::XInt64Data<T>::UnaryOp(JsValueRef *argv, unsigned short argc, void *ctx, std::function<T(T)> op)
{
	// we need a 'this'
	if (argc == 0)
		return inst->undefVal;

	// get the source value
	T a = 0;
	if (auto self = XInt64Data<T>::Recover<XInt64Data<T>>(argv[0], _T("Int64 math")); self != nullptr)
		a = self->i;
	else
		return inst->Throw(_T("Int64 math: 'this' is not an int64 type"));

	// figure the result
	T result = op(a);

	// create a new object to represent the result
	JsValueRef newobj = JS_INVALID_REFERENCE;
	if (JsErrorCode err = CreateFromInt(result, newobj); err != JsNoError)
		return inst->Throw(err, _T("Int64 math"));

	// return the result objecct
	return newobj;
}

template<typename T>
JsValueRef JavascriptEngine::XInt64Data<T>::BinOp(JsValueRef *argv, unsigned short argc, void *ctx, std::function<T(T, T)> op)
{
	// we need a 'this'
	if (argc == 0)
		return inst->undefVal;

	// return self unchanged if there's no second operand
	if (argc == 1)
		return argv[0];

	// get the source value
	T a = 0;
	if (auto self = XInt64Data<T>::Recover<XInt64Data<T>>(argv[0], _T("Int64 math")); self != nullptr)
		a = self->i;
	else
		return inst->Throw(_T("Int64 math: 'this' is not an int64 type"));

	// get the operand type
	JsValueType type;
	JsErrorCode err = JsGetValueType(argv[1], &type);
	if (err != JsNoError)
		return inst->Throw(err, _T("Int64 math"));

	switch (type)
	{
	case JsUndefined:
		// return self unchanged if the second operand is 'undefined'
		return argv[0];

	case JsNull:
		// math op on null yields null
		return inst->nullVal;

	case JsNumber:
		// number - a native double type
		{
			// get the double
			double d;
			JsNumberToDouble(argv[1], &d);

			// range-check it
			bool overflow;
			if (std::is_signed<T>::value)
				overflow = d < static_cast<double>(INT64_MIN) || d > static_cast<double>(INT64_MAX);
			else
				overflow = d < 0.0 || d > static_cast<double>(UINT64_MAX);
			if (overflow)
				return inst->Throw(_T("Int64 math overflow converting number operand"));

			// convert it to our type and do the math
			return ToJs(op(a, static_cast<T>(d)));
		}
		break;

	case JsObject:
		// we can operate on other 64-bit types
		{
			if (auto b = XInt64Data<INT64>::Recover<XInt64Data<INT64>>(argv[1], nullptr); b != nullptr)
				return ToJs(op(a, static_cast<T>(b->i)));
			else if (auto b = XInt64Data<UINT64>::Recover<XInt64Data<UINT64>>(argv[1], nullptr); b != nullptr)
				return ToJs(op(a, static_cast<T>(b->i)));
			else
				return inst->Throw(_T("Int64 math: invalid operand"));
		}
		break;

	default:
		// try parsing anything else as a string
		{
			T b;
			if (!ParseString(argv[1], b))
				return inst->undefVal;

			return ToJs(op(a, b));
		}
		break;
	}
}

template<typename T>
JsValueRef CALLBACK JavascriptEngine::XInt64Data<T>::Compare(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
{
	// we need a 'this'
	if (argc == 0)
		return inst->undefVal;

	// return self unchanged if there's no second operand
	if (argc == 1)
		return argv[0];

	// get the source value
	T a = 0;
	if (auto self = XInt64Data<T>::Recover<XInt64Data<T>>(argv[0], _T("Int64 math")); self != nullptr)
		a = self->i;
	else
		return inst->Throw(_T("Int64 math: 'this' is not an int64 type"));

	// get the operand type
	JsValueType type;
	JsErrorCode err = JsGetValueType(argv[1], &type);
	if (err != JsNoError)
		return inst->Throw(err, _T("Int64 math"));

	// basic comparison operator
	auto Cmp = [](T a, T b) { return ToJsInt(a == b ? 0 : a < b ? -1 : 1); };

	switch (type)
	{
	case JsUndefined:
		// compare to zero if the second operand is null or undefined
		return Cmp(a, 0);

	case JsNull:
		// math op on null yields null
		return inst->nullVal;

	case JsNumber:
		// Number - a native double type
		{
			// get the double
			double d;
			JsNumberToDouble(argv[1], &d);

			// check if we're signed or unsigned
			if constexpr (std::is_signed<T>::value)
			{
				// Unsigned:
				//
				//  - self is non-negative, so if d < 0, self > d
				//  - self is in 0..UINT64_MAX, so if d is > UINT64_MAX, self < d
				if (d < 0)
					return ToJsInt(1);
				else if (d > static_cast<double>(UINT64_MAX))
					return ToJsInt(-1);
			}
			else
			{
				// Signed:  self is in INT64_MIN..INT64_MAX, so if d is outside that
				// range above or below, self < or > d
				if (d < static_cast<double>(INT64_MIN))
					return ToJsInt(1);
				else if (d > static_cast<double>(INT64_MAX))
					return ToJsInt(-1);
			}

			// it's in range - cast it to our type and compare it
			return Cmp(a, static_cast<T>(d));
		}
		break;

	case JsObject:
		// we can operate on another 64-bit int type
		if (auto b = XInt64Data<INT64>::Recover<XInt64Data<INT64>>(argv[1], nullptr); b != nullptr)
		{
			// b is signed
			if (std::is_signed<T>::value)
			{
				// apples to apples
				return Cmp(a, b->i);
			}
			else
			{
				// compare(unsigned, signed): if b < 0, a > b; otherwise b fits
				// in an unsigned, so cast it and compare two unsigneds
				return b->i < 0 ? ToJsInt(1) : Cmp(a, static_cast<T>(b->i));
			}
		}
		else if (auto b = XInt64Data<UINT64>::Recover<XInt64Data<UINT64>>(argv[1], nullptr); b != nullptr)
		{
			// b is unsigned
			if (std::is_unsigned<T>::value)
			{
				// apples to apples
				return Cmp(a, b->i);
			}
			else
			{
				// compare(signed, unsigned): if a < 0, a < b; otherwise a fits
				// in an unsigned, so cast it and compare two unsigneds
				return a < 0 ? ToJsInt(-1) : Cmp(static_cast<std::make_unsigned<T>::type>(a), b->i);
			}
		}
		else
			return inst->Throw(_T("Int64 math: invalid operand"));
		break;

	default:
		// for anything else, try parsing it as a string and converting to type T
		{
			T b;
			if (!ParseString(argv[1], b))
				return inst->undefVal;

			return Cmp(a, b);
		}
		break;
	}
}

template<typename T>
JsValueRef JavascriptEngine::XInt64Data<T>::ToJs(T val)
{
	// create a new object to represent the result
	JsValueRef newobj;
	JsErrorCode err;
	if ((err = CreateFromInt(val, newobj)) != JsNoError)
		return inst->Throw(err, _T("Int64 math"));

	// return the result object
	return newobj;
}

template<typename T>
JsValueRef JavascriptEngine::XInt64Data<T>::ToJsInt(int val)
{
	// create a new object to represent the result
	JsValueRef ret;
	JsErrorCode err;
	if ((err = JsIntToNumber(val, &ret)) != JsNoError)
		return inst->Throw(err, _T("Int64 math"));

	// return the result
	return ret;
}

// -----------------------------------------------------------------------
//
// Dynamic code generation manager
//

JavascriptEngine::CodeGenManager::CodeGenManager()
{
	// get the hardware memory manager page size
	SYSTEM_INFO si;
	GetNativeSystemInfo(&si);
	memPageSize = si.dwPageSize;

	// Store the function size.  Use the worst-case (largest) size, since
	// we always allocate at fixed size.  We round up to 16-byte boundaries,
	// since the Microsoft compilers align proc entrypoints, even though
	// there's no particular hardware reason to do this.
	funcSize = IF_32_64(16, 64);
}

JavascriptEngine::CodeGenManager::~CodeGenManager()
{
}

// DLL import callback glue function.  This is an external assembler routine:
// see DllCallGlue32.asm and DllCallGlue64.asm.
extern "C" void DllImportCallbackGlue(void);

FARPROC JavascriptEngine::CodeGenManager::Generate(JavascriptCallbackWrapper *wrapper)
{
	// Allocate a function block.  If a block is available from
	// the recycle list, use that; otherwise allocate a new one
	// out of virtual memory.
	BYTE *addr = nullptr;
	if (recycle.size() != 0)
	{
		// a recycled function is available - reuse it
		addr = recycle.front().addr;
		recycle.pop_front();
	}
	else
	{
		// There's no free list entry to reuse, so carve out a new chunk from
		// a page.  If the current page doesn't have enough space left, allocate
		// a whole new page.
		if (pages.size() == 0 || pages.back().used + funcSize > memPageSize)
		{
			// allocate a new page
			BYTE *ptr = static_cast<BYTE*>(VirtualAlloc(NULL, memPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
			if (ptr == NULL)
				return nullptr;

			// add the page
			pages.emplace_back(ptr);
		}

		// carve out space from the last page
		addr = pages.back().addr + pages.back().used;
		pages.back().used += funcSize;
	}

	// Get the context object and glue function address as INT_PTR values
	// for easier byte dissection
	INT_PTR iWrapper = reinterpret_cast<INT_PTR>(wrapper);
	INT_PTR iGlueAddr = reinterpret_cast<INT_PTR>(&DllImportCallbackGlue);
	INT_PTR iAddr = reinterpret_cast<INT_PTR>(addr);
	static const auto ByteAt = [](INT_PTR val, int shift) { return static_cast<BYTE>((val >> shift) & 0xFF);  };
	static const auto Put2 = [](BYTE *addr, int val)
	{
		addr[0] = ByteAt(val, 0);
		addr[1] = ByteAt(val, 8);
	};
	static const auto Put4 = [](BYTE *addr, INT_PTR val)
	{
		addr[0] = ByteAt(val, 0);
		addr[1] = ByteAt(val, 8);
		addr[2] = ByteAt(val, 16);
		addr[3] = ByteAt(val, 24);
	};
	static const auto Put8 = [](BYTE *addr, INT_PTR val)
	{
		addr[0] = ByteAt(val, 0);
		addr[1] = ByteAt(val, 8);
		addr[2] = ByteAt(val, 16);
		addr[3] = ByteAt(val, 24);
		addr[4] = ByteAt(val, 32);
		addr[5] = ByteAt(val, 40);
		addr[6] = ByteAt(val, 48);
		addr[7] = ByteAt(val, 56);
	};

	// Generate the thunk code.  The machine code is necessarily platform-specific.
#if defined(_M_IX86)
	
	// x86 __cdecl:  The caller removes arguments, so we just do a simple
	// return when done.
	// 
	//    mov eax, <wrapper object address>
	//    call DllImportCallback
	//    ret
	//
	// x86 __stdcall:  The callee (the thunk) removes arguments.  We'll have
	// to count up our stack arguments and perform a RET <n> instruction to 
	// remove them.  
	//
	//    mov eax, <wrapper object address>
	//    call DllImportCallback
	//    ret <stack slots * 4>
	//
	addr[0] = 0xB8;   // MOV EAX, <immediate32>
	Put4(addr + 1, iWrapper);

	// The CALL instructions uses EIP-relative addressing.  Figure the
	// offset from "$" (the next instruction after the CALL) to the glue
	// routine entrypoint.
	INT_PTR iRelJmp = iGlueAddr - (iAddr + 10);

	// generate the CALL
	addr[5] = 0xE8;     // CALL $+<immediate32>
	Put4(addr + 6, iRelJmp);

	// generate RET sequence
	switch (wrapper->callingConv)
	{
	case 'C':
		// __cdecl: caller removes arguments.  Use a simple return
		addr[10] = 0xC3;    // RET
		break;

	case 'S':
		// __stdcall: thunk removes arguments.  Use a CALL to the glue routine so
		// that we can come back to the thunk.
		{
			// count argument stack slots
			SigParser sig(wrapper->sig);
			MarshallStackArgSizer mas(&sig, nullptr, 0, 0);
			mas.Marshall();
			
			// generate the return with argument removal
			addr[10] = 0xc2;    // RET <bytes to remove>
			Put2(addr + 11, mas.nSlots * 4);
		}
		break;

	default:
		inst->Throw(MsgFmt(_T("dllImport: unsupported calling convention in callback function (%c)"), wrapper->callingConv));
		break;
	}

#elif defined(_M_X64)

	// x64:
	//
	//   mov   rax, <wrapper object address>
	//   movq  [rsp + 8], RCX or XMM0   ; depending on whether first arg is float/double or int
	//   movq  [rsp + 16], RDX or XMM1
	//   movq  [rsp + 24], R8 or XMM2
	//   movq  [rsp + 32], R9 or XMM3
	//   jmp DllImportCallback

	// set up RAX with the context object address
	addr[0] = 0x48;  // movabs rax, <immediate64>
	addr[1] = 0xb8;  // "
	Put8(addr + 2, iWrapper);

	// move the first four arguments from registers to the stack
	int ofs = 10;
	SigParser sig(wrapper->sig);
	MarshallBasicSizer sizer(&sig, JS_INVALID_REFERENCE);
	static const char *intRegs[] = {
		"\x48\x89\x4C\x24\x08",         // mov [rsp+8], rcx
		"\x48\x89\x54\x24\x10",         // mov [rsp+16], rdx
		"\x4C\x89\x44\x24\x18",         // mov [rsp+24], r8
		"\x4C\x89\x4C\x24\x20"          // mov [rsp+32], r9
	};
	static const char *fpRegs[] = {
		"\x66\x0F\xD6\x44\x24\x08",     // movq [rsp+8], xmm0
		"\x66\x0F\xD6\x4C\x24\x10",     // movq [rsp+16], xmm1
		"\x66\x0F\xD6\x54\x24\x18",     // movq [rsp+24], xmm2
		"\x66\x0F\xD6\x5C\x24\x20"      // movq [rsp+32], xmm3
	};
	for (int i = 0; i < 4 && sizer.p < sig.sigEnd(); ++i, sizer.NextArg())
	{
		if (*sizer.p == '%')
			++sizer.p;

		switch (*sizer.p)
		{
		case 'f':
		case 'd':
			// Double or Float.  The value for this argument is in XMMn.
			memcpy(addr + ofs, fpRegs[i], 6);
			ofs += 6;
			break;

		default:
			// Anything else is in [RCX, RDX, R8, R8][n]
			memcpy(addr + ofs, intRegs[i], 5);
			ofs += 5;
			break;
		}
	}

	// Determine which type of jump to use.  If the target address (DllImportCallback)
	// is within INT32_MIN..INT32_MAX of the next instruction's address, store
	INT_PTR iRelJmp = iGlueAddr - (iAddr + ofs + 5);
	if (iRelJmp >= INT32_MIN && iRelJmp <= INT32_MAX)
	{
		// jump is within INT32 offset - use a RIP-relative immediate jump
		addr[ofs++] = 0xE9;   // jmp $+<immediate32>
		Put4(addr + ofs, iRelJmp);
	}
	else
	{
		// jump exceeds 32 bits - jump indirect through R10
		addr[ofs++] = 0x49;   // movabs r10, <immediate64>
		addr[ofs++] = 0xba;   // "
		Put8(addr + ofs, iGlueAddr);
		ofs += 8;

		addr[ofs++] = 0x41;   // jmp r10
		addr[ofs++] = 0xff;   // "
		addr[ofs++] = 0xe2;   // "
	}

#else
#error Processor architecture not supported.  Add the appropriate code here to build for this target
#endif

	// return the function pointer
	return reinterpret_cast<FARPROC>(addr);
}

// native argv marshaller for callbacks
class JavascriptEngine::MarshallFromNativeArgv : public Marshaller
{
public:
	MarshallFromNativeArgv(SigParser *sig, void *argv, JsValueRef *jsArgv) :
		Marshaller(sig), argv(static_cast<arg_t*>(argv)), jsArgv(jsArgv)
	{
		// store the implied 'this' argument in the first slot
		jsArgv[0] = inst->undefVal;
		jsArgCur = 1;

		// start at the first argumnet
		curArg = this->argv;
	}

	virtual bool Marshall() override
	{
		// skip the return value type
		NextArg();

		// process each argument from the native vector
		for (const WCHAR *end = sig->sigEnd(); p < end ; NextArg())
		{
			if (*p == '%')
				++p;

			// structs/unions require special handling
			if (*p == '{')
			{
				if (p[1] == 'S')
					DoStruct();
				else if (p[1] == 'U')
					DoUnion();
				else if (p[1] == 'I')
					DoInterface();
				else
					Error(MsgFmt(_T("dllImport: internal error: invalid composite type '%c' in signature %.*s"), 
						p[1], static_cast<int>(sig->sig.length()), sig->sig.data()));
			}
			else
			{
				// marshall one value
				SigParser subsig(p, EndOfArg());
				MarshallFromNativeValue mv(&subsig, curArg);
				mv.MarshallValue();

				// store it in the javascript argument array
				jsArgv[jsArgCur++] = mv.jsval;

				// skip the correct number of native argument slots
				switch (*p)
				{
				case 'l':
				case 'L':
				case 'd':
					// these types take 2 slots on x86
					curArg += IF_32_64(2, 1);
					break;

				default:
					// all other types take 1 slot on all platforms
					curArg += 1;
					break;
				}
			}
		}

		// success
		return true;
	}

	void DoStructOrUnion(size_t structSize)
	{
		void *structp = curArg;
		size_t stackSlotSize = structSize;

#if defined(_M_IX86)
		// On x86, a struct/union of any size can be passed inline on the stack.
		// Continue with the arg slot pointer serving as the struct pointer.

#elif defined(_M_X64)
		// On x64, only structs/unions under 8 bytes can be passed by value.
		// Anything over 8 bytes is actually passed in the stack/parameter register
		// as a pointer, even if the struct is declared to be passed by value.
		if (structSize > 8)
		{
			// the stack slot contains a pointer to the struct
			structp = *reinterpret_cast<void**>(curArg);
			stackSlotSize = sizeof(arg_t);
		}
#else
#error Processor architecture not supported.  Add the appropriate code here to build for this target
#endif
		// process the struct
		SigParser subsig(p, EndOfArg());
		MarshallFromNativeValue mv(&subsig, structp);
		mv.MarshallValue();

		// store it in the javascript argument array
		jsArgv[jsArgCur++] = mv.jsval;

		// skip arguments, rounding up to a DWORD boundary
		curArg += (stackSlotSize + sizeof(arg_t) - 1) / sizeof(arg_t);
	}

	virtual void DoStruct() override
	{
		size_t size = SizeofStruct(JS_INVALID_REFERENCE, _T("dllImport: struct type in callback cannot use indetermine array size"));
		DoStructOrUnion(size);
	}

	virtual void DoUnion() override
	{
		size_t size = SizeofUnion(JS_INVALID_REFERENCE, _T("dllImport: array type in callback cannot use indetermine array size"));
		DoStructOrUnion(size);
	}

	virtual void DoInterface() override { Error(_T("dllImport: interface cannot be passed by reference")); }

	// native arguments on stack
	arg_t *argv;

	// current native argument pointer
	arg_t *curArg;

	// Javascript argument array
	JsValueRef *jsArgv;
	int jsArgCur;
};

// marshall a Javascript callback return value to native code
class JavascriptEngine::MarshallToNativeReturn : public MarshallToNative
{
public:
	MarshallToNativeReturn(SigParser *sig, JsValueRef jsval, void *hiddenStructp) :
		MarshallToNative(sig), jsval(jsval), hiddenStructp(hiddenStructp)
	{ }

	virtual JsValueRef GetNextVal() override { return jsval; }

	virtual void *Alloc(size_t size, int nItems = 1) override 
	{
		// If there's a hidden struct pointer, use that space.  The actual
		// return value from the function is the pointer.
		if (hiddenStructp != nullptr)
		{
			retval = reinterpret_cast<UINT_PTR>(hiddenStructp);
			return hiddenStructp;
		}

		// otherwise, the result has to fit in the return register
		if (size <= sizeof(retval))
			return &retval;

		// the return value is larger than expected - allocate temp space so
		// that we don't crash, and flag it as an error
		Error(_T("dllImport: return value from Javascript callback doesn't fit in return register"));
		return inst->marshallerContext->Alloc(size);
	}

	virtual void DoArray() override
	{
		// array returns are invalid
		Error(_T("dllImport: array types is invalid as Javascript callback return"));
	}

	virtual void DoVoid() override { /* void return - we have nothing to do */ }

	// A returned BSTR is the caller's responsibility to clean up
	virtual void ScheduleBSTRCleanup(BSTR) override { }

	// Don't allow returning VARIANT values, since the cleanup
	// requirements are unclear.  COM doesn't normally use
	// VARIANT returns; when they need to be passed from callee
	// to caller, it's normally through OUT parameters instead.
	virtual void DoVariant() override { Error(_T("VARIANT cannot be used as a return type")); }

	// javascript value we're marshalling
	JsValueRef jsval;

	// Hidden return struct pointer.  If the function returns a struct or union
	// by value, the Microsoft calling conventions require the caller to allocate
	// space for the struct (typically in the caller's local stack frame) and 
	// pass the pointer to the allocated space in a hidden extra argument
	// prepended to the nominal argument list.  This is said pointer.
	void *hiddenStructp;

	// native return value
	UINT64 retval = 0;
};

// C++ entrypoint for assembler glue 
UINT64 JavascriptEngine_CallCallback(void *wrapper_, void *argv_)
{
	// downcast the arguments
	auto wrapper = static_cast<JavascriptEngine::JavascriptCallbackWrapper*>(wrapper_);
	auto argv = static_cast<JavascriptEngine::arg_t *>(argv_);

	// get the native argument count
	int argc = wrapper->argc;

	// If the caller passed us a hidden first argument containing a pointer to
	// a caller-allocated area for filling in a struct-by-value return value,
	// it doesn't count as a Javascript argument.
	void *hiddenStructp = nullptr;
	if (wrapper->hasHiddenStructArg)
	{
		// get the caller's hidden struct area
		hiddenStructp = *reinterpret_cast<void**>(argv);

		// skip it to get to the first actual input argument
		++argv;
	}

	// Allocate the argument vector, adding the implied extra argument for 'this'
	JsValueRef *jsArgv = static_cast<JsValueRef*>(alloca(sizeof(JsValueRef) * (argc + 1)));

	// marshall the arguments to javascript
	JavascriptEngine::SigParser sig(wrapper->sig);
	JavascriptEngine::MarshallFromNativeArgv m(&sig, argv, jsArgv);
	m.Marshall();

	// call the Javascript function
	JsValueRef jsResult;
	JsCallFunction(wrapper->jsFunc, jsArgv, argc + 1, &jsResult);

	// marshall the result to native code
	JavascriptEngine::MarshallToNativeReturn mr(&sig, jsResult, hiddenStructp);
	mr.MarshallValue();

	// return the marshalling result
	return mr.retval;
}

JavascriptEngine::JavascriptCallbackWrapper::JavascriptCallbackWrapper(JsValueRef jsFunc, SigParser *sigprs) :
	jsFunc(jsFunc), hasHiddenStructArg(false)
{
	// store the calling convention code - it's the first character of the function signature
	const WCHAR *sig = sigprs->sig.data(), *sigEnd = sigprs->sigEnd();
	callingConv = *sig++;

	// save the signature, minus the calling convention
	this->sig.assign(sig, sigEnd - sig);

	// Check the function signature to see if it has a large struct-by-value
	// return.  If it has a struct-by-value return with a struct over 8 bytes,
	// the caller allocates temporary space for the returned struct contents,
	// and passes us a pointer to it in a hidden, extra first argument.  That
	// argument counts in the native stack but doesn't get passed to Javascript,
	// so we'll have to know whether it's there or not when the glue function
	// is invoked.
	if (*sig == '@')
	{
		// expand the type reference
		std::wstring_view refsig;
		if (inst->LookUpNativeType(sig + 1, Marshaller::EndOfArg(sig + 1, sigEnd) - (sig + 1), refsig))
		{
			if (refsig[1] == 'S')
			{
				SigParser subsig(refsig.data() + 3, refsig.data() + refsig.length() - 1);
				MarshallStructSizer ss(&subsig, JS_INVALID_REFERENCE);
				ss.Marshall();
				if (ss.size > 8)
					hasHiddenStructArg = true;
			}
			else if (sig[1] == 'U')
			{
				SigParser subsig(refsig.data() + 3, refsig.data() + refsig.length() - 1);
				MarshallUnionSizer ss(&subsig, JS_INVALID_REFERENCE);
				ss.Marshall();
				if (ss.size > 8)
					hasHiddenStructArg = true;
			}
		}
	}

	// set up a basic marshaller just to count the arguments
	SigParser argsig(sig, sigEnd);
	MarshallBasicSizer sizer(&argsig, JS_INVALID_REFERENCE);

	// skip the return value argument
	sizer.NextArg();

	// count arguments
	for (argc = 0; sizer.p < sizer.sig->sigEnd(); sizer.NextArg(), ++argc);

	// Create a thunk.  Do this after parsing the arguments, since the thunk
	// generator might take special action based on the argument types.
	thunk = inst->codeGenManager.Generate(this);
	if (thunk == nullptr)
	{
		inst->Throw(_T("dllImport: unable to create thunk for Javascript callback"));
		return;
	}
}

JavascriptEngine::JavascriptCallbackWrapper::~JavascriptCallbackWrapper()
{
	// delete our thunk by adding it to the free list
	if (thunk != nullptr)
		inst->codeGenManager.Recycle(thunk);
}

// -----------------------------------------------------------------------
//
// Native objects
//

template<typename T>
JsValueRef JavascriptEngine::CreateNativeObject(SigParser *sig, void *data, T **pCreatedObj)
{
	// no object created yet
	if (pCreatedObj != nullptr)
		*pCreatedObj = nullptr;

	JsErrorCode err;
	auto Error = [this](const TCHAR *msg)
	{
		Throw(msg);
		return JS_INVALID_REFERENCE;
	};

	// check if it's a COM interface
	const WCHAR *p = sig->sig.data();
	bool isCOM = (p[0] == '*' && p[1] == '@' && p[2] == 'I') || (p[0] == '@' && p[1] == 'I');

	// get the size of the object	if (!isCOM)
	MarshallBasicSizer sizer(sig, JS_INVALID_REFERENCE);
	if (!sizer.Marshall() || sizer.error)
		return JS_INVALID_REFERENCE;

	// Zero-size objects (e.g., void or void[5]) are invalid
	if (sizer.size == 0 && !isCOM)
		return Error(_T("dllImport: creating native object: can't create type with zero size"));

	// Cache based on the type signature
	WSTRING cacheKey(sig->sig);

	// Look up the prototype object for the native type signature in
	// our type cache.  If we've encountered this same type before,
	// we can use the same prototype for it again; otherwise we'll
	// have to create a new prototype for a data view object for this
	// type signature.
	NativeTypeCacheEntry *entry = nullptr;
	if (auto it = nativeTypeCache.find(cacheKey); it != nativeTypeCache.end())
	{
		// got it - reuse the existing entry
		entry = &it->second;
	}
	else
	{
		// There's no entry for this type, so create one.  First, create
		// the Javascript object to serve as the prototype for the external
		// view object for this type.
		JsValueRef proto = JS_INVALID_REFERENCE;
		if ((err = JsCreateObject(&proto)) != JsNoError)
			return Throw(err, _T("dllImport: creating prototype for native data view object"));

		// if it's a COM object, set the prototype's prototype to COMObject
		if (isCOM && (err = JsSetPrototype(proto, COMPointer_proto)) != JsNoError)
			return Throw(err, _T("dllImport: setting COMPointer prototype"));

		// create the cache entry
		entry = &nativeTypeCache.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(cacheKey),
			std::forward_as_tuple(proto)).first->second;

		// add getters and setters
		InitNativeObjectProto(entry, sig);
	}

	// create the appropriate native wrapper
	if (isCOM)
	{
		// COM interface pointer
		COMImportData **n = nullptr;
		if constexpr (std::is_same<T, COMImportData>::value) n = pCreatedObj;
		SigParser ifcSig(sig->sig);
		IUnknown *pUnknown = data == nullptr ? nullptr :
			p[0] == '*' ? *static_cast<IUnknown**>(data) : 
			static_cast<IUnknown*>(data);
		return COMImportData::Create(n, entry->proto, pUnknown, &ifcSig);
	}
	else
	{
		// For all other types, create a NativeTypeWrapper for the object
		NativeTypeWrapper **n = nullptr;
		if constexpr (std::is_same<T, NativeTypeWrapper>::value) n = pCreatedObj;
		return NativeTypeWrapper::Create(n, entry->proto, sig, sizer.size, data);
	}
}

// Add a getter, setter, and (optionally) valueOf to a native type view object
template<typename ViewType>
void JavascriptEngine::AddToNativeTypeView(NativeTypeCacheEntry *entry, const WCHAR *name, ViewType *view,
	bool hasValueOf, bool hasSetter)
{
	// add the native type view to the list for this entry
	entry->views.emplace_back(view);

	// make a string for the name
	JsValueRef nameStr;
	JsErrorCode err;
	if ((err = JsPointerToString(name, wcslen(name), &nameStr)) != JsNoError)
	{
		Throw(err, _T("dllImport: creating getter/setter for native object"));
		return;
	}

	// set up the getter
	JsValueRef desc;
	JsValueRef propstr;
	JsValueRef trueval;
	JsValueRef funcval = JS_INVALID_REFERENCE;
	if ((err = JsCreateObject(&desc)) == JsNoError
		&& (err = JsGetTrueValue(&trueval)) == JsNoError
		&& (err = JsCreateString("enumerable", 10, &propstr)) == JsNoError
		&& (err = JsObjectSetProperty(desc, propstr, trueval, true)) == JsNoError
		&& (err = JsCreateString("get", 3, &propstr)) == JsNoError
		&& (err = JsCreateFunction(&ViewType::Getter, view, &funcval)) == JsNoError)
		err = JsObjectSetProperty(desc, propstr, funcval, true);

	// If appropriate, add valueOf and toString, for implicit conversions of
	// the object to a Javascript value.  This applies to scalar types, not
	// structs or arrays.
	if (hasValueOf && err == JsNoError
		&& (err = JsCreateString("valueOf", 7, &propstr)) == JsNoError
		&& (err = JsObjectSetProperty(entry->proto, propstr, funcval, true)) == JsNoError
		&& (err = JsCreateString("toString", 8, &propstr)) == JsNoError
		&& (err = JsCreateFunction(&ViewType::ToString, view, &funcval)) == JsNoError)
		err = JsObjectSetProperty(entry->proto, propstr, funcval, true);

	// if there's a setter, add that as well
	if (hasSetter && err == JsNoError
		&& (err = JsCreateString("set", 3, &propstr)) == JsNoError
		&& (err = JsCreateFunction(&ViewType::Setter, view, &funcval)) == JsNoError)
		err = JsObjectSetProperty(desc, propstr, funcval, true);

	// define the getter/setter
	bool ok;
	if (err == JsNoError)
		err = JsObjectDefineProperty(entry->proto, nameStr, desc, &ok);

	// throw on any error
	if (err != JsNoError)
		Throw(err, _T("dllImport: creating getter/setter for native object"));
}


void JavascriptEngine::InitNativeObjectProto(NativeTypeCacheEntry *entry, SigParser *sigprs)
{
	// get the native signature
	const WCHAR *p = sigprs->sig.data();
	const WCHAR *endp = sigprs->sigEnd();

	// check for const qualification
	bool isConst = false;
	if (*p == '%')
	{
		isConst = true;
		++p;
	}

	// if it's a reference type, descend into the referenced typed
	if (p < endp && *p == '@')
	{
		++p;
		std::wstring_view reftype;
		if (!LookUpNativeType(p, endp - p, reftype))
			return;

		p = reftype.data();
		endp = p + reftype.length();
	}

	// service routine to add the getter/setter property
	auto AddGetterSetter = [this, entry, isConst, sigprs](size_t offset, const WCHAR *name, const WCHAR *sig, size_t siglen, bool hasValueOf)
	{
		// expand references
		if (sig[0] == '@')
		{
			std::wstring_view reftype;
			if (!inst->LookUpNativeType(sig + 1, siglen - 1, reftype))
				return;

			sig = reftype.data();
			siglen = reftype.length();
		}

		switch (*sig)
		{
		case 'b':
			AddToNativeTypeView(entry, name, new PrimitiveNativeTypeView<bool>(offset), hasValueOf, !isConst);
			break;

		case 'c':
			AddToNativeTypeView(entry, name, new PrimitiveNativeTypeView<INT8>(offset), hasValueOf, !isConst);
			break;

		case 'C':
			AddToNativeTypeView(entry, name, new PrimitiveNativeTypeView<UINT8>(offset), hasValueOf, !isConst);
			break;

		case 's':
			AddToNativeTypeView(entry, name, new PrimitiveNativeTypeView<INT16>(offset), hasValueOf, !isConst);
			break;

		case 'S':
			AddToNativeTypeView(entry, name, new PrimitiveNativeTypeView<UINT16>(offset), hasValueOf, !isConst);
			break;

		case 'i':
			AddToNativeTypeView(entry, name, new PrimitiveNativeTypeView<INT32>(offset), hasValueOf, !isConst);
			break;

		case 'I':
			AddToNativeTypeView(entry, name, new PrimitiveNativeTypeView<UINT32>(offset), hasValueOf, !isConst);
			break;

		case 'f':
			AddToNativeTypeView(entry, name, new PrimitiveNativeTypeView<float>(offset), hasValueOf, !isConst);
			break;

		case 'd':
			AddToNativeTypeView(entry, name, new PrimitiveNativeTypeView<double>(offset), hasValueOf, !isConst);
			break;

		case 'l':
			AddToNativeTypeView(entry, name, new Int64NativeTypeView<INT64, INT64>(offset), hasValueOf, !isConst);
			break;

		case 'L':
			AddToNativeTypeView(entry, name, new Int64NativeTypeView<UINT64, INT64>(offset), hasValueOf, !isConst);
			break;


			// For INT_PTR and SIZE_T types, use the Int64 viewer, but
			// adapted to the corresponding native type.  That will provide
			// uniform Javascript semantics across platforms.  Note that
			// even though these are nominally "int64" viewers, they'll
			// follow the templates and actually use int32 types on x86.
		case 'z':
			AddToNativeTypeView(entry, name, new Int64NativeTypeView<SSIZE_T, INT64>(offset), hasValueOf, !isConst);
			break;

		case 'Z':
			AddToNativeTypeView(entry, name, new Int64NativeTypeView<SIZE_T, UINT64>(offset), hasValueOf, !isConst);
			break;

		case 'p':
			AddToNativeTypeView(entry, name, new Int64NativeTypeView<INT_PTR, INT64>(offset), hasValueOf, !isConst);
			break;

		case 'P':
			AddToNativeTypeView(entry, name, new Int64NativeTypeView<UINT_PTR, UINT64>(offset), hasValueOf, !isConst);
			break;

		case 'H':
			AddToNativeTypeView(entry, name, new HandleNativeTypeView(offset), hasValueOf, !isConst);
			break;

		case 'h':
			AddToNativeTypeView(entry, name, new HWNDNativeTypeView(offset), hasValueOf, !isConst);
			break;

		case 'B':
			AddToNativeTypeView(entry, name, new BSTRNativeTypeView(offset), hasValueOf, !isConst);
			break;

		case 'V':
			AddToNativeTypeView(entry, name, new VariantNativeTypeView(offset), hasValueOf, !isConst);
			break;

		case 't':
		case 'T':
			// create a pointer to the underlying type (CHAR or const CHAR), but record
			// the original string type code as well, so that the pointer view can be
			// smart about conversions to and from Javascript strings
			{
				// figure the underlying pointer type
				const WCHAR *ptrsig = (*sig == 't' ?
					(isConst ? L"%c" : L"c") :
					(isConst ? L"%S" : L"S"));

				// add the pointer view
				SigParser subsig(ptrsig, wcslen(ptrsig));
				AddToNativeTypeView(entry, name, new PointerNativeTypeView(offset, &subsig, *sig), hasValueOf, !isConst);
			}
			break;

		case '{':
		case '[':
			// Add a nested type viewer.  These don't have setters, as they're
			// structural elements of the enclosing type.
			{
				SigParser subsig(sig, siglen);
				AddToNativeTypeView(entry, name, new NestedNativeTypeView(offset, &subsig), false, !isConst);
			}
			break;

		case '*':
		case '&':
			// Pointer/ref
			{
				// check if it's a COM interface pointer
				bool isCom = (sig[1] == '@' && sig[2] == 'I');

				// Pointer to other type.  Add a getter/setter of the given name, using
				// a PointerNativeTypeView of the underlying pointer.
				SigParser subsig(sig + 1, Marshaller::EndOfArg(sig, sig + siglen));
				AddToNativeTypeView(entry, name, new PointerNativeTypeView(offset, &subsig, 0), hasValueOf && !isCom, !isConst);
			}
			break;
		}
	};

	// check the native value type
	switch (auto curType = *p)
	{
	case '[':
		// Array type.  An array doesn't have any getter/setters; instead, it has
		// an .at(index) method that returns a view of the element at the index.
		{
			// get the first index value
			int dim;
			bool empty;
			if (!Marshaller::ParseArrayDim(p, endp, dim, empty))
			{
				Throw(_T("dllImport: invalid array dimension in native type view"));
				break;
			}
			if (empty)
			{
				Throw(_T("dllImport: unspecified array dimension not allowed in native type view"));
				break;
			}

			// figure the size of the underlying type
			SigParser subsig(p, endp);
			MarshallBasicSizer sizer(&subsig, JS_INVALID_REFERENCE);
			sizer.MarshallValue();

			// Add a read-only length property with the array length
			JsErrorCode err;
			JsValueRef propval;
			const TCHAR *where = _T("JsDoubleToNumber");
			if ((err = JsDoubleToNumber(static_cast<double>(dim), &propval)) != JsNoError
				|| (err = SetReadonlyProp(entry->proto, "length", propval, where)) != JsNoError)
				Throw(err, MsgFmt(_T("dllImport: creating .length method for native array type: %s"), where));

			// Add getter/setter properties for [0], [1], [2], etc.  This will at
			// least make it look superficially like an array.
			size_t eleOffset = 0;
			for (int i = 0 ; i < dim; ++i, eleOffset += sizer.size)
			{
				TCHAR iAsStr[32];
				IF_32_64(_itow_s(i, iAsStr, 10), _ui64tow_s(i, iAsStr, countof(iAsStr), 10));
				AddGetterSetter(eleOffset, iAsStr, p, endp - p, false);
			}
		}
		break;

	case '{':
		// Composite type
		if (p[1] == 'S' || p[1] == 'U')
		{
			// Struct or union type.  The prototype has a getter and setter for
			// each element, with the same name as the struct element.
			auto Parse = [this, p, endp, &AddGetterSetter](MarshallStructOrUnionSizer &sizer)
			{
				sizer.MarshallStructMembers([this, &sizer, &AddGetterSetter](WSTRING &memberName, WSTRING &memberSig)
				{
					// marshall the value
					sizer.MarshallValue();

					// add a getter and setter for this type
					AddGetterSetter(sizer.lastItemOfs, memberName.c_str(), memberSig.c_str(), memberSig.length(), false);

					// continue the iteration
					return true;
				});
			};
			SigParser subsig(p + 3, endp - 1);
			if (p + 1 < endp && p[1] == 'U')
				Parse(MarshallUnionSizer(&subsig, JS_INVALID_REFERENCE));
			else
				Parse(MarshallStructSizer(&subsig, JS_INVALID_REFERENCE));
		}
		else if (p[1] == 'I')
		{
			// COM interface.  Treat this the same as a pointer to a COM interface, 
			// as "COM interface by value" isn't meaningful.
			COMImportData::CreatePrototype(entry->proto, p, Marshaller::EndOfArg(p, endp));
		}
		else
			Throw(MsgFmt(_T("dllImport: native object prototype setup: invalid composite type code '%c'"), p[1]));
		break;

	case 'b':
	case 'c':
	case 'C':
	case 's':
	case 'S':
	case 'i':
	case 'I':
	case 'd':
	case 'f':
	case 'l':
	case 'L':
	case 'z':
	case 'Z':
	case 'p':
	case 'P':
	case 'H':
	case 'h':
	case 'V':
	case 'B':
		// The whole native object is a primitive scalar type.  Add a getter/setter
		// for "value".  Also add the same getter as the valueOf() method.  Since
		// this is the only value in the object, it's at offset zero.
		AddGetterSetter(0, L"value", p, Marshaller::EndOfArg(p, endp) - p, true);
		break;

	case '*':
	case '&':
		// Check what we're pointing at
		if (curType == '*' && p[1] == '@' && p[2] == 'I')
		{
			// Pointer to COM interface type.  This use a prototype with properties
			// named for the interface members, each with a Javascript function that
			// invokes the native function via the normal JS-to-native marshalling.
			COMImportData::CreatePrototype(entry->proto, p + 1, Marshaller::EndOfArg(p + 1, endp));
		}
		else
		{
			// A pointer to anything else is a scalar value containing the pointer.
			// The 'value' yields a NativePointer object containing a dereferencable
			// version of the pointer.
			AddGetterSetter(0, L"value", p, Marshaller::EndOfArg(p, endp) - p, true);
		}
		break;

	case 't':
	case 'T':
		// String pointers are scalars with .value getter/setters
		AddGetterSetter(0, L"value", p, Marshaller::EndOfArg(p, endp) - p, true);
		break;

	case 'v':
		// Void can't be used for in a data view
		Throw(_T("dllImport: a native type view can't be created for VOID data"));
		break;

	default:
		// other types are invalid
		Throw(MsgFmt(_T("dllImport: native object prototype setup: invalid native type code '%c'"), *p));
		break;
	}
}

bool JavascriptEngine::IsPointerType(const WCHAR *sig)
{
	if (*sig == '%')
		++sig;
	return (*sig == '*');
}

bool JavascriptEngine::IsArrayType(const WCHAR *sig)
{
	if (*sig == '%')
		++sig;
	return (*sig == '[');
}

const WCHAR *JavascriptEngine::SkipPointerOrArrayQual(const WCHAR *sig)
{
	if (*sig == '*')
		return sig + 1;

	if (*sig == '[')
	{
		const WCHAR *p;
		for (p = sig + 1; *p != 0 && *p != ']'; ++p);
		if (*p == ']')
			return p + 1;
	}

	return sig;
}

bool JavascriptEngine::IsPointerConversionValid(SigParser *fromSig, SigParser *toSig)
{
	const WCHAR *from = fromSig->sig.data(), *fromEnd = from + fromSig->sig.length();
	const WCHAR *to = toSig->sig.data(), *toEnd = to + toSig->sig.length();

	// scan pointer qualifiers
	while (from < fromEnd && to < toEnd)
	{
		// check for const qualification on the 'from' type
		if (*from == '%')
		{
			// we can't remove const qualification with a cast
			if (*to != '%')
				return false;

			// matching const qualifiers; skip the '%' in both types so that
			// we can compare the underlying types
			++from;
			++to;
		}

		// a cast that adds const qualification is legal, so if the 'to'
		// type is const-qualified, skip the qualifier and compare the rest
		// according to the underlying type
		if (to < toEnd && *to == '%')
			++to;

		// if we have two pointers at this point, remove this level of
		// pointer qualification and proceed to the underlying types
		if (to < toEnd && *to == '*' && from < fromEnd && *from == '*')
		{
			++to;
			++from;
		}
		else
			break;
	}

	// A cast from anything to or from void* is legal.  Note that this
	// is less restrictive than the standard C rules: in standard C, a
	// cast-less conversion from any pointer type to void* is valid, but
	// the reverse isn't true: a cast form void* to another pointer type
	// requires an explicit cast.  It would be cumbersome to represent
	// explicit casting operators in Javascript, though, so we instead
	// use void* to allow a cast to anything: if you want to perform an
	// otherwise incompatible conversion, assign the source pointer to
	// a void* variable, and assign void* to your destination pointer.
	//
	// This is also less restrictive than standard C in another subtler
	// way.  In standard C, the ability to cast from T* to void* for any
	// type T only goes one pointer level deep: conversion from T** to 
	// void** isn't allowed without a cast.  That's not just for the sake
	// of fastidiousness; there exist actual platforms where pointers to 
	// different types have different representations, which makes it
	// impossible for the compiler to correctly generate code in general
	// for a cast from T** to void**.  However, on all Windows hardware,
	// pointer representations are in fact identical across all types,
	// so double- (or N-)indirect casts are fine at the hardware level.
	// They're still usually dubious at the program logic level, of
	// course, but they're nonetheless required in practice for at
	// least one common Windows programming situation: COM.  Many COM
	// calls, including CoCreateInstance() and the ubiquitous
	// IUnknown::QueryInterface(), use void** arguments to represent
	// OUT parameters that are populated by the callee with interface
	// pointers of types that vary at run-time.  Microsoft resorted to
	// this because there was no type-safe alternative in C.  It would
	// have been possible to come up with something more type-safe in 
	// C++, but COM was specifically designed to be language-neutral,
	// which would have ruled out anything like C++ RTTI or templates.
	if (*to == 'v' || *from == 'v')
		return true;

	// casts between identical pointer types are legal
	if (std::wstring_view(from, fromEnd - from) == std::wstring_view(to, toEnd - to))
		return true;

	// A cast to a smaller array of the same type is legal
	if (from < fromEnd && from[0] == '[' && to < toEnd && to[0] == '[')
	{
		// read the two array dimensions
		int fromDim, toDim;
		bool fromEmpty, toEmpty;
		const WCHAR *pfrom = from, *pto = to;
		if (Marshaller::ParseArrayDim(pfrom, fromEnd, fromDim, fromEmpty)
			&& Marshaller::ParseArrayDim(pto, toEnd, toDim, toEmpty)
			&& toDim <= fromDim)
		{
			// The dimensions are compatible; allow it if the types are identical.
			// Note that the types have to be identical, not just compatible, since
			// the element types have to be the same for the pointer arithmetic
			// within the new array to work properly.
			if (std::wstring_view(pfrom, fromEnd - pfrom) == std::wstring_view(pto, toEnd - pto))
				return true;
		}
	}

	// A cast from an array to a single element of the same type is legal
	if (from < fromEnd && from[0] == '[')
	{
		// skip the array specifier in the 'from'
		const WCHAR *pfrom = from;
		for (; pfrom < fromEnd && *pfrom != ']'; ++pfrom);
		if (pfrom < fromEnd && *pfrom == ']' && std::wstring_view(pfrom + 1, fromEnd - pfrom - 1) == std::wstring_view(to, toEnd - to))
			return true;
	}

	// other conversions are illegal
	return false;
}

// -----------------------------------------------------------------------
//
// Scalar native type view.  This provides a view on a simple scalar type
// (int, float, etc) within a native data object.
//

JsValueRef CALLBACK JavascriptEngine::ScalarNativeTypeView::Getter(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
{
	// recover the native object from 'this'
	JsValueRef jsval;
	if (auto obj = NativeTypeWrapper::Recover<NativeTypeWrapper>(argv[0], _T("dllImport: data object view: primitive data getter")); obj != nullptr)
	{
		// get the data view definition for this getter from the context
		auto view = static_cast<const ScalarNativeTypeView*>(ctx);

		// do the type-specific conversion
		if (view->Get(argv[0], obj->data + view->offset, &jsval) == JsNoError)
			return jsval;
	}

	// failed - return undefined
	JsGetUndefinedValue(&jsval);
	return jsval;
}

JsValueRef CALLBACK JavascriptEngine::ScalarNativeTypeView::ToString(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
{
	// recover the native object from 'this'
	JsValueRef jsval;
	if (auto obj = NativeTypeWrapper::Recover<NativeTypeWrapper>(argv[0], _T("dllImport: data object view: primitive data getter")); obj != nullptr)
	{
		// get the data view definition for this getter from the context
		auto view = static_cast<const ScalarNativeTypeView*>(ctx);

		// do the type-specific conversion
		if (view->Get(argv[0], obj->data + view->offset, &jsval) == JsNoError)
		{
			// now try calling the native toString() on the result
			JsPropertyIdRef propid;
			JsValueRef objval, toStringFunc;
			if (JsCreatePropertyId("toString", 8, &propid) == JsNoError
				&& JsConvertValueToObject(jsval, &objval) == JsNoError
				&& JsGetProperty(objval, propid, &toStringFunc) == JsNoError)
			{
				// if there's an argument to our toString, pass it to the value toString
				JsValueRef tsargv[2] = { jsval, JS_INVALID_REFERENCE };
				unsigned short tsargc = 1;
				if (argc >= 2)
					tsargv[tsargc++] = argv[1];

				// call toString
				if (JsCallFunction(toStringFunc, tsargv, tsargc, &jsval) == JsNoError)
					return jsval;
			}
		}
	}

	const char str[] = "[Native Type]";
	JsCreateString(str, countof(str) - 1, &jsval);
	return jsval;
}

JsValueRef CALLBACK JavascriptEngine::ScalarNativeTypeView::Setter(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
{
	// make sure we have a value to set
	if (argc < 2)
		return JavascriptEngine::ThrowSimple("Setting: missing value");

	// recover the native object from 'this'
	if (auto obj = NativeTypeWrapper::Recover<NativeTypeWrapper>(argv[0], _T("dllImport: native object view: primitive data setter")); obj != nullptr)
	{
		// get the data view definition for this setter, from the context
		auto view = static_cast<const ScalarNativeTypeView*>(ctx);

		// call the virtual setter
		view->Set(argv[0], obj->data + view->offset, argv[1]);
	}

	// return the value assigned as the result
	return argv[1];
}

// -----------------------------------------------------------------------
//
// Pointer native type view.  This provides a type view on a pointer
// type (int*, etc) in a native data object.
//

JavascriptEngine::PointerNativeTypeView::PointerNativeTypeView(size_t offset, SigParser *sig, WCHAR stringType) :
	ScalarNativeTypeView(offset), 
	sig(sig->sig), stringType(stringType)
{
	// figure the size of the underlying type
	MarshallBasicSizer sizer(sig, JS_INVALID_REFERENCE);
	sizer.MarshallValue();
	this->size = sizer.size;
}

JsErrorCode JavascriptEngine::PointerNativeTypeView::Get(JsValueRef self, void *nativep, JsValueRef *jsval) const
{
	// create the NativePointer javascript object to represent the pointer
	SigParser sigprs(sig);
	return TryGet(&sigprs, nativep, jsval);
}

JsErrorCode JavascriptEngine::PointerNativeTypeView::TryGet(SigParser *sig, void *nativep, JsValueRef *jsval) const
{
	__try
	{
		if (sig->sig[0] == '@' && sig->sig[1] == 'I')
		{
			// COM interface pointer.  Create a COMPointer object.
			*jsval = inst->CreateNativeObject(sig, *reinterpret_cast<IUnknown**>(nativep));
			return JsNoError;
		}
		else
		{
			// non-COM type - return a NativePointer object
			void *ptr = *reinterpret_cast<void* const*>(nativep);
			return NativePointerData::Create(ptr, size, sig, stringType, jsval);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		ThrowSimple("Bad native pointer dereference: memory location is invalid or inaccessible");
		return JsNoError;
	}
}

JsErrorCode JavascriptEngine::PointerNativeTypeView::Set(JsValueRef self, void *nativep, JsValueRef jsval) const
{
	auto Apply = [this, nativep](void *newPointerVal)
	{
		// check what we're pointing at
		if (sig[0] == '@' && sig[1] == 'I')
		{
			// COM interface pointer.  Add a reference on the new pointer to
			// reflect the reference we're creating by copying the pointer value 
			// into this new location.  Note that we must always count a new
			// reference before releasing an old one in a situation like this,
			// to account for the situation where the new and old pointer are
			// the same (as releasing first in this case could incorrectly let
			// the ref count reach zero and delete the underlying object, even
			// though we have two live pointers to it).
			IUnknown *pNewUnk = reinterpret_cast<IUnknown*>(newPointerVal);
			if (pNewUnk != nullptr)
				pNewUnk->AddRef();

			// now release the old object whose reference we're about to overwrite
			IUnknown **ppDestUnk = reinterpret_cast<IUnknown**>(nativep);
			if (*ppDestUnk != nullptr)
				(*ppDestUnk)->Release();

			// move the new interface pointer into the target slot
			*ppDestUnk = pNewUnk;
		}
		else
		{
			// other - just set the new pointer value
			*reinterpret_cast<void**>(nativep) = newPointerVal;
		}

		// success
		return JsNoError;
	};

	// Javascript null or undefined counts as a null pointer
	if (jsval == inst->nullVal || jsval == inst->undefVal)
		return Apply(nullptr);

	// set up a signature parser for my reference element signature
	SigParser toEle(sig);
		
	// if the value is another native pointer object, use its pointer value
	if (auto ptr = NativePointerData::Recover<NativePointerData>(jsval, nullptr); ptr != nullptr)
	{
		// Make sure the other pointer conversion is legal
		SigParser fromEle(ptr->sig);
		if (!IsPointerConversionValid(&fromEle, &toEle))
		{
			inst->Throw(_T("Incompatible pointer type conversion; assign through a void* to override type checking"));
			return JsErrorInvalidArgument;
		}

		// looks good - set the pointer
		return Apply(ptr->ptr);
	}

	// if the value is a native object of the same type as the pointer referent,
	// store the address of the native object in the pointer
	if (auto obj = NativeTypeWrapper::Recover<NativeTypeWrapper>(jsval, nullptr); obj != nullptr)
	{
		// if it's a native object of the pointer referent type, use its address
		SigParser fromEle(obj->sig);
		if (!IsPointerConversionValid(&fromEle, &toEle))
		{
			inst->Throw(_T("Incompatible pointer type conversion; assign through a void* to override type checking"));
			return JsErrorInvalidArgument;
		}

		// set the pointer
		return Apply(obj->data);
	}

	inst->Throw(_T("Invalid type for pointer assignment"));
	return JsErrorInvalidArgument;
}

// -----------------------------------------------------------------------
//
// Nested native type view.  This represents a compositive object within
// a composite object, such as a struct within a struct, an array within
// a struct, or an element of an array of structs.
//
// This type view obviously only has a getter.  You can't set the nested
// object wholesale; you set the fields of the nested object by using the
// getter to get a NativeObject that views the nested object, then using
// its setters to set the individual fields therein.
//

JavascriptEngine::NestedNativeTypeView::NestedNativeTypeView(size_t offset, SigParser *sig) :
	NativeTypeView(offset), 
	sig(sig->sig)
{
}

JsValueRef CALLBACK JavascriptEngine::NestedNativeTypeView::Getter(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
{
	// recover the native object from 'this'
	JsValueRef jsval;
	if (auto obj = NativeTypeWrapper::Recover<NativeTypeWrapper>(argv[0], _T("dllImport: data object view: nested type getter")); obj != nullptr)
	{
		// get the data view definition for this getter from the context
		auto view = static_cast<const NestedNativeTypeView*>(ctx);

		// create a native data viewer for the element data
		SigParser sig(view->sig);
		return inst->CreateNativeObject(&sig, obj->data + view->offset);
	}

	// failed - return undefined
	JsGetUndefinedValue(&jsval);
	return jsval;
}

// -----------------------------------------------------------------------
//
// NativeObject type
//

JsValueRef JavascriptEngine::NativeTypeWrapper::Create(
	NativeTypeWrapper **pCreatedObject, JsValueRef proto, SigParser *sig, size_t size, void *extData)
{
	// create the wrapper object
	auto *wrapper = new NativeTypeWrapper(sig, size, extData);

	// create a Javascript external object for our wrapper
	JsValueRef jsobj;
	JsErrorCode err;
	if ((err = CreateExternalObjectWithPrototype(jsobj, proto, wrapper)) != JsNoError)
		return inst->Throw(err, _T("dllImport: creating external object for native data"));

	// pass it back to the caller if desired
	if (pCreatedObject != nullptr)
		*pCreatedObject = wrapper;

	// return the object
	return jsobj;
}

void JavascriptEngine::NativeTypeWrapper::InitCbSize(SigParser *sig, BYTE *data, size_t mainStructSize)
{
	const WCHAR *p = sig->sig.data(), *end = sig->sigEnd();
	if (p + 2 < end && p[0] == '{' && p[1] == 'S')
	{
		// set up a sizer
		SigParser sigprs(p + 3, end - 1);
		JavascriptEngine::MarshallStructSizer sizer(&sigprs, JS_INVALID_REFERENCE);

		// if we don't know the overall size yet, figure it
		if (mainStructSize == 0)
			mainStructSize = sizer.SizeofStruct(JS_INVALID_REFERENCE, nullptr);

		// search for a cbSize field
		while (sizer.p < sigprs.sigEnd())
		{
			// marshall the next item to get its name and offset
			sizer.MarshallValue();
			const WSTRING &t = sizer.curPropType;

			// check for a cbSize field or a nested struct
			if (sizer.curProp == L"cbSize")
			{
				// only consider 16-, 32-, and 64-bit int types
				switch (t[0])
				{
				case 's':
				case 'S':
					*reinterpret_cast<UINT16*>(data + sizer.lastItemOfs) = static_cast<UINT16>(mainStructSize);
					break;

				case 'i':
				case 'I':
					*reinterpret_cast<UINT32*>(data + sizer.lastItemOfs) = static_cast<UINT32>(mainStructSize);
					break;

				case 'l':
				case 'L':
					*reinterpret_cast<UINT64*>(data + sizer.lastItemOfs) = static_cast<UINT64>(mainStructSize);
					break;
				}
			}
			else if (t[0] == '{' && t[1] == 'S')
			{
				// sub-struct: visit it
				SigParser subsig(t);
				InitCbSize(&subsig, data + sizer.lastItemOfs, mainStructSize);
			}
		}
	}
}

JavascriptEngine::NativeTypeWrapper::NativeTypeWrapper(SigParser *sig, size_t size, void *extData) :
	sig(sig->sig), size(size)
{
	// If the caller didn't provide an external data for us to view, create
	// our own internal data.
	if (extData == nullptr)
	{
		// allocate space
		data = new BYTE[size];
		ZeroMemory(data, size);
		isInternalData = true;


		// Add it to the collection of native objects managed from Javascript
		// native type wrappers.  This allows our dead object scanner to recognize
		// pointers from this object to other wrapped native objects, so that we
		// can determine when it's safe to delete the underlying storage.
		inst->nativeDataMap.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(data),
			std::forward_as_tuple(data, size, this->sig));
	}
	else
	{
		// we're using the caller's data buffer
		data = static_cast<BYTE*>(extData);
		isInternalData = false;
	}
}

JavascriptEngine::NativeTypeWrapper::~NativeTypeWrapper()
{
	// If we allocated our own internal data, mark it as orphaned and set up
	// a scan for dead objects.
	if (isInternalData)
	{
		// Mark this object as no longer referenced from its wrapper.  This makes
		// it an orphaned object.  We can't delete it immediately, because it might
		// still be referenced by pointers in other native objects with live
		// Javascript wrapper objects.  But it's no longer *directly* reachable
		// from Javascript; it can only be reached through native pointer now.
		auto it = inst->nativeDataMap.find(data);
		if (it != inst->nativeDataMap.end())
		{
			// found it - mark is as orphaned
			it->second.isWrapperAlive = false;

			// Schedule a dead object scan.  This will scan the set of native objects
			// still reachable from Javascript native wrappers to find pointers to
			// orphaned objects.  Any orphaned objects with no inbound pointers from
			// live objects (directly or indirectly) will be deleted.
			inst->ScheduleDeadObjectScan();
		}
	}
}

JsValueRef CALLBACK JavascriptEngine::NativeTypeWrapper::AddressOf(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
{
	// one argument is required (in addition to 'this')
	JsErrorCode err;
	JsValueRef jsval = inst->undefVal;
	if (argc >= 2)
	{
		// recover the native object
		if (auto obj = Recover<NativeTypeWrapper>(argv[1], nullptr); obj != nullptr)
		{
			// get our type signature
			const WCHAR *sig = obj->sig.c_str();
			const WCHAR *sigEnd = sig + obj->sig.length();
			WSTRING eleSig = obj->sig;

			// drill down into the reference type if applicable
			if (sig[0] == '@')
			{
				// look up the reference
				std::wstring_view refsig;
				if (!inst->LookUpNativeType(obj->sig.substr(1), refsig))
					return jsval;

				// use the reference
				sig = refsig.data();
				sigEnd = sig + refsig.length();
			}

			// Assume we'll return the address at offset zero from the main object.
			// For composite objects (structs, arrays), we can get element offsets
			// via the second argument.
			size_t offset = 0;
			size_t size = obj->size;

			// Check for an additional structure field/index argument
			if (argc >= 3)
			{
				// Check the object type
				if (sig[0] == '{')
				{
					// Struct/union type.  The additional arugment is a field name.
					JsValueRef strval;
					const WCHAR *p;
					size_t len;
					if ((err = JsConvertValueToString(argv[2], &strval)) != JsNoError
						|| JsStringToPointer(strval, &p, &len) != JsNoError)
						return inst->Throw(err, _T("NativeObject.addressOf(): getting struct member name"));

					// search for the struct member
					auto Search = [p, len, &offset, &size, &eleSig](MarshallStructOrUnionSizer &sizer) -> bool
					{
						bool found = false;
						sizer.MarshallStructMembers([p, len, &offset, &size, &eleSig, &sizer, &found](WSTRING &memberName, WSTRING &memberSig)
						{
							// marshall the value
							sizer.MarshallValue();

							// is this the member we're looking for?
							if (memberName.compare(0, std::string::npos, p, len) == 0)
							{
								// it's a match - save the stats
								offset = sizer.lastItemOfs;
								size = sizer.lastItemSize;
								eleSig = memberSig;
								found = true;

								// done - stop searching
								return false;
							}
							
							// continue searching
							return true;
						});

						// throw an error if we didn't find it
						if (!found)
						{
							inst->Throw(MsgFmt(_T("NativeObject.addressOf(): field \"%.*s\" not found in struct/union"), (int)len, p));
							return false;
						}

						// success
						return true;
					};

					// find the offset of the field
					SigParser sigprs(sig + 3, sigEnd - 1);
					switch (sig[1])
					{
					case 'S':
						if (!Search(MarshallStructSizer(&sigprs, JS_INVALID_REFERENCE)))
							return inst->undefVal;
						break;

					case 'U':
						if (!Search(MarshallUnionSizer(&sigprs, JS_INVALID_REFERENCE)))
							return inst->undefVal;
						break;

					case 'I':
						return inst->Throw(_T("NativeObject.addressOf(): cannot take address of interface member element"));

					default:
						return inst->Throw(_T("NativeObject.addressOf(): invalid composite type"));
					}
				}
				else if (sig[0] == '[')
				{
					// Array type.  The additional arugment is an element index.
					JsValueRef numval;
					double d;
					if ((err = JsConvertValueToNumber(argv[2], &numval)) != JsNoError
						|| (err = JsNumberToDouble(numval, &d)) != JsNoError)
						return inst->Throw(err, _T("NativeObject.addressOf(): getting array index"));

					// get the array dimension
					int dim;
					bool isEmpty;
					if (!Marshaller::ParseArrayDim(sig, sigEnd, dim, isEmpty))
						return inst->undefVal;

					// make sure it's in range
					if (d < 0.0 || d >= static_cast<double>(dim))
						return inst->Throw(err, _T("NativeObject.addressOf(): array index out of bounds"));

					// figure the element size
					eleSig = SkipPointerOrArrayQual(sig);
					SigParser elePrs(eleSig);
					MarshallBasicSizer sizer(&elePrs);
					sizer.MarshallValue();

					// figure the offset
					size = sizer.size;
					offset = static_cast<size_t>(d) * size;
				}
			}

			// If the object contains an array of type T[n], the address of the array
			// has the simple pointer type "pointer to T", T*, NOT "pointer to array of T",
			// (T*)[n].  So strip the array dimension specifier in this case to get the
			// underlying type.
			if (eleSig[0] == '[')
				eleSig = SkipPointerOrArrayQual(eleSig.c_str());

			// create a native pointer object pointing to the object's data
			SigParser subSig(eleSig);
			if ((err = NativePointerData::Create(obj->data + offset, size, &subSig, 0, &jsval)) != JsNoError)
				return inst->Throw(err, _T("NativeObject.addressOf()"));
		}
		else if (auto comObj = Recover<COMImportData>(argv[1], nullptr); comObj != nullptr)
		{
			// create the wrapper
			SigParser mainSig(comObj->sig);
			if ((err = NativePointerData::Create(&comObj->pUnknown, sizeof(IUnknown*), &mainSig, 0, &jsval)) != JsNoError)
				return inst->Throw(err, _T("NativeObject.addressOf(COM object)"));
		}
		else
		{
			return inst->Throw(_T("NativeObject.addressOf() argument is not a native object"));
		}
	}

	return jsval;
}

void JavascriptEngine::ScheduleDeadObjectScan()
{
	// if a scan isn't already scheduled, schedule one
	if (!deadObjectScanPending)
	{
		// Schedule it for a little in the future.  Javascript objects
		// typically go out of scope in groups, so we're like to have a
		// set of several native objects collected at once.  Defer our
		// scan for a few moments so that the JS GC has a chance to get
		// through its backlog before we do our scan, so that any objects
		// that are going to become free on this GC pass are fully 
		// finalized before we start checking references.
		AddTask(new DeadObjectScanTask(1000));

		// note that we have a pending scan
		deadObjectScanPending = true;
	}
}

void JavascriptEngine::DeadObjectScan()
{
	// we no longer have a pending scan
	deadObjectScanPending = false;

	// Build the root set of the scan as the objects reachable from Javascript.
	std::list<std::pair<BYTE*, NativeDataTracker&>> workQueue;
	for (auto &it : nativeDataMap)
	{
		if ((it.second.isReferenced = it.second.isWrapperAlive) != false)
			workQueue.emplace_back(it.first, it.second);
	}

	// trace a pointer
	auto Trace = [this, &workQueue](BYTE *ptr)
	{
		// get the matching element in the map, or the element at the
		// next higher memory address if there isn't an exact match
		auto &it = nativeDataMap.lower_bound(ptr);

		// Check what we found
		if (it == nativeDataMap.end() || it->first != ptr)
		{
			// We didn't match, so lower_bound() will have given us the item
			// at the next higher address.  Back up to the previous item if
			// we're not already at the first item.
			if (it != nativeDataMap.begin())
				it--;
		}

		// if we have a valid item, check to see if our pointer is within
		// its bounds
		if (it != nativeDataMap.end() && ptr >= it->first && ptr < it->first + it->second.size)
		{
			// This looks like a pointer into this object.  If the object isn't
			// already marked as referenced, so mark it, and add it to the work
			// queue so that we can scan its contents the same way.
			if (!it->second.isReferenced)
			{
				it->second.isReferenced = true;
				workQueue.emplace_back(it->first, it->second);
			}
		}
	};

	// Trace references from NativePointer objects
	for (auto &it : nativePointerMap)
		Trace(it.second);

	// Process the work queue
	while (workQueue.size() != 0)
	{
		// get the bounds of the first element
		auto &qEle = workQueue.front();
		BYTE *base = qEle.first;
		BYTE **p = reinterpret_cast<BYTE**>(base);
		BYTE **endp = reinterpret_cast<BYTE**>(base + qEle.second.size);

		// Scan it as an array of pointers.  Pointers will always be aligned
		// on pointer-size boundaries, so we don't need to worrry about other
		// alignment interpretations.  Note the odd loop condition: we keep
		// going as long as p + 1 <= endp, rather than while p < endp.  This
		// is because the overall memory block might *not* be aligned on the
		// pointer size, so we could have a partial last slot.  The odd test
		// ensures that we have at least one whole slot left to inspect.
		for (; p + 1 <= endp; ++p)
		{
			// try tracing what's in this slot as a pointer
			Trace(*p);
		}

		// we're now done with this work queue element; remove it
		workQueue.pop_front();
	}

	// All reachable objects should now be marked as referenced.  Objects not
	// marked as referenced are unreachable and can be deleted.  Make a list of
	// the unreachable items.
	std::list<BYTE*> deadList;
	for (auto &it : nativeDataMap)
	{
		if (!it.second.isReferenced)
			deadList.emplace_back(it.first);
	}

	// Delete the unreachable objects
	for (auto &it : deadList)
		nativeDataMap.erase(it);
}

JavascriptEngine::NativeDataTracker::~NativeDataTracker()
{
	std::function<void(const WCHAR*, size_t, BYTE*)> Visit = [&Visit](const WCHAR *sig, size_t sigLen, BYTE *data)
	{
		// If this is a struct, scan for COM pointers.  This only applies
		// to structs: don't do it for unions, as we can't make assumptions
		// about which union interpretation applies; and don't do it for
		// bare pointers, because these are handled as COMImportData objects,
		// which manage the reference counting directly.
		if (sig[0] == '@' && sig[1] == 'S')
		{
			std::wstring_view subsig;
			if (inst->LookUpNativeType(sig + 1, sigLen - 1, subsig, true))
			{
				// set up a sizer
				SigParser sigprs(subsig);
				MarshallStructSizer sizer(&sigprs, JS_INVALID_REFERENCE);

				// process each field
				sizer.MarshallStructMembers([&sizer, &data, &Visit](WSTRING &memberName, WSTRING &memberSig)
				{
					// marshall the value to figure its size
					sizer.MarshallValue();

					// get its offset
					BYTE *memberData = data + sizer.lastItemOfs;

					// process it
					Visit(memberSig.c_str(), memberSig.length(), memberData);

					// continue the scan
					return true;
				});
			}
		}
		else if (sig[0] == '[')
		{
			// array - visit each item
			int dim;
			bool empty;
			const WCHAR *sigEnd = sig + sigLen;
			if (Marshaller::ParseArrayDim(sig, sigEnd, dim, empty) && !empty)
			{
				SigParser subsig(sig, sigEnd);
				MarshallBasicSizer sizer(&subsig, JS_INVALID_REFERENCE);
				sizer.Marshall();

				for (int i = 0; i < dim; ++i, data += sizer.size)
					Visit(sig, sigEnd - sig, data);
			}
		}
		else if (sig[0] == '*' && sig[1] == '@' && sig[2] == 'I')
		{
			// Embedded IUnknown pointer
			if (auto ppUnk = reinterpret_cast<IUnknown**>(data); *ppUnk != nullptr)
			{
				(*ppUnk)->Release();
				*ppUnk = nullptr;
			}
		}
		else if (sig[0] == 'B')
		{
			// Embedded BSTR
			if (auto pbstr = reinterpret_cast<BSTR*>(data); *pbstr != nullptr)
			{
				SysFreeString(*pbstr);
				*pbstr = nullptr;
			}
		}
		else if (sig[0] == 'V')
		{
			// Embedded VARIANT
			VariantClear(reinterpret_cast<VARIANT*>(data));
		}
	};

	// free internal objects
	Visit(sig.c_str(), sig.length(), data);

	// release the memory
	delete[] data;
}

// -----------------------------------------------------------------------
//
// Imported COM object reference
//

JavascriptEngine::COMImportData::COMImportData(IUnknown *pUnknown, SigParser *ifcSig) :
	pUnknown(pUnknown), sig(ifcSig->sig)
{
	// Our type is always "pointer to interface", with a signature of the form
	//
	//   *@I.name
	//
	// However, our caller might pass us just the interface type.  If our type
	// signature uses the plain {I...} form, add the pointer qualifier.
	if (this->sig[0] != '*')
		this->sig = L"*" + this->sig;

	// set up to parse the interface signature
	const WCHAR *p = this->sig.c_str();
	const WCHAR *end = p + this->sig.length();

	// get the name reference
	if (p < end && *p == '*')
		++p;
	if (p < end && *p == '@')
	{
		std::wstring_view r;
		++p;
		if (!inst->LookUpNativeType(p, Marshaller::EndOfArg(p, end) - p, r))
			return;

		p = r.data();
		end = p + r.length();
	}

	if (p + 2 >= end || p[0] != '{' || p[1] != 'I' || p[2] != ' ')
	{
		inst->Throw(_T("DllImport: invalid interface type signature"));
		return;
	}

	// extract the GUID - it comes after the *{I<space>
	p += 3;
	const WCHAR *guid = p;
	for (; p < end && *p != ' '; ++p);
	this->guid.assign(guid, p - guid);

	// count the number of vtable entries
	for (p = Marshaller::EndOfArg(p + 1, end); p < end && *p != '}'; ++vtableCount)
	{
		for (p = Marshaller::EndOfArg(p, end); p < end && *p == ' '; ++p);
	}
}

JavascriptEngine::COMImportData::~COMImportData()
{
	// release our underlying interface if we have one
	if (pUnknown != nullptr)
	{
		pUnknown->Release();
		pUnknown = nullptr;
	}
}

JsValueRef JavascriptEngine::COMImportData::Create(COMImportData **pCreatedObj, JsValueRef proto, IUnknown *pUnknown, SigParser *sig)
{
	// create the wrapper object
	auto *obj = new COMImportData(pUnknown, sig);

	// create a Javascript external object for the wrapper
	JsValueRef jsobj;
	JsErrorCode err;
	if ((err = CreateExternalObjectWithPrototype(jsobj, proto, obj)) != JsNoError)
		return inst->Throw(err, _T("dllImport: creating external object for COM interface pointer"));

	// if this is a new alias to an existing object, count the new reference
	if (pUnknown != nullptr)
		pUnknown->AddRef();

	// return the new object if desired
	if (pCreatedObj != nullptr)
		*pCreatedObj = obj;

	// return the object
	return jsobj;
}

bool JavascriptEngine::COMImportData::CreatePrototype(JsValueRef proto, const WCHAR *sig, const WCHAR *sigEnd)
{
	auto Error = [](const char *msg)
	{
		ThrowSimple(msg);
		return false;
	};

	// traverse into a type reference
	const WCHAR *p = sig;
	if (p < sigEnd && *p == '@')
	{
		// look up the name
		std::wstring_view r;
		const WCHAR *name = ++p;
		size_t nameLen = Marshaller::EndOfArg(p, sigEnd) - p;
		if (!inst->LookUpNativeType(name, nameLen, r))
			return false;

		// parse from the name
		p = r.data();
		sigEnd = p + r.length();
	}

	// check for the "{I" prefix
	if (p + 2 >= sigEnd || p[0] != '{' || p[1] != 'I' || p[2] != ' ')
		return Error("Importing COM object: invalid interface signature");

	// skip the "{I" prefix and GUID
	p += 3;
	const WCHAR *guid = p;
	for (; p < sigEnd && *p != ' '; ++p);

	// Get DllImport._bindCOM
	const TCHAR *where;
	JsValueRef bindExt;
	if (inst->GetProp(bindExt, inst->dllImportObject, "_bindCOM", where) != JsNoError)
		return Error("Importing COM object: Unable to find dllImport._bindCOM");

	// get the "value" and "enumerable" property IDs, for property descriptors
	JsPropertyIdRef valuePropId, enumerablePropId, trueVal;
	if (JsCreatePropertyId("value", 5, &valuePropId) != JsNoError
		|| JsCreatePropertyId("enumerable", 10, &enumerablePropId) != JsNoError
		|| JsGetTrueValue(&trueVal) != JsNoError)
		return Error("Importing COM object: getting descriptor property IDs/values");

	// bind each function
	int vtableIndex = 0;
	for (; p < sigEnd && *p != '}'; p = Marshaller::EndOfArg(p, sigEnd), ++vtableIndex)
	{
		// skip spaces
		for (; p < sigEnd && *p == ' '; ++p);

		// parse out the function name
		const WCHAR *f = p;
		for (; p < sigEnd && *p != ';'; ++p);

		// turn it into a string
		JsValueRef funcName;
		if (JsPointerToString(f, p - f, &funcName) != JsNoError)
			return false;

		// skip the ';'
		if (*p != ';')
			return false;
		++p;

		// find the end of the function signature
		const WCHAR *funcSig = p;
		const WCHAR *funcSigEnd = p = Marshaller::EndOfArg(p, sigEnd);

		// make a javascript string from the function signature
		JsValueRef funcSigVal;
		if (JsPointerToString(funcSig, funcSigEnd - funcSig, &funcSigVal) != JsNoError)
			return Error("Importing COM object: Error creating string from COM method signature");

		// get the vtable index as a javascript number
		JsValueRef vtableIndexVal;
		if (JsIntToNumber(vtableIndex, &vtableIndexVal) != JsNoError)
			return Error("Importing COM object: Error converting vtable index to number");

		// Call dllImport._bindCOM(dllImport, comObj, vtableIndex, funcsig).  The return value 
		// is the wrapped function pointer.
		JsValueRef boundFunc;
		JsValueRef bindArgv[3] = { inst->dllImportObject, vtableIndexVal, funcSigVal };
		if (JsCallFunction(bindExt, bindArgv, static_cast<unsigned short>(countof(bindArgv)), &boundFunc) != JsNoError)
			return Error("Importing COM object: JsCallFunction(dllImport._bindCOM() failed");

		// Now store the wrapped function pointer as the Javascript property
		// named for the COM interface method.  This will allow calling the
		// native COM method as though it were a normal Javascript method.
		bool ok;
		JsValueRef propDesc;
		if (JsCreateObject(&propDesc) != JsNoError
			|| JsSetProperty(propDesc, valuePropId, boundFunc, true) != JsNoError
			|| JsSetProperty(propDesc, enumerablePropId, trueVal, true) != JsNoError
			|| JsObjectDefineProperty(proto, funcName, propDesc, &ok) != JsNoError)
			return Error("Importing COM object: adding bound function property to object");
	}

	// success
	return true;
}

JsValueRef CALLBACK JavascriptEngine::COMImportData::IsNull(
	JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
{
	if (argc >= 2)
	{
		if (auto comObj = Recover<COMImportData>(argv[1], _T("COMPointer::isNull")); comObj != nullptr)
			return comObj->pUnknown == nullptr ? inst->trueVal : inst->falseVal;
	}

	return inst->Throw(_T("COMPointer.isNull: invalid argument"));
}

JsValueRef CALLBACK JavascriptEngine::COMImportData::Clear(
	JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
{
	if (argc >= 2)
	{
		if (auto comObj = Recover<COMImportData>(argv[1], _T("COMPointer::clear")); comObj != nullptr)
		{
			if (comObj->pUnknown != nullptr)
			{
				comObj->pUnknown->Release();
				comObj->pUnknown = nullptr;
			}
			return inst->undefVal;
		}
	}

	return inst->Throw(_T("COMPointer.clear: invalid argument"));
}

// -----------------------------------------------------------------------
//
// Native COM VARIANT object
//

JsValueRef JavascriptEngine::VariantData::Create(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);
	if (!isConstructCall)
		return js->Throw(_T("Variant() must be called as constructor"));

	// create the external object
	auto v = new VariantData();
	JsValueRef jsval;
	JsErrorCode err;
	if ((err = CreateExternalObjectWithPrototype(jsval, js->Variant_proto, v)) != JsNoError)
		return js->Throw(err, _T("creating Variant"));

	// set the value if there's an initializer argument
	if (argc >= 2)
		Set(v->v, argv[1]);

	// return the new object
	return jsval;
}

JsErrorCode JavascriptEngine::VariantData::CreateFromNative(const VARIANT *src, JsValueRef &dest)
{
	// create the external object
	auto js = inst;
	JsErrorCode err;
	auto v = new VariantData();
	if ((err = CreateExternalObjectWithPrototype(dest, js->Variant_proto, v)) != JsNoError)
		return err;

	// copy the source value
	VariantCopy(&v->v, src);
	return JsNoError;
}

void JavascriptEngine::VariantData::CopyFromJavascript(VARIANT *dest, JsValueRef src)
{
	if (auto v = VariantData::Recover<VariantData>(src, nullptr); v != nullptr)
	{
		// it's a variant - copy it directly
		VariantCopy(dest, &v->v);
	}
	else
	{
		// set the value from the Javsacript source
		Set(*dest, src);
	}
}

JsValueRef JavascriptEngine::VariantData::GetVt(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);
	JsValueRef ret = js->undefVal;
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant.vt")); v != nullptr)
		JsIntToNumber(static_cast<int>(v->v.vt), &ret);

	return ret;
}

JsValueRef JavascriptEngine::VariantData::SetVt(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);
	
	if (argc < 2)
		return js->Throw(_T("Variant.vt [setter]: missing value"));

	JsValueRef ret = argv[0];
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant.vt")); v != nullptr)
	{
		// Clear any prior value, so that we free any memory allocated
		// with the prior type.  If we didn't do this, we could leak
		// the allocated memory, since the new type would make us lose
		// track of the original interpretation that implied allocated
		// memory.  Worse, the new type could cause us to misinterpret
		// the old value as some kind of reference type that it never
		// was, which could cause us to try to free a block of memory
		// that isn't actually a block of memory, or otherwise follow
		// a bad pointer.  Clearing the old value will avoid all of 
		// those misadventures by freeing any old memory and resetting
		// the internal pointer field to null.
		VariantClear(&v->v);

		// set the new value to the number in the argument
		JsErrorCode err;
		JsValueRef numval;
		int i;
		if ((err = JsConvertValueToNumber(argv[1], &numval)) != JsNoError
			|| (err = JsNumberToInt(numval, &i)) != JsNoError)
			return js->Throw(err, _T("Variant.vt [setter]"));

		v->v.vt = i;
	}
	return ret;
}

template<typename T>
T JavascriptEngine::VariantData::SetByValue(VARIANT &v, void *pData, VARTYPE vt)
{
	v.vt = vt;
	return *static_cast<T*>(pData);
}

template<typename T>
T *JavascriptEngine::VariantData::SetByRef(VARIANT &v, void *pData, VARTYPE vt)
{
	v.vt = vt | VT_BYREF;
	return static_cast<T*>(pData);
}

void JavascriptEngine::VariantData::Set(VARIANT &v, JsValueRef val)
{
	auto js = inst;
	JsValueType type;
	JsErrorCode err;
	if ((err = JsGetValueType(val, &type)) != JsNoError)
	{
		js->Throw(err, _T("Variant.Set"));
		return;
	}

	// clear any old value
	VariantClear(&v);

	switch (type)
	{
	case JsUndefined:
		v.vt = VT_EMPTY;
		break;

	case JsNull:
		v.vt = VT_NULL;
		break;

	case JsNumber:
		v.vt = VT_R8;
		err = JsNumberToDouble(val, &v.dblVal);
		break;

	case JsString:
		v.vt = VT_BSTR;
		{
			const wchar_t *p;
			size_t len;
			if ((err = JsStringToPointer(val, &p, &len)) == JsNoError)
			{
				if (len < static_cast<size_t>(UINT_MAX))
					v.bstrVal = SysAllocStringLen(p, static_cast<UINT>(len));
				else
					js->Throw(_T("String is too long to convert to VARIANT string"));
			}
		}
		break;

	case JsBoolean:
		v.vt = VT_BOOL;
		{
			bool b;
			if ((err = JsBooleanToBool(val, &b)) == JsNoError)
				v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
		}
		break;

	case JsObject:
		if (auto i = XInt64Data<INT64>::Recover<XInt64Data<INT64>>(val, nullptr); i != nullptr)
		{
			v.vt = VT_I8;
			v.llVal = i->i;
		}
		else if (auto u = XInt64Data<INT64>::Recover<XInt64Data<INT64>>(val, nullptr); u != nullptr)
		{
			v.vt = VT_UI8;
			v.ullVal = u->i;
		}
		else if (auto o = NativeTypeWrapper::Recover<NativeTypeWrapper>(val, nullptr); o != nullptr)
		{
			switch (o->sig[0])
			{
			case 'c': v.cVal = SetByValue<CHAR>(v, o->data, VT_I1); break;
			case 'C': v.bVal = SetByValue<BYTE>(v, o->data, VT_UI1); break;
			case 's': v.iVal = SetByValue<SHORT>(v, o->data, VT_I2); break;
			case 'S': v.uiVal = SetByValue<USHORT>(v, o->data, VT_UI2); break;
			case 'i': v.lVal = SetByValue<LONG>(v, o->data, VT_I4); break;
			case 'I': v.ulVal = SetByValue<ULONG>(v, o->data, VT_UI4); break;
			case 'f': v.fltVal = SetByValue<FLOAT>(v, o->data, VT_R4); break;
			case 'd': v.dblVal = SetByValue<DOUBLE>(v, o->data, VT_R8); break;
			case 'B': v.bstrVal = SysAllocString(SetByValue<BSTR>(v, o->data, VT_BSTR)); break;
			case '*':
				switch (o->sig[1])
				{
				case 't':
					// single-byte string
					if (o->data != nullptr)
						v.bstrVal = SysAllocString(AnsiToWide(SetByValue<CHAR*>(v, o->data, VT_BSTR)).c_str());
					else
						v.bstrVal = SetByValue<BSTR>(v, nullptr, VT_BSTR);
					break;

				case 'T':
					// unicode string
					v.bstrVal = SysAllocString(SetByValue<OLECHAR*>(v, o->data, VT_BSTR));
					break;

				default:
					js->Throw(_T("Variant.Set: native pointer type not supported"));
				}
				break;

			default:
				js->Throw(_T("Variant.Set: native type not supported"));
				break;
			}
		}
		else if (auto p = NativePointerData::Recover<NativePointerData>(val, nullptr); p != nullptr)
		{
			switch (p->sig[0])
			{
			case 'c': v.pcVal = SetByRef<CHAR>(v, p->ptr, VT_I1); break;
			case 'C': v.pbVal = SetByRef<BYTE>(v, p->ptr, VT_UI1); break;
			case 's': v.piVal = SetByRef<SHORT>(v, p->ptr, VT_I2); break;
			case 'S': v.puiVal = SetByRef<USHORT>(v, p->ptr, VT_UI2); break;
			case 'i': v.plVal = SetByRef<LONG>(v, p->ptr, VT_I4); break;
			case 'I': v.pulVal = SetByRef<ULONG>(v, p->ptr, VT_UI4); break;
			case 'f': v.pfltVal = SetByRef<FLOAT>(v, p->ptr, VT_R4); break;
			case 'd': v.pdblVal = SetByRef<DOUBLE>(v, p->ptr, VT_R8); break;
			case 'B': v.pbstrVal = SetByRef<BSTR>(v, p->ptr, VT_BSTR); break;
			default:
				js->Throw(_T("Variant.Set: pointer type not supported"));
				break;
			}
		}
		else if (auto pv = VariantData::Recover<VariantData>(val, nullptr); pv != nullptr)
		{
			v.vt = VT_BYREF | VT_VARIANT;
			v.pvarVal = &v;
		}
		else if (auto pi = COMImportData::Recover<COMImportData>(val, nullptr); pi != nullptr)
		{
			v.vt = VT_UNKNOWN;
			if ((v.punkVal = pi->pUnknown) != nullptr)
				v.punkVal->AddRef();
		}
		else
			js->Throw(_T("Variant.Set: invalid object type"));
		break;

	case JsArray:
	case JsArrayBuffer:
	case JsTypedArray:
	case JsFunction:
	case JsError:
	case JsSymbol:
	case JsDataView:
	default:
		js->Throw(_T("Variant.Set: invalid type"));
		return;
	}

	if (err != JsNoError)
		js->Throw(err, _T("Variant.Set"));
}

JsValueRef JavascriptEngine::VariantData::GetValue(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);
	JsValueRef ret = js->undefVal;
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant.value")); v != nullptr)
		ret = Get(v->v);

	return ret;
}

template<typename T>
JsValueRef JavascriptEngine::VariantData::GetByRef(T *pData, const WCHAR *sig)
{
	SigParser s(sig, wcslen(sig));
	return inst->CreateNativeObject(&s, pData);
}

JsValueRef JavascriptEngine::VariantData::GetByRefArray(const VARIANT &v, const WCHAR *sig)
{
	inst->Throw(_T("Variant arrays are not implemented"));
	return inst->undefVal;

#if 0
	SAFEARRAY *a = (v.vt & VT_BYREF) != 0 ? *v.pparray : v.parray;
	UINT nDims = SafeArrayGetDim(a);
	for (UINT i = 0; i < nDims; ++i)
	{
		LONG lb, ub;
		SafeArrayGetLBound(a, i, &lb);
		SafeArrayGetUBound(a, i, &ub);
	}
#endif
}

DATE JavascriptEngine::VariantData::JsDateToVariantDate(JsValueRef val)
{
	auto js = JavascriptEngine::Get();
	DATE date;
	if (JsErrorCode err = js->JsDateToVariantDate(val, date); err != JsNoError)
	{
		js->Throw(err, _T("converting Javascript Date to Variant Date"));
		return 0;
	}
	return date;
}

JsValueRef JavascriptEngine::VariantData::VariantDateToJsDate(DATE date)
{
	auto js = JavascriptEngine::Get();
	JsValueRef result;
	if (JsErrorCode err = js->VariantDateToJsDate(date, result); err != JsNoError)
	{
		js->Throw(err, _T("converting Variant Date to Javascript Date"));
		return js->undefVal;
	}
	return result;
}

JsValueRef JavascriptEngine::VariantData::Get(const VARIANT &v)
{
	auto js = inst;
	JsErrorCode err = JsNoError;
	JsValueRef ret = inst->undefVal;

	// if it's by reference, and the pointer is null, return null
	if ((v.vt & VT_BYREF) != 0 && v.byref == nullptr)
		return js->nullVal;

	switch (v.vt)
	{
	case VT_EMPTY: ret = js->undefVal; break;
	case VT_NULL: ret = js->nullVal; break;

	case VT_DATE: ret = VariantDateToJsDate(v.date); break;

	case VT_CY:
		// Currency type (96-bit fixed point, scaled by 10000).  Note:
		// converting to Javascript Number (== C double) can result in
		// loss of precision, plus rounding errors due to binary vs
		// decimal scaling.  For faithful conversion, we'd have to
		// implement a new native class for this type.
		{
			double d;
			if (!SUCCEEDED(VarR8FromCy(v.cyVal, &d)))
				js->Throw(_T("Error converting Variant CURRENCY to number"));
			err = JsDoubleToNumber(d, &ret);
		}
		break;

	case VT_BYREF | VT_DECIMAL:
		// Decimal type (96-bit floating point, decimal scaling).  As
		// with Currency, this type cannot be represented with perfect
		// fidelity via Javascript Number, so we'd have to add a new
		// native class if we needed round-trip fidelity.
		{
			double d;
			if (!SUCCEEDED(VarR8FromDec(v.pdecVal, &d)))
				js->Throw(_T("Error converting Variant CURRENCY to number"));
			err = JsDoubleToNumber(d, &ret);
		}
		break;

	case VT_ARRAY: 
		err = JsErrorNotImplemented; 
		break;

	case VT_BSTR:
		err = (v.bstrVal == nullptr ? JsPointerToString(L"", 0, &ret) : JsPointerToString(v.bstrVal, SysStringLen(v.bstrVal), &ret));
		break;

	case VT_UNKNOWN: ret = GetByRef(v.punkVal, L"@I.IUnknown"); break;
	case VT_BYREF | VT_UNKNOWN: ret = GetByRef(v.ppunkVal, L"**@I.IUnknown"); break;

	case VT_DISPATCH: ret = js->WrapAutomationObject(WSTRING(L"[Return Value]"), static_cast<IDispatch*>(v.punkVal)); break;
	case VT_BYREF | VT_DISPATCH: ret = js->WrapAutomationObject(WSTRING(L"[Return Value]"), static_cast<IDispatch*>(*v.ppunkVal)); break;

	case VT_BYREF | VT_VARIANT: ret = GetByRef(v.pvarVal, L"*V"); break;

	case VT_I1: err = JsIntToNumber(v.cVal, &ret); break;
	case VT_UI1: err = JsIntToNumber(v.bVal, &ret); break;
	case VT_I2: err = JsIntToNumber(v.iVal, &ret); break;
	case VT_UI2: err = JsIntToNumber(v.uiVal, &ret); break;
	case VT_I4: err = JsIntToNumber(v.lVal, &ret); break;
	case VT_UI4: err = JsIntToNumber(v.ulVal, &ret); break;
	case VT_INT: err = JsIntToNumber(v.intVal, &ret); break;
	case VT_UINT: err = JsIntToNumber(v.uintVal, &ret); break;
	case VT_I8: err = XInt64Data<INT64>::CreateFromInt(v.llVal, ret); break;
	case VT_UI8: err = XInt64Data<UINT64>::CreateFromInt(v.ullVal, ret); break;
	case VT_R4: err = JsDoubleToNumber(v.fltVal, &ret); break;
	case VT_R8: err = JsDoubleToNumber(v.dblVal, &ret); break;
	case VT_BOOL: err = JsBoolToBoolean(v.boolVal != 0, &ret); break;
	case VT_ERROR: err = JsIntToNumber(v.scode, &ret); break;

	case VT_BYREF | VT_I1: ret = GetByRef(v.pcVal, L"*c"); break;
	case VT_BYREF | VT_UI1: ret = GetByRef(v.pbVal, L"*C"); break;
	case VT_BYREF | VT_I2: ret = GetByRef(v.piVal, L"*s"); break;
	case VT_BYREF | VT_UI2: ret = GetByRef(v.puiVal, L"*S"); break;
	case VT_BYREF | VT_I4: ret = GetByRef(v.plVal, L"*i"); break;
	case VT_BYREF | VT_UI4: ret = GetByRef(v.pulVal, L"*I"); break;
	case VT_BYREF | VT_I8: ret = GetByRef(v.pllVal, L"*l"); break;
	case VT_BYREF | VT_UI8: ret = GetByRef(v.pullVal, L"*L"); break;
	case VT_BYREF | VT_INT: ret = GetByRef(v.pintVal, L"*i"); break;
	case VT_BYREF | VT_UINT: ret = GetByRef(v.puintVal, L"*I"); break;
	case VT_BYREF | VT_R4: ret = GetByRef(v.pfltVal, L"*f"); break;
	case VT_BYREF | VT_R8: ret = GetByRef(v.pdblVal, L"*d"); break;
	case VT_BYREF | VT_ERROR: ret = GetByRef(v.pscode, L"*i"); break;
	case VT_BYREF | VT_BOOL: ret = GetByRef(v.pboolVal, L"*s"); break;  // VARIANT_BOOL = SHORT
	case VT_BYREF | VT_BSTR: ret = GetByRef(v.pbstrVal, L"*B"); break;

	case VT_BYREF | VT_DATE: err = JsErrorNotImplemented; break;

	case VT_BYREF | VT_CY: err = JsErrorNotImplemented; break;

	case VT_BYREF | VT_ARRAY: err = JsErrorNotImplemented; break;

	case VT_ARRAY | VT_BYREF | VT_I1:
	case VT_ARRAY | VT_I1: ret = GetByRefArray(v, L"c"); break;

	case VT_ARRAY | VT_BYREF | VT_UI1:
	case VT_ARRAY | VT_UI1: ret = GetByRefArray(v, L"C"); break;

	case VT_ARRAY | VT_BYREF | VT_I2:
	case VT_ARRAY | VT_I2: ret = GetByRefArray(v, L"s"); break;

	case VT_ARRAY | VT_BYREF | VT_UI2:
	case VT_ARRAY | VT_UI2: ret = GetByRefArray(v, L"S"); break;

	case VT_ARRAY | VT_BYREF | VT_I4:
	case VT_ARRAY | VT_I4: ret = GetByRefArray(v, L"i"); break;

	case VT_ARRAY | VT_BYREF | VT_UI4:
	case VT_ARRAY | VT_UI4: ret = GetByRefArray(v, L"I"); break;

	case VT_ARRAY | VT_BYREF | VT_I8:
	case VT_ARRAY | VT_I8: ret = GetByRefArray(v, L"l"); break;

	case VT_ARRAY | VT_BYREF | VT_UI8:
	case VT_ARRAY | VT_UI8: ret = GetByRefArray(v, L"L"); break;

	case VT_ARRAY | VT_BYREF | VT_INT:
	case VT_ARRAY | VT_INT: ret = GetByRefArray(v, L"i"); break;

	case VT_ARRAY | VT_BYREF | VT_UINT:
	case VT_ARRAY | VT_UINT: ret = GetByRefArray(v, L"I"); break;

	case VT_ARRAY | VT_BYREF | VT_R4:
	case VT_ARRAY | VT_R4: ret = GetByRefArray(v, L"f"); break;

	case VT_ARRAY | VT_BYREF | VT_R8:
	case VT_ARRAY | VT_R8: ret = GetByRefArray(v, L"d"); break;

	case VT_ARRAY | VT_BYREF | VT_ERROR:
	case VT_ARRAY | VT_ERROR: ret = GetByRefArray(v, L"i"); break;

	case VT_ARRAY | VT_BYREF | VT_BOOL:
	case VT_ARRAY | VT_BOOL: ret = GetByRefArray(v, L"s"); break;  // VARIANT_BOOL = SHORT

	case VT_ARRAY | VT_BYREF | VT_BSTR:
	case VT_ARRAY | VT_BSTR: ret = GetByRefArray(v, L"B"); break;

	case VT_ARRAY | VT_BYREF | VT_VARIANT:
	case VT_ARRAY | VT_VARIANT: ret = GetByRefArray(v, L"V"); break;

	case VT_USERDEFINED:
		return js->Throw(_T("Variant.Get: user-defined types are not supported"));

	default:
		return js->Throw(_T("Variant.Get: untranslatable type"));
	}

	if (err != JsNoError)
		return js->Throw(err, _T("Variant.Get"));

	return ret;
}

JsValueRef JavascriptEngine::VariantData::SetValue(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);

	if (argc < 2)
		return js->Throw(_T("Variant.value [setter]: missing value"));

	JsValueRef ret = argv[0];
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant.value")); v != nullptr)
		Set(v->v, argv[1]);

	return ret;
}


template<typename T, VARTYPE vt, T VARIANT::*ele>
JsErrorCode JavascriptEngine::VariantData::AddNumGetSet(JavascriptEngine *js, const CHAR *name, const TCHAR* &where)
{
	return js->AddGetterSetter(js->Variant_proto, name, 
		&VariantData::GetNumVal<T, vt, ele>, js, 
		&VariantData::SetNumVal<T, vt, ele>, js,
		where);
}

template<typename T, VARTYPE vt, T VARIANT::*ele>
JsValueRef CALLBACK JavascriptEngine::VariantData::GetNumVal(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);

	JsValueRef ret = js->undefVal;
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant [getter]")); v != nullptr)
	{
		if (v->v.vt != vt)
			return js->Throw(_T("Wrong type for variant getter"));

		JsErrorCode err;
		if ((err = JsDoubleToNumber(static_cast<double>(v->v.*ele), &ret)) != JsNoError)
			return js->Throw(_T("Variant [getter]"));
	}

	return ret;
}

template<typename T, VARTYPE vt, T VARIANT::*ele>
JsValueRef CALLBACK JavascriptEngine::VariantData::SetNumVal(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);

	if (argc < 2)
		return js->Throw(_T("Variant [setter]: missing value"));

	JsValueRef ret = argv[0];
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant [setter]")); v != nullptr)
	{
		JsValueRef numval;
		double d;
		JsErrorCode err;
		if ((err = JsConvertValueToNumber(argv[1], &numval)) != JsNoError
			|| (err = JsNumberToDouble(numval, &d)) != JsNoError)
			return js->Throw(err, _T("Variant [setter]"));

		VariantClear(&v->v);
		v->v.vt = vt;
		v->v.*ele = static_cast<T>(d);
	}

	return ret;
}

JsValueRef CALLBACK JavascriptEngine::VariantData::GetCY(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);

	JsValueRef ret = js->undefVal;
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant [getter]")); v != nullptr)
	{
		if (v->v.vt != VT_CY)
			return js->Throw(_T("Wrong type for variant getter"));

		JsErrorCode err;
		double d;
		if (!SUCCEEDED(VarR8FromCy(v->v.cyVal, &d)))
			return js->Throw(_T("Error converting Currency value to double"));
		if ((err = JsDoubleToNumber(d, &ret)) != JsNoError)
			return js->Throw(_T("Variant cyVal [getter]"));
	}

	return ret;
}

JsValueRef CALLBACK JavascriptEngine::VariantData::SetCY(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);

	if (argc < 2)
		return js->Throw(_T("Variant [setter]: missing value"));

	JsValueRef ret = argv[0];
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant [setter]")); v != nullptr)
	{
		JsValueRef numval;
		double d;
		CY cy;
		JsErrorCode err;
		if ((err = JsConvertValueToNumber(argv[1], &numval)) != JsNoError
			|| (err = JsNumberToDouble(numval, &d)) != JsNoError)
			return js->Throw(err, _T("Variant [setter]"));
		
		if (!SUCCEEDED(VarCyFromR8(d, &cy)))
			return js->Throw(_T("Error converting Number value to Currency"));

		VariantClear(&v->v);
		v->v.vt = VT_CY;
		v->v.cyVal = cy;
	}

	return ret;
}

JsValueRef CALLBACK JavascriptEngine::VariantData::GetDecimal(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);

	JsValueRef ret = js->undefVal;
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant decVal [getter]")); v != nullptr)
	{
		if (v->v.vt != VT_DECIMAL)
			return js->Throw(_T("Wrong type for variant getter"));

		if (v->v.pdecVal == nullptr)
			return js->nullVal;

		JsErrorCode err;
		double d;
		if (!SUCCEEDED(VarR8FromDec(v->v.pdecVal, &d)))
			return js->Throw(_T("Error converting Decimal value to double"));
		if ((err = JsDoubleToNumber(d, &ret)) != JsNoError)
			return js->Throw(_T("Variant decVal [getter]"));
	}

	return ret;
}

JsValueRef CALLBACK JavascriptEngine::VariantData::SetDecimal(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);

	if (argc < 2)
		return js->Throw(_T("Variant decVal [setter]: missing value"));

	JsValueRef ret = argv[0];
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant decVal [setter]")); v != nullptr)
	{
		JsValueRef numval;
		double d;
		JsErrorCode err;
		if ((err = JsConvertValueToNumber(argv[1], &numval)) != JsNoError
			|| (err = JsNumberToDouble(numval, &d)) != JsNoError)
			return js->Throw(err, _T("Variant decVal [setter]"));

		if (!SUCCEEDED(VarDecFromR8(d, &v->decimal)))
			return js->Throw(_T("Error converting Number value to Currency"));

		VariantClear(&v->v);
		v->v.vt = VT_DECIMAL;
		v->v.pdecVal = &v->decimal;
	}

	return ret;
}

JsValueRef CALLBACK JavascriptEngine::VariantData::GetDate(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);

	JsValueRef ret = js->undefVal;
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant date [getter]")); v != nullptr)
	{
		if (v->v.vt != VT_DATE)
			return js->Throw(_T("Wrong type for variant date getter"));

		ret = VariantDateToJsDate(v->v.date);
	}

	return ret;
}

JsValueRef CALLBACK JavascriptEngine::VariantData::SetDate(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);

	if (argc < 2)
		return js->Throw(_T("Variant date [setter]: missing value"));

	JsValueRef ret = argv[0];
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant date [setter]")); v != nullptr)
	{
		VariantClear(&v->v);
		v->v.vt = VT_DATE;
		v->v.date = JsDateToVariantDate(argv[1]);
	}

	return ret;
}


JsValueRef CALLBACK JavascriptEngine::VariantData::GetBool(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);

	JsValueRef ret = js->undefVal;
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant boolVal [getter]")); v != nullptr)
	{
		if (v->v.vt != VT_BOOL)
			return js->Throw(_T("Wrong type for variant boolVal getter"));

		ret = v->v.boolVal ? js->trueVal : js->falseVal;
	}

	return ret;
}

JsValueRef CALLBACK JavascriptEngine::VariantData::SetBool(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);

	if (argc < 2)
		return js->Throw(_T("Variant boolVal [setter]: missing value"));

	JsValueRef ret = argv[0];
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant boolVal [setter]")); v != nullptr)
	{
		JsErrorCode err;
		JsValueRef boolVal;
		bool b;
		if ((err = JsConvertValueToBoolean(argv[1], &boolVal)) != JsNoError
			|| (err = JsBooleanToBool(boolVal, &b)) != JsNoError)
			return js->Throw(err, _T("Variant boolVal [setter]"));

		VariantClear(&v->v);
		v->v.vt = VT_BOOL;
		v->v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
	}

	return ret;
}


JsValueRef CALLBACK JavascriptEngine::VariantData::GetBSTR(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);

	JsValueRef ret = js->undefVal;
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant bstrVal [getter]")); v != nullptr)
	{
		if (v->v.vt != VT_BSTR)
			return js->Throw(_T("Wrong type for variant bstrVal getter"));

		JsErrorCode err;
		if ((err = JsPointerToString(v->v.bstrVal, SysStringLen(v->v.bstrVal), &ret)) != JsNoError)
			return js->Throw(err, _T("Variant bstrVal [getter]"));
	}

	return ret;
}

JsValueRef CALLBACK JavascriptEngine::VariantData::SetBSTR(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);

	if (argc < 2)
		return js->Throw(_T("Variant bstrVal [setter]: missing value"));

	JsValueRef ret = argv[0];
	if (auto v = VariantData::Recover<VariantData>(argv[0], _T("Variant bstrVal [setter]")); v != nullptr)
	{
		JsErrorCode err;
		JsValueRef strVal;
		const wchar_t *p; 
		size_t len;
		if ((err = JsConvertValueToString(argv[1], &strVal)) != JsNoError
			|| (err = JsStringToPointer(strVal, &p, &len)) != JsNoError)
			return js->Throw(err, _T("Variant bstrVal [setter]"));

		if (len > static_cast<size_t>(UINT_MAX))
			return js->Throw(_T("Variant bstrVal [setter]: string is too long to convert to BSTR"));

		VariantClear(&v->v);
		v->v.vt = VT_BSTR;
		v->v.bstrVal = SysAllocStringLen(p, static_cast<UINT>(len));
	}

	return ret;
}

// --------------------------------------------------------------------------
//
// OLE Automation interface.  This implements access to scripting objects
// through IDispatch interfaces.  We provide automatic mapping to Javascript
// types to allow calling automation methods directly from Javascript.
//

//
// createAutomationObject() implementation
//
JsValueRef CALLBACK JavascriptEngine::CreateAutomationObject(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsErrorCode err;
	auto js = static_cast<JavascriptEngine*>(ctx);

	// check arguments
	WSTRING className;
	if (argc >= 2)
	{
		JsValueRef strval;
		const wchar_t *p;
		size_t len;
		if ((err = JsConvertValueToString(argv[1], &strval)) != JsNoError
			|| (err = JsStringToPointer(strval, &p, &len)) != JsNoError)
			return js->Throw(err, _T("createAutomationObject: getting class name argument"));

		className.assign(p, len);
	}

	HRESULT hr;
	auto ComErr = [js, &hr, &className](const CHAR *where)
	{
		WindowsErrorMessage err(hr);
		return js->Throw(MsgFmt(_T("createAutomationObject(\"%ws\"): %hs: %s"), className.c_str(), where, err.Get()));
	};

	// If it looks like a GUID, use the GUID.  Otherwise, take it as a
	// Program ID (ProgID) and look up the class that way.
	CLSID clsid;
	if (!ParseGuid(className.c_str(), clsid))
	{
		if (!SUCCEEDED(hr = CLSIDFromProgID(className.c_str(), &clsid)))
			return ComErr("looking up program ID");
	}

	// create the COM object
	RefPtr<IDispatch> disp;
	if (!SUCCEEDED(hr = CoCreateInstance(clsid, NULL, CLSCTX_LOCAL_SERVER | CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&disp))))
		return ComErr("creating instance");

	// wrap it in an AutomationObjectData external object
	return js->WrapAutomationObject(className, disp);
}

JsValueRef JavascriptEngine::WrapAutomationObject(WSTRING &className, IDispatch *disp)
{
	JsErrorCode err;
	HRESULT hr;
	auto ComErr = [this, &hr, &className](const CHAR *where)
	{
		WindowsErrorMessage err(hr);
		return Throw(MsgFmt(_T("createAutomationObject(\"%ws\"): %hs: %s"), className.c_str(), where, err.Get()));
	};

	// if the dispatch interface is null, return javascript null
	if (disp == nullptr)
		return nullVal;

	// get the type info
	RefPtr<ITypeInfo> typeInfo;
	if (!SUCCEEDED(hr = disp->GetTypeInfo(0, LOCALE_USER_DEFAULT, &typeInfo)))
		return ComErr("getting type information");

	// get the type attributes
	TYPEATTRHolder typeAttr(typeInfo);
	if (!SUCCEEDED(hr = typeInfo->GetTypeAttr(&typeAttr)))
		return ComErr("getting type attributes");

	// look up the GUID in the interface cache
	JsValueRef proto;
	TSTRING typeGuid = FormatGuid(typeAttr->guid);
	if (auto it = automationInterfaceCache.find(typeGuid); it != automationInterfaceCache.end())
	{
		// we already have a prototype for this interface type
		proto = it->second;
	}
	else
	{
		// We don't have a prototype for this interface yet.  Create one.
		if ((err = JsCreateObject(&proto)) != JsNoError)
			return Throw(err, _T("createAutomationObject: creating object for interface prototype"));

		// add an external reference on the proto, since we're going to keep it
		// in our cache
		JsAddRef(proto, nullptr);

		// add it to the cache
		automationInterfaceCache.emplace(typeGuid, proto);

		// Get dllImport._bindDispatch
		JsValueRef bindProp;
		const TCHAR *where;
		if ((err = GetProp(bindProp, inst->dllImportObject, "_bindDispatch", where)) != JsNoError)
			return Throw(err, MsgFmt(_T("createAutomationObject: getting dllImport._bindDispatch: %s"), where));

		// Map of getter/setters.  We populate this as we encounter get/set
		// methods in the automation interface.  We have to defer creation
		// of the Javascript equivalents until we've traversed the entire
		// interface, because Javascript requires us to set up the get/set
		// pair for a given property name as a unit.
		struct GetSet
		{
			struct
			{
				INVOKEKIND invkind = INVOKE_PROPERTYGET;
				JsValueRef func = JS_INVALID_REFERENCE;
			}
			get, set;
		};
		std::map<WSTRING, GetSet> getSet;

		// get the function descriptors
		for (WORD i = 0; i < typeAttr->cFuncs; ++i)
		{
			// get this function descriptor
			FUNCDESCHolder funcDesc(typeInfo);
			if (!SUCCEEDED(hr = typeInfo->GetFuncDesc(i, &funcDesc)))
				return ComErr("getting function descriptor");

			// If this is the special _NewEnum member, the enclosing interface is a
			// collection interface with an enumerator, and this is the dispatch method
			// to create the enumerator. 
			if (funcDesc->memid == DISPID_NEWENUM)
			{
				// create the Javascript iterator binding
				JsValueRef makeIterProp;
				JsValueRef bindArgs[4] = { inst->dllImportObject, proto }, bindResult;
				if ((err = GetProp(makeIterProp, inst->dllImportObject, "_makeIterable", where)) != JsNoError
					|| (err = JsIntToNumber(static_cast<int>(i), &bindArgs[2])) != JsNoError
					|| (err = JsIntToNumber(static_cast<int>(funcDesc->invkind), &bindArgs[3])) != JsNoError
					|| (err = JsCallFunction(makeIterProp, bindArgs, static_cast<unsigned short>(countof(bindArgs)), &bindResult)) != JsNoError)
					return Throw(err, _T("createAutomationObject: creating @@iterator wrapper"));
			}

			// Skip restricted, hidden, and non-dispatch functions, with the
			// following special exceptions:
			if ((funcDesc->wFuncFlags & (FUNCFLAG_FRESTRICTED | FUNCFLAG_FHIDDEN)) != 0
				|| funcDesc->funckind != FUNC_DISPATCH)
				continue;

			// get the function name
			UINT nNames;
			BStringArray names(32);
			if (!SUCCEEDED(hr = typeInfo->GetNames(funcDesc->memid, &names, names.n, &nNames)))
				return ComErr("getting function name");

			// if it's not named, skip it
			if (nNames == 0)
				continue;

			// Build the lambda wrapping the method via
			//    dllImport._bindDispatch(funcIndex, dispatchType)
			//
			JsValueRef bindArgs[3] = { inst->dllImportObject }, bindResult;
			if ((err = JsIntToNumber(static_cast<int>(i), &bindArgs[1])) != JsNoError
				|| (err = JsIntToNumber(static_cast<int>(funcDesc->invkind), &bindArgs[2])) != JsNoError
				|| (err = JsCallFunction(bindProp, bindArgs, static_cast<unsigned short>(countof(bindArgs)), &bindResult)) != JsNoError)
				return Throw(err, _T("createAutomationObject: creating method wrapper"));

			// If it's a regular "method call" dispatch, bind it immediately.  If it's
			// a getter or setter, add it to the get/set map.  We have to bind each
			// get/set pair as a unit, so we have to wait until we've visited all of
			// the functions to be sure we have the full set.  
			//
			// Some functions that take arguments are marked as property getters, such
			// as OLE automation collection .Item methods.  For Javascript purposes,
			// such a function is a function, not a getter, as a JS getter can't take
			// any arguments.
			if (funcDesc->invkind == INVOKE_FUNC || funcDesc->cParams != 0 || funcDesc->cParamsOpt != 0)
			{
				// method call - bind now
				JsValueRef propKey;
				if ((err = JsPointerToString(names[0], SysStringLen(names[0]), &propKey)) != JsNoError
					|| (err = JsObjectSetProperty(proto, propKey, bindResult, true)) != JsNoError)
					return Throw(err, _T("createAutomationObject: binding method wrapper"));
			}
			else
			{
				// property get/set - find the map entry for this property name
				WSTRING name(names[0], SysStringLen(names[0]));
				auto it = getSet.find(name);
				if (it == getSet.end())
					it = getSet.emplace(name, GetSet()).first;

				if (funcDesc->invkind == INVOKE_PROPERTYGET)
				{
					it->second.get.invkind = funcDesc->invkind;
					it->second.get.func = bindResult;
				}
				else
				{
					it->second.set.invkind = funcDesc->invkind;
					it->second.set.func = bindResult;
				}

				// Stash the function reference in the stack to protect it from the javascript
				// garbage collector.  The Get/Set map won't protect it because it allocates heap
				// memory to store the struct data.
				*reinterpret_cast<JsValueRef*>(alloca(sizeof(JsValueRef))) = bindResult;
			}
		}

		// find all of the getter/setters
		JsPropertyIdRef enumerableProp, getProp, setProp;
		if ((err = JsCreatePropertyId("enumerable", 10, &enumerableProp)) != JsNoError
			|| (err = JsCreatePropertyId("get", 3, &getProp)) != JsNoError
			|| (err = JsCreatePropertyId("set", 3, &setProp)) != JsNoError)
			return Throw(err, _T("creating get/set descriptor"));

		for (auto &gs : getSet)
		{
			// create the descriptor and add 'enumerable'
			JsValueRef propKey, propDesc;
			if ((err = JsPointerToString(gs.first.c_str(), gs.first.length(), &propKey)) != JsNoError
				|| (err = JsCreateObject(&propDesc)) != JsNoError
				|| (err = JsSetProperty(propDesc, enumerableProp, trueVal, true)) != JsNoError)
				return Throw(err, _T("initializing get/set descriptor"));

			// add 'get' if there's a getter
			if (gs.second.get.func != JS_INVALID_REFERENCE
				&& (err = JsSetProperty(propDesc, getProp, gs.second.get.func, true)) != JsNoError)
				return Throw(err, _T("creating getter descriptor"));

			// add 'set' if there's a setter
			if (gs.second.set.func != JS_INVALID_REFERENCE
				&& (err = JsSetProperty(propDesc, setProp, gs.second.set.func, true)) != JsNoError)
				return Throw(err, _T("creating setter descriptor"));

			// add the property
			bool ok;
			if ((err = JsObjectDefineProperty(proto, propKey, propDesc, &ok)) != JsNoError || !ok)
				return Throw(err, _T("binding get/set"));
		}
	}

	// create a new Javascript object based on the automation interface prototype
	JsValueRef jsobj;
	if ((err = CreateExternalObjectWithPrototype(jsobj, proto, new AutomationObjectData(disp))) != JsNoError)
		return Throw(err, _T("createAutomationObject: creating Javascript external object"));

	// return the new object
	return jsobj;
}

template<typename T, T VARIANTARG::*ele>
bool JavascriptEngine::MarshallAutomationNum(VARIANTARG &v, JsValueRef jsval)
{
	JsErrorCode err;
	JsValueRef numval;
	double d;
	if ((err = JsConvertValueToNumber(jsval, &numval)) != JsNoError
		|| (err = JsNumberToDouble(numval, &d)) != JsNoError)
	{
		JavascriptEngine::Get()->Throw(err, _T("Passing numeric argument to automation function"));
		return false;
	}

	v.*ele = static_cast<T>(d);
	return true;
}

// IDispatch type parser
bool JavascriptEngine::MarshallAutomationArg(VARIANTARG &v, JsValueRef jsval, ITypeInfo *typeInfo, TYPEDESC &desc)
{
	JsErrorCode err;
	HRESULT hr;
	auto ComErr = [this, &hr](const CHAR *where)
	{
		WindowsErrorMessage err(hr);
		Throw(MsgFmt(_T("invoking automation object method: %hs: %s"), where, err.Get()));
		return false;
	};

	// get the datatype of the javascript actual that we're converting to VARIANT
	JsValueType jstype;
	if ((err = JsGetValueType(jsval, &jstype)) != JsNoError)
	{
		Throw(err, _T("Getting argument value type"));
		return false;
	}

	// If we're converting to a user-defined type, decode the destination
	// type first so that we can determine what we're really converting to.
	if (desc.vt == VT_USERDEFINED)
	{
		HRESULT hr;
		auto ComErr = [&hr](const CHAR *msg)
		{
			WindowsErrorMessage werr(hr);
			JavascriptEngine::Get()->Throw(MsgFmt(_T("Getting automation type info: %hs: %s"), msg, werr.Get()));
			return L"";
		};

		// get the type info for the referenced user-defined type
		RefPtr<ITypeInfo> subInfo;
		if (!SUCCEEDED(hr = typeInfo->GetRefTypeInfo(desc.hreftype, &subInfo)))
			return ComErr("Getting referenced type info");

		// get the type attributes
		TYPEATTRHolder attr(subInfo);
		if (!SUCCEEDED(hr = subInfo->GetTypeAttr(&attr)))
			return ComErr("Getting referenced type attributes");

		// let's see what we have
		switch (attr->typekind)
		{
		case TKIND_ENUM:
			// Enum type.  The type attributes don't say what the underlying VT_ type
			// is, and I can't find any documentation about a standard type.  So far,
			// the ones I've encountered use VT_I4, but I've also seen Stack Overflow
			// questions saying that VT_I4 *doesn't* work for some interfaces.  So
			// maybe there is no standard and it's just up to the interface to decide
			// what it wants to use.  What seems safe is to iterate over the named
			// constants making up the enum, and use the VT_ type of the first one.
			{
				// assume VT_I4 if we don't have any constants to inspect
				TYPEDESC enumdesc;
				enumdesc.vt = VT_I4;

				// if we have any constants, get the first one and use its type
				if (attr->cVars != 0)
				{
					VARDESCHolder vardesc(subInfo);
					if (!SUCCEEDED(hr = subInfo->GetVarDesc(0, &vardesc)))
						return ComErr("getting struct member descriptor");

					// use its type
					if (vardesc->varkind == VAR_CONST && vardesc->lpvarValue != nullptr)
						enumdesc.vt = vardesc->lpvarValue->vt;
				}

				// now marshall the value as the target type
				return MarshallAutomationArg(v, jsval, subInfo, enumdesc);
			}

		case TKIND_RECORD:
			// struct type
			{
				// get the record type
				if (!SUCCEEDED(hr = GetRecordInfoFromTypeInfo(subInfo, &v.pRecInfo)))
					return ComErr("Getting Variant RECORD type info");

				// if the source value is a variant of the same type, copy it
				if (auto vo = VariantData::Recover<VariantData>(jsval, nullptr); vo != nullptr)
				{
					if (vo->v.vt == VT_USERDEFINED && v.pRecInfo->IsMatchingType(vo->v.pRecInfo))
					{
						if (!SUCCEEDED(hr = VariantCopy(&v, &vo->v)))
							return ComErr("Copying Variant RECORD type");

						return true;
					}
					else
					{
						Throw(_T("Variant RECORD parameter type mismatch"));
						return false;
					}
				}
				
				// we need a Javascript object as the source
				if (jstype != JsObject)
				{
					Throw(_T("Type mismatch for Variant RECORD parameter "));
					return false;
				}

				// get the size of the struct
				ULONG recSize;
				if (!SUCCEEDED(v.pRecInfo->GetSize(&recSize)))
					return ComErr("Getting user-define record size");

				// allocate a temporary copy of the struct
				void *tempRec = marshallerContext->Alloc(recSize);

				// use this as the result value
				v.vt = VT_USERDEFINED | VT_BYREF;
				v.pvRecord = tempRec;

				// marshall the individual fields
				for (USHORT i = 0; i < attr->cVars; ++i)
				{
					// get this member descriptor
					VARDESCHolder vardesc(subInfo);
					if (!SUCCEEDED(hr = subInfo->GetVarDesc(i, &vardesc)))
						return ComErr("getting struct member descriptor");

					// get the member name
					BString fieldName;
					UINT nNames;
					if (!SUCCEEDED(hr = subInfo->GetNames(vardesc->memid, &fieldName, 1, &nNames)))
						return ComErr("getting struct member name");

					// get the corresponding Javascript property
					JsValueRef jsPropKey, jsPropVal;
					if ((err = JsPointerToString(fieldName, SysStringLen(fieldName), &jsPropKey)) != JsNoError
						|| (err = JsObjectGetProperty(jsval, jsPropKey, &jsPropVal)) != JsNoError)
					{
						Throw(err, _T("Getting object property for Variant RECORD"));
						return false;
					}

					// convert it to a variant, recursively
					VARIANTARG vfield;
					if (!MarshallAutomationArg(vfield, jsPropVal, subInfo, vardesc->elemdescVar.tdesc))
						return false;

					// store the field
					v.pRecInfo->PutField(0, tempRec, fieldName, &vfield);
				}

				// success
				return true;
			}

		case TKIND_DISPATCH:
			// dispatch interface
			{
				if (auto dispobj = AutomationObjectData::Recover<AutomationObjectData>(jsval, nullptr); dispobj != nullptr)
				{
					v.vt = VT_DISPATCH;
					v.punkVal = dispobj->disp;
					return true;
				}
			}

		case TKIND_ALIAS:
			return MarshallAutomationArg(v, jsval, subInfo, attr->tdescAlias);

		default:
			// others aren't handled
			JavascriptEngine::ThrowSimple("Unimplemented user-defined parameter type in automation object interface");
			return L"";
		}
	}

	// Given a VARIANT value in, copy or convert to the desired type using
	// VARIANT coercion rules.
	auto FromVariant = [this, &v, &desc, &hr, &ComErr](VARIANT &vsrc)
	{
		// if the desired type is VARIANT, just copy it
		if (desc.vt == VT_VARIANT)
		{
			if (!SUCCEEDED(hr = VariantCopy(&v, &vsrc)))
				return ComErr("copying Variant to parameter slot");
		}
		else
		{
			// convert it to the desired variant type
			if (!SUCCEEDED(hr = VariantChangeType(&v, &vsrc, 0, desc.vt)))
				return ComErr("converting Variant to parameter type");
		}

		// success
		return true;
	};

	// If the source value is a Variant object, copy it or convert it using VARIANT
	// type coercion rules.  This allows callers to manage type conversion on the JS
	// side by creating a JS Variant and assigning a value to the desired VARIANT
	// subtype field.
	if (auto vo = VariantData::Recover<VariantData>(jsval, nullptr); vo != nullptr)
		return FromVariant(vo->v);

	// If the source value is a native data value, convert it to a variant
	// of the same native type, then coerce the variant to the desired type,
	// using VARIANT coercion rules.
	if (auto o = NativeTypeWrapper::Recover<NativeTypeWrapper>(jsval, nullptr); o != nullptr)
	{
		// convert the native value to VARIANT
		VARIANT nv;
		VariantData::Set(nv, jsval);

		// coerce the variant to the desired slot type
		return FromVariant(nv);
	}

	// convert from the javascript type
	switch (v.vt = desc.vt)
	{
	case VT_I2:
		return MarshallAutomationNum<SHORT, &VARIANTARG::iVal>(v, jsval);

	case VT_I4:
		return MarshallAutomationNum<LONG, &VARIANTARG::lVal>(v, jsval);

	case VT_R4:
		return MarshallAutomationNum<FLOAT, &VARIANTARG::fltVal>(v, jsval);

	case VT_R8:
		return MarshallAutomationNum<DOUBLE, &VARIANTARG::dblVal>(v, jsval);

	case VT_DATE:
		v.date = VariantData::JsDateToVariantDate(jsval);
		return true;

	case VT_BSTR:
		{
			JsValueRef strval;
			const wchar_t *p;
			size_t len;
			if ((err = JsConvertValueToString(jsval, &strval)) != JsNoError
				|| (err = JsStringToPointer(strval, &p, &len)) != JsNoError)
				return Throw(err, _T("Passing string argument to automation function")), false;

			if (len > static_cast<size_t>(UINT_MAX))
				return Throw(_T("String argument is too long to convert to BSTR for automation function")), false;

			v.bstrVal = SysAllocStringLen(p, static_cast<UINT>(len));
			return true;
		}

	case VT_DISPATCH:
		// The parameter calls for a dispatch interface 
		if (jstype == JsNull || jstype == JsUndefined)
		{
			// pass a null interface pointer
			v.vt = VT_DISPATCH;
			v.punkVal = nullptr;
			return true;
		}
		else if (jstype == JsFunction)
		{
			// Javascript function.  Wrap it in a simple IDispatch with the JS
			// function as its only member, at dispatch member ID 0.  VARIANT
			// doesn't have a "function pointer" type per se, so some automation
			// interfaces that need the equivalent of a callback function pointer
			// use a single-member IDispatch as a proxy.  It's a nice universal
			// way to represent a callback across language boundaries.  Because
			// our dispatch interface only contains a single member, we don't
			// need to provide any type information; the convention for a
			// callback proxy IDispatch object is to simply provide one member
			// function at member ID 0.
			class TestDispatch : public IDispatch, public RefCounted
			{
			public:
				TestDispatch(JsValueRef jsfunc) : jsfunc(jsfunc)
				{
					// keep the callback function alive as long as the interface
					// is around
					JsAddRef(jsfunc, nullptr);
				}

				~TestDispatch()
				{
					JsRelease(jsfunc, nullptr);
				}

				// caller's javascript function passed in as the argument
				JsValueRef jsfunc;

				STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj)
				{
					// we support IUnknown and IDispatch
					if (riid == IID_IDispatch || riid == IID_IUnknown)
					{
						*ppvObj = this;
						AddRef();
						return S_OK;
					}
					return E_NOINTERFACE;
				}

				STDMETHODIMP_(ULONG) AddRef() { return RefCounted::AddRef(); }
				STDMETHODIMP_(ULONG) Release() { return RefCounted::Release(); }

				STDMETHODIMP GetTypeInfoCount(UINT *pctinfo)
				{
					if (pctinfo == nullptr)
						return E_INVALIDARG;

					*pctinfo = 0;
					return S_OK;
				}
				STDMETHODIMP GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo) { return E_NOTIMPL; }
				STDMETHODIMP GetIDsOfNames(REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId) { return E_NOTIMPL; }
				STDMETHODIMP Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
				{
					// if no arguments were provided, set up a default empty argument list
					DISPPARAMS defaultParams = { NULL, NULL, 0, 0 };
					if (pDispParams == nullptr)
						pDispParams = &defaultParams;

					// return a Javascript error
					JsErrorCode err;
					auto js = Get();
					auto Error = [&err, &js, pExcepInfo]()
					{
						if (pExcepInfo != nullptr)
							pExcepInfo->bstrDescription = SysAllocString(js->JsErrorToString(err));

						js->Throw(err, _T("Callback via auto IDispatch"));

						return DISP_E_EXCEPTION;
					};


					// we support an unnamed member with dispid 0
					if (dispIdMember == 0)
					{
						// pass arguments to javascript as variants, plus a 'this' parameter
						unsigned int jsargc = pDispParams->cArgs + 1;
						JsValueRef *jsargv = static_cast<JsValueRef*>(alloca(jsargc * sizeof(JsValueRef)));
						jsargv[0] = js->GetUndefVal();
						for (unsigned int jsargi = 1, dispargi = pDispParams->cArgs - 1; jsargi < jsargc; ++jsargi, --dispargi)
						{
							if ((err = VariantData::CreateFromNative(&pDispParams->rgvarg[dispargi], jsargv[jsargi])) != JsNoError)
								return Error();
						}

						// invoke the javascript function
						JsValueRef jsresult;
						if ((err = JsCallFunction(jsfunc, jsargv, static_cast<unsigned short>(jsargc), &jsresult)) != JsNoError)
							return Error();

						// translate the result back to a variant
						if (pVarResult != nullptr)
							VariantData::CopyFromJavascript(pVarResult, jsresult);

						// success
						return S_OK;
					}

					return DISP_E_UNKNOWNINTERFACE;
				}
			};

			v.vt = VT_DISPATCH;
			v.pdispVal = new TestDispatch(jsval);

			return true;
		}
		else if (auto cp = COMImportData::Recover<COMImportData>(jsval, nullptr); cp != nullptr)
		{
			// We have a COM interface.  Make sure it supports IDispatch.
			IDispatch *idisp;
			if (cp->pUnknown != nullptr && SUCCEEDED(cp->pUnknown->QueryInterface(IID_PPV_ARGS(&idisp))))
			{
				v.vt = VT_DISPATCH;
				v.pdispVal = idisp;
				return true;
			}

			// no IDispatch available
			Throw(_T("COM interface does not support IDispatch"));
			return false;
		}

		// anything else is invalid
		Throw(_T("Invalid value for IDispatch argument"));
		return false;

	case VT_ERROR:
		return MarshallAutomationNum<SCODE, &VARIANTARG::scode>(v, jsval);

	case VT_BOOL:
		{
			JsValueRef boolval;
			bool b;
			if ((err = JsConvertValueToBoolean(jsval, &boolval)) != JsNoError
				|| (err = JsBooleanToBool(boolval, &b)) != JsNoError)
				return Throw(err, _T("Passing boolean argument to automation function")), false;

			v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
			return true;
		}

	case VT_VARIANT:
		// variant requested - use the default conversion
		VariantData::CopyFromJavascript(&v, jsval);
		return true;
		
	case VT_I1:
		return MarshallAutomationNum<CHAR, &VARIANTARG::cVal>(v, jsval);

	case VT_UI1:
		return MarshallAutomationNum<BYTE, &VARIANTARG::bVal>(v, jsval);

	case VT_UI2:
		return MarshallAutomationNum<USHORT, &VARIANTARG::uiVal>(v, jsval);

	case VT_UI4:
		return MarshallAutomationNum<ULONG, &VARIANTARG::ulVal>(v, jsval);

	case VT_INT:
		return MarshallAutomationNum<INT, &VARIANTARG::intVal>(v, jsval);

	case VT_UINT:
		return MarshallAutomationNum<UINT, &VARIANTARG::uintVal>(v, jsval);

	case VT_VOID:
		return true;

	case VT_HRESULT:
		return MarshallAutomationNum<SCODE, &VARIANTARG::scode>(v, jsval);

	case VT_PTR:
		Throw(_T("pointers are not implemented"));
		return false;

	case VT_SAFEARRAY:
		Throw(_T("arrays are not implemented"));
		return false;

	default:
		JavascriptEngine::ThrowSimple("Unhandled type in automation object interface");
		return L"";
	}
}

//
// Invoke an automation IDispatch method
//
// This is called from a js wrapper function that we bind for each method
// in an automation object.  Called as:
//
//   _invokeAutomationMethod(extobj, funcIndex, dispType, ...args);
//
// extobj = AutomationObjectData external object, containing the 
//   IDispatch interface pointer to be invoked
//
// funcIndex = int, function index in dispatch interface
//
// dispType = int, dispatch type (DISPATCH_METHOD, DISPATCH_PROPERTYGET, DISPATCH_PROPERTYPUT)
//
// ...args = the Javascript caller's arguments
// 
JsValueRef CALLBACK JavascriptEngine::InvokeAutomationMethod(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	auto js = static_cast<JavascriptEngine*>(ctx);
	JsErrorCode err;
	HRESULT hr;
	auto ComErr = [js, &hr](const CHAR *where)
	{
		WindowsErrorMessage err(hr);
		return js->Throw(MsgFmt(_T("invoking automation object method: %hs: %s"), where, err.Get()));
	};

	// set up a temporary memory allocator context
	MarshallerContext marshallCtx;

	// make sure the base arguments are present
	unsigned short jsargi = 1;
	if (argc < 3)
		return js->Throw(_T("_invokeAutomationMethod: missing arguments"));

	// get the IDispatch wrapper
	auto obj = AutomationObjectData::Recover<AutomationObjectData>(argv[jsargi++], _T("_invokeAutomationMethod"));
	if (obj == nullptr)
		return js->undefVal;

	// get the function index
	int funcIndex;
	if ((err = JsNumberToInt(argv[jsargi++], &funcIndex)) != JsNoError)
		return js->Throw(err, _T("_invokeAutomationMethod: invalid member ID"));

	// get the dispatch type
	int dispType;
	if ((err = JsNumberToInt(argv[jsargi++], &dispType)) != JsNoError)
		return js->Throw(err, _T("_invokeAutomationMethod: invalid dispatch type"));

	// get the type info
	RefPtr<ITypeInfo> typeInfo;
	if (!SUCCEEDED(hr = obj->disp->GetTypeInfo(0, LOCALE_USER_DEFAULT, &typeInfo)))
		return ComErr("getting type information");

	// get the type attributes
	TYPEATTRHolder typeAttr(typeInfo);
	if (!SUCCEEDED(hr = typeInfo->GetTypeAttr(&typeAttr)))
		return ComErr("getting type attributes");

	// get the function descriptor
	FUNCDESCHolder funcDesc(typeInfo);
	if (!SUCCEEDED(hr = typeInfo->GetFuncDesc(static_cast<UINT>(funcIndex), &funcDesc)))
		return ComErr("getting function descriptor");

	// Allocate space for the VARIANTARG parameter array.  This will
	// contain the arguments that we actually pass to Javascript.
	VARIANTARGArray va(funcDesc->cParams);

	// Figure the number of FIXED arguments in the va array (that is,
	// excluding any varargs).  If we don't have varargs, this is simply
	// the total array size.  If we do have varargs, this is one less
	// than the total array size, because the last slot is the special
	// SAFEARRAY slot.
	SHORT vargcFixed = static_cast<SHORT>(va.n);
	if (funcDesc->cParamsOpt == -1)
	{
		// We have varargs, so the last va slot is actually the SAFEARRAY
		// containing the varargs.  Remove it from the fixed argument count.
		vargcFixed -= 1;

		// Figure the the number of slots needed for the varargs array.  We
		// need one slot for each javascript actual past the last fixed 
		// parameter slot.
		int nActualArgs = argc - jsargi;
		int nExtraArgs = max(0, nActualArgs - vargcFixed);

		// allocate the array
		SAFEARRAYBOUND bounds;
		bounds.cElements = static_cast<ULONG>(nExtraArgs);
		bounds.lLbound = 0;
		va.v[vargcFixed].vt = VT_ARRAY | VT_VARIANT;
		va.v[vargcFixed].parray = SafeArrayCreate(VT_VARIANT, 1, &bounds);
	}

	// populate the arguments
	unsigned short firstJsArg = jsargi;
	SHORT vargc;
	for (vargc = 0; vargc < vargcFixed; ++vargc, ++jsargi)
	{
		// get this parameter descriptor
		auto &desc = funcDesc->lprgelemdescParam[vargc];

		// the argument array is built in reverse order
		VARIANTARG &vdest = va.v[va.n - vargc - 1];

		// check if there's a javascript actual for this slot
		if (jsargi < argc)
		{
			// copy it into the vector only if it's IN argument only
			if ((desc.paramdesc.wParamFlags & PARAMFLAG_FIN) != 0
				&& !js->MarshallAutomationArg(vdest, argv[jsargi], typeInfo, desc.tdesc))
				return js->undefVal;
		}
		else
		{
			// We're past the last javascript actual.
			//
			// - If there's a default value, use the default
			// - Otherwise, if the parameter is optional, pass "missing"
			// - Otherwise throw an error
			//
			if ((desc.paramdesc.wParamFlags & PARAMFLAG_FHASDEFAULT) != 0)
			{
				// default value provided - pass it
				VariantCopy(&vdest, &desc.paramdesc.pparamdescex->varDefaultValue);
			}
			else if ((desc.paramdesc.wParamFlags & PARAMFLAG_FOPT) != 0)
			{
				// optional - pass "missing"
				vdest.vt = VT_ERROR;
				vdest.scode = DISP_E_PARAMNOTFOUND;
			}
			else
			{
				// this argument was required, so this is an error
				return js->Throw(_T("Not enough arguments"));
			}
		}
	}

	// If we have a varargs slot, and we haven't exhausted all of the 
	// Javascript actuals, copy the remaining Javascript actuals to the
	// varargs array.
	if (funcDesc->cParamsOpt == -1)
	{
		// set up a generic VARIANT type descriptor for the varargs slots
		TYPEDESC tdesc;
		tdesc.vt = VT_VARIANT;

		// lock the SAFEARRAY data
		auto psa = va.v[0].parray;
		if (!SUCCEEDED(hr = SafeArrayLock(psa)))
			return ComErr("locking varargs safearray");

		// get the raw VARIANT array
		VARIANT *psav = static_cast<VARIANT*>(psa->pvData);

		// add each additional argument to the varargs array
		for (; jsargi < argc; ++jsargi, ++psav)
		{
			if (!js->MarshallAutomationArg(*psav, argv[jsargi], typeInfo, tdesc))
				return js->undefVal;
		}

		// unlock the array
		SafeArrayUnlock(psa);
	}

	// set up the argument DISPPARAMS
	DISPPARAMS params = { va.v, NULL, static_cast<UINT>(vargc), 0 };

	// If we're calling a "property put" method (that is, we're assigning a new
	// value to a property variable of the dispatch interface), OLE requires us
	// to provide the value as a named property, with name DISPID_PROPERTYPUT.
	DISPID dispidNamed = DISPID_PROPERTYPUT;
	if ((dispType & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) != 0)
	{
		params.cNamedArgs = 1;
		params.rgdispidNamedArgs = &dispidNamed;
	}

	// call the function
	VARIANTEx result;
	EXCEPINFOEx exc;
	hr = obj->disp->Invoke(funcDesc->memid, IID_NULL, LOCALE_USER_DEFAULT, static_cast<WORD>(dispType), &params, &result, &exc, NULL);

	// check the results
	if (SUCCEEDED(hr))
	{
		// Success
		
		// Copy any OUT parameters back to the corresponding Javascript Variant 
		// argument variables
		for (vargc = 0, jsargi = firstJsArg; vargc < vargcFixed; ++vargc, ++jsargi)
		{
			// get this parameter descriptor
			auto &desc = funcDesc->lprgelemdescParam[vargc];

			// stop if we're past the last javascript argument
			if (jsargi >= argc)
				break;
	
			// if this is an OUT parameter, copy it back
			if ((desc.paramdesc.wParamFlags & PARAMFLAG_FOUT) != 0)
			{
				// We can only copy OUT parameters back to Variant objects.  For
				// anything that's not a variant, simply ignore the OUT result.
				if (auto vo = VariantData::Recover<VariantData>(argv[jsargi], nullptr); vo != nullptr)
					VariantCopy(&vo->v, &va.v[vargc]);
			}
		}

		// translate and return the return value
		return VariantData::Get(result);
	}

	// check for an exception
	if (hr == DISP_E_EXCEPTION)
	{
		if (exc.bstrDescription != nullptr)
			js->Throw(exc.bstrDescription);
		else
		{
			WindowsErrorMessage werr(exc.scode);
			js->Throw(MsgFmt(_T("%s (system error code %08lx)"), werr.Get(), werr.GetCode()));
		}
		return js->undefVal;
	}

	// other error
	WindowsErrorMessage werr(hr);
	js->Throw(MsgFmt(_T("IDispatch::Invoke failed: %s (%08lx)"), werr.Get(), werr.GetCode()));
	return js->undefVal;
}
