;------------------------------------------------------------------------------
; @file
; Search for the SEC Core entry point within the Boot Firmware Volume.
;
; Once the BFV (Boot Firmware Volume) has been found by Flat32SearchForBfvBase,
; this routine walks the FFS (Firmware File System) inside it, looking for
; a file of type EFI_FV_FILETYPE_SECURITY_CORE (0x03). When found, it parses
; the PE32 or TE (Terse Executable) image inside to extract the entry point.
;
; FFS File System Structure:
;   - FV Header (variable length, HeaderLength at offset +0x30)
;   - FFS File 1 (header + sections, 8-byte aligned)
;   - FFS File 2 ...
;   - ...
;
; FFS File Header (24 bytes = 0x18):
;   Offset 0x00: Name[16]     (file GUID)
;   Offset 0x10: IntegrityCheck (2 bytes)
;   Offset 0x12: Type          (1 byte — 0x03 = SEC Core)
;   Offset 0x13: Attributes    (1 byte)
;   Offset 0x14: Size[3]       (24-bit file size, little-endian)
;   Offset 0x17: State         (1 byte — bit 5 set = valid file)
;
; FFS Section Header (4 bytes):
;   Offset 0x00: Size[3]       (24-bit section size)
;   Offset 0x03: Type          (1 byte — 0x10 = PE32, 0x12 = TE)
;
; Copyright (c) 2008 - 2011, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

BITS    32

; FFS file type for SEC Core (the first firmware phase after reset vector)
%define EFI_FV_FILETYPE_SECURITY_CORE         0x03

;============================================================================
; Flat32SearchForSecEntryPoint — Find SEC Core in the BFV's file system
;============================================================================
;
; Called from: Main16 (32-bit mode) via "OneTimeCall Flat32SearchForSecEntryPoint"
; Returns to:  Main16 via OneTimeCallRet
;              -> jumps to Flat32SearchForSecEntryPointOneTimerCallReturn in Main.nasm.inc
;
; Modified:  EAX, EBX, ECX, EDX
; Preserved: EDI, EBP, ESP
;
; @param[in]   EBP  Address of Boot Firmware Volume (BFV) — from previous step
; @param[out]  ESI  SEC Core Entry Point Address
;
Flat32SearchForSecEntryPoint:

    ;
    ; Initialize EBX and ESI to 0.
    ; ESI will hold the SEC entry point when found (or 0 if not found).
    ;
    xor     ebx, ebx                ; EBX = 0 (used for intermediate calculations)
    mov     esi, ebx                 ; ESI = 0 (SEC entry point, not found yet)

    ;
    ; Skip past the FV (Firmware Volume) header to reach the first FFS file.
    ;
    ; The FV header length is a UINT16 at offset +0x30 in the FV header.
    ; EAX = EBP (BFV base) + HeaderLength = address of first FFS file.
    ;
    mov     eax, ebp                 ; EAX = BFV base address
    mov     bx, [ebp + 0x30]        ; BX = FV header length (UINT16)
    add     eax, ebx                 ; EAX = first FFS file address
    jc      secEntryPointWasNotFound ; If addition overflowed, something is very wrong

    jmp     searchingForFfsFileHeaderLoop  ; Start scanning FFS files

;
; moveForwardWhileSearchingForFfsFileHeaderLoop — Advance by 1 byte
;
; This is the "fallback" case: the current address doesn't look like a valid
; FFS file header. Advance by 1 byte and try again. (The 8-byte alignment
; fixup below will snap us to the next valid alignment boundary.)
;
moveForwardWhileSearchingForFfsFileHeaderLoop:
    inc     eax                      ; EAX++ (try next byte)
    jc      secEntryPointWasNotFound ; Overflow past 0xFFFFFFFF -> give up

;============================================================================
; searchingForFfsFileHeaderLoop — Main FFS file scanning loop
;============================================================================
;
; EAX = current candidate address for an FFS file header.
; We iterate through all FFS files in the volume looking for file type 0x03.
;
searchingForFfsFileHeaderLoop:
    test    eax, eax                 ; Is EAX zero (wrapped around)?
    jz      secEntryPointWasNotFound ; Yes -> we've gone past the end

    ;
    ; FFS files must be 8-byte aligned. Round EAX up to next 8-byte boundary:
    ;   add 7, then clear the bottom 3 bits.
    ;
    add     eax, 7                   ; Round up: EAX = EAX + 7
    jc      secEntryPointWasNotFound ; Overflow -> give up
    and     al, 0xf8                 ; Clear bottom 3 bits (AND with 11111000b)
                                     ; Now EAX is aligned to 8 bytes

    ;
    ; Check if there's a valid FFS file at the current address.
    ;
    ; First, check the State field (offset +0x17, byte). Bit 5 of State
    ; indicates the file is valid (EFI_FILE_DATA_VALID = 0x20). Without
    ; this bit set, it's not a properly written file.
    ;
    mov     bl, [eax + 0x17]         ; BL = FFS file State byte
    test    bl, 0x20                  ; Is bit 5 (DATA_VALID) set?
    jz      moveForwardWhileSearchingForFfsFileHeaderLoop  ; No -> skip, not valid

    ;
    ; Read the file size (3-byte little-endian field at offset +0x14).
    ; We load 4 bytes and mask off the top byte to get a 24-bit value.
    ;
    mov     ecx, [eax + 0x14]        ; ECX = Size[3] + State byte (4 bytes)
    and     ecx, 0x00ffffff           ; Mask to 24 bits = file size in bytes

    ;
    ; Sanity check: file size must be non-zero.
    ;
    or      ecx, ecx                 ; Is file size zero?
    jz      moveForwardWhileSearchingForFfsFileHeaderLoop  ; Yes -> invalid, skip

    ;
    ; Compute the end address of this FFS file.
    ; ECX = EAX (file start) + file size = address of next file (or end of volume)
    ;
    add     ecx, eax                 ; ECX = end of this FFS file
    jz      jumpSinceWeFoundTheLastFfsFile  ; If ECX wrapped to 0, this is the last file
    jc      moveForwardWhileSearchingForFfsFileHeaderLoop  ; Overflow -> invalid

jumpSinceWeFoundTheLastFfsFile:

    ;
    ; We have a seemingly valid FFS file at EAX with end at ECX.
    ; Check if this file's Type (offset +0x12) is SEC Core (0x03).
    ;
    cmp     byte [eax + 0x12], EFI_FV_FILETYPE_SECURITY_CORE ; Type == 0x03?
    jne     readyToTryFfsFileAtEcx    ; Not SEC Core -> skip to next file

;============================================================================
; fileTypeIsSecCore — Found a SEC Core file! Extract its entry point.
;============================================================================
;
; The SEC Core FFS file has been found at EAX. Now we need to parse the
; PE32/TE image within it to find the executable entry point.
;
fileTypeIsSecCore:
    ;
    ; Call GetEntryPointOfFfsFile to parse the PE32 or TE image.
    ;
    ; Input:  EAX = start of FFS file, ECX = end of FFS file
    ; Output: EAX = entry point address (or 0 if parsing failed)
    ;
    ; This is a OneTimeCall — it can only be called once in the boot sequence.
    ; Returns to fileTypeIsSecCoreOneTimerCallReturn (right after this line ↓)
    ;
    OneTimeCall GetEntryPointOfFfsFile

    ;
    ; Check if GetEntryPointOfFfsFile succeeded.
    ;
    test    eax, eax                 ; Is EAX non-zero (valid entry point)?
    jnz     doneSeachingForSecEntryPoint  ; Yes! We found it

readyToTryFfsFileAtEcx:
    ;
    ; This FFS file wasn't SEC Core (or failed to parse).
    ; Move to the next FFS file. ECX points to just past this file.
    ;
    mov     eax, ecx                 ; EAX = start of next FFS file
    jmp     searchingForFfsFileHeaderLoop  ; Continue scanning

;============================================================================
; secEntryPointWasNotFound — No SEC Core in the BFV (fatal error)
;============================================================================
secEntryPointWasNotFound:
    xor     eax, eax                 ; EAX = 0 (no entry point)

doneSeachingForSecEntryPoint:
    ;
    ; Store the result in ESI (the output register).
    ;
    mov     esi, eax                 ; ESI = SEC entry point (or 0 if not found)

    test    esi, esi                 ; Did we find the SEC entry point?
    jnz     secCoreEntryPointWasFound ; Yes -> success

secCoreEntryPointWasNotFound:
    ;
    ; FATAL: SEC Core not found. The system cannot boot.
    ; Output debug POST code and hang forever.
    ;
    debugShowPostCode POSTCODE_SEC_NOT_FOUND  ; POST code 0xF0
    jz      $                         ; Infinite loop — hang forever
                                      ; (jz is always taken here because
                                      ; test esi,esi set ZF=1 when ESI=0)

secCoreEntryPointWasFound:
    debugShowPostCode POSTCODE_SEC_FOUND      ; POST code 0xF1

    ;
    ; Return to Main16 (32-bit mode).
    ; Jumps to Flat32SearchForSecEntryPointOneTimerCallReturn in Main.nasm.inc.
    ;
    OneTimeCallRet Flat32SearchForSecEntryPoint


;============================================================================
; Section type constants for PE32 and TE images within FFS files.
;============================================================================
;
; An FFS file contains one or more "sections." Each section has a 4-byte
; header (3 bytes size + 1 byte type). The actual executable image is inside
; a section of type PE32 (0x10) or TE (0x12).
;
%define EFI_SECTION_PE32                  0x10  ; Standard PE32/PE32+ image
%define EFI_SECTION_TE                    0x12  ; Terse Executable (stripped PE)

;============================================================================
; GetEntryPointOfFfsFile — Parse PE32/TE image to find entry point address
;============================================================================
;
; Called from: fileTypeIsSecCore via "OneTimeCall GetEntryPointOfFfsFile"
; Returns to:  fileTypeIsSecCore via OneTimeCallRet
;              -> jumps to GetEntryPointOfFfsFileOneTimerCallReturn
;              (right after the OneTimeCall in the code above)
;
; This function iterates through the sections of an FFS file, finds the
; PE32 or TE section, and extracts the executable entry point from the
; image header.
;
; Input:
;   EAX - Start of FFS file (points to FFS file header)
;   ECX - End of FFS file (one past last byte)
;
; Output:
;   EAX - Entry point of the executable (or 0 if not found)
;
; Modified:
;   EBX
;
GetEntryPointOfFfsFile:
    ;
    ; Sanity check: is the file start address valid?
    ;
    test    eax, eax                       ; Is EAX zero?
    jz      getEntryPointOfFfsFileErrorReturn  ; Yes -> invalid, return 0

    ;
    ; Skip past the FFS file header (0x18 = 24 bytes) to reach the first section.
    ; The FFS file header is: GUID(16) + IntegrityCheck(2) + Type(1) +
    ; Attributes(1) + Size(3) + State(1) = 24 bytes.
    ;
    add     eax, 0x18                      ; EAX = address of first section header

;============================================================================
; getEntryPointOfFfsFileLoopForSections — Iterate through sections
;============================================================================
;
; Each section has a 4-byte header:
;   Offset 0x00: Size[3] (24-bit little-endian section size, includes header)
;   Offset 0x03: Type    (1 byte)
;
getEntryPointOfFfsFileLoopForSections:
    ;
    ; Check if we've gone past the end of the FFS file.
    ;
    cmp     eax, ecx                       ; Current pos >= end of file?
    jae     getEntryPointOfFfsFileErrorReturn  ; Yes -> no more sections, give up

    ;
    ; Check the section type. We're looking for PE32 (0x10) or TE (0x12).
    ; Section type is at offset +3 in the section header.
    ;
    cmp     byte [eax + 3], EFI_SECTION_PE32   ; Is it a PE32 section?
    je      getEntryPointOfFfsFileFoundPe32Section  ; Yes!

    cmp     byte [eax + 3], EFI_SECTION_TE     ; Is it a TE section?
    je      getEntryPointOfFfsFileFoundTeSection    ; Yes!

    ;
    ; Neither PE32 nor TE. Skip this section and try the next one.
    ;
    ; Read the 24-bit section size to know how far to advance.
    ;
    mov     ebx, dword [eax]               ; Load 4 bytes (Size[3] + Type)
    and     ebx, 0x00ffffff                 ; Mask to 24-bit size
    add     eax, ebx                        ; Advance past this section
    jc      getEntryPointOfFfsFileErrorReturn  ; Overflow -> give up

    ;
    ; FFS sections must be 4-byte aligned. Round up to next 4-byte boundary.
    ;
    add     eax, 3                          ; Round up: EAX + 3
    jc      getEntryPointOfFfsFileErrorReturn  ; Overflow -> give up
    and     al, 0xfc                        ; Clear bottom 2 bits (align to 4)
    jmp     getEntryPointOfFfsFileLoopForSections  ; Try next section

;============================================================================
; getEntryPointOfFfsFileFoundPe32Section — Parse PE32 (standard PE) image
;============================================================================
;
; PE32 image layout:
;   Offset 0x00: DOS Header — starts with 'MZ' (0x4D5A)
;   Offset 0x3C: e_lfanew — UINT16 offset to PE signature
;   PE Signature: 'PE\0\0' (0x50450000)
;   After signature: COFF header (20 bytes) + Optional header
;   Optional header offset +0x10 (from opt hdr start): AddressOfEntryPoint (RVA)
;
; The entry point is: image_base + AddressOfEntryPoint
; (but since it's loaded in ROM, image_base = address of 'MZ' in memory)
;
getEntryPointOfFfsFileFoundPe32Section:
    add     eax, 4                          ; Skip 4-byte section header -> start of PE image

    ;
    ; Verify DOS header magic 'MZ'
    ;
    cmp     word [eax], 'MZ'                ; Check for DOS header signature
    jne     getEntryPointOfFfsFileErrorReturn  ; Not 'MZ' -> not a valid PE

    ;
    ; Follow e_lfanew to find the PE signature.
    ; e_lfanew is a UINT16 at offset 0x3C in the DOS header, giving the
    ; offset from the start of the DOS header to the PE signature.
    ;
    movzx   ebx, word [eax + 0x3c]         ; EBX = e_lfanew (offset to PE sig)
    add     ebx, eax                        ; EBX = absolute address of PE signature

    ;
    ; Verify PE signature 'PE\0\0'
    ;
    ; The backtick syntax in NASM allows escape sequences: `PE\x00\x00`
    ; produces the 4-byte string 50h 45h 00h 00h.
    ;
    cmp     dword [ebx], `PE\x00\x00`      ; Check for PE signature
    jne     getEntryPointOfFfsFileErrorReturn  ; Not PE -> invalid image

    ;
    ; Extract the entry point address.
    ;
    ; PE header layout after the 4-byte "PE\0\0" signature:
    ;   +0x04: COFF header (20 bytes = 0x14)
    ;     +0x04 + 0x14 = +0x18: Optional header starts here
    ;       +0x18 + 0x10 = +0x28: AddressOfEntryPoint (RVA, 4 bytes)
    ;
    ; Simplified: [EBX + 0x4 + 0x14 + 0x10] = AddressOfEntryPoint
    ;
    ; The entry point RVA is relative to the image base. Since the image is
    ; in ROM at address EAX, the absolute entry point = EAX + RVA.
    ;
    add     eax, [ebx + 0x4 + 0x14 + 0x10] ; EAX = image_base + AddressOfEntryPoint
    jmp     getEntryPointOfFfsFileReturn     ; Return with entry point in EAX

;============================================================================
; getEntryPointOfFfsFileFoundTeSection — Parse TE (Terse Executable) image
;============================================================================
;
; TE is a simplified/stripped PE format used in UEFI to save space.
; It replaces the DOS header + PE header with a compact 40-byte TE header.
;
; TE Header (40 bytes = 0x28):
;   Offset 0x00: Signature     (2 bytes — 'VZ' = 0x5A56)
;   Offset 0x02: Machine       (2 bytes)
;   Offset 0x04: NumberOfSections (1 byte)
;   Offset 0x06: StrippedSize  (2 bytes — bytes removed from original PE)
;   Offset 0x08: AddressOfEntryPoint (4 bytes — RVA from original PE base)
;   ...
;
; Entry point calculation:
;   absolute_entry = image_address + AddressOfEntryPoint
;                    + sizeof(EFI_TE_IMAGE_HEADER)  [0x28]
;                    - StrippedSize
;
; The StrippedSize adjustment is needed because the original PE's entry point
; RVA is relative to the start of the PE headers, which were stripped.
; We add the TE header size and subtract the stripped portion to compensate.
;
getEntryPointOfFfsFileFoundTeSection:
    add     eax, 4                          ; Skip 4-byte section header -> start of TE image
    mov     ebx, eax                        ; EBX = start of TE image

    ;
    ; Verify TE signature 'VZ'
    ;
    cmp     word [ebx], 'VZ'                ; Check for TE signature
    jne     getEntryPointOfFfsFileErrorReturn  ; Not 'VZ' -> not a valid TE

    ;
    ; Calculate entry point:
    ;   EAX = image_address + AddressOfEntryPoint + 0x28 - StrippedSize
    ;
    add     eax, [ebx + 0x8]               ; EAX += AddressOfEntryPoint (RVA)
    add     eax, 0x28                       ; EAX += sizeof(EFI_TE_IMAGE_HEADER)
    movzx   ebx, word [ebx + 0x6]          ; EBX = StrippedSize (16-bit, zero-extended)
    sub     eax, ebx                        ; EAX -= StrippedSize (adjust for stripped PE hdrs)
    jmp     getEntryPointOfFfsFileReturn    ; Return with entry point in EAX

;============================================================================
; Error and return paths
;============================================================================

getEntryPointOfFfsFileErrorReturn:
    mov     eax, 0                          ; Return 0 = entry point not found

getEntryPointOfFfsFileReturn:
    ;
    ; Return to caller (fileTypeIsSecCore).
    ; Jumps to GetEntryPointOfFfsFileOneTimerCallReturn, which is right after
    ; the "OneTimeCall GetEntryPointOfFfsFile" line above.
    ;
    OneTimeCallRet GetEntryPointOfFfsFile

