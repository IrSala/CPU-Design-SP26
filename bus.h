#pragma once
#include <cstdint>

//Carries 16-bit data between CPU components (registers, memory, I/O)
struct DataBus {
    uint16_t data = 0;

    void     write(uint16_t val) { data = val; }
    uint16_t read() const        { return data; }
};

//Carries 16-bit memory addresses (can address 0x0000 - 0xFFFF = 65536 locations)
struct AddressBus {
    uint16_t address = 0;

    void     write(uint16_t addr) { address = addr; }
    uint16_t read() const         { return address; }
};

//Carries fetched instructions from memory to the Instruction Register
struct InstructionBus {
    uint16_t instruction = 0;

    void     write(uint16_t instr) { instruction = instr; }
    uint16_t read() const          { return instruction; }
};