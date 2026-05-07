#pragma once
#include <cstdint>

// ============================================================
//  ISA.h  —  16-bit Harvard CPU Instruction Set
//
//  All instructions are 16 bits wide.
//
//  FORMAT:
//    Bits [15:12] — opcode   (4 bits, 16 possible instructions)
//    Bits [11:8]  — dest/src reg (4 bits, R0–R15)
//    Bits [7:4]   — src reg1 (4 bits)
//    Bits [3:0]   — src reg2 (4 bits)
//
//  Exceptions to the above:
//    MOVI / ADDI  — bits [7:0] are an 8-bit immediate
//    JMP  / JAL   — bits [11:0] are a 12-bit signed PC-relative offset
//    BEQ  / BNE   — bit[11] is condition, bits [10:0] are signed offset,
//                   bits [11:8] are unused as a reg field
// ============================================================

// ------------------------------------------------------------
//  Opcodes
// ------------------------------------------------------------
enum class Opcode : uint8_t {
    ADD    = 0x0,  // Rd = Rs1 + Rs2
    SUB    = 0x1,  // Rd = Rs1 - Rs2
    AND    = 0x2,  // Rd = Rs1 & Rs2
    OR     = 0x3,  // Rd = Rs1 | Rs2
    MUL    = 0x4,  // Rd = Rs1 * Rs2  (lower 16 bits)
    SHL    = 0x5,  // Rd = Rs1 << Rs2
    SHR    = 0x6,  // Rd = Rs1 >> Rs2
    CMP    = 0x7,  // set flags for Rs1 - Rs2, discard result
    LOAD   = 0x8,  // Rd = Mem[Rs1]
    STORE  = 0x9,  // Mem[Rs1] = Rs2
    MOVI   = 0xA,  // Rd = imm8  (zero-extended)
    ADDI   = 0xB,  // Rd = Rd + imm8  (signed)
    JMP    = 0xC,  // PC = PC + imm12  (unconditional, PC-relative)
    JAL    = 0xD,  // R14 = PC+1, PC = PC + imm12  (call — saves return addr)
    JR     = 0xE,  // PC = Rd  (return: JR R14)
    BRANCH = 0xF,  // conditional branch — see below
};

// BRANCH encoding:
//   bit[11]   : 0 = BEQ (branch if Z==1), 1 = BNE (branch if Z==0)
//   bits[10:0]: signed PC-relative offset in words
//
//   Example:  BEQ +3  →  0xF | 0 | 0x003  →  0xF003
//             BNE -2  →  0xF | 1 | 0x7FE  →  0xFF FE

// ------------------------------------------------------------
//  MMIO addresses  (data memory)
// ------------------------------------------------------------
static constexpr uint16_t MMIO_OUT = 0x7F00;  // write here → console output
static constexpr uint16_t MMIO_IN  = 0x7F01;  // read here  → console input

// ------------------------------------------------------------
//  Register conventions
// ------------------------------------------------------------
static constexpr uint8_t REG_ZERO = 0;   // R0 always == 0  (writes ignored)
static constexpr uint8_t REG_LINK = 14;  // R14 = return address (written by JAL)
static constexpr uint8_t REG_SP   = 15;  // R15 = stack pointer (by convention)

// ------------------------------------------------------------
//  Encoding helpers
// ------------------------------------------------------------

// R-type: ADD, SUB, AND, OR, MUL, SHL, SHR, CMP
inline constexpr uint16_t encodeR(Opcode op, uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return (uint16_t)((uint8_t)op << 12 | (rd & 0xF) << 8 | (rs1 & 0xF) << 4 | (rs2 & 0xF));
}

// I-type: MOVI, ADDI  (8-bit immediate in bits [7:0])
inline constexpr uint16_t encodeI(Opcode op, uint8_t rd, int8_t imm8) {
    return (uint16_t)((uint8_t)op << 12 | (rd & 0xF) << 8 | (uint8_t)imm8);
}

// M-type: LOAD, STORE  (Rd or Rs in [11:8], address register in [7:4])
inline constexpr uint16_t encodeM(Opcode op, uint8_t rd, uint8_t raddr) {
    return (uint16_t)((uint8_t)op << 12 | (rd & 0xF) << 8 | (raddr & 0xF) << 4);
}

// J-type: JMP, JAL  (12-bit signed PC-relative offset in bits [11:0])
inline constexpr uint16_t encodeJ(Opcode op, int16_t imm12) {
    return (uint16_t)((uint8_t)op << 12 | (imm12 & 0x0FFF));
}

// JR-type: JR  (register to jump to in bits [11:8])
inline constexpr uint16_t encodeJR(uint8_t rd) {
    return (uint16_t)((uint8_t)Opcode::JR << 12 | (rd & 0xF) << 8);
}

// BRANCH: BEQ / BNE  (cond bit in bit[11], 11-bit signed offset in bits [10:0])
inline constexpr uint16_t encodeBEQ(int16_t imm11) {
    return (uint16_t)((uint8_t)Opcode::BRANCH << 12 | (imm11 & 0x07FF));
}
inline constexpr uint16_t encodeBNE(int16_t imm11) {
    return (uint16_t)((uint8_t)Opcode::BRANCH << 12 | 0x0800 | (imm11 & 0x07FF));
}

// ------------------------------------------------------------
//  Decoding helpers
// ------------------------------------------------------------
inline constexpr Opcode  decodeOpcode(uint16_t i) { return (Opcode)((i >> 12) & 0xF); }
inline constexpr uint8_t decodeRd    (uint16_t i) { return (i >> 8) & 0xF; }
inline constexpr uint8_t decodeRs1   (uint16_t i) { return (i >> 4) & 0xF; }
inline constexpr uint8_t decodeRs2   (uint16_t i) { return  i       & 0xF; }
inline constexpr int8_t  decodeImm8  (uint16_t i) { return (int8_t)(i & 0xFF); }

inline constexpr int16_t decodeImm12(uint16_t i) {
    uint16_t raw = i & 0x0FFF;
    return (int16_t)((raw & 0x0800) ? (raw | 0xF000) : raw);
}

// For BRANCH: check condition bit and extract 11-bit signed offset
inline constexpr bool    isBNE         (uint16_t i) { return (i & 0x0800) != 0; }
inline constexpr int16_t decodeBranchOffset(uint16_t i) {
    uint16_t raw = i & 0x07FF;
    return (int16_t)((raw & 0x0400) ? (raw | 0xF800) : raw);
}
