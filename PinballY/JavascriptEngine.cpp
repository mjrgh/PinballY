// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//

#include "stdafx.h"
#include "JavascriptEngine.h"

#pragma comment(lib, "ChakraCore.lib")

JavascriptEngine::JavascriptEngine() :
	inited(false),
	srcCookie(1),
	nextTaskID(1.0)
{
}

bool JavascriptEngine::Init(ErrorHandler &eh)
{
	JsErrorCode err;
	auto Error = [&err, &eh](const TCHAR *where)
	{
		eh.SysError(LoadStringT(IDS_ERR_JSINIT), MsgFmt(_T("%s failed: %s"), where, JsErrorToString(err)));
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
	JsSetCurrentContext(JS_INVALID_REFERENCE);
	JsDisposeRuntime(runtime);
}

bool JavascriptEngine::Run(const TCHAR *script, const TCHAR *url, ErrorHandler &eh)
{
	JsErrorCode err;
	auto Error = [&err, &eh](const TCHAR *where)
	{
		eh.SysError(LoadStringT(IDS_ERR_JSRUN), MsgFmt(_T("%s failed: %s"), where, JsErrorToString(err)));
		return false;
	};

	// run the script
	if ((err = JsRunScript(script, srcCookie++, url, nullptr)) != JsNoError && err != JsErrorScriptException)
		return Error(_T("JsRunScript"));

	// check for thrown exceptions
	bool isExc = false;
	if ((err = JsHasException(&isExc)) != JsNoError)
		return Error(_T("JsHasException"));

	if (isExc)
	{
		JsValueRef md;
		if ((err = JsGetAndClearExceptionWithMetadata(&md)) != JsNoError)
			return Error(_T("JsGetAndClearExceptionWithMetadata"));

		const TCHAR *where;
		auto ExcError = [&err, &where, &eh, &Error](const TCHAR *prop)
		{
			return Error(MsgFmt(_T("%s, getting property from exception metadata"), where));
		};

		int lineno, colno;
		JsValueRef exc;
		TSTRING msg, url;
		if ((err = GetProp(lineno, md, "line", where)) != JsNoError)
			return ExcError(_T("line"));
		if ((err = GetProp(colno, md, "column", where)) != JsNoError)
			return ExcError(_T("column"));
		if ((err = GetProp(url, md, "url", where)) != JsNoError)
			return ExcError(_T("url"));
		if ((err = GetProp(exc, md, "exception", where)) != JsNoError)
			return ExcError(_T("exception"));
		if ((err = GetProp(msg, exc, "message", where)) != JsNoError)
			return ExcError(_T("exception.message"));

		// report the scripting error
		eh.Error(MsgFmt(IDS_ERR_JSEXC, msg.c_str(), url.c_str(), lineno + 1, colno + 1));
	}

	// success
	return true;
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
		eh.SysError(LoadStringT(IDS_ERR_JSINITHOST), MsgFmt(_T("Setting up native function callback for %hs: %s failed: %s"),
			name, where, JsErrorToString(err)));
		return false;
	};

	// set the name in the binder object
	func->callbackName = name;

	// get the global object
	JsValueRef global;
	if ((err = JsGetGlobalObject(&global)) != JsNoError)
		return Error(_T("JsGetGlobalObject"));

	// create the property by name
	JsPropertyIdRef propId;
	if ((err = JsCreatePropertyId(name, strlen(name), &propId)) != JsNoError)
		return Error(_T("JsCreatePropertyId"));

	// create the native function wrapper
	JsValueRef funcval;
	if ((err = JsCreateFunction(&NativeFunctionBinderBase::SInvoke, func, &funcval)) != JsNoError)
		return Error(_T("JsCreateFunction"));

	// set the global property
	if ((err = JsSetProperty(global, propId, funcval, true)) != JsNoError)
		return Error(_T("JsSetProperty"));
	
	// success
	return true;
}

void CALLBACK JavascriptEngine::PromiseContinuationCallback(JsValueRef task, void *ctx)
{
	// add the task
	reinterpret_cast<JavascriptEngine*>(ctx)->AddTask(task, 0);
}

double JavascriptEngine::AddTask(JsValueRef func, ULONGLONG dt, LONGLONG interval)
{
	// maintain a counted external reference on the function object as
	// long as we're storing this value, as our task queue storage isn't
	// visible to the Javascript garbage collector
	JsAddRef(func, nullptr);

	// enqueue the task
	double id = nextTaskID++;
	taskQueue.emplace_back(id, func, GetTickCount64() + dt, interval);

	// return the next ID
	return id;
}

void JavascriptEngine::CancelTask(double id)
{
	// search for the task by ID
	for (auto& task : taskQueue)
	{
		if (task.id == id)
		{
			// Found it.  Mark the task as invalid.  We're not allowed
			// to actually delete or unlink the task from the list, 
			// because we could be running in a task right now, meaning
			// that we're nested within a call to RunTasks(), meaning
			// that RunTasks() has an active iterator going on the list.
			// Deleting a task could corrupt that iterator.  So just
			// mark the task as canceled; RunTasks() will eventually
			// do the actual deletion of the task object.
			task.valid = false;

			// Task IDs are unique, so there's no need to look for
			// another copy.
			break;
		}
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
		if (task.readyTime < nextReadyTime)
			nextReadyTime = task.readyTime;
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

		// presume we'll keep the task
		bool keep = true;

		// check the task status
		if (!it->valid)
		{
			// The task has been canceled.  Simply delete it from the
			// queue without invoking it.
			keep = false;
		}
		else if (GetTickCount64() >= it->readyTime)
		{
			// The task is ready to run.  Invoke it.  'this' is the global object.
			JsValueRef global, result;
			JsGetGlobalObject(&global);
			JsCallFunction(it->func, &global, 1, &result);

			// If it's an interval task, and it hasn't been canceled,
			// re-schedule it.  Otherwise, it's now finished, so remove
			// it from the queue.
			if (it->valid && it->interval >= 0.0)
			{
				// it's an interval task - reschedule it
				it->readyTime = GetTickCount64() + it->interval;
			}
			else
			{
				// it's not an interval task, or it's been canceled;
				// the task is now completed
				keep = false;
			}
		}

		// If we're not keeping the task, remove it
		if (!keep)
		{
			// Release the external reference on the task function
			JsRelease(it->func, nullptr);

			// remove the task from the queue
			taskQueue.erase(it);
		}

		// advance to the next task
		it = nxt;
	}
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

