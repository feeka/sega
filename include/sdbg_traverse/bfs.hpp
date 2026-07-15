// bfs.hpp — space-efficient BFS (plan idea #3).
//
// Replaces the textbook std::queue + Theta(n log n)-bit distance array with a
// 2-bit/cell colored choice dictionary. Distances are STREAMED (emitted in
// wavefront order) rather than stored for random access — hence "distance
// stream", not "distance oracle". Working memory beyond the graph is one
// ChoiceDictionary (~2 bits/node).
//
// Colors: 0=WHITE (unvisited), 1=CUR (current wavefront), 2=NEXT (next
// wavefront), 3=DONE (settled). Each round:
//   iterate(CUR): emit (v, dist); push WHITE out-neighbours to NEXT; settle v.
//   iterate(NEXT): promote to CUR.
// Terminates when NEXT is empty.

#pragma once
#include "sdbg_traverse/nav_oracle.hpp"
#include "sdbg_traverse/choice_dictionary.hpp"
#include <cstdint>
#include <queue>
#include <vector>

namespace sdbgt {
namespace color { enum : uint8_t { WHITE = 0, CUR = 1, NEXT = 2, DONE = 3 }; }

struct BfsStats { uint64_t visited = 0, edges_scanned = 0, rounds = 0; };

// Single-source space-efficient BFS. emit(node_t v, uint64_t dist) is called
// exactly once per reachable node, in nondecreasing distance order.
template <class Oracle, class Emit>
BfsStats se_bfs(const Oracle& g, node_t src, ChoiceDictionary& cd, Emit&& emit) {
    BfsStats st;
    cd.set(src, color::CUR);
    uint64_t dist = 0;
    node_t nb[kSigma];

    for (;;) {
        // ---- process current wavefront ----
        cd.iterate(color::CUR, [&](size_t v) {
            emit(node_t(v), dist);
            ++st.visited;
            int d = g.outgoing(node_t(v), nb);
            st.edges_scanned += d;
            for (int i = 0; i < d; ++i) {
                node_t w = nb[i];
                if (g.valid(w) && cd.get(w) == color::WHITE) cd.set(w, color::NEXT);
            }
            cd.set(v, color::DONE);
        });
        ++st.rounds;

        // ---- promote NEXT -> CUR; stop if empty ----
        bool any = false;
        cd.iterate(color::NEXT, [&](size_t v) { cd.set(v, color::CUR); any = true; });
        if (!any) break;
        ++dist;
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
