/**
 * test_integration.cpp — Layer 2 integration tests
 *
 * Covers:
 *   Suite 1 — Assembler  (each mnemonic → expected encoded word)
 *   Suite 2 — Memory     (bounds checking, read/write, load helpers)
 *   Suite 3 — Cache      (hit/miss stats, write-back, flush, reset)
 *
 * Build (from project root):
 *   g++ -std=c++17 -Wall -Wextra \
 *       -I./isa -I./emulator -I./assembler \
 *       tests/test_integration.cpp \
 *       emulator/register.cpp \
 *       emulator/Memory.cpp \
 *       assembler/assembler.cpp \
 *       -o tests/test_integration
 *
 * Run:
 *   ./tests/test_integration
 */

#include "assembler/assembler.h"

#include "isa/isa.h"
#include "Memory.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// ════════════════════════════════════════════════════════════════════════════
//  Minimal test harness (same style as test_runner.cpp)
// ════════════════════════════════════════════════════════════════════════════

static int  s_passed = 0;
static int  s_failed = 0;
static std::string s_suite;

static void beginSuite(const std::string& name) {
    s_suite = name;
    std::cout << "\n── " << name << " ──\n";
}

static void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "  PASS  " << label << '\n';
        ++s_passed;
    } else {
        std::cout << "  FAIL  " << label << "  [suite: " << s_suite << "]\n";
        ++s_failed;
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Assembler helper — assemble one line and return the first instruction word
// ════════════════════════════════════════════════════════════════════════════

static uint16_t assemble1(const std::string& line, bool* ok = nullptr) {
    Assembler a;
    auto r = a.assemble(".text\n" + line + "\n");
    if (ok) *ok = r.success;
    if (!r.success || r.instructions.empty()) return 0xFFFF;
    return r.instructions[0];
}

// Assemble a snippet and return the full instruction vector
static std::vector<uint16_t> assembleAll(const std::string& src, bool* ok = nullptr) {
    Assembler a;
    auto r = a.assemble(src);
    if (ok) *ok = r.success;
    return r.instructions;
}

// ════════════════════════════════════════════════════════════════════════════
//  Suite 1 — Assembler
// ════════════════════════════════════════════════════════════════════════════

static void testAssembler() {
    beginSuite("Assembler");

    // ── R-type instructions ───────────────────────────────────────────────
    {
        uint16_t w = assemble1("ADD R1, R2, R3");
        check(decodeOpcode(w) == Opcode::ADD, "ADD: opcode");
        check(decodeRd(w)  == 1,              "ADD: Rd = 1");
        check(decodeRs1(w) == 2,              "ADD: Rs1 = 2");
        check(decodeRs2(w) == 3,              "ADD: Rs2 = 3");
    }
    {
        uint16_t w = assemble1("SUB R4, R5, R6");
        check(decodeOpcode(w) == Opcode::SUB, "SUB: opcode");
        check(decodeRd(w) == 4 && decodeRs1(w) == 5 && decodeRs2(w) == 6,
              "SUB: Rd=4, Rs1=5, Rs2=6");
    }
    {
        uint16_t w = assemble1("AND R0, R1, R2");
        check(decodeOpcode(w) == Opcode::AND, "AND: opcode");
    }
    {
        uint16_t w = assemble1("OR R7, R8, R9");
        check(decodeOpcode(w) == Opcode::OR,  "OR: opcode");
        check(decodeRd(w) == 7,               "OR: Rd = 7");
    }
    {
        uint16_t w = assemble1("MUL R3, R4, R5");
        check(decodeOpcode(w) == Opcode::MUL, "MUL: opcode");
    }
    {
        uint16_t w = assemble1("SHL R1, R2, R3");
        check(decodeOpcode(w) == Opcode::SHL, "SHL: opcode");
    }
    {
        uint16_t w = assemble1("SHR R1, R2, R3");
        check(decodeOpcode(w) == Opcode::SHR, "SHR: opcode");
    }
    {
        // CMP has no Rd — assembler sets Rd field to 0
        uint16_t w = assemble1("CMP R2, R3");
        check(decodeOpcode(w) == Opcode::CMP, "CMP: opcode");
        check(decodeRd(w)     == 0,           "CMP: Rd field = 0");
        check(decodeRs1(w)    == 2,           "CMP: Rs1 = 2");
        check(decodeRs2(w)    == 3,           "CMP: Rs2 = 3");
    }

    // ── I-type instructions ───────────────────────────────────────────────
    {
        uint16_t w = assemble1("MOVI R5, 42");
        check(decodeOpcode(w) == Opcode::MOVI, "MOVI: opcode");
        check(decodeRd(w) == 5,                "MOVI: Rd = 5");
        check((w & 0xFF) == 42,                "MOVI: imm8 = 42");
    }
    {
        // LI is an alias for MOVI
        uint16_t w = assemble1("LI R3, 0xFF");
        check(decodeOpcode(w) == Opcode::MOVI, "LI (MOVI alias): opcode");
        check((w & 0xFF) == 0xFF,              "LI: imm8 = 0xFF");
    }
    {
        uint16_t w = assemble1("ADDI R2, -1");
        check(decodeOpcode(w) == Opcode::ADDI, "ADDI: opcode");
        check(decodeRd(w)     == 2,            "ADDI: Rd = 2");
        check(decodeImm8(w)   == -1,           "ADDI: imm8 = -1");
    }
    {
        uint16_t w = assemble1("ADDI R0, 10");
        check(decodeImm8(w) == 10, "ADDI: imm8 = +10");
    }

    // ── M-type instructions ───────────────────────────────────────────────
    {
        uint16_t w = assemble1("LOAD R1, R2");
        check(decodeOpcode(w) == Opcode::LOAD, "LOAD: opcode");
        check(decodeRd(w)     == 1,            "LOAD: Rd = 1");
        check(decodeRs1(w)    == 2,            "LOAD: Rs1 (addr) = 2");
    }
    {
        // STORE Raddr, Rs  →  encodeM(STORE, Raddr, Rs)
        uint16_t w = assemble1("STORE R5, R3");
        check(decodeOpcode(w) == Opcode::STORE, "STORE: opcode");
        check(decodeRd(w)     == 5,             "STORE: addr reg in Rd field = 5");
        check(decodeRs1(w)    == 3,             "STORE: data reg in Rs1 field = 3");
    }

    // ── Jump instructions ─────────────────────────────────────────────────
    {
        // JMP to label 2 words ahead: offset = target - (pc+1) = 2 - 1 = 1
        auto words = assembleAll(".text\nNOP\nJMP done\nNOP\ndone: NOP\n");
        // word[1] is the JMP; target=3, pc=1, offset = 3-(1+1) = 1
        check(words.size() >= 2,                      "JMP to label: assembled");
        check(decodeOpcode(words[1]) == Opcode::JMP,  "JMP: opcode");
        check(decodeImm12(words[1])  == 1,            "JMP: offset = +1");
    }
    {
        // JAL / CALL alias
        uint16_t w = assemble1("JAL 0");
        check(decodeOpcode(w) == Opcode::JAL, "JAL: opcode");
    }
    {
        uint16_t w = assemble1("JR R14");
        check(decodeOpcode(w) == Opcode::JR, "JR: opcode");
        check(decodeRd(w)     == 14,         "JR: Rd = R14");
    }

    // ── Branch instructions ───────────────────────────────────────────────
    {
        // BEQ to label 1 word ahead: offset = 2-(0+1) = 1
        auto words = assembleAll(".text\nBEQ target\nNOP\ntarget: NOP\n");
        check(words.size() >= 1,                          "BEQ to label: assembled");
        check(decodeOpcode(words[0]) == Opcode::BRANCH,   "BEQ: opcode = BRANCH");
        check(!isBNE(words[0]),                           "BEQ: condition bit = 0");
        check(decodeBranchOffset(words[0]) == 1,          "BEQ: offset = +1");
    }
    {
        auto words = assembleAll(".text\nBNE target\nNOP\ntarget: NOP\n");
        check(decodeOpcode(words[0]) == Opcode::BRANCH,   "BNE: opcode = BRANCH");
        check(isBNE(words[0]),                            "BNE: condition bit = 1");
    }

    // ── Pseudo-instructions ───────────────────────────────────────────────
    {
        uint16_t w = assemble1("NOP");
        check(w == 0x0000, "NOP = 0x0000 (ADD R0,R0,R0)");
    }
    {
        uint16_t w = assemble1("RET");
        check(decodeOpcode(w) == Opcode::JR, "RET: opcode = JR");
        check(decodeRd(w)     == REG_LINK,   "RET: Rd = R14 (link)");
    }
    {
        uint16_t w = assemble1("MOV R3, R7");
        check(decodeOpcode(w) == Opcode::ADD, "MOV: encodes as ADD");
        check(decodeRd(w)     == 3,           "MOV: Rd = 3");
        check(decodeRs1(w)    == 7,           "MOV: Rs1 = 7 (source)");
        check(decodeRs2(w)    == REG_ZERO,    "MOV: Rs2 = R0 (zero)");
    }
    {
        // PUSH Rs → STORE R15, Rs  ;  ADDI R15, -1  (2 words)
        auto words = assembleAll(".text\nPUSH R1\n");
        check(words.size() == 2,                        "PUSH: emits 2 words");
        check(decodeOpcode(words[0]) == Opcode::STORE,  "PUSH word 0: STORE");
        check(decodeRd(words[0])     == REG_SP,         "PUSH word 0: addr = SP");
        check(decodeRs1(words[0])    == 1,              "PUSH word 0: data = R1");
        check(decodeOpcode(words[1]) == Opcode::ADDI,   "PUSH word 1: ADDI");
        check(decodeRd(words[1])     == REG_SP,         "PUSH word 1: Rd = SP");
        check(decodeImm8(words[1])   == -1,             "PUSH word 1: imm = -1");
    }
    {
        // POP Rd → ADDI R15, 1  ;  LOAD Rd, R15  (2 words)
        auto words = assembleAll(".text\nPOP R2\n");
        check(words.size() == 2,                        "POP: emits 2 words");
        check(decodeOpcode(words[0]) == Opcode::ADDI,   "POP word 0: ADDI");
        check(decodeImm8(words[0])   == 1,              "POP word 0: imm = +1");
        check(decodeOpcode(words[1]) == Opcode::LOAD,   "POP word 1: LOAD");
        check(decodeRd(words[1])     == 2,              "POP word 1: Rd = R2");
        check(decodeRs1(words[1])    == REG_SP,         "POP word 1: addr = SP");
    }

    // ── Labels ────────────────────────────────────────────────────────────
    {
        // Forward label reference
        bool ok = false;
        assembleAll(".text\nJMP end\nNOP\nend: NOP\n", &ok);
        check(ok, "Forward label reference: assembles without error");
    }
    {
        // Backward label reference (loop)
        bool ok = false;
        assembleAll(".text\nloop: NOP\nJMP loop\n", &ok);
        check(ok, "Backward label (loop): assembles without error");
    }
    {
        // Duplicate label → error
        bool ok = true;
        assembleAll(".text\nfoo: NOP\nfoo: NOP\n", &ok);
        check(!ok, "Duplicate label: assembler reports error");
    }
    {
        // Undefined label → error
        bool ok = true;
        assembleAll(".text\nJMP nowhere\n", &ok);
        check(!ok, "Undefined label: assembler reports error");
    }

    // ── Data section ──────────────────────────────────────────────────────
    {
        Assembler a;
        auto r = a.assemble(".data\n.word 0x1234\n.word 0xABCD\n");
        check(r.success,              "Data section: assembles ok");
        check(r.data.size() == 2,     "Data section: 2 words emitted");
        check(r.data[0] == 0x1234,    "Data .word 0: 0x1234");
        check(r.data[1] == 0xABCD,    "Data .word 1: 0xABCD");
    }
    {
        // .string emits chars + null terminator
        Assembler a;
        auto r = a.assemble(".data\n.string \"Hi\"\n");
        check(r.success,           ".string: assembles ok");
        check(r.data.size() == 3,  ".string \"Hi\": 3 words (H, i, 0)");
        check(r.data[0] == 'H',    ".string: word 0 = 'H'");
        check(r.data[1] == 'i',    ".string: word 1 = 'i'");
        check(r.data[2] == 0,      ".string: word 2 = null terminator");
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Suite 2 — Memory (uncached)
// ════════════════════════════════════════════════════════════════════════════

static void testMemory() {
    beginSuite("Memory (uncached)");

    Memory mem(256, 256);  // 256-word instruction bank, 256-word data bank

    // ── Basic read/write ─────────────────────────────────────────────────
    mem.writeInstruction(0, 0xABCD);
    check(mem.readInstruction(0) == 0xABCD, "writeInstruction / readInstruction");

    mem.writeData(10, 0x1234);
    check(mem.readData(10) == 0x1234,       "writeData / readData");

    // ── Reset zeros all memory ────────────────────────────────────────────
    mem.reset();
    check(mem.readInstruction(0) == 0, "reset: instruction[0] = 0");
    check(mem.readData(10)       == 0, "reset: data[10] = 0");

    // ── loadInstructionMemory ─────────────────────────────────────────────
    std::vector<uint16_t> prog = {0x0001, 0x0002, 0x0003};
    mem.loadInstructionMemory(prog);
    check(mem.readInstruction(0) == 0x0001, "loadInstruction: word 0");
    check(mem.readInstruction(1) == 0x0002, "loadInstruction: word 1");
    check(mem.readInstruction(2) == 0x0003, "loadInstruction: word 2");

    // ── loadDataMemory ────────────────────────────────────────────────────
    std::vector<uint16_t> data = {0xAAAA, 0xBBBB};
    mem.loadDataMemory(data);
    check(mem.readData(0) == 0xAAAA, "loadData: word 0");
    check(mem.readData(1) == 0xBBBB, "loadData: word 1");

    // ── Word counts ───────────────────────────────────────────────────────
    check(mem.instructionWordCount() == 256, "instructionWordCount = 256");
    check(mem.dataWordCount()        == 256, "dataWordCount = 256");

    // ── Bounds checking — out_of_range exceptions ─────────────────────────
    {
        bool threw = false;
        try { (void)mem.readInstruction(256); }
        catch (const std::out_of_range&) { threw = true; }
        check(threw, "readInstruction(256) throws out_of_range");
    }
    {
        bool threw = false;
        try { mem.writeInstruction(256, 0); }
        catch (const std::out_of_range&) { threw = true; }
        check(threw, "writeInstruction(256) throws out_of_range");
    }
    {
        bool threw = false;
        try { (void)mem.readData(256); }
        catch (const std::out_of_range&) { threw = true; }
        check(threw, "readData(256) throws out_of_range");
    }
    {
        bool threw = false;
        try { mem.writeData(256, 0); }
        catch (const std::out_of_range&) { threw = true; }
        check(threw, "writeData(256) throws out_of_range");
    }

    // ── hasCaches returns false when no cache configured ──────────────────
    check(!mem.hasCaches(), "hasCaches() = false (no cache constructed)");

    // ── cachedRead throws logic_error when no cache ───────────────────────
    {
        bool threw = false;
        try { (void)mem.cachedReadInstruction(0); }
        catch (const std::logic_error&) { threw = true; }
        check(threw, "cachedReadInstruction without cache throws logic_error");
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Suite 3 — Cache
// ════════════════════════════════════════════════════════════════════════════

static void testCache() {
    beginSuite("Cache");

    // Direct-mapped cache: 16 words total, block size 4, associativity 1
    // → 4 sets, 1 way each, 2 offset bits, 2 index bits, 12 tag bits
    CacheConfig cfg{ 16, 4, 1 };

    // Instruction cache: 64-word backing store
    // Data cache: 64-word backing store
    Memory mem(64, 64, cfg, cfg);

    check(mem.hasCaches(), "hasCaches() = true after construction");

    // ── Instruction cache: cold miss then hit ─────────────────────────────
    mem.writeInstruction(0, 0x1111);
    mem.writeInstruction(1, 0x2222);

    // First read: cold miss — block is fetched
    uint16_t v0 = mem.cachedReadInstruction(0);
    check(v0 == 0x1111, "iCache cold read: correct value");
    check(mem.instructionCacheStats().misses == 1, "iCache: 1 miss after cold read");
    check(mem.instructionCacheStats().hits   == 0, "iCache: 0 hits after cold read");

    // Second read of same address: hit
    uint16_t v1 = mem.cachedReadInstruction(0);
    check(v1 == 0x1111, "iCache warm read: correct value");
    check(mem.instructionCacheStats().hits   == 1, "iCache: 1 hit after warm read");

    // Adjacent word in same block: also a hit (block was fetched together)
    uint16_t v2 = mem.cachedReadInstruction(1);
    check(v2 == 0x2222, "iCache: adjacent word in same block = hit");
    check(mem.instructionCacheStats().hits == 2,   "iCache: 2 hits total");

    // ── Data cache: write-back / write-allocate ───────────────────────────
    mem.writeData(0, 0xAAAA);   // prime backing store

    // Cache miss on first read
    uint16_t dv = mem.cachedReadData(0);
    check(dv == 0xAAAA, "dCache cold read: correct value");
    check(mem.dataCacheStats().misses == 1, "dCache: 1 miss");

    // Write hit — marks line dirty
    mem.cachedWriteData(0, 0xBBBB);
    check(mem.dataCacheStats().hits == 1, "dCache: write hit recorded");

    // Read back from cache (still a hit — line is valid)
    uint16_t dv2 = mem.cachedReadData(0);
    check(dv2 == 0xBBBB, "dCache: read after write returns new value");

    // Backing store not yet updated (write-back policy)
    check(mem.readData(0) == 0xAAAA, "dCache write-back: backing store unchanged before flush");

    // Flush writes dirty lines back
    mem.flushCaches();
    check(mem.readData(0) == 0xBBBB, "dCache flush: backing store updated to 0xBBBB");

    // ── resetStats ────────────────────────────────────────────────────────
    mem.resetCacheStats();
    check(mem.instructionCacheStats().hits   == 0, "resetStats: iCache hits = 0");
    check(mem.instructionCacheStats().misses == 0, "resetStats: iCache misses = 0");
    check(mem.dataCacheStats().hits          == 0, "resetStats: dCache hits = 0");
    check(mem.dataCacheStats().misses        == 0, "resetStats: dCache misses = 0");

    // ── resetCaches invalidates all lines ────────────────────────────────
    // After reset the next read must be a miss again
    mem.resetCaches();
    (void)mem.cachedReadInstruction(0);
    check(mem.instructionCacheStats().misses == 1, "resetCaches: next read is a miss");

    // ── Invalid cache config throws ───────────────────────────────────────
    {
        // blockSize must be a power of 2
        bool threw = false;
        try { CacheConfig bad{16, 3, 1}; Memory m2(64, 64, bad, bad); }
        catch (const std::invalid_argument&) { threw = true; }
        check(threw, "Bad blockSize (3, not power of 2): throws invalid_argument");
    }
    {
        // totalWords must be a multiple of blockSize
        bool threw = false;
        try { CacheConfig bad{10, 4, 1}; Memory m2(64, 64, bad, bad); }
        catch (const std::invalid_argument&) { threw = true; }
        check(threw, "totalWords not multiple of blockSize: throws invalid_argument");
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  main
// ════════════════════════════════════════════════════════════════════════════

int main() {
    std::cout << "══════════════════════════════════════════\n";
    std::cout << "  CPU Emulator — Layer 2 Integration Tests\n";
    std::cout << "══════════════════════════════════════════\n";

    testAssembler();
    testMemory();
    testCache();

    std::cout << "\n══════════════════════════════════════════\n";
    std::cout << "  Results: " << s_passed << " passed, " << s_failed << " failed\n";
    std::cout << "══════════════════════════════════════════\n";

    return s_failed > 0 ? 1 : 0;
}
