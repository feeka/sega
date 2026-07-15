// cosmo_oracle.hpp — production NavOracle backend over cosmo/VARI BOSS.
//
// Wraps cosmo's debruijn_graph so se_bfs / se_dfs run unchanged on a real
// succinct de Bruijn graph. Guarded by SDBGT_WITH_COSMO because it needs cosmo
// + sdsl on the include path (POSIX-oriented; build in a Linux/sdsl env).
//
// cosmo API used (verified from debruijn_graph.hpp):
//   size_t num_nodes();                          // nodes are ids in [0, num_nodes())
//   size_t outdegree(size_t v); indegree(size_t v);
//   ssize_t outgoing(size_t u, symbol_type x);   // per-symbol; returns node or -1
//   ssize_t incoming(size_t v, symbol_type x);   // per-symbol; returns node or -1
//   static debruijn_graph load_from_packed_edges(istream&, label_type, ...);
//   void load(istream&);                          // after serialize()
//
// NOTE: symbol range. cosmo encodes $=0 and A,C,G,T=1..sigma, so we enumerate
// x = 1..kSigma. Confirm against the loaded graph's alphabet before trusting
// large-scale numbers; dummy/sentinel handling for valid() is a v1 TODO.

#pragma once
#if defined(SDBGT_WITH_COSMO)

#include "sdbg_traverse/nav_oracle.hpp"
#include <fstream>
#include <string>

// cosmo header (provide its include dir via CMake):
#include "debruijn_graph.hpp"

namespace sdbgt {

template <class DBG = debruijn_graph<>>
class CosmoOracle {
public:
    explicit CosmoOracle(const DBG& g) : g_(g) {}

    static CosmoOracle load_dbg(const std::string& path, DBG& storage) {
        std::ifstream in(path, std::ios::binary);
        storage.load(in);
        return CosmoOracle(storage);
    }

    size_t N() const { return g_.num_nodes(); }

    // v1: treat all nodes as valid. Filter BOSS dummy nodes here once handled.
    bool valid(node_t) const { return true; }

    int outdeg(node_t v) const { return int(g_.outdegree(size_t(v))); }
    int indeg (node_t v) const { return int(g_.indegree(size_t(v))); }

    int outgoing(node_t v, node_t out[kSigma]) const {
        int d = 0;
        for (int x = 1; x <= kSigma; ++x) {
            auto w = g_.outgoing(size_t(v), typename DBG::symbol_type(x));
            if (w >= 0) out[d++] = node_t(w);
        }
        return d;
    }
    int incoming(node_t v, node_t in[kSigma]) const {
        int d = 0;
        for (int x = 1; x <= kSigma; ++x) {
            auto u = g_.incoming(size_t(v), typename DBG::symbol_type(x));
            if (u >= 0) in[d++] = node_t(u);
        }
        return d;
    }

private:
    const DBG& g_;
};

} // namespace sdbgt
#endif // SDBGT_WITH_COSMO
