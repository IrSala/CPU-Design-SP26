# Running instructions — 16-bit Harvard CPU project

Everything you need to **build, run and test** the project. For the project
description, CPU schematic, ISA reference and walk-throughs see `README.md`.

The flow is always:

```text
.asm source  →  ./assembler_bin  →  .bin  →  ./run_emulator  →  output
```

---

## 1. Prerequisites

* `g++` with C++17 support (Apple Clang or GCC ≥ 7).
* GNU `make` (only needed for the assembler).
* No external libraries.

The repository ships with prebuilt arm64 executables (`assembler_bin`,
`run_emulator`, `tests/test_runner`, `tests/test_integration`,
`tests/test_e2e`), so on macOS arm64 you can skip straight to §3.

---

## 2. Building from source

### 2.1 One-command build (recommended)

A `build.sh` script at the project root compiles every binary, assembles
every demo program, and (optionally) runs the tests and the demos for you:

```bash
./build.sh              # build everything (binaries + .bin files)
./build.sh --run-tests  # build, then run all 3 test suites
./build.sh --run-demos  # build, then run all 4 demo programs
./build.sh --all        # build + run tests + run demos
./build.sh --clean      # remove every artefact the script produces
./build.sh --help       # show the inline help
```

After `./build.sh` finishes you have:

* `./assembler_bin`, `./run_emulator`
* `./tests/test_runner`, `./tests/test_integration`, `./tests/test_e2e`
* `./timer.bin`, `./hello.bin`, `./fib.bin`, `./factorial.bin`

…so any of the demos can be run immediately with e.g.
`./run_emulator timer.bin`.

The script aborts on the first compile error (`set -euo pipefail`) so build
failures are obvious. Test failures during `--run-tests` / `--all` are
tolerated and reported per suite — `test_e2e` is known to fail 6/15 on a
fresh build (see §7).

### 2.2 Building manually (what the script does under the hood)

Run every command **from the project root**. All build lines use the same
four include roots — sources mix `"isa.h"` / `"isa/isa.h"` and
`"assembler.h"` / `"assembler/assembler.h"`, so all four are required:

```text
-I. -I./assembler -I./emulator -I./isa
```

```bash
# 1. Assembler  (uses the Makefile, produces ./assembler_bin)
make

# 2. Emulator runner
g++ -std=c++17 -Wall -Wextra -I. -I./assembler -I./emulator -I./isa \
    run_emulator.cpp \
    emulator/control_unit.cpp emulator/Memory.cpp emulator/register.cpp \
    -o run_emulator

# 3. Test suites
g++ -std=c++17 -Wall -Wextra -I. -I./assembler -I./emulator -I./isa \
    tests/test_runner.cpp emulator/register.cpp \
    -o tests/test_runner

g++ -std=c++17 -Wall -Wextra -I. -I./assembler -I./emulator -I./isa \
    tests/test_integration.cpp assembler/assembler.cpp \
    emulator/Memory.cpp emulator/register.cpp emulator/control_unit.cpp \
    -o tests/test_integration

g++ -std=c++17 -Wall -Wextra -I. -I./assembler -I./emulator -I./isa \
    tests/test_e2e.cpp assembler/assembler.cpp \
    emulator/Memory.cpp emulator/register.cpp emulator/control_unit.cpp \
    -o tests/test_e2e
```

`make clean` deletes `./assembler_bin` **and every `*.bin` in the project
root**, including the convenience-shipped `factorial.bin`. The script's
`--clean` does the same, plus removes the test binaries. To regenerate
`factorial.bin` after a clean:

```bash
./assembler_bin programLayoutAndExecution/factorial.asm factorial.bin
```

---

## 3. Using the assembler

```text
Usage: ./assembler_bin <source.asm> [output.bin] [--listing] [--disasm] [--hex]
```

Optional flags:

* `--listing`  print a per-line annotated listing (PC, encoded word, source).
* `--disasm`   disassemble the output back to mnemonics (sanity check).
* `--hex`      hex-dump every encoded word in both memory banks.

The output `.bin` file is one blob with this header:

```text
[2 bytes : instr-word count][2 bytes : data-word count]
[ instr-word-count × 2 bytes : instruction words (big-endian) ]
[ data-word-count  × 2 bytes : data words        (big-endian) ]
```

---

## 4. Using the emulator

```text
Usage: ./run_emulator <file.bin>
```

It performs five steps:

1. Reads the header and loads instruction words into instruction memory.
2. Loads data words into data memory.
3. Resets all registers and the clock.
4. Runs Fetch → Decode → Execute up to **1000 cycles** or until PC walks
   off the end of the loaded program (whichever comes first).
5. Dumps both memory banks (instruction + data) with disassembly for
   post-mortem inspection.

MMIO output to `0x7F00` / `0x7F01` is interleaved live with the program’s
execution (it’s the program’s stdout).

---

## 5. Running the four bundled programs

```bash
# Timer — countdown 20 → 0, printed via MMIO_OUT_INT (0x7F01)
./assembler_bin programs/timer/timer.asm timer.bin
./run_emulator  timer.bin

# Hello, World — string from .data printed one char at a time via MMIO_OUT_CHAR
./assembler_bin programs/hello/hello_world.asm hello.bin
./run_emulator  hello.bin

# Fibonacci — first 10 numbers via MMIO_OUT_INT
./assembler_bin programs/fibonacci/fibonacci.asm fib.bin
./run_emulator  fib.bin

# Factorial (recursion + function calls + stack)
./assembler_bin programLayoutAndExecution/factorial.asm factorial.bin
./run_emulator  factorial.bin
```

A pre-assembled `factorial.bin` is included, so this works immediately
without needing the assembler:

```bash
./run_emulator factorial.bin
```

Expected outputs:

| Program     | Output                                               |
|-------------|------------------------------------------------------|
| `timer`     | `20`, `19`, `18`, … `1`, `0` (one per line)          |
| `fibonacci` | `0`, `1`, `1`, `2`, `3`, `5`, `8`, `13`, `21`, `34`  |
| `hello`     | `Hello, World!\n`                                    |
| `factorial` | `x` (ASCII 120 = 5! = 120 written to MMIO_OUT_CHAR)  |

---

## 6. Reference C program (factorial driver)

The C source for the recursive program lives in
`programLayoutAndExecution/factorial.cpp`. Compile and run it on the host
machine to check the expected output:

```bash
g++ -std=c++17 programLayoutAndExecution/factorial.cpp -o factorial_ref
./factorial_ref
```

It prints `n! = …` for `n = 0..10`.

---

## 7. Running the test suites

```bash
./tests/test_runner          # ALU + register file + ISA encode/decode unit tests
./tests/test_integration     # Memory bank + cache integration tests
./tests/test_e2e             # End-to-end (assembler → emulator) tests
```

Status against the **prebuilt** binaries shipped in this repo:

| Suite                  | Result          |
|------------------------|-----------------|
| `test_runner`          | 119 / 119 PASS  |
| `test_integration`     | 114 / 114 PASS  |
| `test_e2e` (prebuilt)  | 16 / 16 PASS    |

Status when **rebuilt from current sources** with the build commands in §2:

| Suite                  | Result          |
|------------------------|-----------------|
| `test_runner`          | 119 / 119 PASS  |
| `test_integration`     | 114 / 114 PASS  |
| `test_e2e` (rebuilt)   | 9 PASS / 6 FAIL |
