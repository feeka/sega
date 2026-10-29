#include <sega/sega.hpp>
#include <iostream>
int main(){
    sega::EfCsr32 graph(6,{{0,1},{0,2},{1,3},{2,3},{3,4}});
    sega::BfsWorkspace<> workspace;
    const auto stats=sega::bfs(graph,0,[](sega::Vertex v,uint64_t distance){
        std::cout<<v<<" "<<distance<<"\n";
    },workspace);
    const auto size=graph.storage();
    std::cout<<"storage_bytes="<<size.total()<<" payload_capacity_bytes="<<graph.resident_payload_bytes()
             <<" traversal_allocated_peak_bytes="<<stats.aux_peak_bytes()<<"\n";
    return stats.visited==5 && stats.arcs_scanned==5?0:1;
}
