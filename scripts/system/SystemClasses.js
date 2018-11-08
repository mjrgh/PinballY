// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Javascript class framework for PinballY system objects.  PinballY
// loads this file automatically at startup when Javascript is enabled.
//


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
class CommandEvent extends Event
{
    constructor(command)
    {
        super("command", { cancelable: true });
        Object.defineProperty(this, "command", { value: command });
    }
}

// Base class for keyboard events
class KeyEvent extends Event
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

class KeyDownEvent extends KeyEvent
{
    constructor(vkey, key, code, location, repeat, background)
    {
        super("keydown", vkey, key, code, location, repeat, background);
    }
}

class KeyUpEvent extends KeyEvent
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
class JoystickButtonEvent extends Event
{
    constructor(type, unit, button, repeat, background)
    {
        super(type, { cancelable: true });
    }
}

class JoystickButtonDownEvent extends JoystickButtonEvent
{
    constructor(unit, button, repeat, background)
    {
        super("joystickbuttondown", unit, button, repeat, background);
    }
}

class JoystickButtonUpEvent extends JoystickButtonEvent
{
    constructor(unit, button, repeat, background)
    {
        super("joystickbuttonup", unit, button, repeat, background);
    }
}    

// Game launch event.  This is fired on the main application object
// when a game is about to be launched.
class LaunchEvent extends Event
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
let mainWindow = new EventTarget();


// ------------------------------------------------------------------------
//
// Native DLL interface
//

this.DllImport = class DllImport
{
    constructor()
    {
        // create a CParser to parse function bindings and struct types
        this.cparser = new CParser();
    }
    
    // Bind a DLL function.  This loads the DLL (if it hasn't already
    // been loaded) and binds the function, returning a callable function
    // object that can be invoked to call the DLL function.  Throws errors
    // if the DLL can't be found, the function doesn't exist, or the
    // signature can't be parsed.
    //
    // The function signature is provided as a string formatted in the
    // C language format for a function declaration.  In most cases, you
    // can simply copy a function declaration from a Windows SDK header
    // to get the signature for a standard Windows function.
    //
    bind(dllName, signature)
    {
        // if the signature is specified as an iterable, bind each signature,
        // and return a list of the bindings
        if (typeof signature !== "string" && typeof signature[Symbol.iterator] === "function")
        {
            let lst = [];
            for (let sig of signature)
                lst.push(this.bind(dllName, sig));
            return lst;
        }

        // parse the C-style function declaration into our internal format
        try
        {
            var decl = this.cparser.parse(signature);
        }
        catch (exc)
        {
            throw new Error("DllImport.bind(" + signature + "): error parsing function signature: " + exc);
        }
        if (!decl || decl.length == 0)
            throw new Error("DllImport.bind(" + signature + "): unable to parse signature");
        if (decl.length > 1)
            throw new Error("DllImport.bind(" + signature +
                            "): multiple declarations found; bind() requires a single function definition");
        decl = decl[0];
        if (decl.type != "Declaration" || !decl.name)
            throw new Error("DllImport.bind(" + signature +
                            "): C-style function declaration is required (decl.type=" + decl.type + ", name=" + decl.name + ")");

        // make sure it looks like a function declaration
        var desc = decl.unparse();
        if (!/^\((.+)\)$/.test(desc))
            throw new Error("DllImport.bind(" + signature + "): this is not a function declaration");

        // toss out the enclosing parens, since it has to be a function
        desc = RegExp.$1;

        // bind the function (_bind() is a native callback provided by
        // the system)
        var nativeFunc = this._bind(dllName, decl.name);

        // The native function is an external (native) object representing
        // the callback's machine code address.  That object doesn't have
        // any meaning to the Javascript engine.  It can only be used in
        // calls to the native _call() callback, which recovers the native
        // function pointer from the external data, converts the Javascript
        // arguments into native stack arguments, and invokes the native
        // DLL code at the target address.  Wrap the native function pointer
        // in a lambda that calls _call() to invoke the native function.
        return (...args) => (this._call(nativeFunc, desc, ...args));
    }

    // Define a type.  The argument is a standard C struct, union, enum, or
    // typedef statement, or a list of statements separated by semicolons.
    // The types are added to the internal type table in the DllImport
    // object, so they can be used in subsequent function bindings.  Note
    // that C struct/union/enum namespace rules apply, NOT C++ rules, so
    // "struct foo" doesn't define a global "foo" type, just a "struct foo"
    // type.  You can make it a global name entry with an explicit typedef,
    // as in "typedef struct foo foo".
    define(types) { this.cparser.parse(types); }

    // Create an instance of a native type.  The argument is a string
    // giving a type signature, which can be a native primitive type
    // (char, unsigned int, float), a pointer type (char*), a struct
    // or union type, an array of any of these (int[10], struct foo[5]).
    // Types previously defined with define("typedef...") can be used.
    // Returns an object representing the native value; this can then
    // be used in calls to native functions where pointers to native
    // types are required.
    create(type) { return this._create(this.cparser.parse(type)[0].unparse()); }

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
    sizeof(type) { return this._sizeof(this.cparser.parse(type)[0].unparse()) }

    // Internal method to wrap an external function object in a callbable lambda
    _bindExt(nativeFunc, desc) { return (...args) => (this._call(nativeFunc, desc, ...args)); }
};

// Default DllImport instance.  Each instance acts as a namespace for
// imported functions and struct definitions.  If multiple namespaces
// are needed (for different DLLs with conflicting struct type names,
// for example), additional instances can be created as needed.
let dllImport = new DllImport();

// Prototype for HANDLE objects.  These are created by the DllImport
// native code for HANDLE values returned by native DLL calls.
this.HANDLE = function HANDLE(...args) { return HANDLE._new(...args); };

// Prototype for NativePointer objects.  These are created by the
// DllImport native code to represent pointers to native objects
// passed back from DLL calls.
this.NativePointer = function NativePointer(...args) { return NativePointer._new(...args); };

// Prototype for Int64 (signed 64-bit integer) and Uint64 (unsigned
// 64-bit integer) objects.  These are system objects used by DllImport
// to represent 64-bit integer types from native code.  The system
// provides basic arithmetic methods on these.
this.Int64 = function Int64(...args) { return Int64._new(...args); };
this.Uint64 = function Uint64(...args) { return Uint64._new(...args); };


// Convert a Uint16 or Uint8 buffer to a Javascript string, treating the
// buffer as a null-terminated string of Unicode characters.  
Uint16Array.prototype.toString = Uint8Array.prototype.toString = function()
{
    let end = this.findIndex(c => c == 0);
    return String.fromCharCode.apply(null, end >= 0 ? this.slice(0, end) : this);
};

// Convert a Uint16Array to a Javascript string, with no null termination
Uint16Array.prototype.toStringRaw = Uint8Array.prototype.toStringRaw = function(length)
{
    return String.fromCharCode.apply(null, length === undefined ? this : this.slice(0, length));
};

// Create a Uint16 or Uint8 buffer from a Javascript string.  If the length
// is specified, the buffer is created at the given size, otherwise it's big
// enough to hold the characters of the string plus a null terminator.
Uint16Array.fromString = Uint8Array.fromString = function(str, length)
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
