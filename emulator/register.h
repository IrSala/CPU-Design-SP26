#ifndef REGISTER_H
#define REGISTER_H

#include <array>
#include <cstdint>
#include <cstddef>
#include <stdlib.h>
#include <stdio.h>

class Registers{
    public:
        //Number of General Purpose Registers
        static constexpr size_t NUM_REG = 16;

    private:
        std::array<uint16_t,NUM_REG> generalRegs;
        uint16_t instructionRegister;
        uint16_t stackptrRegister;
        uint16_t programcountRegister;
    
    public:
        Registers();
        void reset();

        //General Purpose Register access
        void setGeneralReg(size_t index, uint16_t value);
        uint16_t getGeneralReg(size_t index) const;

        // Special registers access
        void setInstructionRegister(uint16_t value);
        uint16_t getInstructionRegister() const;

        void setstackptrRegister(uint16_t value);
        uint16_t getstackptrRegister() const;

        void setprogramcountRegister(uint16_t value);
        uint16_t getprogramcountRegister() const;

        // Utility
        void incrementPC();

};

#endif