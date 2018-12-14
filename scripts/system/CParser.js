// PinballY C declaration parser
//
// Adapted from cparse by Jakob Löw
// 
// "THE BEER-WARE LICENSE":
// Jakob Löw <jakob@m4gnus.de> wrote this code. As long as you retain this notice you
// can do whatever you want with this stuff. If we meet some day, and you think
// this stuff is worth it, you can buy me a beer in return.
//
// The new portions written for PinballY are copyright 2018 Michael Roberts
// GPL v3 or later | NO WARRANTY
//
// -----------------------------------------------------------------------------------
//
// This modified version is specifically for struct and function declarations for the
// PinballY Javascript DllImport feature.  It's been extended to support some standard
// C syntax that was missing from the original (such as pointers to functions, unnamed
// parameters, struct/union/enum qualifiers in parameter names), some Microsoft-specific
// syntax (such as function calling convention specifiers), and to provide pre-defined
// types for many of the Win32 SDK typedefs and macros.  Parsing that we don't need in
// this context has been removed, such as function bodies and global variables.  The
// interface has been changed to an object that encapsulates type tables for typedefs,
// structs, unions, and enums, so that declarations persist across calls: that is, you
// can define a struct in one call, and then refer to it in a later call, such as in
// a function parameter type or a nested struct definition.
//
// To create a parser object:
//
//    let parser = new CParser();
//
// A parser object keeps track of the typedef, struct, union, and enum types that
// have been defined, so these can be used as referenced types in subsequent function
// declarations.
//
//    parser.parse("typedef struct RECT { int left; int top; int right; int bottom; } RECT, *LPRECT");
//    parser.parse("BOOL GetWindowRect(HWND, LPRECT)");
//
// Each call to parse() returns an array of "statement" objects representing the
// top-level statements parsed.  You can parse multiple struct definitions in one
// call, for example, by separating them with semicolons, just like in normal C
// code.  Each statement object is basically a little AST (abstract syntax tree)
// representing the parse results.  These ASTs aren't in any standard format;
// they use an idiosyncratic internal tree structure.
//
// Here's where we get to the interesting part.  The statement object has an
// unparse() method that flattens the AST into a string format defined by the
// PinballY DllImport layer.  The string format contains the same information
// that's in the tree, but in a compact string notation that's designed to be
// very easy to parse mechanically.  The DllImport Javascript layer passes this
// compact notation for each DLL function call to the DllImport C++ layer, which
// uses the information to marshall the Javascript caller's arguments into native
// C++ arguments for the DLL target function, and then to marshall the results
// back to Javascript values on return.
//
// Why do we take the output of one parser and turn it into yet another parsed
// language?  Clearly the two languages contain the same information, at an
// abstract level; they're just different representations.  There are two
// reasons.  The first is that the DllImport layer that does the actual DLL
// data marshalling is (necessarily) written in C++, and it's just easier to
// write a parser for a complex language in Javascript than in C++.  So it's
// better in terms of productivity to do the original C struct and function
// parsing here.  The second reason is performance.  The converted notation
// is not only easy to parse, it's fast to parse.  The C++ code has to parse
// this on every DLL call and apply it to do the argument marshalling, so it
// improves performance to do the relatively time-consuming part of the parse
// once when binding a DLL function, so that we only have to do a quick scan
// of the converted string on each call.
//
// The "unparse" syntax:
//
// - Primitive types are specified by one letter; see the primitive type
//   list in the code for the full list.  Generally, a small letter is
//   the signed version of a type and the capital is the unsigned version:
//   i = signed int, I = unsigned int.
//
// - The primitive types are collapsed into the Microsoft C compiler
//   definitions for the C types.  This means that the distinctions
//   between like portable types are lost: e.g., "long" -> i.  We want
//   the actual native types, so we don't care about the portable
//   abstraction.
//
// - The pre-defined type list includes some not-so-primitive types,
//   such as Windows HANDLE substypes and zero-terminated string types.
//   These are included to allow for more automatic handling of these
//   special cases in the marshalling layer.  Handles are included
//   because they're 64-bit quantities on x64 platforms; that doesn't
//   have a good JS primitive type mapping, so we use native (JS
//   external) objects to represent them.  The string types are
//   included because the zero-terminated quality lets us make
//   assumptions about copying that we couldn't for plain CHAR or
//   WCHAR pointers, and also lets us do character set translation
//   (between Unicode and ANSI) as needed.
//
// - Structures are denoted by {S ... }.  Inside is a space-delimited
//   list of the members.  Each member is written name:type, where type
//   is constructed out of the syntax elements we're describing here.
//   For example, {S a:i} is a struct with one int member named "a".
//   Members are in declaration order.
//
//   Struct members are tagged with names because the marshalling
//   layer can use this to translate between Javascript objects and
//   native structs, using the C struct member tags as Javascript
//   property names for the corresponding slots.
//
// - Unions are just like structs, but written {U ... }.
//
// - An array of N elements of type T is written [N]T
//
// - A pointer to type T is written *T
//
// - A type "const T" is written %T
//
// - Hence, a pointer to a constant value is *%T, and a const pointer
//   to a non-const value is %*T
//
// - A C++ reference to type T is written &T
//
// - Typedefs are fully expanded whenever referenced.  For example,
//   if we have "typedef struct { int a; } foo", and then we want to
//   represent "struct { foo *p; }", we get {S p:*{S a:i}}.  That is,
//   a struct with an element p, which is a pointer to a struct with
//   an int element i.
//
// - Struct and union types are likewise fully expanded when refrenced.
//
// - A function is written (CR A...), where C is the calling convention,
//   R is the return type, and the A's are the parameter types, in order
//   of the parameters in the C language form of the declaration.  (There
//   are no name tags on the arguments, as there are with structs, since
//   there's no need to know these in the native marshalling layer.)
//
//   The calling convention is one letter, the capitalized first letter
//   of the corresponding Microsoft __xxx keyword:  C for __cdecl, S for
//   __stdcall, etc.  (The set of Microsoft keywords happens to be unique
//   in the first letter, so this works.)  The SDK macros (CDECL, WINAPI,
//   etc) are mapped to the expanded values, so WINAPI == __stdcall == S.
//
//   The return type is constructed from the unparse rules, as are the
//   argument types.  So "int __stdcall foo(const char *p, float f)"
//   unparses to (Si *%p f).  Note that argument names aren't included
//   in the unparsed form, as they have no significance in the type
//   formulation.
//
// - A pointer to a func is written in the obvious way, *(...)
//

let CParser = (function()
{
    // Create a parser instance.  This creates a parser namespace.  Types
    // defined via typedef, struct declarations, etc are enrolled in our
    // namespace so that they can be referenced (in function declarations
    // and other structs, for example).  
    function createParser()
    {
        const callingConvMap = {
            "__stdcall": "S",
            "__cdecl": "C",
            "__fastcall": "F",
            "__thiscall": "T",
            "__vectorcall": "V",
            "CDECL": "C",
            "STDCALL": "S",
            "STDMETHODCALLTYPE": "S",
            "WINAPI": "S",
            "CALLBACK": "S"
        };

        const primitiveTypes = {

            // C primitive types, with mappings to DllImport types.
            //
            // In normal C usage, you're supposed to think of short, int, etc
            // as abstract types that are different sizes on different hardware.
            // You're not supposed to assume an exact bit size.  So what we're
            // about to do here will be heresy if you have a proper C way of
            // thinking.  We're going to map the abstract C types to EXACT
            // HARDWARE TYPES with specific bit sizes.  That's because this
            // code works with what comes OUT of the C compiler, not what
            // goes IN.  It's true that C source code has to think of the C
            // types as abstract and portable, but when you actually compile
            // a C program for a given computer, the machine code that comes
            // out of the compiler is tied to the hardware types of the target
            // platform.  That's what we're doing here: we're setting up the
            // type associations from the abstract C type names to the actual
            // x86 and x64 types.
            //
            // The critical thing here is that we use the same type mappings
            // as the Microsoft compilers.  That's required so that our client
            // Javascript code that uses this mechanism to declare a function
            // as taking an 'int' or 'long' argument will get the exact same
            // stack arrangement that a target DLL using the same type expects.
            //
            // The MS compiler uses identical bit sizes for x86 and x64 mode
            // for *most* of the basic C types: char, short, int, long, int64,
            // float, double, and long double are all the same in both modes.
            // The types that vary between x86 and x64 are: all pointer types;
            // most Windows handle types (which are mostly pointer types under
            // the covers); INT_PTR and related types; size_t, ptrdiff_t, and
            // related sizing types.

            "bool": "b",
            "char": "c",
            "unsigned char": "C",
            "short": "s",
            "short int": "s",
            "unsigned short": "S",
            "unsigned short int": "S",
            "int": "i",
            "signed": "i",
            "unsigned": "I",
            "unsigned int": "I",
            "long": "i",
            "long int": "i",
            "unsigned long": "I",
            "unsigned long int": "I",
            "float": "f",
            "long float": "f",
            "double": "d",
            "long double": "d",
            "__int64": "l",
            "long long": "l",
            "long long int": "l",
            "unsigned __int64": "L",
            "unsigned long long": "l",
            "unsigned long long int": "L",
            "void": "v",

            // Types that vary between 32- and 64- bit mode require their own
            // primitive types.  These can't be mapped to the underlying integer
            // primitives since those are all fixed size across platforms.
            "size_t": "Z",
            "SIZE_T": "Z",
            "SSIZE_T": "z",   // signed size_t - not an MSVC type name, but defined (in caps) in the SDK
            "ptrdiff_t": "z",
            "INT_PTR": "p",
            "LONG_PTR": "p",
            "UINT_PTR": "P",
            "ULONG_PTR": "P",
            "DWORD_PTR": "P",

            // Windows SDK type aliases for primitive C types
            "LPARAM": "P",
            "WPARAM": "P",
            "BOOL": "i",
            "BOOLEAN": "c",
            "__int8": "c",
            "__uint8": "C",
            "BYTE": "C",
            "CHAR": "c",
            "CCHAR": "c",
            "INT8": "c",
            "UCHAR": "C",
            "UINT8": "C",
            "__int16": "s",
            "__uint16": "S",
            "__wchar_t": "S",
            "wchar_t": "S",
            "WORD": "S",
            "ATOM": "S",
            "LANGID": "S",
            "INT16": "s",
            "UINT16": "S",
            "SHORT": "s",
            "USHORT": "S",
            "INT": "i",
            "UINT": "I",
            "INT32": "i",
            "UINT32": "I",
            "LONG": "i",
            "LONG32": "i",
            "ULONG": "I",
            "ULONG32": "I",
            "__int32": "i",
            "__uint32": "I",
            "DWORD": "I",
            "DWORD32": "I",
            "COLORREF": "I",
            "LCTYPE": "I",
            "LGRPID": "I",
            "__int64": "l",
            "__uint64": "L",
            "INT64": "l",
            "UINT64": "L",
            "LONGLONG": "l",
            "DWORDLONG": "L",
            "ULONGLONG": "L",
            "DWORD64": "L",
            "FLOAT": "f",
            "DOUBLE": "d",
            "VOID": "v",
            "PVOID": "*v",
            "LPVOID": "*v",
            "HRESULT": "i",

            // Windows SDK types with special handling in our DllImport layer
            "IID": "G",
            "GUID": "G",
            "UUID": "G",
            "CLSID": "G",
            "REFIID": "&%G",
            "REFCLSID": "&%G",
            "HANDLE": "H",
            "HACCEL": "H",
            "HDC": "H",
            "HWND": "h",
            "HGDIOBJ": "H",
            "HCOLORSPACE": "H",
            "HCONV": "H",
            "HDDEDATA": "H",
            "HDESC": "H",
            "HDROP": "H",
            "HDWP": "H",
            "HENHMETAFILE": "H",
            "HGLOBAL": "H",
            "HHOOK": "H",
            "HINSTANCE": "H",
            "HKEY": "H",
            "HKL": "H",
            "HLOCAL": "H",
            "HMENU": "H",
            "HMETAFILE": "H",
            "HMODULE": "H",
            "HMONITOR": "H",
            "HPALETTE": "H",
            "HRGN": "H",
            "HRSRC": "H",
            "HSZ": "H",
            "HWINSTA": "H",
            "HFONT": "I",
            "HPEN": "H",
            "HBRUSH": "H",
            "HBITMAP": "H",
            "HICON": "H",
            "HCURSOR": "H",
            "HFILE": "I",
            "LPSTR": "t",
            "LPCSTR": "%t",
            "LPTSTR": "T",
            "LPCTSTR": "%T",
            "LPWSTR": "T",
            "LPCWSTR": "%T",
            "OLECHAR": "S",
            "LPOLESTR": "T",
            "LPCOLESTR": "%T",
            "VARIANT": "V",
            "LPVARIANT": "*V",
            "VARIANTARG": "V",
            "LPVARIANTARG": "*V",
            "BSTR": "B",
        };

        const typeModifiers = [
            "const", "volatile", "long", "short", "signed", "unsigned",
            "CONST", "LONG", "SHORT", "SIGNED", "UNSIGNED"       // Windows SDK upper case macros for common modifiers
        ];

        // next anonymous ID serial number
        var nextAnon = 1;

        // Top-level type namespace.  Primitive types, typedef names, and
        // predefined SDK types go in this namespace.
        var typeMap = { };
        for (let t in primitiveTypes)
            typeMap[t] = { type: "PrimitiveType", primitive: primitiveTypes[t] };

        // Qualified namespaces for "struct foo", "union foo", "enum foo".  In
        // C, each struct, union, and enum definition lives within a namespace
        // for that object type.
        var structMap = { };
        var unionMap = { };
        var enumMap = { };
        var interfaceMap = { };
        var constantMap = { };
        var namespaceMap = { };

        // type modifiers
        var typeModMap = { };
        for (let t of typeModifiers)
            typeModMap[t] = true;

        const stringEscapes = {
            "a": "\a",
            "b": "\b",
            "f": "\f",
            "n": "\n",
            "r": "\r",
            "t": "\t",
            "v": "\v",
            "\\": "\\",
            "'": "'",
            "\"": "\"",
            "?": "\?"
        };

        let parse = function(src)
        {
            var curr;
            var index = -1;

            var position = {line: 1, column: 1, index: 0};

            // Namespace stack.  Each time we enter a namespace, we add its name to
            // the stack.
            var namespaces = [];

            next();
            return parseRoot();

            function parseRoot()
            {
                var stmts = [];

                while(curr)
                {
                    var pos = getPos();

                    skipBlanks();
                    if (lookahead("namespace"))
                    {
                        let n = readIdentifier();
                        namespaceMap[n] = true;

                        if (lookahead("{"))
                        {
                            // enter the namespace
                            namespaces.push(n);
                        }
                        else if (lookahead(";"))
                        {
                            /* just a namespace declaration, with no contents */
                        }
                        else
                            throwError("invalid 'namespace' syntax: expected { or ;")
                    }
                    else if (lookahead("typedef"))
                    {
                        let defs = readDefinitions(typeMap);
                        endOfStatement();

                        for (let def of defs)
                        {
                            def.type = "TypeDefStatement";
                            typeMap[def.name] = def.defType;
                            stmts.push(def);
                        }
                    }
                    else if (lookahead(";"))
                    {
                        // empty statement
                    }
                    else if (namespaces.length && lookahead("}"))
                    {
                        // end of namespace
                        namespaces.pop();
                    }
                    else
                    {
                        // anything else has to be some kind of declaration
                        var def = readDefinition();

                        if (lookahead("{"))
                            unexpected("Function body definitions are not supported");

                        endOfStatement();
                        stmts.push(def);
                    }
                }

                for (let s of stmts)
                    s.unparse = () => unparse(s);

                return stmts;
            }

            function endOfStatement()
            {
                if (!lookahead(";") && index < src.length)
                    throwError("end of statement expected");
            }

            function parseStruct(stype)
            {
                let [deftype, map, typeCode] = (stype == "struct" ? ["StructType", structMap, "S"] : ["UnionType", unionMap, "U"]);
                let startPos = getPos();
                let s = {type: deftype, member: [], pos: startPos};

                if (lookahead("{"))
                {
                    // No name - it's an anonymous struct.  Give it a unique
                    // internal identifier.
                    s.name = "$" + (nextAnon++);
                    map[s.name] = s;
                }
                else
                {
                    // Read the name, which might be a qualified identifier, and
                    // resolve it within the struct or union namespace.
                    s.name = resolveQualifiedIdentifier(readQualifiedIdentifier(), map);

                    // Check if this is a defining statement
                    if (peekPat(/^[;{]/))
                    {
                        // A definition is always in the current namespace.  Figure
                        // the implied symbol and make sure it matches.
                        let cc = s.name.lastIndexOf("::");
                        let root = cc >= 0 ? s.name.substr(cc + 2) : s.name;
                        let fullname = namespaces.concat(root).join("::");
                        if (fullname != s.name)
                        {
                            // It doesn't match our earlier resolution.  They either
                            // explicitly named a different namespace, or we matched
                            // an existing symbol and implicitly applied the other
                            // namespace.  If it was explicit, it's an error.
                            if (!map[s.name])
                                throwError("struct definition can not override current namespace");

                            s.name = fullname;
                        }
                    }

                    // If there's an open brace, it's a new struct definition;
                    // otherwise it's either a reference to an existing struct
                    // or a forward declaration of a struct type.
                    if (!lookahead("{"))
                    {
                        // if the symbol wasn't already defined, define it
                        if (!map[s.name])
                            map[s.name] = s;
                        
                        // return the name for 
                        return map[s.name];
                    }

                    // check for a previous definition
                    let prv = map[s.name];
                    if (prv)
                    {
                        // we can't redefine it, so the previous definition must be
                        // a forward declaration only
                        if (prv.defined)
                            throwError(stype + " " + s.name + " is already defined");

                        // use the previous definition
                        s = prv;
                    }
                    else
                    {
                        // not previously seen - add it to the map
                        map[s.name] = s;
                    }
                }

                // set the starting pos for the new definition
                s.pos = startPos;

                // parse the struct members
                while (!lookahead("}"))
                {
                    for (let def of readDefinitions())
                        s.member.push(def);
                    consume(";");
                }

                // mark the struct as defined
                s.defined = true;

                // pass the definition to the native layer
                _defineInternalType(
                    typeCode + "." + s.name,
                    "{" + typeCode + " "
                    + s.member.map(m => m.name + ";" + unparse(m)).join(" ")
                    + "}");

                // return the struct definition
                return s;
            }

            function parseEnum()
            {
                let startPos = getPos();
                let e = {type: "EnumDefinition", member: [], pos: startPos};

                if (lookahead("{"))
                {
                    e.name = "$" + (nextAnon++);
                }
                else
                {
                    e.name = readIdentifier();

                    let prv = enumMap[e.name];
                    if (prv)
                        e = prv;
                    else
                        enumMap[e.name] = e;

                    if (!lookahead("{"))
                        return e;

                    e.pos = startPos;
                }

                let index = 0;
                while (identifierIncoming())
                {
                    let eleName = readIdentifier();

                    if (lookahead("="))
                        index = readNumber();

                    e.member.push({ name: eleName, value: index });
                    constantMap[eleName] = index;

                    ++index;

                    if(!lookahead(","))
                        break;
                }

                consume("}");
                enumMap[e.name] = e;
                return e;
            }

            function parseArgumentDefinition(inInterface)
            {
                var args = [];
                if (lookahead(")"))
                    return args;

                for (;;)
                {
                    args.push(readDefinition(false, inInterface));

                    if (lookahead(")"))
                        return args;

                    consume(",");
                }
            }

            function parseInterface(kw)
            {
                // set up the prospective interface descriptor
                ifc = {
                    type: "InterfaceType",
                    vtable: []
                };

                // check the format
                let guid;
                if (kw == "interface")
                {
                    // interface <name> "<guid>" : <base> { ... }
                    ifc.name = resolveQualifiedIdentifier(readQualifiedIdentifier(), interfaceMap);
                    if (altStringIncoming())
                        guid = readAltString();
                }
                else if (kw == "MIDL_INTERFACE")
                {
                    // MIDL_INTERFACE("<guid>") <name> : public <base> { ... }
                    consume("(");
                    guid = readAltString();
                    consume(")");
                    ifc.name = resolveQualifiedIdentifier(readQualifiedIdentifier(), interfaceMap);
                }
                else
                    throwError("invalid interface syntax");

                // Check if we're defining the interface or creating a forward declaration.
                if (peekPat(/^[:{]/))
                {
                    // A definition is always in the current namespace.  Figure
                    // the implied symbol and make sure it matches.
                    let cc = ifc.name.lastIndexOf("::");
                    let root = cc >= 0 ? ifc.name.substr(cc + 2) : ifc.name;
                    let fullname = namespaces.concat(root).join("::");
                    if (fullname != ifc.name)
                    {
                        // It doesn't match our earlier resolution.  They either
                        // explicitly named a different namespace, or we matched
                        // an existing symbol and implicitly applied the other
                        // namespace.  If it was explicit, it's an error.
                        if (!interfaceMap[ifc.name])
                            throwError("struct definition can not override current namespace");

                        ifc.name = fullname;
                    }
                }

                // If a ":" or "{" follows, we have an interface definition.  Otherwise
                // we just have an interface reference (e.g., interface IUnknown*).
                if (lookahead(":"))
                {
                    // skip the optional 'public'
                    lookahead("public");
                    
                    // read and look up the base interface - it must already be defined
                    let baseName = resolveQualifiedIdentifier(readQualifiedIdentifier(), interfaceMap);
                    let baseIfc = interfaceMap[baseName];
                    if (!baseIfc || !baseIfc.defined)
                        throwError("base interface \"" + baseName + "\" is undefined");

                    // add the base interface's vtable to the new interface's vtable
                    ifc.vtable = [].concat(baseIfc.vtable);

                    // we need a "{" at this point
                    consume("{");
                }
                else if (!lookahead("{"))
                {
                    // It's just a reference.  Look up the fully qualified name, and
                    // return the existing interface definition.
                    ifc.name = resolveQualifiedIdentifier(ifc.name, interfaceMap);

                    // if it's not already defined, add a new forward entry for it
                    let prv = interfaceMap[ifc.name];
                    if (!prv)
                    {
                        // add it to the interface namespace
                        prv = interfaceMap[ifc.name] = ifc;

                        // make sure we're not redefining a top-level typedef
                        if (typeMap[ifc.name])
                            throwError("interface " + ifc.name + " is already defined as a different type");

                        // add it to the top-level typedef namespace as well
                        typeMap[ifc.name] = ifc;
                    }

                    // return the map entry
                    return prv;
                }

                // Look for an existing definition
                let prv = interfaceMap[ifc.name];
                if (prv)
                {
                    // make sure it was only forward-declared, not defined
                    if (prv.defined)
                        throwError("interface " + ifc.name + " is already defined");

                    // use the existing entry, copying the new information we've gathered so far
                    Object.assign(prv, ifc);
                    ifc = prv;
                }
                else
                {
                    // new entry - add it to the map
                    interfaceMap[ifc.name] = ifc;

                    // make sure it wasn't already defined as a different type at the top level
                    if (typeMap[ifc.name])
                        throwError("interface " + ifc.name + " is already defined as a different type");

                    // add it as a top-level typedef
                    typeMap[ifc.name] = ifc;
                }

                // validate the GUID format
                if (!/\{?([0-9a-f]{8}-([0-9a-f]{4}-){3}[0-9a-f]{12})\}?/i.test(guid))
                    throwError("invalid GUID format: use standard {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} format (with or without braces)");

                // store the GUID minus any braces, normalizing as upper-case
                ifc.guid = RegExp.$1.toUpperCase();

                // parse function definitions
                while (!lookahead("}"))
                {
                    // skip "public:" specifiers
                    if (lookahead("public"))
                        consume(":");
                    
                    // skip "virtual" keywords
                    lookahead("virtual");

                    // parse a definition
                    let def = readDefinition(false, true);

                    // skip pure virtual "=0" suffix
                    if (lookahead("="))
                        consume("0");

                    // skip the terminating semicolon
                    consume(";");

                    // only function declarations are allowed within an interface
                    if (def.type != "Declaration" || def.defType.type != "FunctionType")
                        throwError("an interface must consist solely of function declarations");

                    // Functions in an interface are always STDMETHODCALLTYPE.  It's an error if
                    // any other calling type is defined.
                    if (def.defType.callingConvention && def.defType.callingConvention != "STDMETHODCALLTYPE")
                        throwError("interface functions must use STDMETHODCALLTYPE calling convention");

                    // if it was defaulted, use STDMETHODCALLTYPE
                    def.defType.callingConvention = "STDMETHODCALLTYPE";

                    // Every COM interface has a hidden 'this' argument in first position.
                    // Insert it as a void* argument.
                    def.defType.arguments.unshift({
                        name: "[this]",
                        defType: {
                            type: "PointerType",
                            target: { type: "PrimitiveType", primitive: "v"}
                        }
                    });

                    // add it to the list
                    ifc.vtable.push(def);
                }

                // mark the interface as defined
                ifc.defined = true;

                // pass the definition to the native layer
                _defineInternalType(
                    "I." + ifc.name,
                    "{I " + ifc.guid + " "
                    + ifc.vtable.map(m => m.name + ";" + unparse(m)).join(" ")
                    + "}");

                // return the interface as a declaration
                return ifc;
            }

            function readDefinitions(qualifiedTypeMap) { return readDefinition(true, false, qualifiedTypeMap); }
            function readDefinition(multi, inInterface, qualifiedTypeMap)
            {
                // If we're in an interface, skip __RPC__xxx IDL annotations
                if (inInterface)
                    lookaheadPat(/^(__RPC__|_In_|_Out_)([a-zA-Z0-9_]|\([^)]*\))+/);
                
                // A C declaration looks like this:
                //
                //   <declaration-specifier> <pointer>[opt] <declarator>
                //
                // The declaration specifier is basically just a type list, so
                // start by parsing that.
                let pos = getPos();
                let declSpecBase = parseDeclSpec();

                // we might have multiple declarators sharing this decl spec,
                // depending on the context, so do the rest in a loop
                let results = [];
                for (;;)
                {
                    // parse pointer qualifiers
                    let declSpec = parsePointer(declSpecBase);

                    // Parse the direct declarator.  This returns a simple parse
                    // tree representing the syntax of the declarator.
                    let tree = parseDirectDeclarator();

                    // Now walk down the tree and apply the decl spec.  This will
                    // yield the "Declaration" node with the proper type set, after
                    // applying the modifiers captured in the parse tree.
                    let decl = setTreeTypes(tree, declSpec);
                    
                    // if we're in multi-definition mode, parse a comma-separated
                    // list of declarators
                    if (multi)
                    {
                        // typedef - allow a list separated by commas
                        results.push(decl);
                        if (!lookahead(","))
                        {
                            // no more list entries - return what we have
                            return results;
                        }
                    }
                    else
                    {
                        // for other types, just define one identifier and return
                        return decl;
                    }
                }

                // Walk a direct declarator parse tree to apply the decl spec.
                // Returns a "Declaration" node with the proper type set.
                function setTreeTypes(tree, declSpec)
                {
                    switch (tree.type)
                    {
                    case "*":
                        declSpec = {
                            type: "PointerType",
                            target: declSpec
                        };
                        return setTreeTypes(tree.target, declSpec);

                    case "&":
                        declSpec = {
                            type: "ReferenceType",
                            target: declSpec
                        };
                        return setTreeTypes(tree.target, declSpec);

                    case "const":
                        (declSpec = Object.assign({ }, declSpec)).isConst = true;
                        return setTreeTypes(tree.target, declSpec);

                    case "volatile":
                        (declSpec = Object.assign({ }, declSpec)).isVolatile = true;
                        return setTreeTypes(tree.target, declSpec);
                        
                    case "name":
                        return {
                            type: "Declaration",
                            name: tree.name,
                            defType: declSpec
                        };

                    case "function":
                        if (declSpec.type == "ArrayType" || declSpec.type == "IncompleteArrayType")
                            throwError("Function returning array is an invalid C type");
                        
                        declSpec = {
                            type: "FunctionType",
                            returnType: declSpec,
                            arguments: tree.arguments,
                            callingConvention: tree.func.callingConvention
                        };
                        return setTreeTypes(tree.func, declSpec);

                    case "array":
                        if (declSpec.type == "FunctionType")
                            throwError("Array of function is an invalid C type");
                        
                        declSpec = {
                            type: tree.length ? "ArrayType" : "IncompleteArrayType",
                            length: tree.length,
                            target: declSpec
                        };
                        return setTreeTypes(tree.target, declSpec);

                    default:
                        throwError("unknown type in direct declarator parse tree: " + tree.type);
                    }
                }

                function parseDeclSpec()
                {
                    let types = [];
                    let composites = [];
                    let all = [];
                    let pos = getPos();
                    let qualifiers = { };

                    for (;;)
                    {
                        let s = peekSym();
                        if (s == "struct" || s == "union")
                        {
                            consume(s);
                            composites.push(parseStruct(s));
                            break;
                        }
                        else if (s == "interface" || s == "MIDL_INTERFACE")
                        {
                            consume(s);
                            composites.push(parseInterface(s));
                            break;
                        }
                        else if (s == "enum")
                        {
                            consume(s);
                            composites.push(parseEnum());
                            break;
                        }
                        else if (s == "const" || s == "CONST" || s == "volatile")
                        {
                            consume(s);
                            qualifiers[s.toLowerCase()] = true;
                        }
                        else if (s)
                        {
                            // It's some other symbol.  Check if it's a type name.  This
                            // could require looking ahead several tokens if it has namespace
                            // qualifiers, so save the current position first so that we can
                            // backtrack if it doesn't turn out to be a type name.
                            let oldPos = savePos();

                            // read a (possibly qualified) identifier
                            let id = resolveQualifiedIdentifier(readQualifiedIdentifier(), typeMap);

                            // try mapping it to a type
                            if (typeMap[id])
                            {
                                // it's a type - add it to the type list
                                types.push(id);
                                s = id;
                            }
                            else
                            {
                                // not a type - pop the position
                                restorePos(oldPos);

                                // we must have reached the end of the type list section
                                break;
                            }
                        }
                        else
                        {
                            // not a symbol - we're done with type names
                            break;
                        }

                        all.push(s);
                    }

                    // the declaration can consist of either ONE composite type
                    // (struct/union/enum) OR any number of type names
                    if (composites.length != 0 && types.length != 0)
                        throwError("invalid combination of type names \"" + all.join(" ") + "\"");

                    let type;
                    if (composites.length)
                    {
                        // exactly one composite type is allowed
                        if (composites.length > 1)
                            throwError("invalid declaration: multiple composite types specified");
                        
                        // use the parsed composite type
                        type = composites[0];
                    }
                    else if (types.length)
                    {
                        // type by name - make sure the combined type name is valid
                        let typeName = types.join(" ");
                        if (!typeMap[typeName])
                            throwError("invalid type name \"" + typeName + "\"");

                        // build the basic type reference
                        type = {
                            type: "Type",
                            pos: pos,
                            name: typeName
                        };
                    }
                    else
                    {
                        unexpected("type specifier");
                    }

                    // add the qualifiers
                    type.isConst = !!qualifiers["const"];
                    type.isVolatile = !!qualifiers["volatile"];

                    // return the result
                    return type;
                }

                function parsePointer(type)
                {
                    for (;;)
                    {
                        // check for a pointer/reference specifier
                        let pos = getPos();
                        let reftype;
                        if (lookahead("*"))
                            reftype = "PointerType";
                        else if (lookahead("&"))
                            reftype = "ReferenceType";
                        else
                            break;

                        // got it - wrap the qualified type in a pointer to that type
                        type = {
                            type: reftype,
                            target: type,
                            pos: pos
                        };

                        // check for type qualifiers
                        for (;;)
                        {
                            if (lookahead("const") || lookahead("CONST"))
                                type.isConst = true;
                            else if (lookahead("volatile"))
                                type.isVolatile = true;
                            else
                                break;
                        }
                    }

                    return type;
                }

                function parsePointerDirectDeclarator()
                {
                    let reftype = lookahead("*") || lookahead("&");
                    if (reftype)
                        return { type: reftype, target: parsePointerDirectDeclarator() };

                    let qual = lookahead("const") || lookahead("CONST") || lookahead("volatile");
                    if (qual)
                        return { type: qual.toLowerCase(), target: parsePointerDirectDeclarator() };

                    return parseDirectDeclarator();
                }

                function parseDirectDeclarator()
                {
                    let ret;
                    if (lookahead("("))
                    {
                        // parse a nested declarator:
                        //   '(' calling-convention[opt]  pointer[opt]  direct-declarator ')'
                        //

                        // parse the calling convention
                        let cc = parseCallingConvention();

                        // parse the nested pointers and direct declarator
                        ret = parsePointerDirectDeclarator();

                        // apply the calling convention
                        if (cc)
                            ret.callingConvention = cc;

                        // skip the closing paren
                        consume(")");
                    }
                    else
                    {
                        ret = {
                            type: "name",
                            callingConvention: parseCallingConvention()
                        };

                        if (qualifiedTypeMap)
                            ret.name = qualifiedIdentifierIncoming() && resolveQualifiedIdentifier(readQualifiedIdentifier(), qualifiedTypeMap);
                        else
                            ret.name = identifierIncoming() && readIdentifier();
                    }

                    // check for function and array postfixes
                    for (;;)
                    {
                        if (lookahead("("))
                        {
                            // parse arguments
                            let args = parseArgumentDefinition(inInterface);

                            // exactly one 'void' argument actually means there are zero arguments
                            if (args.length == 1 && args[0].defType.name == "void")
                                args = [];

                            // validate other arguments
                            for (let a of args)
                            {
                                if (a.defType.name == "void")
                                    throwError("'void' function parameters are invalid");
                                if (a.defType.name == "FunctionType")
                                    throwError("functions can't be passed as parameters by value (did you mean to use a pointer?)");
                            }

                            ret = {
                                type: "function",
                                func: ret,
                                arguments: args
                            };
                        }
                        else if (lookahead("["))
                        {
                            var length;
                            if (numberIncoming())
                            {
                                length = readNumber();
                                if (length <= 0)
                                    throwError("array dimension must be greater than zero")

                                consume("]");
                            }
                            else if (!lookahead("]"))
                                unexpected("numeric constant or empty array dimension");

                            ret = {
                                type: "array",
                                length: length,
                                target: ret
                            };
                        }
                        else
                        {
                            // no (more) postfixes
                            break;
                        }
                    }

                    return ret;
                }

                function parseCallingConvention()
                {
                    let s = peekSym();
                    if (callingConvMap[s])
                    {
                        consume(s);
                        return s;
                    }
                    return undefined;
                }
            }

            function stringIncoming()
            {
                return curr && curr == "\"";
            }
            function readString(keepBlanks)
            {
                var val = [];
                next(true, true);
                while(curr && curr != "\"")
                {
                    if(curr == "\\")
                    {
                        next(true, true);
                        val.push(readEscapeSequence());
                    }
                    else
                    {
                        val.push(curr);
                        next(true, true);
                    }
                }

                if(!lookahead("\"", keepBlanks))
                    unexpected("\"");

                return val.join("");
            }

            // "alternative" string - " or ' quoting
            function altStringIncoming()
            {
                return curr && (curr == "\"" || curr == "'");
            }
            function readAltString(keepBlanks)
            {
                let val = [];
                let qu = curr;
                next(true, true);
                while (curr && curr != qu)
                {
                    if (curr == "\\")
                    {
                        next(true, true);
                        val.push(readEscapeSequence());
                    }
                    else
                    {
                        val.push(curr);
                        next(true, true);
                    }
                }

                if (!lookahead(qu, keepBlanks))
                    unexpected(qu);

                return val.join("");
            }

            function readEscapeSequence()
            {
                if(curr == "x")
                {
                    next(true, true);
                    var val = 0;
                    while(/[0-9A-Fa-f]/.test(curr))
                    {
                        val = (val << 4) + parseInt(curr, 16);
                        next(true, true);
                    }

                    return String.fromCharCode(val);
                }
                else if(/[0-7]/.test(curr))
                {
                    var val = 0;
                    while(/[0-7]/.test(curr))
                    {
                        val = (val << 3) + parseInt(curr, 16);
                        next(true, true);
                    }

                    return String.fromCharCode(val);
                }
                else if(stringEscapes[curr])
                {
                    var escape = stringEscapes[curr];
                    next(true, true);
                    return escape;
                }

                unexpected("escape sequence");
            }

            function numberIncoming()
            {
                return curr && /[0-9]/.test(curr);
            }
            function readNumber(keepBlanks)
            {
                var val = read(/[0-9\.]/, "Number", /[0-9]/, keepBlanks);
                return parseFloat(val);
            }

            function qualifiedIdentifierIncoming()
            {
                return curr && /::|[A-Za-z_]/.test(curr);
            }

            function resolveQualifiedIdentifier(id, map)
            {
                // if it starts with an explicit global qualifier, strip the
                // "::" prefixand return the result
                if (id.startsWith("::"))
                    return id.substr(2);

                // It's not explicitly global, so it could be implicitly qualified
                // by the current namespace or any parent namespace.  Search from
                // the current namespace outwards for an existing identifier.
                for (let cur = [].concat(namespaces) ; ; cur.pop())
                {
                    let qual = cur.concat(id).join("::");
                    if (map[qual])
                        return qual;

                    // If we're out of parents, this is a new name, which means
                    // that it's implicitly in the current namespace.
                    if (cur.length == 0)
                        return namespaces.concat(id).join("::");
                }
            }

            function readQualifiedIdentifier()
            {
                let lst = [];

                if (lookahead("::"))
                    lst.push("::");

                while (identifierIncoming())
                {
                    lst.push(readIdentifier());
                    if (lookahead("::"))
                        lst.push("::");
                    else
                        break;
                }

                if (lst.length == 0 || lst[lst.length-1] == "::")
                    unexpected("identifier");

                return lst.join("");
            }

            function identifierIncoming()
            {
                return curr && /[A-Za-z_]/.test(curr);
            }
            function readIdentifier(keepBlanks)
            {
                return read(/[A-Za-z0-9_]/, "Identifier", /[A-Za-z_]/, keepBlanks);
            }

            function read(reg, expected, startreg, keepBlanks)
            {
                startreg = startreg || reg;

                if(!startreg.test(curr))
                    unexpected(expected);

                var val = [curr];
                next(true);

                while(curr && reg.test(curr))
                {
                    val.push(curr);
                    next(true);
                }

                if(!keepBlanks)
                    skipBlanks();

                return val.join("");
            }

            function getPos()
            {
                return {
                    line: position.line,
                    column: position.column,
                    index: index
                };
            }

            function unexpected(expected)
            {
                var pos = getPos();
                var found = identifierIncoming() ? readIdentifier() : JSON.stringify(curr || "EOF");
                throwError("expecting " + JSON.stringify(expected) + ", found " + found);
            }
            function throwError(msg)
            {
                var pos = getPos();
                throw new Error([
                    "line ",
                    pos.line,
                    ", col ",
                    pos.column,
                    ", index ",
                    pos.index,
                    ": ",
                    msg
                ].join(""));
            }

            function peekSym()
            {
                if (/^(::|[_a-zA-Z][_a-zA-Z0-9]*)/.test(src.substr(index)))
                    return RegExp.$1;
                else
                    return false;
            }

            function peekPat(pat)
            {
                if (pat.test(src.substr(index)))
                    return RegExp.lastMatch;
                else
                    return false;
            }

            function lookaheadPat(pat)
            {
                if (pat.test(src.substr(index)))
                {
                    var s = RegExp.lastMatch;
                    consume(s);
                    return s;
                }
                return false;
            }

            function lookahead(str, keepBlanks)
            {
                var _index = index;
                for(var i = 0; i < str.length; i++)
                {
                    if(curr != str[i])
                    {
                        index = _index;
                        curr = src[index];
                        return false;
                    }
                    next(true);
                }

                if(/^[_a-zA-Z][_a-zA-Z0-9]*$/.test(str) && /[_a-zA-Z]/.test(curr))
                {
                    index = _index;
                    curr = src[index];
                    return false;
                }

                if(!keepBlanks)
                    skipBlanks();

                return str;
            }

            function consume(str)
            {
                for(var i = 0; i < str.length; i++)
                {
                    if(curr != str[i])
                        unexpected(str);
                    next();
                }
            }

            function skipBlanks()
            {
                if(/[\s\n]/.test(curr))
                    next();
            }

            function savePos()
            {
                let p = {
                    curr: curr,
                    index: index,
                    position: { }
                };
                Object.assign(p.position, position);
                return p;
            }

            function restorePos(p)
            {
                curr = p.curr;
                index = p.index;
                Object.assign(position, p.position);
            }

            function next(includeSpaces, includeComments)
            {
                includeSpaces = includeSpaces || false;

                position.column++;
                position.index++;
                if(curr == "\n")
                {
                    position.line++;
                    position.column = 1;
                }
                index++;
                curr = src[index];

                do
                {
                    var skipped = skipComments() || skipSpaces();
                } while(skipped);

                function skipSpaces()
                {
                    if(includeSpaces)
                        return;

                    if(/[\s\n]/.test(curr))
                    {
                        while(curr && /[\s\n]/.test(curr))
                        {
                            position.column++;
                            position.index++;
                            if(curr == "\n")
                            {
                                position.line++;
                                position.column = 1;
                            }
                            index++;
                            curr = src[index];
                        }
                        return true;
                    }
                }

                function skipComments()
                {
                    if(includeComments)
                        return;
                    if(curr && curr == "/" && src[index + 1] == "/")
                    {
                        while(curr != "\n")
                        {
                            index++;
                            curr = src[index];
                        }
                        return true;
                    }
                    if(curr && curr == "/" && src[index + 1] == "*")
                    {
                        while(curr != "*" || src[index + 1] != "/")
                        {
                            position.column++;
                            position.index++;
                            if(curr == "\n")
                            {
                                position.line++;
                                position.column = 1;
                            }
                            index++;
                            curr = src[index];
                        }
                        index += 2;
                        curr = src[index];
                        return true;
                    }
                }
            }
        };

        // ---------------------------------------------------------------------------
        //
        // Unparsing section.  These functions convert a parsed declaration into the
        // special notation used in the PinballY DllImport system.  This is a compact
        // type notation that's easy and fast to parse mechanically; it's used in the
        // DLL interface C++ code to marshall arguments and return values between
        // Javascript and native code.
        //

        function unparse(stmt)
        {
            function unparseType(t)
            {
                switch (t.type)
                {
                case "Type":
                    return (t.isConst ? "%" : "")
                        + unparseType(typeMap[t.name] || { type: "UnknownType", name: t.name });

                case "PrimitiveType":
                    return (t.isConst ? "%" : "") + (t.primitive || unparseType({ type: "UnknownType", name: t.name }));

                case "UnknownType":
                    throwError("Unknown type in unparse: " + t.name);

                case "PointerType":
                    return (t.isConst ? "%*" : "*") + unparseType(t.target, true);

                case "ReferenceType":
                    return (t.isConst ? "%&" : "&") + unparseType(t.target, true);

                case "ArrayType":
                    return "[" + t.length + "]" + unparseType(t.target, false);

                case "IncompleteArrayType":
                    return "[]" + unparseType(t.target, false);

                case "FunctionType":
                    return "(" + unparseFunc(t, false) + ")";

                case "InterfaceType":
                    return unparseInterface(t);

                case "StructType":
                    return unparseStruct(t);

                case "UnionType":
                    return unparseUnion(t);

                case "EnumDefinition":
                    return unparseEnum(t);

                case "Literal":
                    return t.value;

                default:
                    return "<unknown-type:" + t.type + ">";
                }
            }

            function unparseFunc(t)
            {
                s = (callingConvMap[t.callingConvention] || "C")
                    + unparseType(t.returnType);
                for (let a of t.arguments)
                    s += " " + unparseType(a.defType, false);
                return s;
            }

            function unparseStruct(t)
            {
                return "@S." + t.name;
            }

            function unparseUnion(t)
            {
                return "@U." + t.name;
            }

            function unparseInterface(t)
            {
                return "@I." + t.name;
            }

            function unparseEnum(t)
            {
                return "E" + t.name;
            }

            let s = "";
            switch (stmt.type)
            {
            case "Declaration":
                return unparseType(stmt.defType);

            case "TypeDefStatement":
                return unparseType(stmt.defType);

            default:
                return unparseType(stmt);
            }
        }

        let uuidof = function(type)
        {
            // check for a global type alias
            let alias = typeMap[type];
            if (alias)
            {
                if (alias.type == "InterfaceType")
                    return alias.guid;

                throw new Error("\"" + type + "\" is not an interface");
            }
            
            // check if it's an "interface <foo>" type
            if (/^\s*interface\s+([a-z_][a-z0-9_]*\s*$)/i.test(type))
                type = RegExp.$1;

            // check if it's the name of an interface
            let i = interfaceMap[type];
            if (i)
                return i.guid;

            // unknown type
            throw new Error("unknown interface");
        }

        // return our collection of public entrypoints
        return {
            parse: parse,
            uuidof: uuidof,
            enums: enumMap,
            structs: structMap,
            unions: unionMap,
            interfaces: interfaceMap,
            constants: constantMap
        };
    }

    // CParser constructor
    return function()
    {
        // create a unique parser instance, and assign its methods as
        // our methods
        Object.assign(this, createParser());
    };

})();
