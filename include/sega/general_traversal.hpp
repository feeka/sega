#pragma once
#include "graph_types.hpp"
#include "sdbg_traverse/nav_oracle.hpp"
#include "sdbg_traverse/choice_dictionary.hpp"
#include <deque>
#include <type_traits>

namespace sega {
struct TraversalStats {
    uint64_t visited=0, finished=0, roots=0, arcs_scanned=0;
    uint64_t max_pending=0, max_depth=0, aux_fixed_bytes=0, aux_dynamic_peak_bytes=0;
    unsigned id_bits=0;
    uint64_t aux_peak_bytes() const { return aux_fixed_bytes + aux_dynamic_peak_bytes; }
};
template<class Index, class Graph>
void check_traversal(const Graph& g, Vertex source, bool forest) {
    static_assert(std::is_unsigned<Index>::value, "unsigned traversal IDs required");
    if (g.num_vertices() && g.num_vertices()-1 > std::numeric_limits<Index>::max())
        throw std::overflow_error("graph does not fit traversal identifier width");
    if (!forest && !g.valid(source)) throw std::out_of_range("invalid source vertex");
}

// A workspace may be reused sequentially; each query clears its bitmap in O(n/64).
// It retains bitmap/deque allocations, is not thread-safe, and must not be copied.
template<class Index=uint32_t> struct BfsWorkspace {
    sdbgt::detail::HeapCounter heap;
    std::vector<uint64_t,sdbgt::detail::CountingAllocator<uint64_t>> visited;
    std::deque<Index,sdbgt::detail::CountingAllocator<Index>> queue;
    BfsWorkspace():visited(sdbgt::detail::CountingAllocator<uint64_t>(heap)),
        queue(sdbgt::detail::CountingAllocator<Index>(heap)){}
    BfsWorkspace(const BfsWorkspace&)=delete;
    BfsWorkspace& operator=(const BfsWorkspace&)=delete;
    void reset(uint64_t n){
        heap.peak=heap.current;queue.clear();visited.resize(n/64+(n%64!=0));
        std::fill(visited.begin(),visited.end(),0);
    }
};

// Results are streamed to the sink. Radius counts outgoing hops; boundary
// vertices are emitted but their edges are not scanned. No result is truncated.
template<class Index=uint32_t, class Graph, class Emit>
TraversalStats bfs(const Graph& g, Vertex source, Emit&& emit, BfsWorkspace<Index>& workspace,
                   bool forest=false, uint64_t radius=UINT64_MAX) {
    check_traversal<Index>(g,source,forest);
    if(forest && radius!=UINT64_MAX)throw std::invalid_argument("bounded BFS requires a source, not a forest");
    TraversalStats st;st.id_bits=8*sizeof(Index);workspace.reset(g.num_vertices());
    auto& visited=workspace.visited;auto& queue=workspace.queue;
    st.aux_fixed_bytes=visited.capacity()*sizeof(uint64_t);
    auto seen=[&](Vertex v){return (visited[v/64]>>(v%64))&1;};
    auto mark=[&](Vertex v){visited[v/64] |= uint64_t(1)<<(v%64);};
    auto tree=[&](Vertex root) {
        ++st.roots;mark(root);queue.push_back(Index(root));
        st.max_pending=std::max(st.max_pending,uint64_t(queue.size()));
        uint64_t distance=0;
        while(!queue.empty()) {
            uint64_t remaining=queue.size();
            while(remaining--) {
                Vertex u=queue.front();queue.pop_front();++st.visited;emit(u,distance);
                if(distance==radius)continue;
                auto cursor=g.neighbors(u);Vertex v;
                while(g.next(cursor,v)) {
                    ++st.arcs_scanned;
                    if(!seen(v)){mark(v);queue.push_back(Index(v));
                        st.max_pending=std::max(st.max_pending,uint64_t(queue.size()));}
                }
            }
            ++distance;
        }
    };
    if(forest){for(Vertex v=0;v<g.num_vertices();++v)if(g.valid(v) && !seen(v))tree(v);}
    else tree(source);
    // The shared counter includes bitmap resize overlap as well as the queue.
    st.aux_dynamic_peak_bytes=workspace.heap.peak-st.aux_fixed_bytes;return st;
}
template<class Index=uint32_t, class Graph, class Emit>
TraversalStats bfs(const Graph& g, Vertex source, Emit&& emit, bool forest=false) {
    BfsWorkspace<Index> workspace;
    return bfs<Index>(g,source,std::forward<Emit>(emit),workspace,forest);
}

// Packed colors plus a deque of frames (blocked stack). Each frame resumes
// at its next edge, so a high-degree vertex never restarts its adjacency scan.
template<class Index=uint32_t, class Graph, class Discover, class Finish>
TraversalStats dfs(const Graph& g, Vertex source, Discover&& discover, Finish&& finish, bool forest=false) {
    check_traversal<Index>(g, source, forest);
    TraversalStats st; st.id_bits=8*sizeof(Index);
    sdbgt::ChoiceDictionary colors(g.num_vertices());
    st.aux_fixed_bytes=colors.resident_bytes();
    struct Frame { Index vertex; typename Graph::Cursor cursor; };
    sdbgt::detail::HeapCounter heap;
    {
        std::deque<Frame,sdbgt::detail::CountingAllocator<Frame>> stack{sdbgt::detail::CountingAllocator<Frame>(heap)};
        auto enter=[&](Vertex v,Vertex parent) {
            colors.set(v,1); ++st.visited; discover(v,parent);
            stack.push_back({Index(v),g.neighbors(v)});
            st.max_depth=std::max(st.max_depth,uint64_t(stack.size()));
        };
        auto tree=[&](Vertex root) {
            ++st.roots; enter(root,no_vertex);
            while (!stack.empty()) {
                auto& frame=stack.back();
                Vertex v;
                if (!g.next(frame.cursor,v)) {
                    const Vertex v=frame.vertex; colors.set(v,2); ++st.finished; finish(v); stack.pop_back();
                } else {
                    const Vertex parent=frame.vertex; ++st.arcs_scanned;
                    if (colors.get(v)==0) enter(v,parent);
                }
            }
        };
        if (forest) { for(Vertex v=0;v<g.num_vertices();++v) if(g.valid(v) && colors.get(v)==0) tree(v); }
        else tree(source);
    }
    st.aux_dynamic_peak_bytes=heap.peak;
    return st;
}
} // namespace sega
