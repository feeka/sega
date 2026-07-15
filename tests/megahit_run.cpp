// megahit_run.cpp — REAL run + validation on a MEGAHIT SDBG.
//
// Loads an actual succinct de Bruijn graph, then:
//   BFS: se_bfs vs baseline_bfs on the real graph (distances must match), plus
//        an independent cross-check vs an InMemoryOracle rebuilt from the same
//        edges (BFS distances are neighbour-order-independent).
//   DFS: se_dfs (EHK reconstruction) vs baseline_dfs_explicit on the real graph,
//        SAME oracle => same neighbour order => discovery/finish/parent must match.
// Also prints the reconstruction tax and the aux-state bits/node on THIS graph.
//
// Build: `cmake -B build && cmake --build build` (megahit vendored at libs/megahit),
// then ./build/megahit_run <graph_prefix>. Manual, from repo root:
//   MH=libs/megahit/src
//   g++ -std=c++17 -O2 -march=native -pthread -DSDBGT_WITH_MEGAHIT
//       -Iinclude -Ibackends -I$MH tests/megahit_run.cpp
//       $MH/sdbg/sdbg_meta.cpp $MH/sdbg/sdbg_raw_content.cpp -o megahit_run
#define SDBGT_WITH_MEGAHIT 1
#include "sdbg_traverse/nav_oracle.hpp"
#include "sdbg_traverse/choice_dictionary.hpp"
#include "sdbg_traverse/bfs.hpp"
#include "sdbg_traverse/dfs.hpp"
#include "megahit_oracle.hpp"

#include <cstdio>
#include <map>
#include <vector>

using namespace sdbgt;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: megahit_run <graph_prefix>\n"); return 2; }

    SDBG g;
    std::printf("loading '%s' ...\n", argv[1]);
    g.LoadFromFile(argv[1]);
    MegaHitOracle oracle(g);
    const size_t N = oracle.N();
    std::printf("SDBG loaded: N=%zu edges, k=%u\n", N, g.k());

    size_t valid_count = 0; node_t src = kNoNode;
    for (node_t v = 0; v < N; ++v)
        if (oracle.valid(v)) { ++valid_count; if (src == kNoNode) src = v; }
    std::printf("valid nodes: %zu ; source = %llu\n", valid_count, (unsigned long long)src);
    if (src == kNoNode) { std::printf("no valid nodes to traverse\n"); return 1; }

    int failures = 0;

    // ---------- BFS ----------
    std::map<node_t, uint64_t> bfs_se;
    {
        std::map<node_t, uint64_t> bfs_base;
        auto s = se_bfs(oracle, src, [&](node_t v, uint64_t d) { bfs_se[v] = d; });
        auto b = baseline_bfs(oracle, src, [&](node_t v, uint64_t d) { bfs_base[v] = d; });
        bool ok = (bfs_se == bfs_base);
        std::printf("[BFS] se visited=%llu  baseline visited=%llu  distances match: %s\n",
                    (unsigned long long)s.visited, (unsigned long long)b.visited, ok ? "YES" : "NO");
        if (!ok) ++failures;
    }
    // independent cross-check vs InMemory (order-independent for BFS distances)
    if (N <= 5000000) {
        std::vector<std::pair<node_t, node_t>> edges;
        node_t nb[kSigma];
        for (node_t v = 0; v < N; ++v)
            if (oracle.valid(v)) { int d = oracle.outgoing(v, nb); for (int i = 0; i < d; ++i) edges.push_back({v, nb[i]}); }
        InMemoryOracle ref(N, edges);
        std::map<node_t, uint64_t> bfs_ref;
        baseline_bfs(ref, src, [&](node_t v, uint64_t d) { bfs_ref[v] = d; });
        bool ok = (bfs_se == bfs_ref);
        std::printf("[BFS] cross-check vs InMemory reference (%zu edges): %s\n", edges.size(), ok ? "YES" : "NO");
        if (!ok) ++failures;
    } else {
        std::printf("[BFS] InMemory cross-check skipped (N=%zu too large)\n", N);
    }

    // ---------- DFS ----------
    {
        std::vector<node_t> da, fa, pa(N, kNoNode), db, fb, pb(N, kNoNode);
        ChoiceDictionary visited(N); TwoBitArray psym(N);
        auto s = se_dfs(oracle, src, visited, psym,
                        [&](node_t v, node_t p) { da.push_back(v); pa[v] = p; },
                        [&](node_t v) { fa.push_back(v); });
        std::vector<uint8_t> state(N, 0);
        baseline_dfs_explicit(oracle, src, state,
                        [&](node_t v, node_t p) { db.push_back(v); pb[v] = p; },
                        [&](node_t v) { fb.push_back(v); });
        bool okd = (da == db), okf = (fa == fb), okp = (pa == pb);
        std::printf("[DFS] discovered se=%llu baseline=%zu | discovery:%s finish:%s parents:%s\n",
                    (unsigned long long)s.discovered, db.size(),
                    okd ? "OK" : "MISMATCH", okf ? "OK" : "MISMATCH", okp ? "OK" : "MISMATCH");
        if (s.finished)
            std::printf("[DFS] reconstruction tax = %.2f in-neighbour probes / finish\n",
                        double(s.recover_probes) / double(s.finished));
        if (!(okd && okf && okp)) ++failures;
    }

    // ---------- aux-state space on THIS real graph ----------
    {
        ChoiceDictionary cd(N);
        double se_bits = cd.resident_bytes() * 8.0 / double(N);
        std::printf("[space] choice-dict aux = %.2f bits/node  vs  32-bit distance array  (%.1fx smaller)\n",
                    se_bits, 32.0 / se_bits);
    }

    if (failures) { std::printf("RESULT: %d CHECK(S) FAILED\n", failures); return 1; }
    std::printf("RESULT: ALL CHECKS PASSED (on a real MEGAHIT SDBG)\n");
    return 0;
}
