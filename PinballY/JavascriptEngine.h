// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Javascript interface.  This encapsulates the JSRT API exposed by
// ChakraCore to provide script execution services.
//

#pragma once
#include "../ChakraCore/include/ChakraCore.h"
#include "../ChakraCore/include/ChakraDebug.h"
#include "../ChakraCore/include/ChakraDebugService.h"
#include "../ChakraCore/include/ChakraDebugProtocolHandler.h"

#include <map>

extern "C" UINT64 JavascriptEngine_CallCallback(void *wrapper, void *argv);

// Javascript engine interface
class JavascriptEngine : public RefCounted
{
public:
	// stack argument slot type
	typedef IF_32_64(UINT32, UINT64) arg_t;

	// get the global singleton
	static JavascriptEngine *Get() { return inst; }

	// debugger options
	struct DebugOptions
	{
		// enable debugging
		bool enable = false;

		// Should we wait for the debugger UI to connect at startup?
		// This should generally be set to true when the program is
		// launched as a child process of the debugger UI, to ensure
		// that the debugger can take control and set up breakpoints
		// and such before we start executing any Javascript code.
		bool waitForDebugger = true;

		// where should we break at startup?
		enum
		{
			// don't break at all at startup
			None,

			// break at first system initialization code
			SystemCode,

			// break at first user code
			UserCode
		}
		initBreak = UserCode;

		// localhost port number for the debug service connection
		uint16_t port = 9228;

		// name/description of the debug service
		CSTRING serviceName = "ChakraCore Instance";
		CSTRING serviceDesc = "ChakraCore Instance";

		// favorite icon data
		const BYTE* favIcon = nullptr;
		size_t favIconSize = 0;

		// Message window and callback.  When a message is received on
		// the debug socket, we'll fire the given message to the given
		// window.  The window should simply call OnDebugMessageQueued()
		// when receiving this message.  Socket messages are received
		// asynchronously in background threads, where they're queued
		// for later processing on the main thread.  The point of this
		// window message is to make the main thread respond quickly
		// when an incoming message is queued.
		HWND messageHwnd = NULL;
		UINT messageId = 0;
	};

	// callback when a debug message is queued
	void OnDebugMessageQueued();

	// initialize/terminate
	static bool Init(ErrorHandler &eh, DebugOptions *debug);
	static void Terminate();

	// Bind the dllImport callbacks
	bool BindDllImportCallbacks(ErrorHandler &eh);

	// Get the canonical file:/// URL for a local file path
	static WSTRING GetFileUrl(const WCHAR *path);

	// Evaluate a script
	bool EvalScript(const WCHAR *scriptText, const WCHAR *url, JsValueRef *returnVal, ErrorHandler &eh);

	// Load a module
	bool LoadModule(const TCHAR *url, ErrorHandler &eh);

	// Debugger console logging
	void DebugConsoleLog(const TCHAR *type, const TCHAR *msg);

	// special values
	JsValueRef GetNullVal() const { return nullVal; }
	JsValueRef GetUndefVal() const { return undefVal; }
	JsValueRef GetZeroVal() const { return zeroVal; }
	JsValueRef GetFalseVal() const { return falseVal; }
	JsValueRef GetTrueVal() const { return trueVal; }

	// simple value conversions
	static JsErrorCode ToString(TSTRING &s, const JsValueRef &val);
	static JsErrorCode ToInt(int &i, const JsValueRef &val);

	// get a property value
	JsErrorCode GetProp(int &intval, JsValueRef obj, const CHAR *prop, const TCHAR* &errWhere);
	JsErrorCode GetProp(TSTRING &strval, JsValueRef obj, const CHAR *prop, const TCHAR* &errWhere);
	JsErrorCode GetProp(JsValueRef &val, JsValueRef obj, const CHAR *prop, const TCHAR* &errWhere);

	// set a read-only property
	JsErrorCode SetReadonlyProp(JsValueRef object, const CHAR *propName, JsValueRef propVal,
		const TCHAR* &errWhere);

	// Add a getter and/or setter property.  Pass JS_INVALID_REFERENCE
	// for getter or setter to omit it.
	JsErrorCode AddGetterSetter(JsValueRef object, const CHAR *propName, 
		JsValueRef getter, JsValueRef setter, const TCHAR* &where);

	JsErrorCode AddGetterSetter(JsValueRef object, const CHAR *propName, 
		JsNativeFunction getter, void *getterCtx,
		JsNativeFunction setter, void *setterCtx,
		const TCHAR* &where);

	// get a property from the global object
	template<typename T>
	JsErrorCode GetGlobProp(T &val, const CHAR *prop, const TCHAR* &errWhere)
	{
		JsValueRef g;
		if (JsErrorCode err = JsGetGlobalObject(&g); err != JsNoError)
		{
			errWhere = _T("JsGetGlobalObject");
			return err;
		}

		return this->GetProp(val, g, prop, errWhere);
	}

	// "Throw" an error.  This doesn't actually throw in the sense of
	// interrupting the C++ execution flow, but it does interrupt the
	// javascript execution flow when control returns to the interpreter.
	// This sets an exception state in the engine for the given JS error
	// code.  For the caller's convenience, this returns 'undefined', so
	// that this can be easily returned as the (unused) return value of
	// a native code callback.
	JsValueRef Throw(JsErrorCode err);

	// Throw an error from a Javascript callback.  This is the same as
	// the regular Throw(JsErrorCode), but adds the name of the callback
	// to the diagnostics.
	JsValueRef Throw(JsErrorCode err, const TCHAR *cbname);

	// Throw an error using a string exception
	JsValueRef Throw(const TCHAR *errorMessage);

	// throw a string exception with no context
	static JsValueRef ThrowSimple(const char *msg);

	// Check if the Javascript context is in an exception state
	bool HasException();

	// Queued task - timeout, interval, promise completion, module ready, etc
	struct Task;

	// Add a task to the queue
	void AddTask(Task *task);

	// Enumerate tasks.  The predicate returns true to continue the enumeration.
	void EnumTasks(std::function<bool(Task *)>);

	// Are any tasks pending?
	bool IsTaskPending() const { return taskQueue.size() != 0; }

	// Get the scheduled time of the next task.  This is the time in terms
	// of GetTickCount64() for the next task ready to execute.  This can be
	// earlier than the current time. 
	ULONGLONG GetNextTaskTime();

	// Run ready scheduled tasks 
	void RunTasks();

	// Default return values for type conversions from Javascript to
	// native.  These are used in case of errors in function calls,
	// conversions, etc.
	template<typename T> static T DefaultReturnValue();
	template<> static JsValueRef DefaultReturnValue<JsValueRef>()
	{
		JsValueRef val;
		JsGetUndefinedValue(&val);
		return val;
	}
	template<> static bool DefaultReturnValue<bool>() { return false; }
	template<> static int DefaultReturnValue<int>() { return 0; }
	template<> static float DefaultReturnValue<float>() { return 0.0f; }
	template<> static double DefaultReturnValue<double>() { return 0.0; }
	template<> static CSTRING DefaultReturnValue<CSTRING>() { return ""; }
	template<> static WSTRING DefaultReturnValue<WSTRING>() { return L""; }

	// Type converters from Javascript to native
	template<typename T> static T JsToNative(JsValueRef jsval);
	template<> static JsValueRef JsToNative<JsValueRef>(JsValueRef jsval) { return jsval; }
	template<typename NumType> static NumType JsToNativeNumType(JsValueRef jsval)
	{
		JsValueRef num;
		double d;
		if (JsConvertValueToNumber(jsval, &num) == JsNoError
			&& JsNumberToDouble(num, &d) == JsNoError)
			return static_cast<NumType>(d);
		throw "JsToNativeNumType error";
	}
	template<> static int JsToNative<int>(JsValueRef jsval) { return JsToNativeNumType<int>(jsval); }
	template<> static float JsToNative<float>(JsValueRef jsval) { return JsToNativeNumType<float>(jsval); }
	template<> static double JsToNative<double>(JsValueRef jsval) { return JsToNativeNumType<double>(jsval); }
	template<> static bool JsToNative<bool>(JsValueRef jsval)
	{
		JsValueRef boolval;
		bool b;
		if (JsConvertValueToBoolean(jsval, &boolval) == JsNoError
			&& JsBooleanToBool(boolval, &b) == JsNoError)
			return b;
		throw "JsToNative<bool> error";
	}
	template<> static WSTRING JsToNative<WSTRING>(JsValueRef jsval)
	{
		JsValueRef strval;
		const wchar_t *p;
		size_t len;
		if (JsConvertValueToString(jsval, &strval) == JsNoError
			&& JsStringToPointer(strval, &p, &len) == JsNoError)
			return WSTRING(p, len);
		throw "JsToNative<WSTRING> error";
	}
	template<> static CSTRING JsToNative<CSTRING>(JsValueRef jsval)
	{
		JsValueRef strval;
		const wchar_t *p;
		size_t len;
		if (JsConvertValueToString(jsval, &strval) == JsNoError
			&& JsStringToPointer(strval, &p, &len) == JsNoError)
			return WideToAnsiCnt(p, len);
		throw "JsToNative<CSTRING> error";
	}

	// Native-to-javascript type converters
	template<typename T> static JsValueRef NativeToJs(T t);
	template<> static JsValueRef NativeToJs(JsValueRef jsval) { return jsval; }
	template<> static JsValueRef NativeToJs(bool b)
	{
		JsValueRef jsval;
		if (JsBoolToBoolean(b, &jsval) == JsNoError)
			return jsval;
		throw "NativeToJs<bool> Error";
	}
	template<> static JsValueRef NativeToJs(int i)
	{
		JsValueRef jsval;
		if (JsIntToNumber(i, &jsval) == JsNoError)
			return jsval;
		throw "NativeToJs<int> Error";
	}
	template<> static JsValueRef NativeToJs(double d)
	{
		JsValueRef jsval;
		if (JsDoubleToNumber(d, &jsval) == JsNoError)
			return jsval;
		throw "NativeToJs<double> Error";
	}
	template<> static JsValueRef NativeToJs(const CHAR *p)
	{
		JsValueRef jsval;
		WSTRING s = AnsiToWide(p);
		if (JsPointerToString(s.c_str(), s.length(), &jsval) == JsNoError)
			return jsval;
		throw "NativeToJs<CHAR*> Error";
	}
	template<> static JsValueRef NativeToJs(const CSTRING &c)
	{
		JsValueRef jsval;
		WSTRING s = AnsiToWide(c.c_str());
		if (JsPointerToString(s.c_str(), s.length(), &jsval) == JsNoError)
			return jsval;
		throw "NativeToJs<CSTRING&> Error";
	}
	template<> static JsValueRef NativeToJs(CSTRING c) { return NativeToJs<const CSTRING&>(c); }
	template<> static JsValueRef NativeToJs(const WCHAR *p)
	{
		JsValueRef jsval;
		if (JsPointerToString(p, wcslen(p), &jsval) == JsNoError)
			return jsval;
		throw "NativeToJs<WCHAR*> error";
	}
	template<> static JsValueRef NativeToJs(const WSTRING &s)
	{
		JsValueRef jsval;
		if (JsPointerToString(s.c_str(), s.length(), &jsval) == JsNoError)
			return jsval;
		throw "NativeToJs<WSTRING&> error";
	}
	template<> static JsValueRef NativeToJs(WSTRING s) { return NativeToJs<const WSTRING&>(s); }

	// Call a Javascript function.  Converts arguments from native types
	// to Javascript, and converts results back to the given native type.
	// This is for a plain function call, with the 'this' argument set to
	// 'undefined'; only pass the explicit arguments.
	template<typename ReturnType, typename... ArgTypes>
	static ReturnType CallFunc(JsValueRef func, ArgTypes... args)
	{
		const size_t argc = sizeof...(ArgTypes) + 1;
		JsValueRef args[argc] = { JS_INVALID_REFERENCE, NativeToJs(args)... };
		JsGetUndefinedValue(&args[0]);
		JsValueRef result;
		if (JsCallFunction(func, args, static_cast<unsigned short>(argc), &result) != JsNoError)
			throw "CallFunc Error";
		return JsToNative<ReturnType>(result);
	}

	// Call a Javascript method of an object.
	template<typename ReturnType, typename... ArgTypes>
	static ReturnType CallMethod(JsValueRef thisval, JsPropertyIdRef prop, ArgTypes... args)
	{
		JsValueRef func;
		if (JsGetProperty(thisval, prop, &func) == JsNoError)
		{
			const size_t argc = sizeof...(ArgTypes) + 1;
			JsValueRef argv[argc] = { thisval, NativeToJs(args)... };
			JsValueRef result;
			if (JsCallFunction(func, argv, static_cast<unsigned short>(argc), &result) == JsNoError)
				return JsToNative<ReturnType>(result);
		}
		throw "CallMethod Error";
	}

	// Call a Javascript constructor.  The 'this' argument is automatically
	// supplied as 'undefined', so only pass the explicit arguments.
	template<typename... ArgTypes>
	static JsValueRef CallNew(JsValueRef ctor, ArgTypes... args)
	{
		const size_t argc = sizeof...(ArgTypes) + 1;
		JsValueRef argv[argc] = { JS_INVALID_REFERENCE, NativeToJs(args)... };
		JsGetUndefinedValue(&argv[0]);
		JsValueRef result;
		if (JsConstructObject(ctor, argv, static_cast<unsigned short>(argc), &result) == JsNoError)
			return result;
		throw "CallNew Error";
	}

	template<typename... ArgTypes> 
	static int CallJsInt(JsValueRef func, ArgTypes... args) 
		{ return CallJs<int, ArgTypes...>(func, args...); }

	template<typename... ArgTypes>
	static int CallJsFloat(JsValueRef func, ArgTypes... args) 
		{ return CallJs<float, ArgTypes...>(func, args...); }

	template<typename... ArgTypes>
	static int CallJsDouble(JsValueRef func, ArgTypes... args) 
		{ return CallJs<double, ArgTypes...>(func, args...); }

	template<typename... ArgTypes>
	static int CallJsTSTRING(JsValueRef func, ArgTypes... args) 
		{ return CallJs<TSTRING, ArgTypes...>(func, args...); }

	// Fire an event.  This calls <target>.dispatchEvent(new <eventType>(args)).
	template<typename... ArgTypes>
	bool FireEvent(JsValueRef eventTarget, JsValueRef eventType, ArgTypes... args)
	{
		try
		{
			// call the method and return the result
			return CallMethod<bool, JsValueRef>(eventTarget, dispatchEventProp, CallNew(eventType, args...));
		}
		catch (...)
		{
			// on any error, return true to indicate that system default processing
			// for the event should proceed
			return true;
		}
	}

	// Type converters for the native functions
	template<typename T> class ToNativeConverter { };
	class ToNativeConverterBase
	{
	public:
		void Check(JsErrorCode err, bool &ok, const CSTRING &name) const
		{
			if (err != JsNoError)
			{
				// flag the error
				ok = false;

				// create an exception message
				MsgFmt msg(IDS_ERR_JSCB, JsErrorToString(err), name.c_str());
				JsValueRef str;
				JsPointerToString(msg.Get(), _tcslen(msg.Get()), &str);

				// create an exception object for the error
				JsValueRef exc;
				JsCreateError(str, &exc);

				// set the error state
				JsSetException(exc);
			}
		}
	};

	template<> class ToNativeConverter<bool> : public ToNativeConverterBase
	{
		bool Empty() const { return false; }
		bool Conv(JsValueRef val, bool &ok, const CSTRING &name) const
		{
			JsValueRef boolval;
			Check(JsConvertValueToBoolean(val, &boolval), ok, name);

			bool b;
			JsBooleanToBool(val, &b);
			return b;
		}
	};

	template<> class ToNativeConverter<int> : public ToNativeConverterBase
	{
	public:
		int Empty() const { return 0; }
		int Conv(JsValueRef val, bool &ok, const CSTRING &name) const
		{
			JsValueRef num;
			Check(JsConvertValueToNumber(val, &num), ok, name);

			int i = 0;
			if (ok)
				Check(JsNumberToInt(num, &i), ok, name);

			return i;
		}
	};

	template<> class ToNativeConverter<double> : public ToNativeConverterBase
	{
	public:
		double Empty() const { return 0.0; }
		double Conv(JsValueRef val, bool &ok, const CSTRING &name) const
		{
			JsValueRef num;
			Check(JsConvertValueToNumber(val, &num), ok, name);

			double d = 0.0;
			if (ok)
				Check(JsNumberToDouble(num, &d), ok, name);

			return d;
		}
	};

	template<> class ToNativeConverter<float> : public ToNativeConverterBase
	{
	public:
		float Empty() const { return 0.0f; }
		float Conv(JsValueRef val, bool &ok, const CSTRING &name) const
		{
			JsValueRef num;
			Check(JsConvertValueToNumber(val, &num), ok, name);

			double d = 0.0;
			if (ok)
				Check(JsNumberToDouble(num, &d), ok, name);

			if (ok && (d < FLT_MIN || d > FLT_MAX))
			{
				Check(JsErrorInvalidArgument, ok, name);
				return NAN;
			}

			return (float)d;
		}
	};

	template<> class ToNativeConverter<CSTRING> : public ToNativeConverterBase
	{
	public:
		CSTRING Empty() const { return ""; }
		CSTRING Conv(JsValueRef val, bool &ok, const CSTRING &name) const
		{
			JsValueRef str;
			Check(JsConvertValueToString(val, &str), ok, name);

			const wchar_t *pstr = L"";
			size_t len = 0;
			if (ok)
				Check(JsStringToPointer(str, &pstr, &len), ok, name);

			WSTRING w(pstr, len);
			return WSTRINGToCSTRING(w);
		}
	};

	template<> class ToNativeConverter<WSTRING> : public ToNativeConverterBase
	{
	public:
		TSTRING Empty() const { return _T(""); }
		TSTRING Conv(JsValueRef val, bool &ok, const CSTRING &name) const
		{
			JsValueRef str;
			Check(JsConvertValueToString(val, &str), ok, name);

			const wchar_t *pstr = L"";
			size_t len = 0;
			if (ok)
				Check(JsStringToPointer(str, &pstr, &len), ok, name);

			WSTRING w(pstr, len);
			return w;
		}
	};

	template<> class ToNativeConverter<JsValueRef> : public ToNativeConverterBase
	{
	public:
		JsValueRef Empty() const
		{
			JsValueRef val;
			JsGetUndefinedValue(&val);
			return val;
		}

		JsValueRef Conv(JsValueRef val, bool &, const CSTRING&) const { return val; }
	};

	// Javascript-to-native function binder.    This collection of template
	// classes is used to create native callbacks.  The callbacks are
	// implemented with normal native C++ type signatures; the binder does
	// the translation between the Javascript values that the JS engine
	// passes us as argument values and the native types.
	//
	// These are implemented as a collection of variadic templates, to
	// allow for an arbitrary number of arguments to the callback function.
	//
	template<typename T> class NativeFunctionBinder { };

	// Base class for native function binders
	class NativeFunctionBinderBase
	{
	public:
		// static invoker - this is the callback entrypoint passed to the Javascript engine,
		// with 'this' as the state object
		static JsValueRef CALLBACK SInvoke(JsValueRef callee, bool isConstructor, JsValueRef *argv, unsigned short argc, void *cbState)
		{
			return reinterpret_cast<NativeFunctionBinderBase*>(cbState)->Invoke(callee, isConstructor, argv, argc);
		}

		// Virtual invoker.  Each template subclass overrides this to translate the
		// javascript argument array into native C++ values.
		virtual JsValueRef Invoke(JsValueRef callee, bool isConstructor, JsValueRef *argv, unsigned short argc) const = 0;

		// Callback name expoed to Javascript
		CSTRING callbackName;
	};

	// Zero-argument function binder.  R is the return type.
	template <typename R>
	class NativeFunctionBinder<R()> : public NativeFunctionBinderBase
	{
	public:
		std::tuple<> Bind(JsValueRef *argv, unsigned short argc, bool& /*ok*/) const
		{
			return std::make_tuple();
		}

	};

	// One-argument function binder
	template <typename R, typename T>
	class NativeFunctionBinder<R(T)> : public NativeFunctionBinder<R()>
	{
	public:
		std::tuple<T> Bind(JsValueRef *argv, unsigned short argc, bool &ok) const
		{
			// if there's an argument, convert the value
			ToNativeConverter<T> converter;
			if (argc >= 1)
			{
				// do the type conversion
				T nativeVal = converter.Conv(argv[0], ok, callbackName);

				// on success, store the value and return the tuple
				if (ok)
					return std::make_tuple(nativeVal);
			}

			// no argument or conversion failure - return a default value
			return std::make_tuple(converter.Empty());
		}
	};

	// Two-or-more-argument function binder.  This recursively invokes the
	// parent binder, which is the binder with one argument removed.
	template <typename R, typename T, typename... Ts>
	class NativeFunctionBinder<R(T, Ts...)> : public NativeFunctionBinder<R(Ts...)>
	{
	public:
		std::tuple<T, Ts...> Bind(JsValueRef *argv, unsigned short argc, bool &ok) const
		{
			// if there's at least one more argument, convert the value
			ToNativeConverter<T> converter;
			if (argc >= 1)
			{
				// do the type conversion
				T nativeVal = converter.Conv(argv[0], ok, callbackName);

				// on success, store the value and return a tuple consisting of
				// this argument and a tuple of the remaining arguments
				if (ok)
					return std::tuple_cat(std::make_tuple(nativeVal), NativeFunctionBinder<R(Ts...)>::Bind(argv + 1, argc - 1, ok));
			}

			// no argument or conversion failure - return a default value
			return std::tuple_cat(std::make_tuple(converter.Empty()), NativeFunctionBinder<R(Ts...)>::Bind(argv, 0, ok));
		}
	};


	// Type converters from native types back to javascript
	template<typename R, typename... Ts> class FromNativeConverter { };

	template<typename... Ts> class FromNativeConverter<void, Ts...>
	{
	public:
		JsValueRef Apply(std::function<void(Ts...)> func, std::tuple<Ts...> args)
		{
			// call the function (no return value)
			std::apply(func, args);

			// return 'undefined'
			JsValueRef v;
			JsGetUndefinedValue(&v);
			return v;
		}
	};

	template<typename... Ts> class FromNativeConverter<int, Ts...>
	{
	public:
		JsValueRef Apply(std::function<int(Ts...)> func, std::tuple<Ts...> args)
		{
			// call the function 
			int i = std::apply(func, args);

			// return the integer value
			JsValueRef v;
			JsIntToNumber(i, &v);
			return v;
		}
	};

	template<typename... Ts> class FromNativeConverter<unsigned int, Ts...>
	{
	public:
		JsValueRef Apply(std::function<int(Ts...)> func, std::tuple<Ts...> args)
		{
			// call the function 
			unsigned int u = std::apply(func, args);

			// Return the integer value.  Cast through double before
			// converting to Javascript, to retain the unsignedness.
			JsValueRef v;
			JsDoubleToNumber(static_cast<double>(u), &v);
			return v;
		}
	};

	template<typename... Ts> class FromNativeConverter<UINT64, Ts...>
	{
	public:
		JsValueRef Apply(std::function<int(Ts...)> func, std::tuple<Ts...> args)
		{
			// call the function 
			size_t u = std::apply(func, args);

			// return it as a double, even though it might overflow
			JsValueRef v;
			JsDoubleToNumber(static_cast<double>(u), &v);
			return v;
		}
	};

	template<typename... Ts> class FromNativeConverter<double, Ts...>
	{
	public:
		JsValueRef Apply(std::function<double(Ts...)> func, std::tuple<Ts...> args)
		{
			// call the function 
			double d = std::apply(func, args);

			// return the integer value
			JsValueRef v;
			JsDoubleToNumber(d, &v);
			return v;
		}
	};

	template<typename... Ts> class FromNativeConverter<TSTRING, Ts...>
	{
	public:
		JsValueRef Apply(std::function<TSTRING(Ts...)> func, std::tuple<Ts...> args)
		{
			// call the function
			TSTRING s = std::apply(func, args);

			// return the string value
			JsValueRef v;
			JsPointerToString(s.c_str(), s.length(), &v);
			return v;
		}
	};

	template<typename... Ts> class FromNativeConverter<JsValueRef, Ts...>
	{
	public:
		JsValueRef Apply(std::function<JsValueRef(Ts...)> func, std::tuple<Ts...> args)
		{
			// call the function and return the value
			return std::apply(func, args);
		}
	};

	// Native callback wrapper
	template <typename R> class NativeFunction { };
	template <typename R, typename... Ts>
	class NativeFunction<R(Ts...)> : public NativeFunctionBinder<R(Ts...)>
	{
	public:
		// call descriptor
		struct CallDesc
		{
			JsValueRef callee;
			bool isConstructor;
			JsValueRef this_;
			JsValueRef *argv;
			unsigned short argc;
		};

		// virtual member function with call descriptor
		virtual R DImpl(CallDesc &desc, Ts... args) const { return this->Impl(args...); }

		// virtual member function that implements the callback
		virtual R Impl(Ts...) const = 0;

		// static invoker - this is what JS actually calls
		virtual JsValueRef Invoke(JsValueRef callee, bool isConstructor, JsValueRef *argv, unsigned short argc) const override
		{
			// set up the full call descriptor
			CallDesc desc = { callee, isConstructor, argc >= 1 ? argv[0] : JS_INVALID_REFERENCE, argv, argc };

			// set up a lambda to invoke this->Impl with the template arguments
			auto func = [this, &desc](Ts... args) { return this->DImpl(desc, args...); };

			// Convert the javascript values to native values matching the Impl() signature.
			// The first argument is the 'this' pointer, which we don't use.
			bool ok = true;
			auto args = this->Bind(argv + 1, argc - 1, ok);

			// call the function and convert the return value back to js
			FromNativeConverter<R, Ts...> rconv;
			return rconv.Apply(func, args);
		}
	};

	// Create a native function wrapper from a function.  This uses a generic
	// implementation of NativeFunction to wrap any arbitrary static function,
	// with a context object provided as an additional argument.  This is
	// usually syntactically cleaner to set up than the wrapper class itself,
	// since the compiler should be able to deduce the template arguments
	// from the provided function pointer and context object types.
	template <typename ContextType, typename R, typename... Ts>
	static NativeFunction<R(Ts...)>* WrapNativeFunction(R(*func)(ContextType, Ts...), ContextType context)
	{
		class GenericNativeFunction : public NativeFunction<R(Ts...)>
		{
		public:
			using FuncType = R(ContextType, Ts...);

			GenericNativeFunction(FuncType *func, ContextType context) :
				func(func), context(context) { }

			FuncType *func;
			ContextType context;

			virtual R Impl(Ts... args) const override { return func(context, args...); }
		};

		return new GenericNativeFunction(func, context);
	};

	template <typename C, typename R, typename... Ts>
	static NativeFunction<R(Ts...)>* WrapNativeMemberFunction(R(C::*func)(Ts...), C *self)
	{
		class GenericNativeMemberFunction : public NativeFunction<R(Ts...)>
		{
		public:
			GenericNativeMemberFunction(R(C::*func)(Ts...), C *self) : func(func), self(self) { }

			R(C::*func)(Ts...);
			C *self;

			virtual R Impl(Ts... args) const override { return (self->*func)(args...); }
		};

		return new GenericNativeMemberFunction(func, self);
	};

	// Create a global native callback function.  This creates a property
	// of the 'global' object of the given name, and assigns it to a native
	// callback to the given function object.
	bool DefineGlobalFunc(const CHAR *name, NativeFunctionBinderBase *func, ErrorHandler &eh);

	// Install a native callback function for a plain static function
	bool DefineObjPropFunc(JsValueRef obj, const CHAR *objName, const CHAR *propName, JsNativeFunction func, void *ctx, ErrorHandler &eh);

	// Install a native callback function as an object property
	bool DefineObjPropFunc(JsValueRef obj, const CHAR *objName, const CHAR *propName, NativeFunctionBinderBase *func, ErrorHandler &eh);

	// Create a native function wrapper and add it to our internal tracking
	// list for eventual disposal.
	template <typename ContextType, typename R, typename... Ts>
	NativeFunctionBinderBase *CreateAndSaveWrapper(R (*func)(ContextType*, Ts...), ContextType *context)
	{
		// create the wrapper, enlist it, and return it
		return this->nativeWrappers.emplace_back(WrapNativeFunction(func, context)).get();
	}

	template <class C, typename R, typename... Ts>
	NativeFunctionBinderBase *CreateAndSaveWrapper(R (C::*func)(Ts...), C *self)
	{
		// create the wrapper, enlist it, and return it
		return this->nativeWrappers.emplace_back(WrapNativeMemberFunction(func, self)).get();
	}

	// Define a global function, creating a native wrapper for it.  The wrapper
	// is added to an internal list to ensure that it's deleted with the engine.
	template <typename ContextType, typename R, typename... Ts>
	bool DefineGlobalFunc(const CHAR *name, R (*func)(ContextType *, Ts...), ContextType *context, ErrorHandler &eh)
		{ return this->DefineGlobalFunc(name, CreateAndSaveWrapper(func, context), eh); }

	template <class C, typename R, typename... Ts>
	bool DefineGlobalFunc(const CHAR *name, R (C::*func)(Ts...), C *self, ErrorHandler &eh)
		{ return this->DefineGlobalFunc(name, CreateAndSaveWrapper(func, self), eh); }

	// Define an object function, creating a native wrapper for it
	template <typename ContextType, typename R, typename... Ts>
	bool DefineObjPropFunc(JsValueRef obj, const CHAR *objName, const CHAR *propName, 
		R (*func)(ContextType *, Ts...), ContextType *context, ErrorHandler &eh)
		{ return this->DefineObjPropFunc(obj, objName, propName, CreateAndSaveWrapper(func, context), eh); }

	template <class C, typename R, typename... Ts>
	bool DefineObjPropFunc(JsValueRef obj, const CHAR *objName, const CHAR *propName, 
		R (C::*func)(Ts...), C *self, ErrorHandler &eh)
		{ return this->DefineObjPropFunc(obj, objName, propName, CreateAndSaveWrapper(func, self), eh); }

	// Exported value.  This allows the caller to store a javascript value
	// in C++ native code, for later use.  For example, this can be used to
	// store a javascript callback function to invoke on a timeout or event.
	// This adds an explicit external reference to the object as long as the
	// host is holding it.
	class ExportedValue
	{
	public:
		ExportedValue(JsValueRef &val) : val(val)
		{
			// add a reference on the javascript object
			JsAddRef(val, nullptr);

			// add a reference on the javascript engine
			engine = JavascriptEngine::Get();
		}

		~ExportedValue() { JsRelease(val, nullptr); }

		const JsValueRef& Get() const { return val; }

		void Set(JsValueRef &val)
		{
			// Add a reference on the new value.  Note that it's important
			// to do this before releasing the old value's reference, since
			// the two values could be the same; releasing the old value
			// first could cause it to be deleted if we were the last
			// referencer.
			JsAddRef(val, nullptr);

			// remove our reference on any previous value
			JsRelease(val, nullptr);

			// store the new value
			this->val = val;
		}

		void Clear()
		{
			// release any prior value
			JsRelease(val, nullptr);

			// set our value to 'undefined', which is the canonical Javascript type
			// for an empty variable
			JsGetUndefinedValue(&val);
		}

	protected:
		// the value
		JsValueRef val;

		// the engine associated with the value, for reference counting purposes
		RefPtr<JavascriptEngine> engine;
	};

	// Task.  This encapsulates a scheduled task, such as a promise completion
	// function, a timeout, an interval, or a module load handler.
	struct Task
	{
		Task() : id(nextId++), readyTime(0), cancelled(false) { }
		virtual ~Task() { }

		// Execute the task.  Returns true if the task should remain
		// scheduled (e.g., a repeating interval task), false if it should
		// be discarded.
		virtual bool Execute() = 0;

		// Each task is assigned a unique ID (serial number) at creation,
		// to allow for identification in Javascript for purposes like
		// clearTimeout().
		double id;

		// Ready time.  This is the time, in GetTickCount64() time, when
		// the task will be ready to execute.  It can be executed any time
		// after this timestamp.
		ULONGLONG readyTime;

		// Has the task been cancelled?  A task can be cancelled from
		// within script code (e.g., clearTimeout() or clearInterval()).
		// Doing so sets this flag, which tells the queue processor to
		// ignore the task and remove it the next time the queue is
		// processed.
		bool cancelled;

		// next available ID
		static double nextId;
	};

	// Module tasks
	struct ModuleTask : Task
	{
		ModuleTask(JsModuleRecord module, const WSTRING &path) : module(module), path(path) { }

		JsModuleRecord module;
		WSTRING path;
	};

	// Module load/parse task
	struct ModuleParseTask : ModuleTask
	{
		ModuleParseTask(JsModuleRecord module, const WSTRING &path) : ModuleTask(module, path) { }
		virtual bool Execute() override;
	};

	// Module eval task
	struct ModuleEvalTask : ModuleTask
	{
		ModuleEvalTask(JsModuleRecord module, const WSTRING &path) : ModuleTask(module, path) { }
		virtual bool Execute() override;
	};

	// Engine idle task
	struct IdleTask : Task
	{
		virtual bool Execute()
		{
			// perform idle tasks
			unsigned int ticks;
			JsIdle(&ticks);

			// schedule the next idle task
			readyTime = GetTickCount64() + ticks;
			return true;
		}
	};


	// Javascript event task
	struct EventTask : Task
	{
		EventTask(JsValueRef func) : func(func)
		{
			// keep a reference on the callback function
			JsAddRef(func, nullptr);
		}

		virtual ~EventTask()
		{
			// release our reference on the callback function
			JsRelease(func, nullptr);
		}

		// execute the task
		virtual bool Execute() override;

		// the function to call when the event fires
		JsValueRef func;
	};

	// Promise task
	struct PromiseTask : EventTask
	{
		PromiseTask(JsValueRef func) : EventTask(func) { }
	};

	// Timeout task
	struct TimeoutTask : EventTask
	{
		TimeoutTask(JsValueRef func, double dt) : EventTask(func)
		{
			readyTime = GetTickCount64() + (ULONGLONG)dt;
		}
	};

	// Interval task
	struct IntervalTask : EventTask
	{
		IntervalTask(JsValueRef func, double dt) : EventTask(func), dt(dt)
		{
			readyTime = GetTickCount64() + (ULONGLONG)dt;
		}

		virtual bool Execute() override
		{
			// do the basic execution
			__super::Execute();

			// if the task has been cancelled, don't reschedule it
			if (cancelled)
				return false;

			// reschedule it for the next interval
			readyTime = GetTickCount64() + (ULONGLONG)dt;
			return true;
		}

		// repeat interval
		double dt;
	};
	
	// dead native object scan task
	struct DeadObjectScanTask : Task
	{
		DeadObjectScanTask(ULONG dt_ms)
		{
			readyTime = GetTickCount64() + dt_ms;
		}

		virtual bool Execute() override 
		{ 
			inst->DeadObjectScan(); 
			return false; // this is a one-shot task
		}
	};

protected:
	JavascriptEngine();
	~JavascriptEngine();

	// global singleton instance
	static JavascriptEngine *inst;

	// instance initialization
	bool InitInstance(ErrorHandler &eh, DebugOptions *debug);
	bool inited = false;

	// JS runtime handle.  This represents a single-threaded javascript execution
	// environment (heap, compiler, garbage collector).
	JsRuntimeHandle runtime = nullptr;

	// dispatchEvent property
	JsPropertyIdRef dispatchEventProp;

	// JS execution context.  This essentially is the container of the "global"
	// javascript object (that is, the object at the root level of the js namespace
	// that unqualified function and variable names attach to).
	JsContextRef ctx;

	// Debugger service objects
	CSTRING debugServiceName;
	uint16_t debugPort;
	JsDebugService debugService = nullptr;
	JsDebugProtocolHandler debugProtocolHandler = nullptr;

	// debugger options from host
	DebugOptions debugOptions;

	// initial debug break pending
	bool debugInitBreakPending = false;

	// special values
	JsValueRef nullVal;
	JsValueRef undefVal;
	JsValueRef zeroVal;
	JsValueRef falseVal;
	JsValueRef trueVal;

	// Log the current engine exception and clear it.  If an error handler is
	// provided, we'll log the given error message through the handler, in 
	// addition to writing the exception data to the log file; otherwise we'll
	// only write the exception to the log file.
	JsErrorCode LogAndClearException(ErrorHandler *eh = nullptr, int msgid = 0);

	// Module import callbacks
	static JsErrorCode CHAKRA_CALLBACK FetchImportedModule(
		JsModuleRecord referencingModule,
		JsValueRef specifier,
		JsModuleRecord *dependentModuleRecord);

	static JsErrorCode CHAKRA_CALLBACK FetchImportedModuleFromScript(
		JsSourceContext referencingSourceContext,
		JsValueRef specifier,
		JsModuleRecord *dependentModuleRecord);

	static JsErrorCode CHAKRA_CALLBACK NotifyModuleReadyCallback(
		JsModuleRecord referencingModule,
		JsValueRef exceptionVar);

	// common handler for the module import callbacks
	JsErrorCode FetchImportedModuleCommon(
		JsModuleRecord referencingModule,
		const WSTRING &referencingSourcePath,
		JsValueRef specifier,
		JsModuleRecord *dependentModuleRecord);

	JsErrorCode FetchImportedModuleCommon(
		JsModuleRecord refrenceingModule,
		const WSTRING &referencingSourcePath,
		const WSTRING &specifier,
		JsModuleRecord *dependentModuleRecord);

	// host info record, stored in the module table
	struct ModuleHostInfo
	{
		ModuleHostInfo(WSTRING &path, JsModuleRecord module) :
			path(path), module(module) { }

		WSTRING path;                // file path to this module
		JsModuleRecord module;       // engine module record
	};

	// Module table.  The engine requires us to keep track of previously
	// requested module sources.  We keep a table of sources indexed by
	// source file path: absolute path, canonicalized, and converted to 
	// lower case.
	std::unordered_map<WSTRING, ModuleHostInfo> modules;

	// get the normalized filename for a module specifier
	static JsErrorCode GetModuleSource(
		WSTRING &filename, const WSTRING &specifier, const WSTRING &referencingSourceFile);

	// task queue
	std::list<std::unique_ptr<Task>> taskQueue;

	// next available task ID
	double nextTaskID = 1.0;

	// promise continuation callback
	static void CALLBACK PromiseContinuationCallback(JsValueRef task, void *ctx);

	// convert a JsErrorCode value to a string representation, for error logging purposes
	static const TCHAR *JsErrorToString(JsErrorCode err);

	// Script cookie struct.  Each time we parse a script, we pass a cookie 
	// to the JS engine to serve as a host context object that the JS engine
	// can pass back to us in callbacks.  The cookie type is a DWORD_PTR, so
	// we can use it as a pointer to a struct.  We have to manage the memory
	// for these structs, so we maintain them in a list.  They have the same
	// lifetime as the instance, so we never free these separately; we just
	// let them accumulate over the instance lifetime, and let them be freed
	// implicitly when the instance is freed.  The list is only here for the
	// sake of this memory management; we don't actually need to find anything
	// in it since we use a direct pointer to each struct as the cookie value.
	struct SourceCookie
	{
		SourceCookie(const WSTRING &file) : file(file) { }

		WSTRING file;                // script source file
	};
	std::list<SourceCookie> sourceCookies;

	// List of Javascript callback wrappers.  We don't need to use this list
	// while running; it's only needed so that we can delete the wrappers at
	// window destruction time.
	std::list<std::unique_ptr<JavascriptEngine::NativeFunctionBinderBase>> nativeWrappers;

	// DllImport callbacks.  These are used to implement a DllImport object
	// that marshalls calls from Javascript to native code in external DLLs.
	JsValueRef DllImportBind(TSTRING dllName, TSTRING funcName);
	static JsValueRef CALLBACK DllImportCall(JsValueRef callee, bool isConstructCall,
		JsValueRef *argv, unsigned short argc, void *ctx);
	size_t DllImportSizeof(WSTRING typeInfo);
	JsValueRef DllImportCreate(WSTRING typeInfo);
	void DllImportDefineInternalType(WSTRING name, WSTRING typeInfo);

	// Native type map.  This is a map of struct, union, and interface types
	// defined on the Javascript side via dllImport.define(), for expansion
	// to concrete type definitions during marshalling.
	std::map<WSTRING, WSTRING> nativeTypeMap;

	// find a native type
	bool LookUpNativeType(const WCHAR *p, size_t len, std::wstring_view &sig, bool silent = false)
	    { return LookUpNativeType(WSTRING(p, len), sig, silent); }
	bool LookUpNativeType(const WSTRING &s, std::wstring_view &sig, bool silent = false);

	// Base class for our external objects.  All objects we pass to the Javascript
	// engine for use as external object data are of this class.
	class ExternalObject
	{
	public:
		ExternalObject() { memcpy(typeTag, "PBY_EXT", 8); }

		bool Validate() { return this != nullptr && memcmp(this->typeTag, "PBY_EXT", 8) == 0; }

		virtual ~ExternalObject() { }
		static void CALLBACK Finalize(void *data)
		{
			if (auto self = static_cast<ExternalObject*>(data); self->Validate())
				delete self;
		}
		
		template<class Subclass>
		static Subclass *Recover(JsValueRef dataObj, const TCHAR *where)
		{
			auto Error = [where](const TCHAR *fmt, ...)
			{
				// generate a javascript exception if there's an error location, 
				// otherwise fail silently
				if (where != nullptr)
				{
					// format the message
					TSTRINGEx msg;
					va_list ap;
					va_start(ap, fmt);
					msg.FormatV(fmt, ap);
					va_end(ap);

					// convert the message to a javascript string
					JsValueRef str;
					JsPointerToString(msg.c_str(), msg.length(), &str);

					// throw an exception with the error message
					JsValueRef exc;
					JsCreateError(str, &exc);
					JsSetException(exc);
				}

				// return null
				return nullptr;
			};

			// retrieve the external object data from the engine
			void *data;
			if (JsErrorCode err = JsGetExternalData(dataObj, &data); err != JsNoError)
				return Error(_T("%s: error retrieving external object data: %s"), where, JsErrorToString(err));

			// convert it to the base type and validate it
			auto extobj = static_cast<ExternalObject*>(data);
			if (!extobj->Validate())
				return Error(_T("%s: external object data is missing or invalid"), where);

			// downcast to the subclass type - this will use C++ dynamic typing to
			// validate that it's actually the subclass we need
			auto obj = dynamic_cast<Subclass*>(extobj);
			if (obj == nullptr)
				return Error(_T("%s: external object data type mismatch"), where);

			// success
			return obj;
		}

		// Type tag.  This is stored at the start of the object as a
		// crude way to validate that an object that we get from JS is
		// in fact one of our objects.
		CHAR typeTag[8];
	};

	// External object data representing a DLL entrypoint.  We use this
	// because there's no good way to represent a FARPROC in a Javascript
	// native type.  The DLL and entrypoint names are stored purely for
	// debugging purposes; the only thing we really need here is the
	// proc address.
	class DllImportData : public ExternalObject
	{
	public:
		DllImportData(FARPROC procAddr, TSTRING &dllName, TSTRING &funcName) :
			procAddr(procAddr), dllName(dllName), funcName(funcName) { }

		FARPROC procAddr;
		TSTRING dllName;
		TSTRING funcName;
	};

	class SigParser;

	// External object representing a COM object instance. 
	class COMImportData : public ExternalObject
	{
	public:
		static JsValueRef Create(COMImportData **pCreatedObj, JsValueRef proto, IUnknown *pUnknown, SigParser *sig);

		~COMImportData();

		// generate a prototype with the interface bindings for the interface
		// type signature
		static bool CreatePrototype(JsValueRef proto, const WCHAR *sig, const WCHAR *sigEnd);

		// COMPointer::isNull(comPointer)
		static JsValueRef CALLBACK IsNull(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		// COMPointer::clear(comPointer)
		static JsValueRef CALLBACK Clear(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		// Get the vtable from the IUnknown.  A COM interface pointer is
		// simply a pointer to a pointer to an array of FARPROCs, which
		// constitutes the vtable.  Note that this is defined independently
		// of language; we're not talking about a C++ vtable here, rather
		// a *COM* vtable.  It so happens that a COM vtable has exactly
		// the same layout in fact as a C++ vtable in MSVC-compiled code,
		// since Microsoft chose the COM layout to match their compiler's
		// layout, but the COM layout would still be what we're using here
		// even if the MSVC compiler ever changed its layout.  So we're 
		// NOT making any hidden assumptions about compiler implementation 
		// details here: this is a well-defined, permanent COM feature.
		FARPROC *GetVTable() const
		{
			// if we don't have an interface object, there's no vtable
			if (pUnknown == nullptr)
				return nullptr;

			// The COM pointer points to a pointer, which in turns points
			// to an array of FARPROCs.
			return *reinterpret_cast<FARPROC**>(pUnknown);
		}

		// the underlying COM interface object
		IUnknown *pUnknown = nullptr;

		// number of vtable entries
		int vtableCount = 0;

		// interface signature
		WSTRING sig;

		// GUID
		WSTRING guid;

	private:
		COMImportData(IUnknown *pUnknown, SigParser *sig);
	};

	// COMPointer clas and prototype
	JsValueRef COMPointer_class;
	JsValueRef COMPointer_proto;

	// External object data representing a COM VARIANT object
	class VariantData : public ExternalObject
	{
	public:
		VariantData() { VariantInit(&v); }
		~VariantData() { VariantClear(&v); }

		// create a Javascript Variant object from a native Variant value
		static JsErrorCode CreateFromNative(const VARIANT *src, JsValueRef &dest);

		// interpret a Javascript value as a variant and copy it into a native VARIANT
		static void CopyFromJavascript(VARIANT *dest, JsValueRef src);

		// new Variant()
		static JsValueRef CALLBACK Create(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		// Variant.prototype.vt getter/setter
		static JsValueRef CALLBACK GetVt(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);
		static JsValueRef CALLBACK SetVt(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		// Variant.prototype.value getter/setter
		static JsValueRef CALLBACK GetValue(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);
		static JsValueRef CALLBACK SetValue(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		// Variant.prototype.date getter/setter
		static JsValueRef CALLBACK GetDate(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);
		static JsValueRef CALLBACK SetDate(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		// Variant.prototype.boolVal getter/setter
		static JsValueRef CALLBACK GetBool(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);
		static JsValueRef CALLBACK SetBool(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		// Variant.prototype.cyVal getter/setter
		static JsValueRef CALLBACK GetCY(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);
		static JsValueRef CALLBACK SetCY(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		// Variant.prototype.decVal getter/setter
		static JsValueRef CALLBACK GetDecimal(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);
		static JsValueRef CALLBACK SetDecimal(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		// Variant.prototype.bstrVal getter/setter
		static JsValueRef CALLBACK GetBSTR(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);
		static JsValueRef CALLBACK SetBSTR(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		// Variant.prototype.<numeric type getter/setter>
		template<typename T, VARTYPE vt, T VARIANT::*ele>
		static JsValueRef CALLBACK GetNumVal(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);
		template<typename T, VARTYPE vt, T VARIANT::*ele>
		static JsValueRef CALLBACK SetNumVal(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		template<typename T, VARTYPE vt, T VARIANT::*ele>
		static JsErrorCode AddNumGetSet(JavascriptEngine *js, const CHAR *name, const TCHAR* &where);

		// date conversions
		static JsValueRef VariantDateToJsDate(DATE date);
		static DATE JsDateToVariantDate(JsValueRef jsval);

		// get as a Javascript value
		static JsValueRef Get(const VARIANT &v);

		// get as a pointer type
		template<typename T>
		static JsValueRef GetByRef(T *pData, const WCHAR *sig);

		// get as an array type
		static JsValueRef GetByRefArray(const VARIANT &v, const WCHAR *sig);

		// set from a Javascript value, inferring the Variant type
		static void Set(VARIANT &v, JsValueRef val);

		// set a pointer type
		template<typename T>
		static T *SetByRef(VARIANT &v, void *pData, VARTYPE vt);

		// the underlying variant
		VARIANT v;

		// Decimal value
		DECIMAL decimal;
	};

	// variant class and prototype objects
	JsValueRef Variant_class;
	JsValueRef Variant_proto;

	// External object data representing a HANDLE object.  We use this to
	// wrap handles mostly for the sake of 64-bit mode, where a handle value
	// could exceed the precision of a Javascript number.
	class HandleData : public ExternalObject
	{
	public:
		HandleData(HANDLE h) : h(h) { }
		HANDLE h;

		static JsErrorCode CreateFromNative(HANDLE h, JsValueRef &jsval);
		static HANDLE FromJavascript(JsValueRef jsval);

		static JsValueRef CALLBACK CreateWithNew(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		static JsValueRef CALLBACK ToString(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		static JsValueRef CALLBACK ToNumber(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		static JsValueRef CALLBACK ToUInt64(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);
	};

	// HANDLE prototype in the Javascript code
	JsValueRef HANDLE_proto = JS_INVALID_REFERENCE;

	// External data object representing a native pointer.  We use this to
	// wrap pointers returned from native code calls, since Javascript has no
	// way to represent a native pointer.
	class NativePointerData : public ExternalObject
	{
	public:
		static JsErrorCode Create(void *ptr, size_t size, SigParser *sig,
			WCHAR stringType, JsValueRef *jsval);

		~NativePointerData();

		// pointer to native data
		void *ptr;

		// size of the underlying data object
		size_t size;

		// type signature of the referenced type (not of the pointer
		// itself, but of the type we point to)
		WSTRING sig;

		// String type code - 'T' for Unicode string, 't' for ANSI string, '\0' for
		// non-string types.  This is set if the original type declaration used one
		// of the explicit string type signifiers rather than a mere character pointer
		// type.  The string types indicate that the data at the pointer is a null-
		// terminated string.
		WCHAR stringType;

		static JsValueRef CALLBACK FromNumber(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		static JsValueRef CALLBACK ToString(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		static JsValueRef CALLBACK ToStringZ(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		static JsValueRef CALLBACK ToNumber(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		static JsValueRef CALLBACK ToUInt64(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);
		
		static JsValueRef CALLBACK ToArrayBuffer(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		static JsValueRef CALLBACK ToArray(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		static JsValueRef CALLBACK To(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		static JsValueRef CALLBACK At(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		static JsValueRef CALLBACK IsNull(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

	private:
		NativePointerData(void *ptr, size_t size, SigParser *sig, WCHAR stringType);

		bool TestAt(void *ptr, size_t size);
	};

	// NativePointer prototype in the Javascript code
	JsValueRef NativePointer_proto = JS_INVALID_REFERENCE;

	// pointer cross reference symbol property
	JsPropertyIdRef xrefPropertyId;

	// External object data representing a 64-bit int type
	template<typename T> class XInt64Data : public ExternalObject
	{
	public:
		XInt64Data(T i) : i(i) { }
		T i;

		// create
		static JsValueRef CALLBACK Create(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		// convert a Javascript value to an XInt64 type
		static T FromJavascript(JsValueRef jsval);

		// create from our own type
		static JsErrorCode CreateFromInt(T i, JsValueRef &jsval);

		// parse a string into our native type
		static bool ParseString(JsValueRef val, T &i);

		// convert to string
		static JsValueRef CALLBACK ToString(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		// convert to object - { high: <high 32 bits>, low: <low 32 bits> }
		static JsValueRef CALLBACK ToObject(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		// toNumber - convert to a number, if possible
		static JsValueRef CALLBACK ToNumber(JsValueRef callee, bool isConstructCall,
			JsValueRef *argv, unsigned short argc, void *ctx);

		// basic math operations
		static JsValueRef CALLBACK Negate(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
			{ return UnaryOp(argv, argc, ctx, [](T a) { return static_cast<T>(-static_cast<INT64>(a)); }); }

		static JsValueRef CALLBACK Add(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
			{ return BinOp(argv, argc, ctx, [](T a, T b) { return a + b; }); }

		static JsValueRef CALLBACK Subtract(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
			{ return BinOp(argv, argc, ctx, [](T a, T b) { return a - b; }); }

		static JsValueRef CALLBACK Multiply(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
			{ return BinOp(argv, argc, ctx, [](T a, T b) { return a * b; }); }

		static JsValueRef CALLBACK Divide(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
			{ return BinOp(argv, argc, ctx, [](T a, T b) { return a / b; }); }

		static JsValueRef CALLBACK Modulo(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
			{ return BinOp(argv, argc, ctx, [](T a, T b) { return a % b; }); }

		// bitwise operations
		static JsValueRef CALLBACK And(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
			{ return BinOp(argv, argc, ctx, [](T a, T b) { return a & b; }); }

		static JsValueRef CALLBACK Or(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
			{ return BinOp(argv, argc, ctx, [](T a, T b) { return a | b; }); }

		static JsValueRef CALLBACK Not(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
			{ return UnaryOp(argv, argc, ctx, [](T a) { return ~a; }); }

		static JsValueRef CALLBACK Shl(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
			{ return BinOp(argv, argc, ctx, [](T a, T b) { return a << b; }); }

		static JsValueRef CALLBACK Ashr(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
			{ return BinOp(argv, argc, ctx, [](T a, T b) { return static_cast<T>(static_cast<INT64>(a) >> b); }); }

		static JsValueRef CALLBACK Lshr(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
			{ return BinOp(argv, argc, ctx, [](T a, T b) { return static_cast<T>(static_cast<UINT64>(a) >> b); }); }

		static JsValueRef CALLBACK Compare(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx);
		
		// Get a math function argument value.  This converts the argument
		// to our type to carry out the arithmetic.  We can convert from a
		// native Javascript number type or form another XInt64 type.
		static JsValueRef BinOp(JsValueRef *argv, unsigned short argc, void *ctx, std::function<T(T, T)> op);
		static JsValueRef UnaryOp(JsValueRef *argv, unsigned short argc, void *ctx, std::function<T(T)> op);

		static JsValueRef ToJs(T val);
		static JsValueRef ToJsInt(int val);
	};

	// prototypes for native INT64 and UINT64 classes
	JsValueRef Int64_proto = JS_INVALID_REFERENCE;
	JsValueRef UInt64_proto = JS_INVALID_REFERENCE;

	// dllImport object
	JsValueRef dllImportObject = JS_INVALID_REFERENCE;

	// DLL handle table.  This is a map of DLLs imported via DllImportGetProc,
	// so that we can reuse HMODULE handles when importing multiple functions
	// from a single library.
	std::unordered_map<TSTRING, HMODULE> dllHandles;

	// Call context for the marshallers.  This is a sort of local stack state
	// across the marshaller subroutines.  We instantiate one of these in local
	// scope each time we enter DllImportCall().  
	//
	// The call context serves as the container for memory allocated for local
	// (native) copies of marshalled Javascript values, the Javascript 'this'
	// object, and information on which native values were passed by reference 
	// from Javascript via object or array arguments.
	//
	// The context is installed as a member variable of the JavascriptEngine
	// object, and we maintain a stack of contexts corresponding to the stack
	// of nested calls into DllImportCall() by linking each context to the
	// context in effect at entry.  This is safe because a Javascript runtime
	// instance represents a single thread, so we don't have to deal with
	// concurrent entries from different threads.
	class MarshallerContext;
	MarshallerContext *marshallerContext = nullptr;
	class MarshallerContext
	{
	public:
		MarshallerContext()
		{ 
			// set up the link to our enclosing context
			enclosing = inst->marshallerContext;
			inst->marshallerContext = this;
		}

		~MarshallerContext() 
		{
			// restore the enclosing call context
			inst->marshallerContext = enclosing; 
		}

		// Allocate memory local to this call context.  The memory is
		// effectively a stack allocation, like a C local variable, that
		// only lasts until the current DllImportCall() invocation returns.
		void *Alloc(size_t size)
		{
			// allocate the block
			mem.emplace_back(size);

			// return the pointer 
			return mem.back().ptr.get();
		}

		// Determine if a pointer refers to a local allocation unit
		bool IsLocal(void *p) const
		{
			// check our memory blocks
			for (auto const &m : mem)
			{
				if (p >= m.ptr.get() && p < m.ptr.get() + m.size)
					return true;
			}

			// not one of ours - check parent blocks
			if (enclosing != nullptr)
				return enclosing->IsLocal(p);

			// not found
			return false;
		}

		// Temporary allocation block.  This represents a block of memory
		// allocated by a marshaller within the current call context.  These
		// are effectively stack allocations that only last until the current 
		// DllImportCall() invocation returns.
		struct Allocation
		{
			Allocation(size_t size) : ptr(new BYTE[size]), size(size) 
			{
				ZeroMemory(ptr.get(), size);
			}

			std::unique_ptr<BYTE> ptr;
			size_t size;
		};

		// Cleanup item.  This is an abstract item that can be added to
		// our internal cleanup list to perform arbitrary cleanup work
		// upon exiting this marshaller context.
		class CleanupItem
		{
		public:
			virtual ~CleanupItem() { }
		};

		// add a cleanup item
		void AddCleanupItem(CleanupItem *item) { cleanupItems.emplace_back(item); }

		// cleanup list
		std::list<std::unique_ptr<CleanupItem>> cleanupItems;

		// list of allocations in this calling context
		std::list<Allocation> mem;

		// enclosing call context
		MarshallerContext *enclosing;

		// Map of objects passed by reference in this stack context.  When
		// we marshall a referenced object from Javascript to native code,
		// we store the Javascript object reference and its native counterpart
		// here.  This lets us pass the same native object for each JS object
		// referenced.  This is especially important when there are cycles in
		// reference graphs, as we'd otherwise infinitely recurse trying to
		// find the end (that doesn't exist) of a circular pointer chain.
		std::unordered_map<JsValueRef, void*> byRefMarshalledObjects;
	};

	// marshalling classes
	class Marshaller;
	class MarshallSizer;
	class MarshallBasicSizer;
	class MarshallStackArgSizer;
	class MarshallStructOrUnionSizer;
	class MarshallStructSizer;
	class MarshallUnionSizer;
	class MarshallToNative;
	class MarshallToNativeArgv;
	class MarshallToNativeByReference;
	class MarshallToNativeStruct;
	class MarshallToNativeUnion;
	class MarshallToNativeArray;
	class MarshallToNativeReturn;
	class MarshallFromNative;
	class MarshallFromNativeArgv;
	class MarshallFromNativeValue;
	class MarshallFromNativeStructOrUnion;
	class MarshallFromNativeStruct;
	class MarshallFromNativeUnion;

	// Native-to-Javascript callback function wrappers.  When Javascript
	// code passes a function reference to a native DLL as a callback, we
	// wrap the js function reference in an object that provides a native
	// function pointer to pass to the DLL.  The native address in turn
	// marshalls arguments into js format and calls the original js
	// function.  
	//
	// The tricky element here is that a native function pointer is just
	// a machine code address, without any context connected to it.  Blame
	// traditional C conventions for this.  Javascript functions, in 
	// contrast, are lambdas, which combine a code address and a context 
	// object.  
	//
	// Since a native function pointer is nothing but a machine code 
	// address, the only way to bind a context into a native function
	// pointer is to make it implicit in the machine code address.  We
	// can't just "add bits" to an address, though; as far as callers are
	// concerned, the address is a true machine code address that they'll
	// use in a CALL instruction.  So we have to make it point to a normal
	// callable function entrypoint.  That leaves us with only one way to
	// bind in a context: the entrypoint has to contain the context object.
	// How can we do this and also make it a callable entrypoint?  By
	// dynamically generating the entrypoint code.
	//
	// So: the first thing we need is a dynamically allocated memory area
	// for our generated entrypoint code.  We can't use regular C++ new/
	// malloc tor this, because our entrypoint code has to live in memory
	// pages marked with the "Executable Code" bit in the hardware memory
	// manager.  Memory manager attributes are at a machine page level, so
	// we need to allocate this memory through the low-level Windows APIs
	// for managing virtual memory directly (VirtualAlloc, VirtualProtect).
	// It would be terribly inefficient to allocate a whole page to each
	// generated entrypoint, so we'll set up a simple malloc-like memory
	// pool manager specialized for this task.
	class JavascriptCallbackWrapper;
	struct CodeGenManager
	{
		CodeGenManager();
		~CodeGenManager();

		// generate a thunk function for a given context object
		FARPROC Generate(JavascriptCallbackWrapper *contextObj);

		// recycle a thunk
		void Recycle(FARPROC thunk) { recycle.emplace_back(reinterpret_cast<BYTE*>(thunk)); }

		// native system page size
		DWORD memPageSize;

		// Generated "thunk" function size.  Each function we generate is 
		// the same size, because it uses the same machine code sequence 
		// (the only thing that varies is the context object address we 
		// encode into  the generated code).
		DWORD funcSize;

		// Virtual page list.  When we're called upon to allocate a new
		// function, and there's nothing in the allocation list, we take
		// the next available chunk out of the last page in the list.
		struct Page
		{
			Page(BYTE *addr) : addr(addr), used(0) { }

			BYTE *addr;        // address of the start of the page
			DWORD used;        // bytes used so far
		};
		std::list<Page> pages;

		// Function entrypoint allocation unit
		struct Func
		{
			Func(BYTE *addr) : addr(addr) { }
			BYTE *addr;      // native code address
		};

		// Recycling bin.  This is a list of previously allocated
		// entrypoints that have been discarded and are available for
		// reuse.  All allocation units are the same size (funcSize),
		// so it's trivial to find a fit for a new allocation in the
		// recycling list: any of them will fit perfectly since
		// everything's one size.
		std::list<Func> recycle;
	};
	CodeGenManager codeGenManager;

	// Symbol for callback thunks.  We use this to create references
	// between a Javascript function that's being used as a callback
	// and our external wrapper object:
	//
	//   wrapper[CallbackSymbol] = function_object
	//   function_object[CallbackSymbol] = wrapper
	//
	JsPropertyIdRef callbackPropertyId;

	// Javascript callback from native code
	class JavascriptCallbackWrapper : public ExternalObject
	{
	public:
		JavascriptCallbackWrapper(JsValueRef jsFunc, SigParser *sig);
		~JavascriptCallbackWrapper();

		// javascript callback function
		JsValueRef jsFunc;

		// calling convention code
		WCHAR callingConv;

		// is there a hidden first argument with a struct-by-value return area pointer?
		bool hasHiddenStructArg;

		// native function signature, minus parens and calling convention
		WSTRING sig;

		// number of arguments
		int argc;

		// generated native code thunk
		FARPROC thunk;
	};

	friend UINT64 JavascriptEngine_CallCallback(void *wrapper, void *argv);

	// The Native Type Wrapper object has two uses:
	//
	// 1. It represents native types passed by reference from DLL code, such as a
	// struct or struct pointer returned as a function result, an OUT struct, etc.
	//
	// 2. It can also be used for objects allocated on the Javascript side via
	// dllImport.create().  This lets Javascript code create native objects to
	// pass to DLL code, such as for OUT parameters or structs by reference.
	//
	// When we wrap a native object that comes from DLL code, the underlying
	// memory is owned by the external code, so we can't make any attempt to manage
	// the memory.  Javascript code that uses a wrapped external object has to be
	// aware of the external code's conventions regarding when the data is deleted.
	// Our wrapper simply has no way of knowing whether or not the native memory
	// is still valid; the wrapper can easily outlive the referenced native data,
	// and there's nothing we can do about it.
	//
	// When we create a native object on the Javascript side, though, we do have
	// to manage the memory.  The simplest thing to do would be treat the native 
	// data created as part of a wrapper to be owned exclusively by the wrapper,
	// so that the native data is deleted along with the wrapper.  But that
	// ignores pointers within the native data.  Consider:
	//
	//   let foo = dllImport.create("struct { int *a; }");
	//   let i = dllImport.create("int");
	//   foo.a = NativeObject.addressOf(i);
	//
	// 'foo' now references a native wrapper object, which in turn owns a native
	// data object that looks like { int *a }, which in turn contains a native
	// pointer to a second native data object, an int.  That second native data
	// object is owned by the wrapper object that 'i' references.  Now let's do
	// this:
	//
	//   i = undefined;
	//
	// The 'i' wrapper is now unreferenced, so the JS GC will eventually collect
	// it and call our wrapper finalizer.  If we deleted the native 'int' memory
	// in that finalizer, we'd leave foo.a with a dangling pointer.  That's the
	// way C++ works, to be sure, and it's what you'd expect for objects created
	// in external DLL code.  But Javascript users aren't accustomed to the notion
	// of dangling pointers, so it would be nice if we could extend the normal
	// JS GC treatment to native pointer references, at least as far as native
	// objects allocated purely on the Javascript side via dllImport.create().
	//
	// Here's how: We keep a master map of native objects we've allocated via
	// dllImport.create() - nativeDataMap.  This is keyed by address, and uses
	// a std::map implementation so that we can search for a block *containing*
	// a given address (not just the block *at* a given address).  When we use
	// dllImport.create() to create a native object and its wrapper, we add the
	// native object to the nativeDataMap.  When we finalize a wrapper, we don't
	// delete the native memory block; we just mark it as "orphaned" in the map.
	// We also schedule a "dead object scan".  The dead object scan starts with
	// the set of parented (non-orphaned) objects, and traces the pointers they
	// contain, using a conservative GC trace (that is, anything that looks like
	// a pointer is considered a pointer).  It marks each map object found via
	// the pointer trace as referenced.  Any map objects not marked as 
	// referenced by the end of the scan are considered unreachable and are
	// immediately deleted.
	//
	struct NativeDataTracker
	{
		NativeDataTracker(BYTE *data, size_t size, WSTRING &sig) : 
			data(data),
			size(size), 
			sig(sig), 
			isWrapperAlive(true), 
			isReferenced(true) 
		{ }

		~NativeDataTracker();

		// data pointer
		BYTE *data;

		// size of the object in bytes
		size_t size;

		// type signature
		WSTRING sig;

		// is the Javscript Native Type Wrapper object alive?
		bool isWrapperAlive;

		// is the object referenced on the current dead object scan pass?
		bool isReferenced;
	};
	std::map<BYTE*, NativeDataTracker> nativeDataMap;

	// Native pointer map.  Each NativePointer object maintains an entry here
	// while it's alive, so that we can also include native data blocks reachable
	// through NativePointer objects in the live set.
	std::unordered_map<NativePointerData*, BYTE*> nativePointerMap;

	// is a scan scheduled?
	bool deadObjectScanPending = false;

	// schedule a dead object scan
	void ScheduleDeadObjectScan();

	// Do a dead object scan
	void DeadObjectScan();


	// Native type wrapper object.  This corresponds to the NativeObject
	// class in Javsacript.
	class NativeTypeWrapper : public ExternalObject
	{
	public:
		static JsValueRef Create(NativeTypeWrapper **createdObject,
			JsValueRef proto, SigParser *sig, size_t size, void *extData);

		~NativeTypeWrapper();

		// type signature
		WSTRING sig;

		// native data buffer
		BYTE *data;

		// Is the native data buffer internal?  Yes means that the object was
		// created explicitly by a Javascript caller, so we allocated space for
		// the underlying native data as part of the object.  No means that the
		// object was created from marshalled data from a DLL call, so we're
		// using memory allocated and managed by native code.
		bool isInternalData;

		// size of the data
		size_t size;

		static JsValueRef CALLBACK AddressOf(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx);

	private:
		// constructor is private - create via Create()
		NativeTypeWrapper(SigParser *sig, size_t size, void *extData);

		static void InitCbSize(SigParser *sig, BYTE *data, size_t mainStructSize = 0);
	};

	JsValueRef NativeObject_proto = JS_INVALID_REFERENCE;

	// create a native object
	template<typename T = void>
	JsValueRef CreateNativeObject(SigParser *sig, void *data = nullptr, T **pCreatedObj = nullptr);

	// Native type data view context.  This represents one element in a
	// native structure or array.
	struct NativeTypeView
	{
		NativeTypeView(size_t offset) : offset(offset) { }
		virtual ~NativeTypeView() { }

		static JsValueRef CALLBACK Setter(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
		{
			ThrowSimple("Internal error: this type does not have a setter"); 
			return JS_INVALID_REFERENCE;
		}
		static JsValueRef CALLBACK ToString(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx)
		{
			ThrowSimple("Internal error: this type does not implement to String");
			return JS_INVALID_REFERENCE;
		}

		// offset in the native struct of our data
		size_t offset;
	};

	// Nested type view: a struct or array within a struct or array
	struct NestedNativeTypeView : public NativeTypeView
	{
		NestedNativeTypeView(size_t offset, SigParser *sig);

		static JsValueRef CALLBACK Getter(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx);

		// type signature of nested object
		WSTRING sig;
	};

	// Abstract base class for native type viewers for scalar types
	struct ScalarNativeTypeView : public NativeTypeView
	{
		using NativeTypeView::NativeTypeView;

		// getter - retrieves the native value and converts it to a javascript value
		virtual JsErrorCode Get(JsValueRef self, void *nativep, JsValueRef *jsval) const = 0;

		// setter - takes a javascript value and stores it in the native struct
		virtual JsErrorCode Set(JsValueRef self, void *nativep, JsValueRef jsval) const = 0;

		// Generic getter/setter.  These are the native callback entrypoints from
		// the Javascript engine; they get the native pointer to the data element
		// and invoke the virtual Get/Set methods.
		static JsValueRef CALLBACK Getter(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx);
		static JsValueRef CALLBACK Setter(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx);

		// Generic toString for the type
		static JsValueRef CALLBACK ToString(JsValueRef callee, bool isConstructCall, JsValueRef *argv, unsigned short argc, void *ctx);
	};

	// Native type view for primitive scalar types
	template<typename T>
	struct PrimitiveNativeTypeView : public ScalarNativeTypeView
	{
		using ScalarNativeTypeView::ScalarNativeTypeView;

		virtual JsErrorCode Get(JsValueRef self, void *nativep, JsValueRef *jsval) const override
		{
			__try
			{
				return JsDoubleToNumber(static_cast<double>(*reinterpret_cast<const T*>(nativep)), jsval);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				ThrowSimple("Bad native pointer dereference: memory location is invalid or inaccessible");
				return JsNoError;
			}
		}

		virtual JsErrorCode Set(JsValueRef self, void *nativep, JsValueRef jsval) const override
		{
			// convert the value to numeric
			JsErrorCode err;
			JsValueRef jsnum;
			double d;
			if ((err = JsConvertValueToNumber(jsval, &jsnum)) != JsNoError
				|| (err = JsNumberToDouble(jsnum, &d)) != JsNoError)
				return err;

			// Store the value in the native type.  In analogy to the built-in 
			// Typed Array types in Javascript, we don't do any range checking;
			// we simply truncate values that overflow the native type.
			__try
			{
				*reinterpret_cast<T*>(nativep) = static_cast<T>(d);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				ThrowSimple("Bad native pointer write: memory location is invalid, inaccessible, or read-only");
			}
			return JsNoError;
		}
	};

	// Native type view for INT64 types.  Note that these can be instantiated
	// with T = INT64, UINT64, SSIZE_T, SIZE_T, INT_PTR, or UINT_PTR.  The
	// SIZE_T and INT_PTR types are actually 32-bit types on 32-bit platforms,
	// but we use the 64-bit native type view for consistent behavior on the
	// Javascript side across platforms, specifically the use of Int64/Uint64
	// external objects to represent the values in Javascript.
	template<typename TNative, typename TJavascript>
	struct Int64NativeTypeView : public ScalarNativeTypeView
	{
		using ScalarNativeTypeView::ScalarNativeTypeView;

		virtual JsErrorCode Get(JsValueRef self, void *nativep, JsValueRef *jsval) const override
		{
			__try
			{
				TNative val = *reinterpret_cast<const TNative*>(nativep);
				return XInt64Data<TJavascript>::CreateFromInt(val, *jsval);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				ThrowSimple("Bad native pointer dereference: memory location is invalid or inaccessible");
				return JsNoError;
			}
		}


		virtual JsErrorCode Set(JsValueRef self, void *nativep, JsValueRef jsval) const override
		{
			__try
			{
				*reinterpret_cast<TNative*>(nativep) = static_cast<TNative>(XInt64Data<TJavascript>::FromJavascript(jsval));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				ThrowSimple("Bad native pointer write: memory location is invalid, inaccessible, or read-only");
			}
			return JsNoError;
		}
	};

	// Native type view for HANDLE types
	struct HandleNativeTypeView : public ScalarNativeTypeView
	{
		using ScalarNativeTypeView::ScalarNativeTypeView;

		virtual JsErrorCode Get(JsValueRef self, void *nativep, JsValueRef *jsval) const override
		{ 
			__try
			{
				return HandleData::CreateFromNative(*reinterpret_cast<const HANDLE*>(nativep), *jsval);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				ThrowSimple("Bad native pointer dereference: memory location is invalid or inaccessible");
				return JsNoError;
			}
		}

		virtual JsErrorCode Set(JsValueRef self, void *nativep, JsValueRef jsval) const override
		{ 
			__try
			{
				*reinterpret_cast<HANDLE*>(nativep) = HandleData::FromJavascript(jsval);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				ThrowSimple("Bad native pointer write: memory location is invalid, inaccessible, or read-only");
			}
			return JsNoError;
		}
	};

	// Native type view for VARIANT types
	struct VariantNativeTypeView : public ScalarNativeTypeView
	{
		using ScalarNativeTypeView::ScalarNativeTypeView;

		virtual JsErrorCode Get(JsValueRef self, void *nativep, JsValueRef *jsval) const override
		{
			__try
			{
				return VariantData::CreateFromNative(reinterpret_cast<const VARIANT*>(nativep), *jsval);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				ThrowSimple("Bad native pointer dereference: memory location is invalid or inaccessible");
				return JsNoError;
			}
		}

		virtual JsErrorCode Set(JsValueRef self, void *nativep, JsValueRef jsval) const override
		{
			__try
			{
				VARIANT *pv = reinterpret_cast<VARIANT*>(nativep);
				VariantClear(pv);
				VariantData::CopyFromJavascript(pv, jsval);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				ThrowSimple("Bad native pointer write: memory location is invalid, inaccessible, or read-only");
			}
			return JsNoError;
		}
	};

	// Native type view for BSTR types
	struct BSTRNativeTypeView : public ScalarNativeTypeView
	{
		using ScalarNativeTypeView::ScalarNativeTypeView;

		virtual JsErrorCode Get(JsValueRef self, void *nativep, JsValueRef *jsval) const override
		{
			__try
			{
				BSTR bstr = *reinterpret_cast<BSTR*>(nativep);
				if (bstr == nullptr)
				{
					*jsval = inst->nullVal;
					return JsNoError;
				}
				else
					return JsPointerToString(bstr, SysStringLen(bstr), jsval);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				ThrowSimple("Bad native BSTR dereference: memory location is invalid or inaccessible");
				return JsNoError;
			}
		}

		virtual JsErrorCode Set(JsValueRef self, void *nativep, JsValueRef jsval) const override
		{
			__try
			{
				// free any existing BSTR
				BSTR *pbstr = reinterpret_cast<BSTR*>(nativep);
				if (*pbstr != nullptr)
				{
					SysFreeString(*pbstr);
					*pbstr = nullptr;
				}

				// if the new value is another BSTR, copy it
				if (auto obj = NativeTypeWrapper::Recover<NativeTypeWrapper>(jsval, nullptr); obj != nullptr)
				{
					if (obj->sig == L"B")
					{
						BSTR psrc = *reinterpret_cast<BSTR*>(obj->data);
						if (psrc != nullptr)
							*pbstr = SysAllocString(psrc);

						return JsNoError;
					}
				}

				// otherwise, convert it to a string value
				JsErrorCode err;
				JsValueRef strval;
				const wchar_t *p;
				size_t len;
				if ((err = JsConvertValueToString(jsval, &strval)) != JsNoError
					|| (err = JsStringToPointer(strval, &p, &len)) != JsNoError)
					return err;

				// create a BSTR from the Javascript string
				*pbstr = SysAllocStringLen(p, len);
				return JsNoError;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				ThrowSimple("Bad BSTR write: memory location is invalid, inaccessible, or read-only");
			}
			return JsNoError;
		}
	};

	// Determine if a pointer conversion for a native value is legal
	static bool IsPointerConversionValid(SigParser *sigFrom, SigParser *sigTo);

	// Skip the pointer or array qualifier in a type signature.  Array and pointer
	// types can generally be used interchangeably when conversion to a pointer
	// type is required.  E.g., if we have a variable of type T*, we can assign
	// it a value of type T*, T[], or T[dim].
	static const WCHAR *SkipPointerOrArrayQual(const WCHAR *sig);

	// is the given type a pointer type?
	static bool IsPointerType(const WCHAR *sig);

	// is the given type an array type?
	static bool IsArrayType(const WCHAR *sig);

	// Native type view for pointer types
	struct PointerNativeTypeView : public ScalarNativeTypeView
	{
		PointerNativeTypeView(size_t offset, SigParser *sig, WCHAR stringType);

		virtual JsErrorCode Get(JsValueRef self, void *nativep, JsValueRef *jsval) const override;
		virtual JsErrorCode Set(JsValueRef self, void *nativep, JsValueRef jsval) const override;
			
		// type signature of referenced data
		WSTRING sig;

		// size of underlying type
		size_t size;

		// String type code ('T' or 't', or '\0' for non-strings).  If the original
		// pointer was declared using one of the null-terminated string types, we
		// record the string type code here.  The underlying pointer is still just
		// a pointer to CHAR or WCHAR, but we use this extra field to remember that
		// it was originally declared as a string type, so that we can be smarter
		// about conversions to and from Javascript strings.
		WCHAR stringType;

		virtual JsErrorCode TryGet(SigParser *sig, void *nativep, JsValueRef *jsval) const;
	};

	// Native type cache
	struct NativeTypeCacheEntry
	{
		NativeTypeCacheEntry(JsValueRef proto) : proto(proto) 
		{
			JsAddRef(proto, nullptr);
		}

		~NativeTypeCacheEntry()
		{
			JsRelease(proto, nullptr);
		}

		// Javascript prototype object for this type.  This is the prototype
		// used for an external NativeTypeWrapper object that provides a view
		// on native data with this type signature.
		JsValueRef proto;

		// Native function context objects.  Each method in the prototype
		// is a native function, with a context that specifies its view of 
		// the native data structure.  The context objects are dynamically
		// created when the prototype is set up, so this tracks their memory
		// for eventual deletion.
		std::list<std::unique_ptr<NativeTypeView>> views;
	};
	std::unordered_map<WSTRING, NativeTypeCacheEntry> nativeTypeCache;

	// initialize the prototype object for a native object view
	void InitNativeObjectProto(NativeTypeCacheEntry *entry, SigParser *sig);

	// populate a native type view property
	template<typename ViewType>
	void AddToNativeTypeView(NativeTypeCacheEntry *entry, const WCHAR *name, ViewType *view,
		bool hasValueOf, bool hasSetter);
};
