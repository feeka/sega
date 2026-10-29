#include REPR_HEADER
#include <set>
#include <ctime>

using namespace repr;

// The process has one graph and one measurement. Input and trace I/O are
// outside timed traversal runs; auditing is a separate, explicitly marked run.
int main(int argc,char** argv) {
 try {
    check(argc>=5,"Usage: repr_probe INDEX K OP RESULT.json [QUERY.txt|-] [TRACE.tsv|-] [SOURCE_KMER|forest]");
    const unsigned k=sega_bench::uint_arg(argv[2],"K");
    check(k>=11 && k<=31 && k%2==1,"Shared benchmark requires odd K in [11,31]");
    const std::string op=argv[3];
    const std::set<std::string> allowed={"load","stats","components","scc","unitigs","lookup","neighbors","walk","bfs-base","bfs-base32","bfs-stream","bfs-stream32","dfs-base","dfs-base32","dfs-packed","dfs-packed32","dfs-se","audit"};
    check(allowed.count(op),"Unknown or excluded operation: "+op);
    std::vector<std::string> queries;
    if(argc>5 && std::string(argv[5])!="-") {
        std::ifstream in(argv[5]); check(bool(in),"Cannot open query file");
        std::string s; while(in>>s) { check(s.size()==k&&s.find_first_not_of("ACGT")==std::string::npos,"Invalid query"); queries.push_back(s); }
    }
    std::ofstream trace; bool tracing=argc>6 && std::string(argv[6])!="-";
    if(tracing) { trace.open(argv[6]); check(bool(trace),"Cannot open trace"); }
    auto start=Clock::now();auto cpu_start=std::clock(); Graph g(argv[1],k); double load_ms=milliseconds(start);
    double load_cpu_ms=1000.0*(std::clock()-cpu_start)/CLOCKS_PER_SEC;
    const auto loaded_rss=sega_bench::rss_kb();
    const auto load_peak=sega_bench::peak_kb();
    Json j; j.string("schema","sega-representations-v1"); j.string("backend",Graph::name);
    j.string("operation",op); j.string("navigation",Graph::navigation); j.number("k",k);
    j.number("identifier_slots",g.N()); j.number("load_and_adapter_ms",load_ms);
    j.number("load_and_adapter_cpu_ms",load_cpu_ms);
    j.number("loaded_process_rss_kib",loaded_rss); j.number("load_peak_process_rss_kib",load_peak);
    j.number("adapter_payload_bytes",g.adapter_bytes());
    j.string("adapter_accounting",std::string(Graph::name)=="bifrost"?"lower-bound-excludes-hash-allocator-overhead":"allocated-vector-payload-or-zero");
    j.boolean("trace_enabled",tracing);
    start=Clock::now();cpu_start=std::clock();
    if(op=="stats" || op=="audit") {
        uint64_t count=0,arcs=0,tips=0,isolated=0,branching=0,selfloops=0;
        std::array<uint64_t,5> ih{},oh{};
        for(node_t v=0;v<g.N();++v) if(g.valid(v)) {
            node_t out[4],in[4]; int no=g.outgoing(v,out),ni=g.incoming(v,in);
            check(no>=0&&no<=4&&ni>=0&&ni<=4,"Degree outside DNA alphabet bound");
            ++count; arcs+=no; ++ih[ni]; ++oh[no]; tips+=(no==0||ni==0); isolated+=(no==0&&ni==0); branching+=(no>1||ni>1);
            for(int t=0;t<no;++t) selfloops+=out[t]==v;
            if(op=="audit") {
                auto s=g.label(v); check(s.size()==k&&s.find_first_not_of("ACGT")==std::string::npos,"Invalid label");
                check(g.lookup(s)==v,"Lookup/access round trip failed");
                check(g.lookup(rc(s))!=kNoNode,"Reverse-complement vertex missing");
                check(tracing,"audit requires a trace file"); trace<<s<<'\t';
                for(int t=0;t<no;++t) { if(t) trace<<','; trace<<g.label(out[t]); }
                trace<<'\t'; for(int t=0;t<ni;++t) { if(t) trace<<','; trace<<g.label(in[t]); } trace<<'\n';
            }
        }
        auto arr=[](const auto& a){ std::string s="["; for(auto n:a) { if(s.size()>1)s+=',';s+=std::to_string(n); } return s+"]"; };
        j.number("oriented_kmers",count); j.number("canonical_kmers",count/2); j.number("arcs",arcs);
        j.number("tips_including_isolates",tips); j.number("isolates",isolated); j.number("branch_vertices",branching); j.number("self_loops",selfloops);
        j.raw("indegree_histogram",arr(ih)); j.raw("outdegree_histogram",arr(oh));
    } else if(op=="components") {
        // Union-find on native IDs; O(identifier universe) extra space is
        // explicit. No adjacency or reverse graph is materialized.
        std::vector<int64_t> parent(g.N(),0);
        uint64_t count=0,largest=0;
        for(node_t v=0;v<g.N();++v) if(g.valid(v)){parent[v]=-1;++count;}
        auto root=[&](node_t v){while(parent[v]>=0){auto w=node_t(parent[v]); if(parent[w]>=0)parent[v]=parent[w];v=w;}return v;};
        for(node_t v=0;v<g.N();++v) if(g.valid(v)) {
            node_t out[4];int n=g.outgoing(v,out);
            for(int t=0;t<n;++t){auto a=root(v),b=root(out[t]);if(a==b)continue;if(parent[a]>parent[b])std::swap(a,b);parent[a]+=parent[b];parent[b]=a;--count;}
        }
        for(auto n:parent)if(n<0)largest=std::max(largest,uint64_t(-n));
        j.number("weak_components",count);j.number("largest_weak_component_kmers",largest);j.number("auxiliary_payload_bytes",parent.capacity()*8);
    } else if(op=="scc") {
        std::vector<node_t> order; order.reserve(g.N()); uint64_t first_peak=0;
        {
            sdbgt::ChoiceDictionary colors(g.N()); sdbgt::TwoBitArray parents(g.N());
            auto st=sdbgt::se_dfs(g,kNoNode,colors,parents,[](node_t,node_t){},[&](node_t v){order.push_back(v);},true);
            first_peak=order.capacity()*sizeof(node_t)+st.aux_peak_bytes;
        }
        std::vector<uint64_t> seen((g.N()+63)/64,0);
        sdbgt::detail::HeapCounter heap;
        std::vector<node_t,sdbgt::detail::CountingAllocator<node_t>> stack{sdbgt::detail::CountingAllocator<node_t>(heap)};
        auto marked=[&](node_t v){return (seen[v/64]>>(v%64))&1;};
        auto mark=[&](node_t v){seen[v/64]|=uint64_t(1)<<(v%64);};
        uint64_t count=0,largest=0,singletons=0;
        for(auto it=order.rbegin();it!=order.rend();++it)if(!marked(*it)){
            ++count;uint64_t size=0;mark(*it);stack.push_back(*it);
            while(!stack.empty()){auto v=stack.back();stack.pop_back();++size;node_t in[4];int n=g.incoming(v,in);
                for(int t=0;t<n;++t)if(!marked(in[t])){mark(in[t]);stack.push_back(in[t]);}}
            largest=std::max(largest,size);singletons+=size==1;
        }
        j.number("strong_components",count);j.number("largest_strong_component_kmers",largest);j.number("singleton_strong_components",singletons);
        j.number("auxiliary_peak_bytes",std::max<uint64_t>(first_peak,order.capacity()*sizeof(node_t)+seen.capacity()*8+heap.peak));
        j.string("component_algorithm","Kosaraju via native incoming oracle; no stored reverse graph");
    } else if(op=="unitigs") {
        std::vector<uint64_t> seen((g.N()+63)/64,0);
        auto marked=[&](node_t v){return (seen[v/64]>>(v%64))&1;};
        auto mark=[&](node_t v){seen[v/64]|=uint64_t(1)<<(v%64);};
        uint64_t paths=0,cycles=0,isolates=0,max_arcs=0,total_arcs=0;
        for(node_t v=0;v<g.N();++v)if(g.valid(v)){
            int ni=g.indeg(v);node_t out[4];int no=g.outgoing(v,out);
            if(!ni&&!no){++isolates;continue;}
            if(ni==1&&no==1)continue;
            for(int t=0;t<no;++t){++paths;uint64_t length=1;node_t w=out[t];
                while(g.indeg(w)==1&&g.outdeg(w)==1){check(!marked(w),"Unitig interior repeated");mark(w);node_t next[4];g.outgoing(w,next);w=next[0];++length;}
                total_arcs+=length;max_arcs=std::max(max_arcs,length);
            }
        }
        for(node_t v=0;v<g.N();++v)if(g.valid(v)&&g.indeg(v)==1&&g.outdeg(v)==1&&!marked(v)){
            ++cycles;uint64_t length=0;node_t w=v;
            do{check(!marked(w),"Malformed isolated cycle");mark(w);++length;node_t next[4];g.outgoing(w,next);w=next[0];}while(w!=v);
            total_arcs+=length;max_arcs=std::max(max_arcs,length);
        }
        j.number("oriented_maximal_nonbranching_paths",paths);j.number("isolated_directed_cycles",cycles);j.number("isolates",isolates);
        j.number("partitioned_arcs",total_arcs);j.number("max_path_or_cycle_arcs",max_arcs);j.number("auxiliary_payload_bytes",seen.capacity()*8);
    } else if(op=="lookup"||op=="neighbors"||op=="walk") {
        uint64_t hits=0,steps=0,checksum=0;node_t out[4];
        for(const auto& s:queries){auto v=g.lookup(s);hits+=v!=kNoNode;
            if(tracing)trace<<s<<'\t'<<(v!=kNoNode);
            if(v!=kNoNode){checksum^=v;
                if(op=="neighbors") {int n=g.outgoing(v,out);steps+=n;if(tracing)trace<<'\t';for(int t=0;t<n;++t){checksum^=out[t];if(tracing){if(t)trace<<',';trace<<g.label(out[t]);}}
                    n=g.incoming(v,out);steps+=n;if(tracing)trace<<'\t';for(int t=0;t<n;++t){checksum^=out[t];if(tracing){if(t)trace<<',';trace<<g.label(out[t]);}}}
                if(op=="walk") for(unsigned t=0;t<100;++t){int n=g.outgoing(v,out);if(!n)break;
                    // A shared source must take the same walk in every index,
                    // regardless of native neighbor ordering. Label recovery
                    // at branches is part of this operation's measured cost.
                    if(n>1){std::pair<char,node_t> choices[4];for(int i=0;i<n;++i)choices[i]={g.label(out[i]).back(),out[i]};
                        std::sort(choices,choices+n);for(int i=0;i<n;++i)out[i]=choices[i].second;}
                    v=out[t%n];checksum^=v;++steps;}
            }
            if(tracing)trace<<'\n';
        }
        j.number("query_count",queries.size());j.number("hits",hits);j.number("neighbor_or_walk_steps",steps);j.number("id_checksum",checksum);
    } else if(op!="load") {
        sega_bench::Options o;o.method=op;o.forest=argc<=7||std::string(argv[7])=="forest";o.source=kNoNode;
        if(!o.forest){o.source=g.lookup(argv[7]);check(o.source!=kNoNode,"Source k-mer absent");}
        auto m=sega_bench::run_algorithm(g,o,
            [&](node_t v,uint64_t d){if(tracing)trace<<"B\t"<<g.label(v)<<'\t'<<d<<'\n';},
            [&](node_t v,node_t p){if(tracing)trace<<"D\t"<<g.label(v)<<'\t'<<(p==kNoNode?"-":g.label(p))<<'\n';},
            [&](node_t v){if(tracing)trace<<"F\t"<<g.label(v)<<'\n';});
        j.number("visited",m.visited);j.number("roots",m.roots);j.number("events",m.events);j.number("auxiliary_peak_bytes",m.aux_peak);
        j.number("auxiliary_fixed_bytes",m.aux_fixed);j.number("auxiliary_dynamic_peak_bytes",m.aux_dynamic);j.number("id_bits",m.id_bits);
        j.number("edges_scanned",m.edges);j.number("max_frontier",m.frontier);j.number("max_pending",m.pending);j.number("max_dfs_depth",m.depth);
        j.number("parent_recoveries",m.recoveries);j.number("incoming_calls",m.incoming_calls);j.number("child_scans",m.child_scans);j.number("recover_probes",m.probes);
        j.number("finished",m.finished);
        j.string("source",o.forest?"forest":argv[7]);
    }
    j.number("operation_ms",milliseconds(start));j.number("peak_process_rss_kib",sega_bench::peak_kb());
    j.number("operation_cpu_ms",1000.0*(std::clock()-cpu_start)/CLOCKS_PER_SEC);
    j.number("after_process_rss_kib",sega_bench::rss_kb());j.string("status","ok");
    if(tracing){trace.close();check(bool(trace),"Trace write failed");}
    std::ofstream result(argv[4]);result<<j.str()<<'\n';check(bool(result),"Result write failed");return 0;
 } catch(const std::exception& e){std::cerr<<"Representation probe failed: "<<e.what()<<'\n';return 2;}
}
