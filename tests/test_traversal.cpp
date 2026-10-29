// Independent reference algorithms and exact event comparisons. Tests remain
// active in Release/NDEBUG builds. No result is inferred from counts alone.
#include "sdbg_traverse/bfs.hpp"
#include "sdbg_traverse/dfs.hpp"
#include "sdbg_traverse/validation.hpp"
#include <array>
#include <deque>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <tuple>

using namespace sdbgt;
using Edge=std::pair<node_t,node_t>;
using Event=std::tuple<char,node_t,uint64_t>;
static size_t failures=0, comparisons=0, cases=0;
static void check(bool ok,const std::string& what) {
    ++comparisons;
    if (!ok) {++failures;std::cerr<<"FAIL: "<<what<<'\n';}
}
template<class F> void expect_throw(F&& f,const std::string& what) {
    try {f();check(false,what);} catch(const std::exception&) {check(true,what);}
}

struct TestGraph {
    InMemoryOracle oracle;
    std::vector<std::vector<node_t>> adjacency;
    std::vector<bool> enabled;
    TestGraph(size_t n,const std::vector<Edge>& edges,const std::vector<node_t>& invalid={})
        :oracle(n,edges),adjacency(n),enabled(n,true) {
        for(auto v:invalid)enabled.at(v)=false;
        for(auto e:edges)adjacency.at(e.first).push_back(e.second);
        for(auto& row:adjacency) {
            std::sort(row.begin(),row.end());row.erase(std::unique(row.begin(),row.end()),row.end());
        }
    }
    size_t N() const{return adjacency.size();}
    bool valid(node_t v) const{return v<N()&&enabled[v];}
    int outgoing(node_t v,node_t* out) const{return oracle.outgoing(v,out);}
    int incoming(node_t v,node_t* in) const{return oracle.incoming(v,in);}
};

static std::vector<Event> reference_bfs(const TestGraph& g,node_t source,bool forest) {
    std::vector<Event> out;
    std::vector<uint64_t> dist(g.N(),UINT64_MAX);
    std::deque<node_t> todo;
    auto tree=[&](node_t root) {
        dist[root]=0;todo.push_back(root);
        while(!todo.empty()) {
            node_t v=todo.front();todo.pop_front();out.emplace_back('B',v,dist[v]);
            for(node_t w:g.adjacency[v])if(g.valid(w)&&dist[w]==UINT64_MAX){dist[w]=dist[v]+1;todo.push_back(w);}
        }
    };
    if(forest){for(node_t v=0;v<g.N();++v)if(g.valid(v)&&dist[v]==UINT64_MAX)tree(v);}
    else tree(source);
    return out;
}
static std::vector<Event> reference_dfs(const TestGraph& g,node_t source,bool forest) {
    std::vector<Event> out;
    std::vector<bool> visited(g.N(),false);
    std::function<void(node_t,node_t)> recursive=[&](node_t v,node_t parent){
        visited[v]=true;out.emplace_back('D',v,parent);
        for(node_t w:g.adjacency[v])if(g.valid(w)&&!visited[w])recursive(w,v);
        out.emplace_back('F',v,0);
    };
    // Independent explicit frames with a successor cursor avoid recursion
    // limits in the deep-chain test. Production code instead rescans successors.
    auto frames=[&](node_t root){
        struct Frame{node_t v;size_t next;};std::vector<Frame> stack{{root,0}};
        visited[root]=true;out.emplace_back('D',root,kNoNode);
        while(!stack.empty()) {
            auto& f=stack.back();
            if(f.next==g.adjacency[f.v].size()){out.emplace_back('F',f.v,0);stack.pop_back();continue;}
            node_t parent=f.v,w=g.adjacency[f.v][f.next++];
            if(g.valid(w)&&!visited[w]){visited[w]=true;out.emplace_back('D',w,parent);stack.push_back({w,0});}
        }
    };
    auto tree=[&](node_t root){if(g.N()<8192)recursive(root,kNoNode);else frames(root);};
    if(forest){for(node_t v=0;v<g.N();++v)if(g.valid(v)&&!visited[v])tree(v);}
    else tree(source);
    return out;
}
static std::string event_digest(const std::vector<Event>& events,size_t n) {
    EventRecorder recorder("",true,n);
    for(const auto& e:events) {
        if(std::get<0>(e)=='B')recorder.bfs(std::get<1>(e),std::get<2>(e));
        else if(std::get<0>(e)=='D')recorder.discover(std::get<1>(e),std::get<2>(e));
        else recorder.finish(std::get<1>(e));
    }
    recorder.close();return recorder.digest_hex();
}
static void verify(const TestGraph& g,node_t src,bool forest,const std::string& tag) {
    ++cases;
    const auto bref=reference_bfs(g,src,forest),dref=reference_dfs(g,src,forest);
    const auto bhash=event_digest(bref,g.N()),dhash=event_digest(dref,g.N());
    uint64_t depth=0,max_depth=0,roots=0;
    for(const auto& e:dref) {
        if(std::get<0>(e)=='D'){++depth;max_depth=std::max(max_depth,depth);if(std::get<2>(e)==kNoNode)++roots;}
        else --depth;
    }
    for(int mode=0;mode<6;++mode) {
        std::vector<Event> got;
        auto emit=[&](node_t v,uint64_t d){got.emplace_back('B',v,d);};
        BfsStats s;
        switch(mode){
            case 0:s=se_bfs(g,src,emit,forest);break;
            case 1:s=se_bfs<uint32_t>(g,src,emit,forest);break;
            case 2:s=baseline_bfs(g,src,emit,forest);break;
            case 3:s=baseline_bfs<uint32_t>(g,src,emit,forest);break;
            case 4:s=streaming_bfs(g,src,emit,forest);break;
            default:s=streaming_bfs<uint32_t>(g,src,emit,forest);break;
        }
        const auto name=tag+" BFS mode "+std::to_string(mode);
        check(got==bref,name+" full ordered events");check(s.visited==bref.size(),name+" visit count");
        check(event_digest(got,g.N())==bhash,name+" digest");
        check(s.roots==roots,name+" roots");
        check(s.aux_peak_bytes==s.aux_fixed_bytes+s.aux_dynamic_peak_bytes,name+" allocation accounting");
        check(s.id_bits==(mode%2?32u:64u),name+" ID width");
        if(!bref.empty())check(s.max_frontier>=1&&s.max_pending>=s.max_frontier,name+" frontier metrics");
    }
    for(int mode=0;mode<5;++mode) {
        std::vector<Event> got;
        auto discover=[&](node_t v,node_t p){got.emplace_back('D',v,p);};
        auto finish=[&](node_t v){got.emplace_back('F',v,0);};
        ChoiceDictionary color(g.N());TwoBitArray parents(g.N());std::vector<uint8_t> bytes(g.N(),0);
        DfsStats s;
        switch(mode){
            case 0:s=se_dfs(g,src,color,parents,discover,finish,forest);break;
            case 1:s=baseline_dfs_explicit(g,src,bytes,discover,finish,forest);break;
            case 2:s=baseline_dfs_explicit<uint32_t>(g,src,bytes,discover,finish,forest);break;
            case 3:s=packed_dfs_explicit(g,src,color,discover,finish,forest);break;
            default:s=packed_dfs_explicit<uint32_t>(g,src,color,discover,finish,forest);break;
        }
        const auto name=tag+" DFS mode "+std::to_string(mode);
        check(got==dref,name+" full interleaved events");
        check(s.discovered==dref.size()/2&&s.finished==s.discovered,name+" visit and finish counts");
        check(event_digest(got,g.N())==dhash,name+" digest");
        check(s.roots==roots&&s.max_depth==max_depth,name+" roots and depth");
        check(s.aux_peak_bytes==s.aux_fixed_bytes+s.aux_dynamic_peak_bytes,name+" allocation accounting");
        if(mode==0)check(s.parent_recoveries==s.discovered-s.roots&&s.incoming_calls==2*s.parent_recoveries,name+" reconstruction counters");
    }
}
struct BadOracle {
    size_t N()const{return 2;}bool valid(node_t v)const{return v<2;}
    int outgoing(node_t v,node_t* p)const{if(v==0){p[0]=1;return 1;}return 0;}
    int incoming(node_t,node_t*)const{return 0;}
};
struct OutOfRangeOracle:BadOracle {
    int outgoing(node_t,node_t* p)const{p[0]=2;return 1;}
};
struct ExcessDegreeOracle:BadOracle {
    int outgoing(node_t,node_t*)const{return 5;}
};
struct HugeOracle:BadOracle {
    size_t N()const{return uint64_t(UINT32_MAX)+2;}
};
int main(){
    try {
        for(const auto& item:std::vector<std::pair<std::string,TestGraph>>{
            {"empty",TestGraph(0,{})},{"isolated",TestGraph(1,{})},
            {"diamond",TestGraph(4,{{0,1},{0,2},{1,3},{2,3}})},
            {"cycles",TestGraph(5,{{0,0},{0,1},{1,2},{2,0},{2,3},{3,4},{4,2}})},
            {"disconnected",TestGraph(8,{{0,1},{1,2},{3,4},{4,3},{6,7}})},
            {"duplicates",TestGraph(3,{{0,1},{0,1},{1,2},{1,2}})},
            {"masked",TestGraph(7,{{0,1},{0,2},{2,3},{3,4},{5,6}},{1,4})},
            {"all invalid",TestGraph(4,{{0,1},{1,2},{2,3}},{0,1,2,3})}}) {
            verify(item.second,kNoNode,true,item.first+" forest");
            for(node_t v=0;v<item.second.N();++v)if(item.second.valid(v))verify(item.second,v,false,item.first+" source "+std::to_string(v));
        }
        for(size_t n:{size_t(31),size_t(32),size_t(33),size_t(63),size_t(64),size_t(65),size_t(20000)}){
            std::vector<Edge> edges;for(node_t v=1;v<n;++v)edges.emplace_back(v-1,v);
            TestGraph g(n,edges);verify(g,0,false,"chain "+std::to_string(n));verify(g,kNoNode,true,"chain forest "+std::to_string(n));
        }
        {std::vector<Edge> edges;const size_t n=2047;for(node_t v=1;v<n;++v)edges.emplace_back((v-1)/2,v);verify(TestGraph(n,edges),0,false,"wide binary tree");}
        uint64_t seed=0x4e91ae1234b56781ULL;
        auto rnd=[&](){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;};
        for(size_t trial=0;trial<80;++trial){
            size_t n=2+rnd()%127;std::vector<unsigned> in(n),out(n);std::set<Edge> edges;
            for(size_t i=0;i<4*n;++i){node_t v=rnd()%n,w=rnd()%n;if(out[v]<4&&in[w]<4&&edges.emplace(v,w).second){++out[v];++in[w];}}
            TestGraph g(n,{edges.begin(),edges.end()});
            verify(g,kNoNode,true,"random forest "+std::to_string(trial));
            verify(g,rnd()%n,false,"random source "+std::to_string(trial));
        }
        auto emit=[](node_t,uint64_t){};auto finish=[](node_t){};
        TestGraph empty(0,{}),one(1,{}),masked(2,{}, {1});
        for(node_t src:{node_t(1),kNoNode}) {
            expect_throw([&]{se_bfs(one,src,emit);},"invalid se BFS source");
            expect_throw([&]{baseline_bfs(one,src,emit);},"invalid baseline BFS source");
            expect_throw([&]{streaming_bfs(one,src,emit);},"invalid streaming BFS source");
            expect_throw([&]{ChoiceDictionary c(1);TwoBitArray p(1);se_dfs(one,src,c,p,emit,finish);},"invalid se DFS source");
            expect_throw([&]{std::vector<uint8_t> c(1);baseline_dfs_explicit(one,src,c,emit,finish);},"invalid baseline DFS source");
            expect_throw([&]{ChoiceDictionary c(1);packed_dfs_explicit(one,src,c,emit,finish);},"invalid packed DFS source");
        }
        expect_throw([&]{se_bfs(empty,0,emit);},"empty single-source traversal fails");
        expect_throw([&]{se_bfs(masked,1,emit);},"masked source fails");
        expect_throw([&]{ChoiceDictionary c(0);TwoBitArray p(1);se_dfs(one,0,c,p,emit,finish);},"state size mismatch fails");
        expect_throw([&]{ChoiceDictionary c(1);c.set(0,1);TwoBitArray p(1);se_dfs(one,0,c,p,emit,finish);},"reused active root fails");
        expect_throw([&]{ChoiceDictionary c(2);TwoBitArray p(2);se_dfs(BadOracle{},0,c,p,emit,finish);},"missing predecessor fails");
        expect_throw([&]{se_bfs(OutOfRangeOracle{},0,emit);},"out-of-range neighbor fails");
        expect_throw([&]{se_bfs(ExcessDegreeOracle{},0,emit);},"excess oracle degree fails");
        expect_throw([&]{se_bfs<uint32_t>(HugeOracle{},0,emit);},"32-bit identifier overflow fails");
        expect_throw([&]{baseline_bfs<uint32_t>(HugeOracle{},0,emit);},"32-bit baseline identifier overflow fails");
        expect_throw([&]{streaming_bfs<uint32_t>(HugeOracle{},0,emit);},"32-bit streaming identifier overflow fails");
        expect_throw([&]{InMemoryOracle g(2,{{0,2}});},"out-of-range input edge fails");
        expect_throw([&]{InMemoryOracle g(6,{{0,1},{0,2},{0,3},{0,4},{0,5}});},"input outdegree exceeds four fails");
        expect_throw([&]{InMemoryOracle g(6,{{1,0},{2,0},{3,0},{4,0},{5,0}});},"input indegree exceeds four fails");
    }catch(const std::exception& e){++failures;std::cerr<<"UNEXPECTED EXCEPTION: "<<e.what()<<'\n';}
    std::cout<<"cases="<<cases<<" comparisons="<<comparisons<<" failures="<<failures<<'\n';
    return failures?1:0;
}
