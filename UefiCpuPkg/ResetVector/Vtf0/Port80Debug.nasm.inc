;------------------------------------------------------------------------------
; @file
; Port 0x80 debug support macros.
;
; Port 0x80 is the standard ISA POST (Power-On Self-Test) code port.
; Writing a byte to this port displays it on POST code diagnostic cards
; (PCI/ISA cards with 7-segment LED displays). In QEMU, it shows up in
; the debug console. It's the simplest possible debug output mechanism.
;
; No initialization is needed — the ISA I/O port is always available.
;
; Copyright (c) 2009, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

BITS    16

;
; debugInitialize — No-op for Port 0x80 (the port needs no initialization).
;
%macro  debugInitialize 0
    ;
    ; No initialization is required
    ;
%endmacro

;
; debugShowPostCode — Write a single-byte POST code to I/O port 0x80.
;
; Usage: debugShowPostCode POSTCODE_32BIT_MODE
;   Expands to:
;     mov al, 0x32
;     out 0x80, al
;
; The value appears immediately on POST code display hardware.
;
; @param %1  The POST code byte value to display
;
%macro  debugShowPostCode 1
    mov     al, %1              ; AL = POST code value
    out     0x80, al            ; Write to I/O port 0x80 (POST code display)
%endmacro

