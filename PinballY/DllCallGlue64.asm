; This file is part of PinballY
; Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
;
; 64-bit glue functions for Javascript DLL Import 


_TEXT SEGMENT


; Glue function for calling DLL functions from 64-bit builds.  The Microsoft 
; 64-bit C++ compiler doesn't allow inline assembler, so we have to write this
; glue explicitly as an external .asm module.
;
; This implements an "inverse varargs" function.  A regular varargs function
; is one where the callee doesn't know in advance how many arguments it will
; receive.  This is the opposite: this allows C++ code that has a vector of
; arguments whose size is unknown at compile time to pass those arguments to
; a callee that's expecting a regular fixed argument list.  C++ doesn't have
; any native provision for such a call, even with variadic templates.

; Variadic templates do let you dynamically construct an argument vector
; that you use to ultimately call a statically typed function, but we need
; to take it one step further and call a function whose type is never known
; to the compiler.
;
; We need this for our Javascript DLL import feature, since that allows a
; Javascript caller to invoke functions bound at run-time with argument 
; lists constructed dynamically at run-time.
;
; Here's how this works:
;
;  - The caller prepares arguments as an array of INT64 values
;  - The caller invokes us, passing a pointer to the callee, along
;    with the argument array and number of arguments
;  - We repackage the argument array into the register + stack slots
;    dictated by the x64 calling convention
;  - We invoke the target function
;  - We remove the stack arguments we created
;  - We return to the caller, with the result register left as we
;    found it on return from the callee
;
; In 64-bit mode, Windows uses a single calling convention (unlike in x86
; mode, where Microsoft defines at least dive different calling conventions:
; __cdecl, __stdcall, __fastcall, __thiscall, __vectorcall).  The universal
; x64 convention is similar to x86 __fastcall.   The first four arguments 
; are always passed in registers (RCX, RDX, R8, R9); arguments beyond the
; first four are passed on the stack.  In addition, the caller allocates
; "shadow" slots on the stack where the first four arguments *would* have 
; gone if they'd been passed on the stack.  The caller allocates but does
; not initialize the shadow slots, which are purely for the callee's use.
;
; One additional detail: the first four arguments are assigned to registers
; XMM0, XMM1, XMM2, and XMM3 when they're passed in as float, double, or
; __m128 types.  Each argument is assigned to a register individually, so
; we can have a mix of Rx and XMMn registers as arguments.  The order is
; always the same, though, and each slot always has the same choice of two
; registers:
;
;   first argument = RCX or XMM0
;   second         = RDX or XMM1
;   third          = R8  or XMM2
;   fourth         = R9  or XMM3
;
; Fortunately, all eight of these registers are volatile, meaning we're
; free to overwrite them.  And a given argument will always be in one or the
; other of its pair.  So for our purposes, we don't need to know the type 
; of each callee's argument; we can simply copy each argument into *both* 
; of the paired registers for that argument position.  The caller will pull
; the value from the one it expects to find it in and ignore the contents
; we left in the other register in the pair (since it's a meaningless
; volatile register, from the callee's perspective), so the fact that we
; wrote a value there has no effect other than costing a couple of extra 
; cycles.  (Which is less than it would cost to decide on which register 
; we're really supposed to use based on the type signature.)
;
; The final wrinkle to the calling convention is the return register.
; For float/double/__m128 returns, the result is in XMM0; for all other
; types, the result is in RAX.  Our caller is C++, with no assembly, so
; the only way to get the caller to read the correct register on return
; is to have it call a function declared statically to return a type
; that corresponds to one or the other register.  Since there are two
; possible return registers, we need two return types.
;
; So, we create two aliases for this function, with these C++ prototypes:
;
; UINT64 DllCallGlue64_RAX(FARPROC funcPtr, const void *pArgs, size_t nArgBytes)
; __m128 DllCallGlue64_XMM0(FARPROC funcPtr, const void *pArgs, size_t nArgBytes)
;
; These two aliases just point to the identical assembler entrypoint, so
; they're not actually different functions, but C++ *thinks* they are to
; the extent that it looks in RAX for the UINT64 return version, and looks
; in XMM0 for the __m128 return version.  The caller can use the Javascript
; type signature information to dynamically invoke one or the other function
; on each call based on the expected return type.  This will make the C++ 
; compiler generate code to look in the right register for the return value
; from each call.  Note that *our* code here doesn't even touch the return
; registers - it just passes both RAX and XMM0 back to our caller with the
; value left there by our callee.  So we don't have to know anything here
; about which return type is expected.
;
PUBLIC DllCallGlue64_RAX
PUBLIC DllCallGlue64_XMM0
DllCallGlue64_XMM0:
DllCallGlue64_RAX PROC

    ; Alias for dll_call_glue64 with the result returned in XMM0.  
    ; The C++ caller invokes this alias when it expects a float,
    ; double, or __m128 result.  The x64 calling convention returns
    ; those types in XMM0.  We just leave the result value of the
    ; callee in whichever register the callee left it in, so it's
    ; up to our caller to retrieve the return value from the
    ; correct register.  This alias lets us coerce the compiler
    ; into generating the right code for XMM0 returns.

	; establish our stack frame 
	push   rbp
    mov    rbp, rsp

    ; save registers
    push   rsi
    push   rdi

	; ensure 16-byte alignment
    and    rsp, 0FFFFFFFFFFFFFFF0h

	; save the function pointer in RAX
    mov    rax, rcx

    ; Allocate stack space for the arguments.  The caller builds its
    ; version of the stack frame with ALL arguments in stack slots,
    ; so the "shadow" slots are effectively already included.  Allocate
    ; space in our stack for a copy of the arguments to pass to our
    ; callee.
    sub    rsp, r8

    ; copy the arguments
    mov    rdi, rsp
    mov    rsi, rdx
    mov    rcx, r8
    shr    rcx, 3      ; divide byte by 8 to get qwords
    rep movsq

    ; Set up the first four parameter registers.  Since we don't know
    ; whether the caller wants int or float parameters, populate both
    ; sets of registers.
    mov   rcx, [rsp + 0]
    movd  xmm0, rcx
    mov   rdx, [rsp + 8]
    movd  xmm1, rdx
    mov   r8,  [rsp + 16]
    movd  xmm2, r8
    mov   r9,  [rsp + 24]
    movd  xmm3, r9

    ; call the target
    call  rax

    ; restore registers
    lea   rsp, [rbp - 16]
    pop   rdi
    pop   rsi
    pop   rbp
    ret

DllCallGlue64_RAX ENDP


; Javascript callback glue.  This function is invoked when a native routine
; calls a Javascript function as a callback.  For each Javascript callback
; that the caller passes to native code, the DLL importer generates a little
; function stub, as dynamically generated machine code, that stores a pointer
; to a context object in RAX and then jumps here.  The generated code in 64-
; bit mode also places the register arguments into the shadow slots on the
; stack, so we have a simple argument array on the stack and thus don't need
; to worry about incoming register arguments here.
EXTERN JavascriptEngine_CallCallback : PROC

PUBLIC DllImportCallbackGlue
DllImportCallbackGlue PROC

    ; set up a local stack frame
    push  rbp
    mov   rbp, rsp

    ; set up params for the callee:  JavascriptEngine_CallCallback(context_object_pointer, original_argv_base)
    mov   rcx, rax             ; context object is passed to us in RAX
    lea   rdx, [rbp + 8]       ; original argv base

	; 16-byte-align the stack
    and   rsp, 0FFFFFFFFFFFFFFF0h

    ; push space for the callee's shadow registers
    sub   rsp, 32

    ; call the C++ entrypoint
    call  JavascriptEngine_CallCallback

    ; The C++ code returns all results in RAX.  A caller expecting a float/double
    ; result will be looking in XMM0, per x64 calling conventions.  Simply copy
    ; the RAX result to XMM0 in case the caller is looking there.
    movq  rax, xmm0

    ; restore the stack and return
    mov   rsp, rbp
    pop   rbp
    ret

DllImportCallbackGlue ENDP

_TEXT ENDS

END
