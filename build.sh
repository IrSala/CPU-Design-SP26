#!/usr/bin/env bash
# build.sh — one-command build for the 16-bit Harvard CPU project.
#
# Compiles the assembler, the emulator runner, and all three test
# binaries, then assembles every demo program into a .bin so you can
# run them immediately.
#
# Usage:
#   ./build.sh              # build everything
#   ./build.sh --run-tests  # build, then run all 3 test suites
#   ./build.sh --run-demos  # build, then run all 4 demo programs
#   ./build.sh --all        # build + run tests + run demos
#   ./build.sh --clean      # remove every artefact this script produces, then exit
#   ./build.sh -h|--help    # show this help

set -euo pipefail
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++17 -Wall -Wextra -O2"
INCLUDES=(-I. -I./assembler -I./emulator -I./isa)

run_tests=0
run_demos=0
do_clean=0

for arg in "$@"; do
    case "$arg" in
        --run-tests) run_tests=1 ;;
        --run-demos) run_demos=1 ;;
        --all)       run_tests=1; run_demos=1 ;;
        --clean)     do_clean=1 ;;
        -h|--help)
            sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "Unknown flag: $arg" >&2
            echo "Try: ./build.sh --help" >&2
            exit 2
            ;;
    esac
done

step() { echo; echo "=== $* ==="; }

if [[ $do_clean -eq 1 ]]; then
    step "Clean"
    rm -f assembler_bin run_emulator
    rm -f tests/test_runner tests/test_integration tests/test_e2e
    rm -f timer.bin hello.bin fib.bin factorial.bin
    echo "Removed: assembler_bin, run_emulator, tests/test_*, *.bin"
    exit 0
fi

# ---------------------------------------------------------------------------
# 1. Build C++ binaries
# ---------------------------------------------------------------------------

step "Build assembler_bin"
"$CXX" $CXXFLAGS "${INCLUDES[@]}" \
    main.cpp assembler/assembler.cpp \
    -o assembler_bin

step "Build run_emulator"
"$CXX" $CXXFLAGS "${INCLUDES[@]}" \
    run_emulator.cpp \
    emulator/control_unit.cpp emulator/Memory.cpp emulator/register.cpp \
    -o run_emulator

step "Build tests/test_runner"
"$CXX" $CXXFLAGS "${INCLUDES[@]}" \
    tests/test_runner.cpp emulator/register.cpp \
    -o tests/test_runner

step "Build tests/test_integration"
"$CXX" $CXXFLAGS "${INCLUDES[@]}" \
    tests/test_integration.cpp assembler/assembler.cpp \
    emulator/Memory.cpp emulator/register.cpp emulator/control_unit.cpp \
    -o tests/test_integration

step "Build tests/test_e2e"
"$CXX" $CXXFLAGS "${INCLUDES[@]}" \
    tests/test_e2e.cpp assembler/assembler.cpp \
    emulator/Memory.cpp emulator/register.cpp emulator/control_unit.cpp \
    -o tests/test_e2e

# ---------------------------------------------------------------------------
# 2. Assemble all demo programs
# ---------------------------------------------------------------------------

step "Assemble demo programs"
./assembler_bin programs/timer/timer.asm                timer.bin
./assembler_bin programs/hello/hello_world.asm          hello.bin
./assembler_bin programs/fibonacci/fibonacci.asm        fib.bin
./assembler_bin programLayoutAndExecution/factorial.asm factorial.bin

# ---------------------------------------------------------------------------
# 3. Optional: run tests
# ---------------------------------------------------------------------------

if [[ $run_tests -eq 1 ]]; then
    # Don't abort the script on test failures — we want to show every suite's
    # result and still continue to the demo runs / summary below.
    set +e
    step "Run tests/test_runner";      ./tests/test_runner      | tail -4; rc1=${PIPESTATUS[0]}
    step "Run tests/test_integration"; ./tests/test_integration | tail -4; rc2=${PIPESTATUS[0]}
    step "Run tests/test_e2e";         ./tests/test_e2e         | tail -4; rc3=${PIPESTATUS[0]}
    set -e
    [[ $rc3 -ne 0 ]] && \
        echo "(test_e2e exits non-zero on a fresh build — known regression, see running_instructions.md §7)"
fi

# ---------------------------------------------------------------------------
# 4. Optional: run demo programs
# ---------------------------------------------------------------------------

if [[ $run_demos -eq 1 ]]; then
    step "Run timer.bin (countdown 20 -> 0)"
    ./run_emulator timer.bin | sed -n '1,22p'
    step "Run hello.bin"
    ./run_emulator hello.bin | sed -n '1,2p'
    step "Run fib.bin (first 10 Fibonacci numbers)"
    ./run_emulator fib.bin | sed -n '1,11p'
    step "Run factorial.bin (5! = 120 = ASCII 'x')"
    ./run_emulator factorial.bin | sed -n '1,2p'
fi

# ---------------------------------------------------------------------------
# 5. Summary
# ---------------------------------------------------------------------------

step "Build complete"
echo "Binaries: ./assembler_bin  ./run_emulator"
echo "Tests:    ./tests/test_runner  ./tests/test_integration  ./tests/test_e2e"
echo "Demos:    ./timer.bin  ./hello.bin  ./fib.bin  ./factorial.bin"
echo
echo "Try:"
echo "  ./run_emulator timer.bin       # countdown 20 -> 0"
echo "  ./run_emulator hello.bin       # Hello, World!"
echo "  ./run_emulator fib.bin         # first 10 Fibonacci numbers"
echo "  ./run_emulator factorial.bin   # 5! = 120 (printed as 'x')"
echo
echo "Run all tests:  ./build.sh --run-tests"
echo "Run all demos:  ./build.sh --run-demos"
echo "Clean:          ./build.sh --clean"
