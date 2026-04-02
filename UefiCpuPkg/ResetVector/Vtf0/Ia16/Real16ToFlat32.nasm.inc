;------------------------------------------------------------------------------
; @file
; Transition from 16-bit real mode into 32-bit flat protected mode.
;
; This is one of the most critical mode transitions in x86 boot. The CPU
; starts in 16-bit real mode (8086-compatible) and must be switched to
; 32-bit protected mode to access the full 4GB address space.
;
; Steps performed:
;   1. Disable interrupts (no IDT exists yet)
;   2. Load the GDT (Global Descriptor Table) defining flat segments
;   3. Set CR0.PE (Protection Enable) to enter protected mode
;   4. Far jump to flush the instruction pipeline and load 32-bit CS
;   5. Set CR4 (enable MCE, SSE support)
;   6. Load all data segment registers with flat 0-4GB selectors
;
; Copyright (c) 2008 - 2022, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

;
; CR0 value for SEC phase entry: 0x00000023
;   Bit 0 (PE)  = 1: Protection Enable — switches to protected mode
;   Bit 1 (MP)  = 1: Monitor Coprocessor — required when using x87 FPU
;   Bit 5 (NE)  = 1: Numeric Error — use native FPU error reporting
;   All other bits 0: no paging (PG=0), caching enabled (CD=0, NW=0)
;
%define SEC_DEFAULT_CR0  0x00000023

;
; CR4 value: 0x640
;   Bit 6  (MCE)        = 1: Machine-Check Enable — enable MCE exceptions
;   Bit 9  (OSFXSR)     = 1: OS supports FXSAVE/FXRSTOR (SSE state save)
;   Bit 10 (OSXMMEXCPT) = 1: OS supports unmasked SIMD FP exceptions (#XM)
;   Bit 5  (PAE)        = 0: PAE not enabled yet (done later for x64)
;
%define SEC_DEFAULT_CR4  0x640

BITS    16

;============================================================================
; TransitionFromReal16To32BitFlat — Switch CPU from real mode to protected mode
;============================================================================
;
; Called from: Main16 via "OneTimeCall TransitionFromReal16To32BitFlat"
; Returns to:  Main16 via OneTimeCallRet
;              -> jumps to TransitionFromReal16To32BitFlatOneTimerCallReturn
;              (the label right after the OneTimeCall in Main.nasm.inc)
;
; @param[out]     DS       Selector allowing flat access to all addresses
; @param[out]     ES       Selector allowing flat access to all addresses
; @param[out]     FS       Selector allowing flat access to all addresses
; @param[out]     GS       Selector allowing flat access to all addresses
; @param[out]     SS       Selector allowing flat access to all addresses
; Modified:  EAX, EBX
;
TransitionFromReal16To32BitFlat:

    debugShowPostCode POSTCODE_16BIT_MODE   ; POST code 0x16 — we're in 16-bit mode
                                            ; Expands to: mov al, 0x16; out 0x80, al

    ;
    ; Disable interrupts. This is CRITICAL: we're about to switch modes and
    ; there is no Interrupt Descriptor Table (IDT). Any interrupt (hardware
    ; timer, keyboard, etc.) would cause a triple fault and reset the CPU.
    ;
    cli

    ;
    ; Set DS = 0xF000 so we can address the GDT data structure.
    ;
    ; In the CPU's special reset state, CS.base = 0xFFFF0000 (accessing the
    ; top of the 4GB space), but DS is just 0x0000. We need DS to point to
    ; our code/data area to load the GDTR structure. Setting DS = 0xF000
    ; gives DS.base = 0xF000 * 16 = 0xF0000 in normal real mode, but
    ; combined with the segment override (cs:) on the lgdt instruction,
    ; we actually use CS to access the GDT data. DS=0xF000 is set here
    ; for consistency with the segment layout.
    ;
    mov     bx, 0xf000
    mov     ds, bx

    ;
    ; BX = 16-bit offset of the GDTR structure within the 0xF000 segment.
    ; ADDR16_OF(gdtr) computes the offset of the 'gdtr' label within the
    ; reset vector code's 64KB segment.
    ;
    mov     bx, ADDR16_OF(gdtr)

    ;
    ; Load the Global Descriptor Table Register (GDTR).
    ;
    ; lgdt reads a 6-byte structure from memory:
    ;   Bytes 0-1: GDT limit (size - 1 in bytes)
    ;   Bytes 2-5: GDT linear base address
    ;
    ; The "o32" prefix is CRITICAL here: in 16-bit mode, lgdt normally reads
    ; only a 3-byte (24-bit) base address. With o32, it reads a full 4-byte
    ; (32-bit) base address, which we need because our GDT is in the
    ; 0xFFxxxxxx address range (top of 4GB).
    ;
    ; "cs:" is a segment override — read from the code segment, not DS.
    ; This works because CS.base = 0xFFFF0000 in reset state, pointing to
    ; the correct ROM area.
    ;
o32 lgdt    [cs:bx]

    ;
    ; Enable protected mode by setting CR0.PE (bit 0).
    ;
    ; SEC_DEFAULT_CR0 = 0x23 sets PE, MP, and NE bits.
    ; After this instruction, the CPU is in protected mode, but the old
    ; real-mode CS is still cached. We MUST do a far jump immediately
    ; to load a proper protected-mode code segment selector.
    ;
    mov     eax, SEC_DEFAULT_CR0
    mov     cr0, eax

    ;
    ; Far jump: the critical "mode activation" instruction.
    ;
    ; Even though CR0.PE is set, the CPU's code segment is still using the
    ; old real-mode cached descriptor. This far jump loads CS with
    ; LINEAR_CODE_SEL (0x10), which points to the 32-bit flat code segment
    ; descriptor in our GDT. This:
    ;   1. Flushes the instruction prefetch queue
    ;   2. Reloads CS with the 32-bit code segment descriptor
    ;   3. Transitions instruction decoding to 32-bit mode
    ;
    ; The "dword" keyword forces a 32-bit offset in the far jump operand,
    ; which is needed because the target address is in the 0xFFxxxxxx range
    ; (can't fit in 16 bits).
    ;
    jmp     LINEAR_CODE_SEL:dword ADDR_OF(jumpTo32BitAndLandHere)

; ---- From here on, we are in 32-bit protected mode ----
BITS    32
jumpTo32BitAndLandHere:

    ;
    ; Set additional control register flags now that we're in protected mode.
    ; CR4 = 0x640 enables:
    ;   MCE (bit 6):        Machine-Check Exception support
    ;   OSFXSR (bit 9):     OS supports FXSAVE/FXRSTOR for SSE state
    ;   OSXMMEXCPT (bit 10): OS handles unmasked SIMD FP exceptions
    ;
    mov     eax, SEC_DEFAULT_CR4
    mov     cr4, eax

    debugShowPostCode POSTCODE_32BIT_MODE   ; POST code 0x32 — we're in 32-bit mode

    ;
    ; Load all data segment registers with LINEAR_SEL (0x18), the flat
    ; data segment selector. This gives all segments a base of 0 and a
    ; limit of 4GB, so any 32-bit address can be accessed through any
    ; segment register. This is the "flat memory model."
    ;
    mov     ax, LINEAR_SEL      ; AX = 0x18 (flat data segment selector)
    mov     ds, ax              ; Data Segment = flat
    mov     es, ax              ; Extra Segment = flat
    mov     fs, ax              ; Additional segment = flat
    mov     gs, ax              ; Additional segment = flat
    mov     ss, ax              ; Stack Segment = flat (though we have no stack yet)

    ;
    ; Return to Main16 (now running in 32-bit mode).
    ; Jumps to TransitionFromReal16To32BitFlatOneTimerCallReturn in Main.nasm.inc.
    ;
    OneTimeCallRet TransitionFromReal16To32BitFlat

;============================================================================
; GDT (Global Descriptor Table)
;============================================================================
;
; The GDT defines memory segments for protected mode. In real mode, segments
; are simply base_address = segment_register * 16. In protected mode, segment
; registers hold "selectors" — indices into the GDT, where each entry defines
; base address, limit, and access permissions.
;
; This GDT uses a "flat" model: all segments have base=0 and limit=4GB,
; giving a linear address space where segment:offset == physical address.
;
; The GDTR (GDT Register) structure loaded by lgdt:
;
ALIGN   2

gdtr:
    dw      GDT_END - GDT_BASE - 1   ; GDT limit = total size in bytes - 1
    dd      ADDR_OF(GDT_BASE)         ; GDT base = physical address of GDT_BASE

ALIGN   16

;
; Macros to build the access byte and flags byte of GDT descriptors.
;
; Access byte (byte 5 of descriptor):
;   Bit 7: Present (P) — must be 1 for valid segments
;   Bits 6-5: DPL (Descriptor Privilege Level) — 0 = kernel, 3 = user
;   Bit 4: System (S) — 1 = code/data segment, 0 = system (TSS, gate)
;   Bits 3-0: Type — specific type of code or data segment
;
%define  PRESENT_FLAG(p) (p << 7)
%define  DPL(dpl) (dpl << 5)
%define  SYSTEM_FLAG(s) (s << 4)
%define  DESC_TYPE(t) (t)

; Type field values for code and data segments:
; Bit 3: Executable (1=code, 0=data)
; Bit 2: Direction/Conforming
; Bit 1: Readable (code) or Writable (data)
; Bit 0: Accessed

; Type 0x3 = Data: expand-up, writable, accessed (0b0011)
%define  DATA32_TYPE 3

; Type 0xB = Code: execute, readable, non-conforming, accessed (0b1011)
%define  CODE32_TYPE 0xb

; Type 0xB = same for 64-bit code (the L flag in flags byte differentiates)
%define  CODE64_TYPE 0xb

;
; Flags byte (upper nibble of byte 6):
;   Bit 7: Granularity (G) — 1 = limit in 4KB pages, 0 = in bytes
;   Bit 6: D/B (Default size) — 1 = 32-bit, 0 = 16-bit
;   Bit 5: L (Long mode) — 1 = 64-bit code segment (D must be 0)
;   Bit 4: Available for OS use
; Lower nibble of byte 6 is Limit[19:16].
;
%define  GRANULARITY_FLAG(g) (g << 7)
%define  DEFAULT_SIZE32(d) (d << 6)
%define  CODE64_FLAG(l) (l << 5)
%define  UPPER_LIMIT(l) (l)

;============================================================================
; GDT Entries
;============================================================================
;
; Each descriptor is 8 bytes. The selector value is the byte offset from
; GDT_BASE (computed at assembly time with `equ $-GDT_BASE`).
;

GDT_BASE:

;--- Selector 0x00: Null Descriptor (required by x86 architecture) ---
; The CPU requires the first GDT entry to be all zeros.
; Loading a segment register with selector 0x00 causes a #GP if accessed.
NULL_SEL            equ $-GDT_BASE    ; Selector [0x0]
    DW      0            ; limit 15:0
    DW      0            ; base 15:0
    DB      0            ; base 23:16
    DB      0            ; sys flag, dpl, type
    DB      0            ; limit 19:16, flags
    DB      0            ; base 31:24

;--- Selector 0x08: Spare (unused, reserved for future use) ---
SPARE1_SEL          equ $-GDT_BASE    ; Selector [0x8]
    DW      0            ; limit 15:0
    DW      0            ; base 15:0
    DB      0            ; base 23:16
    DB      0            ; sys flag, dpl, type
    DB      0            ; limit 19:16, flags
    DB      0            ; base 31:24

;--- Selector 0x10: 32-bit Code Segment (LINEAR_CODE_SEL) ---
; Base=0, Limit=4GB, Execute/Read, Ring 0, 32-bit default operand size.
; Access byte = 0x9B: Present=1, DPL=0, S=1, Type=0xB (Code, Read, Accessed)
; Flags = 0xCF: Granularity=1 (4KB), D=1 (32-bit), L=0, Limit[19:16]=0xF
; Combined limit = 0xFFFFF * 4KB = 4GB
LINEAR_CODE_SEL     equ $-GDT_BASE    ; Selector [0x10]
    DW      0xffff       ; limit 15:0  = 0xFFFF
    DW      0            ; base 15:0   = 0
    DB      0            ; base 23:16  = 0
    DB      PRESENT_FLAG(1)|DPL(0)|SYSTEM_FLAG(1)|DESC_TYPE(CODE32_TYPE)           ; 0x9B
    DB      GRANULARITY_FLAG(1)|DEFAULT_SIZE32(1)|CODE64_FLAG(0)|UPPER_LIMIT(0xf)  ; 0xCF
    DB      0            ; base 31:24  = 0

;--- Selector 0x18: 32-bit Data Segment (LINEAR_SEL) ---
; Base=0, Limit=4GB, Read/Write, Ring 0, 32-bit.
; Access byte = 0x93: Present=1, DPL=0, S=1, Type=0x3 (Data, Write, Accessed)
; Used for DS, ES, FS, GS, SS in 32-bit protected mode.
LINEAR_SEL          equ $-GDT_BASE    ; Selector [0x18]
    DW      0xffff       ; limit 15:0  = 0xFFFF
    DW      0            ; base 15:0   = 0
    DB      0            ; base 23:16  = 0
    DB      PRESENT_FLAG(1)|DPL(0)|SYSTEM_FLAG(1)|DESC_TYPE(DATA32_TYPE)           ; 0x93
    DB      GRANULARITY_FLAG(1)|DEFAULT_SIZE32(1)|CODE64_FLAG(0)|UPPER_LIMIT(0xf)  ; 0xCF
    DB      0            ; base 31:24  = 0

;--- Selector 0x20: Spare (unused, reserved for future use) ---
SPARE2_SEL          equ $-GDT_BASE    ; Selector [0x20]
    DW      0            ; limit 15:0
    DW      0            ; base 15:0
    DB      0            ; base 23:16
    DB      0            ; sys flag, dpl, type
    DB      0            ; limit 19:16, flags
    DB      0            ; base 31:24

;--- Selector 0x28: 16-bit Code Segment (LINEAR_CODE16_SEL) ---
; Base=0, Limit=4GB, Execute/Read, Ring 0, 16-bit default operand size.
; Same as LINEAR_CODE_SEL but D=0 (16-bit). Used if returning to 16-bit
; protected mode is needed (e.g., for SMM or compatibility transitions).
; Flags = 0x8F: Granularity=1, D=0 (16-bit), L=0, Limit[19:16]=0xF
LINEAR_CODE16_SEL   equ $-GDT_BASE    ; Selector [0x28]
    DW      0xffff       ; limit 15:0
    DW      0            ; base 15:0
    DB      0            ; base 23:16
    DB      PRESENT_FLAG(1)|DPL(0)|SYSTEM_FLAG(1)|DESC_TYPE(CODE32_TYPE)           ; 0x9B
    DB      GRANULARITY_FLAG(1)|DEFAULT_SIZE32(0)|CODE64_FLAG(0)|UPPER_LIMIT(0xf)  ; 0x8F
    DB      0            ; base 31:24

;--- Selector 0x30: 16-bit Data Segment (LINEAR_DATA16_SEL) ---
; Base=0, Limit=64KB (no granularity), Read/Write, Ring 0, 16-bit.
; Note: This segment has Granularity=0, so limit is in bytes (0xFFFF = 64KB).
; Flags byte = 0x00: G=0, D=0, L=0, Limit[19:16]=0
LINEAR_DATA16_SEL   equ $-GDT_BASE    ; Selector [0x30]
    DW      0xffff       ; limit 15:0 = 0xFFFF
    DW      0            ; base 15:0
    DB      0            ; base 23:16
    DB      PRESENT_FLAG(1)|DPL(0)|SYSTEM_FLAG(1)|DESC_TYPE(DATA32_TYPE)           ; 0x93
    DB      0            ; flags = 0x00 (G=0, D=0), limit 19:16 = 0
    DB      0            ; base 31:24

;--- Selector 0x38: 64-bit Code Segment (LINEAR_CODE64_SEL) ---
; Base=0, Limit=4GB, Execute/Read, Ring 0, 64-bit (Long Mode).
; The L=1 flag (bit 5 of flags) marks this as a 64-bit code segment.
; When CS is loaded with this selector (via far jump), the CPU executes
; 64-bit instructions. D must be 0 when L=1.
; Flags = 0xAF: Granularity=1, D=0, L=1 (64-bit!), Limit[19:16]=0xF
LINEAR_CODE64_SEL   equ $-GDT_BASE    ; Selector [0x38]
    DW      0xffff       ; limit 15:0 = 0xFFFF
    DW      0            ; base 15:0
    DB      0            ; base 23:16
    DB      PRESENT_FLAG(1)|DPL(0)|SYSTEM_FLAG(1)|DESC_TYPE(CODE64_TYPE)           ; 0x9B
    DB      GRANULARITY_FLAG(1)|DEFAULT_SIZE32(0)|CODE64_FLAG(1)|UPPER_LIMIT(0xf)  ; 0xAF
    DB      0            ; base 31:24

GDT_END:

