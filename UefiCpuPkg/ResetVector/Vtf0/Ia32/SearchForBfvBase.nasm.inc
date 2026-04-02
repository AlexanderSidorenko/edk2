;------------------------------------------------------------------------------
; @file
; Search for the Boot Firmware Volume (BFV) base address.
;
; The BFV is the main firmware volume in the flash ROM containing the SEC
; core (and other early boot files). After the CPU transitions to 32-bit
; protected mode, this routine scans the top 16MB of the address space
; to find it.
;
; Strategy: Check every 4KB-aligned address from 0xFFFFF000 downward to
; 0xFF000000. At each candidate address, check for the FFS3 or FFS2 GUID
; at offset +0x10 in the EFI_FIRMWARE_VOLUME_HEADER. If a GUID matches,
; validate the firmware volume length field.
;
; The firmware volume header layout (relevant fields):
;   Offset 0x00: ZeroVector[16]  (16 zero bytes)
;   Offset 0x10: FileSystemGuid  (16 bytes — identifies FFS2 or FFS3)
;   Offset 0x20: FvLength        (8 bytes — total volume size)
;   Offset 0x28: Signature       ("_FVH")
;   Offset 0x30: HeaderLength    (2 bytes)
;
; Copyright (c) 2008 - 2022, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

;
; EFI_FIRMWARE_FILE_SYSTEM2_GUID — identifies FFS2 (Firmware File System v2)
;   { 0x8c8ce578, 0x8a3d, 0x4f1c, { 0x99, 0x35, 0x89, 0x61, 0x85, 0xc3, 0x2d, 0xd3 } }
;
; The GUID is stored in mixed-endian format (first 3 fields little-endian,
; last 2 fields big-endian). When loaded as four DWORDs from memory in
; little-endian order, the bytes become:
;
;#define EFI_FIRMWARE_FILE_SYSTEM2_GUID \
;  { 0x8c8ce578, 0x8a3d, 0x4f1c, { 0x99, 0x35, 0x89, 0x61, 0x85, 0xc3, 0x2d, 0xd3 } }
%define FFS2_GUID_DWORD0 0x8c8ce578
%define FFS2_GUID_DWORD1 0x4f1c8a3d
%define FFS2_GUID_DWORD2 0x61893599
%define FFS2_GUID_DWORD3 0xd32dc385

;
; EFI_FIRMWARE_FILE_SYSTEM3_GUID — identifies FFS3 (adds large file support)
;   { 0x5473c07a, 0x3dcb, 0x4dca, { 0xbd, 0x6f, 0x1e, 0x96, 0x89, 0xe7, 0x34, 0x9a } }
;
;#define EFI_FIRMWARE_FILE_SYSTEM3_GUID \
;  { 0x8c8ce578, 0x3dcb, 0x4dca, { 0xbd, 0x6f, 0x1e, 0x96, 0x89, 0xe7, 0x34, 0x9a } }
%define FFS3_GUID_DWORD0 0x5473c07a
%define FFS3_GUID_DWORD1 0x4dca3dcb
%define FFS3_GUID_DWORD2 0x961e6fbd
%define FFS3_GUID_DWORD3 0x9a34e789

BITS    32

;============================================================================
; Flat32SearchForBfvBase — Scan flash ROM for the Boot Firmware Volume
;============================================================================
;
; Called from: Main16 (in 32-bit mode) via "OneTimeCall Flat32SearchForBfvBase"
; Returns to:  Main16 via OneTimeCallRet
;              -> jumps to Flat32SearchForBfvBaseOneTimerCallReturn in Main.nasm.inc
;
; Modified:  EAX, EBX
; Preserved: EDI, ESP
;
; @param[out]  EBP  Address of Boot Firmware Volume (BFV) base
;
Flat32SearchForBfvBase:

    ;
    ; EAX serves as our scan pointer. Start at 0 and subtract 0x1000 (4KB)
    ; each iteration, effectively scanning from 0xFFFFF000 downward.
    ; (0 - 0x1000 = 0xFFFFF000 due to unsigned wraparound)
    ;
    xor     eax, eax                    ; EAX = 0 (will wrap to 0xFFFFF000 on first sub)

searchingForBfvHeaderLoop:
    ;
    ; Move to next candidate address (4KB lower).
    ; We check every 4KB-aligned address because firmware volumes are always
    ; at least 4KB-aligned in flash.
    ;
    sub     eax, 0x1000                 ; EAX -= 4096 (move down one page)

    ;
    ; Check if we've scanned below 0xFF000000 (beyond the 16MB range).
    ; The firmware volume must be in the top 16MB of the 4GB address space
    ; (this is where the chipset maps the SPI flash).
    ;
    cmp     eax, 0xff000000             ; Have we gone below 0xFF000000?
    jb      searchedForBfvHeaderButNotFound ; Yes -> give up, BFV not in top 16MB

    ;
    ; === Try FFS3 GUID first ===
    ;
    ; Compare all 16 bytes (4 DWORDs) of the GUID at offset +0x10 in the
    ; candidate firmware volume header. We compare one DWORD at a time.
    ;
    cmp     dword [eax + 0x10], FFS3_GUID_DWORD0  ; GUID bytes 0-3
    jne     searchingForFfs2Guid                    ; Mismatch -> try FFS2
    cmp     dword [eax + 0x14], FFS3_GUID_DWORD1  ; GUID bytes 4-7
    jne     searchingForFfs2Guid
    cmp     dword [eax + 0x18], FFS3_GUID_DWORD2  ; GUID bytes 8-11
    jne     searchingForFfs2Guid
    cmp     dword [eax + 0x1c], FFS3_GUID_DWORD3  ; GUID bytes 12-15
    jne     searchingForFfs2Guid
    jmp     checkingFvLength                        ; All 4 DWORDs match! -> validate length

searchingForFfs2Guid:
    ;
    ; === Try FFS2 GUID ===
    ;
    ; FFS3 didn't match; try FFS2 (older firmware file system format).
    ;
    cmp     dword [eax + 0x10], FFS2_GUID_DWORD0  ; GUID bytes 0-3
    jne     searchingForBfvHeaderLoop               ; Mismatch -> try next 4KB address
    cmp     dword [eax + 0x14], FFS2_GUID_DWORD1  ; GUID bytes 4-7
    jne     searchingForBfvHeaderLoop
    cmp     dword [eax + 0x18], FFS2_GUID_DWORD2  ; GUID bytes 8-11
    jne     searchingForBfvHeaderLoop
    cmp     dword [eax + 0x1c], FFS2_GUID_DWORD3  ; GUID bytes 12-15
    jne     searchingForBfvHeaderLoop

checkingFvLength:
    ;
    ; === Validate Firmware Volume Length ===
    ;
    ; The FvLength field at offset +0x20 is a UINT64 (8 bytes).
    ; For a valid BFV at the top of flash, base_address + FvLength should
    ; equal exactly 0x100000000 (4GB). Since we're in 32-bit mode, this
    ; wraps to 0. So we check:
    ;   1. Upper 32 bits of FvLength (at +0x24) must be 0
    ;   2. base_address + lower_32_bits_of_FvLength must wrap to 0
    ;
    cmp     dword [eax + 0x24], 0       ; Upper 32 bits of FvLength must be 0
    jne     searchingForBfvHeaderLoop    ; Non-zero upper bits -> not our volume

    mov     ebx, eax                    ; EBX = candidate base address
    add     ebx, dword [eax + 0x20]     ; EBX = base + FvLength (lower 32 bits)
                                         ; If this wraps to exactly 0, it means
                                         ; the volume ends at 4GB — correct!
    jnz     searchingForBfvHeaderLoop    ; Not zero? -> volume doesn't end at 4GB,
                                         ;              try next address

    ;
    ; Success! The firmware volume at EAX has a valid GUID and its length
    ; makes it end exactly at the 4GB boundary.
    ;
    jmp     searchedForBfvHeaderAndItWasFound

searchedForBfvHeaderButNotFound:
    ;
    ; FATAL: We scanned the entire top 16MB and found no valid firmware volume.
    ; The system cannot boot. Output a debug POST code and hang forever.
    ;
    debugShowPostCode POSTCODE_BFV_NOT_FOUND  ; POST code 0xB0

    ;
    ; Set EAX and EBP to a recognizable "magic" value for debugging.
    ; If someone attaches a debugger or reads register state, 0xBFBFBFBF
    ; (think "BFV BFV BFV BFV") signals that the BFV search failed.
    ;
    mov     eax, 0xBFBFBFBF
    mov     ebp, eax
    jmp     $                           ; Infinite loop — hang forever

searchedForBfvHeaderAndItWasFound:
    ;
    ; Store the BFV base address in EBP (the output register).
    ; EBP is preserved through the rest of boot and passed to SEC.
    ;
    mov     ebp, eax

    debugShowPostCode POSTCODE_BFV_FOUND    ; POST code 0xB1

    ;
    ; Return to Main16 (32-bit mode).
    ; Jumps to Flat32SearchForBfvBaseOneTimerCallReturn in Main.nasm.inc.
    ;
    OneTimeCallRet Flat32SearchForBfvBase

