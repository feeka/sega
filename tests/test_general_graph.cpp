#include "sega/general_graph.hpp"
// Independent adjacency-list references; assertions stay enabled in Release.
#include "sega/general_traversal.hpp"
#include <functional>
#include <iostream>
#include <queue>
#include <random>
#include <set>
#include <tuple>

#include "graph_reference.hpp"
template<class Graph> void navigation(const Graph& g,const Reference& ref) {
    check(g.num_vertices()==ref.out.size(),"vertex count");check(g.num_edges()==ref.ordered.size(),"edge count");
    uint64_t position=0;
    for(Vertex v=0;v<g.num_vertices();++v) {
        check(g.out_begin(v)==position,"boundary start including empty rows");
        check(g.outdegree(v)==ref.out[v].size(),"outdegree");check(g.indegree(v)==ref.in[v].size(),"indegree");
        for(auto target:ref.out[v]) {
            check(g.target(position)==target,"outgoing target");check(g.source(position)==v,"source of edge position");
            auto rank=std::find(ref.in[target].begin(),ref.in[target].end(),v)-ref.in[target].begin();
            check(g.incoming_rank(position)==uint64_t(rank),"incoming rank from independent incoming list");
            check(g.incoming(target,rank)==v,"incoming rank inverse");++position;
        }
        check(g.out_end(v)==position,"boundary end");
        for(uint64_t k=0;k<ref.in[v].size();++k)check(g.incoming(v,k)==ref.in[v][k],"incoming neighbor order");
        throws([&]{g.incoming(v,ref.in[v].size());},"reject incoming ordinal at degree");
    }
    throws([&]{g.out_begin(g.num_vertices());},"reject vertex at n");
    throws([&]{g.out_end(no_vertex);},"reject sentinel vertex");
    throws([&]{g.indegree(g.num_vertices());},"reject indegree invalid vertex");
    throws([&]{g.target(g.num_edges());},"reject edge at m");
    throws([&]{g.source(g.num_edges());},"reject source edge at m");
    throws([&]{g.incoming_rank(g.num_edges());},"reject incoming rank edge at m");
    check(g.storage().total()>=8,"storage includes metadata");
}
template<class Index,class Graph>
void traversals(const Graph& g,const Reference& ref,Vertex source,bool forest) {
    std::vector<Event> got;
    auto bs=sega::bfs<Index>(g,source,[&](Vertex v,uint64_t d){got.emplace_back('B',v,d);},forest);
    const auto expected_bfs=ref.bfs(source,forest);check(got==expected_bfs,"BFS exact ordered vertices and shortest distances");
    uint64_t arcs=0,roots=0;
    for(auto e:expected_bfs){arcs+=ref.out[std::get<1>(e)].size();roots+=std::get<2>(e)==0;}
    check(bs.visited==expected_bfs.size()&&bs.arcs_scanned==arcs&&bs.roots==roots,"BFS counts and one scan per reached arc");
    check(bs.aux_fixed_bytes==8*((g.num_vertices()+63)/64),"BFS bitmap bytes");
    check(bs.id_bits==8*sizeof(Index),"BFS ID width");
    got.clear();
    auto ds=sega::dfs<Index>(g,source,[&](Vertex v,Vertex p){got.emplace_back('D',v,p);},
        [&](Vertex v){got.emplace_back('F',v,0);},forest);
    auto expected_dfs=ref.dfs(source,forest);check(got==expected_dfs,"DFS exact interleaved discovery, parent, finish events");
    uint64_t depth=0,maximum=0;
    for(auto e:expected_dfs){if(std::get<0>(e)=='D')maximum=std::max(maximum,++depth);else --depth;}
    check(ds.visited==expected_dfs.size()/2&&ds.finished==ds.visited&&ds.arcs_scanned==arcs&&ds.roots==roots,"DFS counts and one scan per reached arc");
    check(ds.max_depth==maximum,"DFS depth");check(ds.aux_fixed_bytes==8*((g.num_vertices()+31)/32),"DFS packed-color bytes");
    check(ds.id_bits==8*sizeof(Index),"DFS ID width");
}
template<class Graph> void backend(size_t n,const std::vector<Edge>& edges,const Reference& ref,bool lifecycle) {
    Graph g(n,edges);navigation(g,ref);
    traversals<uint32_t>(g,ref,0,true);traversals<uint64_t>(g,ref,0,true);
    std::set<Vertex> sources;
    if(n<=5){for(Vertex v=0;v<n;++v)sources.insert(v);}else sources={0,n/2,n-1};
    for(auto source:sources){traversals<uint32_t>(g,ref,source,false);traversals<uint64_t>(g,ref,source,false);}
    auto emit=[](auto,auto){};auto finish=[](auto){};
    throws([&]{sega::bfs(g,n,emit);},"BFS invalid source");
    throws([&]{sega::dfs(g,n,emit,finish);},"DFS invalid source");
    if(lifecycle) {
        // Returned graph survives destruction of the copied-from object.
        Graph survivor=[&]{Graph original(n,edges);Graph copy(original);Graph moved(std::move(copy));return moved;}();
        navigation(survivor,ref);
        Graph assigned(0,{});assigned=survivor;navigation(assigned,ref);
        Graph moved_assignment(0,{});moved_assignment=std::move(assigned);navigation(moved_assignment,ref);
        const Graph& alias=survivor;survivor=alias;navigation(survivor,ref);
        if(n) {
            throws([&]{sega::bfs(g,0,[](auto,auto){throw std::runtime_error("sink failure");});},"BFS sink exception propagates");
            throws([&]{sega::dfs(g,0,emit,[](auto){throw std::runtime_error("sink failure");});},"DFS sink exception propagates");
            traversals<uint32_t>(g,ref,0,true);
        }
    }
}
void verify(size_t n,std::vector<Edge> edges,bool lifecycle=false) {
    ++cases;Reference ref(n,edges);std::reverse(edges.begin(),edges.end()); // exercise normalization
    backend<CsrGraph<uint32_t>>(n,edges,ref,lifecycle);backend<CsrGraph<uint64_t>>(n,edges,ref,lifecycle);
    backend<WaveletGraph<false>>(n,edges,ref,lifecycle);backend<WaveletGraph<true>>(n,edges,ref,lifecycle);
}
template<class Graph> void invalid_inputs() {
    throws([]{Graph g(2,{{0,2}});},"invalid target");throws([]{Graph g(2,{{2,0}});},"invalid source");
    throws([]{Graph g(1,{{0,0},{0,0}});},"duplicate self-loop");throws([]{Graph g(2,{{0,1},{0,1}});},"duplicate edge");
    throws([]{Graph g(0,{{0,0}});},"edge in empty graph");throws([]{Graph g(UINT64_MAX,{});},"oversized universe");
}
int main() {
    try {
        invalid_inputs<CsrGraph<uint32_t>>();invalid_inputs<CsrGraph<uint64_t>>();
        invalid_inputs<WaveletGraph<false>>();invalid_inputs<WaveletGraph<true>>();
        throws([]{CsrGraph<uint32_t> g(uint64_t(UINT32_MAX)+2,{});},"CSR32 range check before allocation");
        // All 531 directed simple graphs on 0..3 vertices, including loops.
        for(size_t n=0;n<=3;++n)for(uint64_t mask=0;mask<(uint64_t(1)<<(n*n));++mask) {
            std::vector<Edge> edges;
            for(size_t u=0;u<n;++u)for(size_t v=0;v<n;++v)if((mask>>(u*n+v))&1)edges.emplace_back(u,v);
            verify(n,edges,mask==0 || mask+1==(uint64_t(1)<<(n*n)));
        }
        verify(4,{{0,1},{0,3},{1,3},{3,0}},true);
        for(size_t n:{size_t(31),size_t(32),size_t(63),size_t(64),size_t(65),size_t(127),size_t(128),size_t(129),size_t(257)}) {
            verify(n,{},true);std::vector<Edge> path,star_out,star_in,cycle,loops;
            for(Vertex v=0;v<n;++v) {
                loops.emplace_back(v,v);cycle.emplace_back(v,(v+1)%n);
                if(v){path.emplace_back(v-1,v);star_out.emplace_back(0,v);star_in.emplace_back(v,0);}
            }
            for(auto edges:{path,star_out,star_in,cycle,loops})verify(n,edges);
        }
        std::mt19937_64 rng(20261026);
        for(size_t repeat=0;repeat<120;++repeat) {
            size_t n=4+rng()%80;std::set<Edge> unique;
            for(size_t i=0;i<n*5;++i)unique.emplace(rng()%n,rng()%n);
            verify(n,{unique.begin(),unique.end()},repeat<3);
        }
        std::vector<Edge> deep;for(Vertex v=1;v<20000;++v)deep.emplace_back(v-1,v);verify(20000,deep);
        std::cout<<"{\"status\":\""<<(failures?"failed":"ok")<<"\",\"graph_cases\":"<<cases
            <<",\"backends\":4,\"exhaustive_graphs\":531,\"checks\":"<<checks<<",\"failures\":"<<failures<<"}\n";
        return failures?1:0;
    }catch(const std::exception& e){std::cerr<<"Unexpected failure after "<<cases<<" cases: "<<e.what()<<'\n';return 2;}
}
