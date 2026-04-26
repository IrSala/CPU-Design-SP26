/**
 * test_e2e.cpp — Layer 3 end-to-end tests
 *
 * Covers:
 *   assemble → load into Memory → run via ControlUnit → capture MMIO output
 *
 *   Program 1 — timer.asm       expected MMIO: 20, 19, 18 … 1, 0, 0
 *   Program 2 — fibonacci.asm   expected MMIO: 0, 1, 1, 2, 3, 5, 8, 13, 21, 34
 *   Program 3 — hello_world.asm expected MMIO: ASCII codes of "Hello, World!\n"
 *
 * How MMIO capture works:
 *   ControlUnit writes to MMIO_OUT (0x7F00) via memory_.writeData().
 *   We subclass Memory and override writeData to intercept that address,
 *   storing the value in a vector instead of (or in addition to) printing it.
 *   This lets tests assert on exact output without parsing stdout.
 *
 * How halt works:
 *   The ISA has no HALT instruction. We step manually and stop when the PC
 *   moves past the last loaded instruction word (PC >= programSize).
 *   This prevents the control unit from fetching into unmapped memory.
 *
 * Build (from project root):
 *   g++ -std=c++17 -Wall -Wextra \
 *       -I./isa -I./emulator -I./assembler \
 *       tests/test_e2e.cpp \
 *       emulator/register.cpp \
 *       emulator/Memory.cpp \
 *       emulator/control_unit.cpp \
 *       assembler/assembler.cpp \
 *       -o tests/test_e2e
 *
 * Run:
 *   ./tests/test_e2e
 */

#include "assembler.h"
#include "control_unit.h"
#include "isa.h"
#include "Memory.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// ════════════════════════════════════════════════════════════════════════════
//  Minimal test harness
// ════════════════════════════════════════════════════════════════════════════

static int  s_passed = 0;
static int  s_failed = 0;
static std::string s_suite;

static void beginSuite(const std::string& name) {
    s_suite = name;
    std::cout << "\n── " << name << " ──\n";
}

static void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "  PASS  " << label << '\n';
        ++s_passed;
    } else {
        std::cout << "  FAIL  " << label << "  [suite: " << s_suite << "]\n";
        ++s_failed;
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  CapturingMemory — intercepts MMIO writes into a vector
// ════════════════════════════════════════════════════════════════════════════

class CapturingMemory : public Memory {
public:
    std::vector<uint16_t> output;

    CapturingMemory(std::size_t instrWords, std::size_t dataWords)
        : Memory(instrWords, dataWords) {}

    void writeData(uint16_t address, uint16_t value) {
        if (address == MMIO_OUT) {
            output.push_back(value);
        } else {
            Memory::writeData(address, value);
        }
    }

    uint16_t readData(uint16_t address) const {
        if (address == MMIO_IN) return 0;
        return Memory::readData(address);
    }
};

// ════════════════════════════════════════════════════════════════════════════
//  runProgram
//
//  Steps the CPU one instruction at a time and stops when:
//    (a) PC >= programSize  — execution fell off the end of loaded code, or
//    (b) maxCycles reached  — safety limit to catch infinite loops in tests
//
//  Returns the captured MMIO output vector.
// ════════════════════════════════════════════════════════════════════════════

static std::vector<uint16_t> runProgram(const std::string& source,
                                        std::size_t maxCycles,
                                        bool* assembledOk = nullptr)
{
    Assembler asmr;
    AssemblerResult result = asmr.assemble(source);

    if (assembledOk) *assembledOk = result.success;

    if (!result.success) {
        for (const auto& e : result.errors)
            std::cerr << "  assembler error: " << e << '\n';
        return {};
    }

    const auto programSize = static_cast<uint16_t>(result.instructions.size());

    // 1024-word instruction space, 32768-word data space (covers 0x7F00)
    CapturingMemory mem(1024, 32768);
    mem.loadInstructionMemory(result.instructions);
    if (!result.data.empty())
        mem.loadDataMemory(result.data);

    Registers   regs;
    ALU         alu;
    ControlUnit cu(regs, alu, mem);
    cu.reset();

    for (std::size_t cycle = 0; cycle < maxCycles; ++cycle) {
        // Stop cleanly when PC falls off the end of loaded code
        if (regs.getprogramcountRegister() >= programSize)
            break;
        cu.step();
    }

    return mem.output;
}

// ════════════════════════════════════════════════════════════════════════════
//  Program sources (inline so tests are self-contained)
// ════════════════════════════════════════════════════════════════════════════

static const std::string SRC_TIMER = R"(
.text
start:
    MOVI  R2, 0x7F
    MOVI  R6, 8
    SHL   R2, R2, R6
    MOVI  R1, 20
count_loop:
    CMP   R1, R0
    BEQ   done
    STORE R2, R1
    ADDI  R1, -1
    JMP   count_loop
done:
    STORE R2, R0
    NOP
)";

static const std::string SRC_FIBONACCI = R"(
.text
start:
    MOVI  R5, 0x7F
    MOVI  R6, 8
    SHL   R5, R5, R6
    MOVI  R1, 0
    MOVI  R2, 1
    MOVI  R4, 10
loop:
    CMP   R4, R0
    BEQ   done
    STORE R5, R1
    ADD   R3, R1, R2
    MOV   R1, R2
    MOV   R2, R3
    ADDI  R4, -1
    JMP   loop
done:
    NOP
)";

static const std::string SRC_HELLO = R"(
.data
msg:
    .string "Hello, World!\n"
.text
start:
    MOVI  R3, 0x7F
    MOVI  R6, 8
    SHL   R3, R3, R6
    MOVI  R1, 0
    MOVI  R4, 0
print_loop:
    LOAD  R2, R1
    CMP   R2, R4
    BEQ   done
    STORE R3, R2
    ADDI  R1, 1
    JMP   print_loop
done:
    NOP
)";

// ════════════════════════════════════════════════════════════════════════════
//  Suite 1 — timer.asm
// ════════════════════════════════════════════════════════════════════════════

static void testTimer() {
    beginSuite("timer.asm");

    bool ok = false;
    auto out = runProgram(SRC_TIMER, 5000, &ok);

    check(ok, "timer: assembles without error");

    // Expected: 20 down to 1 from the loop, then 0 from STORE R2, R0 after done:
    std::vector<uint16_t> expected;
    for (int i = 20; i >= 1; --i)
        expected.push_back(static_cast<uint16_t>(i));
    expected.push_back(0);  // loop exits when R1==0, then STORE R2, R0

    check(out.size() == expected.size(),
          "timer: output length = " + std::to_string(expected.size()) +
          " (got " + std::to_string(out.size()) + ")");

    bool allMatch = (out == expected);
    check(allMatch, "timer: output sequence matches 20..1, 0");

    if (!allMatch) {
        std::cout << "    actual:   ";
        for (auto v : out) std::cout << v << ' ';
        std::cout << '\n';
        std::cout << "    expected: ";
        for (auto v : expected) std::cout << v << ' ';
        std::cout << '\n';
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Suite 2 — fibonacci.asm
// ════════════════════════════════════════════════════════════════════════════

static void testFibonacci() {
    beginSuite("fibonacci.asm");

    bool ok = false;
    auto out = runProgram(SRC_FIBONACCI, 5000, &ok);

    check(ok, "fibonacci: assembles without error");

    std::vector<uint16_t> expected = {0, 1, 1, 2, 3, 5, 8, 13, 21, 34};

    check(out.size() == expected.size(),
          "fibonacci: output length = 10 (got " + std::to_string(out.size()) + ")");

    bool allMatch = (out == expected);
    check(allMatch, "fibonacci: output = 0 1 1 2 3 5 8 13 21 34");

    if (!allMatch) {
        std::cout << "    actual:   ";
        for (auto v : out) std::cout << v << ' ';
        std::cout << '\n';
        std::cout << "    expected: ";
        for (auto v : expected) std::cout << v << ' ';
        std::cout << '\n';
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Suite 3 — hello_world.asm
// ════════════════════════════════════════════════════════════════════════════

static void testHelloWorld() {
    beginSuite("hello_world.asm");

    bool ok = false;
    auto out = runProgram(SRC_HELLO, 5000, &ok);

    check(ok, "hello_world: assembles without error");

    std::string expected_str = "Hello, World!\n";
    std::vector<uint16_t> expected;
    for (char c : expected_str)
        expected.push_back(static_cast<uint16_t>(static_cast<unsigned char>(c)));

    check(out.size() == expected.size(),
          "hello_world: output length = " + std::to_string(expected.size()) +
          " chars (got " + std::to_string(out.size()) + ")");

    bool allMatch = (out == expected);
    check(allMatch, "hello_world: output matches \"Hello, World!\\n\"");

    if (!allMatch) {
        std::cout << "    actual:   ";
        for (auto v : out) std::cout << v << '(' << static_cast<char>(v) << ") ";
        std::cout << '\n';
        std::cout << "    expected: ";
        for (auto v : expected) std::cout << v << '(' << static_cast<char>(v) << ") ";
        std::cout << '\n';
    }

    if (!out.empty()) {
        std::string actual_str;
        for (auto v : out) actual_str += static_cast<char>(v);
        check(actual_str == expected_str,
              "hello_world: reconstructed string = \"Hello, World!\\n\"");
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Suite 4 — ControlUnit behaviour
// ════════════════════════════════════════════════════════════════════════════

static void testControlUnit() {
    beginSuite("ControlUnit");

    // ── Clock counts cycles correctly ─────────────────────────────────────
    {
        std::string src = ".text\nMOVI R1, 42\nNOP\nNOP\n";
        Assembler asmr;
        auto r = asmr.assemble(src);

        CapturingMemory mem(64, 64);
        mem.loadInstructionMemory(r.instructions);

        Registers regs; ALU alu;
        ControlUnit cu(regs, alu, mem);
        cu.reset();
        cu.run(3);

        check(cu.clock().cycles() == 3, "clock: 3 cycles after run(3)");
        check(regs.getGeneralReg(1) == 42, "MOVI R1, 42: R1 = 42 after execution");
    }

    // ── R0 is always zero ─────────────────────────────────────────────────
    {
        std::string src = ".text\nMOVI R0, 99\nNOP\n";
        Assembler asmr;
        auto r = asmr.assemble(src);

        CapturingMemory mem(64, 64);
        mem.loadInstructionMemory(r.instructions);

        Registers regs; ALU alu;
        ControlUnit cu(regs, alu, mem);
        cu.reset();
        cu.run(2);

        check(regs.getGeneralReg(0) == 0, "R0 stays 0 after MOVI R0, 99");
    }

    // ── JAL saves return address ──────────────────────────────────────────
    {
        std::string src = ".text\nJAL target\nNOP\ntarget: NOP\n";
        Assembler asmr;
        auto r = asmr.assemble(src);

        CapturingMemory mem(64, 64);
        mem.loadInstructionMemory(r.instructions);

        Registers regs; ALU alu;
        ControlUnit cu(regs, alu, mem);
        cu.reset();
        cu.run(2);

        check(regs.getGeneralReg(REG_LINK) == 1,
              "JAL: R14 (link) = 1 (return address)");
    }

    // ── ADDI accumulates correctly ────────────────────────────────────────
    {
        std::string src = ".text\nMOVI R1, 10\nADDI R1, -3\nADDI R1, -3\nNOP\n";
        Assembler asmr;
        auto r = asmr.assemble(src);

        CapturingMemory mem(64, 64);
        mem.loadInstructionMemory(r.instructions);

        Registers regs; ALU alu;
        ControlUnit cu(regs, alu, mem);
        cu.reset();
        cu.run(4);

        check(regs.getGeneralReg(1) == 4, "ADDI: 10 + (-3) + (-3) = 4");
    }

    // ── LOAD / STORE round-trip ───────────────────────────────────────────
    {
        std::string src = ".text\n"
                          "MOVI R2, 5\n"
                          "MOVI R3, 10\n"
                          "STORE R3, R2\n"
                          "MOVI R4, 0\n"
                          "LOAD R4, R3\n"
                          "NOP\n";
        Assembler asmr;
        auto r = asmr.assemble(src);

        CapturingMemory mem(64, 64);
        mem.loadInstructionMemory(r.instructions);

        Registers regs; ALU alu;
        ControlUnit cu(regs, alu, mem);
        cu.reset();
        cu.run(6);

        check(regs.getGeneralReg(4) == 5, "LOAD/STORE round-trip: R4 = 5");
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  main
// ════════════════════════════════════════════════════════════════════════════

int main() {
    std::cout << "══════════════════════════════════════════\n";
    std::cout << "  CPU Emulator — Layer 3 End-to-End Tests\n";
    std::cout << "══════════════════════════════════════════\n";

    testTimer();
    testFibonacci();
    testHelloWorld();
    testControlUnit();

    std::cout << "\n══════════════════════════════════════════\n";
    std::cout << "  Results: " << s_passed << " passed, " << s_failed << " failed\n";
    std::cout << "══════════════════════════════════════════\n";

    return s_failed > 0 ? 1 : 0;
}
