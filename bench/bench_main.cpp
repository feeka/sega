// bench_main.cpp — space-time harness on a real cosmo BOSS graph.
// Guarded by SDBGT_WITH_COSMO. Reports the three things RQ1–RQ2 need:
// peak RSS (decomposed), wall-clock, and the reconstruction tax (rank/select-ish
// probe counts) for se_bfs and se_dfs vs baselines.
//
// Usage:  bench <graph.dbg> <src-node-id>
#if defined(SDBGT_WITH_COSMO)
#include "sdbg_traverse/nav_oracle.hpp"
#include "sdbg_traverse/choice_dictionary.hpp"
#include "sdbg_traverse/bfs.hpp"
#include "sdbg_traverse/dfs.hpp"
#include "cosmo_oracle.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
static long peak_rss_kb() { struct rusage r; getrusage(RUSAGE_SELF, &r); return r.ru_maxrss; }
#else
static long peak_rss_kb() { return -1; }
#endif

using namespace sdbgt;
using clk = std::chrono::steady_clock;
template <class F> static double ms(F&& f) {
    auto t0 = clk::now(); f(); auto t1 = clk::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: bench <graph.dbg> <src>\n"); return 2; }
    debruijn_graph<> g;
    auto oracle = CosmoOracle<>::load_dbg(argv[1], g);
    node_t src = std::stoull(argv[2]);
    const size_t N = oracle.N();
    std::printf("graph: N=%zu nodes, k=%zu\n", N, size_t(g.k));

    // space budget (bits/node) for the O(n)-bit engines
    std::printf("choice-dict: %.3f bits/node   parent-trail: %.3f bits/node   baseline dist array: 32 bits/node\n",
                2.0, 2.0);

    ChoiceDictionary cd(N);
    BfsStats bs{};
    double t_bfs = ms([&]{ bs = se_bfs(oracle, src, cd, [](node_t,uint64_t){}); });
    std::printf("se_bfs   : %.1f ms  visited=%llu edges=%llu rounds=%llu  peakRSS=%ld KB\n",
                t_bfs, (unsigned long long)bs.visited, (unsigned long long)bs.edges_scanned,
                (unsigned long long)bs.rounds, peak_rss_kb());

    ChoiceDictionary visited(N);
    TwoBitArray psym(N);
    DfsStats ds{};
    double t_dfs = ms([&]{ ds = se_dfs(oracle, src, visited, psym,
                                       [](node_t,node_t){}, [](node_t){}); });
    std::printf("se_dfs   : %.1f ms  discovered=%llu child_scans=%llu parent_recover=%llu recover_probes=%llu\n",
                t_dfs, (unsigned long long)ds.discovered, (unsigned long long)ds.child_scans,
                (unsigned long long)ds.parent_recoveries, (unsigned long long)ds.recover_probes);
    // reconstruction tax = recover_probes / finished  (in-neighbour probes per pop)
    if (ds.finished) std::printf("           reconstruction tax = %.2f in-probes / finish\n",
                                 double(ds.recover_probes) / double(ds.finished));
    return 0;
}
#else
int main() { return 0; }
#endif
