#include "megahit_unitig.hpp"
#include <iostream>
#include <set>

std::string reverse_complement(std::string s) {
    std::reverse(s.begin(),s.end());
    for (char& c:s) c="TGCA"[std::string("ACGT").find(c)];
    return s;
}
void check(bool condition,const char* what) { if (!condition) throw std::runtime_error(what); }
int main(int argc,char** argv) {
    try {
        if (argc!=2) throw std::invalid_argument("usage: test_megahit_unitig <graph prefix or directory>");
        sega::MegaHitUnitigs graph(argv[1]);
        std::set<uint64_t> covered;
        uint64_t arcs=0;
        for (uint64_t v=0;v<graph.num_vertices();++v) {
            auto sequence=graph.sequence(v);
            check(sequence.size()==graph.length(v),"unitig length");
            check(reverse_complement(sequence)==graph.sequence(v^1),"reverse complement spelling");
            check(graph.lookup(graph.name(v))==v,"unitig name roundtrip");
            auto path=graph.native_path(v);
            std::string independent;
            for (auto edge:path) {
                auto word=graph.edge_sequence(edge);
                if (independent.empty()) independent=word;
                else {
                    check(independent.substr(independent.size()-graph.k())==word.substr(0,graph.k()),"native overlap spelling");
                    independent+=word.back();
                }
                covered.insert(edge);
            }
            check(independent==sequence,"unitig equals native edge path");
            for (auto e:graph.neighbors(v)) {
                ++arcs;
                auto target=graph.sequence(e.node);
                check(sequence.substr(sequence.size()-graph.k())==target.substr(0,graph.k()),"unitig arc DNA overlap");
                check(e.overlap==graph.k() && e.weight==target.size()-graph.k(),"base-pair weight");
                bool found=false;
                for (auto incoming:graph.neighbors(e.node,true)) if (incoming.node==v) found=true;
                check(found,"incoming navigation inverse");
                found=false;
                for (auto reverse:graph.neighbors(e.node^1)) if (reverse.node==(v^1)) found=true;
                check(found,"reverse-complement adjacency symmetry");
            }
        }
        check(covered.size()==graph.valid_edges(),"every valid native edge represented");
        check(arcs==graph.num_edges(),"arc counts");
        bool rejected=false; try { graph.sequence(graph.num_vertices()); } catch (const std::out_of_range&) {rejected=true;}
        check(rejected,"invalid unitig rejected");
        std::cout<<"{\"status\":\"passed\",\"unitigs\":"<<graph.num_vertices()/2<<",\"native_edges\":"<<covered.size()<<",\"arcs\":"<<arcs<<"}\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
