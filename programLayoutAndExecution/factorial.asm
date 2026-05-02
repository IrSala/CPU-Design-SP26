; ═══════════════════════════════════════════════════════════════════
; factorial.asm  —  Recursive factorial for the 16-bit Harvard CPU
;
; C equivalent:
;   unsigned factorial(unsigned n) {
;       if (n == 0) return 1;
;       return n * factorial(n - 1);
;   }
;   int main() { MMIO output of factorial(5); }
;
; Calling convention (used throughout):
;   - Caller places argument in R1 before JAL
;   - Return value comes back in R1
;   - R14 = link register (return address, saved by callee via PUSH)
;   - R15 = stack pointer (grows downward; PUSH = STORE + ADDI -1)
;
; Stack frame for each factorial() call:
;   [SP+2] = saved R14 (return address)
;   [SP+1] = saved R1  (n, the argument)
;   SP points one below the last pushed word
;
; Memory layout (Harvard):
;   Instruction memory: .text section assembled at 0x0000
;   Data memory:        .data section at 0x0000  (separate space)
;   Stack:              grows down from 0xFFFE (set in register.cpp)
;   MMIO output:        data address 0x7F00
; ═══════════════════════════════════════════════════════════════════

.text

; ── main ────────────────────────────────────────────────────────────
; Compute factorial(5) and output the result to MMIO.
main:
    ; Build MMIO address R3 = 0x7F00
    MOVI  R3, 0x7F
    MOVI  R6, 8
    SHL   R3, R3, R6          ; R3 = 0x7F00
    ADDI  R3, 1 ;R3 = 0x7F01
    ; Call factorial(5)
    MOVI  R1, 5               ; argument n = 5
    JAL   factorial            ; R14 = return addr; jump to factorial
                               ; result comes back in R1

    ; Output result to MMIO console
    STORE R3, R1              ; Mem[0x7F00] = factorial(5) = 120

    NOP                       ; halt

; ── factorial(n)  —  R1 = n on entry, R1 = n! on exit ──────────────
factorial:
    ; Prologue: save return address and n on the stack
    PUSH  R14                 ; save link register  (2 instructions)
    PUSH  R1                  ; save n              (2 instructions)

    ; Base case: if n == 0, return 1
    CMP   R1, R0              ; Z = 1 if n == 0
    BEQ   base_case

    ; Recursive case: call factorial(n - 1)
    ADDI  R1, -1              ; R1 = n - 1
    JAL   factorial            ; R1 = factorial(n - 1)

    ; Multiply: result = n * factorial(n-1)
    ; R1 now holds factorial(n-1); reload saved n from stack
    POP   R2                  ; R2 = saved n   (2 instructions)
    MUL   R1, R2, R1          ; R1 = n * factorial(n-1)

    ; Epilogue: restore return address and return
    POP   R14                 ; restore link register
    RET                       ; PC = R14

base_case:
    ; n == 0: return 1
    POP   R2                  ; discard saved n (balance the stack)
    MOVI  R1, 1               ; return value = 1
    POP   R14                 ; restore link register
    RET
