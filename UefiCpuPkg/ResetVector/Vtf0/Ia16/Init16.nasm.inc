;------------------------------------------------------------------------------
; @file
; 16-bit initialization code — the first meaningful code after the reset vector.
;
; This file provides two entry points:
;   EarlyBspInitReal16 — Called by the reset vector (ResetVectorVtf0.nasm.inc)
;                        for the Bootstrap Processor (BSP). Sets DI='BP' to
;                        identify this as the BSP, then falls through to Main16.
;
;   EarlyInit16        — Called from Main16 via OneTimeCall. Saves the BIST
;                        (Built-In Self Test) result and initializes debug output.
;
; Copyright (c) 2008 - 2009, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------


BITS    16

;============================================================================
; EarlyBspInitReal16 — BSP Entry Point (jumped to from reset vector)
;============================================================================
;
; This is where execution arrives from the reset vector at 0xFFFFFFF0.
; It marks this processor as the BSP by setting DI to the ASCII string 'BP'
; (0x5042), then jumps to the main initialization sequence.
;
; On multi-core systems, Application Processors (APs) would enter via a
; different path and set DI to 'AP'. The SEC phase later uses this to
; distinguish BSP from APs.
;
; @param[out] DI    'BP' (0x5042) to indicate this is the Bootstrap Processor
;
EarlyBspInitReal16:
    mov     di, 'BP'            ; DI = 0x5042 ('B'=0x42, 'P'=0x50, little-endian)
                                ; This value is passed through to SEC as a parameter
    jmp     short Main16        ; Jump to the main orchestrator (Main.nasm.inc)
                                ; "short" forces a 2-byte encoding (1-byte opcode + 1-byte offset)

;============================================================================
; EarlyInit16 — Save BIST and initialize debug output
;============================================================================
;
; Called from: Main16 via "OneTimeCall EarlyInit16"
; Returns to:  Main16 (the instruction after the OneTimeCall) via OneTimeCallRet
;              -> jumps to label EarlyInit16OneTimerCallReturn in Main.nasm.inc
;
; At CPU reset, the hardware places the BIST (Built-In Self Test) result in
; EAX. A value of 0 means the CPU passed its self-test. We need to preserve
; this value until we can pass it to the SEC phase.
;
; Problem: We have no stack (no RAM), so we can't push EAX to save it.
; Solution: Stash it in ESP. Since there's no stack, ESP is just a spare
;           register. We'll copy it back to EAX before jumping to SEC.
;
; Modified:  EAX (by debugInitialize macro, potentially)
; @param[in]  EAX   BIST result from CPU reset
; @param[out] ESP   BIST result (saved copy — ESP used as scratch, NOT as stack)
;
EarlyInit16:
    mov     esp, eax            ; Save BIST result from EAX into ESP for safekeeping.
                                ; We use ESP because there is NO stack — no RAM is
                                ; available yet. ESP is purely a scratch register here.

    debugInitialize             ; Macro: initialize debug output hardware.
                                ; - Port80: no-op (port 0x80 needs no init)
                                ; - Serial: configures COM1 UART to 115200/8/N/1
                                ; - Disabled: no-op

    OneTimeCallRet EarlyInit16  ; Return to caller in Main16.
                                ; Expands to: jmp EarlyInit16OneTimerCallReturn
                                ; That label is in Main.nasm.inc, right after
                                ; the "OneTimeCall EarlyInit16" line.

