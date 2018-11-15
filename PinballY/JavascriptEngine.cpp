// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//

#include "stdafx.h"
#include "JavascriptEngine.h"
#include "LogFile.h"
#include "../Utilities/FileUtil.h"

#pragma comment(lib, "ChakraCore.lib")

// statics
JavascriptEngine *JavascriptEngine::inst;
double JavascriptEngine::Task::nextId = 1.0;

JavascriptEngine::JavascriptEngine() :
	inited(false),
	nextTaskID(1.0),
	HANDLE_proto(JS_INVALID_REFERENCE),
	NativeObject_proto(JS_INVALID_REFERENCE),
	NativePointer_proto(JS_INVALID_REFERENCE),
	Int64_proto(JS_INVALID_REFERENCE),
	UInt64_proto(JS_INVALID_REFERENCE),
	marshallerContext(nullptr),
	deadObjectScanPending(false)
{
}

bool JavascriptEngine::Init(ErrorHandler &eh)
{
	// if there's already a singleton, there's nothing to do
	if (inst != nullptr)
		return true;

	// create the global singleton
	inst = new JavascriptEngine();
	return inst->InitInstance(eh);
}

bool JavascriptEngine::InitInstance(ErrorHandler &eh)
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

	// Initialize our internal symbol properties, which we use for private
	// properties of some of our objects.
	JsValueRef symName, symbol;
	JsPointerToString(L"Thunk", 5, &symName);
	JsCreateSymbol(symName, &symbol);
	JsGetPropertyIdFromSymbol(symbol, &callbackPropertyId);
	
	JsPointerToString(L"xref", 4, &symName);
	JsCreateSymbol(symName, &symbol);
	JsGetPropertyIdFromSymbol(symbol, &xrefPropertyId);

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

	// Delete any remaining Javascript-allocated native objects.  These
	// were kept alive by inbound references from Javascript objects, but
	// the Javascript objects are all being deleted now.
	for (auto &it : nativeDataMap)
		delete[] it.first;
	
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
			keep = task->Execute();
		}

		// If we're not keeping the task, remove it
		if (!keep)
			taskQueue.erase(it);

		// advance to the next task
		it = nxt;
	}
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
	auto const *cookie = &inst->sourceCookies.emplace_back(path.c_str());

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
		inst->Throw(err, _T("ModuleParseTask"));

	// this is a one shot - don't reschedule
	return false;
}

bool JavascriptEngine::ModuleEvalTask::Execute()
{
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
	Marshaller(const WCHAR *sig, const WCHAR *sigEnd) :
		sig(sig), sigEnd(sigEnd), p(sig),
		error(false), isConst(false)
	{ }

	Marshaller(const Marshaller &m) :
		sig(m.sig), sigEnd(m.sigEnd), p(m.sig),
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

	const double MaxIntInDouble = static_cast<double>(2LL << DBL_MANT_DIG);

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

	// error flag
	bool error;

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

	// Get the length of a Javascript array value
	int GetArrayLength(JsValueRef jsval)
	{
		// read the length property
		int len;
		const TCHAR *where;
		if (JsErrorCode err = inst->GetProp(len, jsval, "length", where); err != JsNoError)
		{
			Error(err, MsgFmt(_T("DllImport: getting length of array argument"), where));
			return -1;
		}

		// make sure the length is non-negative
		return max(len, 0);
	}

	// Parse one array dimension.  Advances p to the character after the ']'.
	static bool ParseArrayDim(const WCHAR* &p, const WCHAR *endp, size_t &dim, bool &empty)
	{
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
		size_t acc = 0;
		for (; p < endp && *p >= '0' && *p <= '9'; ++p)
		{
			acc *= 10;
			acc += static_cast<size_t>(*p - '0');
		}

		// ensure it ends with ']'
		if (p >= endp || *p != ']')
			return false;

		// skip the ']'
		++p;

		// return the results
		dim = acc;
		empty = false;
		return true;
	}

	// Get the array dimension for an argument value.  This can be
	// used to figure the actual size needed for an indeterminate array
	// argument or struct element.
	bool GetActualArrayDim(JsValueRef jsval, size_t &dim, size_t eleSize)
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
					dim = static_cast<size_t>(i);
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
						Error(err, _T("DllImport: getting typed array information"));
						return false;
					}

					dim = static_cast<size_t>(arrayByteLength / eleSize);
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
	MarshallSizer(const WCHAR *sig, const WCHAR *sigEnd)
		: Marshaller(sig, sigEnd)
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
	virtual void DoVoid() override { /* void types have zero size */ }

	// process a function type
	virtual void DoFunction() override { Error(_T("DllImport: function types can't be passed by value")); }
};

// Basic sizer.  This simply adds up the sizes for the measured types without
// regard to alignment.  This is useful mostly to get the size of single type.
class JavascriptEngine::MarshallBasicSizer : public MarshallSizer
{
public:
	MarshallBasicSizer(const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval) :
		MarshallSizer(sig, sigEnd),
		jsval(jsval), size(0), align(0), flex(false)
	{ }

	MarshallBasicSizer(const WSTRING &sig, JsValueRef jsval = JS_INVALID_REFERENCE) :
		MarshallSizer(sig.c_str(), sig.c_str() + sig.length()),
		jsval(jsval), size(0), align(0), flex(false)
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
		size_t dim;
		bool isEmpty;
		if (!ParseArrayDim(p, sigEnd, dim, isEmpty))
			return;

		// note if we found a flexible array dimension
		if (isEmpty)
			flex = true;

		// Figure the size of the underlying type.  Flex arrays aren't 
		// allowed beyond the first dimension of a multi-dimensional array,
		// so we don't have to pass an actual value to measure.
		MarshallBasicSizer sizer(p, EndOfArg(), JS_INVALID_REFERENCE);
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
		Error(_T("DllImport: attempting to take the size of a native function; this is an invalid operation"));
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
	MarshallStructOrUnionSizer(const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval) :
		MarshallBasicSizer(sig, sigEnd, jsval),
		lastItemOfs(0), lastItemSize(0), size(0), align(0), flexError(false)
	{ }

	// Offset of the last item marshalled.  For a struct, this is the aligned
	// offset of the last item.  For a union, this is always zero, since union
	// members are all overlaid on the same memory.
	size_t lastItemOfs;

	// size of the last item
	size_t lastItemSize;

	// total size of the struct, including padding
	size_t size;

	// alignment
	size_t align;

	// flex member error detected and reported
	bool flexError;

	// current property name and type signature
	WSTRING curProp;
	WSTRING curPropType;

	// the current value is the property value
	virtual JsValueRef GetCurVal() override 
	{ 
		// if there's an object and a current property, retrieve the property
		JsValueRef curval = inst->undefVal;
		if (jsval != JS_INVALID_REFERENCE && curProp.length() != 0)
		{
			const TCHAR *where;
			if (JsErrorCode err = inst->GetProp(curval, jsval, WSTRINGToCSTRING(curProp).c_str(), where); err != JsNoError)
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
			// save the tag name
			curProp.assign(tag, p - tag);

			// skip the ':'
			++p;

			// save the type
			curPropType.assign(p, EndOfArg(p));
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
	MarshallStructSizer(const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval) :
		MarshallStructOrUnionSizer(sig, sigEnd, jsval), ofs(0)
	{ }

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
	size_t ofs;
};

// Union size counter
class JavascriptEngine::MarshallUnionSizer : public MarshallStructOrUnionSizer
{
public:
	MarshallUnionSizer(const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval)
		: MarshallStructOrUnionSizer(sig, sigEnd, jsval)
	{ }

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
	MarshallStructSizer s(p + 3, EndOfArg() - 1, GetCurVal());
	s.Marshall();

	// add it to our overall size
	AddStruct(s.size, s.align, 1);
}

void JavascriptEngine::MarshallSizer::DoUnion()
{
	// measure the union size
	MarshallUnionSizer s(p + 3, EndOfArg() - 1, GetCurVal());
	s.Marshall();

	// add it to our overall size
	AddStruct(s.size, s.align, 1);
}

size_t JavascriptEngine::Marshaller::SizeofStruct(JsValueRef jsval, const TCHAR *flexErrorMsg)
{
	// measure the struct size
	MarshallStructSizer s(p + 3, EndOfArg() - 1, jsval);
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
	MarshallUnionSizer s(p + 3, EndOfArg() - 1, jsval);
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
	MarshallToNative(const WCHAR *sig, const WCHAR *sigEnd) :
		Marshaller(sig, sigEnd)
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

	virtual void DoVoid() override { Error(_T("DllImport: 'void' arguments are invalid")); }

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
				case 't':  typeOk = (arrType == JsArrayTypeInt8 || arrType == JsArrayTypeUint8); break;
				case 'T':  typeOk = (arrType == JsArrayTypeInt16 || arrType == JsArrayTypeUint16); break;
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
		// if it's an INT64 or UINT64, convert it to number explicitly
		JsValueType type;
		if (JsGetValueType(v, &type) != JsNoError && type == JsObject)
		{
			// check for Int64
			if (auto obj = XInt64Data<INT64>::Recover<XInt64Data<INT64>>(v, nullptr); obj != nullptr)
			{
				// range-check the signed value
				if (obj->i < static_cast<INT64>(-MaxIntInDouble) || obj->i > static_cast<INT64>(MaxIntInDouble))
					Error(_T("DllImport: Int64 value is out of range for conversion to Number"));

				// cast it and return the result
				return static_cast<double>(obj->i);
			}

			// check for Uint64
			if (auto obj = XInt64Data<UINT64>::Recover<XInt64Data<UINT64>>(v, nullptr); obj != nullptr)
			{
				// range-check the unsigned value
				if (obj->i > static_cast<UINT64>(MaxIntInDouble))
					Error(_T("DllImport: Int64 value is out of range for conversion to Number"));

				// cast it and return the result
				return static_cast<double>(obj->i);
			}
		}

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
				Error(_T("DllImport: 64-bit integer argument out of range"));
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
					Error(_T("DllImport: 64-bit unsigned integer argument value is negative"));
				return obj->i;
			}
			if (auto obj = XInt64Data<UINT64>::Recover<XInt64Data<UINT64>>(v, nullptr); obj != nullptr)
			{
				// if the caller wants a signed result, range-check the unsigned value
				if (isSigned && obj->i > static_cast<UINT64>(INT64_MAX))
					Error(_T("DllImport: 64-bit signed integer argument out of range"));
				return obj->i;
			}
		}

		// otherwise, interpret it as a string value
		JsValueRef strval;
		if ((err = JsConvertValueToString(v, &strval)) != JsNoError)
		{
			Error(err, _T("DllImport: converting 64-bit integer argument value to string"));
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
	MarshallStackArgSizer(const WCHAR *sig, const WCHAR *sigEnd, JsValueRef *argv, int argc, int firstArg) :
		MarshallSizer(sig, sigEnd),
		jsArgv(argv), jsArgc(argc), jsArgCur(firstArg), nSlots(0), hiddenStructArg(false)
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
	MarshallToNativeArgv(const WCHAR *sig, const WCHAR *sigEnd,
		arg_t *nativeArgArray, JsValueRef *argvIn, int argcIn, int firstDllArg) :
		MarshallToNative(sig, sigEnd),
		nativeArgArray(nativeArgArray), argOut(nativeArgArray), 
		argvIn(argvIn), argcIn(argcIn), argInCur(firstDllArg), firstDllArg(firstDllArg),
		structByValueReturn(JS_INVALID_REFERENCE), structByValueReturnPtr(nullptr), structByValueReturnSize(0)
	{ }

	// Flag: we allocated a hidden struct/union for a by-value return
	JsValueRef structByValueReturn;

	// pointer to and byte size of the native data of the by-value return struct
	void *structByValueReturnPtr;
	size_t structByValueReturnSize;

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
		if (*p == '{')
		{
			// create the native object
			NativeTypeWrapper *wrapper;
			structByValueReturn = inst->CreateNativeObject(p, EndOfArg(), wrapper);

			// if the struct size is over 8 bytes, add the hiddden argument
			structByValueReturnSize = (p[1] == 'S' ? SizeofStruct(JS_INVALID_REFERENCE, nullptr) : SizeofUnion(JS_INVALID_REFERENCE, nullptr));
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
	MarshallToNativeArray(const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsArray,
		void *nativeArray, size_t eleSize, int nEles) :
		MarshallToNative(sig, sigEnd),
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
	MarshallToNativeByReference(const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval) :
		MarshallToNative(sig, sigEnd),
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
	virtual void *Alloc(size_t size, int nItems = 1) override { return pointer = inst->marshallerContext->Alloc(size * nItems); }

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
	MarshallToNativeStruct(const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval, void *pointer, size_t size)
		: MarshallToNative(sig, sigEnd),
		structSizer(sig, sigEnd, jsval),
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

		// check for a native struct
		if (auto obj = NativeTypeWrapper::Recover<NativeTypeWrapper>(jsval, nullptr); obj != nullptr)
		{
			// the type signature has to match exactly
			if (obj->sig != sig)
			{
				Error(_T("DllImport: wrong struct/union type for argument"));
				return false;
			}

			// copy the data from the source object
			memcpy(pointer, obj->data, size);

			// success
			return true;
		}

		// Visit each field.  The signature is of the form {S fields}, so note
		// that we skip the "{S " prefix and the "}" suffix.
		for (p += 3; p < sigEnd - 1; NextArg(), structSizer.NextArg())
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
	MarshallToNativeUnion(const WCHAR *sig, const WCHAR *sigEnd, JsValueRef jsval, void *pointer, size_t size) :
		MarshallToNativeStruct(sig, sigEnd, jsval, pointer, size)
	{ }

	// union members always go at the start of the shared memory area
	virtual void *Alloc(size_t size, int nItems = 1) override { return pointer; }
};

void JavascriptEngine::MarshallToNative::DoArrayCommon(JsValueRef jsval)
{
	// Parse the array dimension.  Note that this only parses the first 
	// dimension; if there are multiple dimensions, the others will fall
	// out of the recursive measurement of the underlying type size.
	size_t dim;
	bool isEmpty;
	if (!ParseArrayDim(p, sigEnd, dim, isEmpty))
		return;

	// figure the size of the underlying type
	MarshallBasicSizer sizer(p, EndOfArg(), jsval);
	sizer.MarshallValue();

	// indeterminate size isn't allowed in a sub-array
	if (sizer.flex)
	{
		Error(_T("DllImport: sub-array with indeterminate dimension is invalid"));
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
		MarshallToNativeArray ma(p, EndOfArg(), jsval, Alloc(sizer.size, dim), sizer.size, dim);
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
				MarshallBasicSizer sizer(p + 1, EndOfArg(), jsval);
				sizer.MarshallValue();

				// allocate temporary storage for the array copy
				void *pointer = inst->marshallerContext->Alloc(sizer.size * len);

				// marshall the array values into the native array
				MarshallToNativeArray ma(p + 1, EndOfArg(), jsval, pointer, sizer.size, len);
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
			if ((err = JsHasOwnProperty(jsval, inst->callbackPropertyId, &hasThunk)) != JsNoError
				|| (hasThunk && (err = JsGetProperty(jsval, inst->callbackPropertyId, &thunk)) != JsNoError))
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
				wrapper = new JavascriptCallbackWrapper(jsval, tp + 1, EndOfArg(tp) - 1);
				if ((err = JsCreateExternalObject(wrapper, &JavascriptCallbackWrapper::Finalize, &thunk)) != JsNoError)
				{
					Error(err, _T("DllImport: creating callback function thunk external object"));
					return;
				}

				// Cross-reference the function and the external thunk object.  This
				// will keep the thunk alive as long as the function is alive, and
				// vice versa, ensuring they're collected as a pair.
				if ((err = JsSetProperty(thunk, inst->callbackPropertyId, jsval, true)) != JsNoError
					|| (err = JsSetProperty(jsval, inst->callbackPropertyId, thunk, true)) != JsNoError)
				{
					Error(err, _T("DllImport: setting callback function/thunk cross-references"));
					return;
				}
			}

			// Pass the thunk from the wrapper to the native code to use as the callback
			Store(wrapper->thunk);
		}
		break;

	case JsObject:
		{
			const TCHAR *where = nullptr;
			WSTRING toSig(p + 1, EndOfArg(p + 1) - tp);
			if (auto nativeObj = NativeTypeWrapper::Recover<NativeTypeWrapper>(jsval, where); nativeObj != nullptr)
			{
				// Native data.  Check that it's compatible with the underlying type.
				if (IsPointerConversionValid(SkipPointerOrArrayQual(nativeObj->sig.c_str()), toSig.c_str()))
					Store(nativeObj->data);
				else
					Error(_T("Incompatible pointer type conversion"));
			}
			else if (auto nativePtr = NativePointerData::Recover<NativePointerData>(jsval, where); nativePtr != nullptr)
			{
				// Native pointer.  Check that it's compatible with the underlying type.
				if (IsPointerConversionValid(SkipPointerOrArrayQual(nativePtr->sig.c_str()), toSig.c_str()))
					Store(nativePtr->ptr);
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
			MarshallToNativeByReference mbr(p + 1, EndOfArg(), jsval);
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
	MarshallToNativeStruct ms(p, EndOfArg(), jsval, pointer, size);
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
	MarshallToNativeUnion mu(p, EndOfArg(), jsval, pointer, size);
	mu.Marshall();
}

class JavascriptEngine::MarshallFromNative : public Marshaller
{
public:
	MarshallFromNative(const WCHAR *sig, const WCHAR *sigEnd) :
		Marshaller(sig, sigEnd)
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
	MarshallFromNativeValue(const WCHAR *sig, const WCHAR *sigEnd, void *valp) :
		MarshallFromNative(sig, sigEnd),
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
			Error(_T("DllImport: unrecognized string type code in type signature"));
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
		Check(NativePointerData::Create(ptr, size, p, EndOfArg(p) - p, *tp, &jsval));
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
			&HandleData::Finalize, inst->HANDLE_proto, &jsval));
	}

	virtual void DoFunction() override { Error(_T("DllImport: function can't be returned by value (pointer required)")); }

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
		JsValueRef jsthis = inst->marshallerContext->jsthis;
		if ((err = inst->GetProp(bindExt, jsthis, "_bindExt", where)) != JsNoError)
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
				MarshallBasicSizer sizer(tp, EndOfArg(tp), JS_INVALID_REFERENCE);
				sizer.MarshallValue();

				// create the native pointer wrapper
				Check(NativePointerData::Create(ptr, sizer.size, tp, sizer.sigEnd - tp, 0, &jsval));
			}
			break;
		}
	}

	virtual void DoStruct() override;
	virtual void DoUnion() override;
	virtual void DoArray() override;

	// pointer to native value we're marshalling
	void *valp;
};


void JavascriptEngine::MarshallFromNativeValue::DoStruct()
{
	// marshall the native struct
	NativeTypeWrapper *obj = nullptr;
	WSTRING structSig(p, EndOfArg(p));
	jsval = inst->CreateNativeObject(structSig, valp);
}

void JavascriptEngine::MarshallFromNativeValue::DoUnion()
{
	// marshall the native union
	NativeTypeWrapper *obj = nullptr;
	WSTRING unionSig(p, EndOfArg(p));
	jsval = inst->CreateNativeObject(unionSig, valp);
}

void JavascriptEngine::MarshallFromNativeValue::DoArray()
{
	NativeTypeWrapper *obj = nullptr;
	WSTRING arraySig(p, EndOfArg(p));
	jsval = inst->CreateNativeObject(arraySig, valp);
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
		|| !DefineObjPropFunc(proto, className, "_sizeof", &JavascriptEngine::DllImportSizeof, this, eh)
		|| !DefineObjPropFunc(proto, className, "_create", &JavascriptEngine::DllImportCreate, this, eh))
		return false;

	// find the HANDLE object's prototype
	if ((err = GetProp(classObj, global, "HANDLE", subwhere)) != JsNoError
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

	// find the NativeObject object's prototype
	if ((err = GetProp(classObj, global, "NativeObject", subwhere)) != JsNoError
		|| (err = GetProp(NativeObject_proto, classObj, "prototype", subwhere)) != JsNoError)
		return Error(subwhere);

	// set up NativeObject methods
	if (!DefineObjPropFunc(classObj, "NativeObject", "addressOf", &NativeTypeWrapper::AddressOf, this, eh))
		return false;

	// find the NativePointer object's prototype
	if ((err = GetProp(classObj, global, "NativePointer", subwhere)) != JsNoError
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
		|| !AddGetter(NativePointer_proto, "at", &NativePointerData::At, this)
		|| !DefineObjPropFunc(classObj, "NativePointer", "fromNumber", &NativePointerData::FromNumber, this, eh))
		return false;

	// save a reference on the handle prototype, as we're hanging onto it
	JsAddRef(NativePointer_proto, nullptr);

	// set up the INT64 native type
	if ((err = GetProp(classObj, global, "Int64", subwhere)) != JsNoError
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
		|| !DefineObjPropFunc(classObj, "Int64", "_new", &XInt64Data<INT64>::Create, this, eh))
		return false;

	JsAddRef(Int64_proto, nullptr);

	// set up the UINT64 native type
	if ((err = GetProp(classObj, global, "Uint64", subwhere)) != JsNoError
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
		|| !DefineObjPropFunc(UInt64_proto, "UInt64", "and", &XInt64Data<UINT64>::And, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "UInt64", "or", &XInt64Data<UINT64>::Or, this, eh)
		|| !DefineObjPropFunc(UInt64_proto, "UInt64", "not", &XInt64Data<UINT64>::Not, this, eh)
		|| !DefineObjPropFunc(classObj, "UInt64", "_new", &XInt64Data<UINT64>::Create, this, eh))
		return false;

	JsAddRef(Int64_proto, nullptr);

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
	MarshallBasicSizer sizer(typeInfo);
	sizer.Marshall();

	// return the result
	JsValueRef ret;
	JsIntToNumber(sizer.size, &ret);
	return ret;
}

// DllImportCreate is set up in the Javascript as DllImport.prototype._create(),
// an internal method of the DllImport object.  This creates an instance of a
// native type, for use in calls to native code.
JsValueRef JavascriptEngine::DllImportCreate(WSTRING typeInfo)
{
	return CreateNativeObject(typeInfo);
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

	// we need at least two arguments (
	if (argc < 3)
		return inst->Throw(_T("DllImport.call(): missing arguments"));

	// get the javascript this pointer
	int ai = 0;
	auto jsthis = argv[ai++];

	// set up a temporary allocator for the marshallers
	MarshallerContext tempAlloc(jsthis);

	// get the native function object
	auto func = DllImportData::Recover<DllImportData>(argv[ai++], _T("DllImport.call()"));
	auto funcPtr = func->procAddr;

	// get the function signature, as a string
	const WCHAR *sig;
	size_t sigLen;
	if ((err = JsStringToPointer(argv[ai++], &sig, &sigLen)) != JsNoError)
		return inst->Throw(err, _T("DllImport.call()"));

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
	MarshallStackArgSizer stackSizer(sig, sigEnd, argv, argc, firstDllArg);

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
	MarshallToNativeArgv argPacker(sig, sigEnd, argArray, argv, argc, firstDllArg);
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
		return inst->Throw(_T("DllImport.call(): __fastcall calling convention not supported"));

	case 'T':
		return inst->Throw(_T("DllImport.call(): __thiscall calling convention not supported"));

	case 'V':
		return inst->Throw(_T("DllImport.call(): __vectorcall calling convention not supported"));

	default:
		return inst->Throw(_T("DllImport.call(): unknown calling convention in function signature"));
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
		MarshallFromNativeValue marshallRetVal(sig, sigEnd, &rawret);
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
	return JsCreateExternalObjectWithPrototype(
		new HandleData(h), &HandleData::Finalize, inst->HANDLE_proto, &jsval);
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
		if (d > static_cast<double>(1LL < DBL_MANT_DIG))
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
// Native pointer type
//

JavascriptEngine::NativePointerData::NativePointerData(
	void *ptr, size_t size, const WCHAR *sig, size_t sigLen, WCHAR stringType) :
	ptr(ptr), size(size), sig(sig, sigLen) 
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
	void *ptr, size_t size, const WCHAR *sig, size_t sigLen, WCHAR stringType, JsValueRef *jsval)
{
	// if the native value is a null pointer, return Javascript null
	if (ptr == nullptr)
	{
		*jsval = inst->nullVal;
		return JsNoError;
	}

	// create the external object
	JsErrorCode err;
	if ((err = JsCreateExternalObjectWithPrototype(
		new NativePointerData(ptr, size, sig, sigLen, stringType),
		&NativePointerData::Finalize, inst->NativePointer_proto, jsval)) != JsNoError)
		return err;

	// set the length property to the byte length
	JsValueRef lengthVal;
	const TCHAR *where = _T("JsIntToNumber(length)");
	if ((err = JsIntToNumber(size, &lengthVal)) != JsNoError
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
		// Javscript's generic Object.toString() format of "[Object Class]"
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
		SSIZE_T maxLength = -1;
		SSIZE_T length = -1;
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
						ThrowSimple("NativePointer.toStringZ(): invalid codePage option");
				}
				else
					ThrowSimple("NativePointer.toStringZ(): invalid codePage option");
			}

			// options.maxLength = number
			if (JsCreatePropertyId("maxLength", 9, &propid) == JsNoError
				&& JsGetProperty(argv[1], propid, &propval) == JsNoError
				&& JsConvertValueToNumber(propval, &numval) == JsNoError
				&& JsNumberToDouble(propval, &d) == JsNoError)
				maxLength = static_cast<SSIZE_T>(d);

			// options.length = number
			if (JsCreatePropertyId("length", 6, &propid) == JsNoError
				&& JsGetProperty(argv[1], propid, &propval) == JsNoError
				&& JsConvertValueToNumber(propval, &numval) == JsNoError
				&& JsNumberToDouble(propval, &d) == JsNoError)
				length = static_cast<SSIZE_T>(d);
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
						length = maxLength >= 0 ? strnlen_s(cstr, maxLength) : strlen(cstr);

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
					length = maxLength >= 0 ? wcsnlen_s(wstr, maxLength) : wcslen(wstr);

				// apply the maximum length if specified, even if a length was specified
				if (maxLength >= 0 && length > maxLength)
					length = maxLength;

				// create the Javascript result
				JsPointerToString(wstr, length, &ret);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				ThrowSimple("Memory at native pointer is unreadable, or string is unterminated");
			}
			break;

		default:
			// other types can't be interpreted as strings
			ThrowSimple("Native pointer does not point to a string type");
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
	if (JsErrorCode err = Create(reinterpret_cast<void*>(i), 0, _T("v"), 1, 0, &jsval); err != JsNoError)
		inst->Throw(err, _T("NativePointer.fromNumber"));

	// return the object
	return jsval;
}

JsValueRef CALLBACK JavascriptEngine::NativePointerData::ToArrayBuffer(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = NativePointerData::Recover<NativePointerData>(argv[0], _T("NativePointer.toArrayBuffer()")); self != nullptr)
	{
		// create the array buffer
		JsErrorCode err = JsCreateExternalArrayBuffer(self->ptr, self->size, nullptr, nullptr, &ret);
		if (err != JsNoError)
			return inst->Throw(err, _T("NativePointer.toArrayBuffer(), creating ArrayBuffer object"));

		// Add a cross-reference from the ArrayBuffer to the pointer.  This will 
		// the pointer alive as long as the ArrayBuffer object is alive, which will
		// in turn keep the underlying native storage alive, since the dead object
		// scanner will see our pointer into the native storage.
		if ((err = JsSetProperty(ret, inst->xrefPropertyId, argv[0], true)) != JsNoError)
			return inst->Throw(err, _T("NativePointer.toArrayBuffer(), setting xref"));
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
		// create an array of nEles of our referenced type
		WSTRINGEx sig;
		sig.Format(_T("[%Iu]%s"), static_cast<size_t>(nEles), self->sig.c_str());
		ret = inst->CreateNativeObject(sig, self->ptr);
	}

	return ret;
}

JsValueRef CALLBACK JavascriptEngine::NativePointerData::At(JsValueRef callee, bool isConstructCall,
	JsValueRef *argv, unsigned short argc, void *ctx)
{
	JsValueRef ret = JS_INVALID_REFERENCE;
	if (auto self = NativePointerData::Recover<NativePointerData>(argv[0], _T("NativePointer.toArrayBuffer()")); self != nullptr)
	{
		// we can't dereference a null pointer
		if (self->ptr == nullptr)
			return inst->Throw(_T("Attempting to derefeference a null native pointer (pointer.at())"));

		// we can't dereference a void pointer
		if (self->size == 0 || self->sig == L"v" || self->sig == L"%v")
			return inst->Throw(_T("Native pointer to 'void' can't be dereferenced (pointer.at())"));

		// test the memory area to make sure it's valid
		__try
		{
			// Try reading from the memory.  Declare the referenced memory as
			// volatile so that the compiler doesn't try to optimize away the
			// references.
			volatile BYTE a, b;
			volatile BYTE *p = static_cast<volatile BYTE*>(self->ptr);
			a = p[0];
			b = (self->size != 0 ? p[self->size - 1] : 0);

			// If the referenced type isn't const ('%' prefix), try writing back
			// the same values to make sure the memory is writable.
			if (self->sig[0] != '%')
			{
				p[0] = a;
				if (self->size != 0)
					p[self->size - 1] = b;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return inst->Throw(_T("Bad native pointer dereference: referenced memory location is invalid or inaccessible (pointer.at())"));
		}

		// looks good - create the native data wrapper
		ret = inst->CreateNativeObject(self->sig, self->ptr);
	}
	return ret;
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
			void *extdata;
			if ((err = JsGetExternalData(jsval, &extdata)) != JsNoError)
			{
				inst->Throw(err, _T("Int64 math"));
				return 0;
			}

			if (auto b = XInt64Data<INT64>::Recover<XInt64Data<INT64>>(extdata, nullptr); b != nullptr)
				return static_cast<T>(b->i);
			else if (auto b = XInt64Data<UINT64>::Recover<XInt64Data<UINT64>>(extdata, nullptr); b != nullptr)
				return static_cast<T>(b->i);
			else
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
	// create a new object to represent the result
	auto extobj = new XInt64Data<T>(val);

	// wrap it in a javsacript external object
	JsErrorCode err;
	if ((err = JsCreateExternalObjectWithPrototype(extobj, XInt64Data<T>::Finalize,
		std::is_signed<T>::value ? inst->Int64_proto : inst->UInt64_proto, &jsval)) != JsNoError)
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

		// if the value is negative, start with a sign
		bool neg = false;
		if constexpr (std::is_signed<T>::value)
		{
			if (i < 0)
			{
				neg = true;
				i = -i;
			}
		}

		// special case if the value is zero
		if (i == 0)
		{
			*--p = '0';
		}
		else
		{
			// work backwards through the digits from the least significant end
			while (i != 0)
			{
				int digit = static_cast<int>(i % radix);
				if (digit <= 9)
					*--p = static_cast<WCHAR>(digit + '0');
				else
					*--p = static_cast<WCHAR>(digit - 10 + 'A');

				i /= radix;
			}

			// add the sign
			if (neg)
				*--p = '-';
		}

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

	T result;
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

			// do the math
			result = op(a, static_cast<T>(d));
		}
		break;

	case JsObject:
		// we can operate on other 64-bit types
		{
			if (auto b = XInt64Data<INT64>::Recover<XInt64Data<INT64>>(argv[1], nullptr); b != nullptr)
				result = op(a, static_cast<T>(b->i));
			else if (auto b = XInt64Data<UINT64>::Recover<XInt64Data<UINT64>>(argv[1], nullptr); b != nullptr)
				result = op(a, static_cast<T>(b->i));
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

			result = op(a, b);
		}
		break;
	}

	// create a new object to represent the result
	JsValueRef newobj;
	if ((err = CreateFromInt(result, newobj)) != JsNoError)
		return  inst->Throw(err, _T("Int64 math"));

	// return the result object
	return newobj;
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
			MarshallStackArgSizer mas(wrapper->sig.c_str(), wrapper->sig.c_str() + wrapper->sig.length(), nullptr, 0, 0);
			mas.Marshall();

			// generate the return with argument removal
			addr[10] = 0xc2;    // RET <bytes to remove>
			Put2(addr + 11, mas.nSlots * 4);
		}
		break;

	default:
		inst->Throw(MsgFmt(_T("DllImport: unsupported calling convention in callback function (%c)"), wrapper->callingConv));
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
		Marshaller(wrapper->sig.c_str(), wrapper->sig.c_str() + wrapper->sig.length()),
		argv(static_cast<arg_t*>(argv)), jsArgv(jsArgv)
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
				MarshallFromNativeValue mv(p, EndOfArg(), curArg);
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
		if (size > 8)
		{
			// the stack slot contains a pointer to the struct
			structp = *reinterpret_cast<void**>(curArg);
			stackSlotSize = sizeof(arg_t);
		}
#else
#error Processor architecture not supported.  Add the appropriate code here to build for this target
#endif
		// process the struct
		MarshallFromNativeValue mv(p, EndOfArg(), structp);
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
	MarshallToNativeReturn(const WSTRING &sig, JsValueRef jsval, void *hiddenStructp) :
		MarshallToNative(sig.c_str(), sig.c_str() + sig.length()),
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
		return inst->marshallerContext->Alloc(size);
	}

	virtual void DoArray() override
	{
		// array returns are invalid
		Error(_T("DllImport: array types is invalid as Javascript callback return"));
	}

	virtual void DoVoid() override { /* void return - we have nothing to do */ }

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
	JavascriptEngine::MarshallToNativeReturn mr(wrapper->sig, jsResult, hiddenStructp);
	mr.MarshallValue();

	// return the marshalling result
	return mr.retval;
}

JavascriptEngine::JavascriptCallbackWrapper::JavascriptCallbackWrapper(
	JsValueRef jsFunc, const WCHAR *sig, const WCHAR *sigEnd) :
	jsFunc(jsFunc), hasHiddenStructArg(false)
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
			MarshallStructSizer ss(sig + 3, Marshaller::EndOfArg(sig, sigEnd) - 1, JS_INVALID_REFERENCE);
			ss.Marshall();
			if (ss.size > 8)
				hasHiddenStructArg = true;
		}
		else if (sig[1] == 'U')
		{
			MarshallUnionSizer ss(sig + 3, Marshaller::EndOfArg(sig, sigEnd) - 1, JS_INVALID_REFERENCE);
			ss.Marshall();
			if (ss.size > 8)
				hasHiddenStructArg = true;
		}
	}

	// set up a basic marshaller just to count the arguments
	MarshallBasicSizer sizer(sig, sigEnd, JS_INVALID_REFERENCE);

	// skip the return value argument
	sizer.NextArg();

	// count arguments
	for (argc = 0; sizer.p < sizer.sigEnd; sizer.NextArg(), ++argc);

	// Create a thunk.  Do this after parsing the arguments, since the thunk
	// generator might take special action based on the argument types.
	thunk = inst->codeGenManager.Generate(this);
	if (thunk == nullptr)
	{
		inst->Throw(_T("DllImport: unable to create thunk for Javascript callback"));
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

JsValueRef JavascriptEngine::CreateNativeObject(const WSTRING &sig, void *data, NativeTypeWrapper **pCreatedObj)
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

	// we can't have an empty signature, obviously
	if (sig.length() == 0)
		return Error(_T("DllImport: creating native object: missing type signature"));

	// set up to scan the signature
	const WCHAR *p = sig.c_str();
	const WCHAR *sigEnd = p + sig.length();

	// get the size of the object
	MarshallBasicSizer sizer(p, sigEnd, JS_INVALID_REFERENCE);
	if (!sizer.Marshall() || sizer.error)
		return JS_INVALID_REFERENCE;

	// Zero-size objects (e.g., void or void[5]) are invalid
	if (sizer.size == 0)
		return Error(_T("DllImport: creating native object: can't create type with zero size"));

	// Look up the prototype object for the native type signature in
	// our type cache.  If we've encountered this same type before,
	// we can use the same prototype for it again; otherwise we'll
	// have to create a new prototype for a data view object for this
	// type signature.
	NativeTypeCacheEntry *entry = nullptr;
	if (auto it = nativeTypeCache.find(sig); it != nativeTypeCache.end())
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
			return Throw(err, _T("DllImport: creating prototype for native data view object"));

		// create the cache entry
		entry = &nativeTypeCache.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(sig),
			std::forward_as_tuple(proto)).first->second;

		// add getters and setters
		InitNativeObjectProto(entry, sig);
	}

	// Create a NativeTypeWrapper for the object
	return NativeTypeWrapper::Create(pCreatedObj, entry->proto, p, sigEnd, sizer.size, data);
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
		Throw(err, _T("DllImport: creating getter/setter for native object"));
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
		Throw(err, _T("DllImport: creating getter/setter for native object"));
}


void JavascriptEngine::InitNativeObjectProto(NativeTypeCacheEntry *entry, const WSTRING &sig)
{
	// get the native signature
	const WCHAR *p = sig.c_str();
	const WCHAR *endp = p + sig.length();

	// check for const qualification
	bool isConst = false;
	if (*p == '%')
	{
		isConst = true;
		++p;
	}

	// service routine to add the getter/setter property
	auto AddGetterSetter = [this, entry, isConst](size_t offset, const WCHAR *name, const WCHAR *sig, size_t siglen, bool hasValueOf)
	{
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
			AddToNativeTypeView(entry, name, new Int64NativeTypeView<INT64>(offset), hasValueOf, !isConst);
			break;

		case 'L':
			AddToNativeTypeView(entry, name, new Int64NativeTypeView<UINT64>(offset), hasValueOf, !isConst);
			break;


			// For INT_PTR and SIZE_T types, use the Int64 viewer, but
			// adapted to the corresponding native type.  That will provide
			// uniform Javascript semantics across platforms.  Note that
			// even though these are nominally "int64" viewers, they'll
			// follow the templates and actually use int32 types on x86.
		case 'z':
			AddToNativeTypeView(entry, name, new Int64NativeTypeView<SSIZE_T>(offset), hasValueOf, !isConst);
			break;

		case 'Z':
			AddToNativeTypeView(entry, name, new Int64NativeTypeView<SIZE_T>(offset), hasValueOf, !isConst);
			break;

		case 'p':
			AddToNativeTypeView(entry, name, new Int64NativeTypeView<INT_PTR>(offset), hasValueOf, !isConst);
			break;

		case 'P':
			AddToNativeTypeView(entry, name, new Int64NativeTypeView<UINT_PTR>(offset), hasValueOf, !isConst);
			break;

		case 'H':
			AddToNativeTypeView(entry, name, new HandleNativeTypeView(offset), hasValueOf, !isConst);
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
				AddToNativeTypeView(entry, name, new PointerNativeTypeView(offset, ptrsig, wcslen(ptrsig), *sig), hasValueOf, !isConst);
			}
			break;

		case '*':
		case '&':
			// generic pointer type
			AddToNativeTypeView(entry, name, new PointerNativeTypeView(offset, sig + 1, siglen - 1, 0), hasValueOf, !isConst);
			break;

		case '{':
		case '[':
			// Add a nested type viewer.  These don't have setters, as they're
			// structural elements of the enclosing type.
			AddToNativeTypeView(entry, name, new NestedNativeTypeView(offset, sig, siglen), false, false);
			break;
		}
	};

	// check the native value type
	switch (*p)
	{
	case '[':
		// Array type.  An array doesn't have any getter/setters; instead, it has
		// an .at(index) method that returns a view of the element at the index.
		{
			// get the first index value
			size_t dim;
			bool empty;
			if (!Marshaller::ParseArrayDim(p, endp, dim, empty))
			{
				Throw(_T("DllImport: invalid array dimension in native type view"));
				break;
			}
			if (empty)
			{
				Throw(_T("DllImport: unspecified array dimension not allowed in native type view"));
				break;
			}

			// figure the size of the underlying type
			MarshallBasicSizer sizer(p, endp, JS_INVALID_REFERENCE);
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
			for (size_t i = 0, eleOffset = 0; i < dim; ++i, eleOffset += sizer.size)
			{
				TCHAR iAsStr[32];
				IF_32_64(_itow_s(i, iAsStr, 10), _ui64tow_s(i, iAsStr, countof(iAsStr), 10));
				AddGetterSetter(eleOffset, iAsStr, p, endp - p, false);
			}
		}
		break;

	case '{':
		// Struct/union type.  The prototype has a getter and setter for 
		// each element, with the same name as the struct element.
		{
			auto Parse = [this, p, endp, &AddGetterSetter](MarshallStructOrUnionSizer &sizer)
			{
				for ( ; sizer.p < sizer.sigEnd; sizer.NextArg())
				{
					// Marshall one value through the sizer.  This will read the
					// next tag name and figure the size and offset of the item.
					sizer.MarshallValue();

					// add a getter and setter for this type
					AddGetterSetter(sizer.lastItemOfs, sizer.curProp.c_str(), sizer.curPropType.c_str(), sizer.curPropType.length(), false);
				}
			};
			if (p + 1 < endp && p[1] == 'U')
				Parse(MarshallUnionSizer(p + 3, endp - 1, JS_INVALID_REFERENCE));
			else
				Parse(MarshallStructSizer(p + 3, endp - 1, JS_INVALID_REFERENCE));
		}
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
	case 'D':
	case 'l':
	case 'L':
	case 'z':
	case 'Z':
	case 'p':
	case 'P':
	case 'H':
		// The whole native object is a primitive scalar type.  Add a getter/setter
		// for "value".  Also add the same getter as the valueOf() method.  Since
		// this is the only value in the object, it's at offset zero.
		AddGetterSetter(0, L"value", p, Marshaller::EndOfArg(p, endp) - p, true);
		break;

	case '*':
	case '&':
		// Pointer types are scalars with .value getter/setters, plus an .at
		// property that dereferences the pointer.
		AddGetterSetter(0, L"value", p, Marshaller::EndOfArg(p, endp) - p, true);
		break;

	case 't':
	case 'T':
		// String pointers are scalars with .value getter/setters
		AddGetterSetter(0, L"value", p, Marshaller::EndOfArg(p, endp) - p, true);
		break;

	case 'v':
		// Void can't be used for in a data view
		Throw(_T("DllImport: a native type view can't be created for VOID data"));
		break;

	default:
		// other types are invalid
		Throw(MsgFmt(_T("DllImport: native object prototype setup: invalid native type code '%c'"), *p));
		break;
	}
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

bool JavascriptEngine::IsPointerConversionValid(const WCHAR *from, const WCHAR *to)
{
	// we can cast anything to void* or const void*
	if (to[0] == 'v' || (to[0] == '%' && to[1] == 'v'))
		return true;

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
	if (*to == '%')
		++to;

	// cast from anything to or from void* is legal
	if (*to == 'v' || *from == 'v')
		return true;

	// casts between identical pointer types are legal
	if (wcscmp(from, to) == 0)
		return true;

	// If the 'from' type is a struct, and the first struct element
	// is the 'to' type, it's legal.  This corresponds to a C++ cast
	// from derived class to base class.
	if (from[0] == '{' && from[1] == 'S')
	{
		// find the first element type, and test for compatibility
		// from that element type to the 'to' type
		const WCHAR *p = from + 3;
		for (; *p != 0 && *p != ':'; ++p);
		if (*p == ':')
		{
			++p;
			WSTRING eletype(p, Marshaller::EndOfArg(p, p + wcslen(p)) - p);
			if (IsPointerConversionValid(eletype.c_str(), to))
				return true;
		}
	}

	// A cast to a smaller array of the same type is legal
	if (from[0] == '[' && to[0] == '[')
	{
		// read the two array dimensions
		size_t fromDim, toDim;
		bool fromEmpty, toEmpty;
		const WCHAR *pfrom = from, *pto = to;
		if (Marshaller::ParseArrayDim(pfrom, pfrom + wcslen(pfrom), fromDim, fromEmpty)
			&& Marshaller::ParseArrayDim(pto, pto + wcslen(pto), toDim, toEmpty)
			&& toDim <= fromDim)
		{
			// The dimensions are compatible; allow it if the types are identical.
			// Note that the types have to be identical, not just compatible, since
			// the element types have to be the same for the pointer arithmetic
			// within the new array to work properly.
			if (wcscmp(pfrom, pto) == 0)
				return true;
		}
	}

	// A cast from an array to a single element of the same type is legal
	if (from[0] == '[')
	{
		// skip the array specifier in the 'from'
		const WCHAR *pfrom = from;
		for (; *pfrom != 0 && *pfrom != ']'; ++pfrom);
		if (*pfrom == ']' && wcscmp(pfrom + 1, to) == 0)
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
	if (auto obj = NativeTypeWrapper::Recover<NativeTypeWrapper>(argv[0], _T("DllImport: data object view: primitive data getter")); obj != nullptr)
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
	if (auto obj = NativeTypeWrapper::Recover<NativeTypeWrapper>(argv[0], _T("DllImport: data object view: primitive data getter")); obj != nullptr)
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
	if (auto obj = NativeTypeWrapper::Recover<NativeTypeWrapper>(argv[0], _T("DllImport: native object view: primitive data setter")); obj != nullptr)
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

JavascriptEngine::PointerNativeTypeView::PointerNativeTypeView(size_t offset, const WCHAR *sig, size_t sigLen, WCHAR stringType) :
	ScalarNativeTypeView(offset), sig(sig, sigLen), stringType(stringType)
{
	// figure the size of the underlying type
	MarshallBasicSizer sizer(sig, sig + sigLen, JS_INVALID_REFERENCE);
	sizer.MarshallValue();

	this->size = sizer.size;
}

JsErrorCode JavascriptEngine::PointerNativeTypeView::Get(JsValueRef self, void *nativep, JsValueRef *jsval) const
{
	__try
	{
		// The native field is a pointer type - retrieve it as a void*
		void *ptr = *reinterpret_cast<void* const*>(nativep);

		// create the NativePointer javascript object to represent the pointer
		return NativePointerData::Create(ptr, size, sig.c_str(), sig.length(), stringType, jsval);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		ThrowSimple("Bad native pointer dereference: memory location is invalid or inaccessible");
		return JsNoError;
	}
}

JsErrorCode JavascriptEngine::PointerNativeTypeView::Set(JsValueRef self, void *nativep, JsValueRef jsval) const
{
	// Javascript null or undefined counts as a null pointer
	if (jsval == inst->nullVal || jsval == inst->undefVal)
	{
		*reinterpret_cast<void**>(nativep) = nullptr;
		return JsNoError;
	}

	// if the value is another native pointer object, use its pointer value
	if (auto ptr = NativePointerData::Recover<NativePointerData>(jsval, nullptr); ptr != nullptr)
	{
		// Make sure the other pointer conversion is legal
		if (!IsPointerConversionValid(ptr->sig.c_str(), sig.c_str()))
		{
			inst->Throw(_T("Incompatible pointer type conversion; assign through a void* to override type checking"));
			return JsErrorInvalidArgument;
		}

		// looks good - set the pointer
		*reinterpret_cast<void**>(nativep) = ptr->ptr;
		return JsNoError;
	}

	// if the value is a native object of the same type as the pointer referent,
	// store the address of the native object in the pointer
	if (auto obj = NativeTypeWrapper::Recover<NativeTypeWrapper>(jsval, nullptr); obj != nullptr)
	{
		// if it's a native object of the pointer referent type, use its address
		if (!IsPointerConversionValid(obj->sig.c_str(), sig.c_str()))
		{
			inst->Throw(_T("Incompatible pointer type conversion; assign through a void* to override type checking"));
			return JsErrorInvalidArgument;
		}

		// set the pointer
		*reinterpret_cast<void**>(nativep) = obj->data;
		return JsNoError;
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

JavascriptEngine::NestedNativeTypeView::NestedNativeTypeView(size_t offset, const WCHAR *sig, size_t siglen) :
	NativeTypeView(offset), sig(sig, siglen)
{
}

JsValueRef CALLBACK JavascriptEngine::NestedNativeTypeView::Getter(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
{
	// recover the native object from 'this'
	JsValueRef jsval;
	if (auto obj = NativeTypeWrapper::Recover<NativeTypeWrapper>(argv[0], _T("DllImport: data object view: nested type getter")); obj != nullptr)
	{
		// get the data view definition for this getter from the context
		auto view = static_cast<const NestedNativeTypeView*>(ctx);

		// create a native data viewer for the element data
		return inst->CreateNativeObject(view->sig, obj->data + view->offset);
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
	NativeTypeWrapper **pCreatedObject, JsValueRef proto, const WCHAR *sig, const WCHAR *sigEnd, size_t size, void *extData)
{
	// create the wrapper object
	auto *wrapper = new NativeTypeWrapper(sig, sigEnd, size, extData);

	// pass it back to the caller if desired
	if (pCreatedObject != nullptr)
		*pCreatedObject = wrapper;

	// create a Javascript external object for our wrapper
	JsValueRef jsobj;
	JsErrorCode err;
	if ((err = JsCreateExternalObjectWithPrototype(wrapper, &NativeTypeWrapper::Finalize, proto, &jsobj)) != JsNoError)
		return inst->Throw(err, _T("DllImport: creating external object for native data"));

	// return the object
	return jsobj;
}

void JavascriptEngine::NativeTypeWrapper::InitCbSize(const WCHAR *sig, const WCHAR *sigEnd, BYTE *data, size_t mainStructSize)
{
	if (sig + 2 < sigEnd && sig[0] == '{' && sig[1] == 'S')
	{
		// set up a sizer
		JavascriptEngine::MarshallStructSizer sizer(sig + 3, sigEnd - 1, JS_INVALID_REFERENCE);

		// if we don't know the overall size yet, figure it
		if (mainStructSize == 0)
			mainStructSize = sizer.SizeofStruct(JS_INVALID_REFERENCE, nullptr);

		// search for a cbSize field
		while (sizer.p < sizer.sigEnd)
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
				InitCbSize(t.c_str(), t.c_str() + t.length(), data + sizer.lastItemOfs, mainStructSize);
			}
		}
	}
}

JavascriptEngine::NativeTypeWrapper::NativeTypeWrapper(const WCHAR *sig, const WCHAR *sigEnd, size_t size, void *extData) :
	sig(sig, sigEnd - sig), size(size)
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
			std::forward_as_tuple(size));
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
		if (auto obj = Recover<NativeTypeWrapper>(argv[1], _T("NativeObject.addressOf() argument is not a NativeObject")); obj != nullptr)
		{
			// get our type signature
			const WCHAR *sig = obj->sig.c_str();
			const WCHAR *sigEnd = sig + obj->sig.length();
			WSTRING eleSig = obj->sig;

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
						// search the for field
						for (; sizer.p < sizer.sigEnd; sizer.NextArg())
						{
							sizer.MarshallValue();
							if (sizer.curProp.length() == len && sizer.curProp.compare(0, std::string::npos, p, len) == 0)
							{
								offset = sizer.lastItemOfs;
								size = sizer.lastItemSize;
								eleSig = sizer.curPropType;
								return true;
							}
						}

						// not found
						inst->Throw(MsgFmt(_T("NativeObject.addressOf(): field \"%.*s\" not found in struct/union"), (int)len, p));
						return false;
					};

					// find the offset of the field
					bool ok = (sig[1] == 'S' ?
						Search(MarshallStructSizer(sig + 3, sigEnd - 1, JS_INVALID_REFERENCE)) :
						Search(MarshallUnionSizer(sig + 3, sigEnd - 1, JS_INVALID_REFERENCE)));

					if (!ok)
						return inst->undefVal;
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
					size_t dim;
					bool isEmpty;
					if (!Marshaller::ParseArrayDim(sig, sigEnd, dim, isEmpty))
						return inst->undefVal;

					// make sure it's in range
					if (d < 0.0 || d >= static_cast<double>(dim))
						return inst->Throw(err, _T("NativeObject.addressOf(): array index out of bounds"));

					// figure the element size
					eleSig = SkipPointerOrArrayQual(sig);
					MarshallBasicSizer sizer(eleSig);
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
			if (sig[0] == '[')
				sig = SkipPointerOrArrayQual(sig);

			// create a native pointer object pointing to the object's data
			if ((err = NativePointerData::Create(obj->data + offset, size, eleSig.c_str(), eleSig.length(), 0, &jsval)) != JsNoError)
				return inst->Throw(err, _T("NativeObject.addressOf()"));
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

	// Trace references from NativePointer objects
	for (auto &it : nativePointerMap)
		Trace(it.second);

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
	{
		// delete this item from the map
		nativeDataMap.erase(it);

		// delete the item's memory
		delete[] it;
	}
}
