// megahit_oracle.hpp — production NavOracle backend over MEGAHIT's SDBG.
//
// MEGAHIT's succinct dBG (the backend MCAAT is built on). Guarded by
// SDBGT_WITH_MEGAHIT. MEGAHIT is vendored as a git submodule at libs/megahit;
// CMake enables this automatically. Manually: add -Ilibs/megahit/src and compile
// sdbg/sdbg_meta.cpp + sdbg/sdbg_raw_content.cpp (everything else is header-only:
// kmlib, parallel_hashmap, pprintpp, utils.h).
//
// API used — verified from megahit/src/sdbg/sdbg.h (voutcn/megahit):
//   uint64_t size() const;                     // node/edge count; nodes are [0,size())
//   uint32_t k() const;
//   bool     IsValidEdge(uint64_t) const;
//   int      EdgeOutdegree(uint64_t) const;    // -1 if invalid
//   int      EdgeIndegree (uint64_t) const;
//   int      OutgoingEdges(uint64_t, uint64_t* out) const;  // fills out, returns outdeg
//   int      IncomingEdges(uint64_t, uint64_t* in ) const;
//   void     LoadFromFile(const char* prefix);
//
// A "node" here is a MEGAHIT edge id (a k-mer); adjacency is edge->edge, exactly
// how MCAAT traverses it. Out/in-degree are bounded by sigma = 4.

#pragma once
#if defined(SDBGT_WITH_MEGAHIT)

#include "sdbg_traverse/nav_oracle.hpp"
#include "sdbg/sdbg.h"   // MEGAHIT (include root = megahit/src)

namespace sdbgt {

class MegaHitOracle {
public:
    explicit MegaHitOracle(const SDBG& g) : g_(g) {}

    size_t N() const { return g_.size(); }
    bool valid(node_t v) const { return g_.IsValidEdge(v); }

    int outdeg(node_t v) const { int d = g_.EdgeOutdegree(v); return d < 0 ? 0 : d; }
    int indeg(node_t v)  const { int d = g_.EdgeIndegree(v);  return d < 0 ? 0 : d; }

    int outgoing(node_t v, node_t out[kSigma]) const {
        uint64_t buf[8];                       // sigma<=4; buffer padded for safety
        int d = g_.OutgoingEdges(v, buf);
        if (d <= 0) return 0;
        int n = d < kSigma ? d : kSigma;
        for (int i = 0; i < n; ++i) out[i] = buf[i];
        return n;
    }
    int incoming(node_t v, node_t in[kSigma]) const {
        uint64_t buf[8];
        int d = g_.IncomingEdges(v, buf);
        if (d <= 0) return 0;
        int n = d < kSigma ? d : kSigma;
        for (int i = 0; i < n; ++i) in[i] = buf[i];
        return n;
    }

private:
    const SDBG& g_;
};

} // namespace sdbgt
#endif // SDBGT_WITH_MEGAHIT
