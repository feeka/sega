#pragma once
#include "common.hpp"
#include "variants.hh"
#include "SubsetMatrixSelectSupport.hh"
namespace repr {
struct Graph : MembershipNavigation<Graph> {
    sbwt::plain_matrix_sbwt_t graph;
    sbwt::SubsetMatrixSelectSupport<sdsl::bit_vector> select;
    sdsl::bit_vector dummy;
    unsigned k;
    explicit Graph(const std::string& file, unsigned expected_k) : k(expected_k) {
        std::ifstream in(file,std::ios::binary); check(bool(in),"Cannot open SBWT index");
        // Upstream CLI prepends its variant name before the serialized index.
        std::string variant=sbwt::load_string(in);
        check(variant=="plain-matrix","Only SBWT plain-matrix is supported by this probe");
        graph.load(in); check(graph.get_k()==k,"SBWT k mismatch");
        select=sbwt::SubsetMatrixSelectSupport<sdsl::bit_vector>(graph.get_subset_rank_structure());
        dummy=graph.compute_dummy_node_marks();
    }
    static constexpr const char* name="sbwt";
    static constexpr const char* navigation="native-SBWT-forward-and-select-suffix-group-incoming";
    size_t N() const { return graph.number_of_subsets(); }
    bool valid(node_t v) const { return v<N() && !dummy[v]; }
    size_t adapter_bytes() const { return dummy.capacity()/8+sdsl::size_in_bytes(select.A_bits_ss)+sdsl::size_in_bytes(select.C_bits_ss)+sdsl::size_in_bytes(select.G_bits_ss)+sdsl::size_in_bytes(select.T_bits_ss); }
    std::string label(node_t v) const {
        std::string s(k+1,'\0'); graph.get_kmer_fast(v,s.data(),select); s.resize(k); return s;
    }
    node_t lookup(const std::string& s) const { auto v=graph.search(s); return v<0?kNoNode:node_t(v); }
    int outgoing(node_t v,node_t* out) const {
        int n=0;
        for(char c:std::string("ACGT")){auto w=graph.forward(v,c);if(w>=0&&valid(w))out[n++]=w;}
        return n;
    }
    int incoming(node_t v,node_t* out) const {
        // Invert the native forward step with subset select, then enumerate
        // its suffix group. Every member has the same outgoing destinations.
        const auto& C=graph.get_C_array();unsigned c=0;
        while(c+1<4&&v>=node_t(C[c+1]))++c;
        node_t first=select.select(v-C[c]+1,"ACGT"[c]);
        const auto& starts=graph.get_streaming_support();int n=0;
        for(node_t w=first;w<N()&&(w==first||!starts[w]);++w)if(valid(w)){
            check(n<4,"SBWT incoming degree >4");out[n++]=w;
        }
        return n;
    }
};
}
