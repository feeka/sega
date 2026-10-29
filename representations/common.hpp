#pragma once
#include "benchmark_driver.hpp"
#include <algorithm>
#include <numeric>
#include <unordered_map>

namespace repr {
using sdbgt::node_t;
using sdbgt::kNoNode;
using sega_bench::Json;
using sega_bench::Clock;
using sega_bench::milliseconds;
inline std::string rc(std::string s) {
    std::reverse(s.begin(), s.end());
    for (char& c : s) c = c=='A'?'T':c=='C'?'G':c=='G'?'C':'A';
    return s;
}
inline void check(bool b, const std::string& message) {
    if (!b) throw std::runtime_error(message);
}
// This fallback is deliberately named and reported. It makes eight exact
// dictionary lookups for both directions; it does not construct adjacency lists.
template<class Derived> struct MembershipNavigation {
    int outgoing(node_t v, node_t* out) const {
        const auto& g=static_cast<const Derived&>(*this);
        auto s=g.label(v); s.erase(s.begin()); s.push_back('A'); int d=0;
        for (char c : std::string("ACGT")) { s.back()=c; auto w=g.lookup(s); if(w!=kNoNode) out[d++]=w; }
        return d;
    }
    int incoming(node_t v, node_t* out) const {
        const auto& g=static_cast<const Derived&>(*this);
        auto s=g.label(v); s.pop_back(); s.insert(s.begin(),'A'); int d=0;
        for (char c : std::string("ACGT")) { s.front()=c; auto w=g.lookup(s); if(w!=kNoNode) out[d++]=w; }
        return d;
    }
    int outdeg(node_t v) const { node_t a[4]; return static_cast<const Derived&>(*this).outgoing(v,a); }
    int indeg(node_t v) const { node_t a[4]; return static_cast<const Derived&>(*this).incoming(v,a); }
};
}
