// megahit_bench.cpp — REAL memory + time measurement of one traversal on a real
// MEGAHIT SDBG. One method per process (so VmHWM is that method's peak, not
// polluted by another). Reports:
//   graph_rss_kb     : VmRSS right after loading the SDBG (the graph floor)
//   aux_fixed_bytes  : exact resident size of the method's bookkeeping structures
//   aux_alive_rss_kb : VmRSS with the aux still allocated (measured aux cost)
//   peak_hwm_kb      : VmHWM = process peak resident set (real OS number)
//   wall_ms          : traversal wall time (excludes load)
//
// Traversals use no-op callbacks, so the aux measured IS the algorithm's working
// set (no result-recording overhead).
//
// Build: `cmake -B build && cmake --build build` (megahit vendored at libs/megahit),
// then ./build/megahit_bench <graph_prefix> <bfs-se|bfs-base|dfs-se|dfs-base>.
// Manual, from repo root: MH=libs/megahit/src ; g++ -std=c++17 -O2 -march=native
//   -pthread -DSDBGT_WITH_MEGAHIT -Iinclude -Ibackends -I$MH bench/megahit_bench.cpp
//   $MH/sdbg/sdbg_meta.cpp $MH/sdbg/sdbg_raw_content.cpp -o megahit_bench
#define SDBGT_WITH_MEGAHIT 1
#include "sdbg_traverse/nav_oracle.hpp"
#include "sdbg_traverse/choice_dictionary.hpp"
#include "sdbg_traverse/bfs.hpp"
#include "sdbg_traverse/dfs.hpp"
#include "megahit_oracle.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <chrono>

using namespace sdbgt;

static long status_kb(const char* key) {
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long val = -1;
    size_t klen = std::strlen(key);
    while (std::fgets(line, sizeof line, f)) {
        if (std::strncmp(line, key, klen) == 0) { std::sscanf(line + klen, " %ld", &val); break; }
    }
    std::fclose(f);
    return val;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: megahit_bench <prefix> <bfs-se|bfs-base|dfs-se|dfs-base>\n"); return 2; }
    SDBG g;
    g.LoadFromFile(argv[1]);
    MegaHitOracle oracle(g);
    const size_t N = oracle.N();
    const std::string mode = argv[2];

    node_t src = kNoNode;
    for (node_t v = 0; v < N; ++v) if (oracle.valid(v)) { src = v; break; }

    const long graph_rss = status_kb("VmRSS:");     // graph floor
    long aux_fixed = 0, aux_alive_rss = -1;
    unsigned long long visited = 0;

    using clk = std::chrono::steady_clock;
    auto t0 = clk::now();

    if (mode == "bfs-se") {
        aux_fixed = (long)((N + 63) / 64 * 8);   // 1-bit visited bitmap
        auto s = se_bfs(oracle, src, [](node_t, uint64_t) {});
        aux_alive_rss = status_kb("VmRSS:");         // NOTE: se_bfs frees its bitmap+frontier on return, so this ~= graph floor; the real bfs-se peak is peak_hwm
        visited = s.visited;
    } else if (mode == "bfs-base") {
        aux_fixed = (long)(N * sizeof(uint32_t));    // 32-bit distance array
        auto s = baseline_bfs(oracle, src, [](node_t, uint64_t) {});
        visited = s.visited;                         // dist freed on return -> use peak_hwm
    } else if (mode == "dfs-se") {
        ChoiceDictionary visited_cd(N);
        TwoBitArray psym(N);
        aux_fixed = (long)(visited_cd.resident_bytes() + psym.resident_bytes());
        auto s = se_dfs(oracle, src, visited_cd, psym, [](node_t, node_t) {}, [](node_t) {});
        aux_alive_rss = status_kb("VmRSS:");         // visited + trail still alive
        visited = s.discovered;
    } else if (mode == "dfs-base") {
        std::vector<uint8_t> state(N, 0);
        aux_fixed = (long)state.size();
        auto s = baseline_dfs_explicit(oracle, src, state, [](node_t, node_t) {}, [](node_t) {});
        aux_alive_rss = status_kb("VmRSS:");         // state array still alive (stack peak via hwm)
        visited = s.discovered;
    } else {
        std::fprintf(stderr, "unknown mode: %s\n", mode.c_str());
        return 2;
    }

    auto t1 = clk::now();
    double wall_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const long peak_hwm = status_kb("VmHWM:");

    std::printf("mode=%s N=%zu k=%u visited=%llu graph_rss_kb=%ld aux_fixed_bytes=%ld "
                "aux_alive_rss_kb=%ld peak_hwm_kb=%ld wall_ms=%.1f\n",
                mode.c_str(), N, g.k(), visited, graph_rss, aux_fixed,
                aux_alive_rss, peak_hwm, wall_ms);
    return 0;
}
