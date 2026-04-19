; ═══════════════════════════════════════════════════════════════════
; timer.asm — Countdown timer demonstrating Fetch/Compute/Store
;
; This program counts down from 20 to 0, writing each value to the
; MMIO console port. It is designed to clearly illustrate how every
; instruction passes through the three pipeline stages:
;
;   ┌──────────────────────────────────────────────────────────┐
;   │  FETCH    PC → Address Bus → Instruction Memory          │
;   │           Instruction → Instruction Bus → IR             │
;   │           PC incremented                                  │
;   ├──────────────────────────────────────────────────────────┤
;   │  COMPUTE  Control Unit decodes IR                        │
;   │           Operands placed on Data Bus → ALU inputs       │
;   │           ALU executes operation, sets flags             │
;   ├──────────────────────────────────────────────────────────┤
;   │  STORE    ALU result or memory data routed to:           │
;   │           • a register (most R/I-type instructions)      │
;   │           • data memory (STORE instruction)              │
;   │           • PC (JMP / BEQ / BNE)                         │
;   └──────────────────────────────────────────────────────────┘
;
; Registers:
;   R1  countdown value  (20 → 0)
;   R2  MMIO output address (0x7F00)
;   R6  scratch for shift amount
; ═══════════════════════════════════════════════════════════════════

.text

; ── Initialisation ──────────────────────────────────────────────────────────
start:
    ; Build MMIO address 0x7F00 in R2
    MOVI  R2, 0x7F            ; FETCH: get MOVI; COMPUTE: imm=0x7F; STORE: R2=0x7F
    MOVI  R6, 8               ; FETCH: get MOVI; COMPUTE: imm=8;    STORE: R6=8
    SHL   R2, R2, R6          ; FETCH: get SHL;  COMPUTE: 0x7F<<8;  STORE: R2=0x7F00
    ADDI  R2, 1               ; R2 = 0x7F01 (integer MMIO)
    MOVI  R1, 20              ; FETCH: get MOVI; COMPUTE: imm=20;   STORE: R1=20

; ── Countdown loop ──────────────────────────────────────────────────────────
count_loop:
    ; Cycle A — check counter
    ; FETCH:   load CMP into IR, PC → count_loop+1
    ; COMPUTE: ALU subtracts R0 (0) from R1; sets Z=1 if R1==0
    ; STORE:   flags updated, no register write
    CMP   R1, R0

    ; Cycle B — conditional branch
    ; FETCH:   load BEQ into IR
    ; COMPUTE: control unit checks Z flag
    ; STORE:   if Z==1: PC = done; else PC = count_loop+2
    BEQ   done

    ; Cycle C — output current count
    ; FETCH:   load STORE into IR
    ; COMPUTE: address bus ← R2 (0x7F00), data bus ← R1
    ; STORE:   data_mem[0x7F00] = R1  (MMIO console receives R1)
    STORE R2, R1

    ; Cycle D — decrement counter
    ; FETCH:   load ADDI into IR
    ; COMPUTE: ALU computes R1 + (-1)
    ; STORE:   result → R1
    ADDI  R1, -1

    ; Cycle E — loop back
    ; FETCH:   load JMP into IR
    ; COMPUTE: target = PC + offset (offset computed by assembler)
    ; STORE:   PC ← count_loop
    JMP   count_loop

; ── Done ────────────────────────────────────────────────────────────────────
done:
    ; Output 0 to signal completion
    STORE R2, R0              ; Mem[0x7F00] = 0
    NOP
