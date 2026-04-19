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
//To run Binary file -  
//g++ -std=c++17 -Wall -Wextra     -I./isa -I./emulator     run_emulator.cpp emulator/*.cpp     -o program
//./program binary file to execute the program
//

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./program <file.bin>\n";
        return 1;
    }

    std::ifstream f(argv[1], std::ios::binary);
    if (!f) { std::cerr << "Cannot open: " << argv[1] << "\n"; return 1; }

    // Read header
    uint16_t instrCount = 0, dataCount = 0;
    instrCount = (f.get() << 8) | f.get();
    dataCount  = (f.get() << 8) | f.get();

    // Read instruction words
    std::vector<uint16_t> instrs;
    for (uint16_t i = 0; i < instrCount; ++i) {
        int hi = f.get(), lo = f.get();
        instrs.push_back((static_cast<uint16_t>(hi) << 8) | lo);
    }

    // Read data words
    std::vector<uint16_t> data;
    for (uint16_t i = 0; i < dataCount; ++i) {
        int hi = f.get(), lo = f.get();
        data.push_back((static_cast<uint16_t>(hi) << 8) | lo);
    }

    Registers regs;
    ALU alu;
    Memory mem(256, 256);
    ControlUnit cpu(regs, alu, mem);

    // Load both sections into correct memory banks
    for (std::size_t i = 0; i < instrs.size(); ++i)
        mem.writeInstruction(static_cast<uint16_t>(i), instrs[i]);

    for (std::size_t i = 0; i < data.size(); ++i)
        mem.writeData(static_cast<uint16_t>(i), data[i]);

    regs.reset();

    for (int i = 0; i < 1000; ++i) {
        if (regs.getprogramcountRegister() >= instrs.size()) break;
        cpu.step();
    }

    std::cout << std::flush;
    return 0;
}