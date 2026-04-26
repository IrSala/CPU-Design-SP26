/**
 * test_runner.cpp — Layer 1 unit tests for the 16-bit Harvard CPU emulator
 *
 * Covers:
 *   Suite 1 — ALU  (all 11 operations + flag semantics)
 *   Suite 2 — Registers  (reset state, R0 protection, PC/SP behaviour)
 *   Suite 3 — ISA encode/decode  (round-trip for every instruction format)
 *
 * Build (from project root, adjust include paths as needed):
 *   g++ -std=c++17 -Wall -Wextra \
 *       -I./isa -I./emulator \
 *       tests/test_runner.cpp emulator/register.cpp \
 *       -o tests/test_runner
 *
 * Run:
 *   ./tests/test_runner
 *
 * Exit code 0 = all tests passed, 1 = at least one failure.
 */

#include "alu.h"
#include "register.h"
#include "isa.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

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
//  Suite 1 — ALU
// ════════════════════════════════════════════════════════════════════════════

static void testALU() {
    beginSuite("ALU");
    ALU alu;

    // ── ADD ─────────────────────────────────────────────────────────────────
    {
        uint16_t r = alu.execute(3, 4, ALUOp::ADD);
        check(r == 7,               "ADD 3+4 = 7");
        check(!alu.flags.zero,      "ADD 3+4: zero clear");
        check(!alu.flags.carry,     "ADD 3+4: carry clear");
        check(!alu.flags.overflow,  "ADD 3+4: overflow clear");
        check(!alu.flags.negative,  "ADD 3+4: negative clear");
    }
    {
        // Unsigned wrap-around → carry
        uint16_t r = alu.execute(0xFFFF, 1, ALUOp::ADD);
        check(r == 0,               "ADD 0xFFFF+1 = 0 (wrap)");
        check(alu.flags.zero,       "ADD 0xFFFF+1: zero set");
        check(alu.flags.carry,      "ADD 0xFFFF+1: carry set");
    }
    {
        // Signed overflow: 0x7FFF + 1 = 0x8000 (positive + positive = negative)
        uint16_t r = alu.execute(0x7FFF, 1, ALUOp::ADD);
        check(r == 0x8000,          "ADD 0x7FFF+1 = 0x8000");
        check(alu.flags.overflow,   "ADD 0x7FFF+1: signed overflow set");
        check(alu.flags.negative,   "ADD 0x7FFF+1: negative set");
        check(!alu.flags.carry,     "ADD 0x7FFF+1: carry clear");
    }
    {
        // Zero result
        uint16_t r = alu.execute(5, 0, ALUOp::ADD);
        (void)r;
        alu.execute(0, 0, ALUOp::ADD);
        check(alu.flags.zero,       "ADD 0+0: zero set");
    }

    // ── SUB ─────────────────────────────────────────────────────────────────
    {
        uint16_t r = alu.execute(10, 3, ALUOp::SUB);
        check(r == 7,               "SUB 10-3 = 7");
        check(!alu.flags.carry,     "SUB 10-3: no borrow");
    }
    {
        // Borrow: 2 - 5 requires a borrow
        uint16_t r = alu.execute(2, 5, ALUOp::SUB);
        check(r == static_cast<uint16_t>(2 - 5), "SUB 2-5 wraps correctly");
        check(alu.flags.carry,      "SUB 2-5: borrow (carry) set");
    }
    {
        // Zero result
        uint16_t r = alu.execute(7, 7, ALUOp::SUB);
        check(r == 0,               "SUB 7-7 = 0");
        check(alu.flags.zero,       "SUB 7-7: zero set");
    }
    {
        // Signed overflow: 0x8000 - 1 (most-negative minus positive = positive)
        uint16_t r = alu.execute(0x8000, 1, ALUOp::SUB);
        check(r == 0x7FFF,          "SUB 0x8000-1 = 0x7FFF");
        check(alu.flags.overflow,   "SUB 0x8000-1: signed overflow set");
    }

    // ── AND ─────────────────────────────────────────────────────────────────
    {
        uint16_t r = alu.execute(0xFF0F, 0x0FFF, ALUOp::AND);
        check(r == 0x0F0F,          "AND 0xFF0F & 0x0FFF = 0x0F0F");
        check(!alu.flags.zero,      "AND result non-zero: zero clear");
    }
    {
        uint16_t r = alu.execute(0xAAAA, 0x5555, ALUOp::AND);
        check(r == 0,               "AND 0xAAAA & 0x5555 = 0");
        check(alu.flags.zero,       "AND zero result: zero set");
    }

    // ── OR ──────────────────────────────────────────────────────────────────
    {
        uint16_t r = alu.execute(0xA0A0, 0x0B0B, ALUOp::OR);
        check(r == 0xABAB,          "OR 0xA0A0 | 0x0B0B = 0xABAB");
    }

    // ── XOR ─────────────────────────────────────────────────────────────────
    {
        uint16_t r = alu.execute(0xFFFF, 0xFFFF, ALUOp::XOR);
        check(r == 0,               "XOR 0xFFFF ^ 0xFFFF = 0");
        check(alu.flags.zero,       "XOR self: zero set");
    }
    {
        uint16_t r = alu.execute(0x00FF, 0xFF00, ALUOp::XOR);
        check(r == 0xFFFF,          "XOR 0x00FF ^ 0xFF00 = 0xFFFF");
        check(alu.flags.negative,   "XOR 0xFFFF: negative set (MSB=1)");
    }

    // ── SHL ─────────────────────────────────────────────────────────────────
    {
        uint16_t r = alu.execute(0x0001, 4, ALUOp::SHL);
        check(r == 0x0010,          "SHL 1 << 4 = 0x0010");
    }
    {
        // Shift bit 15 out → carry
        uint16_t r = alu.execute(0x8000, 1, ALUOp::SHL);
        check(r == 0,               "SHL 0x8000 << 1 = 0");
        check(alu.flags.carry,      "SHL 0x8000 << 1: carry set (bit shifted out)");
        check(alu.flags.zero,       "SHL 0x8000 << 1: zero set");
    }

    // ── SHR ─────────────────────────────────────────────────────────────────
    {
        uint16_t r = alu.execute(0x0080, 4, ALUOp::SHR);
        check(r == 0x0008,          "SHR 0x80 >> 4 = 0x08");
    }
    {
        // Logical shift: sign bit NOT preserved
        uint16_t r = alu.execute(0x8000, 1, ALUOp::SHR);
        check(r == 0x4000,          "SHR 0x8000 >> 1 = 0x4000 (logical)");
        check(!alu.flags.negative,  "SHR 0x8000 >> 1: negative clear (logical)");
    }

    // ── ASR ─────────────────────────────────────────────────────────────────
    {
        // Arithmetic shift: sign bit IS preserved
        uint16_t r = alu.execute(0x8000, 1, ALUOp::ASR);
        check(r == 0xC000,          "ASR 0x8000 >> 1 = 0xC000 (sign preserved)");
        check(alu.flags.negative,   "ASR 0x8000 >> 1: negative set");
    }
    {
        uint16_t r = alu.execute(0x0010, 2, ALUOp::ASR);
        check(r == 0x0004,          "ASR 0x10 >> 2 = 0x04 (positive unchanged)");
    }

    // ── MUL ─────────────────────────────────────────────────────────────────
    {
        uint16_t r = alu.execute(6, 7, ALUOp::MUL);
        check(r == 42,              "MUL 6*7 = 42");
    }
    {
        // Truncation: result wider than 16 bits
        uint16_t r = alu.execute(0x0100, 0x0100, ALUOp::MUL);
        check(r == 0,               "MUL 0x100*0x100 truncated to 0");
        check(alu.flags.zero,       "MUL truncated to 0: zero set");
    }

    // ── CMP ─────────────────────────────────────────────────────────────────
    {
    // CMP sets flags based on (a - b); return value is ignored by caller
    alu.execute(5, 5, ALUOp::CMP);
    check(alu.flags.zero,       "CMP 5==5: zero set");
    check(!alu.flags.carry,     "CMP 5==5: no borrow");
    }
    {
    alu.execute(3, 7, ALUOp::CMP);
    check(!alu.flags.zero,      "CMP 3!=7: zero clear");
    check(alu.flags.carry,      "CMP 3<7: borrow (carry) set");
    }

    // ── PASS_A ──────────────────────────────────────────────────────────────
    {
        uint16_t r = alu.execute(0xABCD, 0xFFFF, ALUOp::PASS_A);
        check(r == 0xABCD,          "PASS_A returns a unchanged");
        check(alu.flags.negative,   "PASS_A 0xABCD: negative set (MSB=1)");
        check(!alu.flags.zero,      "PASS_A 0xABCD: zero clear");
    }
    {
        uint16_t r = alu.execute(0, 999, ALUOp::PASS_A);
        check(r == 0,               "PASS_A 0: returns 0");
        check(alu.flags.zero,       "PASS_A 0: zero set");
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Suite 2 — Registers
// ════════════════════════════════════════════════════════════════════════════

static void testRegisters() {
    beginSuite("Registers");
    Registers regs;

    // ── Reset state ──────────────────────────────────────────────────────────
    regs.reset();
    bool allZero = true;
    for (size_t i = 0; i < Registers::NUM_REG; ++i)
        if (regs.getGeneralReg(i) != 0) { allZero = false; break; }
    check(allZero,                   "reset: all 16 GPRs = 0");
    check(regs.getprogramcountRegister() == 0x0000, "reset: PC = 0x0000");
    check(regs.getstackptrRegister()     == 0xFFFE, "reset: SP = 0xFFFE");
    check(regs.getInstructionRegister()  == 0,      "reset: IR = 0");

    // ── General-purpose read/write ────────────────────────────────────────
    regs.setGeneralReg(1, 0x1234);
    check(regs.getGeneralReg(1) == 0x1234, "setGeneralReg/getGeneralReg R1 = 0x1234");

    regs.setGeneralReg(15, 0xBEEF);
    check(regs.getGeneralReg(15) == 0xBEEF, "setGeneralReg R15 = 0xBEEF");

    // ── Out-of-range index returns 0, doesn't crash ───────────────────────
    check(regs.getGeneralReg(16) == 0,  "getGeneralReg(16) returns 0 (out of range)");
    // setGeneralReg(16,...) should be a no-op — call it and verify no crash
    regs.setGeneralReg(16, 0xDEAD);
    check(regs.getGeneralReg(16) == 0,  "setGeneralReg(16,...) is no-op");

    // ── PC register ──────────────────────────────────────────────────────────
    regs.setprogramcountRegister(0x0100);
    check(regs.getprogramcountRegister() == 0x0100, "setPC / getPC = 0x0100");

    regs.incrementPC();
    check(regs.getprogramcountRegister() == 0x0101, "incrementPC: 0x0100 → 0x0101");

    // PC wrap-around at 0xFFFF
    regs.setprogramcountRegister(0xFFFF);
    regs.incrementPC();
    check(regs.getprogramcountRegister() == 0x0000, "incrementPC wraps 0xFFFF → 0x0000");

    // ── Stack pointer ─────────────────────────────────────────────────────
    regs.setstackptrRegister(0x8000);
    check(regs.getstackptrRegister() == 0x8000, "setstackptrRegister / get = 0x8000");

    // ── Instruction register ─────────────────────────────────────────────
    regs.setInstructionRegister(0xDEAD);
    check(regs.getInstructionRegister() == 0xDEAD, "setIR / getIR = 0xDEAD");

    // ── Reset clears everything again ────────────────────────────────────
    regs.setGeneralReg(3, 0x9999);
    regs.setprogramcountRegister(0x4242);
    regs.reset();
    check(regs.getGeneralReg(3)          == 0,      "post-reset: R3 cleared");
    check(regs.getprogramcountRegister() == 0x0000, "post-reset: PC = 0x0000");
    check(regs.getstackptrRegister()     == 0xFFFE, "post-reset: SP = 0xFFFE");
}

// ════════════════════════════════════════════════════════════════════════════
//  Suite 3 — ISA encode/decode round-trips
// ════════════════════════════════════════════════════════════════════════════

static void testISA() {
    beginSuite("ISA encode/decode");

    // ── R-type ───────────────────────────────────────────────────────────────
    {
        // ADD R5, R3, R7
        uint16_t w = encodeR(Opcode::ADD, 5, 3, 7);
        check(decodeOpcode(w) == Opcode::ADD, "R-type ADD: opcode");
        check(decodeRd(w)  == 5,              "R-type ADD: Rd = 5");
        check(decodeRs1(w) == 3,              "R-type ADD: Rs1 = 3");
        check(decodeRs2(w) == 7,              "R-type ADD: Rs2 = 7");
    }
    {
        // CMP R2, R9  (Rd field unused, should be 0 from assembler)
        uint16_t w = encodeR(Opcode::CMP, 0, 2, 9);
        check(decodeOpcode(w) == Opcode::CMP, "R-type CMP: opcode");
        check(decodeRs1(w) == 2,              "R-type CMP: Rs1 = 2");
        check(decodeRs2(w) == 9,              "R-type CMP: Rs2 = 9");
    }
    {
        // SUB R15, R15, R0 — max register indices
        uint16_t w = encodeR(Opcode::SUB, 15, 15, 0);
        check(decodeRd(w)  == 15, "R-type SUB: Rd = 15 (max)");
        check(decodeRs1(w) == 15, "R-type SUB: Rs1 = 15 (max)");
        check(decodeRs2(w) == 0,  "R-type SUB: Rs2 = 0");
    }

    // ── I-type ───────────────────────────────────────────────────────────────
    {
        // MOVI R4, 0xAB
        uint16_t w = encodeI(Opcode::MOVI, 4, static_cast<int8_t>(0xAB));
        check(decodeOpcode(w) == Opcode::MOVI,              "I-type MOVI: opcode");
        check(decodeRd(w) == 4,                             "I-type MOVI: Rd = 4");
        check((w & 0xFF) == 0xAB,                           "I-type MOVI: imm8 = 0xAB");
    }
    {
        // ADDI R2, -1  (0xFF signed)
        uint16_t w = encodeI(Opcode::ADDI, 2, -1);
        check(decodeOpcode(w) == Opcode::ADDI, "I-type ADDI: opcode");
        check(decodeRd(w)     == 2,            "I-type ADDI: Rd = 2");
        check(decodeImm8(w)   == -1,           "I-type ADDI: imm8 = -1");
    }
    {
        // ADDI R0, 127  (max positive imm8)
        uint16_t w = encodeI(Opcode::ADDI, 0, 127);
        check(decodeImm8(w) == 127, "I-type ADDI: imm8 = 127 (max positive)");
    }
    {
        // ADDI R0, -128  (min negative imm8)
        uint16_t w = encodeI(Opcode::ADDI, 0, -128);
        check(decodeImm8(w) == -128, "I-type ADDI: imm8 = -128 (min negative)");
    }

    // ── M-type ───────────────────────────────────────────────────────────────
    {
        // LOAD R6, R10
        uint16_t w = encodeM(Opcode::LOAD, 6, 10);
        check(decodeOpcode(w) == Opcode::LOAD, "M-type LOAD: opcode");
        check(decodeRd(w)     == 6,            "M-type LOAD: Rd = 6");
        check(decodeRs1(w)    == 10,           "M-type LOAD: Rs1 (addr) = 10");
    }
    {
        // STORE R5, R1
        uint16_t w = encodeM(Opcode::STORE, 5, 1);
        check(decodeOpcode(w) == Opcode::STORE, "M-type STORE: opcode");
        check(decodeRd(w)     == 5,             "M-type STORE: Rd (addr reg) = 5");
        check(decodeRs1(w)    == 1,             "M-type STORE: Rs1 (data reg) = 1");
    }

    // ── J-type ───────────────────────────────────────────────────────────────
    {
        // JMP +10
        uint16_t w = encodeJ(Opcode::JMP, 10);
        check(decodeOpcode(w) == Opcode::JMP, "J-type JMP: opcode");
        check(decodeImm12(w)  == 10,          "J-type JMP: imm12 = +10");
    }
    {
        // JMP -1  (one step back)
        uint16_t w = encodeJ(Opcode::JMP, -1);
        check(decodeImm12(w) == -1,           "J-type JMP: imm12 = -1");
    }
    {
        // JAL -2048  (max negative 12-bit)
        uint16_t w = encodeJ(Opcode::JAL, -2048);
        check(decodeOpcode(w) == Opcode::JAL, "J-type JAL: opcode");
        check(decodeImm12(w)  == -2048,       "J-type JAL: imm12 = -2048 (min)");
    }
    {
        // JMP +2047  (max positive 12-bit)
        uint16_t w = encodeJ(Opcode::JMP, 2047);
        check(decodeImm12(w) == 2047,         "J-type JMP: imm12 = +2047 (max)");
    }

    // ── JR-type ──────────────────────────────────────────────────────────────
    {
        // JR R14  (RET)
        uint16_t w = encodeJR(14);
        check(decodeOpcode(w) == Opcode::JR, "JR-type: opcode");
        check(decodeRd(w)     == 14,         "JR-type: Rd = 14 (link register)");
    }
    {
        // JR R0
        uint16_t w = encodeJR(0);
        check(decodeRd(w) == 0, "JR-type: Rd = 0");
    }

    // ── BRANCH ───────────────────────────────────────────────────────────────
    {
        // BEQ +5
        uint16_t w = encodeBEQ(5);
        check(decodeOpcode(w)      == Opcode::BRANCH, "BEQ: opcode = BRANCH");
        check(!isBNE(w),                              "BEQ: condition bit = 0");
        check(decodeBranchOffset(w) == 5,             "BEQ: offset = +5");
    }
    {
        // BNE -3
        uint16_t w = encodeBNE(-3);
        check(decodeOpcode(w)       == Opcode::BRANCH, "BNE: opcode = BRANCH");
        check(isBNE(w),                                "BNE: condition bit = 1");
        check(decodeBranchOffset(w) == -3,             "BNE: offset = -3");
    }
    {
        // BEQ -1024  (min 11-bit signed)
        uint16_t w = encodeBEQ(-1024);
        check(decodeBranchOffset(w) == -1024, "BEQ: offset = -1024 (min)");
        check(!isBNE(w),                      "BEQ -1024: still BEQ");
    }
    {
        // BNE +1023  (max 11-bit signed)
        uint16_t w = encodeBNE(1023);
        check(decodeBranchOffset(w) == 1023, "BNE: offset = +1023 (max)");
        check(isBNE(w),                      "BNE +1023: still BNE");
    }

    // ── Pseudo-instruction encodings ─────────────────────────────────────────
    {
        // NOP = ADD R0, R0, R0
        uint16_t w = encodeR(Opcode::ADD, REG_ZERO, REG_ZERO, REG_ZERO);
        check(w == 0x0000, "NOP encodes as 0x0000");
    }
    {
        // MOV R3, R5 = ADD R3, R5, R0
        uint16_t w = encodeR(Opcode::ADD, 3, 5, REG_ZERO);
        check(decodeRd(w)  == 3, "MOV R3,R5: Rd = 3");
        check(decodeRs1(w) == 5, "MOV R3,R5: Rs1 = 5");
        check(decodeRs2(w) == 0, "MOV R3,R5: Rs2 = 0 (zero reg)");
    }
    {
        // RET = JR R14
        uint16_t w = encodeJR(REG_LINK);
        check(decodeOpcode(w) == Opcode::JR, "RET: opcode = JR");
        check(decodeRd(w)     == 14,         "RET: Rd = R14 (link)");
    }

    // ── MMIO address constant ─────────────────────────────────────────────
    check(MMIO_OUT == 0x7F00, "MMIO_OUT constant = 0x7F00");
    check(MMIO_IN  == 0x7F01, "MMIO_IN  constant = 0x7F01");
}

// ════════════════════════════════════════════════════════════════════════════
//  main
// ════════════════════════════════════════════════════════════════════════════

int main() {
    std::cout << "══════════════════════════════════════════\n";
    std::cout << "  CPU Emulator — Layer 1 Unit Tests\n";
    std::cout << "══════════════════════════════════════════\n";

    testALU();
    testRegisters();
    testISA();

    std::cout << "\n══════════════════════════════════════════\n";
    std::cout << "  Results: " << s_passed << " passed, " << s_failed << " failed\n";
    std::cout << "══════════════════════════════════════════\n";

    return s_failed > 0 ? 1 : 0;
}
