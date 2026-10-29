// nav_oracle.hpp — the navigation interface both traversal policies are written against.
//
// A NavOracle exposes a succinct de Bruijn graph as a *navigation-only* directed
// graph: the identifier universe is [0, N) (some IDs may be invalid),
// in/out-degrees are bounded by sigma = 4, and neighbours are produced on the fly. Nothing about the traversal
// engine depends on how the oracle is implemented, so the SAME se_bfs / se_dfs
// code runs on:
//   • InMemoryOracle   — dependency-free, for tests/validation (this file)
//   • MegaHitOracle    — MEGAHIT edge-to-edge adjacency (backends/megahit_oracle.hpp)
//
// Concept a NavOracle must model:
//   using node_t = uint64_t;
//   size_t N() const;                          // size of the identifier universe
//   bool   valid(node_t v) const;              // false for dummy/sentinel nodes
//   int    outdeg(node_t v) const;             // O(1) out-degree  (<= 4)
//   int    indeg (node_t v) const;             // in-degree (<= 4), cost backend-dependent
//   int    outgoing(node_t v, node_t out[4]) const;  // fills out[], returns outdeg
//   int    incoming(node_t v, node_t in [4]) const;  // fills in[],  returns indeg
//
// Neighbour order MUST be deterministic and stable (the DFS tree is defined
// relative to it). InMemoryOracle uses ascending target-id order; a BOSS backend
// has its own stable deterministic order; no symbol uniqueness is assumed.

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <cassert>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace sdbgt {

namespace detail {
// Requested live heap bytes, including vector reallocation overlap and deque
// map/block allocations. Allocator metadata, alignment overhead, stack objects,
// and graph storage are excluded; these are not resident-set measurements.
struct HeapCounter { size_t current = 0, peak = 0; };
template<class T> struct CountingAllocator {
    using value_type = T;
    using propagate_on_container_swap = std::true_type;
    HeapCounter* counter = nullptr;
    CountingAllocator() = default;
    explicit CountingAllocator(HeapCounter& c) noexcept : counter(&c) {}
    template<class U> CountingAllocator(const CountingAllocator<U>& a) noexcept : counter(a.counter) {}
    T* allocate(size_t n) {
        T* p = std::allocator<T>{}.allocate(n);
        if (counter) { counter->current += n * sizeof(T); counter->peak = std::max(counter->peak, counter->current); }
        return p;
    }
    void deallocate(T* p, size_t n) noexcept {
        if (counter) counter->current -= n * sizeof(T);
        std::allocator<T>{}.deallocate(p, n);
    }
    template<class U> bool operator==(const CountingAllocator<U>& a) const noexcept { return counter == a.counter; }
    template<class U> bool operator!=(const CountingAllocator<U>& a) const noexcept { return !(*this == a); }
};
template<class Index, class Oracle> void require_index_range(const Oracle& g) {
    static_assert(std::is_integral<Index>::value && std::is_unsigned<Index>::value, "Index must be an unsigned integer");
    if (g.N() && g.N() - 1 > std::numeric_limits<Index>::max())
        throw std::overflow_error("identifier universe does not fit requested index width");
}
inline int checked_degree(int degree) {
    if (degree < 0 || degree > 4) throw std::runtime_error("oracle degree outside [0,4]");
    return degree;
}
} // namespace detail

using node_t = uint64_t;
static constexpr int kSigma = 4;                 // DNA: |{A,C,G,T}|
static constexpr node_t kNoNode = ~node_t(0);

namespace detail {
template<class Oracle> void require_source(const Oracle& g, node_t src, bool forest) {
    if (!forest && (src >= g.N() || !g.valid(src)))
        throw std::invalid_argument("source is outside the valid identifier set");
}
template<class Oracle> bool valid_neighbor(const Oracle& g, node_t v) {
    if (v >= g.N()) throw std::out_of_range("oracle returned neighbor outside identifier universe");
    return g.valid(v);
}
} // namespace detail

// A concrete, dependency-free oracle backed by explicit CSR adjacency.
// For tests and small validation graphs only — it stores real edge arrays.
class InMemoryOracle {
public:
    // Edges are directed (from -> to). Degrees are checked <= kSigma; this
    // models bounded-degree adjacency, not necessarily a realizable dBG.
    InMemoryOracle(size_t n, const std::vector<std::pair<node_t, node_t>>& edges)
        : n_(n) {
        std::vector<std::vector<node_t>> out(n), in(n);
        for (auto& e : edges) {
            if (e.first >= n || e.second >= n) throw std::out_of_range("edge endpoint outside identifier universe");
            out[e.first].push_back(e.second);
            in[e.second].push_back(e.first);
        }
        out_off_.assign(n + 1, 0);
        in_off_.assign(n + 1, 0);
        for (size_t v = 0; v < n; ++v) {
            std::sort(out[v].begin(), out[v].end());
            out[v].erase(std::unique(out[v].begin(), out[v].end()), out[v].end());
            std::sort(in[v].begin(), in[v].end());
            in[v].erase(std::unique(in[v].begin(), in[v].end()), in[v].end());
            if (out[v].size() > size_t(kSigma) || in[v].size() > size_t(kSigma))
                throw std::invalid_argument("degree exceeds alphabet bound 4");
            out_off_[v + 1] = out_off_[v] + out[v].size();
            in_off_[v + 1]  = in_off_[v]  + in[v].size();
        }
        out_adj_.reserve(out_off_[n]);
        in_adj_.reserve(in_off_[n]);
        for (size_t v = 0; v < n; ++v) {
            for (node_t w : out[v]) out_adj_.push_back(w);
            for (node_t w : in[v])  in_adj_.push_back(w);
        }
    }

    size_t N() const { return n_; }
    bool valid(node_t v) const { return v < n_; }

    int outdeg(node_t v) const { return valid(v) ? int(out_off_[v + 1] - out_off_[v]) : 0; }
    int indeg(node_t v)  const { return valid(v) ? int(in_off_[v + 1]  - in_off_[v]) : 0; }

    int outgoing(node_t v, node_t out[kSigma]) const {
        if (!valid(v)) return 0;
        int d = 0;
        for (size_t i = out_off_[v]; i < out_off_[v + 1]; ++i) out[d++] = out_adj_[i];
        return d;
    }
    int incoming(node_t v, node_t in[kSigma]) const {
        if (!valid(v)) return 0;
        int d = 0;
        for (size_t i = in_off_[v]; i < in_off_[v + 1]; ++i) in[d++] = in_adj_[i];
        return d;
    }

private:
    size_t n_;
    std::vector<size_t> out_off_, in_off_;
    std::vector<node_t> out_adj_, in_adj_;
};

} // namespace sdbgt
