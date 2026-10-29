#include "sega/packed_graph.hpp"
#include "sega/general_traversal.hpp"
#include "sega/oracle_view.hpp"
#include "graph_reference.hpp"
#include <filesystem>

std::filesystem::path scratch;
template<class Graph> void navigation(const Graph& g,const Reference& ref){
    check(g.num_vertices()==ref.out.size() && g.num_edges()==ref.ordered.size(),"dimensions");
    for(Vertex u=0;u<g.num_vertices();++u){
        auto c=g.neighbors(u);Vertex v;size_t i=0;
        while(g.next(c,v)){
            check(i<ref.out[u].size() && v==ref.out[u][i++],"cursor exact sorted target");
            auto copy=c;Vertex a,b;const bool ha=g.next(copy,a);auto copy2=c;const bool hb=g.next(copy2,b);
            check(ha==hb && (!ha || a==b),"independent resumable cursor copies");
        }
        check(i==ref.out[u].size() && !g.next(c,v),"cursor exhausted stably");
    }
    throws([&]{g.neighbors(g.num_vertices());},"invalid cursor root");
}
template<class Index,class Graph> void traversals(const Graph& g,const Reference& ref,Vertex source,bool forest){
    std::vector<Event> got;
    auto bs=bfs<Index>(g,source,[&](Vertex v,uint64_t d){got.emplace_back('B',v,d);},forest);
    auto expected=ref.bfs(source,forest);check(got==expected,"exact BFS order/distances");
    uint64_t arcs=0;for(auto e:expected)arcs+=ref.out[std::get<1>(e)].size();
    check(bs.visited==expected.size() && bs.arcs_scanned==arcs,"BFS one scan per reached arc");got.clear();
    auto ds=dfs<Index>(g,source,[&](Vertex v,Vertex p){got.emplace_back('D',v,p);},[&](Vertex v){got.emplace_back('F',v,0);},forest);
    check(got==ref.dfs(source,forest),"exact DFS parents/discovery/finish");
    check(ds.visited==expected.size() && ds.finished==ds.visited && ds.arcs_scanned==arcs,"DFS one scan per reached arc");
}
template<class Graph> void backend(size_t n,const std::vector<Edge>& edges){
    Graph g(n,edges);Reference ref(n,edges);navigation(g,ref);
    traversals<uint32_t>(g,ref,0,true);traversals<uint64_t>(g,ref,0,true);
    std::set<Vertex> sources;if(n<=5){for(Vertex v=0;v<n;++v)sources.insert(v);}else sources={0,n/2,n-1};
    BfsWorkspace<> workspace;
    for(auto source:sources){
        traversals<uint32_t>(g,ref,source,false);traversals<uint64_t>(g,ref,source,false);
        auto full=ref.bfs(source,false);
        for(uint64_t radius:{0,1,2,5}){
            std::vector<Event> got,want;uint64_t arcs=0;
            for(auto e:full)if(std::get<2>(e)<=radius){want.push_back(e);if(std::get<2>(e)<radius)arcs+=ref.out[std::get<1>(e)].size();}
            auto st=bfs(g,source,[&](Vertex v,uint64_t d){got.emplace_back('B',v,d);},workspace,false,radius);
            check(got==want && st.arcs_scanned==arcs,"bounded BFS exact distances, completeness, boundary stops and reused workspace");
        }
    }
    const auto file=(scratch/"graph.bin").string();g.save(file);
    check(std::filesystem::file_size(file)==g.storage().total(),"exact saved bytes including headers/checksum");
    throws([&]{g.save(file);},"refuse overwrite");
    Graph loaded=Graph::load(file);std::filesystem::remove(file);navigation(loaded,ref);traversals<uint32_t>(loaded,ref,0,true);
    Graph moved=[&]{Graph copy(g);return Graph(std::move(copy));}();navigation(moved,ref);
    Graph assigned(0,{});assigned=g;navigation(assigned,ref);Graph moved_assigned(0,{});moved_assigned=std::move(assigned);navigation(moved_assigned,ref);
    if(n){
        throws([&]{bfs(g,0,[](auto,auto){throw std::runtime_error("sink");},workspace);},"sink error propagates");
        std::vector<Event> got;bfs(g,0,[&](Vertex v,uint64_t d){got.emplace_back('B',v,d);},workspace);
        check(got==ref.bfs(0,false),"workspace recovers after sink throws");
    }
    throws([&]{bfs(g,n,[](auto,auto){});},"invalid source");
    throws([&]{dfs(g,n,[](auto,auto){},[](auto){});},"invalid DFS source");
}
void verify(size_t n,std::vector<Edge> edges){++cases;std::reverse(edges.begin(),edges.end());backend<OutCsr32>(n,edges);backend<ByteGraph32>(n,edges);backend<PackedCsr32>(n,edges);backend<EfCsr32>(n,edges);}
struct HoleOracle {
    uint64_t N() const{return 6;}
    bool valid(Vertex v) const{return v<6 && v!=2;}
    int outgoing(Vertex u,Vertex* out) const{
        if(u==0){out[0]=3;out[1]=1;return 2;}if(u==1){out[0]=4;return 1;}if(u==3){out[0]=0;return 1;}return 0;
    }
};
void native_test(){
    HoleOracle oracle;OracleView<HoleOracle> g(oracle);
    std::vector<Event> got;bfs(g,0,[&](Vertex v,uint64_t d){got.emplace_back('B',v,d);},true);
    check(got==std::vector<Event>{{'B',0,0},{'B',3,1},{'B',1,1},{'B',4,2},{'B',5,0}},"native order and invalid ID holes in BFS forest");
    got.clear();dfs(g,0,[&](Vertex v,Vertex p){got.emplace_back('D',v,p);},[&](Vertex v){got.emplace_back('F',v,0);},true);
    check(got==std::vector<Event>{{'D',0,no_vertex},{'D',3,0},{'F',3,0},{'D',1,0},{'D',4,1},{'F',4,0},{'F',1,0},{'F',0,0},{'D',5,no_vertex},{'F',5,0}},"native DFS resumable order and holes");
    throws([&]{bfs(g,2,[](auto,auto){});},"native invalid source");
}
template<class Graph> void invalid(){
    throws([]{Graph g(0,{{0,0}});},"edge in empty graph");throws([]{Graph g(2,{{0,2}});},"endpoint bounds");
    throws([]{Graph g(2,{{0,1},{0,1}});},"duplicate rejected");throws([]{Graph g(uint64_t(UINT32_MAX)+2,{});},"32-bit universe limit before allocation");
}
int main(){
    try{
        scratch=std::filesystem::temp_directory_path()/("sega-compact-test-"+std::to_string(::getpid()));
        if(!std::filesystem::create_directory(scratch))throw std::runtime_error("scratch already exists");
        invalid<OutCsr32>();invalid<ByteGraph32>();invalid<PackedCsr32>();invalid<EfCsr32>();native_test();
        {
            BfsWorkspace<> w;OutCsr32 small(65,{}),large(4097,{});
            bfs(small,0,[](auto,auto){},w);
            const auto old_bitmap=w.visited.capacity()*8;
            auto st=bfs(large,0,[](auto,auto){},w);
            check(st.aux_peak_bytes()>=st.aux_fixed_bytes+old_bitmap,"workspace bitmap resize overlap is counted");
            auto shrunk=bfs(small,0,[](auto,auto){},w);
            check(shrunk.aux_fixed_bytes==st.aux_fixed_bytes,"workspace retained capacity is counted after smaller query");
        }

        for(size_t n=0;n<=3;++n)for(uint64_t mask=0;mask<(uint64_t(1)<<(n*n));++mask){
            std::vector<Edge> edges;for(size_t u=0;u<n;++u)for(size_t v=0;v<n;++v)if((mask>>(u*n+v))&1)edges.emplace_back(u,v);verify(n,edges);
        }
        std::mt19937_64 rng(20261026);
        for(unsigned i=0;i<120;++i){size_t n=4+rng()%80;std::set<Edge> e;for(size_t k=0;k<n*5;++k)e.emplace(rng()%n,rng()%n);verify(n,{e.begin(),e.end()});}
        for(size_t n:{size_t(31),size_t(64),size_t(127),size_t(128),size_t(129),size_t(257),size_t(16385)}){
            verify(n,{});std::vector<Edge> star,path;
            for(Vertex v=0;v<n;++v){star.emplace_back(0,v);if(v)path.emplace_back(v-1,v);}verify(n,star);verify(n,path);
        }
        // Cross the 1/2/3/4-byte varint boundaries without allocating 2^28 rows.
        for(uint64_t n:{uint64_t(128),uint64_t(129),uint64_t(16384),uint64_t(16385),uint64_t(2097153)}){
            ByteGraph32 g(n,{{0,n-1}});auto c=g.neighbors(0);Vertex v;check(g.next(c,v)&&v==n-1&&!g.next(c,v),"varint width boundary");
        }
        std::filesystem::remove_all(scratch);
        std::cout<<"{\"status\":\""<<(failures?"failed":"ok")<<"\",\"graph_cases\":"<<cases<<",\"checks\":"<<checks<<",\"failures\":"<<failures<<"}\n";return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
