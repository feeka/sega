// Same small C ABI as the GFA adapter, backed exclusively by MEGAHIT SDBG.
#include "megahit_unitig.hpp"
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace { thread_local std::string error; }
extern "C" {
const char* sega_assembly_error() { return error.c_str(); }
void* sega_assembly_open(const char* prefix) {
    error.clear();
    try { if (!prefix) throw std::invalid_argument("null graph prefix"); return new sega::MegaHitUnitigs(prefix); }
    catch (const std::exception& e) { error=e.what(); return nullptr; }
}
void sega_assembly_close(void* graph) { delete static_cast<sega::MegaHitUnitigs*>(graph); }
void sega_assembly_free(void* text) { std::free(text); }
char* sega_assembly_request(void* graph,const char* operation,const char* argument,uint64_t v,int reverse) {
    error.clear();
    try {
        if (!graph || !operation) throw std::invalid_argument("null graph or operation");
        const auto& g=*static_cast<sega::MegaHitUnitigs*>(graph);
        std::string op(operation); std::ostringstream out;
        if (op=="lookup") {
            if (!argument) throw std::invalid_argument("null node name"); out<<g.lookup(argument);
        } else if (op=="node") {
            out<<"{\"name\":\""<<g.name(v)<<"\",\"sequence\":\""<<g.sequence(v)<<"\",\"KC\":"<<g.coverage(v)<<'}';
        } else if (op=="neighbors") {
            out<<'['; bool first=true;
            for (auto e:g.neighbors(v,reverse!=0)) {
                if (!first) out<<','; first=false;
                out<<'['<<e.node<<','<<e.overlap<<','<<e.weight<<']';
            }
            out<<']';
        } else if (op=="stats") {
            out<<"{\"backend\":\"megahit_sdbg\",\"k\":"<<g.k()<<",\"segments\":"<<g.num_vertices()/2
               <<",\"vertices\":"<<g.num_vertices()<<",\"arcs\":"<<g.num_edges()
               <<",\"stored_edge_ids\":"<<g.stored_edges()<<",\"valid_native_edges\":"<<g.valid_edges()
               <<",\"circular_unitigs\":"<<g.cycles()<<",\"palindromic_unitigs\":"<<g.palindromes()
               <<",\"reverse_adjacency_bytes\":0,\"additional_stored_dna_bytes\":0,\"unitig_index_capacity_bytes\":"<<g.index_bytes()
               <<",\"construction_seen_bitmap_bytes\":"<<g.scratch_bytes()
               <<",\"per_edge_multiplicity_released\":true,\"note\":\"Index sizes exclude SDBG storage and Python caches; use RSS for total memory.\"}";
        } else if (op=="bounds") {
            const auto& u=g.unitig(v);
            out<<"{\"begin\":"<<(v%2?u.rbegin:u.begin)<<",\"end\":"<<(v%2?u.rend:u.end)
               <<",\"edge_count\":"<<u.length<<",\"KC\":"<<u.depth<<",\"cycle\":"<<(u.cycle?"true":"false")<<'}';
        } else if (op=="native_path" || op=="native_neighbors") {
            auto ids=op=="native_path"?g.native_path(v):g.native_neighbors(v,reverse!=0);
            out<<'['; for (size_t i=0;i<ids.size();++i) { if (i) out<<','; out<<ids[i]; } out<<']';
        } else if (op=="native_sequence") {
            out<<'"'<<g.edge_sequence(v)<<'"';
        } else if (op=="gfa_sha256") {
            out<<'"'<<g.gfa_digest()<<'"';
        } else if (op=="export_gfa") {
            if (!argument || !*argument) throw std::invalid_argument("GFA output path is required");
            g.export_gfa(argument); out<<"{\"exported\":true}";
        } else throw std::invalid_argument("unknown MEGAHIT adapter operation");
        auto value=out.str(); auto result=static_cast<char*>(std::malloc(value.size()+1));
        if (!result) throw std::bad_alloc();
        std::memcpy(result,value.c_str(),value.size()+1); return result;
    } catch (const std::exception& e) { error=e.what(); return nullptr; }
}
}
