# 16-bit Harvard CPU — Software Design (CS220, Spring 2026)

A complete software model of a 16-bit, Harvard-architecture CPU together with
a two-pass assembler, an instruction-accurate emulator with memory-mapped I/O,
three demo programs (timer, hello world, fibonacci), and a recursive
factorial program that demonstrates function calls and stack handling.

Everything is plain C++17. No external dependencies. Builds and runs on macOS
(Apple Silicon / Intel) and Linux.

> **Looking for build / run / test commands?** They live in
> [`running_instructions.md`](running_instructions.md). This README only
> describes *what* the project is — its layout, CPU architecture, ISA, and
> per-program walk-throughs.

---

## 1. Project layout

```text
CPU-Design-SP26/
├── isa/
│   └── isa.h                       # Opcodes, encode/decode helpers, MMIO map
│
├── emulator/                       # The CPU itself
│   ├── register.{h,cpp}            # 16 GPRs + PC + IR + SP
│   ├── alu.h                       # ALU + 4 status flags (Z / N / C / V)
│   ├── bus.h                       # DataBus / AddressBus / InstructionBus
│   ├── Memory.{h,cpp}              # Harvard memory + optional caches + MMIO
│   ├── control_unit.{h,cpp}        # Fetch → Decode → Execute state machine
│   └── cpu.h                       # Top-level CPU struct (wires it all up)
│
├── assembler/
│   ├── assembler.{h,cpp}           # Two-pass assembler (labels + literals)
│   └── (built into ./assembler_bin)
├── main.cpp                        # CLI driver for the assembler
│
├── run_emulator.cpp                # CLI loader/runner for .bin files
│
├── programs/
│   ├── timer/timer.asm             # Countdown 20 → 0 (Fetch/Compute/Store demo)
│   ├── hello/hello_world.asm       # "Hello, World!" via MMIO
│   └── fibonacci/fibonacci.asm     # First 10 Fibonacci numbers
│
├── programLayoutAndExecution/
│   ├── factorial.cpp               # C reference: recursive factorial + main()
│   └── factorial.asm               # Same program assembled for our CPU
│
├── tests/
│   ├── test_runner.cpp             # Unit tests (ALU, regs, ISA encode/decode)
│   ├── test_integration.cpp       # Memory + cache integration tests
│   └── test_e2e.cpp                # End-to-end program tests
│
├── Makefile                        # Builds the assembler (`make`)
├── assembler_bin                   # Pre-built assembler executable (arm64)
├── run_emulator                    # Pre-built emulator executable (arm64)
└── factorial.bin                   # Pre-assembled factorial program
```

---

## 2. CPU schematic

The processor is a classic 3-stage Harvard machine. Instruction memory and
data memory are physically separate banks, each with its own address space.

```text
                ┌────────────────────────────────────────────────┐
                │                  CONTROL UNIT                  │
                │  ┌──────────┐   ┌──────────┐   ┌────────────┐  │
                │  │  FETCH   │ → │  DECODE  │ → │  EXECUTE   │  │
                │  └──────────┘   └──────────┘   └────────────┘  │
                └──────┬───────────────┬─────────────┬───────────┘
                       │ PC            │ opcode      │ control signals
                       ▼               ▼             ▼
        ┌──────────────────────┐    ┌────────────────────────────┐
        │   INSTRUCTION MEM    │    │      REGISTER FILE         │
        │   (Harvard, R-only)  │    │   R0 (zero) … R13          │
        │                      │    │   R14 = link (JAL)         │
        │   16-bit words,      │    │   R15 = stack pointer      │
        │   PC-addressed       │    │   PC, IR (special)         │
        └──────────┬───────────┘    └─────┬───────────────┬──────┘
                   │ instruction         │ Rs1           │ Rs2
                   ▼                     ▼               ▼
        ┌────────────────────┐    ┌────────────────────────────┐
        │  INSTRUCTION BUS   │    │            ALU             │
        │  (16-bit)          │    │   ADD/SUB/AND/OR/MUL       │
        └────────────────────┘    │   SHL/SHR/CMP/PASS_A       │
                                  │   Flags: Z  N  C  V        │
                                  └────────────┬───────────────┘
                                               │ result / flags
                                               ▼
                       ┌─────────────┬───────────────────┐
                       │  ADDR BUS   │      DATA BUS     │
                       └──────┬──────┴─────────┬─────────┘
                              ▼                ▼
                    ┌───────────────────────────────────┐
                    │          DATA MEMORY              │
                    │   0x0000 … program data            │
                    │   0x7F00  ── MMIO_OUT_CHAR (write) │
                    │   0x7F01  ── MMIO_OUT_INT  (write) │
                    │   …                                │
                    │   0xFFFE  ── stack top (R15 init)  │
                    └───────────────────────────────────┘
```

A single `step()` of the control unit performs one full
**Fetch → Decode → Execute** cycle and ticks the clock once.

---

## 3. ISA summary

### 3.1 Instruction format

Every instruction is exactly 16 bits.

```text
 15      12 11    8 7     4 3     0
┌─────────┬──────┬───────┬───────┐
│ opcode  │  Rd  │  Rs1  │  Rs2  │   R-type (ADD, SUB, AND, OR, MUL, SHL, SHR, CMP)
├─────────┼──────┼───────┴───────┤
│ opcode  │  Rd  │     imm8      │   I-type (MOVI, ADDI)
├─────────┼──────┼───────┬───────┤
│ opcode  │  Rd  │ Raddr │   -   │   M-type (LOAD)         Rd ← Mem[Raddr]
│ opcode  │ Rdst │  Rs1  │   -   │   M-type (STORE)        Mem[Rdst] ← Rs1
├─────────┼──────┴───────┴───────┤
│ opcode  │       imm12          │   J-type (JMP, JAL)     PC-relative signed
├─────────┼──────┬───────────────┤
│ opcode  │  Rd  │       -       │   JR-type (JR Rd)
├─────────┼──┬──────────────────┤
│  0xF    │c │     imm11         │   BRANCH  c=0→BEQ, c=1→BNE
└─────────┴──┴──────────────────┘
```

### 3.2 Instructions (16 opcodes, all 4 bits)

| Hex | Mnemonic | Effect                                              |
|----:|----------|-----------------------------------------------------|
| 0x0 | `ADD`    | Rd ← Rs1 + Rs2                                      |
| 0x1 | `SUB`    | Rd ← Rs1 − Rs2                                      |
| 0x2 | `AND`    | Rd ← Rs1 & Rs2                                      |
| 0x3 | `OR`     | Rd ← Rs1 \| Rs2                                     |
| 0x4 | `MUL`    | Rd ← (Rs1 × Rs2) mod 2¹⁶                            |
| 0x5 | `SHL`    | Rd ← Rs1 << Rs2                                     |
| 0x6 | `SHR`    | Rd ← Rs1 >> Rs2                                     |
| 0x7 | `CMP`    | flags ← (Rs1 − Rs2); result discarded               |
| 0x8 | `LOAD`   | Rd ← Mem[Rs1]                                       |
| 0x9 | `STORE`  | Mem[Rd] ← Rs1   (Rd holds the address)              |
| 0xA | `MOVI`   | Rd ← imm8 (zero-extended)                           |
| 0xB | `ADDI`   | Rd ← Rd + imm8 (sign-extended)                      |
| 0xC | `JMP`    | PC ← PC + imm12                                     |
| 0xD | `JAL`    | R14 ← PC; PC ← PC + imm12       (call)              |
| 0xE | `JR`     | PC ← Rd                          (return: `JR R14`) |
| 0xF | `BRANCH` | BEQ if Z==1 / BNE if Z==0; PC ← PC + imm11          |

Pseudo-instructions expanded by the assembler:

| Pseudo            | Expansion                                  |
|-------------------|--------------------------------------------|
| `NOP`             | `ADD R0, R0, R0`                           |
| `RET`             | `JR R14`                                   |
| `MOV Rd, Rs`      | `ADD Rd, Rs, R0`                           |
| `LI  Rd, imm`     | `MOVI Rd, imm`                             |
| `CALL label`      | `JAL label`                                |
| `PUSH Rs`         | `STORE R15, Rs ; ADDI R15, -1` (2 words)   |
| `POP  Rd`         | `ADDI R15, 1 ; LOAD Rd, R15`   (2 words)   |

### 3.3 Encoding — examples

```text
ADD  R3, R1, R2   →  0x0312
MOVI R5, 0x7F     →  0xA57F
SHL  R5, R5, R6   →  0x5556
JAL  factorial    →  0xD00? (offset depends on PC)
BEQ  done         →  0xF00? (cond=0, signed offset)
STORE [R5], R1    →  0x9510   (Mem[R5] ← R1)
```

All encoders/decoders live in `isa/isa.h` (`encodeR`, `encodeI`, `encodeM`,
`encodeJ`, `encodeBEQ`, `encodeBNE`, plus matching `decode*`).

### 3.4 Addressing modes

| Mode                | Used by                | Example                          |
|---------------------|------------------------|----------------------------------|
| Register-direct     | All R-type, JR         | `ADD R1, R2, R3`                 |
| Immediate (8-bit)   | MOVI, ADDI             | `MOVI R1, 20`  /  `ADDI R1, -1`  |
| Register-indirect   | LOAD, STORE            | `LOAD R2, [R1]` / `STORE [R3], R2` |
| PC-relative (12-bit)| JMP, JAL               | `JMP loop`   /   `JAL factorial` |
| PC-relative (11-bit)| BEQ, BNE               | `BEQ done`                       |

### 3.5 Flag semantics (set by the ALU)

| Flag | Meaning                                                       |
|------|---------------------------------------------------------------|
| `Z`  | result == 0                                                   |
| `N`  | result MSB == 1 (signed-negative)                             |
| `C`  | unsigned carry-out / borrow on ADD/SUB/SHL                    |
| `V`  | signed overflow on ADD / SUB / CMP                            |

Branches read these flags:

* `BEQ` taken when `Z == 1`
* `BNE` taken when `Z == 0`

`CMP Rs1, Rs2` is `SUB` whose result is thrown away — only the flags survive,
which is what makes the typical `CMP / BEQ` idiom work.

### 3.6 Memory map

```text
INSTRUCTION MEMORY (Harvard bank, read-only at runtime)
  0x0000 ─────────  program code starts here  (PC starts at 0)
  …
  0x00FF ─────────  default size = 256 words   (configurable in run_emulator.cpp)

DATA MEMORY (Harvard bank, read/write)
  0x0000 ─────────  .data section (strings, .word constants)
  0x0001 …
   …
  0x7F00 ─────────  MMIO_OUT_CHAR  ── STORE here writes a character to stdout
  0x7F01 ─────────  MMIO_OUT_INT   ── STORE here writes an integer + newline
   …
  0xFFFE ─────────  initial value of R15 (stack pointer); stack grows down
  0xFFFF ─────────  end of data memory (64 KiB)
```

Register conventions:

* `R0`  hard-wired to `0` (writes ignored).
* `R14` link register, written by `JAL`, read by `JR`/`RET`.
* `R15` stack pointer, initialised to `0xFFFE` by `Registers::reset()`.

---

## 4. Walk-through: how the timer program executes

`programs/timer/timer.asm` is the canonical demo of the
**Fetch / Compute / Store** cycle. Each instruction goes through these stages:

```text
FETCH    PC → AddressBus → InstructionMemory
         word → InstructionBus → IR
         PC ← PC + 1
COMPUTE  ControlUnit decodes IR
         operands → DataBus → ALU
         ALU computes, sets Z/N/C/V flags
STORE    Result routed to one of:
           • destination register (R-type, I-type, LOAD)
           • DataMemory[address-bus]   (STORE  — incl. MMIO writes)
           • PC                         (JMP / JAL / BEQ / BNE / JR)
```

Inside `count_loop`:

```text
CMP   R1, R0     ; FETCH 0x7010 ; COMPUTE R1-R0 ; STORE flags (Z if R1==0)
BEQ   done       ; FETCH 0xF00X ; COMPUTE check Z; STORE PC if taken
STORE R2, R1     ; FETCH 0x9210 ; COMPUTE Mem[R2]←R1 ; STORE 0x7F01 → MMIO
ADDI  R1, -1     ; FETCH 0xB1FF ; COMPUTE R1-1     ; STORE → R1
JMP   count_loop ; FETCH 0xCFFB ; COMPUTE PC-5     ; STORE → PC
```

Running it produces `20 19 18 … 1 0` on stdout, one per line.

---

## 5. Walk-through: how recursion is handled (factorial)

The C reference (`programLayoutAndExecution/factorial.cpp`):

```cpp
unsigned long long factorial(unsigned int n) {
    if (n == 0) return 1;
    return n * factorial(n - 1);
}
int main() { /* prints factorial(0..10) */ }
```

The hand-assembled equivalent (`programLayoutAndExecution/factorial.asm`)
calls `factorial(5)` and writes the result (120 = `'x'`) to `MMIO_OUT_CHAR`.

### 5.1 Memory layout at load time

```text
INSTRUCTION MEMORY                       DATA MEMORY
  0x0000  MOVI R3, 0x7F                    0x0000  ┐
  0x0001  MOVI R6, 8                              │  (no .data in this program)
  0x0002  SHL  R3, R3, R6                         │
  0x0003  MOVI R1, 5                              │
  0x0004  JAL  factorial      ── main ──          │
  0x0005  STORE [R3], R1                          │
  0x0006  NOP   (halt)                            │
  0x0007  STORE [R15], R14    ── factorial ──     │
  0x0008  ADDI  R15, -1       (PUSH R14)          │
  0x0009  STORE [R15], R1                         │
  0x000A  ADDI  R15, -1       (PUSH R1)           │
  0x000B  CMP   R1, R0                            │
  0x000C  BEQ   base_case                         │
  0x000D  ADDI  R1, -1                            │
  0x000E  JAL   factorial    (recursive call)     │
  0x000F  ADDI  R15, 1                            │
  0x0010  LOAD  R2, [R15]    (POP R2 = saved n)   │
  0x0011  MUL   R1, R2, R1   (R1 = n * fact(n-1)) │
  0x0012  ADDI  R15, 1                            │
  0x0013  LOAD  R14, [R15]   (POP R14)            │
  0x0014  JR    R14          (RET)                │
  0x0015  ADDI  R15, 1       (base case POP n)    │
  0x0016  LOAD  R2, [R15]                         │
  0x0017  MOVI  R1, 1        (return 1)           │
  0x0018  ADDI  R15, 1                            │
  0x0019  LOAD  R14, [R15]                        ▼
  0x001A  JR    R14                              0xFFFE  ← R15 starts here
                                                 stack grows DOWNWARD
```

### 5.2 Calling convention

* Argument in `R1`, return value in `R1`.
* `R14` is the link register (written by `JAL`, restored by `RET`).
* `R15` is the stack pointer; `PUSH` is `STORE [R15], Rs ; ADDI R15, -1`,
  `POP` is `ADDI R15, 1 ; LOAD Rd, [R15]`.

### 5.3 Stack growth during `factorial(5)`

Before any push, `R15 = 0xFFFE`. Each recursive frame stores **R14 then n**:

```text
factorial(5)  pushes 14, 5      → R15 = 0xFFFA
factorial(4)  pushes 14, 4      → R15 = 0xFFF6
factorial(3)  pushes 14, 3      → R15 = 0xFFF2
factorial(2)  pushes 14, 2      → R15 = 0xFFEE
factorial(1)  pushes 14, 1      → R15 = 0xFFEA
factorial(0)  pushes 14, 0      → R15 = 0xFFE6   (base case)
```

Stack at maximum depth (one slot per word):

```text
addr     value       owned by
0xFFFE   ret-addr    factorial(5)
0xFFFD   5           factorial(5)
0xFFFC   ret-addr    factorial(4)
0xFFFB   4           factorial(4)
…
0xFFE7   ret-addr    factorial(0)
0xFFE6   0           factorial(0)  ← base case POPs and returns 1
```

The base case returns `1` in `R1` and `JR R14` jumps back to the caller’s
post-`JAL` instruction. Each unwound frame `POP`s its saved `n` into `R2`,
multiplies (`MUL R1, R2, R1`), `POP`s `R14`, and `RET`s. After five returns
`R1 = 5*4*3*2*1*1 = 120`, which `main` writes to `MMIO_OUT_CHAR (0x7F00)`,
producing the literal character `x` (ASCII 120) on stdout.

### 5.4 Per-call Fetch/Compute/Store walkthrough

```text
JAL  factorial   FETCH  : load 0xD00X into IR
                 COMPUTE: target = PC + imm12
                 STORE  : R14 ← PC (return addr); PC ← target

PUSH R14         FETCH  : STORE [R15], R14
                 COMPUTE: addr = R15
                 STORE  : Mem[R15] ← R14
                 then     ADDI R15, -1   (decrement SP)

CMP  R1, R0      FETCH  : 0x7010
                 COMPUTE: ALU R1 - R0  (sets Z if n==0)
                 STORE  : flags only

BEQ  base_case   FETCH  : 0xF00X
                 COMPUTE: read Z flag
                 STORE  : PC ← target if Z==1

MUL  R1, R2, R1  FETCH  : 0x4121
                 COMPUTE: R2 * R1 in ALU
                 STORE  : R1 ← result

JR   R14         FETCH  : 0xEE00
                 COMPUTE: read R14
                 STORE  : PC ← R14
```

This is the recursion-and-stack proof. You can watch every step yourself
by running `./run_emulator factorial.bin` — the post-execution dump shows
both memory banks plus the disassembly with addresses.

---

## 6. Requirements coverage

| Requirement                                                 | Where it lives                                                                                            | Met? |
|-------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------|:----:|
| **CPU schematic** (simple drawing of CPU architecture)      | Section 2 of this README (ASCII block diagram of fetch/decode/execute, regs, ALU, buses, memory, MMIO)    | Yes  |
| **ISA — instruction format**                                | `isa/isa.h` (top comment + bit-field doc) and §3.1 here                                                   | Yes  |
| **ISA — instructions**                                      | `enum class Opcode` in `isa/isa.h`; pseudo-ops in `assembler/assembler.h`                                 | Yes  |
| **ISA — encoding**                                          | `encodeR / encodeI / encodeM / encodeJ / encodeBEQ / encodeBNE` and matching `decode*` in `isa/isa.h`     | Yes  |
| **ISA — addressing modes**                                  | §3.4 here; implemented in `emulator/control_unit.cpp`                                                     | Yes  |
| **ISA — flag semantics**                                    | `struct Flags` + `ALU::execute` in `emulator/alu.h`; consumed by BRANCH in `control_unit.cpp`             | Yes  |
| **ISA — memory map**                                        | `MMIO_OUT`/`MMIO_IN` and SP layout in `isa/isa.h` + `Memory::MMIO_OUT_CHAR/INT` in `emulator/Memory.h`    | Yes  |
| **Emulator — registers**                                    | `emulator/register.{h,cpp}` (16 GPRs, IR, PC, SP)                                                         | Yes  |
| **Emulator — ALU**                                          | `emulator/alu.h` (11 ops, all four flags)                                                                 | Yes  |
| **Emulator — control unit**                                 | `emulator/control_unit.{h,cpp}` (3-stage Fetch / Decode / Execute, clock, dispatch over all 16 opcodes)   | Yes  |
| **Emulator — bus**                                          | `emulator/bus.h` (DataBus, AddressBus, InstructionBus); wired in `emulator/cpu.h`                         | Yes  |
| **Emulator — memory**                                       | `emulator/Memory.{h,cpp}` (separate Harvard banks, optional set-associative caches with LRU + write-back) | Yes  |
| **Emulator — memory-mapped I/O**                            | `MMIO_OUT_CHAR=0x7F00`, `MMIO_OUT_INT=0x7F01` intercepted in `ControlUnit::execute` STORE case            | Yes  |
| **Emulator — load, run and memory dump**                    | `run_emulator.cpp` (loader + 1000-cycle run loop + post-run `dumpInstructionMemory` / `dumpDataMemory`)   | Yes  |
| **Assembler — code production**                             | `assembler/assembler.cpp` two-pass assembler (`pass1` collects labels, `pass2` emits encoded words)       | Yes  |
| **Assembler — labels and numeric literals**                 | `labels_` map + `parseInt` (decimal, `0x…`, `0b…`, `'c'`, `'\n'`); `.word` / `.string` / `.ascii`         | Yes  |
| **Programs — Timer (Fetch/Compute/Store demo)**             | `programs/timer/timer.asm` (heavily annotated per-stage)                                                  | Yes  |
| **Programs — Hello, World**                                 | `programs/hello/hello_world.asm` (uses `.data .string` + MMIO_OUT_CHAR)                                   | Yes  |
| **Programs — Fibonacci sequence**                           | `programs/fibonacci/fibonacci.asm` (first 10 terms via MMIO_OUT_INT)                                      | Yes  |
| **Recursive C function + driver**                           | `programLayoutAndExecution/factorial.cpp` (recursive `factorial(n)` + `main()`)                           | Yes  |
| **Executable laid out in memory + function calls + recursion shown on the software CPU** | `programLayoutAndExecution/factorial.asm` + §5 of this README                | Yes  |

**Summary:** every listed requirement is satisfied by code that already
exists in this repository. For build/run/test commands and the current
test-suite status (including the known `test_e2e` rebuild regression),
see [`running_instructions.md`](running_instructions.md). None of that
affects the user-visible programs — assemble any of the four `.asm` files
and run the result through `./run_emulator` to verify behaviour
end-to-end.
