; This file is part of PinballY
; Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
;
; 32-bit glue functions for Javascript DLL Import 

.686
.MODEL FLAT, C
_TEXT SEGMENT

PUBLIC DllImportCallbackGlue
EXTERN JavascriptEngine_CallCallback : PROC

DllImportCallbackGlue PROC

    ; set up a local stack frame
    push  ebp
    mov   ebp, esp

    ; Set up params for the callee: 
    ; JavascriptEngine_CallCallback(context_object_pointer, original_argv_base)
	;
	; Note that our immediate caller is the thunk routine, which CALLed us,
	; adding another return address to the stack.  So the original caller's
	; arguments are just above the pushed EBP + two return values = 3 DWORDs.
    lea   edx, [ebp+12]      ; original argv base
    push  edx
    push  eax                ; context object pointer is passed to us in RAX

    ; call the C++ entrypoint
    call  JavascriptEngine_CallCallback

    ; restore the stack and return
    mov   esp, ebp
    pop   ebp
    ret

DllImportCallbackGlue ENDP

_TEXT ENDS

END
