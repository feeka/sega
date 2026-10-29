// Tiny complete DNA de Bruijn graph serialized through MEGAHIT's own writer.
// k=1 gives four source labels and 16 two-symbol edges. Traversal identifiers
// are those 16 edges; (a,b) -> (b,c), c in {A,C,G,T}. This is a backend smoke
// fixture, not a biological benchmark.
#include "sdbg/sdbg_writer.h"
#include "sdbg/sdbg.h"
#include "megahit_oracle.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc,char** argv) {
    try {
        if(argc!=2){std::cerr<<"usage: generate_fixture <new_graph_prefix>\n";return 2;}
        std::string prefix=argv[1];
        if(std::filesystem::exists(prefix+".sdbg_info")||std::filesystem::exists(prefix+".sdbg.0"))
            throw std::runtime_error("fixture files already exist; choose a new prefix");
        auto parent=std::filesystem::path(prefix).parent_path();
        if(!parent.empty())std::filesystem::create_directories(parent);
        SdbgWriter writer;writer.set_num_threads(1);writer.set_num_buckets(4);
        writer.set_file_prefix(prefix);writer.set_kmer_size(1);writer.InitFiles();
        for(unsigned source=0;source<4;++source){
            SdbgWriter::Snapshot snapshot;
            for(unsigned target=0;target<4;++target){
                // First incoming occurrence gets unmarked W; later parallel
                // incoming occurrences get W+4, as required by BOSS navigation.
                uint8_t w=uint8_t(target+1+(source?4:0));
                writer.Write(0,source,w,uint8_t(target==3),0,1,nullptr,&snapshot);
            }
            writer.SaveSnapshot(snapshot);
        }
        writer.Finalize();
        SDBG graph;graph.LoadFromFile(prefix.c_str());sdbgt::MegaHitOracle oracle(graph);
        if(oracle.N()!=16||graph.k()!=1)throw std::runtime_error("fixture dimensions mismatch");
        for(uint64_t edge=0;edge<16;++edge){
            uint64_t out[4],in[4];int od=oracle.outgoing(edge,out),id=oracle.incoming(edge,in);
            if(!oracle.valid(edge)||od!=4||id!=4)throw std::runtime_error("fixture validity/degree mismatch");
            std::sort(out,out+od);std::sort(in,in+id);
            for(uint64_t c=0;c<4;++c)
                if(out[c]!=(edge%4)*4+c||in[c]!=c*4+edge/4)
                    throw std::runtime_error("fixture adjacency differs from independent (a,b)->(b,c) construction");
        }
        std::cout<<"generated and verified complete k=1 MEGAHIT fixture: N=16 arcs=64 prefix="<<prefix<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<"fixture error: "<<e.what()<<'\n';return 1;}
}
