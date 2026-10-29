#pragma once
#include "common.hpp"
#include "megahit_oracle.hpp"
namespace repr {
struct Graph {
    SDBG graph;
    sdbgt::MegaHitOracle oracle;
    unsigned k;
    explicit Graph(const std::string& file, unsigned expected_k) : oracle(graph), k(expected_k) {
        graph.LoadFromFile(file.c_str()); graph.FreeMultiplicity();
        check(graph.k()+1==k,"MEGAHIT native k must be common K minus one");
    }
    static constexpr const char* name="megahit";
    static constexpr const char* navigation="native-BOSS-incoming-and-outgoing";
    size_t N() const { return oracle.N(); }
    bool valid(node_t v) const { return oracle.valid(v); }
    size_t adapter_bytes() const { return 0; }
    std::string label(node_t v) const {
        std::vector<uint8_t> b(k); graph.GetLabel(v,b.data());
        auto w=graph.GetW(v); b[k-1]=w>4?w-4:w;
        std::string s; for(auto c:b) { check(c>=1&&c<=4,"Invalid MEGAHIT DNA label"); s.push_back("ACGT"[c-1]); } return s;
    }
    node_t lookup(const std::string& s) const {
        std::vector<uint8_t> b(k); for(unsigned i=0;i<k;++i) b[i]=std::string("ACGT").find(s[i])+1;
        auto last=graph.IndexBinarySearch(b.data()); if(last==SDBG::kNullID) return kNoNode;
        for(auto v=last;;--v) {
            auto w=graph.GetW(v); if(w>4) w-=4;
            if(w==b.back()&&valid(v)) return v;
            if(v==0 || graph.IsLastOrTip(v-1)) break;
        }
        return kNoNode;
    }
    int outgoing(node_t v,node_t* a) const { return oracle.outgoing(v,a); }
    int incoming(node_t v,node_t* a) const { return oracle.incoming(v,a); }
    int outdeg(node_t v) const { return oracle.outdeg(v); }
    int indeg(node_t v) const { return oracle.indeg(v); }
};
}
