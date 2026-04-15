/**
 * assembler.cpp — Two-pass assembler for the 16-bit Harvard CPU
 *
 * Uses the encoding helpers from isa.h directly so encoding logic
 * is never duplicated.
 */

#include "assembler.h"
#include "isa.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

// ════════════════════════════════════════════════════════════════════════════
//  String utilities
// ════════════════════════════════════════════════════════════════════════════

static std::string toUpper(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

static std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::vector<std::string> splitLines(const std::string& src) {
    std::vector<std::string> out;
    std::istringstream ss(src);
    std::string line;
    while (std::getline(ss, line)) out.push_back(line);
    return out;
}

// ════════════════════════════════════════════════════════════════════════════
//  Public API
// ════════════════════════════════════════════════════════════════════════════

AssemblerResult Assembler::assemble(const std::string& source) {
    reset();
    auto lines = splitLines(source);

    if (!pass1(lines)) {
        AssemblerResult r;
        r.success  = false;
        r.errors   = errors_;
        r.warnings = warnings_;
        return r;
    }

    std::vector<uint16_t> instr, data;
    std::string listing;
    bool ok = pass2(lines, instr, data, listing);

    AssemblerResult r;
    r.success      = ok && errors_.empty();
    r.instructions = std::move(instr);
    r.data         = std::move(data);
    r.listing      = std::move(listing);
    r.errors       = errors_;
    r.warnings     = warnings_;
    return r;
}

void Assembler::reset() {
    labels_.clear();
    errors_.clear();
    warnings_.clear();
}

// ════════════════════════════════════════════════════════════════════════════
//  Error / warning helpers
// ════════════════════════════════════════════════════════════════════════════

void Assembler::error(int ln, const std::string& msg) {
    errors_.push_back("Line " + std::to_string(ln) + ": error: " + msg);
}

void Assembler::warning(int ln, const std::string& msg) {
    warnings_.push_back("Line " + std::to_string(ln) + ": warning: " + msg);
}

// ════════════════════════════════════════════════════════════════════════════
//  Integer literal parser
//  Handles: decimal, 0xHEX, 0bBINARY, 'char'
// ════════════════════════════════════════════════════════════════════════════

bool Assembler::parseInt(const std::string& text, int ln, int64_t& out) {
    if (text.empty()) { error(ln, "empty literal"); return false; }

    // Character literal 'X' or '\n'
    if (text.front() == '\'') {
        if (text.size() >= 3 && text.back() == '\'') {
            if (text[1] == '\\' && text.size() == 4) {
                switch (text[2]) {
                    case 'n': out = '\n'; return true;
                    case 't': out = '\t'; return true;
                    case 'r': out = '\r'; return true;
                    case '0': out = '\0'; return true;
                    case '\\':out = '\\'; return true;
                    case '\'':out = '\''; return true;
                    default: break;
                }
            } else if (text.size() == 3) {
                out = static_cast<unsigned char>(text[1]);
                return true;
            }
        }
        error(ln, "malformed character literal: " + text);
        return false;
    }

    try {
        std::size_t pos;
        if (text.size() > 2 && text[0] == '0' &&
            (text[1] == 'x' || text[1] == 'X')) {
            out = static_cast<int64_t>(std::stoull(text.substr(2), &pos, 16));
        } else if (text.size() > 2 && text[0] == '0' &&
                   (text[1] == 'b' || text[1] == 'B')) {
            out = static_cast<int64_t>(std::stoull(text.substr(2), &pos, 2));
        } else {
            out = std::stoll(text, &pos, 10);
        }
        return true;
    } catch (...) {
        error(ln, "invalid integer literal: " + text);
        return false;
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Operand helpers
// ════════════════════════════════════════════════════════════════════════════

bool Assembler::resolveValue(const Token& t, int ln, int64_t& out) {
    if (t.kind == Token::Kind::Immediate) { out = t.number; return true; }
    if (t.kind == Token::Kind::LabelRef) {
        auto it = labels_.find(t.text);
        if (it == labels_.end()) {
            error(ln, "undefined label '" + t.text + "'");
            return false;
        }
        out = static_cast<int64_t>(it->second);
        return true;
    }
    error(ln, "expected immediate or label, got '" + t.text + "'");
    return false;
}

bool Assembler::expectReg(const Token& t, int ln, uint8_t& out) {
    if (t.kind != Token::Kind::Register) {
        error(ln, "expected register, got '" + t.text + "'");
        return false;
    }
    out = static_cast<uint8_t>(t.number);
    return true;
}

// Zero-extended 8-bit immediate (for MOVI)
bool Assembler::expectImm8(const Token& t, int ln, uint8_t& out) {
    int64_t v;
    if (!resolveValue(t, ln, v)) return false;
    if (v < 0 || v > 255)
        warning(ln, "immediate " + std::to_string(v) + " truncated to 8 bits");
    out = static_cast<uint8_t>(v & 0xFF);
    return true;
}

// Signed 8-bit immediate (for ADDI)
bool Assembler::expectSImm8(const Token& t, int ln, int8_t& out) {
    int64_t v;
    if (!resolveValue(t, ln, v)) return false;
    if (v < -128 || v > 127)
        warning(ln, "signed immediate " + std::to_string(v) + " truncated to 8 bits");
    out = static_cast<int8_t>(v & 0xFF);
    return true;
}

// PC-relative 12-bit signed offset (for JMP/JAL)
// offset = target - (pc + 1)  [branch is relative to the NEXT instruction]
bool Assembler::expectPCRel12(const Token& t, int ln, uint16_t pc, int16_t& out) {
    int64_t target;
    if (!resolveValue(t, ln, target)) return false;
    int64_t offset = target - static_cast<int64_t>(pc + 1);
    if (offset < -2048 || offset > 2047)
        warning(ln, "JMP/JAL offset " + std::to_string(offset) +
                    " out of [-2048, 2047] range; truncated");
    out = static_cast<int16_t>(offset & 0x0FFF);
    return true;
}

// PC-relative 11-bit signed offset (for BEQ/BNE)
bool Assembler::expectPCRel11(const Token& t, int ln, uint16_t pc, int16_t& out) {
    int64_t target;
    if (!resolveValue(t, ln, target)) return false;
    int64_t offset = target - static_cast<int64_t>(pc + 1);
    if (offset < -1024 || offset > 1023)
        warning(ln, "branch offset " + std::to_string(offset) +
                    " out of [-1024, 1023] range; truncated");
    out = static_cast<int16_t>(offset & 0x07FF);
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  wordsEmitted — how many 16-bit words does a mnemonic produce?
//  Used in Pass 1 for PC tracking without full encoding.
// ════════════════════════════════════════════════════════════════════════════

int Assembler::wordsEmitted(const std::string& m) {
    // Pseudo-instructions that expand to 2 words
    if (m == "PUSH" || m == "POP") return 2;
    // Everything else (real instructions + NOP/RET/MOV/LI/CALL) = 1 word
    return 1;
}

// ════════════════════════════════════════════════════════════════════════════
//  Pass 1 — tokenise lightly, collect label → PC mapping
// ════════════════════════════════════════════════════════════════════════════

bool Assembler::pass1(const std::vector<std::string>& rawLines) {
    uint16_t textPC  = 0;
    uint16_t dataPC  = 0;
    bool     inData  = false;

    for (int ln = 1; ln <= static_cast<int>(rawLines.size()); ++ln) {
        // Strip comment and trim
        std::string line = rawLines[ln - 1];
        auto semi = line.find(';');
        if (semi != std::string::npos) line = line.substr(0, semi);
        line = trim(line);
        if (line.empty()) continue;

        // Section switches
        std::string up = toUpper(line);
        if (up == ".TEXT") { inData = false; continue; }
        if (up == ".DATA") { inData = true;  continue; }

        // Peel label
        std::string rest = line;
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string lbl = trim(line.substr(0, colon));
            bool valid = !lbl.empty();
            for (char c : lbl)
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                    { valid = false; break; }
            if (valid) {
                std::string key = toUpper(lbl);
                if (labels_.count(key))
                    error(ln, "duplicate label '" + lbl + "'");
                else
                    labels_[key] = inData ? dataPC : textPC;
                rest = trim(line.substr(colon + 1));
                if (rest.empty()) continue;
            }
        }

        // Get mnemonic
        std::string mnem;
        std::istringstream ss(rest);
        ss >> mnem;
        mnem = toUpper(mnem);

        if (inData) {
            if (mnem == ".WORD") {
                dataPC++;
            } else if (mnem == ".STRING") {
                // Count chars + null terminator
                auto q1 = rest.find('"');
                auto q2 = rest.rfind('"');
                if (q1 != std::string::npos && q2 > q1) {
                    std::string s = rest.substr(q1 + 1, q2 - q1 - 1);
                    size_t cnt = 0;
                    for (size_t i = 0; i < s.size(); ++i) {
                        if (s[i] == '\\' && i + 1 < s.size()) ++i;
                        ++cnt;
                    }
                    dataPC += static_cast<uint16_t>(cnt + 1); // +1 for '\0'
                }
            } else if (mnem == ".ASCII") {
                auto q1 = rest.find('"');
                auto q2 = rest.rfind('"');
                if (q1 != std::string::npos && q2 > q1) {
                    std::string s = rest.substr(q1 + 1, q2 - q1 - 1);
                    size_t cnt = 0;
                    for (size_t i = 0; i < s.size(); ++i) {
                        if (s[i] == '\\' && i + 1 < s.size()) ++i;
                        ++cnt;
                    }
                    dataPC += static_cast<uint16_t>(cnt);
                }
            }
        } else {
            if (!mnem.empty() && mnem[0] != '.') {
                textPC += static_cast<uint16_t>(wordsEmitted(mnem));
            }
        }
    }
    return errors_.empty();
}

// ════════════════════════════════════════════════════════════════════════════
//  Tokeniser — splits one source line into a SourceLine struct
// ════════════════════════════════════════════════════════════════════════════

bool Assembler::tokeniseLine(const std::string& rawIn, int ln, SourceLine& out) {
    out.lineNum = ln;
    out.raw     = rawIn;

    // Strip comment
    std::string line = rawIn;
    auto semi = line.find(';');
    if (semi != std::string::npos) line = line.substr(0, semi);
    line = trim(line);

    if (line.empty()) { out.isEmpty = true; return true; }

    // Section directives
    std::string up = toUpper(line);
    if (up == ".TEXT" || up == ".DATA") {
        out.mnemonic    = up;
        out.isDirective = true;
        return true;
    }

    // Peel label
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string lbl = trim(line.substr(0, colon));
        bool valid = !lbl.empty();
        for (char c : lbl)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                { valid = false; break; }
        if (valid) {
            out.label = toUpper(lbl);
            line = trim(line.substr(colon + 1));
            if (line.empty()) return true;
        }
    }

    // Extract mnemonic
    {
        std::istringstream ss(line);
        ss >> out.mnemonic;
        out.mnemonic = toUpper(out.mnemonic);
        if (!out.mnemonic.empty() && out.mnemonic[0] == '.')
            out.isDirective = true;
        line = trim(line.substr(out.mnemonic.size()));
    }

    // String directive — grab quoted content raw
    if (out.mnemonic == ".STRING" || out.mnemonic == ".ASCII") {
        auto q1 = line.find('"');
        auto q2 = line.rfind('"');
        if (q1 == std::string::npos || q2 == q1) {
            error(ln, out.mnemonic + " requires a quoted string");
            return false;
        }
        Token t;
        t.kind = Token::Kind::String;
        t.text = line.substr(q1 + 1, q2 - q1 - 1);
        t.line = ln;
        out.operands.push_back(t);
        return true;
    }

    // Replace commas with spaces and tokenise operands
    for (char& c : line) if (c == ',') c = ' ';
    std::istringstream ops(line);
    std::string tok;
    while (ops >> tok) {
        Token t;
        t.line = ln;
        std::string upt = toUpper(tok);

        // Register?  R0–R15
        if (upt.size() >= 2 && upt[0] == 'R') {
            bool digits = true;
            for (size_t i = 1; i < upt.size(); ++i)
                if (!std::isdigit(static_cast<unsigned char>(upt[i])))
                    { digits = false; break; }
            if (digits) {
                int r = std::stoi(upt.substr(1));
                if (r >= 0 && r <= 15) {
                    t.kind   = Token::Kind::Register;
                    t.text   = upt;
                    t.number = r;
                    out.operands.push_back(t);
                    continue;
                }
            }
        }

        // Numeric / char literal?
        bool looksNumeric =
            std::isdigit(static_cast<unsigned char>(tok[0])) ||
            tok[0] == '-' || tok[0] == '\'' ||
            (tok.size() > 1 && tok[0] == '0' &&
             (tok[1] == 'x' || tok[1] == 'X' || tok[1] == 'b' || tok[1] == 'B'));

        if (looksNumeric) {
            int64_t val;
            if (!parseInt(tok, ln, val)) return false;
            t.kind   = Token::Kind::Immediate;
            t.text   = tok;
            t.number = val;
            out.operands.push_back(t);
            continue;
        }

        // Label reference (identifier)
        bool validId = !tok.empty() &&
            (std::isalpha(static_cast<unsigned char>(tok[0])) || tok[0] == '_');
        for (size_t i = 1; validId && i < tok.size(); ++i)
            if (!std::isalnum(static_cast<unsigned char>(tok[i])) && tok[i] != '_')
                validId = false;

        if (validId) {
            t.kind = Token::Kind::LabelRef;
            t.text = toUpper(tok);
            out.operands.push_back(t);
            continue;
        }

        error(ln, "unrecognised token: " + tok);
        return false;
    }
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  encodeInstruction — emit 1 or 2 words using isa.h helpers
// ════════════════════════════════════════════════════════════════════════════

bool Assembler::encodeInstruction(const SourceLine& sl, uint16_t pc,
                                   std::vector<uint16_t>& out)
{
    const auto& ops = sl.operands;
    const int   ln  = sl.lineNum;
    const auto  nops = ops.size();
    const std::string& m = sl.mnemonic;

    // Helper: require at least n operands
    auto need = [&](size_t n) -> bool {
        if (nops < n) {
            error(ln, m + " requires " + std::to_string(n) + " operand(s)");
            return false;
        }
        return true;
    };

    // ── Directives ────────────────────────────────────────────────────
    if (m == ".TEXT" || m == ".DATA") return true;

    if (m == ".WORD") {
        if (!need(1)) return false;
        int64_t v;
        if (!resolveValue(ops[0], ln, v)) return false;
        out.push_back(static_cast<uint16_t>(v));
        return true;
    }

    if (m == ".STRING") {
        if (!need(1)) return false;
        const std::string& s = ops[0].text;
        for (size_t i = 0; i < s.size(); ++i) {
            uint16_t ch;
            if (s[i] == '\\' && i + 1 < s.size()) {
                ++i;
                switch (s[i]) {
                    case 'n':  ch = '\n'; break;
                    case 't':  ch = '\t'; break;
                    case 'r':  ch = '\r'; break;
                    case '0':  ch = '\0'; break;
                    case '\\': ch = '\\'; break;
                    case '\'': ch = '\''; break;
                    case '"':  ch = '"';  break;
                    default:   ch = s[i]; break;
                }
            } else {
                ch = static_cast<uint16_t>(s[i]);
            }
            out.push_back(ch);
        }
        out.push_back(0); // null terminator
        return true;
    }

    if (m == ".ASCII") {
        if (!need(1)) return false;
        const std::string& s = ops[0].text;
        for (size_t i = 0; i < s.size(); ++i) {
            uint16_t ch;
            if (s[i] == '\\' && i + 1 < s.size()) {
                ++i;
                switch (s[i]) {
                    case 'n':  ch = '\n'; break;
                    case 't':  ch = '\t'; break;
                    case 'r':  ch = '\r'; break;
                    case '0':  ch = '\0'; break;
                    case '\\': ch = '\\'; break;
                    case '\'': ch = '\''; break;
                    case '"':  ch = '"';  break;
                    default:   ch = s[i]; break;
                }
            } else {
                ch = static_cast<uint16_t>(s[i]);
            }
            out.push_back(ch);
        }
        return true;
    }

    // ── R-type instructions ───────────────────────────────────────────
    // ADD Rd, Rs1, Rs2
    if (m == "ADD") {
        if (!need(3)) return false;
        uint8_t rd, rs1, rs2;
        if (!expectReg(ops[0], ln, rd))  return false;
        if (!expectReg(ops[1], ln, rs1)) return false;
        if (!expectReg(ops[2], ln, rs2)) return false;
        out.push_back(encodeR(Opcode::ADD, rd, rs1, rs2));
        return true;
    }

    if (m == "SUB") {
        if (!need(3)) return false;
        uint8_t rd, rs1, rs2;
        if (!expectReg(ops[0], ln, rd))  return false;
        if (!expectReg(ops[1], ln, rs1)) return false;
        if (!expectReg(ops[2], ln, rs2)) return false;
        out.push_back(encodeR(Opcode::SUB, rd, rs1, rs2));
        return true;
    }

    if (m == "AND") {
        if (!need(3)) return false;
        uint8_t rd, rs1, rs2;
        if (!expectReg(ops[0], ln, rd))  return false;
        if (!expectReg(ops[1], ln, rs1)) return false;
        if (!expectReg(ops[2], ln, rs2)) return false;
        out.push_back(encodeR(Opcode::AND, rd, rs1, rs2));
        return true;
    }

    if (m == "OR") {
        if (!need(3)) return false;
        uint8_t rd, rs1, rs2;
        if (!expectReg(ops[0], ln, rd))  return false;
        if (!expectReg(ops[1], ln, rs1)) return false;
        if (!expectReg(ops[2], ln, rs2)) return false;
        out.push_back(encodeR(Opcode::OR, rd, rs1, rs2));
        return true;
    }

    if (m == "MUL") {
        if (!need(3)) return false;
        uint8_t rd, rs1, rs2;
        if (!expectReg(ops[0], ln, rd))  return false;
        if (!expectReg(ops[1], ln, rs1)) return false;
        if (!expectReg(ops[2], ln, rs2)) return false;
        out.push_back(encodeR(Opcode::MUL, rd, rs1, rs2));
        return true;
    }

    if (m == "SHL") {
        if (!need(3)) return false;
        uint8_t rd, rs1, rs2;
        if (!expectReg(ops[0], ln, rd))  return false;
        if (!expectReg(ops[1], ln, rs1)) return false;
        if (!expectReg(ops[2], ln, rs2)) return false;
        out.push_back(encodeR(Opcode::SHL, rd, rs1, rs2));
        return true;
    }

    if (m == "SHR") {
        if (!need(3)) return false;
        uint8_t rd, rs1, rs2;
        if (!expectReg(ops[0], ln, rd))  return false;
        if (!expectReg(ops[1], ln, rs1)) return false;
        if (!expectReg(ops[2], ln, rs2)) return false;
        out.push_back(encodeR(Opcode::SHR, rd, rs1, rs2));
        return true;
    }

    // CMP Rs1, Rs2  — no destination (Rd field = 0)
    if (m == "CMP") {
        if (!need(2)) return false;
        uint8_t rs1, rs2;
        if (!expectReg(ops[0], ln, rs1)) return false;
        if (!expectReg(ops[1], ln, rs2)) return false;
        out.push_back(encodeR(Opcode::CMP, 0, rs1, rs2));
        return true;
    }

    // ── I-type instructions ───────────────────────────────────────────

    // MOVI Rd, imm8  (zero-extended)
    if (m == "MOVI" || m == "LI") {
        if (!need(2)) return false;
        uint8_t rd, imm;
        if (!expectReg(ops[0], ln, rd))   return false;
        if (!expectImm8(ops[1], ln, imm)) return false;
        out.push_back(encodeI(Opcode::MOVI, rd, static_cast<int8_t>(imm)));
        return true;
    }

    // ADDI Rd, imm8  (signed)
    if (m == "ADDI") {
        if (!need(2)) return false;
        uint8_t rd; int8_t imm;
        if (!expectReg(ops[0], ln, rd))    return false;
        if (!expectSImm8(ops[1], ln, imm)) return false;
        out.push_back(encodeI(Opcode::ADDI, rd, imm));
        return true;
    }

    // ── Memory instructions ───────────────────────────────────────────

    // LOAD Rd, Raddr   →  Rd = Mem[Raddr]
    if (m == "LOAD") {
        if (!need(2)) return false;
        uint8_t rd, raddr;
        if (!expectReg(ops[0], ln, rd))    return false;
        if (!expectReg(ops[1], ln, raddr)) return false;
        out.push_back(encodeM(Opcode::LOAD, rd, raddr));
        return true;
    }

    // STORE Raddr, Rs  →  Mem[Raddr] = Rs
    // isa.h encodeM: op | (rd<<8) | (raddr<<4)
    // For STORE: rd field = address register, raddr field = data register
    if (m == "STORE") {
        if (!need(2)) return false;
        uint8_t raddr, rs;
        if (!expectReg(ops[0], ln, raddr)) return false;
        if (!expectReg(ops[1], ln, rs))    return false;
        // encodeM puts first arg in [11:8] (rd) and second in [7:4] (raddr)
        out.push_back(encodeM(Opcode::STORE, raddr, rs));
        return true;
    }

    // ── Jump instructions ─────────────────────────────────────────────

    // JMP label / imm   (PC-relative, 12-bit signed offset)
    if (m == "JMP") {
        if (!need(1)) return false;
        int16_t off;
        if (!expectPCRel12(ops[0], ln, pc, off)) return false;
        out.push_back(encodeJ(Opcode::JMP, off));
        return true;
    }

    // JAL label / imm   (call: R14 = PC+1, PC += offset)
    if (m == "JAL" || m == "CALL") {
        if (!need(1)) return false;
        int16_t off;
        if (!expectPCRel12(ops[0], ln, pc, off)) return false;
        out.push_back(encodeJ(Opcode::JAL, off));
        return true;
    }

    // JR Rd   (PC = Rd, typically RET = JR R14)
    if (m == "JR") {
        if (!need(1)) return false;
        uint8_t rd;
        if (!expectReg(ops[0], ln, rd)) return false;
        out.push_back(encodeJR(rd));
        return true;
    }

    // ── Branch instructions ───────────────────────────────────────────

    // BEQ label   (branch if Z==1, cond bit=0)
    if (m == "BEQ") {
        if (!need(1)) return false;
        int16_t off;
        if (!expectPCRel11(ops[0], ln, pc, off)) return false;
        out.push_back(encodeBEQ(off));
        return true;
    }

    // BNE label   (branch if Z==0, cond bit=1)
    if (m == "BNE") {
        if (!need(1)) return false;
        int16_t off;
        if (!expectPCRel11(ops[0], ln, pc, off)) return false;
        out.push_back(encodeBNE(off));
        return true;
    }

    // ── Pseudo-instructions ───────────────────────────────────────────

    // NOP → ADD R0, R0, R0
    if (m == "NOP") {
        out.push_back(encodeR(Opcode::ADD, REG_ZERO, REG_ZERO, REG_ZERO));
        return true;
    }

    // RET → JR R14
    if (m == "RET") {
        out.push_back(encodeJR(REG_LINK));
        return true;
    }

    // MOV Rd, Rs → ADD Rd, Rs, R0
    if (m == "MOV") {
        if (!need(2)) return false;
        uint8_t rd, rs;
        if (!expectReg(ops[0], ln, rd)) return false;
        if (!expectReg(ops[1], ln, rs)) return false;
        out.push_back(encodeR(Opcode::ADD, rd, rs, REG_ZERO));
        return true;
    }

    // PUSH Rs → STORE R15, Rs  ;  ADDI R15, -1
    if (m == "PUSH") {
        if (!need(1)) return false;
        uint8_t rs;
        if (!expectReg(ops[0], ln, rs)) return false;
        out.push_back(encodeM(Opcode::STORE, REG_SP, rs));
        out.push_back(encodeI(Opcode::ADDI,  REG_SP, -1));
        return true;
    }

    // POP Rd → ADDI R15, 1  ;  LOAD Rd, R15
    if (m == "POP") {
        if (!need(1)) return false;
        uint8_t rd;
        if (!expectReg(ops[0], ln, rd)) return false;
        out.push_back(encodeI(Opcode::ADDI, REG_SP, 1));
        out.push_back(encodeM(Opcode::LOAD, rd, REG_SP));
        return true;
    }

    error(ln, "unknown mnemonic '" + m + "'");
    return false;
}

// ════════════════════════════════════════════════════════════════════════════
//  Pass 2 — encode all lines, build listing
// ════════════════════════════════════════════════════════════════════════════

bool Assembler::pass2(const std::vector<std::string>& rawLines,
                      std::vector<uint16_t>& instOut,
                      std::vector<uint16_t>& dataOut,
                      std::string& listing)
{
    std::ostringstream lst;
    lst << "; Assembled listing\n"
        << "; Addr  Word   Source\n"
        << "; ────  ────   ──────\n";

    uint16_t textPC = 0;
    uint16_t dataPC = 0;
    bool inData = false;

    for (int ln = 1; ln <= static_cast<int>(rawLines.size()); ++ln) {
        SourceLine sl;
        if (!tokeniseLine(rawLines[ln - 1], ln, sl)) continue;

        if (sl.isEmpty) { lst << ";\n"; continue; }
        if (sl.mnemonic == ".TEXT") { inData = false; continue; }
        if (sl.mnemonic == ".DATA") { inData = true;  continue; }

        // Label-only line
        if (sl.mnemonic.empty()) {
            uint16_t addr = inData ? dataPC : textPC;
            lst << "; " << std::hex << std::uppercase
                << std::setw(4) << std::setfill('0') << addr
                << "        " << sl.label << ":\n";
            continue;
        }

        std::vector<uint16_t> words;
        uint16_t addrBefore = inData ? dataPC : textPC;
        bool ok = encodeInstruction(sl, textPC, words);

        for (size_t i = 0; i < words.size(); ++i) {
            uint16_t addr = addrBefore + static_cast<uint16_t>(i);
            lst << "  " << std::hex << std::uppercase
                << std::setw(4) << std::setfill('0') << addr
                << "  " << std::setw(4) << std::setfill('0') << words[i]
                << "   ";
            if (i == 0) {
                if (!sl.label.empty()) lst << sl.label << ": ";
                lst << rawLines[ln - 1];
            }
            lst << "\n";
        }

        if (ok) {
            if (inData) {
                for (auto w : words) dataOut.push_back(w);
                dataPC += static_cast<uint16_t>(words.size());
            } else {
                for (auto w : words) instOut.push_back(w);
                textPC += static_cast<uint16_t>(words.size());
            }
        }
    }

    listing = lst.str();
    return errors_.empty();
}

// ════════════════════════════════════════════════════════════════════════════
//  Disassembler
// ════════════════════════════════════════════════════════════════════════════

std::string Assembler::disassemble(uint16_t word, uint16_t pc) {
    Opcode   op  = decodeOpcode(word);
    uint8_t  rd  = decodeRd(word);
    uint8_t  rs1 = decodeRs1(word);
    uint8_t  rs2 = decodeRs2(word);
    int8_t   im8 = decodeImm8(word);
    int16_t  im12= decodeImm12(word);

    auto R = [](uint8_t r) { return "R" + std::to_string(r); };

    std::ostringstream s;
    switch (op) {
        case Opcode::ADD:
            if (rd == 0 && rs1 == 0 && rs2 == 0)
                s << "NOP";
            else if (rs2 == REG_ZERO)
                s << "MOV  " << R(rd) << ", " << R(rs1);
            else
                s << "ADD  " << R(rd) << ", " << R(rs1) << ", " << R(rs2);
            break;
        case Opcode::SUB:
            s << "SUB  " << R(rd) << ", " << R(rs1) << ", " << R(rs2); break;
        case Opcode::AND:
            s << "AND  " << R(rd) << ", " << R(rs1) << ", " << R(rs2); break;
        case Opcode::OR:
            s << "OR   " << R(rd) << ", " << R(rs1) << ", " << R(rs2); break;
        case Opcode::MUL:
            s << "MUL  " << R(rd) << ", " << R(rs1) << ", " << R(rs2); break;
        case Opcode::SHL:
            s << "SHL  " << R(rd) << ", " << R(rs1) << ", " << R(rs2); break;
        case Opcode::SHR:
            s << "SHR  " << R(rd) << ", " << R(rs1) << ", " << R(rs2); break;
        case Opcode::CMP:
            s << "CMP  " << R(rs1) << ", " << R(rs2); break;
        case Opcode::LOAD:
            s << "LOAD " << R(rd) << ", " << R(rs1); break;
        case Opcode::STORE:
            s << "STORE " << R(rd) << ", " << R(rs1); break;
        case Opcode::MOVI:
            s << "MOVI " << R(rd) << ", " << static_cast<unsigned>(word & 0xFF); break;
        case Opcode::ADDI:
            if (rd == REG_SP && im8 == -1)
                s << "  ; (PUSH step 2)";
            else if (rd == REG_SP && im8 == 1)
                s << "  ; (POP step 1)";
            s.str("");
            s << "ADDI " << R(rd) << ", " << static_cast<int>(im8); break;
        case Opcode::JMP: {
            uint16_t target = static_cast<uint16_t>(pc + 1 + im12);
            s << "JMP  0x" << std::hex << std::uppercase << target
              << "  ; offset=" << std::dec << static_cast<int>(im12);
            break;
        }
        case Opcode::JAL: {
            uint16_t target = static_cast<uint16_t>(pc + 1 + im12);
            s << "JAL  0x" << std::hex << std::uppercase << target
              << "  ; offset=" << std::dec << static_cast<int>(im12);
            break;
        }
        case Opcode::JR:
            if (rd == REG_LINK) s << "RET";
            else                s << "JR   " << R(rd);
            break;
        case Opcode::BRANCH: {
            bool bne = isBNE(word);
            int16_t off = decodeBranchOffset(word);
            uint16_t target = static_cast<uint16_t>(pc + 1 + off);
            s << (bne ? "BNE  " : "BEQ  ")
              << "0x" << std::hex << std::uppercase << target
              << "  ; offset=" << std::dec << static_cast<int>(off);
            break;
        }
        default:
            s << "??? (0x" << std::hex << std::uppercase
              << std::setw(4) << std::setfill('0') << word << ")";
    }
    return s.str();
}

std::string Assembler::disassembleAll(const std::vector<uint16_t>& words, uint16_t start) {
    std::ostringstream out;
    for (size_t i = 0; i < words.size(); ++i) {
        uint16_t addr = start + static_cast<uint16_t>(i);
        out << "  0x" << std::hex << std::uppercase
            << std::setw(4) << std::setfill('0') << addr
            << ":  " << std::setw(4) << std::setfill('0') << words[i]
            << "  " << disassemble(words[i], addr) << "\n";
    }
    return out.str();
}
