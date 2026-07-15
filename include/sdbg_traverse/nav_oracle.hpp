// nav_oracle.hpp — the navigation interface both traversal policies are written against.
//
// A NavOracle exposes a succinct de Bruijn graph as a *navigation-only* directed
// graph: nodes are dense integer ids in [0, N), out/in-degree are bounded by
// sigma = 4, and neighbours are produced on the fly. Nothing about the traversal
// engine depends on how the oracle is implemented, so the SAME se_bfs / se_dfs
// code runs on:
//   • InMemoryOracle   — dependency-free, for tests/validation (this file)
//   • CosmoOracle      — production BOSS backend (backends/cosmo_oracle.hpp)
//   • MegaHit SDBG     — a second backend behind the same seam
//
// Concept a NavOracle must model:
//   using node_t = uint64_t;
//   size_t N() const;                          // number of nodes
//   bool   valid(node_t v) const;              // false for dummy/sentinel nodes
//   int    outdeg(node_t v) const;             // O(1) out-degree  (<= 4)
//   int    indeg (node_t v) const;             // O(1) in-degree   (<= 4)
//   int    outgoing(node_t v, node_t out[4]) const;  // fills out[], returns outdeg
//   int    incoming(node_t v, node_t in [4]) const;  // fills in[],  returns indeg
//
// Neighbour order MUST be deterministic and stable (the DFS tree is defined
// relative to it). InMemoryOracle uses ascending target-id order; a BOSS backend
// uses ascending edge-symbol order.

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <cassert>

namespace sdbgt {

using node_t = uint64_t;
static constexpr int kSigma = 4;                 // DNA: |{A,C,G,T}|
static constexpr node_t kNoNode = ~node_t(0);

// A concrete, dependency-free oracle backed by explicit CSR adjacency.
// For tests and small validation graphs only — it stores real edge arrays.
class InMemoryOracle {
public:
    // edges: directed (from -> to). Degrees are asserted <= kSigma so the graph
    // is a legal de Bruijn-like graph (parent-symbol trails fit in 2 bits).
    InMemoryOracle(size_t n, const std::vector<std::pair<node_t, node_t>>& edges)
        : n_(n) {
        std::vector<std::vector<node_t>> out(n), in(n);
        for (auto& e : edges) {
            assert(e.first < n && e.second < n);
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
            assert(out[v].size() <= (size_t)kSigma && "out-degree exceeds sigma=4");
            assert(in[v].size()  <= (size_t)kSigma && "in-degree exceeds sigma=4");
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
    bool valid(node_t) const { return true; }

    int outdeg(node_t v) const { return int(out_off_[v + 1] - out_off_[v]); }
    int indeg(node_t v)  const { return int(in_off_[v + 1]  - in_off_[v]); }

    int outgoing(node_t v, node_t out[kSigma]) const {
        int d = 0;
        for (size_t i = out_off_[v]; i < out_off_[v + 1]; ++i) out[d++] = out_adj_[i];
        return d;
    }
    int incoming(node_t v, node_t in[kSigma]) const {
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
