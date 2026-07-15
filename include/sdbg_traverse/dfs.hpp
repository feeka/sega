// dfs.hpp — space-efficient DFS via Elmasry–Hagerup–Kammer stack reconstruction
// (plan idea #1).
//
// The textbook iterative DFS stores an explicit node-id stack of size O(depth),
// i.e. O(depth · log n) bits. EHK instead re-derives the search path from
// REVERSE adjacency: it keeps only
//   • a visited choice dictionary (2 bits/node), and
//   • a parent-symbol trail parent_sym[v] = index of v's DFS parent within v's
//     in-neighbour list (2 bits/node, since in-degree <= sigma = 4).
// On backtrack, the parent is recovered as incoming(v)[parent_sym[v]] — the
// backward-navigation of the succinct dBG IS the reverse-adjacency oracle EHK
// assumes. No node-id stack is ever materialised.
//
// "First unexplored child" is recomputed by re-scanning out-neighbours in order
// and taking the first WHITE one: once a child is colored it stays non-WHITE, so
// re-scanning (cost O(outdeg) <= 4) always makes progress. Neighbour order must
// be deterministic (see NavOracle) so the produced DFS tree is well defined and
// matches a reference DFS using the same order.
//
// Colors: 0=WHITE (undiscovered), 1=ACTIVE (on the implicit stack), 2=DONE.

#pragma once
#include "sdbg_traverse/nav_oracle.hpp"
#include "sdbg_traverse/choice_dictionary.hpp"
#include <cstdint>

namespace sdbgt {
namespace dcolor { enum : uint8_t { WHITE = 0, ACTIVE = 1, DONE = 2 }; }

struct DfsStats {
    uint64_t discovered = 0, finished = 0;
    uint64_t child_scans = 0;      // out-neighbour probes (the forward work)
    uint64_t parent_recoveries = 0;// backward reconstructions (the EHK "tax")
    uint64_t recover_probes = 0;   // in-neighbour probes during reconstruction
};

// index of `parent` within v's in-neighbour list (deterministic order).
template <class Oracle>
static inline uint8_t parent_index(const Oracle& g, node_t v, node_t parent,
                                   uint64_t& probes) {
    node_t in[kSigma];
    int d = g.incoming(v, in);
    for (int i = 0; i < d; ++i) { ++probes; if (in[i] == parent) return uint8_t(i); }
    return 0; // unreachable for a legal discovery edge
}

// Single-source space-efficient DFS from `src`.
//   on_discover(v, parent)  — parent == kNoNode for the root
//   on_finish(v)
// visited and parent_sym are caller-owned (sized to g.N()); this lets a forest
// driver reuse them across roots.
template <class Oracle, class OnDiscover, class OnFinish>
DfsStats se_dfs(const Oracle& g, node_t src,
                ChoiceDictionary& visited, TwoBitArray& parent_sym,
                OnDiscover&& on_discover, OnFinish&& on_finish) {
    DfsStats st;
    node_t nb[kSigma];

    node_t cur = src;
    visited.set(cur, dcolor::ACTIVE);
    on_discover(cur, kNoNode);
    ++st.discovered;

    for (;;) {
        // find first WHITE out-neighbour of cur
        int d = g.outgoing(cur, nb);
        node_t child = kNoNode;
        for (int i = 0; i < d; ++i) {
            ++st.child_scans;
            if (g.valid(nb[i]) && visited.get(nb[i]) == dcolor::WHITE) { child = nb[i]; break; }
        }

        if (child != kNoNode) {
            // descend
            parent_sym.set(child, parent_index(g, child, cur, st.recover_probes));
            visited.set(child, dcolor::ACTIVE);
            on_discover(child, cur);
            ++st.discovered;
            cur = child;
        } else {
            // finish cur, then climb to its reconstructed parent
            visited.set(cur, dcolor::DONE);
            on_finish(cur);
            ++st.finished;
            if (cur == src) break;
            node_t in[kSigma];
            int di = g.incoming(cur, in);
            (void)di;
            uint8_t pidx = parent_sym.get(cur);
            ++st.parent_recoveries;
            ++st.recover_probes;
            cur = in[pidx];               // EHK reconstruction: parent from reverse adjacency
        }
    }
    return st;
}

// Baseline A: naive explicit node-id stack DFS  (Theta(depth · log n) bits).
template <class Oracle, class OnDiscover, class OnFinish>
DfsStats baseline_dfs_explicit(const Oracle& g, node_t src,
                               std::vector<uint8_t>& state,
                               OnDiscover&& on_discover, OnFinish&& on_finish) {
    DfsStats st;
    node_t nb[kSigma];
    struct Frame { node_t v; };
    std::vector<node_t> stack;             // the O(depth·log n)-bit structure
    stack.reserve(1024);

    state[src] = dcolor::ACTIVE;
    on_discover(src, kNoNode);
    ++st.discovered;
    stack.push_back(src);

    while (!stack.empty()) {
        node_t cur = stack.back();
        int d = g.outgoing(cur, nb);
        node_t child = kNoNode;
        for (int i = 0; i < d; ++i) {
            ++st.child_scans;
            if (g.valid(nb[i]) && state[nb[i]] == dcolor::WHITE) { child = nb[i]; break; }
        }
        if (child != kNoNode) {
            state[child] = dcolor::ACTIVE;
            on_discover(child, cur);
            ++st.discovered;
            stack.push_back(child);
        } else {
            state[cur] = dcolor::DONE;
            on_finish(cur);
            ++st.finished;
            stack.pop_back();
        }
    }
    return st;
}

} // namespace sdbgt
