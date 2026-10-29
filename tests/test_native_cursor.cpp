#include "megahit_oracle.hpp"
#include "sega/oracle_view.hpp"
#include "sega/general_traversal.hpp"
#include "graph_reference.hpp"
int main(int argc,char** argv){
    try{
        if(argc!=2)throw std::runtime_error("expected generated fixture prefix");
        SDBG graph;graph.LoadFromFile(argv[1]);sdbgt::MegaHitOracle oracle(graph);sega::OracleView<sdbgt::MegaHitOracle> view(oracle);
        std::vector<Edge> edges;for(Vertex u=0;u<16;++u)for(Vertex c=0;c<4;++c)edges.emplace_back(u,(u%4)*4+c);
        Reference ref(16,edges);for(auto& row:ref.out)std::reverse(row.begin(),row.end());
        for(Vertex source=0;source<16;++source)for(bool forest:{false,true}){
            std::vector<Event> got;
            auto b=bfs(view,source,[&](Vertex v,uint64_t d){got.emplace_back('B',v,d);},forest);
            check(got==ref.bfs(source,forest) && b.visited==16 && b.arcs_scanned==64,"native MEGAHIT BFS exact reference");
            got.clear();auto d=dfs(view,source,[&](Vertex v,Vertex p){got.emplace_back('D',v,p);},[&](Vertex v){got.emplace_back('F',v,0);},forest);
            check(got==ref.dfs(source,forest) && d.visited==16 && d.arcs_scanned==64,"native MEGAHIT DFS exact reference");
        }
        std::cout<<"native MEGAHIT cursor: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
