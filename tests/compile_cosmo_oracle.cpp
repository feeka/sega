// compile_cosmo_oracle.cpp — type-checks CosmoOracle against cosmo's REAL
// debruijn_graph.hpp, and forces se_bfs/se_dfs to instantiate over it.
//
// This does NOT prove a run on a real graph (that needs a constructed .dbg);
// it proves the adapter binds to cosmo's actual API — the check that was
// missing when CosmoOracle had never seen the real headers.
//
// Build (in WSL) — VERIFIED WORKING (g++ 15.2.1, openSUSE Tumbleweed):
//   g++ -std=c++17 -DNDEBUG -O1 -DSDBGT_WITH_COSMO \
//       -Iinclude -Ibackends -I$HOME/cosmo -I$HOME/sdsl-lite/include \
//       tests/compile_cosmo_oracle.cpp -o /tmp/compile_cosmo
//   -DNDEBUG is REQUIRED: strips a broken assert (undeclared `x`) at cosmo
//     debruijn_graph.hpp:143. The xxsds sdsl fork requires C++17. No -lsdsl / no
//     divsufsort link needed for load+navigate (header-only, no SA construction).
//   Output on an empty graph: "instantiated se_bfs/se_dfs over CosmoOracle OK".
#define SDBGT_WITH_COSMO 1
#include "sdbg_traverse/nav_oracle.hpp"
#include "sdbg_traverse/choice_dictionary.hpp"
#include "sdbg_traverse/bfs.hpp"
#include "sdbg_traverse/dfs.hpp"
#include "cosmo_oracle.hpp"   // includes cosmo's debruijn_graph.hpp

#include <cstdio>

using namespace sdbgt;

int main() {
    debruijn_graph<> g;                 // cosmo's real type, default-constructed (empty)
    CosmoOracle<> oracle(g);

    std::printf("CosmoOracle over cosmo debruijn_graph: N=%zu\n", oracle.N());

    // Force template instantiation of both traversals over the cosmo backend.
    // Guarded by N()>0 so it never runs on the empty graph, but the compiler
    // still instantiates and type-checks the full call chain against cosmo's API.
    if (oracle.N() > 0) {
        ChoiceDictionary cd(oracle.N());
        se_bfs(oracle, node_t(0), cd, [](node_t, uint64_t) {});

        ChoiceDictionary visited(oracle.N());
        TwoBitArray psym(oracle.N());
        se_dfs(oracle, node_t(0), visited, psym,
               [](node_t, node_t) {}, [](node_t) {});
    }
    std::printf("instantiated se_bfs/se_dfs over CosmoOracle OK\n");
    return 0;
}
