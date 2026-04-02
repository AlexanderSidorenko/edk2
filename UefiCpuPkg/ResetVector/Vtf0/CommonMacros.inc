;------------------------------------------------------------------------------
; @file
; Common macros used in the ResetVector VTF module.
;
; Copyright (c) 2008, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

;
; ADDR16_OF(x) — Convert assembly label to 16-bit real-mode offset.
;
; In real mode at reset, the CPU runs with CS.base = 0xFFFF0000 (a special
; reset state). The segment spans 0xFFFF0000..0xFFFFFFFF (64KB). An offset
; within this segment is: physical_address - 0xFFFF0000, which is the same
; as 0x10000 - (fourGigabytes - label).
;
; fourGigabytes is a label at the very end of the binary (representing 0x100000000).
; So: offset = 0x10000 - (fourGigabytes - x) = 0x10000 - fourGigabytes + x
;
%define ADDR16_OF(x) (0x10000 - fourGigabytes + x)

;
; ADDR_OF(x) — Convert assembly label to 32-bit physical address.
;
; Since this code is placed at the top of the 4GB address space (ending at
; 0x100000000), and fourGigabytes is the end-label:
;   physical_address = 0x100000000 - (fourGigabytes - x)
;                    = 0x100000000 - fourGigabytes + x
;
%define ADDR_OF(x) (0x100000000 - fourGigabytes + x)

;
; OneTimeCall — Stack-free subroutine call mechanism.
;
; Problem: At CPU reset, there is NO RAM and NO stack. We can't use the
;          normal CALL/RET instructions because CALL pushes a return address
;          to the stack, and RET pops it.
;
; Solution: Use JMP + a compile-time-generated label for the return point.
;
; How it works:
;   OneTimeCall Foo
;     expands to:
;       jmp     Foo                     ; Jump to subroutine body
;   FooOneTimerCallReturn:              ; Return label (defined right after jmp)
;                                       ; Execution resumes here after Foo returns
;
;   (inside Foo, at the end:)
;   OneTimeCallRet Foo
;     expands to:
;       jmp     FooOneTimerCallReturn   ; Jump back to after the call site
;
; Why "OneTime"? The return label is a fixed address. If Foo were called from
; two different places, both returns would go to the first call site. So each
; routine may only be called once — which is fine for this linear boot sequence.
;
; NOTE: "OneTimerCallReturn" contains a historical typo ("Timer" instead of
;       "Time") that has been preserved for compatibility.
;
%macro  OneTimeCall 1
    jmp     %1
%1 %+ OneTimerCallReturn:
%endmacro

%macro  OneTimeCallRet 1
    jmp     %1 %+ OneTimerCallReturn
%endmacro

;
; StartOfResetVectorCode — Marks the beginning of the reset vector binary.
;
; This label is used by ADDR_OF_START_OF_RESET_CODE to compute the start
; address of this code blob at runtime. Tools and later phases can use this
; to determine the size and location of the reset vector code.
;
StartOfResetVectorCode:

%define ADDR_OF_START_OF_RESET_CODE ADDR_OF(StartOfResetVectorCode)

