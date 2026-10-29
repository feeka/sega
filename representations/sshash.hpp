#pragma once
#include "common.hpp"
#include "dictionary_types.hpp"
#include "src/dictionary.cpp"
namespace repr {
struct Graph : MembershipNavigation<Graph> {
    sshash::dictionary_type graph;
    explicit Graph(const std::string& file, unsigned k) {
        essentials::load(graph,file.c_str()); check(graph.k()==k,"SSHash k mismatch");
    }
    static constexpr const char* name="sshash";
    static constexpr const char* navigation="native-SSHash-kmer-neighborhood-query";
    size_t N() const { return 2*graph.num_kmers(); }
    bool valid(node_t v) const { return v<N(); }
    size_t adapter_bytes() const { return 0; }
    std::string label(node_t v) const {
        std::string s(graph.k(),'A'); graph.access(v/2,s.data()); return v%2?rc(s):s;
    }
    node_t lookup(const std::string& s) const {
        auto r=graph.lookup(s.data());
        return r.kmer_id==sshash::constants::invalid_uint64?kNoNode:
            2*r.kmer_id+(r.kmer_orientation==sshash::constants::backward_orientation);
    }
    int outgoing(node_t v,node_t* out) const {
        const auto s=label(v); const auto n=graph.kmer_forward_neighbours(s.data());int d=0;
        for(char c:std::string("ACGT")){const auto& r=n.forward[sshash::default_kmer_t::char_to_uint(c)];
            if(r.kmer_id!=sshash::constants::invalid_uint64)out[d++]=2*r.kmer_id+(r.kmer_orientation==sshash::constants::backward_orientation);}
        return d;
    }
    int incoming(node_t v,node_t* out) const {
        const auto s=label(v); const auto n=graph.kmer_backward_neighbours(s.data());int d=0;
        for(char c:std::string("ACGT")){const auto& r=n.backward[sshash::default_kmer_t::char_to_uint(c)];
            if(r.kmer_id!=sshash::constants::invalid_uint64)out[d++]=2*r.kmer_id+(r.kmer_orientation==sshash::constants::backward_orientation);}
        return d;
    }
};
}
