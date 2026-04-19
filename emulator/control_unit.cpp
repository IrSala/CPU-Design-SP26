#include "control_unit.h"
#include <iostream>
#include <stdexcept>

DecodedInstruction InstructionDecoder::decode(std::uint16_t rawInstruction) const {
    DecodedInstruction decoded;
    decoded.opcode = decodeOpcode(rawInstruction);
    decoded.rd = decodeRd(rawInstruction);
    decoded.rs1 = decodeRs1(rawInstruction);
    decoded.rs2 = decodeRs2(rawInstruction);
    decoded.imm8 = decodeImm8(rawInstruction);
    decoded.imm12 = decodeImm12(rawInstruction);
    decoded.branchOffset = decodeBranchOffset(rawInstruction);
    decoded.branchIsBne = isBNE(rawInstruction);
    return decoded;
}

ControlUnit::ControlUnit(Registers& regs, ALU& alu, Memory& memory)
    : regs_(regs), alu_(alu), memory_(memory) {}

void ControlUnit::reset() {
    regs_.reset();
    clock_.reset();
    alu_.flags = Flags{};
}

void ControlUnit::writeReg(std::uint8_t index, std::uint16_t value) {
    if (index == REG_ZERO) {
        return;
    }
    regs_.setGeneralReg(index, value);
}

std::uint16_t ControlUnit::fetch() {
    const std::uint16_t pc = regs_.getprogramcountRegister();
    const std::uint16_t rawInstruction = memory_.readInstruction(pc);
    regs_.setInstructionRegister(rawInstruction);
    regs_.incrementPC();
    return rawInstruction;
}

void ControlUnit::execute(const DecodedInstruction& instr) {
    switch (instr.opcode) {
        case Opcode::ADD: {
            const std::uint16_t out = alu_.execute(
                regs_.getGeneralReg(instr.rs1),
                regs_.getGeneralReg(instr.rs2),
                ALUOp::ADD);
            writeReg(instr.rd, out);
            break;
        }
        case Opcode::SUB: {
            const std::uint16_t out = alu_.execute(
                regs_.getGeneralReg(instr.rs1),
                regs_.getGeneralReg(instr.rs2),
                ALUOp::SUB);
            writeReg(instr.rd, out);
            break;
        }
        case Opcode::AND: {
            const std::uint16_t out = alu_.execute(
                regs_.getGeneralReg(instr.rs1),
                regs_.getGeneralReg(instr.rs2),
                ALUOp::AND);
            writeReg(instr.rd, out);
            break;
        }
        case Opcode::OR: {
            const std::uint16_t out = alu_.execute(
                regs_.getGeneralReg(instr.rs1),
                regs_.getGeneralReg(instr.rs2),
                ALUOp::OR);
            writeReg(instr.rd, out);
            break;
        }
        case Opcode::MUL: {
            const std::uint16_t out = alu_.execute(
                regs_.getGeneralReg(instr.rs1),
                regs_.getGeneralReg(instr.rs2),
                ALUOp::MUL);
            writeReg(instr.rd, out);
            break;
        }
        case Opcode::SHL: {
            const std::uint16_t out = alu_.execute(
                regs_.getGeneralReg(instr.rs1),
                regs_.getGeneralReg(instr.rs2),
                ALUOp::SHL);
            writeReg(instr.rd, out);
            break;
        }
        case Opcode::SHR: {
            const std::uint16_t out = alu_.execute(
                regs_.getGeneralReg(instr.rs1),
                regs_.getGeneralReg(instr.rs2),
                ALUOp::SHR);
            writeReg(instr.rd, out);
            break;
        }
        case Opcode::CMP: {
            (void)alu_.execute(
                regs_.getGeneralReg(instr.rs1),
                regs_.getGeneralReg(instr.rs2),
                ALUOp::CMP);
            break;
        }
        case Opcode::MOVI: {
            const std::uint16_t value = static_cast<std::uint8_t>(instr.imm8);
            // Use PASS_A so zero/negative flags follow the moved value.
            const std::uint16_t out = alu_.execute(value, 0, ALUOp::PASS_A);
            writeReg(instr.rd, out);
            break;
        }
        case Opcode::ADDI: {
            const std::uint16_t lhs = regs_.getGeneralReg(instr.rd);
            const std::uint16_t rhs = static_cast<std::uint16_t>(
                static_cast<std::int16_t>(instr.imm8));
            const std::uint16_t out = alu_.execute(lhs, rhs, ALUOp::ADD);
            writeReg(instr.rd, out);
            break;
        }
        case Opcode::LOAD: {
            const std::uint16_t addr  = regs_.getGeneralReg(instr.rs1);
            const std::uint16_t value = memory_.readData(addr);
            const std::uint16_t out   = alu_.execute(value, 0, ALUOp::PASS_A);
            writeReg(instr.rd, out);
            break;
        }
        case Opcode::STORE: {
            const std::uint16_t addr  = regs_.getGeneralReg(instr.rd);
            const std::uint16_t value = regs_.getGeneralReg(instr.rs1);

            if (addr == MMIO_OUT) {
                // Memory-mapped console output.
                // Printable ASCII → emit as character; otherwise as decimal.
                if ((value >= 0x20 && value <= 0x7E) ||
                     value == '\n' || value == '\r' || value == '\t') {
                    std::cout << static_cast<char>(value);
                } else {
                    std::cout << static_cast<unsigned>(value) << '\n';
                }
                // Also call memory_.writeData so that subclasses such as
                // CapturingMemory (used in tests) can intercept MMIO writes
                // and record the value without needing to parse stdout.
                memory_.writeData(addr, value);
            } else {
                memory_.writeData(addr, value);
            }

            (void)alu_.execute(value, 0, ALUOp::PASS_A);
            break;
        }
        case Opcode::JMP: {
            const std::uint16_t nextPc = regs_.getprogramcountRegister();
            regs_.setprogramcountRegister(
                static_cast<std::uint16_t>(
                    static_cast<std::int32_t>(nextPc) + instr.imm12));
            break;
        }
        case Opcode::JAL: {
            const std::uint16_t nextPc = regs_.getprogramcountRegister();
            writeReg(REG_LINK, nextPc);
            regs_.setprogramcountRegister(
                static_cast<std::uint16_t>(
                    static_cast<std::int32_t>(nextPc) + instr.imm12));
            break;
        }
        case Opcode::JR: {
            regs_.setprogramcountRegister(regs_.getGeneralReg(instr.rd));
            break;
        }
        case Opcode::BRANCH: {
            const bool take = instr.branchIsBne ? !alu_.flags.zero : alu_.flags.zero;
            if (take) {
                const std::uint16_t nextPc = regs_.getprogramcountRegister();
                regs_.setprogramcountRegister(
                    static_cast<std::uint16_t>(
                        static_cast<std::int32_t>(nextPc) + instr.branchOffset));
            }
            break;
        }
        default:
            throw std::runtime_error("ControlUnit: unknown opcode");
    }
    // Keep architectural rule: R0 is hard-wired to zero.
    regs_.setGeneralReg(REG_ZERO, 0);
}

void ControlUnit::step() {
    const std::uint16_t rawInstruction = fetch();
    const DecodedInstruction decoded = decoder_.decode(rawInstruction);
    execute(decoded);
    clock_.tick();
}

void ControlUnit::run(std::size_t maxCycles) {
    for (std::size_t i = 0; i < maxCycles; ++i) {
        step();
    }
}
