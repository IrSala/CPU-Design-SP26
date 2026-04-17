#include <cstddef>
#include <cstdint>
#include "Memory.h"
#include "alu.h"
#include "isa.h"
#include "register.h"

// Simple clock: one tick per executed instruction.
class Clock {
public:
    void reset() { cycles_ = 0; }
    void tick() { ++cycles_; }
    std::uint64_t cycles() const { return cycles_; }

private:
    std::uint64_t cycles_ = 0;
};

// Decoded view of one 16-bit instruction word.
struct DecodedInstruction {
    Opcode opcode = Opcode::ADD;
    std::uint8_t rd = 0;
    std::uint8_t rs1 = 0;
    std::uint8_t rs2 = 0;
    std::int8_t imm8 = 0;
    std::int16_t imm12 = 0;
    std::int16_t branchOffset = 0;
    bool branchIsBne = false;
};

// Keeps decode logic isolated from execute logic.
class InstructionDecoder {
public:
    DecodedInstruction decode(std::uint16_t rawInstruction) const;
};

// ControlUnit drives the CPU datapath in three clear stages:
// fetch -> decode -> execute.
class ControlUnit {
public:
    ControlUnit(Registers& regs, ALU& alu, Memory& memory);
    void reset();
    void step();
    void run(std::size_t maxCycles);
    const Clock& clock() const { return clock_; }
    const Flags& flags() const { return alu_.flags; }

private:
    Registers& regs_;
    ALU& alu_;
    Memory& memory_;
    InstructionDecoder decoder_;
    Clock clock_;
    std::uint16_t fetch();
    void execute(const DecodedInstruction& instr);
    void writeReg(std::uint8_t index, std::uint16_t value);
};
