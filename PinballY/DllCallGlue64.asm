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

_TEXT SEGMENT

PUBLIC dll_call_glue64

dll_call_glue64 PROC

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

dll_call_glue64 ENDP

_TEXT ENDS

END
