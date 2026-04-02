;------------------------------------------------------------------------------
; @file
; Main routine of the pre-SEC code up through the jump into SEC.
;
; This is the central orchestrator of the reset vector code. It calls each
; initialization step in sequence using OneTimeCall (a stack-free call
; mechanism), then jumps to the SEC (Security) phase entry point — which
; is the first C code in the UEFI boot process.
;
; Execution flow:
;   1. EarlyInit16:                   Save BIST, init debug  (16-bit)
;   2. TransitionFromReal16To32BitFlat: Load GDT, enable PE   (16->32 bit)
;   3. Flat32SearchForBfvBase:        Find firmware volume    (32-bit)
;   4. Flat32SearchForSecEntryPoint:  Find SEC core in FFS    (32-bit)
;   5. [x64] Transition32FlatTo64Flat: Enable paging, LME     (32->64 bit)
;   6. Jump to SEC entry point                                (32 or 64 bit)
;
; Copyright (c) 2008 - 2009, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------


BITS    16

;
; Register contract — what Main16 expects and produces:
;
; @param[in,out]  RAX/EAX  Initial value of the EAX register
;                          (BIST: Built-in Self Test)
; @param[in,out]  DI       'BP': boot-strap processor, or
;                          'AP': application processor
; @param[out]     RBP/EBP  Address of Boot Firmware Volume (BFV)
; @param[out]     DS       Selector allowing flat access to all addresses
; @param[out]     ES       Selector allowing flat access to all addresses
; @param[out]     FS       Selector allowing flat access to all addresses
; @param[out]     GS       Selector allowing flat access to all addresses
; @param[out]     SS       Selector allowing flat access to all addresses
;
; Modified:  EBX, ECX, EDX, EBP
;
; @return         None  This routine jumps to SEC and does not return
;
Main16:
    ;
    ; STEP 1: Save BIST result and initialize debug output.
    ; (Still in 16-bit real mode)
    ;
    ; After this call:
    ;   ESP = BIST value (saved from EAX)
    ;   Debug output initialized (serial/port80/none)
    ;
    ; OneTimeCall jumps to EarlyInit16 (in Ia16/Init16.nasm.inc).
    ; When EarlyInit16 executes "OneTimeCallRet EarlyInit16", it jumps back
    ; to the label EarlyInit16OneTimerCallReturn, which is right here ↓
    ;
    OneTimeCall EarlyInit16

    ;
    ; STEP 2: Transition from 16-bit real mode to 32-bit flat protected mode.
    ;
    ; This loads the GDT, sets CR0.PE, does a far jump to flush the pipeline,
    ; sets CR4, and loads all segment registers with flat selectors.
    ;
    ; After this call:
    ;   CPU is in 32-bit protected mode
    ;   DS=ES=FS=GS=SS = LINEAR_SEL (flat 0-4GB access)
    ;   CS = LINEAR_CODE_SEL (32-bit code segment)
    ;
    ; OneTimeCall jumps to TransitionFromReal16To32BitFlat (Ia16/Real16ToFlat32.nasm.inc).
    ; OneTimeCallRet returns here ↓
    ;
    OneTimeCall TransitionFromReal16To32BitFlat

; ---- From this point forward, we are in 32-bit protected mode ----
BITS    32

    ;
    ; STEP 3: Search for the Boot Firmware Volume (BFV).
    ;
    ; Scans every 4KB-aligned address in the top 16MB of the address space
    ; (0xFF000000..0xFFFFF000) looking for an FFS2 or FFS3 GUID signature
    ; in the firmware volume header.
    ;
    ; After this call:
    ;   EBP = base address of the Boot Firmware Volume
    ;   (or hangs with POST code 0xB0 if not found)
    ;
    ; OneTimeCall jumps to Flat32SearchForBfvBase (Ia32/SearchForBfvBase.nasm.inc).
    ; OneTimeCallRet returns here ↓
    ;
    OneTimeCall Flat32SearchForBfvBase

    ;
    ; At this point: EBP = Start of BFV
    ;

    ;
    ; STEP 4: Search for the SEC (Security Core) entry point within the BFV.
    ;
    ; Walks the FFS (Firmware File System) file headers inside the BFV,
    ; looking for a file of type EFI_FV_FILETYPE_SECURITY_CORE (0x03).
    ; When found, parses the PE32 or TE image header to extract the entry point.
    ;
    ; After this call:
    ;   ESI = SEC Core entry point address
    ;   EBP = Start of BFV (preserved)
    ;   (or hangs with POST code 0xF0 if not found)
    ;
    ; OneTimeCall jumps to Flat32SearchForSecEntryPoint (Ia32/SearchForSecEntry.nasm.inc).
    ; OneTimeCallRet returns here ↓
    ;
    OneTimeCall Flat32SearchForSecEntryPoint

    ;
    ; At this point:
    ;   ESI = SEC Core entry point
    ;   EBP = Start of BFV
    ;

%ifdef ARCH_IA32
    ;--------------------------------------------------------------------
    ; IA-32 path: Jump directly to SEC (no 64-bit transition needed)
    ;--------------------------------------------------------------------

    ;
    ; Restore the original BIST value from ESP back into EAX.
    ; (We stashed it in ESP back in EarlyInit16 because we had no stack.)
    ; SEC expects the BIST result in EAX.
    ;
    mov     eax, esp

    ;
    ; FINAL JUMP: Transfer control to the SEC Core.
    ;
    ; Register state at SEC entry:
    ;   EAX = BIST result (0 = CPU self-test passed)
    ;   EBP = BFV (Boot Firmware Volume) base address
    ;   DI  = 'BP' (bootstrap processor) or 'AP' (application processor)
    ;   DS=ES=FS=GS=SS = flat 0-4GB data selectors
    ;   CS  = flat 0-4GB 32-bit code selector
    ;
    ; SEC will set up Cache-as-RAM (a temporary stack in CPU cache),
    ; then begin the PEI (Pre-EFI Initialization) phase.
    ;
    jmp     esi

%else
    ;--------------------------------------------------------------------
    ; x64 path: Transition to 64-bit long mode first, then jump to SEC
    ;--------------------------------------------------------------------

    ;
    ; STEP 5: Transition from 32-bit flat protected mode to 64-bit long mode.
    ;
    ; This sets up page tables (CR3), enables PAE (CR4.PAE), sets LME
    ; (IA32_EFER.LME), enables paging (CR0.PG), and does a far jump to
    ; a 64-bit code segment.
    ;
    ; After this call:
    ;   CPU is in 64-bit long mode with identity-mapped paging
    ;   CS = LINEAR_CODE64_SEL (64-bit code segment)
    ;
    ; OneTimeCall jumps to Transition32FlatTo64Flat (Ia32/Flat32ToFlat64.nasm.inc).
    ; OneTimeCallRet returns here ↓
    ;
    OneTimeCall Transition32FlatTo64Flat

; ---- From this point forward, we are in 64-bit long mode ----
BITS    64

    ;
    ; Zero the upper 32 bits of registers that were set in 32-bit mode.
    ;
    ; When running in 32-bit mode, only the lower 32 bits of 64-bit registers
    ; are written. The upper 32 bits contain undefined/stale values. We must
    ; explicitly zero-extend them before using them as 64-bit addresses.
    ;
    ; Method: AND with 0x00000000FFFFFFFF to clear bits 63:32.
    ;
    mov     rax, 0x00000000ffffffff     ; Mask: keep only lower 32 bits
    and     rsi, rax                    ; RSI = zero-extended SEC entry point
    and     rbp, rax                    ; RBP = zero-extended BFV base address
    and     rsp, rax                    ; RSP = zero-extended BIST value

    ;
    ; At this point:
    ;   RSI = SEC Core entry point (zero-extended to 64-bit)
    ;   RBP = Start of BFV (zero-extended to 64-bit)
    ;

    ;
    ; Restore the original BIST value from RSP back into RAX.
    ; (We stashed it in ESP back in EarlyInit16 because we had no stack.)
    ; SEC expects the BIST result in RAX.
    ;
    mov     rax, rsp

    ;
    ; FINAL JUMP: Transfer control to the 64-bit SEC Core.
    ;
    ; Register state at SEC entry:
    ;   RAX = BIST result (0 = CPU self-test passed)
    ;   RBP = BFV (Boot Firmware Volume) base address
    ;   DI  = 'BP' (bootstrap processor) or 'AP' (application processor)
    ;   DS=ES=FS=GS=SS = flat data selectors
    ;   CS  = 64-bit code selector
    ;   Paging = enabled, identity mapped (virtual == physical)
    ;
    ; SEC will set up Cache-as-RAM (a temporary stack in CPU cache),
    ; then begin the PEI (Pre-EFI Initialization) phase.
    ;
    jmp     rsi

%endif


