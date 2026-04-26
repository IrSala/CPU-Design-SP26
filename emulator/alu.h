#pragma once
#include <cstdint>

//Flags set by ALU operations, read by Control Unit for branches
struct Flags {
    bool zero     = false;  //result == 0
    bool negative = false;  //result MSB is 1 (signed negative)
    bool carry    = false;  //unsigned overflow / borrow
    bool overflow = false;  //signed overflow
};

//All supported ALU operations
enum class ALUOp {
    ADD,    //a + b
    SUB,    //a - b
    AND,    //a & b
    OR,     //a | b
    XOR,    //a ^ b
    SHL,    //logical shift left
    SHR,    //logical shift right
    ASR,    //arithmetic shift right (preserves sign)
    MUL,    //a * b (truncated to 16 bits)
    CMP,    //compare: sets flags based on (a - b), return value discarded by caller
    PASS_A  //pass a through unchanged (used for MOV)
};

struct ALU {
    Flags flags;  //Control Unit reads these for branch decisions

    //Takes two 16-bit inputs, returns 16-bit result and updates flags
    uint16_t execute(uint16_t a, uint16_t b, ALUOp op) {
        uint32_t result32 = 0;  //32-bit scratch to detect carry out of bit 15
        uint16_t result   = 0;

        switch (op) {
            case ALUOp::ADD:
                result32       = (uint32_t)a + b;
                result         = (uint16_t)result32;
                flags.carry    = (result32 > 0xFFFF);
                flags.overflow = ((~(a ^ b) & (a ^ result)) >> 15) & 1;
                break;

            case ALUOp::SUB:
            case ALUOp::CMP:
                result32       = (uint32_t)a - b;
                result         = (uint16_t)result32;
                flags.carry    = (b > a);  //borrow occurred
                flags.overflow = (((a ^ b) & (a ^ result)) >> 15) & 1;
                // Note: for CMP the return value is ignored by the caller
                // (control_unit.cpp does (void)alu_.execute(...)).
                // result stays as (a - b) so the zero/negative flag update
                // below correctly reflects the comparison.
                break;

            case ALUOp::AND:   result = a & b;               break;
            case ALUOp::OR:    result = a | b;               break;
            case ALUOp::XOR:   result = a ^ b;               break;

            case ALUOp::SHL:
                flags.carry = (b < 16) ? ((a >> (16 - b)) & 1) : 0;
                result      = a << b;
                break;

            case ALUOp::SHR:   result = a >> b;              break;
            case ALUOp::ASR:   result = (int16_t)a >> b;     break;
            case ALUOp::MUL:   result = a * b;               break;
            case ALUOp::PASS_A: result = a;                  break;
        }

        //Zero and negative always update regardless of operation
        flags.zero     = (result == 0);
        flags.negative = (result >> 15) & 1;  // MSB = sign bit

        return result;
    }
};
