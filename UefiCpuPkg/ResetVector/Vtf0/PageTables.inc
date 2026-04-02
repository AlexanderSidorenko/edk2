;------------------------------------------------------------------------------
; @file
; Definitions of Page Table Entry bit flags for the reset vector module.
;
; These constants define the individual bit fields in x86-64 page table entries.
; They apply to all levels: PML5E, PML4E, PDPTE, PDE, and PTE.
;
; A page table entry is a 64-bit value. The low 12 bits are flags, bits 12-51
; hold the physical address of the next table (or page frame), and bits 52-63
; are available/reserved.
;
; Copyright (c) 2021, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

%define PAGE_PRESENT            0x01    ; Bit 0: Page is valid/present in memory
%define PAGE_READ_WRITE         0x02    ; Bit 1: Page is writable (0 = read-only)
%define PAGE_USER_SUPERVISOR    0x04    ; Bit 2: Accessible from ring 3 (0 = kernel only)
%define PAGE_WRITE_THROUGH      0x08    ; Bit 3: Write-through caching (0 = write-back)
%define PAGE_CACHE_DISABLE     0x010    ; Bit 4: Disable caching for this page
%define PAGE_ACCESSED          0x020    ; Bit 5: CPU sets this when page is read
%define PAGE_DIRTY             0x040    ; Bit 6: CPU sets this when page is written (leaf only)
%define PAGE_SIZE              0x080    ; Bit 7: Large page (2MB at PDE, 1GB at PDPTE level)
%define PAGE_GLOBAL           0x0100    ; Bit 8: Don't flush from TLB on CR3 write
%define PAGE_2M_PAT          0x01000    ; Bit 12: PAT bit for 2MB pages (different position than 4KB)

