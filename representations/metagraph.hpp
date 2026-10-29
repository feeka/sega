#pragma once
#include "common.hpp"
#include "graph/representation/succinct/dbg_succinct.hpp"
namespace repr {
struct Graph {
    mtg::graph::DBGSuccinct graph;
    std::vector<uint64_t> real;
    explicit Graph(const std::string& file, unsigned k) : graph(k) {
        check(graph.load(file),"Cannot load MetaGraph succinct index"); check(graph.get_k()==k,"MetaGraph k mismatch");
        real.assign((N()+63)/64,0); graph.call_nodes([&](node_t v){ real[v/64]|=uint64_t(1)<<(v%64); });
    }
    static constexpr const char* name="metagraph";
    static constexpr const char* navigation="native-BOSS-incoming-and-outgoing";
    size_t N() const { return graph.max_index()+1; }
    bool valid(node_t v) const { return v<N() && ((real[v/64]>>(v%64))&1); }
    size_t adapter_bytes() const { return real.capacity()*8; }
    std::string label(node_t v) const { return graph.get_node_sequence(v); }
    node_t lookup(const std::string& s) const { auto v=graph.kmer_to_node(s); return v?v:kNoNode; }
    int outgoing(node_t v,node_t* a) const {
        int n=0; graph.adjacent_outgoing_nodes(v,[&](node_t w){ if(valid(w)){check(n<4,"MetaGraph degree >4"); a[n++]=w;} }); return n;
    }
    int incoming(node_t v,node_t* a) const {
        int n=0; graph.adjacent_incoming_nodes(v,[&](node_t w){ if(valid(w)){check(n<4,"MetaGraph degree >4"); a[n++]=w;} }); return n;
    }
    // Native adjacency may expose BOSS source/sink sentinels. The common
    // logical graph contains only ACGT k-mers, so degrees use the same filter.
    int outdeg(node_t v) const { node_t a[4];return outgoing(v,a); }
    int indeg(node_t v) const { node_t a[4];return incoming(v,a); }
};
}
