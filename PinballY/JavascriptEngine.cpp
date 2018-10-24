// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//

#include "stdafx.h"
#include "JavascriptEngine.h"
#include "LogFile.h"
#include "../Utilities/FileUtil.h"

#pragma comment(lib, "ChakraCore.lib")

double JavascriptEngine::Task::nextId = 1.0;

JavascriptEngine::JavascriptEngine() :
	inited(false),
	nextTaskID(1.0)
{
}

bool JavascriptEngine::Init(ErrorHandler &eh)
{
	JsErrorCode err;
	auto Error = [&err, &eh](const TCHAR *where)
	{
		MsgFmt details(_T("%s failed: %s"), where, JsErrorToString(err));
		eh.SysError(LoadStringT(IDS_ERR_JSINIT), details);
		LogFile::Get()->Write(LogFile::JSLogging, _T(". Javascript engine initialization error: %s\n"), details.Get());
		return false;
	};

	// Create the runtime object - this represents a thread of execution,
	// heap, garbage collector, and compiler.
	if ((err = JsCreateRuntime(JsRuntimeAttributeEnableExperimentalFeatures, nullptr, &runtime)) != JsNoError)
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

	// success
	inited = true;
	return true;
}

JavascriptEngine::~JavascriptEngine()
{
	// Explicitly clear the task queue.  Tasks can hold references to
	// engine objects, so we need to make sure to delete the task queue
	// items while the engine is still valid.
	taskQueue.clear();
	
	// shut down the javascript runtime
	JsSetCurrentContext(JS_INVALID_REFERENCE);
	JsDisposeRuntime(runtime);
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
	if ((err = FetchImportedModuleCommon(nullptr, L"", TCHARToWide(url), &record)) != JsNoError)
		return Error(_T("Fetching main module"));

	// success
	return true;
}

bool JavascriptEngine::EvalScript(const WCHAR *scriptText, const TCHAR *url, JsValueRef *returnVal, ErrorHandler &eh)
{
	JsErrorCode err;
	auto Error = [&err, &eh](const TCHAR *where)
	{
		MsgFmt details(_T("%s failed: %s"), where, JsErrorToString(err));
		eh.SysError(LoadStringT(IDS_ERR_JSRUN), details);
		LogFile::Get()->Write(LogFile::JSLogging, _T("[Javascript] Script error: %s\n"), details.Get());
		return false;
	};

	// create a cookie to represent the script
	sourceCookies.emplace_back(this, TCHARToWide(url));
	const SourceCookie *cookie = &sourceCookies.back();

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

bool JavascriptEngine::FireEvent(const TCHAR *scriptText, const TCHAR *url)
{
	// suppress errors
	SilentErrorHandler eh;

	// evaluate the script
	JsValueRef result = JS_INVALID_REFERENCE;
	if (!EvalScript(TCHARToWCHAR(scriptText), url, &result, eh))
	{
		// script failed - allow the system handling to proceed by default
		return true;
	}

	// convert the return value to bool
	JsValueRef boolResult;
	bool b;
	if (JsConvertValueToBoolean(result, &boolResult) != JsNoError
		|| JsBooleanToBool(boolResult, &b) != JsNoError)
	{
		// invalid type returned - allow the system handling to proceed by default
		return true;
	}

	// return the result from the script
	return b;
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
	if ((err = GetProp(msg, exc, "message", where)) != JsNoError)
		return ExcError(_T("exception.message"));

	// report the scripting error through the error handler, if provided
	if (eh != nullptr)
		eh->Error(MsgFmt(IDS_ERR_JSEXC, msg.c_str(), url.c_str(), lineno + 1, colno + 1));

	// log the error to the log file
	LogFile::Get()->Group(LogFile::JSLogging);
	LogFile::Get()->Write(LogFile::JSLogging,
		_T("[Javascript] Uncaught exception: %s\n")
		_T("    In %s (line %d, col %d)\n")
		_T("    Source code: %s\n\n"),
		msg.c_str(), url.c_str(), lineno + 1, colno + 1, source.c_str());

	// success
	return JsNoError;
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

bool JavascriptEngine::DefineGlobalFunc(const CHAR *name, NativeFunctionBinderBase *func, ErrorHandler &eh)
{
	JsErrorCode err;
	auto Error = [name, &err, &eh](const TCHAR *where)
	{
		eh.SysError(LoadStringT(IDS_ERR_JSINITHOST), MsgFmt(_T("Setting up native function callback for global.%hs: %s failed: %s"),
			name, where, JsErrorToString(err)));
		return false;
	};

	// get the global object
	JsValueRef global;
	if ((err = JsGetGlobalObject(&global)) != JsNoError)
		return Error(_T("JsGetGlobalObject"));

	// define the object property
	return DefineObjPropFunc(global, "global", name, func, eh);
}

bool JavascriptEngine::DefineObjPropFunc(JsValueRef obj, const CHAR *objName, const CHAR *propName, NativeFunctionBinderBase *func, ErrorHandler &eh)
{
	// set the name in the binder object
	func->callbackName = propName;

	// define the property
	return DefineObjPropFunc(obj, objName, propName, &NativeFunctionBinderBase::SInvoke, func, eh);
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

	// create the native function wrapper
	JsValueRef funcval;
	if ((err = JsCreateFunction(func, context, &funcval)) != JsNoError)
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
	taskQueue.emplace_back(task);
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

void JavascriptEngine::RunTasks() 
{
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
		if (task->cancelled)
		{
			// The task has been canceled.  Simply delete it from the
			// queue without invoking it.
			keep = false;
		}
		else if (GetTickCount64() >= task->readyTime)
		{
			// The task is ready to run.  Execute it.
			keep = task->Execute(this);
		}

		// If we're not keeping the task, remove it
		if (!keep)
			taskQueue.erase(it);

		// advance to the next task
		it = nxt;
	}
}

bool JavascriptEngine::EventTask::Execute(JavascriptEngine *js)
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
		js->LogAndClearException();

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

	// get 'self' from the host info
	auto self = hostInfo->self;

	// call the common handler
	return self->FetchImportedModuleCommon(referencingModule, hostInfo->path, specifier, dependentModuleRecord);
}

JsErrorCode CHAKRA_CALLBACK JavascriptEngine::FetchImportedModuleFromScript(
	JsSourceContext referencingSourceContext,
	JsValueRef specifier,
	JsModuleRecord *dependentModuleRecord)
{
	// get the source cookie struct
	const SourceCookie *cookie = reinterpret_cast<SourceCookie*>(referencingSourceContext);
	auto self = cookie->self;

	// call the common handler
	return self->FetchImportedModuleCommon(nullptr, cookie->file, specifier, dependentModuleRecord);
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

	// get the lower-case version for the table key
	WSTRING key = fname;
	std::transform(key.begin(), key.end(), key.begin(), ::towlower);

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

	// set the URL on the module record, so that it can be shown in error messages
	JsValueRef url;
	JsPointerToString(specifier.c_str(), specifier.length(), &url);
	JsSetModuleHostInfo(*dependentModuleRecord, JsModuleHostInfo_Url, url);

	// store the module record in our table
	auto it = modules.emplace(std::piecewise_construct,
		std::forward_as_tuple(key),
		std::forward_as_tuple(this, fname, *dependentModuleRecord));

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
	if (exceptionVar != nullptr)
	{
		// set this exception in the engine
		JsSetException(exceptionVar);

		// get the path from the module info
		LogFile::Get()->Write(LogFile::JSLogging, _T("[Javascript] NotifyModuleReadyCallback exception: module %s\n"), hostInfo->path.c_str());

		// log the exception data
		hostInfo->self->LogAndClearException();
	}
	else
	{
		// queue a task to load the module
		hostInfo->self->AddTask(new ModuleEvalTask(referencingModule, hostInfo->path.c_str()));
	}

	// success
	return JsNoError;
}

bool JavascriptEngine::ModuleParseTask::Execute(JavascriptEngine *js)
{
	// load the script
	LogFile::Get()->Write(LogFile::JSLogging, _T("[Javscript] Loading module from file %ws\n"), path.c_str());
	long len;
	LogFileErrorHandler eh(_T(". "));
	std::unique_ptr<WCHAR> contents(ReadFileAsWStr(WSTRINGToTSTRING(path).c_str(), eh, len, 0));
	if (contents == nullptr)
	{
		LogFile::Get()->Write(LogFile::JSLogging, _T(". Error loading %ws\n"), path.c_str());
		return false;
	}

	// Parse the source.  Note that the source memory is provided as BYTEs,
	// but the file length we have is in WCHARs, so we need to multiply for
	// the parser's sake.
	JsValueRef exc = JS_INVALID_REFERENCE;
	JsErrorCode err = JsParseModuleSource(module, JS_SOURCE_CONTEXT_NONE, 
		(BYTE*)contents.get(), len*sizeof(WCHAR), 
		JsParseModuleSourceFlags_DataIsUTF16LE, &exc);

	// check for errors
	if (exc != JS_INVALID_REFERENCE)
		JsSetException(exc);
	else if (err != JsNoError)
		js->Throw(err, _T("ModuleParseTask"));

	// this is a one shot - don't reschedule
	return false;
}

bool JavascriptEngine::ModuleEvalTask::Execute(JavascriptEngine *js)
{
	// evaluate the module
	JsValueRef result;
	JsErrorCode err = JsModuleEvaluation(module, &result);

	// log any error
	if (err == JsErrorScriptException || err == JsErrorScriptCompile)
	{
		LogFile::Get()->Write(LogFile::JSLogging, _T("[Javascript] Error executing module %s\n"), path.c_str());
		js->LogAndClearException();
	}
	else if (err != JsNoError)
		LogFile::Get()->Write(LogFile::JSLogging, _T("[Javascript] Module evaluation failed for %s: %s\n"), path.c_str(), JsErrorToString(err));

	// this is a one shot - don't reschedule
	return false;
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

	// return the result
	filename = path;
	return JsNoError;
}

// -----------------------------------------------------------------------
//
// DllImport implementation
//

bool JavascriptEngine::BindDllImportCallbacks(const CHAR *className, ErrorHandler &eh)
{
	JsErrorCode err;
	const TCHAR *subwhere = nullptr;
	auto Error = [&err, className, &eh](const TCHAR *where)
	{
		eh.SysError(LoadStringT(IDS_ERR_JSINIT),
			MsgFmt(_T("Binding DLL import callbacks: %s: %s"), where, JsErrorToString(err)));
		return false;
	};

	// get the global object
	JsValueRef global;
	if ((err = JsGetGlobalObject(&global)) != JsNoError)
		return Error(_T("JsGetGlobalObject"));

	// look up the class in the global object
	JsValueRef classObj;
	if ((err = GetProp(classObj, global, className, subwhere)) != JsNoError)
		return Error(subwhere);

	// get its prototype
	JsValueRef proto;
	if ((err = GetProp(proto, classObj, "prototype", subwhere)) != JsNoError)
		return Error(subwhere);

	// set up the bindings
	return DefineObjPropFunc(proto, className, "_bind", &JavascriptEngine::DllImportBind, this, eh)
		&& DefineObjPropFunc(proto, className, "_call", &JavascriptEngine::DllImportCall, this, eh);
}

// DllImportBind - this is set up in the Javascript as DllImport.prototype.bind_(),
// an internal method that bind() calls to get the native function pointer.  Javascript
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
			Throw(MsgFmt(_T("DllImport.bind(): Error loading DLL %s: %s"), dllName.c_str(), winErr.Get()));
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
		Throw(MsgFmt(_T("DllImport.bind(): Error binding %s!%s: %s"), dllName.c_str(), funcName.c_str(), winErr.Get()));
		return nullVal;
	}

	// create an external object with a DllImportData object to represent the result
	JsValueRef ret;
	if (JsErrorCode err = JsCreateExternalObject(new DllImportData(addr, dllName, funcName), &DllImportData::Finalize, &ret); err != JsNoError)
	{
		Throw(err, _T("DllImport.bind()"));
		return nullVal;
	}

	// return the result
	return ret;
}

// assembler glue functions for DLL calls
#if defined(_M_X64)
extern "C" UINT64 dll_call_glue64(FARPROC func, const void *args, size_t nArgBytes);
#endif


// DllImportCall is set up in the Javascript as DllImport.prototype._call(), an 
// internal method of the DllImport object.  When the Javascript caller calls the
// lambda returned from bind(), the lambda invokes DllImport.prototype.call(),
// which in turn invokes _call() (which is how we're exposed to Javascript) as:
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
// The DllImport 'this' argument is important because it's where the Javascript caller
// will have registered any struct type declarations it wishes to define as part of the
// DLL interface.  If any of our arguments contains a struct reference, we'll need to
// query the DllImport object to get the struct layouts for marshalling to the native
// callee.  We can use this to marshall between Javascript objects and native struct
// layouts, so that callers can call API functions that take struct references as input
// or output parameters.
//
JsValueRef JavascriptEngine::DllImportCall(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsErrorCode err;

	// the context is our JavascriptEngine object
	auto js = static_cast<JavascriptEngine*>(ctx);

	// we need at least two arguments (
	if (argc < 3)
		return js->Throw(_T("DllImport.call(): missing arguments"));

	// get the javascript this pointer
	int ai = 0;
	auto jsthis = argv[ai++];

	// get the native function object
	auto func = DllImportData::Recover<DllImportData>(argv[ai++], _T("DllImport.call()"));
	auto funcPtr = func->procAddr;

	// get the function signature, as a string
	const WCHAR *sig;
	size_t sigLen;
	if ((err = JsStringToPointer(argv[ai++], &sig, &sigLen)) != JsNoError)
		return js->Throw(err, _T("DllImport.call()"));

	// figure the signature end pointer, for limiting traversals
	const WCHAR *sigEnd = sig + sigLen;

	// the rest of the Javascript arguments are the arguments to pass to the DLL
	int firstDllArg = ai;

	// Get the calling convention.  This is the first letter of the first token:
	// S[__stdcall], C[__cdecl], F[__fastcall], T[__thiscall], V[__vectorcall]
	WCHAR callConv = *sig++;

	// advance to the next argument in the signature, skipping the rest of the current one
	auto NextArg = [sigEnd](const WCHAR *p)
	{
		// skip to the next space
		for (; p < sigEnd && *p != ' '; ++p);

		// skip to the next non-space
		for (; p < sigEnd && *p == ' '; ++p);

		// return the result
		return p;
	};

	// Parameters the current CPU architecture calling conventions
	const size_t argSlotSize = IF_32_64(4, 8);   // size in bytes of a generic argument slot
	const size_t stackAlign = IF_32_64(4, 16);   // stack pointer alignment size in bytes
	const size_t minArgSlots = IF_32_64(0, 4);   // minimum stack slots allocated for arguments

	// Traverse the function signature string to figure the stack size requirements.
	// The first element is the return type, so skip that and go to the first argument.
	int nSlots = 0;
	for (const WCHAR *p = NextArg(sig); p < sigEnd; p = NextArg(p))
	{
		switch (*p)
		{
		case '*':  // pointer to another type
 		case 'b':  // bool
		case 'c':  // int8
		case 'C':  // uint8
		case 's':  // int16
		case 'S':  // uint16
		case 'i':  // int
		case 'I':  // unsigned int
		case 'f':  // float
		case 'P':  // INT_PTR and related int/pointer types
		case 't':  // CHAR string buffer
		case 'T':  // WCHAR string buffer
			// These types all require one slot on all architectures.  For types
			// shorter than the slot size, the value is widened to the slot size
			// right-aligned (that is, the value is in the low-order bytes).
			nSlots += 1;
			break;

		case 'l':  // int64
		case 'L':  // uint64
		case 'd':  // double
			// 64-bit types takes two slots on x86, 1 on x64
			nSlots += IF_32_64(2, 1);

		case 'v':  // void is invalid as an argument type
			return js->Throw(_T("DllImport.call(): 'void' is not valid as a parameter type"));

		case '@':  // inline struct - not allowed for arguments
			return js->Throw(_T("DllImport.call(): struct by value parameters are not supported (pointer types required)"));
		}
	}

	// Figure the required native argument array size
	size_t argArraySize = max(nSlots, minArgSlots) * argSlotSize;

	// round up to the next higher alignment boundary
	argArraySize = ((argArraySize + stackAlign - 1)/stackAlign) * stackAlign;

	// allocate the argument array
	typedef IF_32_64(UINT32, UINT64) arg_t;
	arg_t *argArray = static_cast<arg_t*>(alloca(argArraySize));

	// current slot we're filling
	arg_t *slot = argArray;

	// get a numeric value as a double 
	auto GetDouble = [js, argv, &ai]()
	{
		// convert to numeric if necessary
		JsErrorCode err;
		JsValueRef numVal;
		if ((err = JsConvertValueToNumber(argv[ai], &numVal)) != JsNoError)
			js->Throw(err, _T("DllImport.call(): marshalling integer argument"));

		// retrieve the double value 
		double d;
		if ((err = JsNumberToDouble(numVal, &d)) != JsNoError)
			js->Throw(err, _T("DllImport.call(): marshalling integer argument"));

		// return it
		return d;
	};

	// get a float value
	auto GetFloat = [js, &GetDouble]()
	{
		// get the double value from javascript
		double d = GetDouble();

		// check the range
		if (d < FLT_MIN || d > FLT_MAX)
			js->Throw(_T("DllImport.call(): single-precision float argument value out of range"));

		// return it
		return static_cast<float>(d);
	};

	// get an integer value (up to 32 bits)
	auto GetInt = [js, &GetDouble](double minVal, double maxVal)
	{
		// get the original double value from javascript
		double d = GetDouble();

		// check the range
		if (d < minVal || d > maxVal)
			js->Throw(_T("DllImport.call(): integer argument value out of range"));

		// return it as a double, so that the caller can do the appropriate sign
		// extension in the conversion process
		return d;
	};

	// get a 64-bit integer value
	auto GetInt64 = [js, &ai, &argv, &GetDouble](bool isSigned) -> INT64
	{
		// check the value type
		JsErrorCode err;
		JsValueType t;
		if ((err = JsGetValueType(argv[ai], &t)) != JsNoError)
		{
			js->Throw(err, _T("DllImport.call(): JsGetValueType failed converting 64-bit integer argument"));
			return 0;
		}

		// if it's a numeric value, convert it from the JS double representation
		if (t == JsNumber)
		{
			// Numeric type - get the double value
			double d = GetDouble();

			// check the range
			if (isSigned ? (d < (double)INT64_MIN || d >(double)INT64_MAX) : (d < 0 || d >(double)UINT64_MAX))
			{
				js->Throw(err, _T("DllImport.call(): 64-bit integer argument out of range"));
				return 0;
			}

			// Return the value reinterpreted as a 
			if (isSigned)
				return static_cast<INT64>(static_cast<UINT64>(d));
			else
				return static_cast<INT64>(d);
		}

		// otherwise, interpret it as a string value
		JsValueRef strval;
		if ((err = JsConvertValueToString(argv[ai], &strval)) != JsNoError)
		{
			js->Throw(err, _T("DllImport.call(): converting 64-bit integer argument value to string"));
			return 0;
		}

		// get the string
		const WCHAR *p;
		size_t len;
		if ((err = JsStringToPointer(strval, &p, &len)) != JsNoError)
		{
			js->Throw(err, _T("DllImport.call(): retrieving string value for 64-bit integer argument"));
			return 0;
		}
		WSTRING str(p, len);

		// check for a 0x prefix
		int radix = 10;
		for (p = str.c_str(); iswspace(*p); ++p);
		if (p[0] == '0' && p[1] == 'x')
		{
			radix = 16;
			p += 2;
		}

		// parse the string
		if (isSigned)
			return _wcstoi64(p, nullptr, radix);
		else
			return static_cast<INT64>(_wcstoui64(p, nullptr, radix));
	};

	// Build the argument array
	ai = firstDllArg;
	for (const WCHAR *p = NextArg(sig); p < sigEnd; p = NextArg(p), ++slot, ++ai)
	{
		// zero the slot
		*slot = 0;

		// check for pointers
		if (*p == '*')
		{
		}

		switch (*p)
		{
		case 'b':  // bool
			*reinterpret_cast<bool*>(slot) = static_cast<bool>(GetDouble());
			break;

		case 'c':  // int8
			*reinterpret_cast<INT8*>(slot) = static_cast<INT8>(GetInt(INT8_MIN, INT8_MAX));
			break;

		case 'C':  // uint8
			*reinterpret_cast<UINT8*>(slot) = static_cast<UINT8>(GetInt(0, UINT8_MAX));
			break;

		case 's':  // int16
			*reinterpret_cast<INT16*>(slot) = static_cast<INT16>(GetInt(INT16_MIN, INT16_MAX));
			break;

		case 'S':  // uint16
			*reinterpret_cast<UINT16*>(slot) = static_cast<UINT16>(GetInt(0, UINT16_MAX));
			break;

		case 'i':  // int32
			*reinterpret_cast<INT32*>(slot) = static_cast<INT32>(GetInt(INT32_MIN, INT32_MAX));
			break;

		case 'I':  // unit32
			*reinterpret_cast<UINT32*>(slot) = static_cast<UINT32>(GetInt(0, UINT32_MAX));
			break;

		case 'f':  // float
			*reinterpret_cast<float*>(slot) = GetFloat();
			break;

		case 'l':  // int64
			*reinterpret_cast<INT64*>(slot) = GetInt64(true);
			slot += IF_32_64(1, 0);  // 64-bit values take two slots on x86
			break;

		case 'L':  // uint64
			*reinterpret_cast<UINT64*>(slot) = GetInt64(false);
			slot += IF_32_64(1, 0);  // 64-bit values take two slots on x86
			break;

		case 'd':  // double
			*reinterpret_cast<double*>(slot) = GetDouble();
			slot += IF_32_64(1, 0);  // 64-bit values take two slots on x86
			break;

		case 'P':  // INT_PTR and related int/pointer types
			// $$$ TO DO
			*reinterpret_cast<INT_PTR*>(slot) = 0;
			break;

		case 't':  // CHAR string buffer
			// $$$ TO DO
			break;

		case 'T':  // WCHAR string buffer
			// $$$ TO DO
			*reinterpret_cast<WCHAR**>(slot) = L"Hello!";
			break;
		}

		// stop on any exception
		bool exc;
		if (JsHasException(&exc) != JsNoError || exc)
			return js->undefVal;
	}

	// all architectures have provisions for 64-bit return values
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
			shr ecx, 2          ; divide by 4 to get DWORD size
			rep movsd
		
			; call the function
			mov eax, funcPtr
			call eax

			; store the result
			mov dword ptr rawret, eax
			mov dword ptr rawret[4], edx

			; caller pops arguments if using __cdecl
			cmp callConv, 'C'
			jne $1

			add esp, argArraySize
		$1:
		}
		break;

	case 'F':
		return js->Throw(_T("DllImport.call(): __fastcall calling convention not supported"));

	case 'T':
		return js->Throw(_T("DllImport.call(): __thiscall calling convention not supported"));

	case 'V':
		return js->Throw(_T("DllImport.call(): __vectorcall calling convention not supported"));

	default:
		return js->Throw(_T("DllImport.call(): unknown calling convention in function signature"));
	}


	// In x86 __stdcall (used by most DLL entrypoints), the callee removes 
	// arguments, so the call is now completed.

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
	rawret = dll_call_glue64(funcPtr, argArray, argArraySize);

#else
#error Processor architecture not supported.  Add the appropriate code here to build for this target
#endif

	// Marshall the result back to Javascript
	const WCHAR *p = sig;
	JsValueRef retval = js->undefVal;
	err = JsNoError;
	switch (*p)
	{
	case '*':
		// $$$ pointer - to do
		break;

	case 'b':  // bool
		err = JsBoolToBoolean(static_cast<bool>(*reinterpret_cast<const int*>(&rawret)), &retval);
		break;

	case 'c':  // int8
		err = JsIntToNumber(*reinterpret_cast<const INT8*>(&rawret), &retval);
		break;

	case 'C':  // uint8
		err = JsIntToNumber(*reinterpret_cast<const UINT8*>(&rawret), &retval);
		break;

	case 's':  // int16
		err = JsIntToNumber(*reinterpret_cast<const INT16*>(&rawret), &retval);
		break;

	case 'S':  // uint16
		err = JsIntToNumber(*reinterpret_cast<const UINT16*>(&rawret), &retval);
		break;

	case 'i':  // int32
		err = JsIntToNumber(*reinterpret_cast<const INT32*>(&rawret), &retval);
		break;

	case 'I':  // unit32
		err = JsDoubleToNumber(*reinterpret_cast<const UINT32*>(&rawret), &retval);
		break;

	case 'f':  // float
		err = JsDoubleToNumber(*reinterpret_cast<const float*>(&rawret), &retval);
		break;

	case 'l':  // int64
		err = JsDoubleToNumber(static_cast<double>(*reinterpret_cast<INT64*>(&rawret)), &retval);
		break;

	case 'L':  // uint64
		err = JsDoubleToNumber(static_cast<double>(*reinterpret_cast<UINT64*>(&rawret)), &retval);
		break;

	case 'd':  // double
		err = JsDoubleToNumber(*reinterpret_cast<double*>(&rawret), &retval);
		break;

	case 'P':  // INT_PTR and related int/pointer types
		// $$$ TO DO
		break;

	case 't':  // CHAR string buffer
		// $$$ TO DO
		break;

	case 'T':  // WCHAR string buffer
		// $$$ TO DO
		break;

	case 'v':  // void
		retval = js->undefVal;
		break;
	}

	// throw any error
	if (err != JsNoError)
		js->Throw(err, _T("DllImport.call(): error converting return value"));

	// done - return the result
	return retval;
}
