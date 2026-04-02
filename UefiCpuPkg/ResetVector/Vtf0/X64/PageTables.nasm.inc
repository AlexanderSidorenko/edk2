;------------------------------------------------------------------------------
; @file
; Emits static page tables for 1:1 identity mapping (virtual == physical).
;
; These page tables are assembled as raw data directly into the ROM image.
; They are NOT constructed at runtime — the binary contains the complete
; page table hierarchy as pre-computed bytes. The code in PageTables64.nasm.inc
; simply loads CR3 with their address.
;
; Two configurations are supported:
;
; 1. PAGE_TABLE_1G defined: Use 1GB pages in PDPTE entries.
;    Maps 512 * 1GB = 512GB of address space.
;    Requires CPU support for 1GB pages (CPUID leaf 0x80000001, EDX bit 26).
;    Total page table size: 4KB (Pdp) + 4KB (PML4) = 8KB minimum.
;
; 2. PAGE_TABLE_1G NOT defined: Use 2MB pages in PDE entries.
;    Maps 4 * 512 * 2MB = 4GB of address space.
;    Universally supported on all x86-64 CPUs.
;    Total page table size: 16KB (4 PDs) + 4KB (Pdp) + 4KB (PML4) = 24KB.
;
; Copyright (c) 2021 - 2023, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

BITS    64

;
; Tell ResetVectorVtf0.nasm.inc to add padding so that page tables are
; 4KB-aligned when the binary is placed at the top of the 4GB address space.
;
%define ALIGN_TOP_TO_4K_FOR_PAGING

;
; Page table non-leaf entry attributes:
; Used for entries that point to the next level of page tables (not to actual pages).
; ACCESSED (bit 5) is pre-set as an optimization — avoids a CPU write-back
; to the page table entry on first access.
;
%define PAGE_NLE_ATTR (PAGE_ACCESSED + \
                        PAGE_READ_WRITE + \
                        PAGE_PRESENT)

;
; Page table big leaf entry attributes:
; Used for entries that directly map physical pages (2MB PDE or 1GB PDPTE).
; PAGE_SIZE (bit 7) marks this as a leaf entry (large page, not a pointer
; to another table).
; DIRTY (bit 6) is pre-set as an optimization.
;
%define PAGE_BLE_ATTR (PAGE_ACCESSED + \
                        PAGE_READ_WRITE + \
                        PAGE_DIRTY + \
                        PAGE_PRESENT + \
                        PAGE_SIZE)

;
; Macro to construct a non-leaf page table entry.
; Takes a label (next-level table) and produces: physical_address | attributes
;
%define PAGE_NLE(address) (ADDR_OF(address) + \
                    PAGE_NLE_ATTR)

;
; Macro to construct a 1GB PDPTE leaf entry.
; Input: x = index (0-511), mapping physical address x * 1GB.
; (x << 30) = physical address (1GB = 2^30 bytes).
;
%define PAGE_PDPTE_1GB(x) ((x << 30) + PAGE_BLE_ATTR)

;
; Macro to construct a 2MB PDE leaf entry.
; Input: x = index (0-2047), mapping physical address x * 2MB.
; (x << 21) = physical address (2MB = 2^21 bytes).
;
%define PAGE_PDE_2MB(x) ((x << 21) + PAGE_BLE_ATTR)

ALIGN 16

%ifdef PAGE_TABLE_1G

;============================================================================
; 1GB Page Tables — Map 0 to 512GB using 512 PDPTE entries
;============================================================================
;
; This is the compact configuration. Each PDPTE entry maps 1GB directly
; (no intermediate Page Directory needed). Only 2 tables total:
;   Pdp (4KB) -> 512 entries, each a 1GB leaf page
;   Pml4 (4KB) -> 1 entry pointing to Pdp
;

Pdp:
    ;
    ; Page-directory pointer table (PDPT): 512 entries, each 8 bytes = 4KB.
    ; Entry i maps physical address [i*1GB .. (i+1)*1GB).
    ; All entries are leaf entries (PAGE_SIZE bit set) mapping 1GB pages.
    ;
    %assign i 0
    %rep      512
        DQ    PAGE_PDPTE_1GB(i)     ; Map 1GB page starting at physical address i*1GB
        %assign i i+1
    %endrep

%else

;============================================================================
; 2MB Page Tables — Map 0 to 4GB using 2048 PDE entries
;============================================================================
;
; This is the universally-compatible configuration. Maps 4GB using 2MB pages.
;
; Structure:
;   Pd (4 pages, 16KB total) -> 2048 entries, each a 2MB leaf page
;   Pdp (4KB) -> 4 entries, each pointing to one Pd page
;   Pml4 (4KB) -> 1 entry pointing to Pdp
;

Pd:
    ;
    ; Page Directory (PD): 2048 entries (4 tables * 512 entries), each 8 bytes.
    ; Entry i maps physical address [i*2MB .. (i+1)*2MB).
    ; Total mapped: 2048 * 2MB = 4GB.
    ;
    ; These 2048 entries occupy 4 consecutive 4KB pages (Pd+0x0000 through
    ; Pd+0x3FFF). Each 4KB page is pointed to by one Pdp entry below.
    ;
    %assign i 0
    %rep    0x800                       ; 0x800 = 2048
        DQ      PAGE_PDE_2MB(i)         ; Map 2MB page starting at physical address i*2MB
        %assign i i+1
    %endrep

Pdp:
    ;
    ; Page-directory pointer table (PDPT): 4 entries (only first 4 used).
    ; Each entry points to one 4KB page of PD entries above.
    ;
    ; Entry 0 -> Pd+0x0000 (covers physical 0-1GB)
    ; Entry 1 -> Pd+0x1000 (covers physical 1-2GB)
    ; Entry 2 -> Pd+0x2000 (covers physical 2-3GB)
    ; Entry 3 -> Pd+0x3000 (covers physical 3-4GB)
    ;
    DQ      PAGE_NLE(Pd)               ; -> Pd page 0 (PDE entries 0-511)
    DQ      PAGE_NLE(Pd + 0x1000)      ; -> Pd page 1 (PDE entries 512-1023)
    DQ      PAGE_NLE(Pd + 0x2000)      ; -> Pd page 2 (PDE entries 1024-1535)
    DQ      PAGE_NLE(Pd + 0x3000)      ; -> Pd page 3 (PDE entries 1536-2047)

    ;
    ; Pad the Pdp table to exactly 4KB (the remaining 508 entries are zero/not-present).
    ;
    TIMES   0x1000 - ($ - Pdp) DB 0

%endif

;============================================================================
; PML4 — Page Map Level 4 (top level for 4-level paging)
;============================================================================
;
; The PML4 is the root page table. CR3 points here.
; Only the first entry is used, pointing to the Pdp table above.
; This maps virtual addresses 0 through 512GB (or 4GB, depending on config)
; to the same physical addresses (identity mapping).
;
; The remaining 511 entries are zero (not present), so virtual addresses
; above 512GB will cause page faults.
;
Pml4:
    DQ      PAGE_NLE(Pdp)              ; Entry 0: -> Pdp table
    TIMES   0x1000 - ($ - Pml4) DB 0   ; Pad to 4KB (remaining 511 entries = 0)

%ifdef USE_5_LEVEL_PAGE_TABLE
;============================================================================
; PML5 — Page Map Level 5 (top level for 5-level paging / LA57)
;============================================================================
;
; Only used when the CPU supports 5-level paging (CPUID.07H.ECX[16]=1).
; The PML5 sits above PML4 and extends the virtual address space from
; 48 bits to 57 bits.
;
; Only the first entry points to PML4; the rest are zero (not present).
;
Pml5:
    DQ      PAGE_NLE(Pml4)             ; Entry 0: -> PML4 table
    TIMES   0x1000 - ($ - Pml5) DB 0   ; Pad to 4KB (remaining 511 entries = 0)
%endif

;
; EndOfPageTables — Marks the end of all page table data.
;
; Referenced by ResetVectorVtf0.nasm.inc for padding calculations to ensure
; the page tables are 4KB-aligned in the final ROM image.
;
EndOfPageTables:
