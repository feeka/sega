// Packed two-bit state arrays. ChoiceDictionary is a historical API name:
// findany()/iterate() scan all packed words in the worst case, O(N), even when
// most words are zero. This is not a constant-time succinct choice dictionary.
// Traversals use only O(1) get/set; they do not use these scanning operations.

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cassert>
#include <algorithm>
#include <stdexcept>

namespace sdbgt {

class ChoiceDictionary {
public:
    static constexpr int    kCellsPerWord = 32;     // 64 bits / 2 bits
    static constexpr size_t kNPos = ~size_t(0);

    explicit ChoiceDictionary(size_t n)
        : n_(n), words_(n / kCellsPerWord + (n % kCellsPerWord != 0), 0) {}

    size_t size() const { return n_; }

    // Nominal bits/cell; resident_bytes reports allocated payload, not OS RSS.
    static constexpr double bits_per_cell() { return 2.0; }
    size_t resident_bytes() const { return words_.capacity() * sizeof(uint64_t); }

    uint8_t get(size_t v) const {
        if (v >= n_) throw std::out_of_range("packed-state index out of range");
        return uint8_t((words_[v >> 5] >> ((v & 31) << 1)) & 3ULL);
    }

    void set(size_t v, uint8_t color) {
        if (v >= n_ || color >= 4) throw std::out_of_range("invalid packed-state index/color");
        uint64_t& w = words_[v >> 5];
        const unsigned s = unsigned((v & 31) << 1);
        w = (w & ~(3ULL << s)) | (uint64_t(color) << s);
    }

    void clear_all() { std::fill(words_.begin(), words_.end(), 0ULL); }

    // Return some cell with the given (non-default) color, or kNPos.
    size_t findany(uint8_t color) const {
        if (color == 0 || color >= 4) throw std::invalid_argument("scan color must be in [1,3]");
        for (size_t wi = 0; wi < words_.size(); ++wi) {
            uint64_t x = words_[wi];
            if (x == 0) continue;
            for (int c = 0; c < kCellsPerWord; ++c) {
                size_t idx = (wi << 5) + c;
                if (idx >= n_) break;
                if (((x >> (c << 1)) & 3ULL) == color) return idx;
            }
        }
        return kNPos;
    }

    // Call fn(idx) for every cell currently holding `color`. A snapshot of each
    // word is taken before any callback runs, so fn may safely re-color cells in
    // the same word. Re-coloring future words affects their later snapshots.
    template <class Fn>
    void iterate(uint8_t color, Fn&& fn) const {
        if (color == 0 || color >= 4) throw std::invalid_argument("scan color must be in [1,3]");
        for (size_t wi = 0; wi < words_.size(); ++wi) {
            uint64_t x = words_[wi];           // snapshot
            if (x == 0) continue;
            const size_t base = wi << 5;
            for (int c = 0; c < kCellsPerWord; ++c) {
                size_t idx = base + c;
                if (idx >= n_) break;
                if (((x >> (c << 1)) & 3ULL) == color) fn(idx);
            }
        }
    }

    size_t count(uint8_t color) const {
        size_t total = 0;
        iterate(color, [&](size_t) { ++total; });
        return total;
    }

private:
    size_t n_;
    std::vector<uint64_t> words_;
};

// A dense 2-bit array (0..3) used for the DFS predecessor-index trail: parent_sym[v]
// = index of v's DFS parent within v's in-neighbour list. 2 bits/node, since
// in-degree <= sigma = 4. Distinct from ChoiceDictionary only in intent.
class TwoBitArray {
public:
    explicit TwoBitArray(size_t n)
        : n_(n), words_(n / 32 + (n % 32 != 0), 0) {}
    uint8_t get(size_t v) const {
        if (v >= n_) throw std::out_of_range("packed-state index out of range");
        return uint8_t((words_[v >> 5] >> ((v & 31) << 1)) & 3ULL);
    }
    size_t size() const { return n_; }
    void set(size_t v, uint8_t val) {
        if (v >= n_ || val >= 4) throw std::out_of_range("invalid packed-state index/value");
        uint64_t& w = words_[v >> 5];
        const unsigned s = unsigned((v & 31) << 1);
        w = (w & ~(3ULL << s)) | (uint64_t(val & 3) << s);
    }
    size_t resident_bytes() const { return words_.capacity() * sizeof(uint64_t); }

private:
    size_t n_;
    std::vector<uint64_t> words_;
};

} // namespace sdbgt
