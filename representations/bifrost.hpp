#pragma once
#include "common.hpp"
#include "CompactedDBG.hpp"
namespace repr {
struct Graph : MembershipNavigation<Graph> {
    CompactedDBG<> graph;
    std::vector<UnitigMap<void,void,true>> unitigs;
    std::vector<uint64_t> offsets;
    std::unordered_map<std::string,size_t> heads;
    unsigned k;
    explicit Graph(const std::string& file, unsigned expected_k) : graph(expected_k), k(expected_k) {
        check(graph.read(file,1,false),"Cannot load Bifrost graph");
        const auto& const_graph=graph;
        offsets.push_back(0);
        for(const auto& u : const_graph) {
            heads.emplace(u.getUnitigHead().toString(),unitigs.size());
            unitigs.push_back(u); offsets.push_back(offsets.back()+u.size-k+1);
        }
    }
    static constexpr const char* name="bifrost";
    static constexpr const char* navigation="native-Bifrost-unitig-neighbors-and-internal-offsets";
    size_t N() const { return 2*offsets.back(); }
    bool valid(node_t v) const { return v<N(); }
    // Hash-table allocator overhead is not derivable portably. Report the
    // auditable payload lower bound, never present it as a complete allocation.
    size_t adapter_bytes() const {
        return unitigs.capacity()*sizeof(unitigs[0])+offsets.capacity()*8+
            heads.size()*(sizeof(std::pair<const std::string,size_t>)+k)+heads.bucket_count()*sizeof(void*);
    }
    std::string label(node_t v) const {
        auto id=v/2; size_t u=std::upper_bound(offsets.begin(),offsets.end(),id)-offsets.begin()-1;
        auto s=unitigs[u].getUnitigKmer(id-offsets[u]).toString(); return v%2?rc(s):s;
    }
    node_t lookup(const std::string& s) const {
        const auto u=graph.find(Kmer(s.c_str())); if(u.isEmpty) return kNoNode;
        auto id=heads.at(u.getUnitigHead().toString());
        return 2*(offsets[id]+u.dist)+!u.strand;
    }
    int adjacent(node_t v,node_t* out,bool backward) const {
        const auto id=v/2;const bool reverse=v%2;
        const size_t u=std::upper_bound(offsets.begin(),offsets.end(),id)-offsets.begin()-1;
        const auto pos=id-offsets[u],length=offsets[u+1]-offsets[u];
        const bool increasing=(!reverse)!=backward;
        if(increasing && pos+1<length){out[0]=2*(id+1)+reverse;return 1;}
        if(!increasing && pos>0){out[0]=2*(id-1)+reverse;return 1;}
        auto mapping=unitigs[u];mapping.strand=!reverse;int d=0;
        auto append=[&](const auto& n){
            check(d<4,"Bifrost degree >4");const size_t target=heads.at(n.getUnitigHead().toString());
            const size_t last=offsets[target+1]-offsets[target]-1;
            const size_t target_pos=backward?(n.strand?last:0):(n.strand?0:last);
            out[d++]=2*(offsets[target]+target_pos)+!n.strand;
        };
        if(backward){for(const auto& n:mapping.getPredecessors())append(n);}
        else {for(const auto& n:mapping.getSuccessors())append(n);}
        return d;
    }
    int outgoing(node_t v,node_t* out) const { return adjacent(v,out,false); }
    int incoming(node_t v,node_t* out) const { return adjacent(v,out,true); }
};
}
