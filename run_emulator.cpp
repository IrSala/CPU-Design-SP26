#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

#include "register.h"
#include "alu.h"
#include "Memory.h"
#include "control_unit.h"

//
// run_emulator.cpp — Binary loader and executor for the 16-bit Harvard CPU
//
// To compile:
//   g++ -std=c++17 -Wall -Wextra -I./isa -I./emulator
//       run_emulator.cpp emulator/*.cpp -o program
//
// To run:
//   ./program <file.bin>          — execute program, show output only
//

#include "isa/isa.h"  // adjust path if needed

static std::string disassemble(std::uint16_t word) {
    const Opcode op = decodeOpcode(word);
    const uint8_t rd  = decodeRd(word);
    const uint8_t rs1 = decodeRs1(word);
    const uint8_t rs2 = decodeRs2(word);

    auto reg = [](uint8_t r) { return "R" + std::to_string(r); };

    switch (op) {
        // R-type
        case Opcode::ADD:  return "ADD  " + reg(rd) + ", " + reg(rs1) + ", " + reg(rs2);
        case Opcode::SUB:  return "SUB  " + reg(rd) + ", " + reg(rs1) + ", " + reg(rs2);
        case Opcode::AND:  return "AND  " + reg(rd) + ", " + reg(rs1) + ", " + reg(rs2);
        case Opcode::OR:   return "OR   " + reg(rd) + ", " + reg(rs1) + ", " + reg(rs2);
        case Opcode::MUL:  return "MUL  " + reg(rd) + ", " + reg(rs1) + ", " + reg(rs2);
        case Opcode::SHL:  return "SHL  " + reg(rd) + ", " + reg(rs1) + ", " + reg(rs2);
        case Opcode::SHR:  return "SHR  " + reg(rd) + ", " + reg(rs1) + ", " + reg(rs2);
        case Opcode::CMP:  return "CMP  " + reg(rs1) + ", " + reg(rs2);

        // Memory
        case Opcode::LOAD:  return "LOAD  " + reg(rd) + ", [" + reg(rs1) + "]";
        case Opcode::STORE: return "STORE [" + reg(rs1) + "], " + reg(rs2);

        // Immediate
        case Opcode::MOVI: return "MOVI " + reg(rd) + ", " + std::to_string(word & 0xFF);
        case Opcode::ADDI: return "ADDI " + reg(rd) + ", " + std::to_string(decodeImm8(word));

        // Jumps
        case Opcode::JMP: return "JMP  " + std::to_string(decodeImm12(word));
        case Opcode::JAL: return "JAL  " + std::to_string(decodeImm12(word));
        case Opcode::JR:  return "JR   " + reg(rd);

        // Branch
        case Opcode::BRANCH:
            if (isBNE(word))
                return "BNE  " + std::to_string(decodeBranchOffset(word));
            else
                return "BEQ  " + std::to_string(decodeBranchOffset(word));

        default: return "???";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./program <file.bin>\n";
        return 1;
    }

    // ── Open binary file ─────────────────────────────────────────────────
    std::ifstream f(argv[1], std::ios::binary);
    if (!f) { std::cerr << "Cannot open: " << argv[1] << "\n"; return 1; }

    // Read header
    // The binary format starts with a 4-byte header:
    // [2 bytes: instruction word count][2 bytes: data word count]
    // This tells the loader how many words to read for each memory bank.
    uint16_t instrCount = 0, dataCount = 0;
    instrCount = (f.get() << 8) | f.get();
    dataCount  = (f.get() << 8) | f.get();

    // Read instruction words
    // Instructions are stored big-endian (high byte first).
    // These will be loaded into the CPU's instruction memory bank.
    std::vector<uint16_t> instrs;
    for (uint16_t i = 0; i < instrCount; ++i) {
        int hi = f.get(), lo = f.get();
        instrs.push_back((static_cast<uint16_t>(hi) << 8) | lo);
    }

    // Read data words
    // Data words follow the instructions in the binary file.
    // These will be loaded into the CPU's data memory bank.
    std::vector<uint16_t> data;
    for (uint16_t i = 0; i < dataCount; ++i) {
        int hi = f.get(), lo = f.get();
        data.push_back((static_cast<uint16_t>(hi) << 8) | lo);
    }

     // ── Initialise CPU components ────────────────────────────────────────
    // Harvard architecture: separate instruction and data memory banks
    Registers regs;
    ALU alu;
    Memory mem(256, 256);
    ControlUnit cpu(regs, alu, mem);

    // Write each instruction word into the instruction bank starting at
    // address 0. The CPU fetches from here during execution.
    for (std::size_t i = 0; i < instrs.size(); ++i)
        mem.writeInstruction(static_cast<uint16_t>(i), instrs[i]);

    // Write data words into the data bank starting at address 0.
    // LOAD/STORE instructions access this bank during execution.
    for (std::size_t i = 0; i < data.size(); ++i)
        mem.writeData(static_cast<uint16_t>(i), data[i]);

    // Ensure PC = 0 and all general-purpose registers are cleared before execution begins.
    regs.reset();

    // Step the CPU one instruction at a time (Fetch → Decode → Execute).
    // Stop when the PC runs past the loaded program or after 1000 cycles
    for (int i = 0; i < 1000; ++i) {
        if (regs.getprogramcountRegister() >= instrs.size()) break;
        cpu.step();
    }

    //print the contents of both memory banks after execution.
    std::cout << "\n--- Instruction Memory ---\n";
    mem.dumpInstructionMemory(0, instrs.size(), std::cout, [](std::uint16_t word) { return disassemble(word); });

    if (!data.empty()) {
    std::cout << "\n--- Data Memory ---\n";
    mem.dumpDataMemory(0, data.size(), std::cout,
        [](std::uint16_t word) -> std::string {
            if (word == 0x0000) return "NULL";
            const char c = static_cast<char>(word & 0xFF);
            if (std::isprint(static_cast<unsigned char>(c)))
                return std::string("'") + c + "'";
            return "non-printable";
        });
}
    std::cout << std::flush;
    return 0;
}