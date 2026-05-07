#pragma once
#include <cstdint>
#include "alu.h"
#include "bus.h"
#include "register.h"
#include "Memory.h"
#include "isa.h"

struct CPU {
    // --- Components ---
    Registers      regs;
    ALU            alu;
    DataBus        dataBus;
    AddressBus     addrBus;
    InstructionBus instrBus;
    Memory*        memory = nullptr;  // set before calling run()

    // --- Fetch ---
    // Reads the instruction at PC from memory, loads it into the
    // instruction bus and instruction register, then increments PC.
    void fetch() {
        uint16_t pc = regs.getprogramcountRegister();

        addrBus.write(pc);
        uint16_t instr = memory->readInstruction(pc);
        instrBus.write(instr);
        regs.setInstructionRegister(instr);

        regs.incrementPC();
    }

    // --- Decode ---
    // Pulls the current instruction out of the instruction register
    // and returns the opcode. The control unit will use this to
    // decide what to execute.
    Opcode decode() {
        uint16_t instr = regs.getInstructionRegister();
        return decodeOpcode(instr);
    }

    // --- Execute (stub — control unit fills this in) ---
    // Dispatches to the right operation based on the opcode.
    void execute() {
        uint16_t instr = regs.getInstructionRegister();
        Opcode   op    = decodeOpcode(instr);

        switch (op) {
            case Opcode::ADD: case Opcode::SUB: case Opcode::AND:
            case Opcode::OR:  case Opcode::MUL: case Opcode::SHL:
            case Opcode::SHR: case Opcode::CMP:
                // R-type: control unit implements these
                break;

            case Opcode::MOVI: case Opcode::ADDI:
                // I-type: control unit implements these
                break;

            case Opcode::LOAD: case Opcode::STORE:
                // M-type: control unit implements these
                break;

            case Opcode::JMP: case Opcode::JAL:
            case Opcode::JR:  case Opcode::BRANCH:
                // J-type: control unit implements these
                break;
        }
    }

    // --- Single step (one full Fetch / Decode / Execute cycle) ---
    void step() {
        fetch();
        decode();   // control unit reads opcode here
        execute();  // control unit drives ALU + registers + memory
    }
};
