#pragma once
/**
 * Assembler for the 16-bit Harvard CPU
 * ═════════════════════════════════════
 *
 * Instruction formats (from isa.h):
 *
 *   R-type  [15:12]=op  [11:8]=Rd  [7:4]=Rs1  [3:0]=Rs2
 *     ADD, SUB, AND, OR, MUL, SHL, SHR, CMP
 *
 *   I-type  [15:12]=op  [11:8]=Rd  [7:0]=imm8
 *     MOVI (zero-extended), ADDI (signed)
 *
 *   M-type  [15:12]=op  [11:8]=Rd/Rs  [7:4]=Raddr
 *     LOAD  Rd, Raddr        Rd = Mem[Raddr]
 *     STORE Raddr, Rs        Mem[Raddr] = Rs  (Raddr in [11:8], Rs in [7:4])
 *
 *   J-type  [15:12]=op  [11:0]=imm12 (signed PC-relative)
 *     JMP  label / imm12
 *     JAL  label / imm12     R14 = PC+1; PC += imm12
 *
 *   JR-type [15:12]=0xE [11:8]=Rd
 *     JR   Rd                PC = Rd
 *
 *   BRANCH  [15:12]=0xF [11]=cond  [10:0]=imm11 (signed PC-relative)
 *     BEQ  label             branch if Z==1
 *     BNE  label             branch if Z==0
 *
 * Pseudo-instructions (expanded by assembler):
 *   NOP              → ADD R0, R0, R0
 *   RET              → JR R14
 *   MOV Rd, Rs       → ADD Rd, Rs, R0
 *   LI  Rd, imm      → MOVI Rd, imm
 *   CALL label       → JAL label
 *   PUSH Rs          → STORE R15, Rs  ;  ADDI R15, -1   (2 words)
 *   POP  Rd          → ADDI R15, 1    ;  LOAD  Rd, R15  (2 words)
 *
 * Directives:
 *   .text / .data    section switch
 *   .word <value>    emit one 16-bit word into data section
 *   .string "..."    null-terminated string (one word per char) into data
 *   .ascii  "..."    same but no null terminator
 */

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

struct AssemblerResult {
    bool                     success = false;
    std::vector<uint16_t>    instructions;  ///< text section words
    std::vector<uint16_t>    data;          ///< data section words
    std::string              listing;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

class Assembler {
public:
    Assembler() = default;

    AssemblerResult assemble(const std::string& source);

    static std::string disassemble   (uint16_t word, uint16_t pc = 0);
    static std::string disassembleAll(const std::vector<uint16_t>& words,
                                      uint16_t startPC = 0);

private:
    struct Token {
        enum class Kind { Mnemonic, Register, Immediate, LabelRef, String, Directive };
        Kind        kind;
        std::string text;
        int64_t     number = 0;
        int         line   = 0;
    };

    struct SourceLine {
        int                lineNum     = 0;
        std::string        raw;
        std::string        label;
        std::string        mnemonic;
        std::vector<Token> operands;
        bool               isDirective = false;
        bool               isEmpty     = false;
    };

    std::unordered_map<std::string, uint16_t> labels_;
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;

    void reset();
    bool pass1(const std::vector<std::string>& rawLines);
    int  wordsEmitted(const std::string& mnemonic);
    bool pass2(const std::vector<std::string>& rawLines,
               std::vector<uint16_t>& instOut,
               std::vector<uint16_t>& dataOut,
               std::string& listing);

    bool tokeniseLine    (const std::string& raw, int lineNum, SourceLine& out);
    bool parseInt        (const std::string& text, int lineNum, int64_t& out);
    bool encodeInstruction(const SourceLine& sl, uint16_t pc,
                           std::vector<uint16_t>& out);

    bool expectReg      (const Token& t, int ln, uint8_t& out);
    bool expectImm8     (const Token& t, int ln, uint8_t& out);
    bool expectSImm8    (const Token& t, int ln, int8_t& out);
    bool expectPCRel12  (const Token& t, int ln, uint16_t pc, int16_t& out);
    bool expectPCRel11  (const Token& t, int ln, uint16_t pc, int16_t& out);
    bool resolveValue   (const Token& t, int ln, int64_t& out);

    void error  (int ln, const std::string& msg);
    void warning(int ln, const std::string& msg);
};
