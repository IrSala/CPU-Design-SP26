#pragma once

/**
 * Memory subsystem for a 16-bit Harvard-architecture CPU emulator.
 *
 * Design notes
 * ────────────
 * Harvard separation
 *   Instruction and data live in independent backing arrays.  Each bank has
 *   its own independent cache so the fetch path and load/store path never
 *   alias the same physical storage.
 *
 * Addressing
 *   Memory is WORD-ADDRESSABLE.  A 16-bit address selects one 16-bit word;
 *   there is no byte-level view.  Each bank may hold at most 65 536 words
 *   (the full 16-bit word address space).
 *
 * Cache layer (optional)
 *   A simulated set-associative cache can be placed in front of each bank.
 *   Caches are configured at construction time via CacheConfig (total
 *   capacity, block size, associativity).  Supported organisations:
 *     direct-mapped          associativity = 1
 *     N-way set-associative  associativity = N
 *     fully associative      associativity = numLines
 *
 *   Write policy  : write-back, write-allocate (data cache).
 *     Hit  → update cache line and mark dirty.
 *     Miss → fetch block from backing store into cache, then write to cache.
 *     Eviction of a dirty line → write block back to backing store.
 *
 *   Replacement   : LRU via a per-line monotonic access counter.
 *
 *   The instruction cache is read-only during normal execution.
 *   Program / data loading bypasses caches and resets the affected cache so
 *   that stale lines cannot be observed.
 *
 * Bounds checking
 *   Every access validates the address and throws std::out_of_range with
 *   the bank name, operation, attempted address, and valid range.
 *
 * Extensibility
 *   Memory-mapped I/O, ROM regions, access permissions, and separate loaders
 *   can be layered in without changing the public read/write API.
 */

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// ════════════════════════════════════════════════════════════════════════════
//  Cache helper types
// ════════════════════════════════════════════════════════════════════════════

/**
 * Configuration for one cache bank.
 *
 * Constraints enforced at Cache construction:
 *   - blockSize      >= 1, power of 2
 *   - associativity  >= 1
 *   - totalWords     >  0, multiple of blockSize
 *   - numLines       (= totalWords / blockSize) multiple of associativity
 *   - numSets        (= numLines / associativity) power of 2
 *   - offsetBits + indexBits <= 16   (must fit in a 16-bit word address)
 */
struct CacheConfig {
    std::size_t totalWords;     ///< total cache capacity in 16-bit words
    std::size_t blockSize;      ///< words per cache line / block  (power of 2)
    std::size_t associativity;  ///< ways per set
};

/** Cumulative cache access counters.  Tracked separately per cache bank. */
struct CacheStats {
    std::size_t reads  = 0;
    std::size_t writes = 0;
    std::size_t hits   = 0;
    std::size_t misses = 0;

    /** hits / (hits + misses).  Returns 0.0 when no accesses have occurred. */
    [[nodiscard]] double hitRate() const noexcept;
    void reset() noexcept;
};

/**
 * One cache line (block).
 *
 * Fields:
 *   valid      – line contains meaningful data
 *   dirty      – line has been written to but not yet flushed to backing store
 *   tag        – upper address bits that identify the block
 *   data       – blockSize words of cached content
 *   lastAccess – monotonic counter snapshot; lower = older (used for LRU)
 */
struct CacheLine {
    bool              valid      = false;
    bool              dirty      = false;
    std::uint16_t     tag        = 0;
    std::vector<std::uint16_t> data;
    std::size_t       lastAccess = 0;
};

/**
 * Simulated set-associative cache backed by a word-addressable memory vector.
 *
 * ── Address decomposition (16-bit word address) ──
 *
 *     MSB                                                    LSB
 *     ┌──────────────┬───────────────────┬────────────────────┐
 *     │  tag          │  index            │  offset            │
 *     │  (tagBits)    │  (indexBits)      │  (offsetBits)      │
 *     └──────────────┴───────────────────┴────────────────────┘
 *
 *   offset   = log2(blockSize)  bits – selects the word within a block
 *   index    = log2(numSets)    bits – selects the set
 *   tag      = 16 − offset − index   – uniquely identifies the block
 *
 * ── Write policy : write-back, write-allocate ──
 *   On hit  → update cache line, mark dirty.
 *   On miss → fetch block from backing store, write to fetched line, mark dirty.
 *   Eviction of a dirty line → write block back to backing store.
 *   flush() → write-back every dirty line, mark clean (lines stay valid).
 *
 * ── Replacement : LRU ──
 *   A monotonic access counter increments on every read/write.  Each line
 *   records the counter value at its most recent access.  On eviction the
 *   line with the smallest counter (least-recently-used) is chosen; invalid
 *   (empty) lines are preferred over any valid line.
 */
class Cache {
public:
    Cache(const CacheConfig& config,
          std::vector<std::uint16_t>& backingMem,
          std::size_t backingWordCount,
          std::string name);

    [[nodiscard]] std::uint16_t read(std::uint16_t address);
    void write(std::uint16_t address, std::uint16_t value);

    /** Invalidate every line, zero stats and access counter. */
    void reset();

    /** Write-back all dirty lines; lines remain valid but become clean. */
    void flush();

    void resetStats() noexcept;

    [[nodiscard]] const CacheStats&  stats()  const noexcept;
    [[nodiscard]] const CacheConfig& config() const noexcept;
    [[nodiscard]] const std::string& name()   const noexcept;

    [[nodiscard]] std::size_t numSets()    const noexcept;
    [[nodiscard]] std::size_t numLines()   const noexcept;
    [[nodiscard]] std::size_t offsetBits() const noexcept;
    [[nodiscard]] std::size_t indexBits()  const noexcept;
    [[nodiscard]] std::size_t tagBits()    const noexcept;

    void printStats(std::ostream& out = std::cout) const;
    void dump(std::ostream& out = std::cout) const;

private:
    CacheConfig   config_;
    std::string   name_;

    std::size_t   numLines_;
    std::size_t   numSets_;
    std::size_t   offsetBits_;
    std::size_t   indexBits_;
    std::size_t   tagBits_;
    std::uint16_t offsetMask_;          // (1 << offsetBits) - 1
    std::uint16_t indexMask_;           // (1 << indexBits)  - 1

    std::vector<CacheLine> lines_;      // layout: lines_[set * assoc + way]
    CacheStats  stats_;
    std::size_t accessCounter_ = 0;

    std::vector<std::uint16_t>* backingMem_;
    std::size_t backingWordCount_;

    // Address decomposition helpers
    [[nodiscard]] std::uint16_t getTag(std::uint16_t address)    const;
    [[nodiscard]] std::size_t   getIndex(std::uint16_t address)  const;
    [[nodiscard]] std::size_t   getOffset(std::uint16_t address) const;
    [[nodiscard]] std::uint16_t blockBase(std::uint16_t address) const;
    [[nodiscard]] std::uint16_t reconstructBase(std::uint16_t tag,
                                                std::size_t setIdx) const;

    void        loadBlock(CacheLine& line, std::uint16_t baseAddr);
    void        writeBackBlock(const CacheLine& line, std::uint16_t baseAddr);
    std::size_t findVictim(std::size_t setStart) const;
};

// ════════════════════════════════════════════════════════════════════════════
//  Memory
// ════════════════════════════════════════════════════════════════════════════

class Memory {
public:
    /** Construct backing memory only (no caches). */
    Memory(std::size_t instructionWordCount, std::size_t dataWordCount);

    /**
     * Construct with backing memory and separate instruction / data caches.
     * @throws std::invalid_argument on invalid bank sizes or cache configs.
     */
    Memory(std::size_t instructionWordCount, std::size_t dataWordCount,
           const CacheConfig& iCacheConfig, const CacheConfig& dCacheConfig);

    ~Memory();

    // Caches hold pointers to internal vectors — no copy / move.
    Memory(const Memory&)            = delete;
    Memory& operator=(const Memory&) = delete;
    Memory(Memory&&)                 = delete;
    Memory& operator=(Memory&&)      = delete;

    // ── Direct (uncached) access ───────────────────────────────────────
    //    Bypass caches.  Useful for debugging, memory dumps, and loading.

    /** Zero-fill both banks.  Resets caches if present. */
    void reset();

    [[nodiscard]] std::uint16_t readInstruction(std::uint16_t address) const;
    void writeInstruction(std::uint16_t address, std::uint16_t value);

    [[nodiscard]] std::uint16_t readData(std::uint16_t address) const;
    void writeData(std::uint16_t address, std::uint16_t value);

    /** Load words into instruction memory at address 0.  Resets iCache. */
    void loadInstructionMemory(const std::vector<std::uint16_t>& words);

    /** Load words into data memory at address 0.  Resets dCache. */
    void loadDataMemory(const std::vector<std::uint16_t>& words);

    void dumpInstructionMemory(std::uint16_t startAddress, std::size_t length,
                               std::ostream& out = std::cout) const;
    void dumpDataMemory(std::uint16_t startAddress, std::size_t length,
                        std::ostream& out = std::cout) const;

    [[nodiscard]] std::size_t instructionWordCount() const noexcept;
    [[nodiscard]] std::size_t dataWordCount()        const noexcept;

    // ── Cached access ──────────────────────────────────────────────────
    //    Route through the cache layer.  Non-const because a read may
    //    trigger a cache-line fill (modifying cache state).
    //    Throws std::logic_error if no caches are configured.

    [[nodiscard]] std::uint16_t cachedReadInstruction(std::uint16_t address);
    [[nodiscard]] std::uint16_t cachedReadData(std::uint16_t address);
    void cachedWriteData(std::uint16_t address, std::uint16_t value);

    // ── Cache management ───────────────────────────────────────────────

    /** True if caches were configured at construction time. */
    [[nodiscard]] bool hasCaches() const noexcept;

    /** Invalidate all cache lines and reset stats. */
    void resetCaches();

    /** Flush (write-back) all dirty cache lines to backing memory. */
    void flushCaches();

    /** Reset cache hit/miss/read/write counters to zero. */
    void resetCacheStats();

    // ── Cache statistics / debugging ───────────────────────────────────

    [[nodiscard]] const CacheStats& instructionCacheStats() const;
    [[nodiscard]] const CacheStats& dataCacheStats()        const;
    void printCacheStats(std::ostream& out = std::cout)     const;

    void dumpInstructionCache(std::ostream& out = std::cout) const;
    void dumpDataCache(std::ostream& out = std::cout)        const;

private:
    std::vector<std::uint16_t> instructionMem_;
    std::vector<std::uint16_t> dataMem_;
    std::unique_ptr<Cache>     iCache_;
    std::unique_ptr<Cache>     dCache_;

    static void throwIfInstructionOutOfRange(const Memory& self,
                                             std::uint16_t address,
                                             const char* operation);
    static void throwIfDataOutOfRange(const Memory& self,
                                      std::uint16_t address,
                                      const char* operation);
    void throwIfDumpOutOfRange(std::size_t bankSize,
                               std::uint16_t startAddress,
                               std::size_t length,
                               const char* bankName) const;
    static std::string formatOutOfRangeMessage(const char* memoryKind,
                                               const char* operation,
                                               std::uint16_t address,
                                               std::size_t wordCount);
    void requireCaches(const char* method) const;
};
