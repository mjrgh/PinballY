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
//   unparses to (Si *%p f).  Note that argument names aren't
//
// - A pointer to a func is written in the obvious way, *(...)
//

var CParser = (function()
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
            "WINAPI": "S",
            "CALLBACK": "S"
        };

        const primitiveTypes = {

            // C primitive types, with mappings to DllImport types.  Note that
            // some of the types have abstracted meanings that don't *portably*
            // map to the bit sizes that we map to here.  These mappings are
            // strictly for the type mappings used by the Microsoft compiler,
            // so we lost portable abstraction in the DllImport conversion.
            // The MS compiler uses identical bit sizes for x86 and x64 mode
            // for *most* of the basic C types: char, short, int, long, int64,
            // float, double, and long double are all the same in both modes.

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

            // Windows SDK type aliases for primitive C types
            "BOOL": "i",
            "BOOLEAN": "c",
            "__int8": "c",
            "__uint8": "C",
            "BYTE": "C",
            "CHAR": "c",
            "CCHAR": "c",
            "INT8": "c",
            "UINT8": "C",
            "__int16": "s",
            "__uint16": "S",
            "__wchar_t": "S",
            "wchar_t": "S",
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
            "DWORD64": "L",
            "FLOAT": "f",
            "DOUBLE": "d",
            "VOID": "v",
            "PVOID": "*v",
            "LPVOID": "*v",

            // Windows SDK types with special handling in our DllImport layer
            "HRESULT": "i",
            "HANDLE": "H",
            "HACCEL": "H",
            "HDC": "H",
            "HWND": "H",
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
            "INT_PTR": "P",
            "DWORD_PTR": "P",
            "LPARAM": "P",
            "WPARAM": "P",
            "LONG_PTR": "P"
        };

        const typeModifiers = [
            "const", "volatile", "long", "short", "signed", "unsigned",
            "CONST", "LONG", "SHORT", "SIGNED", "UNSIGNED"       // Windows SDK upper case macros for common modifiers
        ];


        // Top-level type namespace.  Primitive types and typedef names go in
        // this namespace.
        var typeMap = { };
        for (let t in primitiveTypes)
            typeMap[t] = { type: "PrimitiveType", primitive: primitiveTypes[t] };

        // Qualified namespaces for "struct foo", "union foo", "enum foo".  In
        // C, each struct, union, and enum definition lives within a namespace
        // for that object type.
        var structMap = { };
        var unionMap = { };
        var enumMap = { };

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

            var position = {line: 1};

            next();
            return parseRoot();

            function parseRoot()
            {
                var stmts = [];

                while(curr)
                {
                    var pos = getPos();

                    skipBlanks();
                    if (lookahead("typedef"))
                    {
                        let defs = readDefinitions();
                        endOfStatement();

                        for (let def of defs)
                        {
                            def.type = "TypeDefStatement";
                            typeMap[def.name] = def.defType;
                            stmts.push(def);
                        }
                    }
                    else if (definitionIncoming())
                    {
                        var def = readDefinition();

                        if (lookahead("{"))
                        {
                            unexpected("Function body definitions are not supported");
                        }

                        endOfStatement();
                        stmts.push(def);
                    }
                    else
                    {
                        unexpected("struct, union, enum, typdef, extern, FunctionDeclaration or VariableDeclaration");
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
                let [deftype, map] = (stype == "struct" ? ["StructType", structMap] : ["UnionType", unionMap]);
                let s = {type: deftype, member: [], pos: getPos()};

                if (lookahead("{"))
                {
                    s.name = "<anonymous>";
                }
                else
                {
                    s.name = readIdentifier();
                    if (!lookahead("{"))
                    {
                        let prv = map[s.name];
                        if (!prv)
                            prv = { type: deftype, member: [], pos: getPos() };
                        return prv;
                    }
                }

                while (definitionIncoming())
                {
                    for (let def of readDefinitions())
                        s.member.push(def);
                    consume(";");
                }

                consume("}");
                map[s.name] = s;
                return s;
            }

            function parseEnum()
            {
                var e = {type: "EnumDefinition", member: [], pos: getPos()};

                if (lookahead("{"))
                {
                    e.name = "<anonymous>";
                }
                else
                {
                    e.name = readIdentifier();
                    consume("{");
                }

                while(identifierIncoming())
                {
                    e.member.push(readIdentifier());

                    if(!lookahead(","))
                        break;
                }

                consume("}");
                enumMap[e.name] = e;
                return e;
            }

            function parseArgumentDefinition()
            {
                var args = [];
                while (definitionIncoming())
                {
                    args.push(readDefinition());

                    if (lookahead(")"))
                        return args;
                    consume(",");
                }
                consume(")");
                return args;
            }

            function definitionIncoming()
            {
                let s = peekSym();
                return typeModMap[s] || typeMap[s] || s == "struct" || s == "union" || s == "enum";
            }

            function readDefinitions() { return readDefinition(true); }
            function readDefinition(multi)
            {
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

                    // parse the direct declarator
                    let decl = parseDirectDeclarator(declSpec);
                    
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
                        }
                        else if (s == "enum")
                        {
                            consume(s);
                            composites.push(parseEnum());
                        }
                        else if (s == "const" || s == "CONST" || s == "volatile")
                        {
                            consume(s);
                            qualifiers[s.toLowerCase()] = true;
                        }
                        else if (typeMap[s])
                        {
                            consume(s);
                            types.push(s);
                        }
                        else
                            break;

                        all.push(s);
                    }

                    // the declaration can consist of either ONE composite type
                    // (struct/union/enum) OR any number of type names
                    if (composites.length != 0 && types.length != 0)
                        throwError("invalid combination of type names \"" + all.join(" ") + "\"");

                    let type;
                    if (composites.length)
                    {
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

                function parsePointerDirectDeclarator(declSpec)
                {
                    for (;;)
                    {
                        let reftype;
                        if (lookahead("*"))
                            reftype = "PointerType";
                        else if (lookahead("&"))
                            reftype = "ReferenceType";
                        
                        if (reftype)
                        {
                            declSpec = {
                                type: reftype,
                                target: declSpec
                            };
                        }
                        
                        let qual;
                        if (lookahead("const") || lookahead("CONST"))
                            qual = "isConst";
                        else if (lookahead("volatile"))
                            qual = "isVolatile";

                        if (qual)
                            declSpec[qual] = true;

                        if (!reftype && !qual)
                            break;
                    }

                    return parseDirectDeclarator(declSpec);
                }

                function parseDirectDeclarator(declSpec)
                {
                    let outerType = { };
                    let decl;

                    if (lookahead("("))
                    {
                        // parse a nested declarator:
                        //   '(' calling-convention[opt]  pointer[opt]  direct-declarator ')'
                        //

                        // parse the calling convention
                        declSpec.callingConvention = parseCallingConvention();

                        // The type of the nested declarator will be (possibly pointer to)
                        // the enclosing type expression, so create a placeholder object
                        // that will ultimately hold our final calculated type, and use
                        // that as the type for the recursive descent.
                        decl = parsePointerDirectDeclarator({
                            type: "ProxyType",
                            target: outerType
                        });

                        // skip the closing paren
                        consume(")");
                    }
                    else
                    {
                        declSpec.callingConvention = parseCallingConvention();
                        if (identifierIncoming())
                        {
                            decl = {
                                type: "Declaration",
                                pos: getPos(),
                                name: readIdentifier(),
                                defType: outerType
                            };
                        }
                        else
                        {
                            // Anonymous type declaration.  This can be used for casts and
                            // parameter names.
                            decl = {
                                type: "Anonymous",
                                pos: getPos(),
                                defType: outerType
                            };
                        }
                    }

                    // check for function and array postfixes
                    for (;;)
                    {
                        if (lookahead("("))
                        {
                            declSpec = {
                                type: "FunctionType",
                                callingConvention: declSpec.callingConvention,
                                returnType: declSpec,
                                arguments: parseArgumentDefinition()
                            };

                            // normalize the arguments for type validation purposes
                            declSpec = normalizeType(declSpec);

                            // function returning array or function is invalid
                            if (declSpec.returnType.type == "ArrayType" || declSpec.returnType.type == "IncompleteArrayType")
                                throwError("a function returning an array type is invalid");
                            if (declSpec.returnType.type == "FunctionType")
                                throwError("a function can't return a function as its result (did you mean to return a pointer to a function?)");

                            // exactly one 'void' argument actually means there are zero arguments
                            if (declSpec.arguments.length == 1 && declSpec.arguments[0].defType.name == "void")
                                declSpec.arguments = [];

                            // validate other arguments
                            for (let a of declSpec.arguments)
                            {
                                if (a.defType.name == "void")
                                    throwError("'void' function parameters are invalid");
                                if (a.defType.type == "FunctionType")
                                    throwError("functions can't be used as parameters (did you mean 'pointer to function'?)");
                            }
                        }
                        else if (lookahead("["))
                        {
                            if (lookahead("]"))
                            {
                                declSpec = {
                                    type: "IncompleteArrayType",
                                    target: declSpec
                                };
                            }
                            else if (numberIncoming())
                            {
                                declSpec = {
                                    type: "ArrayType",
                                    target: declSpec,
                                    length: readNumber()
                                };
                                consume("]");
                            }
                            else
                                throwError("numeric constant required for array size specification");

                            // array of function is invalid
                            declSpec = normalizeType(declSpec);
                            if (declSpec.target.type == "FunctionType")
                                throwError("array of functions is invalid");
                        }
                        else
                        {
                            // no (more) postfixes
                            break;
                        }
                    }

                    // set the outer type to the final declSpec
                    Object.assign(outerType, declSpec);

                    // return the declarator
                    return decl;
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
                    column: index
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
                    index,
                    ": ",
                    msg
                ].join(""));
            }

            function peekSym()
            {
                if (/^([_a-zA-Z][_a-zA-Z0-9]*)/.test(src.substr(index)))
                    return RegExp.$1;
                else
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
                return true;
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

            function next(includeSpaces, includeComments)
            {
                includeSpaces = includeSpaces || false;

                if(curr == "\n")
                    position.line++;
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
                            if(curr == "\n")
                                position.line++;
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
                            if(curr == "\n")
                                position.line++;
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

        // Normalize a type by removing any proxy wrappers
        function normalizeType(t)
        {
            // If this node has already been normalied, terminate the recursion.
            // We can visit subtrees multiple times in the course of assembling
            // higher level structures out of components, such as functions with
            // struct arguments.
            if (t.normalized)
                return t;

            // mark it as normalized to skip future traversals
            t.normalized = true;

            // if it has a target, normalize the target
            if (t.target)
                t.target = normalizeType(t.target);

            // a few types require special handling
            switch (t.type)
            {
            case "FunctionType":
                // normalize the return type
                t.returnType = normalizeType(t.returnType);
                
                // normalize arguments
                for (let i = 0; i < t.arguments.length; ++i)
                    t.arguments[i].defType = normalizeType(t.arguments[i].defType);
                break;

            case "StructType":
            case "UnionType":
                for (let i = 0; i < t.member.length; ++i)
                    t.member[i].defType = normalizeType(t.member[i].defType);
                break;

            case "ProxyType":
                // normalize out proxies entirely
                return t.target;

            case "Type":
                {
                    let type = typeMap[t.name];
                    if (type && !type.primitive)
                        return type;
                }
                break;
            }

            // return the normalized type
            return t;
        }
        

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
                    return (t.isConst ? "%*" : "*") + unparseType(t.target);

                case "ReferenceType":
                    return (t.isConst ? "%&" : "&") + unparseType(t.target);

                case "ArrayType":
                    return "[" + t.length + "]" + unparseType(t.target);

                case "IncompleteArrayType":
                    return "*" + unparseType(t.target);

                case "FunctionType":
                    return "(" + unparseFunc(t) + ")";

                case "StructType":
                    return unparseStruct(t);

                case "UnionType":
                    return unparseUnion(t);

                case "EnumDefinition":
                    return unparseEnum(t);

                case "Literal":
                    return t.value;

                case "ProxyType":
                    return unparseType(t.target);

                default:
                    return "<unknown-type:" + t.type + ">";
                }
            }

            function unparseFunc(t)
            {
                s = (callingConvMap[t.callingConvention] || "C")
                    + unparseType(t.returnType);
                for (let a of t.arguments)
                    s += " " + unparseType(a.defType);
                return s;
            }

            function unparseStruct(t)
            {
                let s = [];
                for (let m of t.member)
                    s.push(m.name + ":" + unparseType(m.defType));
                return "{S " + s.join(" ") + "}";
            }

            function unparseUnion(t)
            {
                let s = [];
                for (let m of t.member)
                    s.push(m.name + ":" + unparseType(m.defType));
                return "{U " + s.join(" ") + "}";
            }

            function unparseEnum(t)
            {
                let s = [];
                return "E" + t.name;
            }

            let s = "";
            switch (stmt.type)
            {
            case "Declaration":
                return unparseType(stmt.defType);
                break;

            case "FunctionDeclaration":
                return unparseFunc(stmt);
                break;

            case "TypeDefStatement":
                return unparseType(stmt.defType);
                break;

            case "StructType":
                return unparseStruct(stmt);
                break;

            case "UnionType":
                return unparseUnion(stmt);
                break;

            case "EnumDefinition":
                return unparseEnum(stmt);
                break;

            default:
                throw new Error("CParser.unparse: unknown node type " + stmt.type);
                break;
            }
        }

        // our only exposed entrypoint is the parse() function
        return parse;
    }

    // CParser constructor
    return function()
    {
        this.parse = createParser();
    }

})();
