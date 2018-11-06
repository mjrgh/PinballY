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
	NativePointer_proto(JS_INVALID_REFERENCE),
	marshallerContext(nullptr)
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

	// initialize symbols
	JsValueRef symName, symbol;
	JsPointerToString(L"Thunk", 5, &symName);
	JsCreateSymbol(symName, &symbol);
	JsGetPropertyIdFromSymbol(symbol, &callbackPropertyId);

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
	if ((err = FetchImportedModuleCommon(nullptr, L"[System]", TCHARToWide(url), &record)) != JsNoError)
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
	auto const *cookie = &sourceCookies.emplace_back(this, TCHARToWide(url));

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

	// create a property descriptor object
	JsValueRef descriptor;
	JsValueRef valueStr, propNameStr;
	bool result;
	if (!Check(JsCreateObject(&descriptor), _T("JsCreateObject(property descriptor)"))
		|| !Check(JsCreateString("value", 5, &valueStr), _T("JsCreateString(value)"))
		|| !Check(JsObjectSetProperty(descriptor, valueStr, propVal, true), _T("JsObjectSetProperty(value)"))
		|| !Check(JsCreateString(propName, strlen(propName), &propNameStr), _T("JsCreateString(propName)"))
		|| !Check(JsObjectDefineProperty(object, propNameStr, descriptor, &result), _T("JsObjectDefineProperty")))
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

	// Allocate a cookie.  This is required to give the module a source
	// context, which ChakraCore uses to identify module source locations
	// for functions defined therein, in stack traces and other debugging.
	// (CC doesn't use the path we store here; the cookie is an opaque
	// identifier as far as it's concerned.  The cookie's only purpose to
	// CC is to serve as a unique identifier for the module; CC uses the
	// cookie as a key into its own internal table to associate the source 
	// code with the module record, where the URL is actually stored.  The
	// URL it uses is the one we added to the module record previously.)
	auto const *cookie = &js->sourceCookies.emplace_back(js, path.c_str());

	// Parse the source.  Note that the source memory is provided as BYTEs,
	// but the file length we have is in WCHARs, so we need to multiply for
	// the parser's sake.
	JsValueRef exc = JS_INVALID_REFERENCE;
	JsErrorCode err = JsParseModuleSource(module, reinterpret_cast<JsSourceContext>(cookie),
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

	// replace '/' characters in the path with '\'
	for (TCHAR *sl = path; *sl != 0; ++sl)
	{
		if (*sl == '/')
			*sl = '\\';
	}

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

	// Figure the size of a struct/union in the native representation.  flexErrorMsg
	// is an error message to show if a flex array is found and the size of the 
	// unspecified dimension can't be inferred from jsval.
	size_t SizeofStruct(JsValueRef jsval, const TCHAR *flexErrorMsg);
	size_t SizeofUnion(JsValueRef jsval, const TCHAR *flexErrorMsg);

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
		case 't': return DoString();
		case 'T': return DoString();
		case 'v': return DoVoid();
		case '{':
			return p[1] == 'S' ? DoStruct() : DoUnion();

		case '(': return DoFunction();
		case '[': return DoArray();

		default:
			Error(MsgFmt(_T("DllImport: internal error: unknown type code '%c' in signature %.*s"), *p, (int)(sigEnd - sig), sig));
			break;
		}
	}

	// flag: the current type being processed was marked 'const'
	bool isConst;

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

	// javascript engine
	JavascriptEngine *js;

	// error flag
	bool error;

	// Throw an error.  This doesn't actually "throw" in the C++ exception sense;
	// it just sets the exception in the Javascript engine, and sets our internal
	// error flag.
	void Error(const TCHAR *msg)
	{
		error = true;
		if (!js->HasException())
			js->Throw(msg);
	}

	// Throw an error from an engine error code
	void Error(JsErrorCode err, const TCHAR *msg)
	{
		error = true;
		if (!js->HasException())
			js->Throw(err, msg);
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

	const WCHAR *EndOfArg() const { return EndOfArg(p, sigEnd); }
	const WCHAR *EndOfArg(const WCHAR *p) const { return EndOfArg(p, sigEnd); }

	// find the end of the current argument slot; does not advance p
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

	// Get the length of a Javascript array value
	int GetArrayLength(JsValueRef jsval)
	{
		// read the length property
		int len;
		const TCHAR *where;
		if (JsErrorCode err = js->GetProp(len, jsval, "length", where); err != JsNoError)
		{
			Error(err, MsgFmt(_T("DllImport: getting length of array argument"), where));
			return -1;
		}

		// make sure the length is non-negative
		return max(len, 0);
	}

	// Parse array dimensions
	bool GetArrayDims(std::vector<size_t> &dims, size_t &nElementsFlat, JsValueRef jsval)
	{
		// start with one element; we'll compute the product as we scan the dimensions
		nElementsFlat = 1;

		// scan dimensions
		while (p < sigEnd)
		{
			// skip the '['
			++p;

			// if the first element is empty, it's a flexible size array
			size_t s = 0;
			if (*p == ']')
			{
				// check that it's really the innermost element
				if (dims.size() != 0)
				{
					Error(_T("DllImport: empty array index (\"[]\") can be only be used for the first (innermost) index"));
					return false;
				}

				// add it to the dimension list as a zero
				dims.emplace_back(0);
			}
			else
			{
				// read the size
				for (; p < sigEnd && *p != ']'; ++p)
				{
					if (!iswdigit(*p))
					{
						Error(_T("DllImport: invalid array size in type signature"));
						return false;
					}
					s *= 10;
					s += *p - '0';
				}

				if (s == 0)
				{
					Error(_T("DllImport: array dimensions must be nonzero"));
					return false;
				}

				// multiply it into the cumulative size:  the total number of elements
				// is the product of all of the dimensions
				nElementsFlat *= s;

				// add this to the dimension list
				dims.emplace_back(s);
			}

			// skip the ']'
			if (p < sigEnd && *p == ']')
				++p;

			// stop if we've reached the last dimension
			if (p >= sigEnd || *p != '[')
				break;
		}

		// make the dimensions concrete
		return ConcretizeArrayDims(dims, nElementsFlat, jsval);
	}

	// make array dimensions concrete, by referencing an unspecified dimension
	// to the actual Javascript array value provided
	bool ConcretizeArrayDims(std::vector<size_t> &dims, size_t &nElementsFlat, JsValueRef jsval)
	{
		// If we have a flex array, we need to infer the size from the argument
		if (dims[0] == 0)
		{
			// check if we have a concrete argument to infer the size from
			if (jsval != JS_INVALID_REFERENCE)
			{
				// check the argument type
				JsValueType type;
				JsErrorCode err;
				if ((err = JsGetValueType(jsval, &type)) != JsNoError)
				{
					Error(err, _T("DllImport: getting type of struct member array"));
					return false;
				}

				int dim = 0;
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
					if ((dim = GetArrayLength(jsval)) < 0)
						return false;
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
					if ((dim = GetArrayLength(jsval)) < 0)
						return false;

					// divide the actual parameter array size by the other dimensions
					// to get the overall size, rounding up
					dim = (dim + nElementsFlat - 1) / nElementsFlat;
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

				// set the new first dimension, and refigure the total count
				dims[0] = dim;
				nElementsFlat *= dim;
			}
			else
			{
				// There's no concrete argument to apply.  The abstract size of a
				// flex array is simply zero.  When such an array is used in a
				// struct, it's a placeholder for additional elements allocated
				// by the caller, but it doesn't contribute to sizeof(the struct).
				nElementsFlat = 0;
			}
		}

		// success
		return true;
	}
};

// Generic size counter
class JavascriptEngine::MarshallSizer : public Marshaller
{
public:
	MarshallSizer(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd)
		: Marshaller(js, sig, sigEnd)
	{ }

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

	// process a struct type
	virtual void DoStruct() override;

	// process a union type
	virtual void DoUnion() override;

	// process an array
	virtual void DoArray() override = 0;

	// process a void type
	virtual void DoVoid() override { Error(_T("DllImport: 'void' types can't be passed by value")); }

	// process a function type
	virtual void DoFunction() override { Error(_T("DllImport: function types can't be passed by value")); }
};

// Basic sizer.  This simply adds up the sizes for the measured types without
// regard to alignment.  This is useful mostly to get the size of single type.
class JavascriptEngine::MarshallBasicSizer : public MarshallSizer
{
public:
	MarshallBasicSizer(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval) :
		MarshallSizer(js, sig, sigEnd),
		jsval(jsval), size(0), flex(false)
	{ }

	MarshallBasicSizer(JavascriptEngine *js, const WSTRING &sig, JsValueRef jsval = JS_INVALID_REFERENCE) :
		MarshallSizer(js, sig.c_str(), sig.c_str() + sig.length()),
		jsval(jsval), size(0), flex(false)
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
		// Figure the dimensions
		size_t totalCnt;
		std::vector<size_t> dims;
		if (!GetArrayDims(dims, totalCnt, GetCurVal()))
			return;

		// note if we found a flexible array dimension
		if (dims.size() > 0 && dims[0] == 0)
			flex = true;

		// figure the size of the underlying type
		MarshallBasicSizer sizer(js, p, EndOfArg(), GetCurVal());
		sizer.MarshallValue();

		// add n elements of the underlying type
		Add(sizer.size, sizer.size, totalCnt);
	}

	virtual JsValueRef GetCurVal() override { return jsval; }

	// concrete value being sized, if available
	JsValueRef jsval;

	// computed size of type
	size_t size;

	// alignment of the type
	size_t align;

	// a flexible array was encountered
	bool flex;
};

// Common base for structs and unions
class JavascriptEngine::MarshallStructOrUnionSizer : public MarshallBasicSizer
{
public:
	MarshallStructOrUnionSizer(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval) :
		MarshallBasicSizer(js, sig, sigEnd, jsval),
		lastItemOfs(0), size(0), align(0), flexError(false)
	{ }

	// Offset of last item marshalled.  For a struct, this is the aligned offset
	// of the last item.  For a union, this is always zero, since union members
	// are all overlaid on the same memory.
	size_t lastItemOfs;

	// total size of the struct, including padding
	size_t size;

	// alignment
	size_t align;

	// flex member error detected and reported
	bool flexError;

	// current property name
	TSTRING curProp;

	// the current value is the property value
	virtual JsValueRef GetCurVal() override 
	{ 
		// if there's an object and a current property, retrieve the property
		JsValueRef curval = js->undefVal;
		if (jsval != JS_INVALID_REFERENCE && curProp.length() != 0)
		{
			const TCHAR *where;
			if (JsErrorCode err = js->GetProp(curval, jsval, WSTRINGToCSTRING(curProp).c_str(), where); err != JsNoError)
				Error(err, MsgFmt(_T("DllImport: measuring struct/union size: %s"), where));
		}

		return curval;
	}

	virtual void MarshallValue() override
	{
		// skip the struct member name tag if present
		const TCHAR *tag = p;
		while (p < sigEnd && *p != ':')
			++p;

		// if we found the tag name, store it
		if (p < sigEnd && *p == ':')
		{
			curProp.assign(tag, p - tag);
			++p;
		}
		else
			curProp = _T("");

		// A flex array is only allowed as the last element, so if we already 
		// have a flex array element, we can't have another member
		if (flex && !flexError)
		{
			Error(_T("DllImport: an unspecified array dimension can only be used in the last member of a struct"));
			flexError = true;
		}

		// do the normal work
		__super::MarshallValue();
	}
};

// Struct size counter
class JavascriptEngine::MarshallStructSizer : public MarshallStructOrUnionSizer
{
public:
	MarshallStructSizer(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval) :
		MarshallStructOrUnionSizer(js, sig, sigEnd, jsval), ofs(0)
	{ }

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
};

// Union size counter
class JavascriptEngine::MarshallUnionSizer : public MarshallStructOrUnionSizer
{
public:
	MarshallUnionSizer(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval)
		: MarshallStructOrUnionSizer(js, sig, sigEnd, jsval)
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
};

void JavascriptEngine::MarshallSizer::DoStruct()
{
	// measure the struct size
	MarshallStructSizer s(js, p + 3, EndOfArg() - 1, GetCurVal());
	s.Marshall();

	// add it to our overall size
	AddStruct(s.size, s.align, 1);
}

void JavascriptEngine::MarshallSizer::DoUnion()
{
	// measure the union size
	MarshallUnionSizer s(js, p + 3, EndOfArg() - 1, GetCurVal());
	s.Marshall();

	// add it to our overall size
	AddStruct(s.size, s.align, 1);
}

size_t JavascriptEngine::Marshaller::SizeofStruct(JsValueRef jsval, const TCHAR *flexErrorMsg)
{
	// measure the struct size
	MarshallStructSizer s(js, p + 3, EndOfArg() - 1, jsval);
	s.Marshall();

	// check for flex errors
	if (s.flex && flexErrorMsg != nullptr)
		Error(flexErrorMsg);

	// return the struct size
	return s.size;
}

size_t JavascriptEngine::Marshaller::SizeofUnion(JsValueRef jsval, const TCHAR *flexErrorMsg)
{
	// measure the struct size
	MarshallUnionSizer s(js, p + 3, EndOfArg() - 1, jsval);
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
			Error(err, _T("DllImport: getting string argument type"));
			return;
		}

		// Let's check the type
		switch (type)
		{
		case JsArrayBuffer:
			// Array buffer.  This is an opaque byte array type that a caller
			// can use for a reference to any type.  Pass the callee the underlying
			// byte buffer.
			{
				ChakraBytePtr buffer = nullptr;
				unsigned int bufferLen;
				JsErrorCode err = JsGetArrayBufferStorage(jsval, &buffer, &bufferLen);
				if (err != JsNoError)
					Error(err, _T("DllImport: retrieving ArrayBuffer storage pointer"));

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
				case 't':  typeOk = arrType == JsArrayTypeUint8; break;
				case 'T':  typeOk = arrType == JsArrayTypeUint16; break;
				}

				if (!typeOk)
				{
					Error(_T("DllImport: Javascript typed array type doesn't match native string argument type"));
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
					Error(err, _T("DllImport: converting argument to string"));
					return;
				}

				// retrieve the string pointer
				const wchar_t *strp;
				size_t len;
				if ((err = JsStringToPointer(strval, &strp, &len)) != JsNoError)
				{
					Error(err, _T("DllImport: retrieving string pointer"));
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
					Error(MsgFmt(_T("DllImport: internal error: string type ID expected in signature %.*s, found '%c'"), (int)(sigEnd - sig), sig, *p));
					break;
				}
			}
			break;
		}
	}

	// locally allocated string copies, to be cleaned up on return
	std::list<WSTRING> wstrings;
	std::list<CSTRING> cstrings;

	virtual void DoPointer() override;

	virtual void DoFunction() override
	{
		// functions can't be passed by value, only by reference
		Error(_T("DllImport: functions can't be passed by value (pointer required)"));
	}

	virtual void DoStruct() override;
	virtual void DoUnion() override;

	// get a boolean value
	bool GetBool(JsValueRef v)
	{
		// convert to a JS boolean
		JsErrorCode err;
		JsValueRef boolVal;
		if ((err = JsConvertValueToBoolean(v, &boolVal)) != JsNoError)
		{
			Error(err, _T("DllImport: marshalling bool argument"));
			return false;
		}

		// convert to a C bool
		bool b;
		if ((err = JsBooleanToBool(boolVal, &b)) != JsNoError)
		{
			Error(err, _T("DllImport: marshalling bool argument"));
			return false;
		}

		return b;
	}

	// get a numeric value as a double 
	double GetDouble(JsValueRef v)
	{
		// convert to numeric if necessary
		JsErrorCode err;
		JsValueRef numVal;
		if ((err = JsConvertValueToNumber(v, &numVal)) != JsNoError)
		{
			Error(err, _T("DllImport: marshalling integer argument"));
			return std::numeric_limits<double>::quiet_NaN();
		}

		// Retrieve the double value.  Javscript represents all numbers as
		// doubles internally, so no conversion is required to convert to a
		// native C++ double and there's no need for range checking.
		double d;
		if ((err = JsNumberToDouble(numVal, &d)) != JsNoError)
		{
			Error(err, _T("DllImport: marshalling integer argument"));
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
			Error(_T("DllImport: single-precision float argument value out of range"));
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
			Error(_T("DllImport: integer argument value out of range"));
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
			Error(err, _T("DllImport: JsGetValueType failed converting 64-bit integer argument"));
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
				Error(err, _T("DllImport: 64-bit integer argument out of range"));
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
			Error(err, _T("DllImport: converting 64-bit integer argument value to string"));
			return 0;
		}

		// get the string
		const WCHAR *p;
		size_t len;
		if ((err = JsStringToPointer(strval, &p, &len)) != JsNoError)
		{
			Error(err, _T("DllImport: retrieving string value for 64-bit integer argument"));
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
			Error(err, _T("DllImport: JsGetValueType failed converting HANDLE argument"));
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
				auto h = JavascriptEngine::HandleData::Recover<HandleData>(v, _T("DllImport: converting HANDLE argument"));
				return h != nullptr ? h->h : NULL;
			}

		default:
			Error(err, _T("DllImport: invalid value for HANDLE argument"));
			return NULL;
		}
	};
};

// Count argument slots
class JavascriptEngine::MarshallStackArgSizer : public MarshallSizer
{
public:
	MarshallStackArgSizer(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd, JsValueRef *argv, int argc, int firstArg) :
		MarshallSizer(js, sig, sigEnd),
		jsArgv(argv), jsArgc(argc), jsArgCur(firstArg), nSlots(0), hiddenStructArg(false)
	{ }

	virtual JsValueRef GetCurVal() { return jsArgCur < jsArgc ? jsArgv[jsArgCur] : js->undefVal; }

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
		if (*p == '{')
		{
			const TCHAR *flexErr = _T("DllImport: struct with unspecified array dimension can't be used as a return value");
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
	virtual void DoFunction() override { Error(_T("DllImport: function by value parameters are not supported (pointer type required)")); }
	virtual void DoVoid() override { Error(_T("DllImport: 'void' is not a valid parameter type")); }

	// array-by-value decays to a pointer to the underlying type
	virtual void DoArray() override { Add(sizeof(void*)); }

	// argument array from the Javascript caller
	JsValueRef *jsArgv;
	int jsArgc;
	int jsArgCur;

	// number of stack slots required for the native argument vector
	int nSlots;

	// is there a hidden first argument for a return-by-value struct?
	bool hiddenStructArg;
};

// marshall arguments to the native argument vector in the stack
class JavascriptEngine::MarshallToNativeArgv : public MarshallToNative
{
public:
	MarshallToNativeArgv(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd,
		arg_t *nativeArgArray, JsValueRef *argvIn, int argcIn, int firstDllArg) :
		MarshallToNative(js, sig, sigEnd),
		nativeArgArray(nativeArgArray), argOut(nativeArgArray), 
		argvIn(argvIn), argcIn(argcIn), argInCur(firstDllArg), firstDllArg(firstDllArg),
		structByValueReturn(false)
	{ }

	// Flag: we allocated a hidden struct/union for a by-value return
	bool structByValueReturn;

	// List of by-reference arguments to be marshalled back to Javascript on return
	struct ByRefArg
	{
		ByRefArg(arg_t *pNativeArg, JsValueRef jsArg, const WCHAR *sig, const WCHAR *sigEnd) :
			pNativeArg(pNativeArg), jsArg(jsArg), sig(sig, sigEnd - sig)
		{ }

		// native argv index where the native pointer is stored in the arguments
		arg_t *pNativeArg;

		// the javascript by-reference argument value (Array, Object)
		JsValueRef jsArg;

		// type signature for the referenced type
		WSTRING sig;
	};
	std::list<ByRefArg> byRefArgs;

	virtual bool Marshall()
	{
		// If the return type is a struct/union BY VALUE with a size over
		// 8 bytes, we need to allocate temporary space for the struct in
		// the caller's frame, and pass a pointer to the allocated space
		// as a hidden first parameter.
		if (*p == '{')
		{
			JsValueRef jsval = argInCur < argcIn ? argvIn[argInCur] : js->undefVal;
			size_t size = (p[1] == 'S' ? SizeofStruct(jsval, nullptr) : SizeofUnion(jsval, nullptr));
			if (size > 8)
			{
				// allocate space for the struct by reference
				AllocStructByRef(size);

				// flag it
				structByValueReturn = true;
			}
		}

		// skip the return value entry
		NextArg();

		// do the rest of the marshalling normally
		return __super::Marshall();
	}

	virtual void DoArray() override
	{
		// In an argument list, an array decays to a pointer to the
		// underlying type.  This only goes one deep, so scan to the
		// first ']', and process the rest as though it were a pointer
		// to the underlying type.
		for (; p < sigEnd && *p != ']'; ++p);

		// Note that we leave p parked at the ']', because DoPointer
		// expects to be lined at the pointer signifier, usually * or &,
		// but ] works too.
		DoPointer();
	}

	virtual void DoPointer() override
	{
		// If an Object or Array Javascript value is passed by pointer to
		// native code, and it's not 'const', treat it as a possible OUT
		// argument.
		if (p[1] != '%')
			byRefArgs.emplace_back(argOut, GetCurVal(), p, EndOfArg(p));

		// do the base class work
		__super::DoPointer();
	}

	// Get and consume the next Javascript input argument.  Returns 'undefined' 
	// if we're past the last argument.
	virtual JsValueRef GetNextVal() override
	{
		return (argInCur < argcIn ? argvIn[argInCur++] : js->undefVal);
	}

	JsValueRef GetCurVal()
	{
		return (argInCur < argcIn ? argvIn[argInCur] : js->undefVal);
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
		void *p = js->marshallerContext->Alloc(size * nItems);

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
	MarshallToNativeArray(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsArray,
		void *nativeArray, size_t eleSize, int nEles) :
		MarshallToNative(js, sig, sigEnd),
		jsArray(jsArray),
		nativeArray(static_cast<BYTE*>(nativeArray)), eleSize(eleSize), nEles(nEles),
		idxIn(0), idxOut(0)
	{ }

	virtual void MarshallValue() override
	{
		// marshall each array element
		for (int i = 0; i < nEles; ++i)
		{
			// reset to the start of the signature
			p = sig;

			// marshall this value
			__super::MarshallValue();
		}
	}

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
			Error(err, _T("DllImport: indexing argument array"));
			return js->nullVal;
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
		Error(_T("DllImport: array of array not supported"));
	}

	// javascript array
	JsValueRef jsArray;

	// current input element
	int idxIn;

	// current output element
	int idxOut;

	// native array: pointer, element size, number of elements
	BYTE *nativeArray;
	int eleSize;
	int nEles;
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

	virtual void DoArray() override { DoArrayCommon(jsval); }

	// allocate native storage
	virtual void *Alloc(size_t size, int nItems = 1) override { return pointer = js->marshallerContext->Alloc(size * nItems); }

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
		structSizer(js, sig, sigEnd, jsval),
		jsval(jsval), pointer(static_cast<BYTE*>(pointer)), size(size), propval(JS_INVALID_REFERENCE)
	{ }

	virtual void DoArray() override { DoArrayCommon(propval); }

	virtual bool Marshall() override
	{
		// get the value we're converting, and get its type
		JsValueType jstype;
		JsErrorCode err;
		if ((err = JsGetValueType(jsval, &jstype)) != JsNoError)
		{
			Error(err, _T("DllImport: getting value type for struct argument"));
			return false;
		}

		// we can't dereference null or undefined
		if (jstype == JsNull || jstype == JsUndefined)
		{
			Error(err, _T("DllImport: null or missing value for struct argument"));
			return false;
		}

		// we can only convert object types
		if (jstype != JsObject)
		{
			Error(err, _T("DllImport: object required for struct argument"));
			return false;
		}

		// visit each field
		for (; p < sigEnd; NextArg(), structSizer.NextArg())
		{
			// get and skip the property name
			const WCHAR *propStart = p;
			for (; p < sigEnd && *p != ':'; ++p);
			const WCHAR *propEnd = p;
			if (p < sigEnd) ++p;
			WSTRING propName(propStart, propEnd - propStart);

			// Marshall the field into the sizer.  This will set the "last offset"
			// field in the sizer to the current field offset, taking into account
			// its alignment.
			structSizer.MarshallValue();

			// look up the property in the object
			JsPropertyIdRef propId;
			if ((err = JsGetPropertyIdFromName(propName.c_str(), &propId)) != JsNoError)
			{
				Error(err, _T("DllImport: looking up property name for struct conversion"));
				return false;
			}

			// check if the object has the property
			bool hasProp;
			if ((err = JsHasProperty(jsval, propId, &hasProp)) == JsNoError && hasProp)
			{
				// retrieve the property value
				if ((err = JsGetProperty(jsval, propId, &propval)) != JsNoError)
				{
					Error(err, _T("DllImport: retrieving property value for struct conversion"));
					return false;
				}

				// marshall the current value
				MarshallValue();
			}
			else if (propName == _T("cbSize"))
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
		}

		// success
		return true;
	}

	// the next marshalling value is the current property value
	virtual JsValueRef GetNextVal() override { return propval; }

	// the next allocation goes into the current native slot
	virtual void *Alloc(size_t size, int nItems = 1) override { return pointer + structSizer.lastItemOfs; }

	// javascript object value that we're marshalling into a native representation
	JsValueRef jsval;

	// current property value being visited
	JsValueRef propval;

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
	MarshallToNativeUnion(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval, void *pointer, size_t size) :
		MarshallToNativeStruct(js, sig, sigEnd, jsval, pointer, size)
	{ }

	// union members always go at the start of the shared memory area
	virtual void *Alloc(size_t size, int nItems = 1) override { return pointer; }
};

void JavascriptEngine::MarshallToNative::DoArrayCommon(JsValueRef jsval)
{
	// figure the dimensions
	size_t totalCnt;
	std::vector<size_t> dims;
	if (!GetArrayDims(dims, totalCnt, jsval) || totalCnt == 0)
		return;

	// figure the size of the underlying type
	MarshallBasicSizer sizer(js, p, EndOfArg(), jsval);
	sizer.MarshallValue();
	if (sizer.size != 0)
	{
		// marshall the native array
		MarshallToNativeArray ma(js, p, EndOfArg(), jsval, Alloc(sizer.size, totalCnt), sizer.size, totalCnt);
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
	if (tp < sigEnd && *tp == '%')
		++tp;

	// check the type
	switch (jstype)
	{
	case JsNull:
	case JsUndefined:
		// A null or missing/undefined value passed as a pointer type maps
		// to a native null pointer.  Nulls can't be used for references.
		if (*p == '&')
			Error(_T("DllImport: null or missing value is invalid for a reference ('&') type"));

		// store the null
		Store<void*>(nullptr);
		break;

	case JsString:
		// If the underlying type is an 8- or 16-bit int, convert the string
		// to a character buffer of the appropriate type, with null termination,
		// and pass a pointer to the buffer.
		switch (*tp)
		{
		case 'c':
		case 'C':
			// Pointer to int8.  Marshall as a pointer to a null-terminated
			// buffer of ANSI characters.
			break;

		case 's':
		case 'S':
			// Pointer to int16.  Marshall as a pointer to a null-terminated
			// buffer of Unicode characters.
			break;

		default:
			// strings can't be passed for other reference types
			Error(_T("DllImport: string argument can only be used for char and wchar pointers"));
			break;
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
				Error(err, _T("DllImport: retrieving ArrayBuffer storage pointer"));

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
				MarshallBasicSizer sizer(js, p + 1, EndOfArg(), jsval);
				sizer.MarshallValue();

				// allocate temporary storage for the array copy
				void *pointer = js->marshallerContext->Alloc(sizer.size * len);

				// marshall the array values into the native array
				MarshallToNativeArray ma(js, p + 1, EndOfArg(), jsval, pointer, sizer.size, len);
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
				Error(_T("DllImport: Javascript typed array type doesn't match native pointer argument type"));
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
				Error(_T("DllImport: function argument value can only be used for a function pointer parameter"));
				return;
			}

			// look for a callback thunk
			JsValueRef thunk = JS_INVALID_REFERENCE;
			bool hasThunk;
			if ((err = JsHasOwnProperty(jsval, js->callbackPropertyId, &hasThunk)) != JsNoError
				|| (hasThunk && (err = JsGetProperty(jsval, js->callbackPropertyId, &thunk)) != JsNoError))
			{
				Error(err, _T("DllImport: getting callback function thunk"));
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
					Error(err, MsgFmt(_T("DllImport: recovering callback function thunk data: %s"), where));
					return;
				}
			}
			else
			{
				// create a new thunk
				wrapper = new JavascriptCallbackWrapper(js, jsval, tp + 1, EndOfArg(tp) - 1);
				if ((err = JsCreateExternalObject(wrapper, &JavascriptCallbackWrapper::Finalize, &thunk)) != JsNoError)
				{
					Error(err, _T("DllImport: creating callback function thunk external object"));
					return;
				}

				// Cross-reference the function and the external thunk object.  This
				// will keep the thunk alive as long as the function is alive, and
				// vice versa, ensuring they're collected as a pair.
				if ((err = JsSetProperty(thunk, js->callbackPropertyId, jsval, true)) != JsNoError
					|| (err = JsSetProperty(jsval, js->callbackPropertyId, thunk, true)) != JsNoError)
				{
					Error(err, _T("DllImport: setting callback function/thunk cross-references"));
					return;
				}
			}

			// Pass the thunk from the wrapper to the native code to use as the callback
			Store(wrapper->thunk);
		}
		break;

	default:
		// anything else gets marshalled by referenced
		{
			MarshallToNativeByReference mbr(js, p + 1, EndOfArg(), jsval);
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
	MarshallToNativeStruct ms(js, p + 3, EndOfArg() - 1, jsval, pointer, size);
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
	MarshallToNativeUnion mu(js, p + 3, EndOfArg() - 1, jsval, pointer, size);
	mu.Marshall();
}

class JavascriptEngine::MarshallFromNative : public Marshaller
{
public:
	MarshallFromNative(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd) :
		Marshaller(js, sig, sigEnd)
	{ }

	MarshallFromNative(Marshaller &m) : Marshaller(m) { }

	bool Check(JsErrorCode err)
	{
		if (err != JsNoError)
		{
			Error(err, _T("DllImport: converting native value to Javascript"));
			error = true;
		}
		return !error;
	}
};

class JavascriptEngine::MarshallFromNativeValue : public MarshallFromNative
{
public:
	MarshallFromNativeValue(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd, const void *valp) :
		MarshallFromNative(js, sig, sigEnd),
		valp(valp), jsval(JS_INVALID_REFERENCE)
	{ }

	// Javascript result value
	JsValueRef jsval;

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

	const double MaxIntInDouble = static_cast<double>(2LL << DBL_MANT_DIG);

	virtual void DoInt64() override 
	{
		// Any int64 value can fit in a javascript double in terms of range, 
		// but there will loss of integer precision for values outside of the 
		// range +/- 2^53, so treat it as an error if it's outside this range.
		auto ll = *reinterpret_cast<const INT64*>(valp);
		auto d = static_cast<double>(ll);
		if (d < -MaxIntInDouble || d > MaxIntInDouble)
			/* $$$ TO DO - do something to preserve the full precision, like an external 64-bit object or string value return */;

		Check(JsDoubleToNumber(d, &jsval));
	}

	virtual void DoUInt64() override
	{
		auto ll = *reinterpret_cast<const UINT64*>(&valp);
		auto d = static_cast<double>(ll);
		if (d > MaxIntInDouble)
			/* $$$ TO DO - preserve precision */;

		Check(JsDoubleToNumber(d, &jsval));
	}

	virtual void DoString() override
	{
		// skip any const qualifier in the underlying type
		const WCHAR *tp = p;
		if (*tp == '%')
			++tp;

		// convert the string based on the character set width
		switch (*tp)
		{
		case 't':
			{
				WSTRING wstr(AnsiToWide(*static_cast<const CHAR* const*>(valp)));
				Check(JsPointerToString(wstr.c_str(), wstr.length(), &jsval));
			}
			break;

		case 'T':
			{
				auto wstr = *static_cast<const WCHAR* const*>(valp);
				Check(JsPointerToString(wstr, wcslen(wstr), &jsval));
			}
			break;

		default:
			Error(_T("DllImport: unrecognized string type code in type signature"));
			break;
		}
	}

	virtual void DoFloat() override { Check(JsDoubleToNumber(*reinterpret_cast<const float*>(valp), &jsval)); }
	virtual void DoDouble() override { Check(JsDoubleToNumber(*reinterpret_cast<const double*>(valp), &jsval)); }

	virtual void DoHandle() override
	{
		// HANDLE values are 64 bits on x64, so we can't reliably convert
		// these to and from Javascript Number values.  Wrap it in an 
		// external data object to ensure we preserve all bits.
		Check(JsCreateExternalObjectWithPrototype(
			new HandleData(*reinterpret_cast<const HANDLE*>(valp)),
			&HandleData::Finalize, js->HANDLE_proto, &jsval));
	}

	virtual void DoFunction() override { Error(_T("DllImport: function can't be returned by value (pointer required)")); }

	void DoPointerToFunction(const WCHAR *funcSig)
	{
		// get the native function pointer
		FARPROC procAddr = *const_cast<FARPROC*>(reinterpret_cast<const FARPROC*>(valp));

		// a null native function pointer translate to a null javascript result
		if (procAddr == nullptr)
		{
			jsval = js->nullVal;
			return;
		}

		// wrap the function pointer in an external data object
		JsValueRef extObj;
		Check(JsCreateExternalObject(
			new DllImportData(procAddr, TSTRING(_T("[Return/OUT value from DLL invocation]")), TSTRING(_T("[Anonymous]"))),
			&DllImportData::Finalize, &extObj));

		// The external object isn't by itself callbable from Javascript;
		// it has to go through our special system _call() function.  We now 
		// have to wrap the function in a lambda that calls <dllImport>.call() 
		// with the function object and its signature.

		// Set up the signature.  By convention, we normalize out the parens
		// in the normal signature for a function type.
		JsValueRef funcSigVal;
		JsErrorCode err;
		if ((err = JsPointerToString(funcSig + 1, EndOfArg(funcSig) - funcSig - 2, &funcSigVal)) != JsNoError)
		{
			Error(err, _T("DllImport: JsPointerToString(native callback signature"));
			return;
		}
		
		// Get this._bindExt
		const TCHAR *where;
		JsValueRef bindExt;
		JsValueRef jsthis = js->marshallerContext->jsthis;
		if ((err = js->GetProp(bindExt, jsthis, "_bindExt", where)) != JsNoError)
		{
			Error(err, MsgFmt(_T("DllImport: getting this._bindExt(): %s"), where));
			return;
		}

		// Call this._bind(this, extObj, funcsig).  This return value from that function
		// is the wrapped function pointer.
		JsValueRef bindArgv[3] = { jsthis, extObj, funcSigVal };
		if ((err = JsCallFunction(bindExt, bindArgv, static_cast<unsigned short>(countof(bindArgv)), &jsval)) != JsNoError)
		{
			Error(err, _T("DllImport: JsCallFunction(this._bindExt())"));
			return;
		}
	}

	virtual void DoVoid() override { jsval = js->undefVal; }

	virtual void DoPointer() override 
	{
		// if the pointer is a local object, we don't have to do anything with it
		void *ptr = *static_cast<void* const*>(valp);
		if (js->marshallerContext->IsLocal(ptr))
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
			// This is a pointer returned by reference from the native callee.  
			// Javascript doesn't have its own way to represent a native pointer,
			// so wrap this in an external data object.
			{
				// get the size of the dereferenced type
				MarshallBasicSizer sizer(js, tp, EndOfArg(tp), JS_INVALID_REFERENCE);
				sizer.MarshallValue();

				// create the native pointer wrapper
				Check(JsCreateExternalObjectWithPrototype(
					new NativePointerData(ptr, sizer.size),
					&NativePointerData::Finalize, js->NativePointer_proto, &jsval));
			}
			break;
		}
	}

	virtual void DoStruct() override;
	virtual void DoUnion() override;
	virtual void DoArray() override;

	// process an array of known size
	void DoArrayHelper(const std::vector<size_t> &dims, size_t nElesFlat);

	// pointer to native value we're marshalling
	const void *valp;
};

class JavascriptEngine::MarshallFromNativeStructOrUnion : public MarshallFromNative
{
public:
	MarshallFromNativeStructOrUnion(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd, const void *structp, JsValueRef jsobj) :
		MarshallFromNative(js, sig, sigEnd), structp(static_cast<const BYTE*>(structp)), jsobj(jsobj)
	{ }

	virtual bool Marshall() override = 0;

	bool MarshallCommon(MarshallStructOrUnionSizer &sizer)
	{
		// marshall each struct/union element
		for (; p < sigEnd && *p != '}'; NextArg(), sizer.NextArg())
		{
			// read the tag
			const WCHAR *tagp = p;
			for (; p < sigEnd && *p != ':'; ++p);
			size_t taglen = p - tagp;

			// make a javascript string out of it
			CSTRING jstagc(WideToAnsiCnt(tagp, taglen));
			JsValueRef jstag;
			JsPropertyIdRef propid;
			if (!Check(JsPointerToString(tagp, taglen, &jstag))
				|| !Check(JsCreatePropertyId(jstagc.c_str(), jstagc.length(), &propid)))
				return false;
				
			// skip to the type spec
			if (p < sigEnd && *p == ':')
				++p;

			// retrieve the existing value for this property
			bool hasProp;
			if (!Check(JsHasProperty(jsobj, propid, &hasProp)))
				return false;

			// go to the next field in the sizer, to figure the native offset of this field
			sizer.MarshallValue();

			// Set up a marshaller, using the current value of the property as the 
			// starting point.  This will let us unpack an array into an existing 
			// array, or unpack a sub-struct into an existing referenced object.
			const WCHAR *pEnd = EndOfArg(p);
			MarshallFromNativeValue mv(js, p, pEnd, structp + sizer.lastItemOfs);
			if (hasProp && !Check(JsGetProperty(jsobj, propid, &mv.jsval)))
				return false;

			// marshall the value
			mv.MarshallValue();
			if (mv.error)
				return false;

			// write the value to the object under the current tag name
			if (!Check(JsObjectSetProperty(jsobj, jstag, mv.jsval, false)))
				return false;
		}

		// success
		return true;
	}

	// native structure pointer
	const BYTE *structp;

	// Javascript object we're marshalling the struct contents into
	JsValueRef jsobj;
};

class JavascriptEngine::MarshallFromNativeStruct : public MarshallFromNativeStructOrUnion
{
public:
	MarshallFromNativeStruct(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd, const void *structp, JsValueRef jsobj) :
		MarshallFromNativeStructOrUnion(js, sig, sigEnd, structp, jsobj)
	{ }

	virtual bool Marshall() override
	{
		// set up a struct sizer, to keep track of the native struct layout
		// as we decode its fields
		MarshallStructSizer sizer(js, sig, sigEnd, JS_INVALID_REFERENCE);

		// do the common marshalling
		return MarshallCommon(sizer);
	}
};

class JavascriptEngine::MarshallFromNativeUnion : public MarshallFromNativeStructOrUnion
{
public:
	MarshallFromNativeUnion(JavascriptEngine *js, const WCHAR *sig, const WCHAR *sigEnd, const void *structp, JsValueRef jsobj) :
		MarshallFromNativeStructOrUnion(js, sig, sigEnd, structp, jsobj)
	{ }

	virtual bool Marshall() override
	{
		// set up a union sizer
		MarshallUnionSizer sizer(js, sig, sigEnd, JS_INVALID_REFERENCE);

		// do the common marshalling
		return MarshallCommon(sizer);
	}
};

void JavascriptEngine::MarshallFromNativeValue::DoStruct()
{
	// if we don't already have an object value, create one
	if (jsval == JS_INVALID_REFERENCE || jsval == js->undefVal)
	{
		if (!Check(JsCreateObject(&jsval)))
			return;
	}

	// marshall it through a struct unpacker
	MarshallFromNativeStruct ms(js, p + 3, EndOfArg(p) - 1, valp, jsval);
	ms.Marshall();
}

void JavascriptEngine::MarshallFromNativeValue::DoUnion()
{
	// if we don't already have an object value, create one
	if (jsval == JS_INVALID_REFERENCE || jsval == js->undefVal)
	{
		if (!Check(JsCreateObject(&jsval)))
			return;
	}

	// marshall it through a union unpacker
	MarshallFromNativeUnion ms(js, p + 3, EndOfArg(p) - 4, valp, jsval);
	ms.Marshall();
}

void JavascriptEngine::MarshallFromNativeValue::DoArray()
{
	std::vector<size_t> dims;
	size_t nElesFlat;
	if (!GetArrayDims(dims, nElesFlat, jsval))
		return;

	// If the flat size is zero, this is an indetermine array with no Javascript
	// array to tell us how big it actually is.  We can't marshall this; return
	// it as undefined.
	if (nElesFlat == 0)
	{
		jsval = js->undefVal;
		return;
	}

	// process the array
	DoArrayHelper(dims, nElesFlat);
}

void JavascriptEngine::MarshallFromNativeValue::DoArrayHelper(const std::vector<size_t> &dims, size_t nElesFlat)
{
	// if there isn't already an array object to fill, create a new one
	if (jsval == JS_INVALID_REFERENCE && !Check(JsCreateArray(nElesFlat, &jsval)))
		return;

	// figure the size of the underlying type
	MarshallBasicSizer sizer(js, p, EndOfArg(), JS_INVALID_REFERENCE);
	sizer.MarshallValue();

	// populate the array
	auto arrp = static_cast<const BYTE*>(valp);
	for (size_t i = 0; i < nElesFlat; ++i, arrp += sizer.size)
	{
		// marshall one value
		MarshallFromNativeValue mv(js, sizer.sig, sizer.sigEnd, arrp);
		mv.MarshallValue();

		// store it in the Javascript array
		JsValueRef jsindex;
		if (!Check(JsIntToNumber(i, &jsindex))
			|| !Check(JsSetIndexedProperty(jsval, jsindex, mv.jsval)))
			return;
	}
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
		|| !DefineObjPropFunc(proto, className, "_call", &JavascriptEngine::DllImportCall, this, eh)
		|| !DefineObjPropFunc(proto, className, "_sizeof", &JavascriptEngine::DllImportSizeof, this, eh))
		return false;

	// find the HANDLE object's prototype
	if ((err = GetProp(classObj, global, "HANDLE", subwhere)) != JsNoError
		|| (err = GetProp(HANDLE_proto, classObj, "prototype", subwhere)) != JsNoError)
		return Error(subwhere);

	// set up HANDLE.prototype function bindings
	if (!DefineObjPropFunc(HANDLE_proto, "HANDLE", "toString", &HandleData::ToString, nullptr, eh)
		|| !DefineObjPropFunc(HANDLE_proto, "HANDLE", "toNumber", &HandleData::ToNumber, nullptr, eh))
			return false;

	// save a reference on the handle prototype, as we're hanging onto it
	JsAddRef(HANDLE_proto, nullptr);

	// find the NativePointer object's prototype
	if ((err = GetProp(classObj, global, "NativePointer", subwhere)) != JsNoError
		|| (err = GetProp(NativePointer_proto, classObj, "prototype", subwhere)) != JsNoError)
		return Error(subwhere);

	// set up NativePointer.prototype function bindings
	if (!DefineObjPropFunc(NativePointer_proto, "NativePointer", "toString", &NativePointerData::ToString, nullptr, eh)
		|| !DefineObjPropFunc(NativePointer_proto, "NativePointer", "toNumber", &NativePointerData::ToNumber, nullptr, eh)
		|| !DefineObjPropFunc(NativePointer_proto, "NativePointer", "toArrayBuffer", &NativePointerData::ToArrayBuffer, this, eh))
		return false;

	// save a reference on the handle prototype, as we're hanging onto it
	JsAddRef(NativePointer_proto, nullptr);

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
	if (JsErrorCode err = JsCreateExternalObject(
		new DllImportData(addr, dllName, funcName), 
		&DllImportData::Finalize, &ret); err != JsNoError)
	{
		Throw(err, _T("DllImport.bind()"));
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

// DllImportSizeof is set up in the Javascript as DllImport.prototype._sizeof(),
// an internal method of the DllImport object.  This can be used to retrieve the
// size of a native data structure defined via the Javascript-side C parser.
JsValueRef JavascriptEngine::DllImportSizeof(WSTRING typeInfo)
{
	// measure the size
	MarshallBasicSizer sizer(this, typeInfo);
	sizer.Marshall();

	// return the result
	JsValueRef ret;
	JsIntToNumber(sizer.size, &ret);
	return ret;
}

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

	// set up a temporary allocator for the marshallers
	MarshallerContext tempAlloc(js, jsthis);

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
	MarshallStackArgSizer stackSizer(js, sig, sigEnd, argv, argc, firstDllArg);

	// the remaining items in the signature are the argument types - size them
	if (!stackSizer.Marshall())
		return js->undefVal;

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
	MarshallToNativeArgv argPacker(js, sig, sigEnd, argArray, argv, argc, firstDllArg);
	argPacker.Marshall();

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
		return js->Throw(_T("DllImport.call(): __fastcall calling convention not supported"));

	case 'T':
		return js->Throw(_T("DllImport.call(): __thiscall calling convention not supported"));

	case 'V':
		return js->Throw(_T("DllImport.call(): __vectorcall calling convention not supported"));

	default:
		return js->Throw(_T("DllImport.call(): unknown calling convention in function signature"));
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
	switch (*sig)
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
	//   a pointer to the returned struct (in our allocated memory).
	//
	// - If the return value is a "struct by value" type, and we didn't
	//   allocate the hidden argument, the struct is packed into the return
	//   register(s).
	//
	// - Otherwise, it's a simple scalar value (possibly a pointer, but 
	//   that's still scalar in the sense that it's not a struct) contained
	//   in the return register(s).
	//
	// For all of these types, set up a native-to-javascript marshaller with
	// a pointer to the return value, and marshall it to javascript.
	void *pointerToReturnValue = argPacker.structByValueReturn ? reinterpret_cast<void*>(rawret) : &rawret;
	MarshallFromNativeValue marshallRetVal(js, sig, sigEnd, pointerToReturnValue);
	marshallRetVal.MarshallValue();

	// We also have to marshall back any OUT arguments - that is, arguments
	// in the Javascript argument vector that were passed by reference (Object,
	// Array) that correspond to pointer types in the native argument vector,
	// excluding those with const qualification for the underlying value.
	//
	// The argument packer creates a list of OUT arguments as it marshalls
	// the Javascript arguments to native values, so we just have to go
	// through that list and marshall the out values back to Javascript.
	for (auto const &byRefArg : argPacker.byRefArgs)
	{
		// get the native type signature for the argument
		const WCHAR *argSig = byRefArg.sig.c_str();
		const WCHAR *argSigEnd = argSig + byRefArg.sig.length();

		// skip the pointer/array specifier
		argSig++;

		// get the type of the Javascript value
		JsValueType jstype;
		if ((err = JsGetValueType(byRefArg.jsArg, &jstype)) != JsNoError)
		{
			js->Throw(err, _T("DllImport.call: getting type of by-reference argument"));
			break;
		}

		// All by-reference values are passed on the stack as a pointer.
		// pNativeArg in the byRef struct points to the stack slot, so 
		// reinterpret that as a pointer to a pointer.  Make it a void*
		// for now - we'll determine the actual type from the signature.
		void *valp = *reinterpret_cast<void**>(byRefArg.pNativeArg);

		// check what kind of Javascript by-reference container we have for
		// storing the result
		switch (jstype)
		{
		case JsArray:
			// Array types can be marshalled from from pointers or arrays.
			// They're marshalled the same way in either case: we marshall
			// back the number of elements in the Javascript array from the
			// native array. 
			{
				// set up a value marshaller on the array
				MarshallFromNativeValue mv(js, argSig, argSigEnd, valp);
				mv.jsval = byRefArg.jsArg;

				// get the javascript argument array size
				int len = mv.GetArrayLength(byRefArg.jsArg);
				std::vector<size_t> dims;
				dims.push_back(len);
				size_t nElesFlat = len;

				// now process the result as an array
				mv.DoArrayHelper(dims, nElesFlat);
			}
			break;

		case JsObject:
			// object types can be marshalled back from structs/unions
			if (*argSig == '{')
			{
				if (argSig[1] == 'S')
				{
					MarshallFromNativeStruct ms(js, argSig + 3, argSigEnd - 1, valp, byRefArg.jsArg);
					ms.Marshall();
				}
				else if (argSig[1] == 'U')
				{
					MarshallFromNativeUnion mu(js, argSig + 3, argSigEnd - 1, valp, byRefArg.jsArg);
					mu.Marshall();
				}
			}
			break;
		}
	}

	// done - return the result from the return value marshaller
	return marshallRetVal.jsval;
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

JsValueRef JavascriptEngine::NativePointerData::Create(JavascriptEngine *js, void *ptr, size_t size)
{
	// create the external object
	JsValueRef jsval;
	JsErrorCode err = JsCreateExternalObjectWithPrototype(
		new NativePointerData(ptr, size), &NativePointerData::Finalize,
		js->HANDLE_proto, &jsval);

	// throw an error on failure
	if (err != JsNoError)
	{
		js->Throw(err, _T("NativePointer::Create"));
		return JS_INVALID_REFERENCE;
	}

	// set the length property to the byte length
	JsValueRef lengthVal;
	const TCHAR *where = _T("JsIntToNumber(length)");
	if ((err = JsIntToNumber(size, &lengthVal)) != JsNoError
		|| (err = js->SetReadonlyProp(jsval, "length", lengthVal, where)) != JsNoError)
	{
		js->Throw(err, MsgFmt(_T("NativePointer::Create: %s"), where));
		return JS_INVALID_REFERENCE;
	}

	// return the external object
	return jsval;
}

JsValueRef CALLBACK JavascriptEngine::NativePointerData::ToString(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = NativePointerData::Recover<NativePointerData>(argv[0], _T("NativePointer.toString()")); self != nullptr)
	{
		WCHAR buf[40];
		swprintf_s(buf, L"0x%p[%Iu bytes]", self->ptr, self->size);
		JsPointerToString(buf, wcslen(buf), &ret);
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

JsValueRef CALLBACK JavascriptEngine::NativePointerData::ToArrayBuffer(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = NativePointerData::Recover<NativePointerData>(argv[0], _T("NativePointer.toArrayBuffer()")); self != nullptr)
	{
		JsErrorCode err = JsCreateExternalArrayBuffer(self->ptr, self->size, nullptr, nullptr, &ret);
		if (err != JsNoError)
			static_cast<JavascriptEngine*>(ctx)->Throw(err, _T("NativePointer.toArrayBuffer()"));
	}
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
			MarshallStackArgSizer mas(wrapper->js, wrapper->sig.c_str(), wrapper->sig.c_str() + wrapper->sig.length(), nullptr, 0, 0);
			mas.Marshall();

			// generate the return with argument removal
			addr[10] = 0xc2;    // RET <bytes to remove>
			Put2(addr + 11, mas.nSlots * 4);
		}
		break;

	default:
		wrapper->js->Throw(MsgFmt(_T("DllImport: unsupported calling convention in callback function (%c)"), wrapper->callingConv));
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
	MarshallBasicSizer sizer(wrapper->js, wrapper->argSig);
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
	for (int i = 0; i < 4 && sizer.p < sizer.sigEnd; ++i, sizer.NextArg())
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
	MarshallFromNativeArgv(JavascriptCallbackWrapper *wrapper, void *argv, JsValueRef *jsArgv) :
		Marshaller(wrapper->js, wrapper->sig.c_str(), wrapper->sig.c_str() + wrapper->sig.length()),
		argv(static_cast<arg_t*>(argv)), jsArgv(jsArgv)
	{
		// store the implied 'this' argument in the first slot
		jsArgv[0] = js->undefVal;
		jsArgCur = 1;

		// start at the first argumnet
		curArg = this->argv;
	}

	virtual bool Marshall() override
	{
		// skip the return value type
		NextArg();

		// process each argument from the native vector
		for (; p < sigEnd; NextArg())
		{
			if (*p == '%')
				++p;

			// structs/unions require special handling
			if (*p == '{')
			{
				p[1] == 'S' ? DoStruct() : DoUnion();
			}
			else
			{
				// marshall one value
				MarshallFromNativeValue mv(js, p, EndOfArg(), curArg);
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
					curArg += 2;
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
		// On x86, a struct/union of any size can be passed on the stack.
		// Continue with the arg slot pointer serving as the struct pointer.

#elif defined(_M_X64)
		// On x64, a struct/union under 8 bytes can be passed in a single stack
		// slot/register; anything over 8 bytes is passed by reference.
		if (size > 8)
		{
			// the stack slot actually contains a pointer to the struct
			structp = *reinterpret_cast<void**>(curArg);
			stackSlotSize = sizeof(arg_t);
		}
#else
#error Processor architecture not supported.  Add the appropriate code here to build for this target
#endif
		// process the struct
		MarshallFromNativeValue mv(js, p, EndOfArg(), structp);
		mv.MarshallValue();

		// store it in the javascript argument array
		jsArgv[jsArgCur++] = mv.jsval;

		// skip arguments, rounding up to a DWORD boundary
		curArg += (stackSlotSize + sizeof(arg_t) - 1) / sizeof(arg_t);
	}

	virtual void DoStruct() override
	{
		size_t size = SizeofStruct(JS_INVALID_REFERENCE, _T("DllImport: struct type in callback cannot use indetermine array size"));
		DoStructOrUnion(size);
	}

	virtual void DoUnion() override
	{
		size_t size = SizeofUnion(JS_INVALID_REFERENCE, _T("DllImport: array type in callback cannot use indetermine array size"));
		DoStructOrUnion(size);
	}

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
	MarshallToNativeReturn(JavascriptEngine *js, const WSTRING &sig, JsValueRef jsval, void *hiddenStructp) :
		MarshallToNative(js, sig.c_str(), sig.c_str() + sig.length()),
		jsval(jsval), hiddenStructp(hiddenStructp), retval(0)
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
		Error(_T("DllImport: return value from Javascript callback doesn't fit in return register"));
		return js->marshallerContext->Alloc(size);
	}

	virtual void DoArray() override
	{
		// array returns are invalid
		Error(_T("DllImport: array types is invalid as Javascript callback return"));
	}

	// javascript value we're marshalling
	JsValueRef jsval;

	// Hidden return struct pointer.  If the function returns a struct or union
	// by value, the Microsoft calling conventions require the caller to allocate
	// space for the struct (typically in the caller's local stack frame) and 
	// pass the pointer to the allocated space in a hidden extra argument
	// prepended to the nominal argument list.  This is said pointer.
	void *hiddenStructp;

	// native return value
	UINT64 retval;
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
	JavascriptEngine::MarshallFromNativeArgv m(wrapper, argv, jsArgv);
	m.Marshall();

	// call the Javascript function
	JsValueRef jsResult;
	JsCallFunction(wrapper->jsFunc, jsArgv, argc + 1, &jsResult);

	// marshall the result to native code
	JavascriptEngine::MarshallToNativeReturn mr(wrapper->js, wrapper->sig, jsResult, hiddenStructp);
	mr.MarshallValue();

	// return the marshalling result
	return mr.retval;
}

JavascriptEngine::JavascriptCallbackWrapper::JavascriptCallbackWrapper(
	JavascriptEngine *js, JsValueRef jsFunc, const WCHAR *sig, const WCHAR *sigEnd) :
	js(js), jsFunc(jsFunc), hasHiddenStructArg(false)
{
	// store the calling convention code - it's the first character of the function signature
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
	if (*sig == '{')
	{
		if (sig[1] == 'S')
		{
			MarshallStructSizer ss(js, sig + 3, Marshaller::EndOfArg(sig, sigEnd) - 1, JS_INVALID_REFERENCE);
			ss.Marshall();
			if (ss.size > 8)
				hasHiddenStructArg = true;
		}
		else if (sig[1] == 'U')
		{
			MarshallUnionSizer ss(js, sig + 3, Marshaller::EndOfArg(sig, sigEnd) - 1, JS_INVALID_REFERENCE);
			ss.Marshall();
			if (ss.size > 8)
				hasHiddenStructArg = true;
		}
	}

	// set up a basic marshaller just to count the arguments
	MarshallBasicSizer sizer(js, sig, sigEnd, JS_INVALID_REFERENCE);

	// skip the return value argument
	sizer.NextArg();

	// count arguments
	for (argc = 0; sizer.p < sizer.sigEnd; sizer.NextArg(), ++argc);

	// Create a thunk.  Do this after parsing the arguments, since the thunk
	// generator might take special action based on the argument types.
	thunk = js->codeGenManager.Generate(this);
	if (thunk == nullptr)
	{
		js->Throw(_T("DllImport: unable to create thunk for Javascript callback"));
		return;
	}
}

JavascriptEngine::JavascriptCallbackWrapper::~JavascriptCallbackWrapper()
{
	// delete our thunk by adding it to the free list
	if (thunk != nullptr)
		js->codeGenManager.Recycle(thunk);
}
