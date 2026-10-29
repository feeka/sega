// BFS with streamed (identifier, distance) output. Explicit frontiers require
// O(N + W log N) bits, not worst-case O(N) bits, where W is maximum layer width.
// N is the identifier-universe size; M counts traversable adjacency arcs. With
// O(1) outgoing navigation the total cost is O(N + M) including initialization.
#pragma once
#include "sdbg_traverse/nav_oracle.hpp"
#include "sdbg_traverse/choice_dictionary.hpp"
#include <cstdint>
#include <deque>
#include <limits>
#include <queue>
#include <vector>

namespace sdbgt {
struct BfsStats {
    uint64_t visited=0, edges_scanned=0, rounds=0, max_frontier=0, max_pending=0;
    uint64_t roots=0, aux_fixed_bytes=0, aux_dynamic_peak_bytes=0, aux_peak_bytes=0;
    unsigned id_bits=64;
};
namespace detail {
inline void bfs_memory(BfsStats& st, const HeapCounter& h) {
    st.aux_dynamic_peak_bytes = h.peak;
    st.aux_peak_bytes = st.aux_fixed_bytes + h.peak;
}
} // namespace detail

// Two frontier vectors, with one-bit visited state. Forest mode processes
// unvisited valid roots in ascending identifier order and resets root distance.
// Output retention by the caller is excluded from auxiliary accounting.
template<class Index=node_t, class Oracle, class Emit>
BfsStats se_bfs(const Oracle& g, node_t src, Emit&& emit, bool forest=false) {
    detail::require_index_range<Index>(g);
    detail::require_source(g, src, forest);
    BfsStats st; st.id_bits = sizeof(Index)*8;
    const size_t N=g.N();
    std::vector<uint64_t> vis(N/64 + (N%64!=0), 0);
    st.aux_fixed_bytes=vis.capacity()*sizeof(uint64_t);
    auto seen=[&](node_t v) { return (vis[v>>6]>>(v&63))&1ULL; };
    auto mark=[&](node_t v) { vis[v>>6] |= 1ULL<<(v&63); };
    detail::HeapCounter heap;
    using Vec=std::vector<Index,detail::CountingAllocator<Index>>;
    Vec cur{detail::CountingAllocator<Index>(heap)}, nxt{detail::CountingAllocator<Index>(heap)};
    auto visit_root=[&](node_t root) {
        ++st.roots;
        mark(root); cur.push_back(Index(root));
        uint64_t level=0;
        node_t nb[kSigma];
        while (!cur.empty()) {
            st.max_frontier=std::max(st.max_frontier,uint64_t(cur.size()));
            st.max_pending=std::max(st.max_pending,uint64_t(cur.size()+nxt.size()));
            for (size_t j=0;j<cur.size();++j) {
                node_t v=cur[j]; emit(v,level); ++st.visited;
                int d=detail::checked_degree(g.outgoing(v,nb)); st.edges_scanned+=d;
                for (int i=0;i<d;++i) {
                    node_t w=nb[i];
                    if (detail::valid_neighbor(g,w) && !seen(w)) { mark(w); nxt.push_back(Index(w)); }
                }
                // Pending work excludes already processed entries retained in cur.
                st.max_pending=std::max(st.max_pending,uint64_t(cur.size()-j-1+nxt.size()));
            }
            cur.swap(nxt); nxt.clear(); ++level; ++st.rounds;
        }
    };
    if (forest) { for (node_t v=0;v<N;++v) if (g.valid(v)&&!seen(v)) visit_root(v); }
    else visit_root(src);
    detail::bfs_memory(st,heap);
    return st;
}

// Conventional streaming comparison: bitmap + FIFO, using layer counters to
// emit distances. It has the same output contract and asymptotic space bound
// as se_bfs; this is an engineering comparison, not a new BFS algorithm.
template<class Index=node_t, class Oracle, class Emit>
BfsStats streaming_bfs(const Oracle& g, node_t src, Emit&& emit, bool forest=false) {
    detail::require_index_range<Index>(g); detail::require_source(g,src,forest);
    BfsStats st; st.id_bits=sizeof(Index)*8;
    const size_t N=g.N();
    std::vector<uint64_t> vis(N/64+(N%64!=0),0);
    st.aux_fixed_bytes=vis.capacity()*sizeof(uint64_t);
    auto seen=[&](node_t v) { return (vis[v>>6]>>(v&63))&1ULL; };
    auto mark=[&](node_t v) { vis[v>>6]|=1ULL<<(v&63); };
    detail::HeapCounter heap;
    std::deque<Index,detail::CountingAllocator<Index>> q{detail::CountingAllocator<Index>(heap)};
    auto visit_root=[&](node_t root) {
        ++st.roots; mark(root); q.push_back(Index(root)); uint64_t level=0;
        node_t nb[kSigma];
        while (!q.empty()) {
            size_t remaining=q.size();
            st.max_frontier=std::max(st.max_frontier,uint64_t(remaining));
            st.max_pending=std::max(st.max_pending,uint64_t(q.size()));
            while (remaining--) {
                node_t v=q.front(); q.pop_front(); emit(v,level); ++st.visited;
                int d=detail::checked_degree(g.outgoing(v,nb)); st.edges_scanned+=d;
                for (int i=0;i<d;++i) {
                    node_t w=nb[i];
                    if (detail::valid_neighbor(g,w)&&!seen(w)) { mark(w);q.push_back(Index(w)); }
                }
                st.max_pending=std::max(st.max_pending,uint64_t(q.size()));
            }
            ++level; ++st.rounds;
        }
    };
    if (forest) { for (node_t v=0;v<N;++v) if (g.valid(v)&&!seen(v)) visit_root(v); }
    else visit_root(src);
    detail::bfs_memory(st,heap); return st;
}

// Distance-array baseline. The concrete array costs 32*N bits and keeps
// distances internally until return; it does not expose random-access results
// through this streaming API. UINT32_MAX is reserved for undiscovered nodes.
template<class Index=node_t, class Oracle, class Emit>
BfsStats baseline_bfs(const Oracle& g, node_t src, Emit&& emit, bool forest=false) {
    detail::require_index_range<Index>(g); detail::require_source(g,src,forest);
    BfsStats st; st.id_bits=sizeof(Index)*8;
    const size_t N=g.N();
    std::vector<uint32_t> dist(N,UINT32_MAX);
    st.aux_fixed_bytes=dist.capacity()*sizeof(uint32_t);
    detail::HeapCounter heap;
    std::deque<Index,detail::CountingAllocator<Index>> q{detail::CountingAllocator<Index>(heap)};
    auto visit_root=[&](node_t root) {
        ++st.roots; dist[root]=0; q.push_back(Index(root));
        node_t nb[kSigma];
        while (!q.empty()) {
            size_t remaining=q.size();
            st.max_frontier=std::max(st.max_frontier,uint64_t(remaining));
            st.max_pending=std::max(st.max_pending,uint64_t(q.size()));
            while (remaining--) {
                node_t v=q.front(); q.pop_front(); emit(v,uint64_t(dist[v])); ++st.visited;
                int d=detail::checked_degree(g.outgoing(v,nb)); st.edges_scanned+=d;
                for (int i=0;i<d;++i) {
                    node_t w=nb[i];
                    if (detail::valid_neighbor(g,w)&&dist[w]==UINT32_MAX) {
                        if (dist[v]==UINT32_MAX-1) throw std::overflow_error("BFS distance exceeds 32-bit baseline range");
                        dist[w]=dist[v]+1; q.push_back(Index(w));
                    }
                }
                st.max_pending=std::max(st.max_pending,uint64_t(q.size()));
            }
            ++st.rounds;
        }
    };
    if (forest) { for (node_t v=0;v<N;++v) if (g.valid(v)&&dist[v]==UINT32_MAX) visit_root(v); }
    else visit_root(src);
    detail::bfs_memory(st,heap); return st;
}
} // namespace sdbgt
