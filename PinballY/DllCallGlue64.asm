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
; a callee that is expecting a regular fixed argument list.  C++ doesn't
; allow such a call natively.
;
; We need this for our Javascript DLL import feature, since that needs to
; allow a Javascript caller to invoke functions bound at run-time with
; argument lists constructed dynamically at run-time.
;
; In 64-bit mode, Windows uses a single calling convention.  The first
; four arguments are always passed in registers; additional arguments are
; passed on the stack.  In addition, the caller creates "shadow" slots
; on the stack where the first four arguments *would* have gone if they
; had been passed on the stack.
;
; UINT64 dll_call_glue64(FARPROC funcPtr, const void *pArgs, size_t nArgBytes)
;
;    RCX = funcPtr
;    RDX = pArgs
;    R8 = nArgBytes
;
; The return value is in XMM0 for float, double, and __m128 vector types,
; and in RAX for everything else.  We don't mess with the return registers
; at all; we just leave the return value in whichever register the callee
; put it in.  So it's up to the C++ caller to pull the result from the
; right register.  To facilitate this, we provide two aliases to this same
; code.  These are identical at the assembler level, but we declare them
; to have UINT64 and __m128 return types in the C++ code.  The caller can
; invoke the alias that corresponds to the return register used for the
; return type for each call, to coerce the compiler to retrieve the result
; from the correct register on a call-by-call basis.
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
