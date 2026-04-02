;------------------------------------------------------------------------------
; @file
; Transition from 32-bit flat protected mode into 64-bit flat protected mode
; (Long Mode).
;
; x86-64 Long Mode requires three things beyond protected mode:
;   1. Page tables loaded in CR3 (identity mapping virtual == physical)
;   2. PAE (Physical Address Extension) enabled in CR4
;   3. LME (Long Mode Enable) set in IA32_EFER MSR
;   4. Paging enabled (CR0.PG = 1)
;   5. A far jump to a 64-bit code segment
;
; Steps 1-5 must happen in this exact order. The CPU actually enters long mode
; when PG is set (step 4), but it doesn't execute 64-bit instructions until
; the far jump loads a 64-bit CS (step 5).
;
; Copyright (c) 2008 - 2018, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

BITS    32

;============================================================================
; Transition32FlatTo64Flat — Switch from 32-bit protected to 64-bit long mode
;============================================================================
;
; Called from: Main16 (32-bit mode) via "OneTimeCall Transition32FlatTo64Flat"
; Returns to:  Main16 via OneTimeCallRet
;              -> jumps to Transition32FlatTo64FlatOneTimerCallReturn in Main.nasm.inc
;
; After return, the CPU is in 64-bit long mode with identity-mapped paging.
;
; Modified:  EAX
;
Transition32FlatTo64Flat:

    ;
    ; STEP 1: Load page tables into CR3.
    ;
    ; SetCr3ForPageTables64 (in Ia32/PageTables64.nasm.inc) sets CR3 to
    ; the physical address of the PML4 (or PML5 for 5-level paging).
    ; The page tables are statically built into the ROM image at assembly time.
    ;
    ; OneTimeCall jumps to SetCr3ForPageTables64.
    ; OneTimeCallRet returns to SetCr3ForPageTables64OneTimerCallReturn (right here ↓)
    ;
    OneTimeCall SetCr3ForPageTables64

    ;
    ; STEP 2: Enable PAE (Physical Address Extension) in CR4.
    ;
    ; PAE (bit 5) is required for long mode. It extends page table entries
    ; from 32-bit to 64-bit, allowing physical addresses larger than 4GB.
    ;
    ; bts = "Bit Test and Set" — copies the current bit 5 to CF, then sets it to 1.
    ; We use bts instead of "or eax, (1<<5)" for clarity and smaller encoding.
    ;
    mov     eax, cr4                    ; Read current CR4
    bts     eax, 5                      ; Set bit 5 (PAE) to 1
    mov     cr4, eax                    ; Write back to CR4

    ;
    ; STEP 3: Set LME (Long Mode Enable) in IA32_EFER MSR.
    ;
    ; IA32_EFER (Extended Feature Enable Register) is MSR 0xC0000080.
    ; Bit 8 (LME) enables long mode. Setting it doesn't activate long mode
    ; immediately — that happens when paging is enabled (step 4).
    ;
    ; rdmsr/wrmsr use ECX for the MSR number and EDX:EAX for the value.
    ;
    mov     ecx, 0xc0000080             ; ECX = MSR number (IA32_EFER)
    rdmsr                                ; Read IA32_EFER -> EDX:EAX
    bts     eax, 8                      ; Set bit 8 (LME) in the low 32 bits
    wrmsr                                ; Write EDX:EAX -> IA32_EFER

    ;
    ; STEP 4: Enable paging by setting CR0.PG (bit 31).
    ;
    ; This is the moment the CPU actually enters long mode (since PAE and LME
    ; are already set). The CPU is now in a "compatibility sub-mode" of long
    ; mode — it's technically 64-bit capable, but still executing 32-bit
    ; code because CS hasn't been reloaded yet.
    ;
    mov     eax, cr0                    ; Read current CR0
    bts     eax, 31                     ; Set bit 31 (PG) — enable paging
    mov     cr0, eax                    ; Write back to CR0
                                         ; From this point: long mode is ACTIVE
                                         ; but CS still contains a 32-bit selector

    ;
    ; STEP 5: Far jump to load the 64-bit code segment.
    ;
    ; LINEAR_CODE64_SEL (0x38) is the 64-bit code segment in our GDT (defined
    ; in Real16ToFlat32.nasm.inc). Its L flag (Long mode) = 1 and D flag = 0.
    ;
    ; This far jump:
    ;   1. Loads CS with the 64-bit code selector (0x38)
    ;   2. Forces the CPU to reload the segment descriptor from the GDT
    ;   3. The L=1 flag activates 64-bit instruction decoding
    ;   4. Flushes the instruction pipeline
    ;
    ; After landing at jumpTo64BitAndLandHere, the CPU executes 64-bit code.
    ;
    jmp     LINEAR_CODE64_SEL:ADDR_OF(jumpTo64BitAndLandHere)

; ---- From here on, we are in 64-bit long mode ----
BITS    64
jumpTo64BitAndLandHere:

    debugShowPostCode POSTCODE_64BIT_MODE   ; POST code 0x64 — we're in 64-bit mode

    ;
    ; Return to Main16 (now running in 64-bit mode!).
    ; Jumps to Transition32FlatTo64FlatOneTimerCallReturn in Main.nasm.inc.
    ;
    OneTimeCallRet Transition32FlatTo64Flat

