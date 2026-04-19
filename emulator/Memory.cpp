#include "Memory.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

// ════════════════════════════════════════════════════════════════════════════
//  Helpers (file-local)
// ════════════════════════════════════════════════════════════════════════════

namespace {

constexpr std::size_t kMaxAddressableWords = 65536; // full 16-bit word space

void throwIfBankSizeInvalid(std::size_t instructionWordCount,
                            std::size_t dataWordCount) {
    if (instructionWordCount > kMaxAddressableWords ||
        dataWordCount > kMaxAddressableWords) {
        throw std::invalid_argument(
            "Memory: each bank may hold at most 65536 words "
            "(full 16-bit word address space)");
    }
}

bool isPowerOfTwo(std::size_t v) {
    return v > 0 && (v & (v - 1)) == 0;
}

// Undefined for v == 0; callers guarantee v >= 1 and power of 2.
std::size_t log2u(std::size_t v) {
    std::size_t r = 0;
    while (v >>= 1) ++r;
    return r;
}

} // namespace

// ════════════════════════════════════════════════════════════════════════════
//  CacheStats
// ════════════════════════════════════════════════════════════════════════════

double CacheStats::hitRate() const noexcept {
    const std::size_t total = hits + misses;
    if (total == 0) return 0.0;
    return static_cast<double>(hits) / static_cast<double>(total);
}

void CacheStats::reset() noexcept {
    reads = writes = hits = misses = 0;
}

// ════════════════════════════════════════════════════════════════════════════
//  Cache — construction and configuration validation
// ════════════════════════════════════════════════════════════════════════════

Cache::Cache(const CacheConfig& config,
             std::vector<std::uint16_t>& backingMem,
             std::size_t backingWordCount,
             std::string name)
    : config_(config)
    , name_(std::move(name))
    , backingMem_(&backingMem)
    , backingWordCount_(backingWordCount)
{
    // ── Validate configuration ──────────────────────────────────────
    if (config_.blockSize == 0 || !isPowerOfTwo(config_.blockSize))
        throw std::invalid_argument(
            name_ + ": blockSize must be a positive power of 2");
    if (config_.associativity == 0)
        throw std::invalid_argument(
            name_ + ": associativity must be >= 1");
    if (config_.totalWords == 0)
        throw std::invalid_argument(
            name_ + ": totalWords must be > 0");
    if (config_.totalWords % config_.blockSize != 0)
        throw std::invalid_argument(
            name_ + ": totalWords must be a multiple of blockSize");

    numLines_ = config_.totalWords / config_.blockSize;

    if (numLines_ % config_.associativity != 0)
        throw std::invalid_argument(
            name_ + ": numLines (totalWords/blockSize) must be a "
            "multiple of associativity");

    numSets_ = numLines_ / config_.associativity;

    if (!isPowerOfTwo(numSets_))
        throw std::invalid_argument(
            name_ + ": numSets must be a power of 2");

    offsetBits_ = log2u(config_.blockSize);
    indexBits_  = log2u(numSets_);
    tagBits_    = 16 - offsetBits_ - indexBits_;

    // Guard: the combined offset+index bits must fit in a 16-bit address.
    if (offsetBits_ + indexBits_ > 16)
        throw std::invalid_argument(
            name_ + ": cache geometry requires more than 16 address bits");

    offsetMask_ = static_cast<std::uint16_t>((1u << offsetBits_) - 1);
    indexMask_  = static_cast<std::uint16_t>((1u << indexBits_) - 1);

    // ── Allocate lines ──────────────────────────────────────────────
    lines_.resize(numLines_);
    for (auto& line : lines_) {
        line.data.assign(config_.blockSize, 0);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Cache — address decomposition
//
//  16-bit word address layout:
//
//      [ tag (tagBits) | index (indexBits) | offset (offsetBits) ]
//
//  offset : selects the word within a block
//  index  : selects the set
//  tag    : uniquely identifies the block within a set
// ════════════════════════════════════════════════════════════════════════════

std::uint16_t Cache::getTag(std::uint16_t address) const {
    return static_cast<std::uint16_t>(address >> (offsetBits_ + indexBits_));
}

std::size_t Cache::getIndex(std::uint16_t address) const {
    return (address >> offsetBits_) & indexMask_;
}

std::size_t Cache::getOffset(std::uint16_t address) const {
    return address & offsetMask_;
}

// Zero the offset bits to get the first word address of the containing block.
std::uint16_t Cache::blockBase(std::uint16_t address) const {
    return static_cast<std::uint16_t>(address & static_cast<uint16_t>(~offsetMask_));
}

// Inverse of decomposition: rebuild a block-aligned address from tag + set.
std::uint16_t Cache::reconstructBase(std::uint16_t tag, std::size_t setIdx) const {
    return static_cast<std::uint16_t>(
        (static_cast<std::size_t>(tag) << (indexBits_ + offsetBits_)) |
        (setIdx << offsetBits_));
}

// ════════════════════════════════════════════════════════════════════════════
//  Cache — block transfer between cache and backing store
// ════════════════════════════════════════════════════════════════════════════

void Cache::loadBlock(CacheLine& line, std::uint16_t baseAddr) {
    for (std::size_t i = 0; i < config_.blockSize; ++i) {
        const std::size_t addr = static_cast<std::size_t>(baseAddr) + i;
        line.data[i] = (addr < backingWordCount_) ? (*backingMem_)[addr] : 0;
    }
}

void Cache::writeBackBlock(const CacheLine& line, std::uint16_t baseAddr) {
    for (std::size_t i = 0; i < config_.blockSize; ++i) {
        const std::size_t addr = static_cast<std::size_t>(baseAddr) + i;
        if (addr < backingWordCount_) {
            (*backingMem_)[addr] = line.data[i];
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Cache — LRU replacement
//
//  Scan the set for the best victim:
//    1) Prefer any invalid (empty) slot — free fill, no writeback needed.
//    2) Among valid lines choose the one with the smallest lastAccess
//       (least recently used).
// ════════════════════════════════════════════════════════════════════════════

std::size_t Cache::findVictim(std::size_t setStart) const {
    // First pass: grab an invalid slot if one exists.
    for (std::size_t w = 0; w < config_.associativity; ++w) {
        if (!lines_[setStart + w].valid) {
            return setStart + w;
        }
    }

    // All valid — pick LRU.
    std::size_t victimIdx = setStart;
    std::size_t minAccess = lines_[setStart].lastAccess;
    for (std::size_t w = 1; w < config_.associativity; ++w) {
        if (lines_[setStart + w].lastAccess < minAccess) {
            minAccess = lines_[setStart + w].lastAccess;
            victimIdx = setStart + w;
        }
    }
    return victimIdx;
}

// ════════════════════════════════════════════════════════════════════════════
//  Cache — read
//
//  1. Decompose address → tag, set index, word offset.
//  2. Search the set for a valid line with a matching tag.
//     Hit  → return data[offset], update LRU.
//     Miss → pick victim (LRU), evict dirty line if needed, fetch block
//            from backing store, return data[offset].
// ════════════════════════════════════════════════════════════════════════════

std::uint16_t Cache::read(std::uint16_t address) {
    const std::uint16_t tag = getTag(address);
    const std::size_t   idx = getIndex(address);
    const std::size_t   off = getOffset(address);
    const std::size_t   setStart = idx * config_.associativity;

    ++stats_.reads;
    ++accessCounter_;

    // Search set for hit.
    for (std::size_t w = 0; w < config_.associativity; ++w) {
        CacheLine& line = lines_[setStart + w];
        if (line.valid && line.tag == tag) {
            ++stats_.hits;
            line.lastAccess = accessCounter_;
            return line.data[off];
        }
    }

    // Miss — fill from backing store.
    ++stats_.misses;

    const std::size_t victim = findVictim(setStart);
    CacheLine& vline = lines_[victim];

    if (vline.valid && vline.dirty) {
        writeBackBlock(vline, reconstructBase(vline.tag, idx));
    }

    const std::uint16_t base = blockBase(address);
    loadBlock(vline, base);
    vline.valid      = true;
    vline.dirty      = false;
    vline.tag        = tag;
    vline.lastAccess = accessCounter_;

    return vline.data[off];
}

// ════════════════════════════════════════════════════════════════════════════
//  Cache — write  (write-back, write-allocate)
//
//  Hit  → update word in cache line, mark dirty.
//  Miss → allocate: evict victim if dirty, fetch block, then update word.
// ════════════════════════════════════════════════════════════════════════════

void Cache::write(std::uint16_t address, std::uint16_t value) {
    const std::uint16_t tag = getTag(address);
    const std::size_t   idx = getIndex(address);
    const std::size_t   off = getOffset(address);
    const std::size_t   setStart = idx * config_.associativity;

    ++stats_.writes;
    ++accessCounter_;

    // Search set for hit.
    for (std::size_t w = 0; w < config_.associativity; ++w) {
        CacheLine& line = lines_[setStart + w];
        if (line.valid && line.tag == tag) {
            ++stats_.hits;
            line.data[off]  = value;
            line.dirty      = true;
            line.lastAccess = accessCounter_;
            return;
        }
    }

    // Miss — write-allocate: load block, then write.
    ++stats_.misses;

    const std::size_t victim = findVictim(setStart);
    CacheLine& vline = lines_[victim];

    if (vline.valid && vline.dirty) {
        writeBackBlock(vline, reconstructBase(vline.tag, idx));
    }

    const std::uint16_t base = blockBase(address);
    loadBlock(vline, base);
    vline.valid      = true;
    vline.tag        = tag;
    vline.lastAccess = accessCounter_;

    vline.data[off]  = value;
    vline.dirty      = true;
}

// ════════════════════════════════════════════════════════════════════════════
//  Cache — management
// ════════════════════════════════════════════════════════════════════════════

void Cache::reset() {
    for (auto& line : lines_) {
        line.valid      = false;
        line.dirty      = false;
        line.tag        = 0;
        std::fill(line.data.begin(), line.data.end(),
                  static_cast<std::uint16_t>(0));
        line.lastAccess = 0;
    }
    stats_.reset();
    accessCounter_ = 0;
}

void Cache::flush() {
    for (std::size_t s = 0; s < numSets_; ++s) {
        for (std::size_t w = 0; w < config_.associativity; ++w) {
            CacheLine& line = lines_[s * config_.associativity + w];
            if (line.valid && line.dirty) {
                writeBackBlock(line, reconstructBase(line.tag, s));
                line.dirty = false;
            }
        }
    }
}

void Cache::resetStats() noexcept { stats_.reset(); }

const CacheStats&  Cache::stats()  const noexcept { return stats_; }
const CacheConfig& Cache::config() const noexcept { return config_; }
const std::string& Cache::name()   const noexcept { return name_; }

std::size_t Cache::numSets()    const noexcept { return numSets_; }
std::size_t Cache::numLines()   const noexcept { return numLines_; }
std::size_t Cache::offsetBits() const noexcept { return offsetBits_; }
std::size_t Cache::indexBits()  const noexcept { return indexBits_; }
std::size_t Cache::tagBits()    const noexcept { return tagBits_; }

// ════════════════════════════════════════════════════════════════════════════
//  Cache — stats / dump
// ════════════════════════════════════════════════════════════════════════════

void Cache::printStats(std::ostream& out) const {
    out << name_ << " stats:\n"
        << "  Reads:    " << stats_.reads  << '\n'
        << "  Writes:   " << stats_.writes << '\n'
        << "  Hits:     " << stats_.hits   << '\n'
        << "  Misses:   " << stats_.misses << '\n'
        << "  Hit rate: ";
    if (stats_.hits + stats_.misses > 0) {
        out << std::fixed << std::setprecision(2)
            << (stats_.hitRate() * 100.0) << '%';
    } else {
        out << "N/A (no accesses)";
    }
    out << '\n';
}

void Cache::dump(std::ostream& out) const {
    const std::ios::fmtflags savedFlags = out.flags();

    out << "=== " << name_ << " ===\n"
        << "Config: " << std::dec
        << config_.totalWords << " words, "
        << config_.blockSize  << " words/block, ";

    if (config_.associativity == 1)
        out << "direct-mapped";
    else if (config_.associativity == numLines_)
        out << "fully associative";
    else
        out << config_.associativity << "-way set-associative";

    out << ", " << numSets_ << (numSets_ == 1 ? " set" : " sets") << '\n'
        << "Bits: tag=" << tagBits_
        << " index=" << indexBits_
        << " offset=" << offsetBits_ << '\n';

    printStats(out);
    out << '\n';

    for (std::size_t s = 0; s < numSets_; ++s) {
        out << "Set " << std::dec << s << ":\n";
        for (std::size_t w = 0; w < config_.associativity; ++w) {
            const CacheLine& line = lines_[s * config_.associativity + w];
            out << "  Way " << w << ": "
                << (line.valid ? "[V]" : "[ ]") << ' '
                << (line.dirty ? "[D]" : "[C]") << ' '
                << "tag=0x" << std::hex << std::uppercase
                << std::setw(4) << std::setfill('0')
                << static_cast<unsigned>(line.tag)
                << "  data={";
            for (std::size_t i = 0; i < line.data.size(); ++i) {
                if (i > 0) out << ' ';
                out << "0x" << std::setw(4) << std::setfill('0')
                    << static_cast<unsigned>(line.data[i]);
            }
            out << "}  lru=" << std::dec << line.lastAccess << '\n';
        }
    }

    out.flags(savedFlags);
}

// ════════════════════════════════════════════════════════════════════════════
//  Memory — error helpers (unchanged from original)
// ════════════════════════════════════════════════════════════════════════════

std::string Memory::formatOutOfRangeMessage(const char* memoryKind,
                                            const char* operation,
                                            std::uint16_t address,
                                            std::size_t wordCount) {
    std::ostringstream oss;
    oss << memoryKind << ' ' << operation
        << ": attempted word address 0x" << std::hex << std::uppercase
        << std::setw(4) << std::setfill('0')
        << static_cast<unsigned>(address);
    if (wordCount == 0) {
        oss << "; bank is empty (no valid addresses)";
    } else {
        const auto last = static_cast<std::uint16_t>(wordCount - 1);
        oss << "; valid word range [0x0000, 0x"
            << std::setw(4) << std::setfill('0') << std::uppercase
            << static_cast<unsigned>(last) << ']';
    }
    return oss.str();
}

void Memory::throwIfInstructionOutOfRange(const Memory& self,
                                          std::uint16_t address,
                                          const char* operation) {
    if (address >= self.instructionMem_.size()) {
        throw std::out_of_range(
            formatOutOfRangeMessage("instruction memory", operation,
                                    address, self.instructionMem_.size()));
    }
}

void Memory::throwIfDataOutOfRange(const Memory& self,
                                   std::uint16_t address,
                                   const char* operation) {
    if (address >= self.dataMem_.size()) {
        throw std::out_of_range(
            formatOutOfRangeMessage("data memory", operation,
                                    address, self.dataMem_.size()));
    }
}

void Memory::throwIfDumpOutOfRange(std::size_t bankSize,
                                   std::uint16_t startAddress,
                                   std::size_t length,
                                   const char* bankName) const {
    if (length == 0) return;

    const std::size_t start = startAddress;
    if (bankSize == 0) {
        throw std::out_of_range(
            formatOutOfRangeMessage(bankName, "dump", startAddress, 0));
    }
    if (start >= bankSize) {
        throw std::out_of_range(
            formatOutOfRangeMessage(bankName, "dump (start)",
                                    startAddress, bankSize));
    }
    if (length > bankSize - start) {
        std::ostringstream oss;
        oss << bankName << " dump: range [0x" << std::hex << std::uppercase
            << std::setw(4) << std::setfill('0')
            << static_cast<unsigned>(startAddress) << ", +"
            << std::dec << length
            << ") extends past end of bank; valid word range [0x0000, 0x"
            << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
            << static_cast<unsigned>(
                   static_cast<std::uint16_t>(bankSize - 1))
            << ']';
        throw std::out_of_range(oss.str());
    }
}

void Memory::requireCaches(const char* method) const {
    if (!iCache_ || !dCache_) {
        throw std::logic_error(
            std::string(method) +
            ": caches are not configured; use the Memory constructor "
            "that accepts CacheConfig parameters");
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Memory — constructors / destructor
// ════════════════════════════════════════════════════════════════════════════

Memory::Memory(std::size_t instructionWordCount, std::size_t dataWordCount) {
    throwIfBankSizeInvalid(instructionWordCount, dataWordCount);
    instructionMem_.assign(instructionWordCount, 0);
    dataMem_.assign(dataWordCount, 0);
}

Memory::Memory(std::size_t instructionWordCount, std::size_t dataWordCount,
               const CacheConfig& iCacheConfig, const CacheConfig& dCacheConfig) {
    throwIfBankSizeInvalid(instructionWordCount, dataWordCount);
    instructionMem_.assign(instructionWordCount, 0);
    dataMem_.assign(dataWordCount, 0);

    iCache_ = std::make_unique<Cache>(
        iCacheConfig, instructionMem_, instructionWordCount,
        "Instruction Cache");
    dCache_ = std::make_unique<Cache>(
        dCacheConfig, dataMem_, dataWordCount,
        "Data Cache");
}

Memory::~Memory() = default;

// ════════════════════════════════════════════════════════════════════════════
//  Memory — direct (uncached) access
// ════════════════════════════════════════════════════════════════════════════

void Memory::reset() {
    std::fill(instructionMem_.begin(), instructionMem_.end(),
              static_cast<std::uint16_t>(0));
    std::fill(dataMem_.begin(), dataMem_.end(),
              static_cast<std::uint16_t>(0));
    if (iCache_) iCache_->reset();
    if (dCache_) dCache_->reset();
}

std::uint16_t Memory::readInstruction(std::uint16_t address) const {
    throwIfInstructionOutOfRange(*this, address, "read");
    return instructionMem_[address];
}

void Memory::writeInstruction(std::uint16_t address, std::uint16_t value) {
    throwIfInstructionOutOfRange(*this, address, "write");
    instructionMem_[address] = value;
}

std::uint16_t Memory::readData(std::uint16_t address) const {
    throwIfDataOutOfRange(*this, address, "read");
    return dataMem_[address];
}

void Memory::writeData(std::uint16_t address, std::uint16_t value) {
    if (address == MMIO_OUT_CHAR) return;  // MMIO char — handled by control unit
    if (address == MMIO_OUT_INT)  return;  // MMIO int  — handled by control unit
    throwIfDataOutOfRange(*this, address, "write");
    dataMem_[address] = value;
}

void Memory::loadInstructionMemory(const std::vector<std::uint16_t>& words) {
    if (words.size() > instructionMem_.size()) {
        std::ostringstream oss;
        oss << "instruction memory load: " << std::dec << words.size()
            << " words do not fit in bank of "
            << instructionMem_.size() << " words";
        throw std::out_of_range(oss.str());
    }
    for (std::size_t i = 0; i < words.size(); ++i) {
        instructionMem_[i] = words[i];
    }
    if (iCache_) iCache_->reset();
}

void Memory::loadDataMemory(const std::vector<std::uint16_t>& words) {
    if (words.size() > dataMem_.size()) {
        std::ostringstream oss;
        oss << "data memory load: " << std::dec << words.size()
            << " words do not fit in bank of "
            << dataMem_.size() << " words";
        throw std::out_of_range(oss.str());
    }
    for (std::size_t i = 0; i < words.size(); ++i) {
        dataMem_[i] = words[i];
    }
    if (dCache_) dCache_->reset();
}

void Memory::dumpInstructionMemory(std::uint16_t startAddress,
                                   std::size_t length,
                                   std::ostream& out) const {
    throwIfDumpOutOfRange(instructionMem_.size(), startAddress,
                          length, "instruction memory");

    const std::ios::fmtflags old = out.flags();
    out << "Instruction memory dump [0x" << std::hex << std::uppercase
        << std::setw(4) << std::setfill('0')
        << static_cast<unsigned>(startAddress) << " .. 0x";
    if (length == 0) {
        out << std::setw(4) << static_cast<unsigned>(startAddress)
            << ") (empty)\n";
        out.flags(old);
        return;
    }
    const auto endAddr = static_cast<std::uint16_t>(
        static_cast<std::size_t>(startAddress) + length - 1);
    out << std::setw(4) << static_cast<unsigned>(endAddr) << "):\n";

    for (std::size_t i = 0; i < length; ++i) {
        const auto addr = static_cast<std::uint16_t>(startAddress + i);
        out << "  0x" << std::setw(4) << std::setfill('0')
            << static_cast<unsigned>(addr) << ": 0x"
            << std::setw(4) << static_cast<unsigned>(instructionMem_[addr])
            << '\n';
    }
    out.flags(old);
}

void Memory::dumpDataMemory(std::uint16_t startAddress,
                            std::size_t length,
                            std::ostream& out) const {
    throwIfDumpOutOfRange(dataMem_.size(), startAddress,
                          length, "data memory");

    const std::ios::fmtflags old = out.flags();
    out << "Data memory dump [0x" << std::hex << std::uppercase
        << std::setw(4) << std::setfill('0')
        << static_cast<unsigned>(startAddress) << " .. 0x";
    if (length == 0) {
        out << std::setw(4) << static_cast<unsigned>(startAddress)
            << ") (empty)\n";
        out.flags(old);
        return;
    }
    const auto endAddr = static_cast<std::uint16_t>(
        static_cast<std::size_t>(startAddress) + length - 1);
    out << std::setw(4) << static_cast<unsigned>(endAddr) << "):\n";

    for (std::size_t i = 0; i < length; ++i) {
        const auto addr = static_cast<std::uint16_t>(startAddress + i);
        out << "  0x" << std::setw(4) << std::setfill('0')
            << static_cast<unsigned>(addr) << ": 0x"
            << std::setw(4) << static_cast<unsigned>(dataMem_[addr])
            << '\n';
    }
    out.flags(old);
}

std::size_t Memory::instructionWordCount() const noexcept {
    return instructionMem_.size();
}

std::size_t Memory::dataWordCount() const noexcept {
    return dataMem_.size();
}

// ════════════════════════════════════════════════════════════════════════════
//  Memory — cached access
//
//  Instruction fetches go through the instruction cache.
//  Data loads/stores go through the data cache.
//  Bounds are checked before delegating to the cache so that the Cache
//  class itself never sees an invalid address.
// ════════════════════════════════════════════════════════════════════════════

std::uint16_t Memory::cachedReadInstruction(std::uint16_t address) {
    requireCaches("cachedReadInstruction");
    throwIfInstructionOutOfRange(*this, address, "cached read");
    return iCache_->read(address);
}

std::uint16_t Memory::cachedReadData(std::uint16_t address) {
    requireCaches("cachedReadData");
    throwIfDataOutOfRange(*this, address, "cached read");
    return dCache_->read(address);
}

void Memory::cachedWriteData(std::uint16_t address, std::uint16_t value) {
    requireCaches("cachedWriteData");
    throwIfDataOutOfRange(*this, address, "cached write");
    dCache_->write(address, value);
}

// ════════════════════════════════════════════════════════════════════════════
//  Memory — cache management
// ════════════════════════════════════════════════════════════════════════════

bool Memory::hasCaches() const noexcept {
    return iCache_ != nullptr && dCache_ != nullptr;
}

void Memory::resetCaches() {
    requireCaches("resetCaches");
    iCache_->reset();
    dCache_->reset();
}

void Memory::flushCaches() {
    requireCaches("flushCaches");
    iCache_->flush();
    dCache_->flush();
}

void Memory::resetCacheStats() {
    requireCaches("resetCacheStats");
    iCache_->resetStats();
    dCache_->resetStats();
}

// ════════════════════════════════════════════════════════════════════════════
//  Memory — cache statistics / debugging
// ════════════════════════════════════════════════════════════════════════════

const CacheStats& Memory::instructionCacheStats() const {
    requireCaches("instructionCacheStats");
    return iCache_->stats();
}

const CacheStats& Memory::dataCacheStats() const {
    requireCaches("dataCacheStats");
    return dCache_->stats();
}

void Memory::printCacheStats(std::ostream& out) const {
    requireCaches("printCacheStats");
    iCache_->printStats(out);
    out << '\n';
    dCache_->printStats(out);
}

void Memory::dumpInstructionCache(std::ostream& out) const {
    requireCaches("dumpInstructionCache");
    iCache_->dump(out);
}

void Memory::dumpDataCache(std::ostream& out) const {
    requireCaches("dumpDataCache");
    dCache_->dump(out);
}
