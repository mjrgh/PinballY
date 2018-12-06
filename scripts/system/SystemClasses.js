// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Javascript class framework for PinballY system objects.  PinballY
// loads this file automatically at startup when Javascript is enabled.
//


// ------------------------------------------------------------------------
//
// Console object.  This is designed to act similar to the browser
// equivalent.
//
this.console = {
    assert: function(assertion, ...args)
    {
        if (!assertion)
        {
            this.log(...args);
            this.log(this._stack().join("\n"));
        }
    },

    count: function(label) { this._applyCount(label, (key, disp) => {
        this.log(disp + ": " + (this._countTable[key] = (this._countTable[key] || 0) + 1)); });
    },

    countReset: function(label) { this._applyCount(label, (key, disp) => { this._countTable[key] = 0; }); },

    error: function(...args) { this._log("error", this.format(...args)); },

    exception: function(...args) { this._log("error", this.format(...args)); },

    info: function(...args) { this._log("info", this.format(...args)); },

    log: function(...args) { this._log("log", this.format(...args)); },

    time: function(label) { this._timeTable[label || "default"] = Date.now(); },

    timeEnd: function(label) { this._timerOp(label, true); },

    timeLog: function(label) { this._timerOp(label, false); },

    trace: function() { this.log("trace", this._stack().join("\n")); },

    warning: function(...args) { this.log("warning", this.format(...args)); },

    // formatter for assert(), error(), log()
    format: function(...args)
    {
        if (args.length == 0)
            return "";

        var pat = /%([\-+ #0]*)(\d+)?(\.\d+)?([oOdisfxX])/g;
        if (args.length > 1 && pat.test(args[0]))
        {
            let ok = true;
            let i = 1;
            let expansion = args[0].replace(pat, (match, flags, width, prec, spec) =>
            {
                if (i >= args.length)
                {
                    ok = false;
                    return "";
                }
                let a = args[i++];

                let doInt = (radix, prefix) =>
                {
                    let s = Math.trunc(a).toString(radix);

                    if (flags.indexOf("+") >= 0)
                        s = (a > 0 ? "+" : a == 0 ? " " : "") + s;
                    else if (flags.indexOf(" ") >= 0 && a >= 0)
                        s = " " + s;

                    if (prec && prec != "" && +prec.substr(1) > s.length)
                    {
                        let extra = +prec.substr(1) - s.length;
                        if (extra == 0 && a == 0)
                            s = "";
                        else
                            s = s.replace(/^[+\-]?/, "$&" + "0".repeat(extra));
                    }
                    else if (width && width != "" && +width > s.length)
                    {
                        let extra = +width - s.length;
                        if (flags.indexOf("0") >= 0)
                            s = s.replace(/^[+\-]?/, "$&" + "0".repeat(extra));
                        else if (flags.indexOf("-") >= 0)
                            s = s + " ".repeat(extra);
                        else
                            s = " ".repeat(extra) + s;
                    }

                    if (flags.indexOf("#") >= 0 && prefix)
                        s = s.replace(/^\s*[+\-]?/, "$&" + prefix);

                    return s;
                };

                let doFloat = () =>
                {
                    let s = a.toFixed(prec && prec != "" ? +prec.substr(1) : 6);

                    if (flags.indexOf("+") >= 0)
                        s = (a > 0 ? "+" : a == 0 ? " " : "") + s;
                    else if (flags.indexOf(" ") >= 0 && a >= 0)
                        s = " " + s;

                    if (width && width != "" && +width > s.length)
                    {
                        let extra = +width - s.length;
                        if (flags.indexOf("0") >= 0)
                            s = s.replace(/^[+\-]?/, "$&" + "0".repeat(extra));
                        else if (flags.indexOf("-") >= 0)
                            s = s + " ".repeat(extra);
                        else
                            s = " ".repeat(extra) + s;
                    }

                    return s;
                };

                let doString = () =>
                {
                    let s = a.toString();

                    if (width && width != "" && +width > s.length)
                    {
                        let extra = +width - s.length;
                        if (flags.indexOf("-") >= 0)
                            s = s + " ".repeat(extra);
                        else
                            s = " ".repeat(extra) + s;
                    }

                    if (prec && prec != "" && +prec.substr(1) < s.length)
                        s = s.substr(0, +prec.substr(1));

                    return s;
                };

                switch (spec)
                {
                case 'o':
                case 'O':
                    return (a === null ? "null" :
                            a === undefined ? "undefined" :
                            a.toString());

                case 'd':
                case 'i':
                    return doInt(10);

                case 'x':
                    return doInt(16, "0x").toLowerCase();
                    
                case 'X':
                    return doInt(16, "0x").toUpperCase();

                case 'f':
                    return doFloat();

                case 's':
                    return doString(a);

                default:
                    ok = false;
                    return a;
                }
            });

            if (ok)
                return expansion;
        }

        return args.join(" ");
    },

    // internal stack trace formatter
    _stack: function()
    {
        // Get the stack from a synthesized error object, remove the first three lines
        // ("Error at", _stack() level, and our internal caller level), and reformat
        // the resulting lines look more like a browser console trace.
        return new Error().stack.split("\n").slice(3).map(l => {
            l = l.replace(/^\s*at\s+/, "");
            if (/(.+)\s*\((.+)\)$/.test(l))
                l = RegExp.$1 + "@" + RegExp.$2;
            else
                l = "@" + l;
            return l;
        });
    },

    // internal handler for count() and countReset()
    _applyCount: function(label, func)
    {
        if (label)
            func(label, label);
        else
            func(this._stack[0], "default");
    },

    // label table for count() and countReset()
    _countTable: { },

    // general handler for timeLog() and timeEnd() - logs the time, and
    // optionally removes the timer
    _timerOp: function(label, remove)
    {
        label = label || "default";
        let now = Date.now();
        let t0 = this._timeTable[label];
        if (t0)
        {
            this._log(label + ": " + (Date.now() - now) + "ms");
            if (remove)
                delete this._timeTable[label];
        }
        else
            this._log("Timer " + label + " does not exist");
    },

    // timer table for time() and timeEnd()
    _timeTable: { },
};


// ------------------------------------------------------------------------
//
// Define Event and EventTarget inside a function scope, so that
// we can define some private properties shared among these classes.
//
let { Event, EventTarget } = (() =>
{
    let _propagationStopped = Symbol("_propagationStopped");
    let _immediatePropagationStopped = Symbol("_immediatePropagationStopped");
    let _defaultPrevented = Symbol("_defaultPrevented");
    let _listeners = Symbol("_listeners");

    // Normalize the 'options' argument to addEventListener or removeEventListener.
    // For browser compatibility, this can be specified as a boolean 'capture'
    // value, or an object with properties giving the options.  We normalize it
    // the object format in all cases.
    let normalizeOptions = (options) => (
        (typeof options === "boolean") ? { capture: capture } : options ? options : { });

    // Add an event listener to an event target
    let add = (target, type, namespaces, listener, options) =>
    {
        // get or create the list of listeners for this event type name
        let lst = target[_listeners][type];
        if (!lst)
            target[_listeners][type] = lst = [];

        // add the new listener
        lst.push({ namespaces: namespaces, listener: listener, options });
    };

    // Internal common handler for eventTarget.on() and eventTarget.one()
    let on = (target, events, once, rest) =>
    {
        // rest can be [data, listener] or just [listener]
        let data = rest.length > 1 ? rest.shift() : undefined;
        let listener = rest[0];

        // build the options object
        let options = { data: data, once: once };

        // process the events
        for (let event of events.trim().split(/\s+/))
        {
            // separate the event name and namespace list
            let [type, ...namespaces] = event.split(".");
            
            // add the listener
            add(target, type, new Set(namespaces), listener, options);
        }
    };

    //
    // Event target.  This is a base class for objects that can be
    // receive events.
    //
    let EventTarget = class EventTarget
    {
        constructor() {
            this[_listeners] = { };
        }

        // Add an event listener.  Similar to the Web browser namesake.
        //
        // 'options' is an optional object containing additional arguments
        // as properties.  Recognized properties:
        //
        // once (boolean): if true, remove the event automatically after
        //   it fires for the first time
        //
        // data (anything): arbitrary user-defined value to be passed to
        //   the event handler in event.data each time it's called
        //    
        addEventListener(type, listener, options) {
            add(this, type, [], listener, normalizeOptions(options));
        }

        // jquery-style on(events, data, func).  'events' is a space-delimited
        // string of event names to bind; each event can optionally include a
        // list of dot-delimited namespaces, to identify the same item for
        // later removal via off().
        on(events, ...rest) { on(this, events, false, rest); }
        
        // jquery style one(events, data, func).  Same as on(), but registers
        // a once-only event.
        one(events, ...rest) { on(this, events, true, rest); }

        // jquery-style off(events, func).  'events' can include namespaces
        off(events, func)
        {
            // process the events
            for (let event of events.trim().split(/\s+/))
            {
                // separate the event name and namespace list
                let [type, ...namespaces] = event.split(".");

                // remove a single type
                let _off = (type) =>
                {
                    // if there's a listener list for this type, process it
                    let lst = this[_listeners][type];
                    if (lst)
                    {
                        // scan the list for matching items
                        for (let i = 0; i < lst.length; ++i)
                        {
                            // if a function was provided, and it doesn't match the
                            // listener on this item, don't remove this item
                            if (func !== undefined && func != lst[i].listener)
                                continue;

                            // if a namespace list was provided, check for a match
                            if (namespaces.length != 0)
                            {
                                // presume no match
                                let found = false;
                                let eventNamespaces = lst[i].namespaces;
                                for (let namespace of namespaces)
                                {
                                    if (eventNamespaces.has(namespace))
                                    {
                                        found = true;
                                        break;
                                    }
                                }

                                // stop if we didn't find a match
                                if (!found)
                                    continue;
                            }

                            // It passed all tests - remove it by splicing it out
                            // of the listener list.  Note that we then have to back
                            // up one slot in the list iteration to compensate.
                            lst.splice(i, 1);
                            --i;
                        }
                    }
                };

                // if type is empty, remove everything matching the namespace;
                // otherwise just process the type list
                if (type == "")
                {
                    for (let curtype in this[_listeners])
                        _off(curtype);
                }
                else
                {
                    // single event type only
                    _off(type);
                }
            }
        }

        removeEventListener(type, listener, options)
        {
            // proceed only if there are any listeners for this type
            var lst = this[_listeners][type];
            if (lst)
            {
                // normalize the options
                options = normalizeOptions(options);
                
                // scan the list for a match to the listener and 'capture' option
                for (let i = 0; i < lst.length; ++i)
                {
                    // check for a match
                    if (lst[i].listener == listener && !!lst[i].options.capture == !!options.capture)
                    {
                        // got it - remove it
                        lst.splice(i, 1);
                        --i;
                    }
                }
            }
        }

        dispatchEvent(event)
        {
            // get the listener list for this event type
            let lst = this[_listeners][event.type];
            if (lst)
            {
                // set up the current target on the event
                event.target = event.currentTarget = this;
                
                // iterate over a copy of the list, so that our iteration isn't
                // affected by changes made in the event handlers
                for (let l of [...lst])
                {
                    // set the event data to the data object from the options
                    event.data = l.options.data;

                    // Call the event, with 'this' set to the current target.
                    l.listener.call(this, event);

                    // if this was a once-only event, remove it
                    if (l.options.once)
                    {
                        for (let i = 0; i < lst.length; ++i)
                        {
                            if (Object.is(lst[i], l))
                            {
                                lst.splice(i, 1);
                                break;
                            }
                        }
                    }

                    // stop if immediate propagation was stopped
                    if (event[_immediatePropagationStopped])
                        break;
                }
            }

            // return "should we do the system processing?" - that is,
            // true if preventDefault was never called, or the event is
            // non-cancelable
            return !(event.cancelable && event.defaultPrevented);
        }
    };

    //
    // Base class for all events.  This is modeled on the Event class
    // used in Web browsers.
    //
    let Event = class Event
    {
        constructor(type, eventInit)
        {
            Object.defineProperty(this, "type", { value: type });
            Object.defineProperty(this, "bubbles", { value: !!eventInit.bubbles });
            Object.defineProperty(this, "cancelable", { value: !!eventInit.cancelable });
            Object.defineProperty(this, "timeStamp", { value: Date.now() });
            this[_propagationStopped] = false;
            this[_immediatePropagationStopped] = false;
            this[_defaultPrevented] = false;
        }

        // If the event is cancelable, cancel the system default action
        // from occurring.  This makes the system ignore the event after
        // all event listeners have returned, but doesn't affect propagation
        // to other event listeners.
        preventDefault()
        {
            if (this.cancelable)
                this[_defaultPrevented] = true;
        }

        // has the system default action been prevented?
        get defaultPrevented() { return this[_defaultPrevented]; }

        // Stop the event from bubbling to parent event target objects.
        // This doesn't stop 
        stopPropagation() { this[_propagationStopped] = true; }

        // Stop the event from propagating to any other event listeners,
        // including other listeners on the same target and listeners on
        // parent targets.
        stopImmediatePropagation()
        {
            this[_propagationStopped] = true;
            this[_immediatePropagationStopped] = true;
        }
    };

    return { Event: Event, EventTarget: EventTarget };
})();

// Command event.  This is fired on the main application object
// when a button mapped to a command is pressed.  The default system
// action is to carry out the command.
this.CommandEvent = class CommandEvent extends Event
{
    constructor(command)
    {
        super("command", { cancelable: true });
        Object.defineProperty(this, "command", { value: command });
    }
}

// Base class for keyboard events
this.KeyEvent = class KeyEvent extends Event
{
    constructor(type, vkey, key, code, location, repeat, background)
    {
        super(type, { cancelable: true });
        this.vkey = vkey;
        this.key = key;
        this.code = code;
        this.location = location;
        this.repeat = repeat;
        this.background = background;
    }
}

this.KeyDownEvent = class KeyDownEvent extends KeyEvent
{
    constructor(vkey, key, code, location, repeat, background)
    {
        super("keydown", vkey, key, code, location, repeat, background);
    }
}

this.KeyUpEvent = class KeyUpEvent extends KeyEvent
{
    constructor(vkey, key, code, location, repeat, background)
    {
        super("keyup", vkey, key,code, location, repeat, background);
    }
}

// key location codes
const KEY_LOCATION_STANDARD = 0;
const KEY_LOCATION_LEFT = 1;
const KEY_LOCATION_RIGHT = 2;
const KEY_LOCATION_NUMPAD = 3;

// aliases for the Web browser names for these constants, for people
// who are used to these
const DOM_KEY_LOCATION_STANDARD = 0;
const DOM_KEY_LOCATION_LEFT = 1;
const DOM_KEY_LOCATION_RIGHT = 2;
const DOM_KEY_LOCATION_NUMPAD = 3;

// Joystick button events
this.JoystickButtonEvent = class JoystickButtonEvent extends Event
{
    constructor(type, unit, button, repeat, background)
    {
        super(type, { cancelable: true });
        this.unit = unit;
        this.button = button;
        this.repeat = repeat;
        this.background = background;
    }
}

this.JoystickButtonDownEvent = class JoystickButtonDownEvent extends JoystickButtonEvent
{
    constructor(unit, button, repeat, background)
    {
        super("joystickbuttondown", unit, button, repeat, background);
    }
}

this.JoystickButtonUpEvent = class JoystickButtonUpEvent extends JoystickButtonEvent
{
    constructor(unit, button, repeat, background)
    {
        super("joystickbuttonup", unit, button, repeat, background);
    }
}    

// Game launch event.  This is fired on the main application object
// when a game is about to be launched.
this.LaunchEvent = class LaunchEvent extends Event
{
    constructor()
    {
        super("launch", { cancelable: true });
    }
}


// ------------------------------------------------------------------------
//
// Main window object
//
// Events:
//    keydown
//    keyup
//    joydown
//    joyup
//    command
//
this.mainWindow = new EventTarget();


// ------------------------------------------------------------------------
//
// Native DLL interface
//

this.dllImport =
{
    // CParser object, to parse function bindings and struct types
    cparser: new CParser(),

    // Bind a collection DLL functions.  This loads the DLL (if it hasn't
    // already been loaded) and binds the functions, returning a callable
    // function object that can be invoked to call the DLL function.
    // Throws errors if the DLL can't be found, the function doesn't exist,
    // or the function declarations can't be parsed.
    //
    // The 'signature' argument can be a single string, or it can be an
    // iterable collection of strings (such as an array of strings).
    //
    // The function declarations use C language syntax.  In most cases,
    // you can simply copy a function declaration from a Windows SDK header
    // to get the signature for a standard Windows function.
    //
    // You can also mix in struct, union, and typedef declarations as
    // desired.  Those type definitions will be added to an internal table
    // within the object, so that they can be referenced in subsequent
    // function and type declarations.  Type definitions don't contribute
    // any properties to the return value.
    //
    // The return value is an object that contains properties with the same
    // names as the declared functions.  Each property's value is a callable
    // Javascript function that you can invoke to call the native function
    // declared under that name.
    //
    bind: function(dllName, signature)
    {
        // The result will be an object, with properties named for the
        // functions declared.
        let result = { };

        // If the signature is specified as an iterable, bind each signature.
        // If it's a simple string, just bind the string.
        if (typeof signature !== "string" && typeof signature[Symbol.iterator] === "function")
        {
            for (let cur of signature)
                Object.assign(result, this.bind(dllName, cur));
            return result;
        }

        // parse the C-style function declaration into our internal format
        try
        {
            var decls = this.cparser.parse(signature);
        }
        catch (exc)
        {
            throw new Error("dllImport.bind(" + signature.substr(0, 50) + (signature.length > 50 ? "..." : "") +
                            "): error parsing function signature: " + exc);
        }
        if (!decls || decls.length == 0)
            throw new Error("dllImport.bind(" + signature.substr(0, 50) + (signature.length > 50 ? "..." : "") +
                            "): unable to parse signature");

        // go through the list and pull out all function declarations
        for (let decl of decls)
        {
            // If it's a declaration that defines a named function, add it to the
            // results.  Ignore typedefs, anonymous declarations, and non-function
            // declarations.
            if (decl.type == "Declaration" && decl.name)
            {
                // unparse the declaration to get the signature
                var desc = decl.unparse();

                // if it's a function declaration, include it in the results
                if (/^\(/.test(desc))
                {
                    // bind it to create the native function target
                    let func = this._bind(dllName, decl.name);

                    // Now wrap the native function object in a callable lamdba
                    // that invokes the native function.  This will make the native
                    // code callable via the normal Javascript function call syntax.
                    result[decl.name] = ((func, desc) => {
                        return (...args) => dllImport._call(func, desc, ...args);
                    })(func, desc);
                }
            }
        }

        // return the result object
        return result;
    },

    // Define a type.  The argument is a standard C struct, union, enum, or
    // typedef statement, or a list of statements separated by semicolons.
    // The types are added to the internal type table in the dllImport
    // object, so they can be used in subsequent function bindings.  Note
    // that C struct/union/enum namespace rules apply, NOT C++ rules, so
    // "struct foo" doesn't define a global "foo" type, just a "struct foo"
    // type.  You can make it a global name entry with an explicit typedef,
    // as in "typedef struct foo foo".
    define: function(types) { this.cparser.parse(types); },

    // Get the UUID of a COM interface type
    uuidof: function(type) { return this.cparser.uuidof(type); },

    // Create an instance of a native type.  The argument is a string
    // giving a type signature, which can be a native primitive type
    // (char, unsigned int, float), a pointer type (char*), a struct
    // or union type, an array of any of these (int[10], struct foo[5]).
    // Types previously defined with define("typedef...") can be used.
    // Returns an object representing the native value; this can then
    // be used in calls to native functions where pointers to native
    // types are required.
    create: function(type) { return this._create(this.cparser.parse(type)[0].unparse()); },

    // Get the native size of a given type on the current platform.  This
    // returns thesize in bytes of the given type on this platform.  The
    // type is specified by name, and it can be a primitive or pre-defined
    // type name (e.g., sizeof("int") or sizeof("HANDLE")), a struct, union,
    // or typedef type previously defined with define() (e.g.,
    // sizeof("struct foo")).
    //
    // This essentially gives you the result of the C sizeof() operator
    // for the given type.  Type sizes are sometimes required for setting
    // up structs to pass to native code.
    sizeof: function(type) { return this._sizeof(this.cparser.parse(type)[0].unparse()) },

    // Internal method to wrap an external function object in a callbable lambda.
    // This is called from the native external object to create a dynamic binding
    // to a native function pointer passed in to Javascript from external code
    // via a return return value or "out" variable.  We create one wrapper per
    // imported function.
    _bindExt: function(nativeFunc, desc)
    {
        return (...args) => (dllImport._call(nativeFunc, desc, ...args));
    },

    // Internal method to wrap a COM interface member in a callbable lambda.
    // These wrappers are bound to the interface prototype, so the "this"
    // pointer on each call is the COMImportData native object wrapping the
    // native COM object pointer.
    _bindCOM: function(vtableIndex, desc)
    {
        return function(...args) { return dllImport._call(this, vtableIndex, desc, this, ...args); };
    },
};

// populate some basic COM types
dllImport.define(`
    struct _GUID { ULONG Data1; USHORT Data2; USHORT Data3; UCHAR Data4[8]; };
    typedef interface IUnknown '00000000-0000-0000-C000-000000000046' {
        HRESULT QueryInterface(REFIID riid, void **ppvObject);
        ULONG AddRef();
        ULONG Release();
    } IUnknown, *LPUNKNOWN;
`);

// COM VARIANT type codes
//
// The [VTPS] codes indicate which contexts each type can be used in:
//
//   V = VARIANT
//   T = TYPEDESC
//   P = OLE property set
//   S = Safe Array
//
// Codes that aren't marked with V aren't valid in the Variant type.
//
const VT_EMPTY = 0;                     // [V P ] empty, similar to Javascript undefined
const VT_NULL = 1;                      // [V P ] SQL style NULL, similar to Javascript null
const VT_I2 = 2;                        // [VTPS] 2-byte (16-bit) signed int (C "__int16")
const VT_I4 = 3;                        // [VTPS] 4-byte (32-bit) signed int (C "__int32")
const VT_R4 = 4;                        // [VTPS] 4-byte real (C "float")        
const VT_R8 = 5;                        // [VTPS] 8-byte real (C "double")    
const VT_CY = 6;                        // [VTPS] Currency - 96-bit fixed point, scaled by 10000
const VT_DATE = 7;                      // [VTPS] VARIANT Date; double value, days since 12/31/1899
const VT_BSTR = 8;                      // [VTPS] BSTR (Basic String)    
const VT_DISPATCH = 9;                  // [VT S] IDispatch* (COM core scripting interface)
const VT_ERROR = 10;                    // [VTPS] 32-bit signed error code (SCODE)
const VT_BOOL = 11;                     // [VTPS] VARIANT BOOL: 16-bit signed int, 0=false, -1=true
const VT_VARIANT = 12;                  // [VTPS] VARIANT* (pointer to VARIANT); Requires VT_BYREF    
const VT_UNKNOWN = 13;                  // [VT S] IUnknown* (root COM interface)
const VT_DECIMAL = 14;                  // [VT S] 16-byte floating point with decimal scaling
const VT_I1 = 16;                       // [VTPS] 1-byte (8-bit) signed int (C "char")
const VT_UI1 = 17;                      // [VTPS] 1-byte (8-bit) unsigned int (C "unsigned char")
const VT_UI2 = 18;                      // [VTPS] 2-byte (16-bit) unsigned int (C "unsigned __int16")    
const VT_UI4 = 19;                      // [VTPS] 4-byte (32-bit) unsigned int (C "unsigned __int32"
const VT_I8 = 20;                       // [ TP ] 8-byte (64-bit) signed int (C "__int64")
const VT_UI8 = 21;                      // [ TP ] 8-byte (64-bit) unsigned int (C "unsigned __int64")
const VT_INT = 22;                      // [VTPS] machine signed int type (C "int"; same as __int32)
const VT_UINT = 23;                     // [VT S] machine unsigned int typ (C "unsigned int"; same as unsigned __int32)    
const VT_VOID = 24;                     // [ T  ] no value (C "void")
const VT_HRESULT = 25;                  // [ T  ] HRESULT (standard system return code)        
const VT_PTR = 26;                      // [ T  ] generic pointer type, equivalent to C void*
const VT_SAFEARRAY = 27;                // [ T  ] safe array type; never used in VARIANT (use VT_ARRAY instead)    
const VT_CARRAY = 28;                   // [ T  ] C-style array
const VT_USERDEFINED = 29;              // [ T  ] user-defined type
const VT_LPSTR = 30;                    // [ TP ] C-style null-terminated string, single-byte characters
const VT_LPWSTR = 31;                   // [ TP ] C-style null-terminated string, wide characters (16-bit)
const VT_RECORD = 36;                   // [V PS] user-defined struct type
const VT_INT_PTR	= 37;               // [ T  ] signed machine register size width
const VT_UINT_PTR	= 38;               // [ T  ] unsigned machine register size width
const VT_FILETIME = 64;                 // [  P ] system FILETIME struct
const VT_BLOB = 65;                     // [  P ] binary long object; length-prefixed bytes
const VT_STREAM = 66;                   // [  P ] name of stream follows
const VT_STORAGE = 67;                  // [  P ] name of storage follows
const VT_STREAMED_OBJECT = 68;          // [  P ] stream contains an object
const VT_STORED_OBJECT = 69;            // [  P ] storage contains an object
const VT_BLOB_OBJECT = 70;              // [  P ] blob contains an object
const VT_CF = 71;                       // [  P ] clipboard format
const VT_CLSID = 72;                    // [  P ] COM CLSID (class ID, a GUID)
const VT_VERSIONED_STREAM = 73;         // [  P ] stream with a GUID version ID
const VT_BSTR_BLOB = 0xfff;             // [  P ] reserved for system use
const VT_VECTOR = 0x1000;               // [  P ] counted array (bit mask: combines with element type code)
const VT_ARRAY = 0x2000;                // [V   ] SAFEARRAY* (bit mask: combines with element type code)
const VT_BYREF = 0x4000;                // [V   ] pointer (bit mask: combines with element type code)
const VT_RESERVED = 0x8000;             // [    ] reserved for system use
const VT_ILLEGAL = 0xffff;              // [    ] illegal type code
const VT_ILLEGALMASKED = 0xfff;         // [    ] illegal type code after applying VT_TYPEMASK
const VT_TYPEMASK = 0xfff;              // [    ] mask for element types (removing vector/array/byref qualifiers)

// Prototype for HANDLE objects.  These are created by the dllImport
// native code for HANDLE values returned by native DLL calls.
this.HANDLE = function HANDLE(...args) { return HANDLE._new(...args); };

// Prototype for NativePointer objects.  These are created by the
// dllImport native code to represent pointers to native objects
// passed back from DLL calls.
this.NativePointer = function NativePointer() {
    throw new Error("new NativePointer() can't be called directly; use <nativeObject>.at()");
};

this.NativePointer.prototype.to = function(type) { return this._to(dllImport.cparser.parse(type)[0].unparse()); };

// Prototype for COMPointer objects.  These are created by the dllImport
// native code to represent COM interface pointers returned from COM
// APIs: CoCreateInstance(), IUnknown::QueryInterface(), etc.
this.COMPointer = function COMPointer() {
    throw new Error("new COMPointer() can't be called directly; use dllImport.create()");
};

// Prototype for NativeObject objects.  These are created by the
// dllImport native code to represent native primitive types, array
// types, and struct types.
this.NativeObject = function NativeObject() {
    throw new Error("new NativeObject() can't be called directly; use dllImport.create()");
};

// Prototype for Int64 (signed 64-bit integer) and Uint64 (unsigned
// 64-bit integer) objects.  These are system objects used by dllImport
// to represent 64-bit integer types from native code.  The system
// provides basic arithmetic methods on these.
this.Int64 = function Int64(...args) { return Int64._new(...args); };
this.Uint64 = function Uint64(...args) { return Uint64._new(...args); };


// Convert a Uint16 or Uint8 buffer to a Javascript string, treating the
// buffer as a null-terminated string of Unicode characters.  
Int16Array.prototype.toString = Uint16Array.prototype.toString = Int8Array.prototype.toString = Uint8Array.prototype.toString = function()
{
    let end = this.findIndex(c => c == 0);
    return String.fromCharCode.apply(null, end >= 0 ? this.slice(0, end) : this);
};

// Convert a Uint16Array to a Javascript string, with no null termination
Int16Array.prototype.toStringRaw = Uint16Array.prototype.toStringRaw = Int8Array.prototype.toStringRaw = Uint8Array.prototype.toStringRaw = function(length)
{
    return String.fromCharCode.apply(null, length === undefined ? this : this.slice(0, length));
};

// Create a Int16/Uint16 or Int8/Uint8 buffer from a Javascript string.
// If the length is specified, the buffer is created at the given size,
// otherwise it's big enough to hold the characters of the string plus
// a null terminator.
Int16Array.fromString = Uint16Array.fromString = Int8Array.fromString = Uint8Array.fromString = function(str, length)
{
    if (length === undefined)
        length = str.length + 1;
    let buf = new this(length);

    let copylen = Math.min(str.length, length);
    let i = 0;
    for (; i < copylen; ++i)
        buf[i] = str.charCodeAt(i);

    for (; i < length; ++i)
        buf[i] = 0;

    return buf;
};
