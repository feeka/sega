#pragma once
#include "graph_types.hpp"
#include "sdbg_traverse/nav_oracle.hpp"
#include <array>
namespace sega {
// Borrow a bounded-degree native oracle, including MEGAHIT, without materializing
// its adjacency. The oracle must outlive this view and all active traversals.
// Native stable neighbor order is preserved; it need not be sorted by vertex ID.
template<class Oracle> class OracleView {
    const Oracle* oracle_;
public:
    struct Cursor { std::array<Vertex,4> values{}; uint8_t position=0,count=0; };
    explicit OracleView(const Oracle& oracle):oracle_(&oracle){}
    uint64_t num_vertices() const{return oracle_->N();}
    bool valid(Vertex v) const{return v<num_vertices() && oracle_->valid(v);}
    Cursor neighbors(Vertex v) const{
        if(!valid(v))throw std::out_of_range("invalid native vertex");
        Cursor c;c.count=uint8_t(sdbgt::detail::checked_degree(oracle_->outgoing(v,c.values.data())));
        for(unsigned i=0;i<c.count;++i)if(!valid(c.values[i]))throw std::runtime_error("native oracle returned invalid target");
        return c;
    }
    bool next(Cursor& c,Vertex& v) const{
        if(c.position==c.count)return false;
        v=c.values[c.position++];return true;
    }
};
} // namespace sega
