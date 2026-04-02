;------------------------------------------------------------------------------
; @file
; First code executed by processor after resetting.
;
; This file contains the reset vector — the very first instruction the CPU
; fetches after power-on or reset. It is placed at the end of the assembled
; binary so that it maps to physical address 0xFFFFFFF0 (the x86 reset vector
; address, 16 bytes below the 4GB boundary).
;
; The VTF-0 (Volume Top File, variant 0) signature at 0xFFFFFFEC allows
; firmware tools to identify this module. The "-0" means no fixups/relocations
; are needed — all addresses are computed at assembly time.
;
; Copyright (c) 2008 - 2014, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

BITS    16

ALIGN   16

;
; Conditional padding to ensure page-table alignment.
;
; When page tables are built into the VTF0 image (x64 builds), the binary
; must be sized so that page tables land on 4KB boundaries when the end of
; the binary is placed at 0x100000000. Two padding regions achieve this:
;
; 1. Pad from EndOfPageTables up to a 4KB boundary (page table alignment)
; 2. Pad the remaining space leaving only the last 0x20 bytes for the
;    reset vector area — this ensures the VTF signature and jmp are
;    isolated in the topmost 4KB page (some platforms require this
;    separation so that the initial reset code page can be locked/protected)
;
%ifdef ALIGN_TOP_TO_4K_FOR_PAGING
    TIMES (0x1000 - ($ - EndOfPageTables)) DB 0
    TIMES (0x1000 - 0x20) DB 0
%endif

;
; Three zero doublewords at 0xFFFFFFE0.
; This area (offsets -32 to -20 from 4GB) is reserved. Some platforms
; place AP (Application Processor) startup addresses here. In this
; generic VTF-0, it's just zero padding.
;
    DD      0, 0, 0

;
; The VTF-0 signature at address 0xFFFFFFEC.
;
; "VTF\0" identifies this as a Volume Top File. The build tools (GenVtf)
; look for this signature to validate the reset vector module.
; The \0 (null byte) after "VTF" distinguishes VTF-0 (no fixups needed)
; from other VTF variants.
;
vtfSignature:
    DB      'V', 'T', 'F', 0

; Align to 16-byte boundary — this places us at exactly 0xFFFFFFF0.
ALIGN   16

;============================================================================
; resetVector — THE VERY FIRST INSTRUCTION EXECUTED BY THE CPU
;============================================================================
;
; Physical address: 0xFFFFFFF0
;
; CPU state at this point:
;   - 16-bit real mode (but CS.base = 0xFFFF0000, a special reset state)
;   - CS = 0xF000, IP = 0xFFF0 (so CS.base + IP = 0xFFFFFFF0)
;   - EAX = BIST (Built-In Self Test) result (0 = pass)
;   - All other registers are architecturally undefined
;   - No RAM available — only CPU registers and flash/ROM
;   - Cache is disabled, interrupts are disabled
;   - DL may contain the initial APIC ID on some platforms
;
; What happens:
;   Two NOPs provide padding within the 16-byte reset vector window
;   (the space from 0xFFFFFFF0 to 0xFFFFFFFF), then we jump to the
;   BSP initialization entry point. The jump is a near relative jump
;   that goes backward into the ROM to EarlyBspInitReal16.
;
resetVector:
    nop                             ; 1 byte padding (0x90)
    nop                             ; 1 byte padding (0x90)
    jmp     EarlyBspInitReal16      ; Jump to BSP init in Init16.nasm.inc
                                    ; (3-byte near jump: 0xE9 + 16-bit relative offset)
                                    ; This leaves 11 unused bytes until 0xFFFFFFFF

; Align to next 16-byte boundary — this defines the "fourGigabytes" label
; at address 0x100000000. All ADDR_OF() calculations reference this label.
ALIGN   16

;
; fourGigabytes — Sentinel label representing the 4GB boundary (0x100000000).
;
; This label is never executed. It's used by the ADDR_OF() and ADDR16_OF()
; macros to convert assembly-time label offsets into runtime physical
; addresses. The physical address of any label X is:
;   ADDR_OF(X) = 0x100000000 - (fourGigabytes - X)
;
fourGigabytes:

