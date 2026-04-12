#pragma once
#include <cstdint>
#include "alu.h"
#include "bus.h"
#include "register.h"   

struct CPU {
    // --- Registers ---
    Registers regs; 

    // --- Your components ---
    ALU        alu;
    DataBus    dataBus;
    AddressBus addrBus;
    InstructionBus instrBus;
};