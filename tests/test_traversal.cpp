// test_traversal.cpp — validate the space-efficient BFS/DFS against textbook
// baselines on hand-built graphs and a seeded random stress test.
//
//   BFS: se_bfs distances must equal baseline_bfs distances (per node).
//   DFS: se_dfs discovery order, finish order and parent map must equal the
//        explicit-stack DFS using the SAME neighbour order.
//
// Build:  g++ -std=c++17 -O2 -Iinclude tests/test_traversal.cpp -o test_traversal
#include "sdbg_traverse/nav_oracle.hpp"
#include "sdbg_traverse/choice_dictionary.hpp"
#include "sdbg_traverse/bfs.hpp"
#include "sdbg_traverse/dfs.hpp"

#include <cstdio>
#include <map>
#include <vector>
#include <string>

using namespace sdbgt;

static int g_failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  [FAIL] %s\n", what.c_str()); ++g_failures; }
}

// ---- BFS: compare se vs baseline distance maps from a given source ----
static void check_bfs(const InMemoryOracle& g, node_t src, const std::string& tag) {
    std::map<node_t, uint64_t> a, b;
    ChoiceDictionary cd(g.N());
    se_bfs(g, src, cd, [&](node_t v, uint64_t d) { a[v] = d; });
    baseline_bfs(g, src, [&](node_t v, uint64_t d) { b[v] = d; });
    check(a == b, tag + ": se_bfs == baseline_bfs (src=" + std::to_string(src) + ")");
}

// ---- DFS: compare discovery/finish order + parent map ----
static void check_dfs(const InMemoryOracle& g, node_t src, const std::string& tag) {
    std::vector<node_t> disc_a, fin_a, par_a(g.N(), kNoNode);
    std::vector<node_t> disc_b, fin_b, par_b(g.N(), kNoNode);

    ChoiceDictionary visited(g.N());
    TwoBitArray psym(g.N());
    se_dfs(g, src, visited, psym,
           [&](node_t v, node_t p) { disc_a.push_back(v); par_a[v] = p; },
           [&](node_t v) { fin_a.push_back(v); });

    std::vector<uint8_t> state(g.N(), 0);
    baseline_dfs_explicit(g, src, state,
           [&](node_t v, node_t p) { disc_b.push_back(v); par_b[v] = p; },
           [&](node_t v) { fin_b.push_back(v); });

    check(disc_a == disc_b, tag + ": DFS discovery order matches (src=" + std::to_string(src) + ")");
    check(fin_a == fin_b,   tag + ": DFS finish order matches (src=" + std::to_string(src) + ")");
    check(par_a == par_b,   tag + ": DFS parent map matches (src=" + std::to_string(src) + ")");
}

static InMemoryOracle make(size_t n, std::vector<std::pair<node_t,node_t>> e) {
    return InMemoryOracle(n, e);
}

int main() {
    std::printf("sdbg-traverse validation\n");

    // 1. path 0->1->2->3
    { auto g = make(4, {{0,1},{1,2},{2,3}});
      check_bfs(g,0,"path"); check_dfs(g,0,"path"); }

    // 2. diamond with a node reachable two ways: 0->1,0->2,1->3,2->3
    { auto g = make(4, {{0,1},{0,2},{1,3},{2,3}});
      check_bfs(g,0,"diamond"); check_dfs(g,0,"diamond"); }

    // 3. directed cycle 0->1->2->0
    { auto g = make(3, {{0,1},{1,2},{2,0}});
      check_bfs(g,0,"cycle"); check_dfs(g,0,"cycle"); }

    // 4. self loop + branch
    { auto g = make(3, {{0,0},{0,1},{0,2},{1,2}});
      check_bfs(g,0,"selfloop"); check_dfs(g,0,"selfloop"); }

    // 5. disconnected: {0->1->2} and {3->4}; single-source from 0 covers only first
    { auto g = make(5, {{0,1},{1,2},{3,4}});
      check_bfs(g,0,"disc"); check_dfs(g,0,"disc"); }

    // 6. seeded random stress: N nodes, out/in-degree capped at sigma=4
    {
        const size_t N = 4000;
        uint64_t s = 0x9E3779B97F4A7C15ULL;             // deterministic LCG
        auto rnd = [&]() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return s >> 33; };
        std::vector<int> outd(N,0), ind(N,0);
        std::vector<std::pair<node_t,node_t>> edges;
        for (size_t it = 0; it < N * 3; ++it) {
            node_t u = rnd() % N, v = rnd() % N;
            if (outd[u] < kSigma && ind[v] < kSigma) { edges.push_back({u,v}); ++outd[u]; ++ind[v]; }
        }
        auto g = make(N, edges);
        for (node_t src : {node_t(0), node_t(1), node_t(N/2), node_t(N-1)}) {
            check_bfs(g, src, "random");
            check_dfs(g, src, "random");
        }
        // concrete space story on this graph:
        ChoiceDictionary cd(N);
        double se_bits   = cd.resident_bytes() * 8.0 / N;           // ~2 bits/node
        double base_bits = (sizeof(uint32_t) * 8.0);               // 32-bit dist array
        std::printf("  [space] N=%zu  se_bfs choice-dict = %.2f bits/node  vs  baseline dist array = %.0f bits/node  (%.1fx)\n",
                    N, se_bits, base_bits, base_bits / se_bits);
    }

    if (g_failures == 0) { std::printf("ALL CHECKS PASSED\n"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
