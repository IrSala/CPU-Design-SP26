; ═══════════════════════════════════════════════════════════════════
; fibonacci.asm — First 10 Fibonacci numbers via MMIO output
;
; ISA notes:
;   R0  = zero register (always 0)
;   R14 = link register (return address for JAL/RET)
;   R15 = stack pointer
;   MOVI Rd, imm8   — load 8-bit zero-extended immediate
;   CMP  Rs1, Rs2   — subtract and set Z/N flags, discard result
;   BEQ  label      — branch if Z==1
;   BNE  label      — branch if Z==0
;   STORE Raddr, Rs — Mem[Raddr] = Rs  (used for MMIO output)
;
; MMIO:
;   Write to data address 0x7F00 to print a value to the console.
;   We load 0x7F as the high byte and 0x00 as the low byte into R14,
;   then reconstruct the full address with SHL + OR.
;
; Registers:
;   R1  = a  (current Fibonacci value, starts at 0)
;   R2  = b  (next Fibonacci value, starts at 1)
;   R3  = temp / scratch
;   R4  = loop counter (10 values total)
;   R5  = MMIO output address (0x7F00)
;
; Fetch → Compute → Store cycle (one loop iteration):
;   Fetch:   PC→IR, PC++
;   Compute: ADD R3, R1, R2  (a+b)
;   Store:   R3 → R1, R2 → new R2, value → MMIO
; ═══════════════════════════════════════════════════════════════════

.text

start:
    ; Set up MMIO address R5 = 0x7F00
    ; MOVI can only load 8 bits, so: R5 = 0x7F, then SHL R5 by 8
    MOVI  R5, 0x7F
    MOVI  R6, 8
    SHL   R5, R5, R6          ; R5 = 0x7F00
    ADDI  R5, 1               ; R5 = 0x7F01 (integer MMIO)
    ; Initialise Fibonacci sequence
    MOVI  R1, 0               ; a = 0  (fib[0])
    MOVI  R2, 1               ; b = 1  (fib[1])
    MOVI  R4, 10              ; loop counter = 10

loop:
    ; Check if counter has reached 0
    CMP   R4, R0              ; Z=1 if R4 == 0
    BEQ   done

    ; Output current value of 'a' to MMIO console
    STORE R5, R1              ; Mem[0x7F00] = R1

    ; Advance: a, b = b, a+b
    ADD   R3, R1, R2          ; R3 = a + b
    MOV   R1, R2              ; a = b
    MOV   R2, R3              ; b = a + b

    ; Decrement counter
    ADDI  R4, -1

    JMP   loop

done:
    JMP done                       ; end of program
