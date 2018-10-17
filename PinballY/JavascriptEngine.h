// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Javascript interface.  This encapsulates the JSRT API exposed by
// ChakraCore to provide script execution services.
//

#pragma once
#include "../ChakraCore/include/ChakraCore.h"

// Javascript engine interface
class JavascriptEngine : public RefCounted
{
public:
	JavascriptEngine();

	// initialize
	bool Init(ErrorHandler &eh);

	// run a script
	bool Run(const TCHAR *script, const TCHAR *url, ErrorHandler &eh);

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
	
	// Add a scheduled task.  
	//
	// 'func' is the Javascript function to invoke.  
	//
	// 'dt' is the time interval in milliseconds (from the current time) 
	// before the task is ready to execute.  This can be used to set 
	// timeouts and intervals.  0 means the task is ready to execute 
	// immediately, but of course it won't actually run immediately, as
	// tasks are only execute on calls to RunTasks().
	// 
	// 'interval' is the repeating interval time, in milliseconds.  To
	// set up a recurring timed task, pass a non-negative interval value;
	// each time the task is executed, it will be re-scheduled to run
	// again after the interval elapses from the finish of the current
	// run.  If 'interval' is negative, the task will be executed once
	// and discarded.
	//
	// Returns the new task's ID, which can be used to cancel the task
	// before it executes.
	double AddTask(JsValueRef func, ULONGLONG dt, LONGLONG interval = -1);

	// Cancel a task
	void CancelTask(double id);

	// Are any tasks pending?
	bool IsTaskPending() const { return taskQueue.size() != 0; }

	// Get the scheduled time of the next task.  This is the time in terms
	// of GetTickCount64() for the next task ready to execute.  This can be
	// earlier than the current time. 
	ULONGLONG GetNextTaskTime();

	// Run ready scheduled tasks 
	void RunTasks();

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

	template<> class ToNativeConverter<TSTRING> : public ToNativeConverterBase
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
			return WSTRINGToTSTRING(w);
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
			{ return reinterpret_cast<NativeFunctionBinderBase*>(cbState)->Invoke(callee, isConstructor, argv, argc); }

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

	// Native callback wrapper.  
	//
	// This class provides the easiest way to implement a native callback
	// function that has a fixed signature.  Create an instance of this
	// class like this:
	//
	//    class MyFunctionClass : public NativeFunction<ReturnType(Arg1, Arg2...)>
	//    {
	//    public:
	//        virtual ReturnType Impl(Arg1 arg1, Arg2 arg2, ...)
	//        {
	//            // do the native function work here
	//            return returnValue;
	//        }
	//    };
	//
	// 
	// 
	template <typename R> class NativeFunction { };
	template <typename R, typename... Ts>
	class NativeFunction<R(Ts...)> : public NativeFunctionBinder<R(Ts...)>
	{
	public:
		// virtual member function that actually implements the callback
		virtual R Impl(Ts...) const = 0;

		// static invoker - this is what JS actually calls
		virtual JsValueRef Invoke(JsValueRef callee, bool isConstructor, JsValueRef *argv, unsigned short argc) const override
		{
			// set up a lambda to invoke this->Impl with the template arguments
			auto func = [this](Ts... args) { return this->Impl(args...); };

			// Convert the javascript values to native values matching the Impl() signature.
			// The first argument is the 'this' pointer, which we don't use.
			bool ok = true;
			auto args = this->Bind(argv + 1, argc - 1, ok);

			// call the function and convert the return value back to js
			FromNativeConverter<R, Ts...> rconv;
			return rconv.Apply(func, args);
		}
	};

	// Create a global native callback function.  This creates a property
	// of the 'global' object of the given name, and assigns it to a native
	// callback to the given function object.
	bool DefineGlobalFunc(const CHAR *name, NativeFunctionBinderBase *func, ErrorHandler &eh);

	// Exported value.  This allows the caller to store a javascript value
	// in C++ native code, for later use.  For example, this can be used to
	// store a javascript callback function to invoke on a timeout or event.
	// This adds an explicit external reference to the object as long as the
	// host is holding it.
	class ExportedValue
	{
	public:
		ExportedValue(JsValueRef &val, JavascriptEngine *engine) :
			val(val),
			engine(engine, RefCounted::DoAddRef)
		{
			JsAddRef(val, nullptr);
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

protected:
	~JavascriptEngine();

	// initialized
	bool inited;

	// special values
	JsValueRef nullVal;
	JsValueRef undefVal;
	JsValueRef zeroVal;
	JsValueRef falseVal;
	JsValueRef trueVal;

	// Task.  This encapsulates a scheduled task, such as a promise completion
	// function, a timeout, an interval, or a module load handler.
	struct Task
	{
		Task(double id, JsValueRef func, ULONGLONG readyTime, LONGLONG interval) :
			id(id), valid(true), func(func), readyTime(readyTime), interval(interval)
		{
		}

		// Task ID.  We use double, because (a) we want to be able to expose
		// this value to Javascript, for use as the task ID for cases like 
		// timeouts and intervals, and (b) we want it to be as large a type
		// as possible, so that we can use a simple serial number to assign
		// ID values without much risk of wrapping.  double is the best fit
		// to these needs; it allows values up to 2^53-1 (about 10^16), and
		// fits the native JS 'number' type.
		double id;

		// Task is valid.  A task can be canceled before it's executed,
		// and this can be done from Javascript code, such as by clearTimeout()
		// or clearInterval().  When that happens, we don't immediately remove
		// the task from the queue, because we could be in a nested call from
		// a queue iteration, and messing with the queue in that context could 
		// corrupt the caller's iterator.  So intead, we simply mark the task
		// as invalid, and leave it for the queue iterator to remove dead
		// tasks.
		bool valid;

		// The javascript function to call when the task is executed.  Note
		// that we must add a counted external reference to all functions
		// stored here, and remove them when the task is deleted.
		JsValueRef func;

		// Task ready timestamp.  This is the earliest time that the task
		// can be executed.
		ULONGLONG readyTime;

		// Repeat interval.  If this is non-negative, it represents the time
		// in milliseconds for re-scheduling the event each time it fires.
		// A negative value means that this is a one-shot event.
		LONGLONG interval;
	};

	// task queue
	std::list<Task> taskQueue;

	// next available task ID
	double nextTaskID;

	// promise continuation callback
	static void CALLBACK PromiseContinuationCallback(JsValueRef task, void *ctx);

	// convert a JsErrorCode value to a string representation, for error logging purposes
	static const TCHAR *JsErrorToString(JsErrorCode err);

	// JS runtime handle.  This represents a single-threaded javascript execution
	// environment (heap, compiler, garbage collector).
	JsRuntimeHandle runtime;

	// JS execution context.  This essentially is the container of the "global"
	// javascript object (that is, the object at the root level of the js namespace
	// that unqualified function and variable names attach to).
	JsContextRef ctx;

	// Source script cookie.  This is an opaque identifier used in the engine
	// to identify script sources uniquely.  We increment this for each source
	// file we load and execute.
	JsSourceContext srcCookie;
};
