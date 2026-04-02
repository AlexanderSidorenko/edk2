;------------------------------------------------------------------------------
; @file
; Definitions of POST CODES for the reset vector module
;
; These codes are sent to I/O port 0x80 (the standard ISA POST code port)
; to indicate boot progress. On real hardware, a POST code display card shows
; these values. On QEMU, they're visible with -debugcon options.
;
; The values are mnemonics: 0x16 = "16"-bit mode, 0x32 = "32"-bit, 0x64 = "64"-bit.
; BFV/SEC codes use 0xBx and 0xFx with 0=not found, 1=found.
;
; Copyright (c) 2009, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

%define POSTCODE_16BIT_MODE     0x16    ; CPU is running in 16-bit real mode
%define POSTCODE_32BIT_MODE     0x32    ; CPU has entered 32-bit protected mode
%define POSTCODE_64BIT_MODE     0x64    ; CPU has entered 64-bit long mode

%define POSTCODE_BFV_NOT_FOUND  0xb0    ; FATAL: Boot Firmware Volume not found (will hang)
%define POSTCODE_BFV_FOUND      0xb1    ; BFV located successfully

%define POSTCODE_SEC_NOT_FOUND  0xf0    ; FATAL: SEC Core entry point not found (will hang)
%define POSTCODE_SEC_FOUND      0xf1    ; SEC Core located, about to jump to it

