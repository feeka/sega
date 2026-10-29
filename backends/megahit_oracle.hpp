// megahit_oracle.hpp — production NavOracle backend over MEGAHIT's SDBG.
//
// MEGAHIT's succinct dBG (the backend MCAAT is built on). Guarded by
// SDBGT_WITH_MEGAHIT. MEGAHIT is vendored as a git submodule at libs/megahit;
// CMake enables this automatically. Manually: add -Ilibs/megahit/src and compile
// sdbg/sdbg_meta.cpp + sdbg/sdbg_raw_content.cpp (everything else is header-only:
// kmlib, parallel_hashmap, pprintpp, utils.h).
//
// API used — verified from megahit/src/sdbg/sdbg.h (voutcn/megahit):
//   uint64_t size() const;                     // edge-ID universe [0,size())
//   uint32_t k() const;
//   bool     IsValidEdge(uint64_t) const;
//   int      EdgeOutdegree(uint64_t) const;    // -1 if invalid
//   int      EdgeIndegree (uint64_t) const;
//   int      OutgoingEdges(uint64_t, uint64_t* out) const;  // fills out, returns outdeg
//   int      IncomingEdges(uint64_t, uint64_t* in ) const;
//   void     LoadFromFile(const char* prefix);
//
// Traversal vertices are MEGAHIT stored edge IDs; adjacency is edge->edge.
// N includes invalid/dummy IDs, so N is not the valid-vertex or traversed count.
// MEGAHIT parameter k denotes node-label length; a stored edge spells k+1 bases.
// The original SDBG edges and the adjacency arcs traversed here are distinct.
// Out/in-degree are bounded by sigma = 4.

#pragma once
#if defined(SDBGT_WITH_MEGAHIT)

#include "sdbg_traverse/nav_oracle.hpp"
#include "sdbg/sdbg.h"   // MEGAHIT (include root = megahit/src)

namespace sdbgt {

class MegaHitOracle {
public:
    explicit MegaHitOracle(const SDBG& g) : g_(g) {}

    size_t N() const { return g_.size(); }
    bool valid(node_t v) const { return v < N() && g_.IsValidEdge(v); }

    int outdeg(node_t v) const {
        return valid(v) ? detail::checked_degree(g_.EdgeOutdegree(v)) : 0;
    }
    int indeg(node_t v) const {
        return valid(v) ? detail::checked_degree(g_.EdgeIndegree(v)) : 0;
    }

    int outgoing(node_t v, node_t out[kSigma]) const {
        if (!valid(v)) return 0;
        // Validate the degree before asking the upstream buffer-writing API to
        // populate four slots. This extra degree navigation is included in all
        // timed variants, and prevents silent truncation or buffer overflow.
        const int expected = outdeg(v);
        const int d = detail::checked_degree(g_.OutgoingEdges(v, out));
        if (d != expected) throw std::runtime_error("unstable outgoing adjacency");
        for (int i=0;i<d;++i) if (out[i]>=N()) throw std::out_of_range("MEGAHIT outgoing ID out of range");
        return d;
    }
    int incoming(node_t v, node_t in[kSigma]) const {
        if (!valid(v)) return 0;
        const int expected = indeg(v);
        const int d = detail::checked_degree(g_.IncomingEdges(v, in));
        if (d != expected) throw std::runtime_error("unstable incoming adjacency");
        for (int i=0;i<d;++i) if (in[i]>=N()) throw std::out_of_range("MEGAHIT incoming ID out of range");
        return d;
    }

private:
    const SDBG& g_;
};

} // namespace sdbgt
#endif // SDBGT_WITH_MEGAHIT
