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
	nextTaskID(1.0),
	HANDLE_proto(JS_INVALID_REFERENCE),
	tempAllocator(nullptr)
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

	// try getting a stack trace
	JsValueRef stackObj;
	GetProp(stackObj, exc, "stack", where);

	TSTRING stack;
	JsValueType stackType;
	if (JsGetValueType(stackObj, &stackType) != JsNoError && stackType != JsUndefined)
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


// --------------------------------------------------------------------------
//
// DllImport marshaller classes
//

// Parameters the current CPU architecture calling conventions
static const size_t argSlotSize = IF_32_64(4, 8);   // size in bytes of a generic argument slot
static const size_t stackAlign = IF_32_64(4, 16);   // stack pointer alignment size in bytes
static const size_t minArgSlots = IF_32_64(0, 4);   // minimum stack slots allocated for arguments
typedef IF_32_64(UINT32, UINT64) arg_t;

// Base class for marshallers
class JavascriptEngine::Marshaller
{
public:
	Marshaller(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd) :
		js(js), sig(sig), sigEnd(sigEnd), p(sig),
		error(false), isConst(false)
	{ }

	Marshaller(const Marshaller &m) :
		js(m.js), sig(m.sig), sigEnd(m.sigEnd), p(m.sig),
		error(false), isConst(false)
	{ }

	// process a signature
	virtual bool Marshall()
	{
		// no errors yet
		error = false;

		// process the signature
		for (; p < sigEnd && !error; NextArg())
			MarshallValue();

		// return true on success, false if there's an error
		return !error;
	}

	// Figure the size of a struct/union in the native representation
	size_t SizeofStruct() const;
	size_t SizeofUnion() const;

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
		case 'b': return DoInt32();
		case 'c': return DoInt8();
		case 'C': return DoUInt8();
		case 's': return DoInt16();
		case 'S': return DoUInt16();
		case 'i': return DoInt32();
		case 'I': return DoUInt32();
		case 'f': return DoFloat();
		case 'd': return DoDouble();
		case 'z': return DoSizeT();
		case 'Z': return DoSizeT();
		case 'P': return DoIntPtr();
		case 'H': return DoHandle();
		case 't': return DoString();
		case 'T': return DoString();
		case 'l': return DoInt64();
		case 'L': return DoUInt64();
		case 'v': return DoVoid();
		case '{':
			return p[1] == 'S' ? DoStruct() : DoUnion();

		case '(': return DoFunction();
		case '[': return DoArray();

		default:
			Error(MsgFmt(_T("DllImport.call: internal error: unknown type code '%c' in signature %.*s"), *p, (int)(sigEnd - sig), sig));
			break;
		}
	}

	// flag: the current type being processed was marked 'const'
	bool isConst;

	// process individual int types
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

	// process float types
	virtual void DoFloat() { }
	virtual void DoDouble() { }

	// handle types
	virtual void DoHandle() { IF_32_64(AnyInt32(), AnyInt64()); }

	// process a pointer/reference type
	virtual void DoPointer() { }
	virtual void DoReference() { DoPointer(); }

	// process an array type
	virtual void DoArray() { }

	// process a struct/union type
	virtual void DoStruct() { }
	virtual void DoUnion() { }

	// process a string type
	virtual void DoString() { }

	// process a void type
	virtual void DoVoid() { }

	// process a function type
	virtual void DoFunction() { }

	// Process a const qualifier
	virtual void DoConst() { }

	// javascript engine
	JavascriptEngine *js;

	// error flag
	bool error;

	// Throw an error.  This doesn't actually "throw" in the C++ exception sense;
	// it just sets the exception in the Javascript engine, and sets our internal
	// error flag.
	void Error(const TCHAR *msg)
	{
		js->Throw(msg);
		error = true;
	}

	// Throw an error from an engine error code
	void Error(JsErrorCode err, const TCHAR *msg)
	{
		js->Throw(err, msg);
		error = true;
	}

	// signature string bounds
	const WCHAR *sig;
	const WCHAR *sigEnd;

	// current position in signature
	const WCHAR *p;

	// advance p to the next argument slot
	void NextArg()
	{
		// find the end of the current argument
		const WCHAR *p = EndOfArg();

		// advance to the start of the next argument
		while (p < sigEnd && *p == ' ')
			++p;

		// save the result
		this->p = p;
	};

	// find the end of the current argument slot; does not advance p
	const WCHAR *EndOfArg() const
	{
		// skip to the next space
		int level = 0;
		for (const WCHAR *p = this->p; p < sigEnd; ++p)
		{
			// count nesting levels
			switch (*p)
			{
			case '(':
			case '[':
			case '{':
				++level;
				break;

			case ')':
			case ']':
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

// Generic size counter
class JavascriptEngine::MarshallSizer : public Marshaller
{
public:
	MarshallSizer(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd)
		: Marshaller(js, sig, sigEnd)
	{ }

	// Add a value of the given byte size to the total
	virtual void Add(size_t bytes, size_t align = 0, int nItems = 1) = 0;
	virtual void AddStruct(size_t bytes, size_t align = 0, int nItems = 1) = 0;

	// process individual int types
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

	// process float types
	virtual void DoFloat() override { Add(4); }
	virtual void DoDouble() override { Add(8); }

	// handle types
	virtual void DoHandle() override { Add(IF_32_64(4, 8)); }

	// process a pointer type
	virtual void DoPointer() override { Add(IF_32_64(4, 8)); }

	// process a string type
	virtual void DoString() override { Add(IF_32_64(4, 8)); }

	// process a struct type
	virtual void DoStruct() override;

	// process a union type
	virtual void DoUnion() override;

	// process a void type
	virtual void DoVoid() override { Error(_T("DllImport.call: 'void' types can't be passed by value")); }

	// process a function type
	virtual void DoFunction() override { Error(_T("DllImport.call: function types can't be passed by value")); }
};

// Struct size counter
class JavascriptEngine::MarshallStructSizer : public MarshallSizer
{
public:
	MarshallStructSizer(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd)
		: MarshallSizer(js, sig, sigEnd),
		ofs(0), size(0), align(0)
	{ }

	// Adjust the offset for the current value's alignment
	void AlignCurrent()
	{
		// save the current position
		auto oldOfs = ofs;
		auto oldSize = size;
		auto oldp = p;

		// parse one item
		MarshallValue();

		// revert to the old position
		ofs = oldOfs;
		size = oldSize;
		p = oldp;
	}

	virtual void MarshallValue() override
	{
		// skip the struct member name tag if present
		while (p < sigEnd && *p != ':')
			++p;

		if (p < sigEnd && *p == ':')
			++p;

		// do the normal work
		__super::MarshallValue();
	}

	virtual void Add(size_t itemBytes, size_t itemAlign = 0, int nItems = 1) override
	{
		// if the alignment was unspecified, the type's own size is its natural alignment
		if (itemAlign == 0)
			itemAlign = itemBytes;

		// add padding to bring the current offset up to the required alignment
		ofs = ((ofs + itemAlign - 1)/itemAlign) * itemAlign;

		// remember this as the last item offset
		lastItemOfs = ofs;

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
	size_t ofs;

	// Last item offset.  This is set to the aligned offset of the last item added.
	size_t lastItemOfs;

	// total size of the struct, including padding
	size_t size;

	// alignment
	size_t align;
};

// Union size counter
class JavascriptEngine::MarshallUnionSizer : public MarshallSizer
{
public:
	MarshallUnionSizer(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd)
		: MarshallSizer(js, sig, sigEnd),
		size(0), align(0)
	{ }

	virtual void Add(size_t itemBytes, size_t itemAlign, int nItems = 1) override
	{
		// use the size of the type as its default alignment
		if (itemAlign == 0)
			itemAlign = itemBytes;

		// the size of a union is the largest of the individual item sizes
		size = max(size, itemBytes * nItems);

		// the alignemnt is the largest of any individual item alignment
		align = max(align, itemAlign);
	}

	virtual void AddStruct(size_t itemBytes, size_t itemAlign = 0, int nItems = 1) override { Add(itemBytes, itemAlign, nItems); }

	// total size of the struct
	size_t size;

	// alignment
	size_t align;
};

void JavascriptEngine::MarshallSizer::DoStruct()
{
	// measure the struct size
	MarshallStructSizer s(js, p + 3, EndOfArg() - 1);
	s.Marshall();

	// add it to our overall size
	AddStruct(s.size, s.align, 1);
}

void JavascriptEngine::MarshallSizer::DoUnion()
{
	// measure the union size
	MarshallUnionSizer s(js, p + 3, EndOfArg() - 1);
	s.Marshall();

	// add it to our overall size
	AddStruct(s.size, s.align, 1);
}

size_t JavascriptEngine::Marshaller::SizeofStruct() const
{
	// measure the struct size
	MarshallStructSizer s(js, p + 3, EndOfArg() - 1);
	s.Marshall();
	return s.size;
}

size_t JavascriptEngine::Marshaller::SizeofUnion() const
{
	// measure the struct size
	MarshallUnionSizer s(js, p + 3, EndOfArg() - 1);
	s.Marshall();
	return s.size;
}

// Base class for marshalling values to native code
class JavascriptEngine::MarshallToNative : public Marshaller
{
public:
	MarshallToNative(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd) :
		Marshaller(js, sig, sigEnd)
	{ }

	MarshallToNative(const Marshaller &m) : Marshaller(m)
	{ }

	// Store a value in newly allocated space
	template<typename T> void Store(T val)
	{
		if (void *p = Alloc(sizeof(val)); p != nullptr)
			*static_cast<T*>(p) = val;
	}

	// get the next value to marshall
	virtual JsValueRef GetNextVal() = 0;

	// allocate storage for a native value
	virtual void *Alloc(size_t size, int nItems = 1) = 0;

	// allocate storage for a native struct
	virtual void *AllocStruct(size_t size, int nItems = 1) { return Alloc(size, nItems); }

	virtual void DoInt8() override { Store(static_cast<INT8>(GetInt(GetNextVal(), INT8_MIN, INT8_MAX))); }
	virtual void DoUInt8() override { Store(static_cast<UINT8>(GetInt(GetNextVal(), 0, UINT8_MAX))); }
	virtual void DoInt16() override { Store(static_cast<INT16>(GetInt(GetNextVal(), INT16_MIN, INT16_MAX))); }
	virtual void DoUInt16() override { Store(static_cast<UINT16>(GetInt(GetNextVal(), 0, UINT16_MAX))); }
	virtual void DoInt32() override { Store(static_cast<INT32>(GetInt(GetNextVal(), INT32_MIN, INT32_MAX))); }
	virtual void DoUInt32() override { Store(static_cast<UINT32>(GetInt(GetNextVal(), 0, UINT32_MAX))); }
	virtual void DoInt64() override { Store(static_cast<INT64>(GetInt64(GetNextVal(), true))); }
	virtual void DoUInt64() override { Store(static_cast<UINT64>(GetInt64(GetNextVal(), false))); }
	virtual void DoSizeT() override { Store(static_cast<SIZE_T>(IF_32_64(GetInt(GetNextVal(), 0, UINT32_MAX), GetInt64(GetNextVal(), false)))); }
	virtual void DoSSizeT() override { Store(static_cast<SSIZE_T>(IF_32_64(GetInt(GetNextVal(), INT32_MIN, INT32_MAX), GetInt64(GetNextVal(), true)))); }
	virtual void DoPtrdiffT() override { Store(static_cast<ptrdiff_t>(IF_32_64(GetInt(GetNextVal(), INT32_MIN, INT32_MAX), GetInt64(GetNextVal(), true)))); }
	virtual void DoFloat() override { Store(GetFloat(GetNextVal())); }
	virtual void DoDouble() override { Store(GetDouble(GetNextVal())); }
	virtual void DoHandle() override { Store(GetHandle(GetNextVal())); }

	virtual void DoString() override
	{
		// get the string value
		JsValueRef strval;
		JsErrorCode err;
		if ((err = JsConvertValueToString(GetNextVal(), &strval)) != JsNoError)
		{
			Error(err, _T("DllImport.call: converting argument to string"));
			return;
		}

		// retrieve the string pointer
		const wchar_t *strp;
		size_t len;
		if ((err = JsStringToPointer(strval, &strp, &len)) != JsNoError)
		{
			Error(err, _T("DllImport.call: retrieving string pointer"));
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
			cstrings.emplace_back(WideToAnsiCnt(strp, len));
			Store(cstrings.back().c_str());
			break;

		default:
			Error(MsgFmt(_T("DllImport.call: internal error: string type ID expected in signature %.*s, found '%c'"), (int)(sigEnd - sig), sig, *p));
			break;
		}
	}

	// locally allocated string copies, to be cleaned up on return
	std::list<WSTRING> wstrings;
	std::list<CSTRING> cstrings;

	virtual void DoPointer() override;

	virtual void DoFunction() override
	{
		//$$$// TO DO
	}

	virtual void DoArray() override
	{
		//$$$// TO DO
	}

	virtual void DoStruct() override;
	virtual void DoUnion() override;

	// get a numeric value as a double 
	double GetDouble(JsValueRef v)
	{
		// convert to numeric if necessary
		JsErrorCode err;
		JsValueRef numVal;
		if ((err = JsConvertValueToNumber(v, &numVal)) != JsNoError)
		{
			Error(err, _T("DllImport.call(): marshalling integer argument"));
			return std::numeric_limits<double>::quiet_NaN();
		}

		// Retrieve the double value.  Javscript represents all numbers as
		// doubles internally, so no conversion is required to convert to a
		// native C++ double and there's no need for range checking.
		double d;
		if ((err = JsNumberToDouble(numVal, &d)) != JsNoError)
		{
			Error(err, _T("DllImport.call(): marshalling integer argument"));
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
		if (d < FLT_MIN || d > FLT_MAX)
		{
			Error(_T("DllImport.call(): single-precision float argument value out of range"));
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
			Error(_T("DllImport.call(): integer argument value out of range"));
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
			Error(err, _T("DllImport.call(): JsGetValueType failed converting 64-bit integer argument"));
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
				Error(err, _T("DllImport.call(): 64-bit integer argument out of range"));
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
		if ((err = JsConvertValueToString(v, &strval)) != JsNoError)
		{
			Error(err, _T("DllImport.call(): converting 64-bit integer argument value to string"));
			return 0;
		}

		// get the string
		const WCHAR *p;
		size_t len;
		if ((err = JsStringToPointer(strval, &p, &len)) != JsNoError)
		{
			Error(err, _T("DllImport.call(): retrieving string value for 64-bit integer argument"));
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

	HANDLE GetHandle(JsValueRef v)
	{
		// check the value type
		JsErrorCode err;
		JsValueType t;
		if ((err = JsGetValueType(v, &t)) != JsNoError)
		{
			Error(err, _T("DllImport.call(): JsGetValueType failed converting HANDLE argument"));
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
				auto h = JavascriptEngine::HandleData::Recover<HandleData>(v, _T("DllImport.call(): converting HANDLE argument"));
				return h != nullptr ? h->h : NULL;
			}

		default:
			Error(err, _T("DllImport.call(): invalid value for HANDLE argument"));
			return NULL;
		}
	};
};

// Count argument slots
class JavascriptEngine::MarshallStackArgSizer : public MarshallSizer
{
public:
	MarshallStackArgSizer(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd) :
		MarshallSizer(js, sig, sigEnd), nSlots(0)
	{ }

	virtual void Add(size_t itemBytes, size_t itemAlign = 0, int nItems = 1) override
	{
		// figure the number of slots required, widening to the slot size
		size_t slotsPerItem = (itemBytes + argSlotSize - 1) / argSlotSize;

		// add the slots
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
	virtual void DoStruct() override { Error(_T("DllImport.call(): struct by value parameters are not supported (pointer or reference type required)")); }
	virtual void DoUnion() override { Error(_T("DllImport.call(): union by value parameters are not supported (pointer or reference type required")); }
	virtual void DoFunction() override { Error(_T("DllImport.call(): function by value parameters are not supported (pointer type required)")); }
	virtual void DoVoid() override { Error(_T("DllImport.call(): 'void' is not a valid parameter type")); }

	// number of stack slots required for the argument vector
	int nSlots;
};

// marshall arguments to the native argument vector in the stack
class JavascriptEngine::MarshallToNativeArgv : public MarshallToNative
{
public:
	MarshallToNativeArgv(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd,
		arg_t *nativeArgArray, JsValueRef *argvIn, int argcIn, int firstDllArg) :
		MarshallToNative(js, sig, sigEnd),
		nativeArgArray(nativeArgArray), argOut(nativeArgArray), argvIn(argvIn), argcIn(argcIn), argInCur(firstDllArg)
	{ }

	// Get and consume the next Javascript input argument.  Returns 'undefined' 
	// if we're past the last argument.
	virtual JsValueRef GetNextVal() override
	{
		return (argInCur < argcIn ? argvIn[argInCur++] : js->undefVal);
	}

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
		void *p = js->tempAllocator->Alloc(size * nItems);

		// rtore a pointer to the newly allocated memory as the inline value
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
	int argcIn;

	// current input argument index
	int argInCur;
};

// Marshall a reference into a native representation.  This allocates local storage
// in the native stack for the referenced value and copies the referenced value into
// the allocated space.
class JavascriptEngine::MarshallToNativeByReference : public MarshallToNative
{
public:
	MarshallToNativeByReference(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval) :
		MarshallToNative(js, sig, sigEnd),
		jsval(jsval),
		pointer(nullptr)
	{
		// get the value type
		if (JsGetValueType(jsval, &jstype) != JsNoError)
			jstype = JsUndefined;
	}

	// Pointer to the native storage for the referenced value.  This is set
	// by Marshall() to the allocated storage.
	void *pointer;

	// allocate native storage
	virtual void *Alloc(size_t size, int nItems = 1) override { return pointer = js->tempAllocator->Alloc(size * nItems); }

	// get the next value
	virtual JsValueRef GetNextVal() override { return jsval; }

	// the javascript source value we're marshalling
	JsValueRef jsval;
	JsValueType jstype;
};

// Marshall a value into a native struct or union
class JavascriptEngine::MarshallToNativeStruct : public MarshallToNative
{
public:
	MarshallToNativeStruct(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval, void *pointer, size_t size)
		: MarshallToNative(js, sig, sigEnd),
		structSizer(js, sig, sigEnd),
		jsval(jsval), pointer(static_cast<BYTE*>(pointer)), size(size), propval(JS_INVALID_REFERENCE)
	{ }

	// the next marshalling value is the current property value
	virtual JsValueRef GetNextVal() override { return propval; }

	// the next allocation goes into the current native slot
	virtual void *Alloc(size_t size, int nItems = 1) override { return pointer + structSizer.lastItemOfs; }

	virtual bool Marshall() override
	{
		// get the value we're converting, and get its type
		JsValueType jstype;
		JsErrorCode err;
		if ((err = JsGetValueType(jsval, &jstype)) != JsNoError)
		{
			Error(err, _T("DllImport.call: getting value type for struct argument"));
			return false;
		}

		// we can't dereference null or undefined
		if (jstype == JsNull || jstype == JsUndefined)
		{
			Error(err, _T("DllImport.call: null or missing value for struct argument"));
			return false;
		}

		// we can only convert object types
		if (jstype != JsObject)
		{
			Error(err, _T("DllImport.call: object required for struct argument"));
			return false;
		}

		// Zero the memory.  Any members not filled in from javascript will be
		// passed as zero bytes.
		ZeroMemory(pointer, size);

		// visit each field
		for (; p < sigEnd; NextArg(), structSizer.MarshallValue())
		{
			// get and skip the property name
			const WCHAR *propStart = p;
			for (; p < sigEnd && *p != ':'; ++p);
			const WCHAR *propEnd = p;
			if (p < sigEnd) ++p;
			WSTRING propName(propStart, propEnd - propStart);

			// look up the property in the object
			JsPropertyIdRef propId;
			if ((err = JsGetPropertyIdFromName(propName.c_str(), &propId)) != JsNoError)
			{
				Error(err, _T("DllImport.call: looking up property name for struct conversion"));
				return false;
			}

			// check if the object has the property
			bool hasProp;
			if ((err = JsHasProperty(jsval, propId, &hasProp)) == JsNoError && hasProp)
			{
				// retrieve the property value
				if ((err = JsGetProperty(jsval, propId, &propval)) != JsNoError)
				{
					Error(err, _T("DllImport.call: retrieving property value for struct conversion"));
					return false;
				}

				// align the current item
				structSizer.AlignCurrent();

				// marshall the current value
				MarshallValue();
			}
		}

		// success
		return true;
	}

	// javascript object value that we're marshalling into a native representation
	JsValueRef jsval;

	// current property value being visited
	JsValueRef propval;

	// struct sizer - we walk this through the fields as we populate the struct, to
	// keep track of the current slot offset in the struct
	MarshallStructSizer structSizer;

	// pointer to the native memory
	BYTE *pointer;

	// size of the native memory area
	size_t size;
};

class JavascriptEngine::MarshallToNativeUnion : public MarshallToNativeStruct
{
public:
	MarshallToNativeUnion(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval, void *pointer, size_t size) :
		MarshallToNativeStruct(js, sig, sigEnd, jsval, pointer, size)
	{ }

	// union members always go at the start of the shared memory area
	virtual void *Alloc(size_t size, int nItems = 1) override { return pointer; }
};

void JavascriptEngine::MarshallToNative::DoPointer()
{
	// get the javascript argument and its type
	JsValueRef jsval = GetNextVal();
	JsValueType jstype;
	if (JsGetValueType(jsval, &jstype) != JsNoError)
		jstype = JsNull;

	// check the type
	switch (jstype)
	{
	case JsNull:
	case JsUndefined:
		// A null or missing/undefined value passed as a pointer type maps
		// to a native null pointer.  Nulls can't be used for references.
		if (*p == '&')
			Error(_T("DllImport.call: null or missing value is invalid for a reference ('&') type"));

		// store the null
		Store<void*>(nullptr);
		break;;

	case JsNumber:
	case JsBoolean:
	case JsObject:
		// Scalar primitive or object.  Marshall it by reference.
		{
			// set up a by-reference marshaller for the referenced type
			MarshallToNativeByReference mbr(js, p + 1, EndOfArg(), jsval);

			// marshall the value
			mbr.MarshallValue();

			// store a pointer to the copy of the value that it allocated
			Store(mbr.pointer);
		}
		break;

	case JsString:
		// String.  
		break;

	case JsArray:
		// Array.  Allocate space for N copies of the native type, then convert
		// each value from the array into the corresponding native slot.
		break;		

	case JsArrayBuffer:
		// Array buffer object.  This is a Javascript object containing an array
		// of bytes.
		// TO DO
		break;

	case JsTypedArray:
		// Typed array object.
		// TO DO
		break;

	case JsFunction:
		// Function object
		// TO DO
		break;

	case JsError:
		// Error object.  This can't be passed by reference.
		Error(_T("DllImport.call: Error object cannot be passed by reference to native code"));
		break;

	case JsDataView:
		// Data view object.  These can't be passed by reference.
		Error(_T("DllImport.call: DataView object cannot be passed by reference"));
		break;

	default:
		// unhandled case
		Error(MsgFmt(_T("DllImport.call: unimplemented type-by-reference (%d)"), static_cast<int>(jstype)));
		break;
	}
}

void JavascriptEngine::MarshallToNative::DoStruct()
{
	// allocate space
	size_t size = SizeofStruct();
	void *pointer = AllocStruct(size);

	// marshall through a struct marshaller
	MarshallToNativeStruct ms(js, sig + 3, EndOfArg() - 1, GetNextVal(), pointer, size);
	ms.Marshall();
}

void JavascriptEngine::MarshallToNative::DoUnion()
{
	// allocate space
	size_t size = SizeofStruct();
	void *pointer = AllocStruct(size);

	// marshall through a struct marshaller
	MarshallToNativeUnion mu(js, sig + 3, EndOfArg() - 1, GetNextVal(), pointer, size);
	mu.Marshall();
}

// --------------------------------------------------------------------------
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
	if (!DefineObjPropFunc(proto, className, "_bind", &JavascriptEngine::DllImportBind, this, eh)
		|| !DefineObjPropFunc(proto, className, "_call", &JavascriptEngine::DllImportCall, this, eh))
		return false;

	// find the HANDLE object's prototype
	if ((err = GetProp(classObj, global, "HANDLE", subwhere)) != JsNoError
		|| (err = GetProp(HANDLE_proto, classObj, "prototype", subwhere)) != JsNoError)
		return Error(subwhere);

	// set up function bindings
	if (!DefineObjPropFunc(HANDLE_proto, "HANDLE", "toString", &HandleData::ToString, nullptr, eh)
		|| !DefineObjPropFunc(HANDLE_proto, "HANDLE", "toNumber", &HandleData::ToNumber, nullptr, eh))
			return false;

	// save a reference on the handle prototype, as we're hadning onto it
	JsAddRef(HANDLE_proto, nullptr);

	// success
	return true;
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

	// set up a temporary allocator for the marshallers
	TempAllocator tempAlloc(js);

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

	// Set up a stack argument sizer to measure how much stack space we need
	// for the native copies of the arguments.  The first type in the function 
	// signature is the return type, so skip that.
	MarshallStackArgSizer stackSizer(js, sig, sigEnd);
	stackSizer.NextArg();

	// the remaining items in the signature are the argument types - size them
	if (!stackSizer.Marshall())
		return js->undefVal;

	// Figure the required native argument array size
	size_t argArraySize = max(stackSizer.nSlots, minArgSlots) * argSlotSize;

	// round up to the next higher alignment boundary
	argArraySize = ((argArraySize + stackAlign - 1) / stackAlign) * stackAlign;

	// allocate the argument array
	arg_t *argArray = static_cast<arg_t*>(alloca(argArraySize));

	// marshall the arguments into the native stack
	MarshallToNativeArgv argPacker(js, sig, sigEnd, argArray, argv, argc, firstDllArg);
	argPacker.NextArg();
	argPacker.Marshall();

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

			; caller removes arguments if using __cdecl; for all others, the callee has
			; already removed them for us
			cmp callConv, 'C'
			jne $1

			; __cdecl -> remove our arguments
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
	const double MaxIntInDouble = static_cast<double>(2LL << DBL_MANT_DIG);
	const WCHAR *p = sig;
	JsValueRef retval = js->undefVal;
	err = JsNoError;
	switch (*p)
	{
	case '*':
		// $$$ pointer - to do
		break;

	case '&':
		// $$$ reference - to do
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

	case 'l': 
		// int64.  Any int64 value can fit in a javascript double, but there will
		// loss of precision for values outside of the range +/- 2^53.
		{
			auto ll = *reinterpret_cast<INT64*>(&rawret);
			auto d = static_cast<double>(ll);
			if (d < -MaxIntInDouble || d > MaxIntInDouble)
				/* $$$ do something to preserve the full precision */;
			else
				err = JsDoubleToNumber(d, &retval);
		}
		break;

	case 'L':  
		// uint64.  As with int64, any 64-bit integer value can fit, but we'll lose
		// precision for values above 2^53.
		{
			auto ll = *reinterpret_cast<UINT64*>(&rawret);
			auto d = static_cast<double>(ll);
			if (d > MaxIntInDouble)
				/* $$$ do something to preserve the full precision */;
			else
				err = JsDoubleToNumber(d, &retval);
		}
		break;

	case 'f':  
		// float - Javascript's native numeric representation is doubles, so any float fits
		err = JsDoubleToNumber(*reinterpret_cast<const float*>(&rawret), &retval);
		break;

	case 'd':
		// double - this is identical to the native Javascript number representation
		err = JsDoubleToNumber(*reinterpret_cast<double*>(&rawret), &retval);
		break;

	case 'H': 
		// HANDLE and related object handle types.  Wrap these in HandleData external objects.
		err = JsCreateExternalObjectWithPrototype(new HandleData(*reinterpret_cast<HANDLE*>(&rawret)), 
			&HandleData::Finalize, js->HANDLE_proto, &retval);
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
	if (auto self = HandleData::Recover<HandleData>(argv[0], _T("HANDLE.toString()")); self != nullptr)
	{
		double d = static_cast<double>(reinterpret_cast<UINT_PTR>(self->h));
		JsDoubleToNumber(d, &ret);
		if (d > static_cast<double>(2LL < DBL_MANT_DIG))
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

