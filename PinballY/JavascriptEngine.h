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

	// Evaluate a script
	bool EvalScript(const WCHAR *scriptText, const TCHAR *url, JsValueRef *returnVal, ErrorHandler &eh);

	// Fire an event.  This evaluates the given script text, converts the 
	// Javascript return value to boolean, and returns the result.  A true
	// return means that the event handler wants to allow the system event
	// handling to proceed; false means that it wants to stop the system
	// handling: that is, the script called preventDefault() or some similar
	// handler function.
	bool FireEvent(const TCHAR *scriptText, const TCHAR *url);

	// Load a module
	bool LoadModule(const TCHAR *url, ErrorHandler &eh);

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

	// Define a global function, creating a native wrapper for it.  The wrapper
	// is added to an internal list to ensure that it's deleted with the engine.
	template <typename ContextType, typename R, typename... Ts>
	bool DefineGlobalFunc(const CHAR *name, R (*func)(ContextType *, Ts...), ContextType *context, ErrorHandler &eh)
	{
		// create the wrapper
		auto wrapper = WrapNativeFunction(func, context);

		// add it to our list for disposal
		this->nativeWrappers.emplace_back(wrapper);

		// define the function
		return this->DefineGlobalFunc(name, wrapper, eh);
	}

	template <typename C, typename R, typename... Ts>
	bool DefineGlobalFunc(const CHAR *name, R (C::*func)(Ts...), C *self, ErrorHandler &eh)
	{
		// create the wrapper
		auto wrapper = WrapNativeMemberFunction(func, self);

		// add it to our list for disposal
		this->nativeWrappers.emplace_back(wrapper);

		// define the function
		return this->DefineGlobalFunc(name, wrapper, eh);
	}

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

	// Task.  This encapsulates a scheduled task, such as a promise completion
	// function, a timeout, an interval, or a module load handler.
	struct Task
	{
		Task() : id(nextId++), readyTime(0), cancelled(false) { }
		virtual ~Task() { }

		// Execute the task.  Returns true if the task should remain
		// scheduled (e.g., a repeating interval task), false if it should
		// be discarded.
		virtual bool Execute(JavascriptEngine *) = 0;

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
		virtual bool Execute(JavascriptEngine *) override;
	};

	// Module eval task
	struct ModuleEvalTask : ModuleTask
	{
		ModuleEvalTask(JsModuleRecord module, const WSTRING &path) : ModuleTask(module, path) { }
		virtual bool Execute(JavascriptEngine *) override;
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
		virtual bool Execute(JavascriptEngine *js) override;

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

		virtual bool Execute(JavascriptEngine *js) override
		{
			// do the basic execution
			__super::Execute(js);

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
		ModuleHostInfo(JavascriptEngine *self, WSTRING &path, JsModuleRecord module) :
			self(self), path(path), module(module) { }

		JavascriptEngine *self;      // 'this' pointer
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
		SourceCookie(JavascriptEngine *self, const WSTRING &file) :
			self(self), file(file) { }

		JavascriptEngine *self;      // the instance that loaded the source
		WSTRING file;                // script source file
	};
	std::list<SourceCookie> sourceCookies;

	// List of Javascript callback wrappers.  We don't need to use this list
	// while running; it's only needed so that we can delete the wrappers at
	// window destruction time.
	std::list<std::unique_ptr<JavascriptEngine::NativeFunctionBinderBase>> nativeWrappers;
};
