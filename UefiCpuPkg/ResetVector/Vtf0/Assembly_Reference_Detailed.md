# x86 Assembly Detailed Reference — Reset Vector Instructions

Complete reference for every instruction, directive, macro, and construct used in
the edk2 x86 reset vector code. Organized by category.

---

## Table of Contents

1. [NASM-Specific Constructs](#1-nasm-specific-constructs)
2. [Data Movement Instructions](#2-data-movement-instructions)
3. [Arithmetic & Logic Instructions](#3-arithmetic--logic-instructions)
4. [Comparison & Test Instructions](#4-comparison--test-instructions)
5. [Branch / Jump Instructions](#5-branch--jump-instructions)
6. [System & Privileged Instructions](#6-system--privileged-instructions)
7. [I/O Instructions](#7-io-instructions)
8. [NASM Pseudo-Instructions & Directives](#8-nasm-pseudo-instructions--directives)
9. [Custom Macros in This Codebase](#9-custom-macros-in-this-codebase)
10. [x86 CPU Modes & Transitions](#10-x86-cpu-modes--transitions)
11. [GDT Segment Descriptor Format](#11-gdt-segment-descriptor-format)
12. [Page Table Structures](#12-page-table-structures)

---

## 1. NASM-Specific Constructs

### Assembler: NASM (Netwide Assembler)

The edk2 reset vector uses **NASM** syntax, not the AT&T syntax used by GNU
assembler. Key differences from AT&T/GAS:

- **Intel syntax**: `mov dst, src` (destination first), not `mov src, dst`
- **No register prefixes**: `eax` not `%eax`
- **No size suffixes**: `mov eax, [addr]` not `movl (%addr), %eax`
- **Square brackets for memory**: `[eax + 4]` means "memory at address eax+4"
- **Immediates are bare**: `mov eax, 42` not `mov $42, %eax`

### File Extensions

- `.nasmb` — NASM source that may use C preprocessor (`#include`, `#if`)
- `.nasm.inc` — NASM include file (pure NASM, no C preprocessor)
- `.inc` — NASM include file (macros, constants)

---

## 2. Data Movement Instructions

### `mov dst, src`
Copy the value of `src` into `dst`. Does NOT affect flags.

```nasm
mov  eax, 0x23       ; Load immediate value into EAX
mov  cr0, eax        ; Copy EAX into control register CR0
mov  esp, eax        ; Copy EAX into ESP
mov  di, 'BP'        ; Load ASCII 'B','P' (0x5042) into DI
mov  ds, ax          ; Load segment register from AX
mov  bx, [ebp+0x30]  ; Load 16-bit value from memory at [EBP+0x30]
```

Operand combinations:
- reg <- reg, reg <- imm, reg <- mem
- mem <- reg, mem <- imm
- Cannot do mem <- mem (use two movs via a register)

When used with segment registers (CS, DS, ES, FS, GS, SS), only 16-bit
register or memory sources are allowed.

### `movzx dst, src`
**Move with Zero Extension.** Copies a smaller source into a larger destination,
filling the upper bits with zeros.

```nasm
movzx ebx, word [eax + 0x3c]  ; Load 16-bit value into 32-bit reg, zero-extend
movzx ebx, word [ebx + 0x6]   ; Same — upper 16 bits of EBX become 0
```

Used in this code to safely widen 16-bit PE header fields to 32-bit registers.

### `xor reg, reg`
Although this is a logic instruction, `xor eax, eax` is the idiomatic way to
zero a register. It's shorter (2 bytes vs 5 for `mov eax, 0`) and faster.

```nasm
xor  eax, eax   ; EAX = 0. Also clears CF, OF, sets ZF=1
xor  ebx, ebx   ; EBX = 0
```

---

## 3. Arithmetic & Logic Instructions

### `add dst, src`
`dst = dst + src`. Sets CF (carry), ZF (zero), SF (sign), OF (overflow).

```nasm
add  eax, ebx      ; EAX = EAX + EBX
add  eax, 7        ; EAX = EAX + 7
add  eax, 0x18     ; EAX = EAX + 24 (skip FFS file header)
add  ebx, eax      ; EBX = EBX + EAX
```

The `jc` (jump if carry) after `add` is used to detect 32-bit overflow — if
adding caused a wraparound past 0xFFFFFFFF, CF is set.

### `sub dst, src`
`dst = dst - src`. Sets CF (borrow), ZF, SF, OF.

```nasm
sub  eax, 0x1000   ; EAX = EAX - 4096 (move down one page)
sub  eax, ebx      ; EAX = EAX - EBX
```

### `inc dst`
`dst = dst + 1`. Sets ZF, SF, OF but does NOT affect CF.

```nasm
inc  eax   ; EAX = EAX + 1 (advance by one byte in search loop)
```

### `and dst, src`
Bitwise AND. Result stored in `dst`. Sets ZF if result is zero.

```nasm
and  al, 0xf8       ; Clear bottom 3 bits (align down to 8-byte boundary)
and  al, 0xfc       ; Clear bottom 2 bits (align down to 4-byte boundary)
and  ecx, 0x00ffffff ; Mask to 24 bits (extract 3-byte FFS file size field)
and  rsi, rax        ; Zero upper 32 bits of RSI (where RAX = 0xFFFFFFFF)
```

### `or dst, src`
Bitwise OR. Result stored in `dst`. Sets ZF if result is zero.

```nasm
or   ecx, ecx   ; Same as "test ecx, ecx" — sets ZF if ECX==0, no other effect
```

### `bt src, bit`
**Bit Test.** Copies bit number `bit` of `src` into the Carry Flag (CF).
Does not modify `src`.

```nasm
bt   ecx, 16   ; CF = bit 16 of ECX (check 5-level paging support from CPUID)
```

### `bts dst, bit`
**Bit Test and Set.** Copies bit number `bit` of `dst` into CF, then sets that
bit to 1 in `dst`.

```nasm
bts  eax, 5    ; CF = old bit 5 of EAX, then set bit 5 (PAE enable in CR4)
bts  eax, 8    ; CF = old bit 8, then set bit 8 (LME in IA32_EFER)
bts  eax, 31   ; CF = old bit 31, then set bit 31 (PG in CR0)
bts  eax, 12   ; CF = old bit 12, then set bit 12 (LA57 for 5-level paging)
```

This is preferred over `or eax, (1 << N)` because it's more readable and
doesn't require a large immediate constant.

---

## 4. Comparison & Test Instructions

### `cmp a, b`
Computes `a - b` but throws away the result. Only the flags are affected.
Used before conditional jumps.

```nasm
cmp  eax, 0xff000000              ; Is EAX below the 16MB scan range?
cmp  dword [eax + 0x10], FFS3_GUID_DWORD0  ; Does GUID match?
cmp  word [eax], 'MZ'             ; Is this a PE32 image? (DOS header magic)
cmp  dword [ebx], `PE\x00\x00`   ; PE signature check
cmp  byte [eax + 0x12], 0x03     ; Is file type SEC Core?
cmp  eax, 07h                     ; Does CPUID support leaf 7?
```

### `test a, b`
Computes `a AND b` but throws away the result. Only flags are affected.
Common pattern: `test eax, eax` to check if EAX is zero (sets ZF).

```nasm
test  bl, 0x20     ; Check if bit 5 is set in FFS file state
test  eax, eax     ; Is EAX zero?
test  esi, esi     ; Is ESI zero? (did we find SEC?)
test  al, LSR_TXRDY ; Is serial TX ready?
```

---

## 5. Branch / Jump Instructions

### `jmp target` (Near Jump)
Unconditional jump to a label. Relative offset encoded by assembler.

```nasm
jmp  Main16                    ; Jump forward/backward to Main16 label
jmp  short Main16              ; Force short (8-bit offset, +-127 bytes) encoding
jmp  $                         ; Jump to self = infinite loop (hang/halt)
```

### `jmp selector:offset` (Far Jump)
Loads a new CS (code segment) and EIP/RIP simultaneously. This is critical for
mode transitions:

```nasm
jmp  LINEAR_CODE_SEL:dword ADDR_OF(jumpTo32BitAndLandHere)
; Loads CS = 0x10 (32-bit code selector), EIP = physical address of target
; This is how you activate protected mode after setting CR0.PE

jmp  LINEAR_CODE64_SEL:ADDR_OF(jumpTo64BitAndLandHere)
; Loads CS = 0x38 (64-bit code selector), RIP = target
; This is how you activate long mode after setting CR0.PG with LME
```

Why far jumps are necessary: the CPU caches segment descriptors internally.
Setting CR0.PE doesn't immediately change code execution mode — the far jump
forces the CPU to reload CS from the GDT, which actually activates the new mode.

### Conditional Jumps

All conditional jumps test flags set by the previous `cmp`, `test`, `add`,
`sub`, etc.

| Instruction | Condition | Flag Test | Meaning |
|-------------|-----------|-----------|---------|
| `je` / `jz` | Equal / Zero | ZF=1 | Result was zero or operands equal |
| `jne` / `jnz` | Not Equal / Not Zero | ZF=0 | Result non-zero or operands differ |
| `jb` / `jc` | Below / Carry | CF=1 | Unsigned less-than, or arithmetic carry |
| `jnb` / `jnc` / `jae` | Not Below / No Carry | CF=0 | Unsigned greater-or-equal |

```nasm
jne  searchingForBfvHeaderLoop    ; GUID dword didn't match, try next address
jb   searchedForBfvHeaderButNotFound ; Below scan range, give up
jz   secEntryPointWasNotFound     ; EAX is zero, not a valid address
jc   secEntryPointWasNotFound     ; Addition overflowed, invalid address
jnc  NotSupport5LevelPaging       ; CPUID bit not set (CF=0 from BT)
jae  getEntryPointOfFfsFileErrorReturn ; Past end of file
```

---

## 6. System & Privileged Instructions

### `cli`
**Clear Interrupt Flag.** Disables maskable hardware interrupts. Essential at
boot because no Interrupt Descriptor Table (IDT) exists yet — any interrupt
would triple-fault the CPU.

```nasm
cli   ; Must be done before switching to protected mode
```

### `lgdt [addr]`
**Load Global Descriptor Table Register.** Reads a 6-byte structure from memory:
- Bytes 0-1: GDT limit (size - 1)
- Bytes 2-5: GDT base address (linear/physical)

```nasm
o32 lgdt [cs:bx]  ; Load GDT. "o32" = use 32-bit base address in 16-bit mode
                   ; "cs:" = segment override (read from code segment, not DS)
```

The `o32` prefix is critical here: in 16-bit mode, `lgdt` normally reads a
3-byte (24-bit) base address. The `o32` prefix makes it read a 4-byte (32-bit)
base, which we need since our GDT is in the 0xFFxxxxxx range.

### `rdmsr` / `wrmsr`
**Read/Write Model-Specific Register.**

```nasm
mov  ecx, 0xC0000080  ; ECX = MSR number (IA32_EFER)
rdmsr                  ; Read MSR -> EDX:EAX (EDX=high 32 bits, EAX=low 32)
bts  eax, 8           ; Set LME bit (bit 8) in the low 32 bits
wrmsr                  ; Write EDX:EAX -> MSR
```

MSR `0xC0000080` is **IA32_EFER** (Extended Feature Enable Register):
- Bit 8 (LME): Long Mode Enable
- Bit 10 (LMA): Long Mode Active (read-only, CPU sets this)
- Bit 11 (NXE): No-Execute Enable

### `cpuid`
**CPU Identification.** Input leaf in EAX (sub-leaf in ECX), outputs in
EAX, EBX, ECX, EDX. Destroys all four registers.

```nasm
mov  eax, 0       ; Leaf 0: get max supported basic leaf
cpuid              ; EAX = max leaf number

mov  eax, 07h     ; Leaf 7: extended feature flags
mov  ecx, 0       ; Sub-leaf 0
cpuid              ; ECX bit 16 = 5-level paging support
```

### Control Register Access

```nasm
mov  eax, cr0     ; Read CR0 into EAX
mov  cr0, eax     ; Write EAX into CR0
mov  eax, cr4     ; Read CR4 into EAX
mov  cr4, eax     ; Write EAX into CR4
mov  cr3, eax     ; Write page table base address
```

Control registers can only be read/written via MOV with general-purpose
registers — no immediate values, no memory operands.

### `nop`
**No Operation.** Single byte (`0x90`). Used for alignment and padding.
At the reset vector, two NOPs precede the jump to provide padding within the
16-byte reset vector space.

---

## 7. I/O Instructions

### `out port, al` / `out dx, al`
Write the byte in AL to the specified I/O port.

```nasm
out  0x80, al     ; Write POST code to port 0x80 (ISA POST code display)
out  dx, al       ; Write AL to port in DX (used for serial UART registers)
```

Port `0x80` is the standard POST code diagnostic port. On real hardware, POST
code reader cards display this value. On QEMU, the `-debugcon stdio` flag shows
them.

### `in al, dx`
Read a byte from the I/O port in DX into AL.

```nasm
mov  dx, (0x3f8 + 5)   ; UART Line Status Register (COM1 + LSR offset)
in   al, dx             ; Read LSR — check if transmitter is ready
```

---

## 8. NASM Pseudo-Instructions & Directives

### `BITS 16` / `BITS 32` / `BITS 64`
Tells NASM what instruction encoding to use. Does NOT change the CPU mode —
that's done by far jumps and CR0/CR4 writes. This must match the actual CPU
mode or the generated code will be wrong.

```nasm
BITS 16   ; Generate 16-bit code (real mode)
; ... code runs, far jump to protected mode ...
BITS 32   ; Now generate 32-bit code
; ... code runs, far jump to long mode ...
BITS 64   ; Now generate 64-bit code
```

### `ALIGN n`
Pad the current position with NOPs (in code) or zeros (in data) until the
address is a multiple of `n`.

```nasm
ALIGN 16   ; Align to 16-byte boundary (important for GDT, reset vector)
ALIGN 2    ; Align to 2-byte boundary (GDTR structure)
```

### `TIMES n DB value`
Emit `value` exactly `n` times. Used for padding.

```nasm
TIMES (0x1000 - ($ - Pml4)) DB 0  ; Pad PML4 table to exactly 4KB
TIMES (0x1000 - 0x20) DB 0         ; Pad to put reset vector in last 0x20 bytes
```

`$` is the current assembly position, so `$ - label` is the distance from
`label` to here.

### `o32` (Operand Size Override Prefix)
In 16-bit mode, instruction operands default to 16-bit. The `o32` prefix
overrides them to 32-bit for a single instruction.

```nasm
o32 lgdt [cs:bx]  ; In 16-bit mode, load GDTR with 32-bit base address
```

Without `o32`, `lgdt` would only read a 24-bit base address, which can't
represent addresses in the 0xFF000000+ range.

### Data Definition Directives

```nasm
DB  'V', 'T', 'F', 0   ; Define bytes: VTF signature string
DW  0xffff              ; Define 16-bit word: GDT segment limit
DD  0, 0, 0             ; Define 32-bit doublewords: padding
DQ  PAGE_NLE(Pdp)       ; Define 64-bit quadword: page table entry
```

### `equ` — Named Constants

```nasm
NULL_SEL       equ $ - GDT_BASE   ; = 0x00 (offset of null descriptor)
LINEAR_CODE_SEL equ $ - GDT_BASE  ; = 0x10 (offset of code descriptor)
LINEAR_SEL     equ $ - GDT_BASE   ; = 0x18 (offset of data descriptor)
```

`$` is the current position. `$ - GDT_BASE` gives the byte offset from the
start of the GDT, which is exactly the selector value needed for segment
register loads.

### Preprocessor Directives

```nasm
%define NAME value      ; Text substitution (like C #define)
%assign i 0             ; Numeric variable (can be reassigned with %assign)
%ifdef SYMBOL           ; Conditional: true if SYMBOL is defined
%ifndef SYMBOL          ; Conditional: true if SYMBOL is NOT defined
%elifdef SYMBOL         ; Else-if defined
%else                   ; Else branch
%endif                  ; End conditional
%error "message"        ; Emit assembler error
%include "file.inc"     ; Include another source file textually
%rep N / %endrep        ; Repeat enclosed block N times
```

### `%rep` Loops (Assembly-Time Repetition)

```nasm
%assign i 0
%rep 512
    DQ  PAGE_PDPTE_1GB(i)   ; Emit 512 page directory pointer entries
    %assign i i+1
%endrep
```

This runs at assembly time, not at runtime. It generates 512 consecutive 8-byte
values in the output binary.

---

## 9. Custom Macros in This Codebase

### `OneTimeCall label`

**The most important construct in this codebase.** A stack-free subroutine call.

Expansion:
```nasm
OneTimeCall Foo
; Expands to:
    jmp     Foo                     ; Jump to the subroutine body
FooOneTimerCallReturn:              ; Label defined immediately after the jmp
                                    ; (this is where execution resumes after Foo)
```

The subroutine `Foo` ends with `OneTimeCallRet Foo`, which jumps back to
`FooOneTimerCallReturn`.

**Why it works**: Since both the call site and the return use the same label
name, the assembler can compute the jump targets at assembly time. No stack or
return address register needed.

**Why "OneTime"**: The return label is hardcoded. If you called `Foo` from two
places, the return would always go to the first. Each routine can only have one
call site.

**Note the typo**: The internal label uses `OneTimerCallReturn` (with an 'r'
in "Timer") — this is a historical typo in the edk2 codebase that has been
preserved for compatibility.

### `OneTimeCallRet label`

Return from a `OneTimeCall` subroutine.

Expansion:
```nasm
OneTimeCallRet Foo
; Expands to:
    jmp     FooOneTimerCallReturn   ; Jump back to after the call site
```

### `ADDR_OF(label)`

Converts an assembly-time label into its runtime physical address.

```nasm
%define ADDR_OF(x) (0x100000000 - fourGigabytes + x)
```

Since the code is placed at the top of 4GB, and `fourGigabytes` is a label at
the very end (representing address 0x100000000), this computes:
`physical_address = 0x100000000 - (distance from label to end) = 0x100000000 - fourGigabytes + x`

### `ADDR16_OF(label)`

Same concept but for 16-bit real mode, within the `0xF000` segment.

```nasm
%define ADDR16_OF(x) (0x10000 - fourGigabytes + x)
```

In real mode with CS=0xF000, the physical address is `0xF000 * 16 + offset =
0xF0000 + offset`. But this code runs in the top of 4GB space (with the
"unreal" CS base of 0xFFFF0000), so `0x10000` represents the segment size.

### `debugShowPostCode code`

Emit a POST code for debug purposes. Depending on build configuration:

- **Port80**: `mov al, code; out 0x80, al`
- **Serial**: Formats as hex and sends to COM1 at 115200 baud
- **Disabled**: No code emitted (no-op)

### `debugInitialize`

Initialize debug output hardware. For serial debug, configures COM1 UART to
115200/8/N/1. For other modes, no-op.

---

## 10. x86 CPU Modes & Transitions

### Real Mode (16-bit)
- CPU starts here at reset
- 20-bit address space (1MB) via segment:offset
- No memory protection, no paging
- Segment registers are 16-bit, address = segment * 16 + offset
- At reset: CS=0xF000, CS.base=0xFFFF0000 (special reset state)

### Protected Mode (32-bit)
- Entered by setting CR0.PE=1 followed by a far jump
- Segment registers become selectors into the GDT
- Can access full 4GB address space with flat segments
- Memory protection via segment limits and privilege levels
- This code uses "flat" model: base=0, limit=4GB for all segments

### Long Mode (64-bit)
- Requires: protected mode + PAE + IA32_EFER.LME + paging enabled
- Entered by setting CR0.PG=1 (with LME already set) followed by far jump
- 64-bit virtual address space, paging mandatory
- Most segment registers ignored (flat addressing assumed)
- CS still meaningful (determines 32-bit vs 64-bit code execution)

### Transition Sequence Used in This Code

```
Real Mode (16-bit)
  |
  | 1. Load GDT (lgdt)
  | 2. Set CR0.PE = 1
  | 3. Far jump to 32-bit code selector
  v
Protected Mode (32-bit, flat, no paging)
  |
  | 4. Load CR3 with page table base
  | 5. Set CR4.PAE = 1
  | 6. Set IA32_EFER.LME = 1 (via wrmsr)
  | 7. Set CR0.PG = 1 (enable paging)
  | 8. Far jump to 64-bit code selector
  v
Long Mode (64-bit, paged)
```

---

## 11. GDT Segment Descriptor Format

Each GDT entry is 8 bytes with this layout:

```
Byte 0-1:  Limit [15:0]      (low 16 bits of segment size)
Byte 2-3:  Base [15:0]       (low 16 bits of segment base address)
Byte 4:    Base [23:16]      (bits 16-23 of base)
Byte 5:    Access byte:
             Bit 7:    Present (P) — must be 1 for valid segments
             Bits 6-5: DPL (Descriptor Privilege Level, 0=kernel)
             Bit 4:    S (System flag, 1=code/data, 0=system)
             Bits 3-0: Type
               Code: 0xB = Execute + Read + Accessed
               Data: 0x3 = Read/Write + Accessed
Byte 6:    Flags + upper limit:
             Bit 7:    Granularity (G) — 1=limit in 4KB units, 0=bytes
             Bit 6:    D/B (Default size) — 1=32-bit, 0=16-bit
             Bit 5:    L (Long mode) — 1=64-bit code segment
             Bit 4:    Available
             Bits 3-0: Limit [19:16]
Byte 7:    Base [31:24]      (bits 24-31 of base)
```

### GDT Entries Used Here

| Selector | Name | Base | Limit | Type | Bits |
|----------|------|------|-------|------|------|
| 0x00 | NULL_SEL | - | - | Null | - |
| 0x08 | SPARE1_SEL | - | - | Null | - |
| 0x10 | LINEAR_CODE_SEL | 0 | 4GB | Code R/X | 32-bit |
| 0x18 | LINEAR_SEL | 0 | 4GB | Data R/W | 32-bit |
| 0x20 | SPARE2_SEL | - | - | Null | - |
| 0x28 | LINEAR_CODE16_SEL | 0 | 4GB | Code R/X | 16-bit |
| 0x30 | LINEAR_DATA16_SEL | 0 | 4GB | Data R/W | 16-bit |
| 0x38 | LINEAR_CODE64_SEL | 0 | 4GB | Code R/X | 64-bit |

All active segments have base=0 and limit=4GB, creating a "flat" address space
where segment:offset == physical address.

---

## 12. Page Table Structures

### 4-Level Paging (Standard x64)

```
CR3 -> PML4 (Page Map Level 4, 512 entries)
         |
         +--> PDPT (Page Directory Pointer Table, 512 entries)
                |
                +--> PD (Page Directory, 512 entries per PDPT entry)
                       |
                       +--> PT or 2MB large page
```

Each entry is 8 bytes (64-bit). Each table is 4KB (512 entries).

### 5-Level Paging (LA57)

Adds PML5 above PML4. Requires CPUID leaf 7 ECX bit 16 and CR4.LA57=1.

### Page Table Configuration in This Code

**With `PAGE_TABLE_1G` defined** (1GB pages):
- PML4: 1 entry -> Pdp
- Pdp: 512 entries, each a 1GB page -> maps 0 to 512GB

**Without `PAGE_TABLE_1G`** (2MB pages):
- PML4: 1 entry -> Pdp
- Pdp: 4 entries -> 4 PD tables
- PD: 4 * 512 = 2048 entries, each a 2MB page -> maps 0 to 4GB

Both create identity mapping (virtual address == physical address).

### Entry Format

```
Bits 0:     Present
Bits 1:     Read/Write
Bits 2:     User/Supervisor
Bits 3:     Write-Through
Bits 4:     Cache Disable
Bits 5:     Accessed
Bits 6:     Dirty (leaf entries only)
Bits 7:     Page Size (1 = this is a leaf / large page)
Bits 8:     Global
Bits 12-51: Physical address of next table or page frame (4KB aligned)
Bits 52-62: Available / reserved
Bit  63:    Execute Disable (if EFER.NXE=1)
```

For non-leaf entries (pointing to next table): bits 7,6 are reserved/ignored.
For leaf entries (2MB or 1GB pages): bit 7 (PS) must be 1.
