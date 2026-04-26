/**
 * main.cpp — Command-line driver for the CPU assembler
 *
 * Usage:
 *   ./assembler <source.asm> [output.bin] [--listing] [--disasm] [--hex]
 *
 * Flags:
 *   --listing   print annotated listing to stdout
 *   --disasm    disassemble the output back to mnemonics
 *   --hex       print hex dump of encoded words
 *
 * Output files (written when output path given):
 *   <output>.bin        — raw 16-bit big-endian instruction words
 *   <output>.data.bin   — raw 16-bit big-endian data words (if any)
 */

#include "assembler/assembler.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f) { std::cerr << "Cannot open: " << path << "\n"; return false; }
    std::ostringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool writeWords(const std::string& path, const std::vector<uint16_t>& words) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { std::cerr << "Cannot write: " << path << "\n"; return false; }
    for (uint16_t w : words) {
        f.put(static_cast<char>(w >> 8));
        f.put(static_cast<char>(w & 0xFF));
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: assembler <source.asm> [output.bin] "
                     "[--listing] [--disasm] [--hex]\n";
        return 2;
    }

    std::string srcFile, outFile;
    bool doListing = false, doDisasm = false, doHex = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--listing") doListing = true;
        else if (a == "--disasm") doDisasm = true;
        else if (a == "--hex")   doHex    = true;
        else if (srcFile.empty()) srcFile = a;
        else if (outFile.empty()) outFile = a;
    }

    std::string source;
    if (!readFile(srcFile, source)) return 2;

    Assembler asmr;
    auto result = asmr.assemble(source);

    for (const auto& w : result.warnings) std::cerr << w << "\n";
    if (!result.success) {
        for (const auto& e : result.errors) std::cerr << e << "\n";
        std::cerr << "\nAssembly FAILED (" << result.errors.size() << " error(s))\n";
        return 1;
    }

    std::cout << "OK: " << result.instructions.size() << " instruction word(s)";
    if (!result.data.empty())
        std::cout << ", " << result.data.size() << " data word(s)";
    std::cout << "\n";

    if (doListing) std::cout << "\n" << result.listing << "\n";

    if (doHex) {
        std::cout << "\n[Text / Instruction memory]\n";
        for (size_t i = 0; i < result.instructions.size(); ++i) {
            if (i % 8 == 0)
                std::cout << "  " << std::hex << std::uppercase
                          << std::setw(4) << std::setfill('0') << i << ":  ";
            std::cout << std::setw(4) << std::setfill('0')
                      << result.instructions[i] << " ";
            if (i % 8 == 7) std::cout << "\n";
        }
        if (result.instructions.size() % 8 != 0) std::cout << "\n";

        if (!result.data.empty()) {
            std::cout << "\n[Data memory]\n";
            for (size_t i = 0; i < result.data.size(); ++i) {
                if (i % 8 == 0)
                    std::cout << "  " << std::hex << std::uppercase
                              << std::setw(4) << std::setfill('0') << i << ":  ";
                std::cout << std::setw(4) << std::setfill('0')
                          << result.data[i] << " ";
                if (i % 8 == 7) std::cout << "\n";
            }
            if (result.data.size() % 8 != 0) std::cout << "\n";
        }
        std::cout << std::dec << "\n";
    }

    if (doDisasm) {
        std::cout << "\n[Disassembly]\n";
        std::cout << Assembler::disassembleAll(result.instructions);
    }

   if (!outFile.empty()) {
    asmr.writeBinary(result, outFile);
    std::cout << "Written to " << outFile
              << " (" << result.instructions.size() << " instruction words, "
              << result.data.size() << " data words)\n";
    }

    return 0;
}
