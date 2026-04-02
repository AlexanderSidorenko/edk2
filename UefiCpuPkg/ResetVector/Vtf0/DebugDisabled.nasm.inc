;------------------------------------------------------------------------------
; @file
; Debug disabled — all debug macros expand to nothing (zero code emitted).
;
; This is the default/production debug backend. When neither DEBUG_PORT80 nor
; DEBUG_SERIAL is defined, this file is included instead. All macros are
; empty, so no debug I/O instructions appear in the final binary.
;
; Copyright (c) 2009, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

BITS    16

;
; debugInitialize — No-op (no debug output to initialize).
;
%macro  debugInitialize 0
    ;
    ; No initialization is required
    ;
%endmacro

;
; debugShowPostCode — No-op (POST codes silently discarded).
;
; @param %1  The POST code byte value (ignored)
;
%macro  debugShowPostCode 1
%endmacro

