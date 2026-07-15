// bfs.hpp — space-efficient BFS.
//
// se_bfs: a 1-bit visited bitmap + explicit frontier lists + STREAMED distances.
// Linear O(N+M) time. It avoids the textbook 32-bit distance array (a 1-bit
// visited marker + streamed distances instead); the frontier lists hold O(max
// level width) node ids, which stays small on near-linear (genomic) graphs.
//
// An earlier version reconstructed each frontier by scanning a 2-bit choice
// dictionary -> O(N*depth), pathological on high-diameter graphs (58 s on a
// CRISPR chain). This explicit-frontier version is linear. The strict O(n)-bit
// variant would swap the frontier lists for a Kammer-Sajenko choice dictionary
// (O(1) iterate) — not needed for the narrow-frontier genomic case.

#pragma once
#include "sdbg_traverse/nav_oracle.hpp"
#include "sdbg_traverse/choice_dictionary.hpp"
#include <cstdint>
#include <queue>
#include <vector>

namespace sdbgt {
struct BfsStats { uint64_t visited = 0, edges_scanned = 0, rounds = 0, max_frontier = 0; };

// Single-source space-efficient BFS. emit(node_t v, uint64_t dist) is called
// exactly once per reachable node, in nondecreasing distance order.
// Aux: a 1-bit visited bitmap (N/8 bytes) + two frontier vectors (O(level width)).
template <class Oracle, class Emit>
BfsStats se_bfs(const Oracle& g, node_t src, Emit&& emit) {
    BfsStats st;
    const size_t N = g.N();
    std::vector<uint64_t> vis((N + 63) / 64, 0);          // 1 bit/node visited
    auto seen = [&](node_t v) { return (vis[v >> 6] >> (v & 63)) & 1ULL; };
    auto mark = [&](node_t v) { vis[v >> 6] |= 1ULL << (v & 63); };

    std::vector<node_t> cur, nxt;
    mark(src);
    cur.push_back(src);
    uint64_t level = 0;
    node_t nb[kSigma];
    while (!cur.empty()) {
        if (cur.size() > st.max_frontier) st.max_frontier = cur.size();
        for (node_t v : cur) {
            emit(v, level);
            ++st.visited;
            int d = g.outgoing(v, nb);
            st.edges_scanned += d;
            for (int i = 0; i < d; ++i) {
                node_t w = nb[i];
                if (g.valid(w) && !seen(w)) { mark(w); nxt.push_back(w); }
            }
        }
        cur.swap(nxt);
        nxt.clear();
        ++level;
        ++st.rounds;
    }
    return st;
}

// Textbook baseline: std::queue + 32-bit distance array (Theta(n log n) bits).
// Used only to validate se_bfs and to anchor the space-time Pareto.
template <class Oracle, class Emit>
BfsStats baseline_bfs(const Oracle& g, node_t src, Emit&& emit) {
    BfsStats st;
    std::vector<uint32_t> dist(g.N(), UINT32_MAX);   // 32 bits/node — the aux we beat
    std::queue<node_t> q;                            // FIFO: holds O(frontier), not O(n)
    dist[src] = 0;
    q.push(src);
    node_t nb[kSigma];
    while (!q.empty()) {
        node_t v = q.front();
        q.pop();
        emit(v, uint64_t(dist[v]));
        ++st.visited;
        int d = g.outgoing(v, nb);
        st.edges_scanned += d;
        for (int i = 0; i < d; ++i) {
            node_t w = nb[i];
            if (g.valid(w) && dist[w] == UINT32_MAX) {
                dist[w] = dist[v] + 1;
                q.push(w);
            }
        }
    }
    return st;
}

} // namespace sdbgt
