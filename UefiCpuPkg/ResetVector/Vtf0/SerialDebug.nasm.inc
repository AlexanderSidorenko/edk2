;------------------------------------------------------------------------------
; @file
; Serial port (UART) debug support macros.
;
; Outputs POST codes as hexadecimal strings to COM1 (0x3F8) at 115200 baud,
; 8 data bits, 1 stop bit, no parity. This is useful for debugging on
; hardware or in virtual machines where serial console is available.
;
; Example output for POST code 0x32:
;   "32\r\n"
;
; The UART is a 16550-compatible chip (or emulation thereof). Communication
; uses programmed I/O (in/out instructions) to the UART's register set.
;
; UART Register Map (base = 0x3F8 for COM1):
;   +0x00 (DLAB=0): TX/RX data register
;   +0x00 (DLAB=1): Baud rate divisor low byte
;   +0x01 (DLAB=0): Interrupt Enable Register (IER)
;   +0x01 (DLAB=1): Baud rate divisor high byte
;   +0x02:          FIFO Control Register (FCR) / read: Interrupt ID (EIR)
;   +0x03:          Line Control Register (LCR) — controls DLAB, parity, etc.
;   +0x04:          Modem Control Register (MCR)
;   +0x05:          Line Status Register (LSR) — bit 5 = TX ready
;   +0x06:          Modem Status Register (MSR)
;
; Copyright (c) 2008 - 2018, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

;//---------------------------------------------
;// UART Register Offsets (from base address)
;//---------------------------------------------
%define BAUD_LOW_OFFSET         0x00    ; Baud rate divisor low byte (when DLAB=1)
%define BAUD_HIGH_OFFSET        0x01    ; Baud rate divisor high byte (when DLAB=1)
%define IER_OFFSET              0x01    ; Interrupt Enable Register (when DLAB=0)
%define LCR_SHADOW_OFFSET       0x01    ; (alternate name for +0x01)
%define FCR_SHADOW_OFFSET       0x02    ; (alternate name for +0x02)
%define IR_CONTROL_OFFSET       0x02    ; (alternate name for +0x02)
%define FCR_OFFSET              0x02    ; FIFO Control Register
%define EIR_OFFSET              0x02    ; Interrupt Identification Register (read)
%define BSR_OFFSET              0x03    ; (alternate name for LCR)
%define LCR_OFFSET              0x03    ; Line Control Register
%define MCR_OFFSET              0x04    ; Modem Control Register
%define LSR_OFFSET              0x05    ; Line Status Register
%define MSR_OFFSET              0x06    ; Modem Status Register

;//---------------------------------------------
;// UART Register Bit Defines
;//---------------------------------------------
%define LSR_TXRDY               0x20    ; Bit 5 of LSR: TX Holding Register Empty
                                         ; (set when UART is ready to accept a byte)
%define LSR_RXDA                0x01    ; Bit 0 of LSR: Receive Data Available
%define DLAB                    0x01    ; Divisor Latch Access Bit (bit 7 of LCR)
                                         ; When LCR bit 7 = 1, offsets 0x00/0x01 access
                                         ; the baud rate divisor instead of data/IER

;
; Default serial port configuration: COM1, 115200 baud, 8N1
;
; UINT16  gComBase = 0x3f8;     // COM1 base I/O address
; UINTN   gBps = 115200;        // Baud rate: 115200 bits per second
; UINT8   gData = 8;            // 8 data bits per character
; UINT8   gStop = 1;            // 1 stop bit
; UINT8   gParity = 0;          // No parity
; UINT8   gBreakSet = 0;        // No break signal
;
%define DEFAULT_COM_BASE 0x3f8
%define DEFAULT_BPS 115200
%define DEFAULT_DATA 8
%define DEFAULT_STOP 1
%define DEFAULT_PARITY 0
%define DEFAULT_BREAK_SET 0

;
; SERIAL_DEFAULT_LCR — Computed LCR (Line Control Register) value.
;
; LCR bit layout:
;   Bits 1:0 = Data bits - 5 (so 8 data bits = 3 = 0b11)
;   Bit 2    = Stop bits (0 = 1 stop bit, 1 = 2 stop bits)
;   Bits 5:3 = Parity (000 = none)
;   Bit 6    = Break control (0 = no break)
;   Bit 7    = DLAB (set separately for baud rate programming)
;
; For 8N1: (0 << 6) | (0 << 3) | (1 << 2)=0 | (8-5)=3 = 0x03
;
%define SERIAL_DEFAULT_LCR ( \
     (DEFAULT_BREAK_SET << 6) | \
     (DEFAULT_PARITY << 3) | \
     (DEFAULT_STOP << 2) | \
     (DEFAULT_DATA - 5) \
    )

%define SERIAL_PORT_IO_BASE_ADDRESS DEFAULT_COM_BASE

;
; inFromSerialPort — Read a byte from a UART register.
;
; @param %1  Register offset (e.g., LSR_OFFSET)
; Result: AL = byte read from the register
;
%macro  inFromSerialPort 1
    mov     dx, (SERIAL_PORT_IO_BASE_ADDRESS + %1)  ; DX = I/O port address
    in      al, dx                                    ; AL = byte from port
%endmacro

;
; waitForSerialTxReady — Spin-wait until the UART transmitter is ready.
;
; Polls the LSR (Line Status Register) until bit 5 (TXRDY) is set,
; indicating the TX holding register is empty and can accept a new byte.
;
; The %%waitingForTx label uses %% (double percent) to make it local to
; each macro expansion — otherwise, multiple invocations would define
; duplicate labels and cause assembler errors.
;
%macro  waitForSerialTxReady 0

%%waitingForTx:
    inFromSerialPort LSR_OFFSET         ; AL = Line Status Register
    test    al, LSR_TXRDY              ; Is bit 5 (TX ready) set?
    jz      %%waitingForTx              ; No -> keep polling

%endmacro

;
; outToSerialPort — Write a byte to a UART register.
;
; @param %1  Register offset (e.g., 0 for TX data, LCR_OFFSET for LCR)
; @param %2  Byte value to write
;
%macro  outToSerialPort 2
    mov     dx, (SERIAL_PORT_IO_BASE_ADDRESS + %1)  ; DX = I/O port address
    mov     al, %2                                    ; AL = value to write
    out     dx, al                                    ; Write byte to port
%endmacro

;
; debugShowCharacter — Send one ASCII character to the serial port.
;
; Waits for TX ready, then sends the character.
;
; @param %1  ASCII character value (e.g., 'A', `\r`, `\n`)
;
%macro  debugShowCharacter 1
    waitForSerialTxReady                ; Spin until UART can accept a byte
    outToSerialPort 0, %1              ; Write character to TX data register (+0x00)
%endmacro

;
; debugShowHexDigit — Send one hex digit (0-F) as its ASCII character.
;
; Converts a 4-bit value to its ASCII hex representation:
;   0-9 -> '0'-'9' (0x30-0x39)
;   A-F -> 'a'-'f' (0x61-0x66)
;
; @param %1  Value 0x0 through 0xF
;
%macro  debugShowHexDigit 1
  %if (%1 < 0xa)
    debugShowCharacter BYTE ('0' + (%1))    ; '0'..'9'
  %else
    debugShowCharacter BYTE ('a' + ((%1) - 0xa))  ; 'a'..'f'
  %endif
%endmacro

;
; debugNewline — Send CR+LF to the serial port.
;
%macro  debugNewline 0
    debugShowCharacter `\r`             ; Carriage Return (0x0D)
    debugShowCharacter `\n`             ; Line Feed (0x0A)
%endmacro

;
; debugShowPostCode — Display a POST code as two hex digits + newline.
;
; Example: debugShowPostCode 0x32 outputs "32\r\n" to the serial port.
;
; @param %1  POST code byte value (e.g., POSTCODE_32BIT_MODE = 0x32)
;
%macro  debugShowPostCode 1
    debugShowHexDigit (((%1) >> 4) & 0xf)   ; High nibble (e.g., 0x3 -> '3')
    debugShowHexDigit ((%1) & 0xf)          ; Low nibble  (e.g., 0x2 -> '2')
    debugNewline                             ; CR+LF
%endmacro

BITS    16

;
; debugInitialize — Configure the COM1 UART for 115200 baud, 8N1.
;
; This is implemented as a real-mode code snippet (not a %macro body that
; emits inline), because it needs multiple I/O operations. The macro jumps
; to the code, which then jumps back.
;
; This uses a manual jmp/label pair (NOT OneTimeCall) because it's invoked
; from within EarlyInit16 which itself uses OneTimeCall.
;
%macro  debugInitialize 0
  jmp  real16InitDebug              ; Jump to initialization code below
real16InitDebugReturn:              ; Return label — execution resumes here
%endmacro

real16InitDebug:
    ;
    ; Step 1: Enable DLAB (Divisor Latch Access Bit) to program baud rate.
    ;
    ; Writing to LCR with bit 7 = 1 switches offsets +0x00 and +0x01 from
    ; data/IER registers to baud rate divisor low/high registers.
    ; We also set the serial format (8N1) in the same write.
    ;
    outToSerialPort LCR_OFFSET, ((DLAB << 7) | SERIAL_DEFAULT_LCR)
                                        ; LCR = 0x83: DLAB=1, 8 data, 1 stop, no parity

    ;
    ; Step 2: Set the baud rate divisor.
    ;
    ; The divisor = 115200 / desired_baud_rate.
    ; For 115200 baud: divisor = 115200 / 115200 = 1 (0x0001).
    ; High byte = 0x00, low byte = 0x01.
    ;
    outToSerialPort BAUD_HIGH_OFFSET, ((115200 / DEFAULT_BPS) >> 8)
                                        ; Divisor high byte: 0x00
    outToSerialPort BAUD_LOW_OFFSET, ((115200 / DEFAULT_BPS) & 0xff)
                                        ; Divisor low byte: 0x01

    ;
    ; Step 3: Clear DLAB to switch back to normal register access.
    ;
    ; Writing LCR with bit 7 = 0 restores normal operation: offset +0x00
    ; becomes the TX/RX data register again, and +0x01 becomes IER.
    ;
    outToSerialPort LCR_OFFSET, SERIAL_DEFAULT_LCR
                                        ; LCR = 0x03: DLAB=0, 8 data, 1 stop, no parity

    jmp     real16InitDebugReturn       ; Return to the debugInitialize call site

