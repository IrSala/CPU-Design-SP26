#include "register.h"

Registers::Registers() {
    reset();
}

void Registers::reset() {
    generalRegs.fill(0); //Fill general purpose registers with 0s
    instructionRegister = 0;
    programcountRegister = 0x0000;
    stackptrRegister = 0xFFFE;  //Top of memory
}

// Set and Get of General Purpose Registers
void Registers::setGeneralReg(size_t index, uint16_t value) {
    if (index < NUM_REG) {
        generalRegs[index] = value;
    }
}

uint16_t Registers::getGeneralReg(size_t index) const {
    if (index < NUM_REG) {
        return generalRegs[index];
    }
    return 0;
}

//  Get/Set of Special Registers and incrementing program counter register
void Registers::setInstructionRegister(uint16_t value) {
    instructionRegister = value;
}

uint16_t Registers::getInstructionRegister() const {
    return instructionRegister;
}

void Registers::setstackptrRegister(uint16_t value) {
    stackptrRegister = value;
}

uint16_t Registers::getstackptrRegister() const {
    return stackptrRegister;
}

void Registers::setprogramcountRegister(uint16_t value) {
    programcountRegister = value;
}

uint16_t Registers::getprogramcountRegister() const {
    return programcountRegister;
}

void Registers::incrementPC() {
    programcountRegister++;
}

