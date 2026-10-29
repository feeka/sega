#include "sega/packed_graph.hpp"
#include "sega/general_traversal.hpp"
#include "benchmark_driver.hpp"
#include <random>
#include <set>
namespace {
using namespace sega;
namespace metrics=sega_bench;
struct Options {
    std::string mode,backend="csr32-out",graph,edges,shape="tiny",traversal="bfs",events;
    uint64_t n=4,seed=1,source=0,radius=UINT64_MAX,queries=1;
    bool forest=false,hash=true,audit=false;
};
Options parse(int argc,char** argv){
    if(argc<2)throw std::invalid_argument("expected build or query; see --help");
    Options o;o.mode=argv[1];std::set<std::string> seen;
    if(o.mode!="build" && o.mode!="query")throw std::invalid_argument("expected build or query");
    for(int i=2;i<argc;++i){
        std::string k=argv[i];if(!seen.insert(k).second)throw std::invalid_argument("duplicate option: "+k);
        if(k=="--forest"){o.forest=true;continue;}if(k=="--audit"){o.audit=true;continue;}
        if(i+1==argc)throw std::invalid_argument("missing value: "+k);
        std::string v=argv[++i];
        if(k=="--backend")o.backend=v;else if(k=="--graph")o.graph=v;
        else if(k=="--edges")o.edges=v;else if(k=="--shape")o.shape=v;
        else if(k=="--vertices")o.n=metrics::uint_arg(v,"vertices");
        else if(k=="--seed")o.seed=metrics::uint_arg(v,"seed");
        else if(k=="--source")o.source=metrics::uint_arg(v,"source");
        else if(k=="--radius")o.radius=metrics::uint_arg(v,"radius");
        else if(k=="--queries")o.queries=metrics::uint_arg(v,"queries");
        else if(k=="--traversal")o.traversal=v;else if(k=="--events")o.events=v;
        else if(k=="--verify" && (v=="hash" || v=="none"))o.hash=v=="hash";
        else throw std::invalid_argument("unknown option or invalid value: "+k);
    }
    if(o.graph.empty())throw std::invalid_argument("--graph FILE is required");
    if(o.backend!="csr32-out" && o.backend!="byte32" && o.backend!="packed32" && o.backend!="ef32")throw std::invalid_argument("backend must be csr32-out, byte32, packed32 or ef32");
    if(o.mode=="build"){
        for(const char* k:{"--forest","--audit","--source","--radius","--queries","--traversal","--events","--verify"})
            if(seen.count(k))throw std::invalid_argument(std::string(k)+" is a query option");
        if(std::filesystem::exists(o.graph))throw std::invalid_argument("graph output exists; choose a new path");
        if(o.n>uint64_t(UINT32_MAX)+1)throw std::invalid_argument("vertices exceed 32-bit IDs");
        if(seen.count("--edges") && (o.edges.empty() || !seen.count("--vertices") || seen.count("--shape") || seen.count("--seed")))
            throw std::invalid_argument("--edges requires --vertices and excludes --shape/--seed");
    }else{
        for(const char* k:{"--vertices","--shape","--edges","--seed"})
            if(seen.count(k))throw std::invalid_argument(std::string(k)+" is a build option");
        if(o.traversal!="bfs" && o.traversal!="dfs")throw std::invalid_argument("traversal must be bfs or dfs");
        if(o.forest && seen.count("--source"))throw std::invalid_argument("choose --source or --forest");
        if(seen.count("--radius") && (o.forest || o.traversal!="bfs"))throw std::invalid_argument("radius requires single-source BFS");
        if(!o.queries || o.queries>100000 || (o.queries!=1 && o.traversal!="bfs"))throw std::invalid_argument("queries must be 1..100000; multiple queries require BFS");
        if(!o.hash && (o.audit || !o.events.empty()))throw std::invalid_argument("audit/events require --verify hash");
        if(!o.events.empty() && std::filesystem::exists(o.events))throw std::invalid_argument("event output exists; choose a new path");
    }
    return o;
}
std::vector<Edge> input(const Options& o){
    std::vector<Edge> edges;
    if(!o.edges.empty()){
        std::ifstream in(o.edges);if(!in)throw std::runtime_error("cannot open edge list");
        std::string line;uint64_t row=0;
        while(std::getline(in,line)){
            ++row;std::istringstream f(line);std::string a,b,extra;
            if(!(f>>a) || a[0]=='#')continue;
            if(!(f>>b) || (f>>extra))throw std::invalid_argument("expected two IDs on line "+std::to_string(row));
            edges.emplace_back(metrics::uint_arg(a,"edge source"),metrics::uint_arg(b,"edge target"));
        }
        if(in.bad())throw std::runtime_error("edge-list read failed");
    }else if(o.shape=="tiny"){
        if(o.n!=4)throw std::invalid_argument("tiny requires 4 vertices");edges={{0,1},{0,3},{1,3},{3,0}};
    }else if(o.shape=="random"){
        if(o.n>10000000)throw std::invalid_argument("synthetic generator limited to 10M vertices");
        edges.reserve(o.n*8);std::mt19937_64 rng(o.seed);
        for(uint64_t i=0;i<o.n*8;++i){const auto u=rng()%o.n;const auto v=rng()%o.n;edges.emplace_back(u,v);}
        std::sort(edges.begin(),edges.end());edges.erase(std::unique(edges.begin(),edges.end()),edges.end());
    }else if(o.shape=="clustered"){
        if(o.n>10000000)throw std::invalid_argument("synthetic generator limited to 10M vertices");
        std::mt19937_64 rng(o.seed);edges.reserve(o.n*8);
        for(Vertex u=0;u<o.n;++u){
            const Vertex begin=(u/256)*256,end=std::min<uint64_t>(o.n,begin+256);
            for(unsigned i=0;i<8;++i)edges.emplace_back(u,begin+rng()%(end-begin));
        }
        std::sort(edges.begin(),edges.end());edges.erase(std::unique(edges.begin(),edges.end()),edges.end());
    }else if(o.shape=="dense"){
        if(o.n>2048)throw std::invalid_argument("dense generator limited to 2048 vertices");
        edges.reserve(o.n*o.n);
        for(Vertex u=0;u<o.n;++u)for(Vertex v=0;v<o.n;++v)edges.emplace_back(u,v);
    }else if(o.shape=="path" || o.shape=="cycle" || o.shape=="disconnected"){
        for(Vertex v=1;v<o.n;++v)if(o.shape!="disconnected" || v%4)edges.emplace_back(v-1,v);
        if(o.shape=="cycle" && o.n)edges.emplace_back(o.n-1,0);
    }else if(o.shape=="star-out" || o.shape=="star-in"){
        for(Vertex v=1;v<o.n;++v)edges.push_back(o.shape=="star-out"?Edge{0,v}:Edge{v,0});
    }else if(o.shape!="empty")throw std::invalid_argument("unknown synthetic shape");
    return edges;
}
template<class Graph> std::string topology(const Graph& g){
    sdbgt::Sha256 hash;uint8_t b[8];auto word=[&](uint64_t x){graph_io::put(b,x);hash.update(b,8);};
    word(g.num_vertices());word(g.num_edges());uint64_t count=0;
    for(Vertex u=0;u<g.num_vertices();++u){auto c=g.neighbors(u);Vertex v,prev=no_vertex;
        while(g.next(c,v)){
            if(v>=g.num_vertices() || (prev!=no_vertex && v<=prev))throw std::runtime_error("outgoing topology audit failed");
            word(u);word(v);prev=v;++count;
        }
    }
    if(count!=g.num_edges())throw std::runtime_error("topology count mismatch");return hash.hex_digest();
}
template<class Graph> int run(const Options& o){
    metrics::Json j;j.number("schema_version",1);j.string("status","ok");j.string("suite","compact-cursor-api");
    j.string("mode",o.mode);j.string("backend",o.backend);j.string("graph_file",o.graph);
    j.string("source_sha256",SEGA_SOURCE_SHA256);j.string("build_flags",SEGA_BUILD_FLAGS);
    const auto start=metrics::Clock::now();
    if(o.mode=="build"){
        auto edges=input(o);j.number("input_ms",metrics::milliseconds(start));
        auto t=metrics::Clock::now();Graph g(o.n,std::move(edges));j.number("build_ms",metrics::milliseconds(t));
        t=metrics::Clock::now();g.save(o.graph);j.number("save_ms",metrics::milliseconds(t));
        j.number("vertices",g.num_vertices());j.number("edges",g.num_edges());
        j.number("graph_file_bytes",std::filesystem::file_size(o.graph));j.number("graph_payload_capacity_bytes",g.resident_payload_bytes());
        j.string("shape",o.edges.empty()?o.shape:"edge-list");j.number("seed",o.seed);
        j.number("process_peak_kib",metrics::peak_kb());j.number("total_ms",metrics::milliseconds(start));
        j.string("peak_definition","builder lifetime including input generation, sorting, construction and saving");
    }else{
        Graph g=Graph::load(o.graph);j.number("load_ms",metrics::milliseconds(start));
        j.number("rss_after_load_kib",metrics::rss_kb());j.number("peak_after_load_kib",metrics::peak_kb());
        j.number("vertices",g.num_vertices());j.number("edges",g.num_edges());j.number("graph_file_bytes",g.storage().total());
        j.number("graph_payload_capacity_bytes",g.resident_payload_bytes());
        j.number("target_bytes",g.storage().targets);j.number("boundary_bytes",g.storage().boundaries);
        j.number("format_bytes",g.storage().metadata);j.number("cursor_bytes",sizeof(typename Graph::Cursor));
        auto t=metrics::Clock::now();const auto digest=o.audit?topology(g):"";j.number("audit_ms",o.audit?metrics::milliseconds(t):0);
        if(o.audit)j.string("topology_sha256",digest);else j.raw("topology_sha256","null");
        std::unique_ptr<BfsWorkspace<uint32_t>> workspace;TraversalStats st;
        if(o.traversal=="bfs")workspace=std::make_unique<BfsWorkspace<uint32_t>>();
        std::unique_ptr<sdbgt::EventRecorder> recorder;
        if(o.hash)recorder=std::make_unique<sdbgt::EventRecorder>(o.events,true,g.num_vertices());
        t=metrics::Clock::now();
        for(uint64_t q=0;q<o.queries;++q){
            if(o.traversal=="bfs")st=bfs(g,o.source,[&](Vertex v,uint64_t d){if(recorder)recorder->bfs(v,d);},*workspace,o.forest,o.radius);
            else st=dfs(g,o.source,[&](Vertex v,Vertex p){if(recorder)recorder->discover(v,p);},[&](Vertex v){if(recorder)recorder->finish(v);},o.forest);
        }
        if(recorder)recorder->close();
        j.number("traversal_ms",metrics::milliseconds(t));j.number("queries",o.queries);j.string("traversal",o.traversal);
        j.boolean("forest",o.forest);j.number("source",o.source);
        if(o.radius==UINT64_MAX)j.raw("radius","null");else j.number("radius",o.radius);
        j.number("visited",st.visited);j.number("finished",st.finished);j.number("roots",st.roots);j.number("arcs_scanned",st.arcs_scanned);
        j.number("max_pending",st.max_pending);j.number("max_depth",st.max_depth);
        j.number("aux_fixed_bytes",st.aux_fixed_bytes);j.number("aux_dynamic_peak_bytes",st.aux_dynamic_peak_bytes);j.number("aux_peak_bytes",st.aux_peak_bytes());
        j.string("aux_definition","last query requested live bitmap/color and queue/stack allocations including retained capacity; excludes graph, sink, allocator overhead and stack locals");
        j.string("verification",o.hash?"sha256":"none");
        if(recorder)j.string("event_sha256",recorder->digest_hex());else j.raw("event_sha256","null");
        j.number("event_count",recorder?recorder->event_count():0);
        j.number("process_peak_kib",metrics::peak_kb());j.number("rss_after_query_kib",metrics::rss_kb());
        j.string("peak_definition","fresh query-process lifetime including loading, checksum and structural validation, optional audit and traversal; excludes graph construction");
    }
    std::cout<<j.str()<<'\n';return 0;
}
}
int main(int argc,char** argv){
    try{
        if(argc==2 && std::string(argv[1])=="--help"){
            std::cout<<"compact_graph_bench build --backend csr32-out|byte32|packed32|ef32 --graph NEWFILE\n"
                "  [--shape tiny|empty|path|cycle|star-out|star-in|disconnected|random|clustered|dense --vertices N --seed N]\n"
                "  OR --edges FILE --vertices N\n"
                "compact_graph_bench query --backend csr32-out|byte32|packed32|ef32 --graph FILE\n"
                "  [--traversal bfs|dfs] [--source ID | --forest] [--radius HOPS] [--queries N]\n"
                "  [--verify hash|none] [--audit] [--events NEWFILE]\n";return 0;
        }
        auto o=parse(argc,argv);
        if(o.backend=="csr32-out")return run<OutCsr32>(o);
        if(o.backend=="byte32")return run<ByteGraph32>(o);
        if(o.backend=="packed32")return run<PackedCsr32>(o);
        return run<EfCsr32>(o);
    }catch(const std::exception& e){metrics::Json j;j.string("status","failed");j.string("error",e.what());std::cout<<j.str()<<'\n';std::cerr<<e.what()<<'\n';return 1;}
}
