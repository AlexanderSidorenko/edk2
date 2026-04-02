# x86 Reset Vector — Execution Overview

## What This Code Does

When an x86 CPU powers on or resets, it begins executing instructions at a fixed
address: `0xFFFFFFF0` (16 bytes below the 4GB boundary). The CPU starts in
**16-bit real mode** — the most primitive x86 operating mode, compatible with the
original 8086 processor from 1978.

This reset vector code is responsible for:

1. Identifying whether this CPU is the Bootstrap Processor (BSP) or an
   Application Processor (AP)
2. Saving the Built-In Self Test (BIST) result
3. Transitioning the CPU from 16-bit real mode to 32-bit protected mode
4. Finding the Boot Firmware Volume (BFV) in the flash/ROM
5. Locating the SEC (Security) phase entry point within the BFV
6. (On x64) Transitioning from 32-bit to 64-bit long mode with paging
7. Jumping to the SEC entry point — the first C code in the boot process

This code runs **without any RAM**. There is no stack, no heap, no C runtime.
Everything is done in CPU registers alone. The `OneTimeCall` macro is a clever
trick to simulate subroutine calls without a stack (see "Key Concepts" below).

---

## File Organization

All files live under `UefiCpuPkg/ResetVector/Vtf0/`. The master file
`Vtf0.nasmb` `%include`s everything in a specific order. NASM assembles them
as one contiguous blob that gets placed at the very top of the firmware ROM
image (just below 4GB).

```
Vtf0.nasmb                          <-- Master file, includes everything
  |
  +-- CommonMacros.inc               <-- OneTimeCall/OneTimeCallRet, address macros
  +-- PostCodes.inc                  <-- Debug POST code constants (0x16, 0x32, 0x64...)
  +-- PageTables.inc                 <-- Page table entry bit definitions
  +-- X64/PageTables.nasm.inc        <-- [x64 only] Static page tables (identity map)
  +-- Port80Debug.nasm.inc           <-- Debug via port 0x80 (or Serial, or Disabled)
  +-- Ia32/SearchForBfvBase.nasm.inc <-- Scan flash for firmware volume header
  +-- Ia32/SearchForSecEntry.nasm.inc<-- Find SEC core in FFS file system
  +-- Ia32/Flat32ToFlat64.nasm.inc   <-- [x64 only] 32-bit to 64-bit transition
  +-- Ia32/PageTables64.nasm.inc     <-- [x64 only] Set CR3 to page table base
  +-- Ia16/Real16ToFlat32.nasm.inc   <-- 16-bit real to 32-bit protected mode
  +-- Ia16/Init16.nasm.inc           <-- Early 16-bit init (BSP marker, BIST save)
  +-- Main.nasm.inc                  <-- Main orchestrator — calls everything in order
  +-- Ia16/ResetVectorVtf0.nasm.inc  <-- The actual reset vector at 0xFFFFFFF0
```

**Important**: The include order matters. NASM lays out code in include order,
and the reset vector code at `0xFFFFFFF0` must be the LAST thing in the binary
(since it maps to the highest addresses in flash). That's why `ResetVectorVtf0`
is included last.

---

## Execution Sequence (Step by Step)

### Phase 1: Reset Vector Entry (16-bit Real Mode)

**File: `Ia16/ResetVectorVtf0.nasm.inc`**

```
Address 0xFFFFFFF0:  nop; nop; jmp EarlyBspInitReal16
```

The CPU fetches its first instruction from `0xFFFFFFF0`. It executes two `nop`s
(padding) then jumps backward in the ROM to `EarlyBspInitReal16`.

At this point:
- CPU is in 16-bit real mode
- CS = 0xF000, IP = 0xFFF0 (which maps to physical 0xFFFFFFF0)
- No RAM is available — only CPU registers and ROM
- EAX contains the BIST (Built-In Self Test) result from CPU power-on

### Phase 2: BSP Identification (16-bit Real Mode)

**File: `Ia16/Init16.nasm.inc`**

```
EarlyBspInitReal16:
    mov  di, 'BP'      ; Mark this CPU as Bootstrap Processor
    jmp  Main16         ; Jump to main initialization sequence
```

The BSP (first CPU to boot) sets `DI = 'BP'` as an identifier. Application
Processors (APs, other CPU cores) would enter differently with `DI = 'AP'`.

### Phase 3: Main Orchestrator Begins (16-bit Real Mode)

**File: `Main.nasm.inc`**

```
Main16:
    OneTimeCall EarlyInit16          ; Save BIST, init debug
    OneTimeCall TransitionFromReal16To32BitFlat  ; -> 32-bit mode
    [now in 32-bit mode]
    OneTimeCall Flat32SearchForBfvBase           ; Find firmware volume
    OneTimeCall Flat32SearchForSecEntryPoint     ; Find SEC core
    [on x64:]
    OneTimeCall Transition32FlatTo64Flat         ; -> 64-bit mode
    jmp  rsi                                     ; Jump to SEC!
```

This is the main sequence. Each `OneTimeCall` is a one-shot jump to a
subroutine that never needs to be called again (see Key Concepts below).

### Phase 4: Save BIST and Initialize Debug (16-bit Real Mode)

**File: `Ia16/Init16.nasm.inc` — `EarlyInit16`**

```
EarlyInit16:
    mov  esp, eax       ; Stash BIST result (EAX at reset) into ESP
    debugInitialize     ; Init debug output (serial/port80/nothing)
    OneTimeCallRet EarlyInit16  ; Return to Main16
```

Since we have no stack, ESP is used as a scratch register to hold the BIST
value. It will be restored to EAX just before jumping to SEC.

### Phase 5: Transition to 32-bit Protected Mode

**File: `Ia16/Real16ToFlat32.nasm.inc` — `TransitionFromReal16To32BitFlat`**

This is where the CPU leaves its primitive 16-bit real mode:

1. **`cli`** — Disable interrupts (no IDT exists yet)
2. **Load GDT** — `lgdt` loads the Global Descriptor Table, which defines
   memory segments for protected mode. The GDT contains:
   - Null descriptor (required by x86)
   - `LINEAR_CODE_SEL` (0x10): 32-bit code, base=0, limit=4GB
   - `LINEAR_SEL` (0x18): 32-bit data, base=0, limit=4GB
   - `LINEAR_CODE64_SEL` (0x38): 64-bit code segment (used later)
3. **Set CR0** — Enable Protection Enable (PE) bit. Value `0x23` means:
   - Bit 0 (PE) = 1: Protected mode on
   - Bit 1 (MP) = 1: Monitor coprocessor
   - Bit 5 (NE) = 1: Numeric error reporting
4. **Far jump** — `jmp LINEAR_CODE_SEL:jumpTo32BitAndLandHere` flushes the
   prefetch queue and loads CS with the 32-bit code selector
5. **Set CR4** — Value `0x640` enables:
   - Bit 5 (PAE) = 0 (not yet — enabled later for 64-bit)
   - Bit 6 (MCE) = 1: Machine-Check Enable
   - Bit 9 (OSFXSR) = 1: OS supports FXSAVE/FXRSTOR
   - Bit 10 (OSXMMEXCPT) = 1: OS supports unmasked SIMD FP exceptions
6. **Load segment registers** — All set to `LINEAR_SEL` (flat 0-4GB access)

After this, the CPU has a flat 32-bit address space covering all 4GB.

### Phase 6: Find the Boot Firmware Volume (32-bit Protected Mode)

**File: `Ia32/SearchForBfvBase.nasm.inc` — `Flat32SearchForBfvBase`**

The code needs to find where the main firmware volume lives in the flash ROM.
It scans every 4KB-aligned address in the top 16MB of the 4GB address space
(from `0xFFFFF000` down to `0xFF000000`), checking for either an FFS2 or FFS3
GUID signature at offset `+0x10` in the candidate volume header.

When a GUID match is found, it validates the firmware volume by checking that
`base + FvLength == 0` (i.e., the volume ends exactly at the 4GB boundary).

Result: `EBP = base address of Boot Firmware Volume`

If not found: hangs with `EAX = EBP = 0xBFBFBFBF` and POST code `0xB0`.

### Phase 7: Find the SEC Core Entry Point (32-bit Protected Mode)

**File: `Ia32/SearchForSecEntry.nasm.inc` — `Flat32SearchForSecEntryPoint`**

Now the code walks the FFS (Firmware File System) inside the BFV:

1. Skip past the firmware volume header (header length at offset `+0x30`)
2. Iterate through FFS file headers (8-byte aligned)
3. For each file, check if file type == `0x03` (SEC Core)
4. When found, call `GetEntryPointOfFfsFile` to parse the PE32 or TE image
   header and extract the entry point address
5. For PE32: Check `MZ` + `PE\0\0` signatures, read AddressOfEntryPoint
6. For TE (Terse Executable): Check `VZ` signature, compute adjusted entry

Result: `ESI = SEC Core entry point address`

If not found: hangs with POST code `0xF0`.

### Phase 8: Transition to 64-bit Long Mode (x64 only)

**File: `Ia32/Flat32ToFlat64.nasm.inc` — `Transition32FlatTo64Flat`**

To enter 64-bit mode, the CPU needs paging enabled:

1. **Set CR3** — Point to the pre-built page tables (identity mapping 0-4GB
   or 0-512GB depending on configuration)
2. **Enable PAE** — Set bit 5 in CR4 (Physical Address Extension, required
   for long mode)
3. **Set LME** — Write to MSR `0xC0000080` (IA32_EFER), set bit 8 (Long
   Mode Enable)
4. **Enable paging** — Set bit 31 (PG) in CR0
5. **Far jump** — `jmp LINEAR_CODE64_SEL:jumpTo64BitAndLandHere` loads the
   64-bit code segment

The page tables themselves are statically built into the ROM image at assembly
time (see `X64/PageTables.nasm.inc`). They create a 1:1 identity map so that
physical address == virtual address.

### Phase 9: Jump to SEC (End of Reset Vector Code)

**File: `Main.nasm.inc` — bottom of `Main16`**

For IA-32:
```
    mov  eax, esp       ; Restore BIST from ESP back to EAX
    jmp  esi            ; Jump to SEC Core entry point
```

For x64:
```
    ; Zero upper 32 bits of 64-bit registers (values were set in 32-bit mode)
    and  rsi, 0xFFFFFFFF
    and  rbp, 0xFFFFFFFF
    and  rsp, 0xFFFFFFFF
    mov  rax, rsp       ; Restore BIST to RAX
    jmp  rsi            ; Jump to SEC Core entry point
```

SEC receives:
- `RAX/EAX` = BIST result
- `RBP/EBP` = BFV base address
- `DI` = `'BP'` (BSP) or `'AP'` (AP)
- All segment registers = flat selectors

---

## Key Concepts

### The OneTimeCall / OneTimeCallRet Mechanism

This is the most important "trick" in this code. Normally, subroutine calls use
`call` (pushes return address to stack) and `ret` (pops it). But **we have no
stack** — no RAM is initialized yet!

The solution is a compile-time trick using NASM macros:

```nasm
%macro OneTimeCall 1
    jmp     %1                      ; Jump to the subroutine
%1 %+ OneTimerCallReturn:           ; Define a label right after the jmp
%endmacro

%macro OneTimeCallRet 1
    jmp     %1 %+ OneTimerCallReturn  ; Jump back to after the OneTimeCall
%endmacro
```

**How it works:**

When you write `OneTimeCall Foo`, it expands to:
```nasm
    jmp     Foo                     ; Jump to Foo
FooOneTimerCallReturn:              ; This label is the "return address"
```

When Foo finishes, it executes `OneTimeCallRet Foo`, which expands to:
```nasm
    jmp     FooOneTimerCallReturn   ; Jump back to after the call site
```

**Why "OneTime"?** Because the return label is a fixed address. If you called
the same routine from two different places, the return would always go to the
first call site. Each routine can only be called once — which is fine, because
this is a linear initialization sequence.

### VTF (Volume Top File)

The VTF is a special file in the firmware volume that must be located at the
very top of the firmware device (just below 4GB). The "VTF-0" variant (used
here) requires no relocations/fixups — all addresses are computed at assembly
time relative to the known 4GB boundary.

The VTF signature `VTF\0` at address `0xFFFFFFEC` allows tools to identify and
validate the volume top file.

### Address Macros

```nasm
%define ADDR16_OF(x) (0x10000 - fourGigabytes + x)
%define ADDR_OF(x)   (0x100000000 - fourGigabytes + x)
```

Since this code is mapped at the top of the 4GB space, and the label
`fourGigabytes` represents address `0x100000000`, these macros convert
assembly-time label addresses into the physical addresses they'll occupy at
runtime.

- `ADDR16_OF(x)`: 16-bit offset within the 0xF000 segment (for real mode)
- `ADDR_OF(x)`: 32-bit flat physical address (for protected mode)

### POST Codes

Debug progress indicators sent to I/O port `0x80` (visible on POST code
display cards or logic analyzers):
- `0x16` = Running in 16-bit mode
- `0x32` = Entered 32-bit protected mode
- `0x64` = Entered 64-bit long mode
- `0xB0` = BFV not found (fatal)
- `0xB1` = BFV found
- `0xF0` = SEC not found (fatal)
- `0xF1` = SEC found

### BIST (Built-In Self Test)

When the CPU resets, it runs a hardware self-test and places the result in EAX.
A value of 0 means the CPU passed. The reset vector code preserves this value
(in ESP, since there's no stack to corrupt) and passes it to SEC in EAX/RAX.

---

## Memory Map at Reset

```
0x0000000000000000  +---------------------------+
                    |  (RAM - not yet usable)    |
                    |  ...                       |
0x00000000FF000000  +---------------------------+  <-- Bottom of 16MB scan range
                    |  Firmware flash region     |
                    |  (mapped by chipset)       |
                    |  Boot Firmware Volume      |
                    |  lives somewhere here      |
                    |  ...                       |
0x00000000FFFFFFD0  |  Page tables (if x64)     |
0x00000000FFFFFFE0  |  Padding (3 zero DWORDs)  |
0x00000000FFFFFFEC  |  VTF signature "VTF\0"    |
0x00000000FFFFFFF0  |  Reset Vector: nop;nop;jmp|  <-- CPU starts HERE
0x0000000100000000  +---------------------------+  <-- 4GB boundary
```

---

## What Happens After This Code

The SEC phase (written in C) takes over and:
1. Initializes a temporary stack in CPU cache (Cache-as-RAM / CAR)
2. Initializes the platform's minimum hardware
3. Finds and launches the PEI (Pre-EFI Initialization) phase
4. PEI initializes RAM, then launches DXE (Driver Execution Environment)
5. DXE loads drivers, then BDS (Boot Device Selection) picks an OS to boot
