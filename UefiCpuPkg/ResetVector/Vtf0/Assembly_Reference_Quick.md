# x86 Assembly Quick Reference — Reset Vector Instructions

Every instruction and NASM directive used in the reset vector code, one per line.

## CPU Instructions

```
nop             — No operation (1-byte padding)
jmp  target     — Unconditional jump (near or far)
jmp  sel:off    — Far jump — loads CS:EIP, flushes pipeline, switches mode
jmp  $          — Infinite loop (hang) — jumps to own address forever
cli             — Clear Interrupt Flag — disables maskable interrupts
mov  dst, src   — Copy src to dst (register, memory, or immediate)
movzx dst, src  — Move with Zero-Extend (e.g., 16-bit -> 32-bit, upper bits = 0)
xor  a, a       — XOR register with itself = zero it (smaller/faster than mov r,0)
add  dst, src   — dst = dst + src, sets CF/ZF/SF/OF flags
sub  dst, src   — dst = dst - src, sets CF/ZF/SF flags
inc  dst        — dst = dst + 1
and  dst, src   — Bitwise AND, result in dst, sets ZF
or   dst, src   — Bitwise OR, result in dst, sets ZF
test dst, src   — Bitwise AND but discards result, only sets flags (ZF, SF, PF)
cmp  a, b       — Subtract b from a, discard result, only sets flags
bt   src, bit   — Bit Test — copy bit N of src into CF (Carry Flag)
bts  dst, bit   — Bit Test and Set — copy bit N into CF, then set bit N to 1
jne  / jnz      — Jump if Not Equal / Not Zero (ZF=0)
je   / jz       — Jump if Equal / Zero (ZF=1)
jb   / jc       — Jump if Below / Carry (CF=1, unsigned <)
jnb  / jnc      — Jump if Not Below / No Carry (CF=0, unsigned >=)
jae             — Jump if Above or Equal (CF=0, same as jnb/jnc)
lgdt [addr]     — Load Global Descriptor Table Register from memory (6-byte ptr)
rdmsr           — Read MSR[ECX] -> EDX:EAX
wrmsr           — Write EDX:EAX -> MSR[ECX]
cpuid           — Query CPU info: EAX=leaf -> EAX,EBX,ECX,EDX results
in   al, port   — Read byte from I/O port into AL
in   al, dx     — Read byte from I/O port DX into AL
out  port, al   — Write byte AL to I/O port
out  dx, al     — Write byte AL to I/O port in DX
```

## Registers Used

```
EAX/RAX  — Accumulator, BIST value at reset, general scratch
EBX      — General scratch (base pointer in some contexts)
ECX      — Counter, MSR address for rdmsr/wrmsr, CPUID sub-leaf
EDX      — Upper 32 bits for rdmsr/wrmsr, I/O port address
ESI/RSI  — SEC entry point address (output)
EDI      — 'BP' or 'AP' processor type marker
EBP/RBP  — BFV base address (output)
ESP/RSP  — BIST scratch storage (NOT used as stack pointer!)
CS       — Code Segment (loaded by far jmp)
DS,ES,FS,GS,SS — Data/Extra/Stack segments (loaded manually)
CR0      — Control: PE (bit 0), PG (bit 31)
CR3      — Page table base address
CR4      — Control: PAE (bit 5), MCE (bit 6), LA57 (bit 12)
```

## NASM Directives & Pseudo-Instructions

```
BITS 16/32/64   — Tell NASM to generate 16/32/64-bit code
ALIGN n         — Pad with NOPs/zeros to next n-byte boundary
TIMES n DB 0    — Repeat: emit n zero bytes (padding)
o32             — Operand-size override prefix (use 32-bit operand in 16-bit mode)
DB val          — Define Byte — emit raw byte(s)
DW val          — Define Word — emit 16-bit value
DD val          — Define Doubleword — emit 32-bit value
DQ val          — Define Quadword — emit 64-bit value
equ             — Define symbolic constant (label equ value)
%define         — Preprocessor text substitution
%assign         — Preprocessor numeric assignment (can be reassigned)
%rep / %endrep  — Repeat block N times
%macro / %endmacro — Define multi-line macro
%include        — Include another source file
%ifdef / %ifndef / %elifdef / %else / %endif — Conditional assembly
%error          — Emit assembler error message
%1, %2          — Macro parameter references (1st, 2nd argument)
%%label         — Macro-local label (unique per macro expansion)
%+              — Token pasting (concatenate strings to form identifiers)
```

## Key Control Register Bits

```
CR0 bit  0 (PE) — Protection Enable (real mode -> protected mode)
CR0 bit  1 (MP) — Monitor Coprocessor
CR0 bit  5 (NE) — Numeric Error
CR0 bit 31 (PG) — Paging Enable

CR4 bit  5 (PAE)        — Physical Address Extension (required for 64-bit)
CR4 bit  6 (MCE)        — Machine-Check Enable
CR4 bit  9 (OSFXSR)     — OS FXSAVE/FXRSTOR support
CR4 bit 10 (OSXMMEXCPT) — OS Unmasked SIMD FP Exceptions
CR4 bit 12 (LA57)       — 5-Level Paging Enable

MSR 0xC0000080 (IA32_EFER):
  bit 8  (LME) — Long Mode Enable
  bit 10 (LMA) — Long Mode Active (read-only, set by CPU when PG+LME)
```

## Page Table Entry Bits

```
Bit  0 — Present (page is valid)
Bit  1 — Read/Write (writable if set)
Bit  2 — User/Supervisor
Bit  3 — Write-Through
Bit  4 — Cache Disable
Bit  5 — Accessed
Bit  6 — Dirty
Bit  7 — Page Size (1=large page: 2MB at PDE level, 1GB at PDPTE level)
Bit  8 — Global
Bit 12 — PAT (for 2MB pages)
```
