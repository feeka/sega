// choice_dictionary.hpp — the shared O(n)-bit state for both traversals.
//
// A colored choice dictionary over n cells with up to 4 colors, stored at
// 2 bits/cell (n/4 bytes). Color 0 is the DEFAULT/empty color: an all-zero
// 64-bit word therefore contains only default cells and is skipped by
// iterate()/findany(), so those scans cost O(#non-default words), not O(n).
//
// This is the pragmatic v1 (plan §4.2): correct, cache-friendly, 2 bits/cell.
// It is NOT yet the asymptotically-optimal Kammer–Sajenko structure whose
// findany/iterate are worst-case O(1)/O(#set); swapping that in later is a
// drop-in behind this same interface (a benchmark axis, not a rewrite).
//
// The 2-bit budget is the whole point: it replaces a 32-bit BFS distance array
// and an O(depth·log n)-bit DFS node stack.

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cassert>

namespace sdbgt {

class ChoiceDictionary {
public:
    static constexpr int    kCellsPerWord = 32;     // 64 bits / 2 bits
    static constexpr size_t kNPos = ~size_t(0);

    explicit ChoiceDictionary(size_t n)
        : n_(n), words_((n + kCellsPerWord - 1) / kCellsPerWord, 0) {}

    size_t size() const { return n_; }

    // bits/cell actually resident (the number we report in the space Pareto).
    static constexpr double bits_per_cell() { return 2.0; }
    size_t resident_bytes() const { return words_.size() * sizeof(uint64_t); }

    uint8_t get(size_t v) const {
        return uint8_t((words_[v >> 5] >> ((v & 31) << 1)) & 3ULL);
    }

    void set(size_t v, uint8_t color) {
        assert(color < 4);
        uint64_t& w = words_[v >> 5];
        const unsigned s = unsigned((v & 31) << 1);
        w = (w & ~(3ULL << s)) | (uint64_t(color) << s);
    }

    void clear_all() { std::fill(words_.begin(), words_.end(), 0ULL); }

    // Return some cell with the given (non-default) color, or kNPos.
    size_t findany(uint8_t color) const {
        assert(color != 0 && color < 4);
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
    // the same word (both BFS layer promotion and DFS rely on this).
    template <class Fn>
    void iterate(uint8_t color, Fn&& fn) const {
        assert(color != 0 && color < 4);
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

// A dense 2-bit array (0..3) used for the DFS parent-symbol trail: parent_sym[v]
// = index of v's DFS parent within v's in-neighbour list. 2 bits/node, since
// in-degree <= sigma = 4. Distinct from ChoiceDictionary only in intent.
class TwoBitArray {
public:
    explicit TwoBitArray(size_t n)
        : n_(n), words_((n + 31) / 32, 0) {}
    uint8_t get(size_t v) const {
        return uint8_t((words_[v >> 5] >> ((v & 31) << 1)) & 3ULL);
    }
    void set(size_t v, uint8_t val) {
        uint64_t& w = words_[v >> 5];
        const unsigned s = unsigned((v & 31) << 1);
        w = (w & ~(3ULL << s)) | (uint64_t(val & 3) << s);
    }
    size_t resident_bytes() const { return words_.size() * sizeof(uint64_t); }

private:
    size_t n_;
    std::vector<uint64_t> words_;
};

} // namespace sdbgt
