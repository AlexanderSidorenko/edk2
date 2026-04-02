;------------------------------------------------------------------------------
; @file
; Sets the CR3 register for 64-bit paging.
;
; CR3 must point to the Page Map Level 4 (PML4) table — or the PML5 table
; if 5-level paging is supported and enabled. The actual page table data is
; statically compiled into the ROM image by X64/PageTables.nasm.inc.
;
; This routine optionally supports 5-level paging (LA57), which was introduced
; with Intel Ice Lake. 5-level paging extends the virtual address space from
; 48 bits (256 TB) to 57 bits (128 PB). It adds a PML5 table above PML4.
;
; Detection: CPUID leaf 7, sub-leaf 0, ECX bit 16 indicates LA57 support.
;
; Copyright (c) 2008 - 2023, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

BITS    32

;============================================================================
; SetCr3ForPageTables64 — Point CR3 to the correct page table root
;============================================================================
;
; Called from: Transition32FlatTo64Flat via "OneTimeCall SetCr3ForPageTables64"
; Returns to:  Transition32FlatTo64Flat via OneTimeCallRet
;              -> jumps to SetCr3ForPageTables64OneTimerCallReturn in
;                 Ia32/Flat32ToFlat64.nasm.inc
;
; The page tables are pre-built into the ROM image (see X64/PageTables.nasm.inc).
; We just need to load the correct base address into CR3.
;
; Modified:  EAX (also EBX, ECX, EDX if USE_5_LEVEL_PAGE_TABLE is defined,
;            since CPUID destroys all four)
;
SetCr3ForPageTables64:

    ;
    ; The page tables exist as static data in the ROM image.
    ; Their assembly-time labels (Pml4, Pml5) are converted to physical
    ; addresses using ADDR_OF().
    ;

%ifdef USE_5_LEVEL_PAGE_TABLE
    ;
    ; ---- 5-level paging detection ----
    ;
    ; First, check if CPUID supports leaf 7 (structured extended feature flags).
    ; CPUID leaf 0 returns the maximum basic leaf number in EAX.
    ;
    mov     eax, 0                      ; CPUID leaf 0
    cpuid                                ; EAX = max supported basic leaf number
    cmp     eax, 07h                    ; Does CPU support leaf 7?
    jb      NotSupport5LevelPaging      ; No -> fall back to 4-level paging

    ;
    ; Query CPUID leaf 7, sub-leaf 0 for extended feature flags.
    ; Bit 16 of ECX indicates 5-level paging (LA57) support.
    ;
    mov     eax, 07h                    ; CPUID leaf 7
    mov     ecx, 0                      ; Sub-leaf 0
    cpuid                                ; Results in EAX, EBX, ECX, EDX
    bt      ecx, 16                     ; Test bit 16 of ECX: LA57 supported?
                                         ; bt copies the bit into CF
    jnc     NotSupport5LevelPaging      ; CF=0 -> bit not set -> no LA57

    ;
    ; 5-level paging IS supported. Load CR3 with PML5 table address.
    ;
    mov     eax, ADDR_OF(Pml5)          ; EAX = physical address of PML5 table
    mov     cr3, eax                     ; CR3 = PML5 base address

    ;
    ; Enable LA57 in CR4 (bit 12). This tells the CPU to use 5-level
    ; page table walks instead of 4-level.
    ;
    mov     eax, cr4                    ; Read current CR4
    bts     eax, 12                     ; Set bit 12 (LA57)
    mov     cr4, eax                    ; Write back to CR4

    jmp     SetCr3Done                  ; Skip 4-level fallback

NotSupport5LevelPaging:
%endif

    ;
    ; ---- Standard 4-level paging ----
    ;
    ; Load CR3 with PML4 table address. This is the common case for most
    ; x86-64 processors. The PML4 table is the root of 4-level page tables
    ; that map up to 256 TB of virtual address space.
    ;
    mov     eax, ADDR_OF(Pml4)          ; EAX = physical address of PML4 table
    mov     cr3, eax                     ; CR3 = PML4 base address

SetCr3Done:

    ;
    ; Return to Transition32FlatTo64Flat.
    ; Jumps to SetCr3ForPageTables64OneTimerCallReturn in Flat32ToFlat64.nasm.inc.
    ;
    OneTimeCallRet SetCr3ForPageTables64

