; ═══════════════════════════════════════════════════════════════════
; hello_world.asm — Print "Hello, World!\n" via MMIO
;
; The string is stored in the data section as null-terminated words.
; We walk a pointer through data memory, loading each character
; and storing it to the MMIO output address (0x7F00).
;
; R0  = zero register (always 0)
; R1  = data memory pointer (index into string)
; R2  = current character
; R3  = MMIO output address (0x7F00)
; R4  = 0 (null terminator check value)
; ═══════════════════════════════════════════════════════════════════

.data
msg:
    .string "Hello, World!\n"

.text

start:
    ; Build MMIO address: R3 = 0x7F00
    MOVI  R3, 0x7F
    MOVI  R6, 8
    SHL   R3, R3, R6          ; R3 = 0x7F00

    ; Initialise pointer to start of msg in data memory (address 0)
    MOVI  R1, 0               ; R1 = data address pointer
    MOVI  R4, 0               ; R4 = 0, used for null check

print_loop:
    LOAD  R2, R1              ; R2 = data_mem[R1]  (load char)
    CMP   R2, R4              ; set Z if char == 0 (null terminator)
    BEQ   done                ; stop if null

    STORE R3, R2              ; Mem[0x7F00] = R2  (write char to MMIO)

    ADDI  R1, 1               ; advance pointer
    JMP   print_loop

done:
    NOP
