#include "sega/general_graph.hpp"
#include "sega/general_traversal.hpp"
#include "benchmark_driver.hpp" // shared JSON, timing, RSS and event encoding
#include <fstream>
#include <filesystem>
#include <random>
#include <set>

namespace {
using namespace sega;
namespace metrics = sega_bench;
struct Options {
    std::string backend="wavelet", traversal="bfs", shape="tiny", edges, events;
    uint64_t vertices=4, source=0, seed=1;
    unsigned id_bits=32;
    bool forest=false, hash=true, audit=false;
};
Options parse(int argc,char** argv) {
    Options o; std::set<std::string> seen;
    for(int i=1;i<argc;++i) {
        std::string key=argv[i];
        if(!seen.insert(key).second) throw std::invalid_argument("duplicate option: "+key);
        if(key=="--forest") {o.forest=true;continue;}
        if(key=="--audit") {o.audit=true;continue;}
        if(i+1==argc) throw std::invalid_argument("missing value: "+key);
        std::string value=argv[++i];
        if(key=="--backend") o.backend=value;
        else if(key=="--traversal") o.traversal=value;
        else if(key=="--shape") o.shape=value;
        else if(key=="--edges") o.edges=value;
        else if(key=="--events") o.events=value;
        else if(key=="--vertices") o.vertices=metrics::uint_arg(value,"vertices");
        else if(key=="--source") o.source=metrics::uint_arg(value,"source");
        else if(key=="--seed") o.seed=metrics::uint_arg(value,"seed");
        else if(key=="--id-bits") {
            auto bits=metrics::uint_arg(value,"id-bits");
            if(bits!=32 && bits!=64) throw std::invalid_argument("id-bits must be 32 or 64");
            o.id_bits=unsigned(bits);
        } else if(key=="--verify" && (value=="hash" || value=="none")) o.hash=value=="hash";
        else throw std::invalid_argument("unknown option or invalid value: "+key);
    }
    if(o.backend!="csr32" && o.backend!="csr64" && o.backend!="wavelet" && o.backend!="wavelet-rrr")
        throw std::invalid_argument("backend must be csr32, csr64, wavelet or wavelet-rrr");
    if(o.traversal!="bfs" && o.traversal!="dfs") throw std::invalid_argument("traversal must be bfs or dfs");
    if(o.forest && seen.count("--source")) throw std::invalid_argument("choose --forest or --source");
    if(seen.count("--edges") && (o.edges.empty() || seen.count("--shape") || !seen.count("--vertices")))
        throw std::invalid_argument("--edges requires --vertices and cannot accompany --shape");
    if(!o.hash && (o.audit || !o.events.empty())) throw std::invalid_argument("audit/events require --verify hash");
    if(!o.events.empty() && std::filesystem::exists(o.events)) throw std::invalid_argument("event output already exists; choose a new path");
    if(o.vertices>=uint64_t(INT64_MAX)) throw std::length_error("vertices outside supported range");
    if(!o.forest && o.source>=o.vertices) throw std::out_of_range("source outside [0, vertices); use --forest for empty graph");
    return o;
}
std::vector<Edge> input(const Options& o) {
    std::vector<Edge> edges;
    if(!o.edges.empty()) {
        std::ifstream in(o.edges);
        if(!in) throw std::runtime_error("cannot open edge list: "+o.edges);
        std::string line; size_t row=0;
        while(std::getline(in,line)) {
            ++row; std::istringstream fields(line); std::string a,b,extra;
            if(!(fields>>a) || a[0]=='#') continue;
            if(!(fields>>b) || (fields>>extra)) throw std::invalid_argument("expected exactly two IDs on edge-list line "+std::to_string(row));
            edges.emplace_back(metrics::uint_arg(a,"edge source"),metrics::uint_arg(b,"edge target"));
        }
        if(in.bad()) throw std::runtime_error("edge-list read failed");
    } else if(o.shape=="tiny") {
        if(o.vertices!=4) throw std::invalid_argument("tiny example requires --vertices 4");
        edges={{0,1},{0,3},{1,3},{3,0}};
    } else if(o.shape=="path" || o.shape=="cycle") {
        for(Vertex v=1;v<o.vertices;++v) edges.emplace_back(v-1,v);
        if(o.shape=="cycle" && o.vertices) edges.emplace_back(o.vertices-1,0);
    } else if(o.shape=="star-out" || o.shape=="star-in") {
        for(Vertex v=1;v<o.vertices;++v) edges.push_back(o.shape=="star-out" ? Edge{0,v} : Edge{v,0});
    } else if(o.shape=="disconnected") {
        for(Vertex v=1;v<o.vertices;++v) if(v%4) edges.emplace_back(v-1,v);
    } else if(o.shape=="random") {
        if(o.vertices>10000000) throw std::invalid_argument("synthetic random generator is limited to 10M vertices");
        std::mt19937_64 rng(o.seed); std::set<Edge> unique;
        for(uint64_t i=0;i<o.vertices*8;++i) if(o.vertices) unique.emplace(rng()%o.vertices,rng()%o.vertices);
        edges.assign(unique.begin(),unique.end());
    } else if(o.shape!="empty") throw std::invalid_argument("unknown synthetic shape");
    return edges;
}
template<class Graph> std::string audit(const Graph& g) {
    sdbgt::Sha256 hash;
    auto integer=[&](uint64_t x){uint8_t b[8];for(unsigned i=0;i<8;++i)b[i]=uint8_t(x>>(8*i));hash.update(b,8);};
    integer(g.num_vertices()); integer(g.num_edges());
    uint64_t pos=0,in_count=0;
    for(Vertex u=0;u<g.num_vertices();++u) {
        if(g.out_begin(u)!=pos || g.out_end(u)<pos || g.out_end(u)>g.num_edges()) throw std::runtime_error("audit: invalid row boundaries");
        Vertex prev=no_vertex;
        for(;pos<g.out_end(u);++pos) {
            auto v=g.target(pos);
            if(v>=g.num_vertices() || (prev!=no_vertex && prev>=v) || g.source(pos)!=u)
                throw std::runtime_error("audit: invalid outgoing navigation");
            auto rank=g.incoming_rank(pos);
            if(rank>=g.indegree(v) || g.incoming(v,rank)!=u) throw std::runtime_error("audit: incoming rank does not recover source");
            integer(u);integer(v);prev=v;
        }
        prev=no_vertex;
        for(uint64_t k=0;k<g.indegree(u);++k) {
            auto v=g.incoming(u,k);
            if(v>=g.num_vertices() || (prev!=no_vertex && prev>=v)) throw std::runtime_error("audit: invalid incoming neighbors");
            prev=v; ++in_count;
        }
    }
    if(pos!=g.num_edges() || in_count!=pos) throw std::runtime_error("audit: edge count mismatch");
    return hash.hex_digest();
}
template<class Index,class Graph>
TraversalStats traverse(const Graph& g,const Options& o,sdbgt::EventRecorder* recorder) {
    auto bfs_emit=[&](Vertex v,uint64_t d){if(recorder)recorder->bfs(v,d);};
    auto discover=[&](Vertex v,Vertex p){if(recorder)recorder->discover(v,p);};
    auto finish=[&](Vertex v){if(recorder)recorder->finish(v);};
    if(o.traversal=="bfs")return bfs<Index>(g,o.source,bfs_emit,o.forest);
    return dfs<Index>(g,o.source,discover,finish,o.forest);
}
template<class Graph> int run(const Options& o,std::vector<Edge> edges,double parse_ms) {
    const long peak_parse=metrics::peak_kb();
    auto start=metrics::Clock::now(); Graph g(o.vertices,std::move(edges));
    const double build_ms=metrics::milliseconds(start);
    const long peak_build=metrics::peak_kb(),rss_build=metrics::rss_kb();
    start=metrics::Clock::now(); const std::string topology=o.audit?audit(g):"";
    const double audit_ms=o.audit?metrics::milliseconds(start):0;
    const auto storage=g.storage();
    TraversalStats st; std::string digest; uint64_t count=0,bytes=0;
    start=metrics::Clock::now();
    if(o.hash) {
        sdbgt::EventRecorder recorder(o.events,true,g.num_vertices());
        st=o.id_bits==32?traverse<uint32_t>(g,o,&recorder):traverse<uint64_t>(g,o,&recorder);
        recorder.close();digest=recorder.digest_hex();count=recorder.event_count();bytes=recorder.output_bytes();
    } else st=o.id_bits==32?traverse<uint32_t>(g,o,nullptr):traverse<uint64_t>(g,o,nullptr);
    const double wall=metrics::milliseconds(start);
    metrics::Json j;
    j.number("schema_version",1);j.string("status","ok");j.string("suite","general-graph-prototype");
    j.string("backend",o.backend);j.string("traversal",o.traversal);j.string("shape",o.edges.empty()?o.shape:"edge-list");
    j.number("vertices",g.num_vertices());j.number("edges",g.num_edges());j.number("seed",o.seed);
    j.boolean("forest",o.forest);if(o.forest)j.raw("source","null");else j.number("source",o.source);
    j.number("id_bits",st.id_bits);j.number("visited",st.visited);j.number("finished",st.finished);j.number("roots",st.roots);
    j.number("arcs_scanned",st.arcs_scanned);j.number("max_pending",st.max_pending);j.number("max_depth",st.max_depth);
    j.number("graph_encoded_bytes",storage.total());j.number("targets_encoded_bytes",storage.targets);
    j.number("boundaries_encoded_bytes",storage.boundaries);j.number("incoming_encoded_bytes",storage.incoming);
    j.string("graph_bytes_definition","sum of SDSL serialized component sizes, plus vertex count; not RSS");
    j.number("aux_fixed_bytes",st.aux_fixed_bytes);j.number("aux_dynamic_peak_bytes",st.aux_dynamic_peak_bytes);
    j.number("aux_peak_bytes",st.aux_peak_bytes());j.string("aux_bytes_definition","requested live allocations; excludes graph, sink, allocator overhead and stack locals");
    j.number("parse_ms",parse_ms);j.number("build_ms",build_ms);j.number("audit_ms",audit_ms);j.number("traversal_ms",wall);
    j.number("peak_after_parse_kib",peak_parse);j.number("peak_after_build_kib",peak_build);j.number("rss_after_build_kib",rss_build);
    j.number("process_peak_kib",metrics::peak_kb());j.number("rss_after_traversal_kib",metrics::rss_kb());
    j.string("peak_definition","process lifetime high-water mark including construction; not isolated traversal peak");
    j.string("verification",o.hash?"sha256":"none");j.number("event_count",count);j.number("event_file_bytes",bytes);
    if(o.hash)j.string("event_sha256",digest);else j.raw("event_sha256","null");
    if(o.audit)j.string("topology_sha256",topology);else j.raw("topology_sha256","null");
    j.string("sdsl_revision",SEGA_SDSL_REVISION);j.string("source_sha256",SEGA_SOURCE_SHA256);j.string("build_flags",SEGA_BUILD_FLAGS);
    std::cout<<j.str()<<'\n';return 0;
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--help") {
            std::cout<<"general_graph_bench --backend csr32|csr64|wavelet|wavelet-rrr --traversal bfs|dfs\n"
                "  [--shape tiny|empty|path|cycle|star-out|star-in|disconnected|random --vertices N]\n"
                "  OR --edges FILE --vertices N (two unsigned IDs per line; # comment lines)\n"
                "  [--source ID | --forest] [--id-bits 32|64] [--seed N]\n"
                "  [--verify hash|none] [--audit] [--events FILE]\n";return 0;
        }
        auto o=parse(argc,argv);auto start=metrics::Clock::now();auto edges=input(o);
        double parse_ms=metrics::milliseconds(start);
        if(o.backend=="csr32")return run<CsrGraph<uint32_t>>(o,std::move(edges),parse_ms);
        if(o.backend=="csr64")return run<CsrGraph<uint64_t>>(o,std::move(edges),parse_ms);
        if(o.backend=="wavelet-rrr")return run<WaveletGraph<true>>(o,std::move(edges),parse_ms);
        return run<WaveletGraph<false>>(o,std::move(edges),parse_ms);
    } catch(const std::exception& e) {
        metrics::Json j;j.number("schema_version",1);j.string("status","failed");j.string("error",e.what());
        std::cout<<j.str()<<'\n';std::cerr<<"General graph experiment failed: "<<e.what()<<'\n';return 1;
    }
}
