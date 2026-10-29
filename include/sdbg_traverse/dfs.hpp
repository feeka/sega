// Bounded-degree predecessor-index DFS. Each discovered non-root stores the
// index of its parent in its stable incoming-neighbor list, not an edge label.
// Packed colors + predecessor indices use 4N rounded bits; no node-ID stack is
// stored. This is related to space-efficient DFS literature, but is not a
// faithful implementation of EHK's general-graph stack-restoration algorithm.
// Re-scanning at most four successors per step preserves deterministic DFS
// order. Backward navigation is performed both at discovery and backtracking.
#pragma once
#include "sdbg_traverse/nav_oracle.hpp"
#include "sdbg_traverse/choice_dictionary.hpp"
#include <cstdint>
#include <vector>

namespace sdbgt {
namespace dcolor { enum : uint8_t { WHITE=0, ACTIVE=1, DONE=2 }; }
struct DfsStats {
    uint64_t discovered=0, finished=0, child_scans=0;
    uint64_t parent_recoveries=0, recover_probes=0, incoming_calls=0, max_depth=0;
    uint64_t roots=0, aux_fixed_bytes=0, aux_dynamic_peak_bytes=0, aux_peak_bytes=0;
    unsigned id_bits=64;
};

// Failure is a broken oracle reciprocity contract, never a valid index zero.
template<class Oracle>
uint8_t parent_index(const Oracle& g,node_t v,node_t parent,uint64_t& probes) {
    node_t in[kSigma]; int d=detail::checked_degree(g.incoming(v,in));
    for (int i=0;i<d;++i) {
        detail::valid_neighbor(g,in[i]); ++probes;
        if (in[i]==parent) return uint8_t(i);
    }
    throw std::runtime_error("discovery edge missing from child's incoming adjacency");
}

// Caller-owned states must be sized to N. A single-source call requires a
// WHITE root; forest mode skips previously colored nodes. Roots have kNoNode
// parent. Existing non-WHITE cells are treated as previously visited.
template<class Oracle,class OnDiscover,class OnFinish>
DfsStats se_dfs(const Oracle& g,node_t src,ChoiceDictionary& visited,
               TwoBitArray& parent_sym,OnDiscover&& discover,OnFinish&& finish,
               bool forest=false) {
    detail::require_source(g,src,forest);
    if (visited.size()!=g.N() || parent_sym.size()!=g.N())
        throw std::invalid_argument("DFS state must match identifier-universe size");
    if (!forest && visited.get(src)!=dcolor::WHITE)
        throw std::invalid_argument("DFS source already visited");
    DfsStats st;
    st.aux_fixed_bytes=visited.resident_bytes()+parent_sym.resident_bytes();
    st.aux_peak_bytes=st.aux_fixed_bytes;
    auto visit_root=[&](node_t root) {
        ++st.roots; node_t cur=root; uint64_t depth=1;
        visited.set(cur,dcolor::ACTIVE); discover(cur,kNoNode); ++st.discovered;
        st.max_depth=std::max(st.max_depth,depth);
        node_t nb[kSigma];
        for (;;) {
            int d=detail::checked_degree(g.outgoing(cur,nb));
            node_t child=kNoNode;
            for (int i=0;i<d;++i) {
                ++st.child_scans;
                if (detail::valid_neighbor(g,nb[i]) && visited.get(nb[i])==dcolor::WHITE) {child=nb[i];break;}
            }
            if (child!=kNoNode) {
                ++st.incoming_calls;
                parent_sym.set(child,parent_index(g,child,cur,st.recover_probes));
                visited.set(child,dcolor::ACTIVE); discover(child,cur); ++st.discovered;
                cur=child; ++depth; st.max_depth=std::max(st.max_depth,depth);
            } else {
                visited.set(cur,dcolor::DONE); finish(cur); ++st.finished;
                if (cur==root) break;
                node_t in[kSigma]; ++st.incoming_calls;
                int di=detail::checked_degree(g.incoming(cur,in));
                uint8_t pidx=parent_sym.get(cur);
                if (pidx>=di) throw std::runtime_error("stored parent index no longer exists");
                node_t parent=in[pidx];
                if (!detail::valid_neighbor(g,parent) || visited.get(parent)!=dcolor::ACTIVE)
                    throw std::runtime_error("reconstructed parent is not an active valid node");
                ++st.parent_recoveries; ++st.recover_probes;
                cur=parent; --depth;
            }
        }
    };
    if (forest) { for (node_t v=0;v<g.N();++v) if (g.valid(v)&&visited.get(v)==dcolor::WHITE) visit_root(v); }
    else visit_root(src);
    return st;
}
namespace detail {
template<class Index,class Oracle,class Get,class Set,class OnDiscover,class OnFinish>
DfsStats explicit_dfs(const Oracle& g,node_t src,size_t state_size,size_t state_bytes,
                      Get get,Set set,OnDiscover&& discover,OnFinish&& finish,bool forest) {
    require_index_range<Index>(g); require_source(g,src,forest);
    if (state_size!=g.N()) throw std::invalid_argument("DFS state must match identifier-universe size");
    if (!forest && get(src)!=dcolor::WHITE) throw std::invalid_argument("DFS source already visited");
    DfsStats st; st.id_bits=sizeof(Index)*8; st.aux_fixed_bytes=state_bytes;
    HeapCounter heap;
    std::vector<Index,CountingAllocator<Index>> stack{CountingAllocator<Index>(heap)};
    auto visit_root=[&](node_t root) {
        ++st.roots; set(root,dcolor::ACTIVE); discover(root,kNoNode); ++st.discovered;
        stack.push_back(Index(root)); st.max_depth=std::max(st.max_depth,uint64_t(stack.size()));
        node_t nb[kSigma];
        while (!stack.empty()) {
            node_t cur=stack.back(),child=kNoNode;
            int d=checked_degree(g.outgoing(cur,nb));
            for (int i=0;i<d;++i) {
                ++st.child_scans;
                if (valid_neighbor(g,nb[i])&&get(nb[i])==dcolor::WHITE) {child=nb[i];break;}
            }
            if (child!=kNoNode) {
                set(child,dcolor::ACTIVE); discover(child,cur); ++st.discovered;
                stack.push_back(Index(child)); st.max_depth=std::max(st.max_depth,uint64_t(stack.size()));
            } else {
                set(cur,dcolor::DONE); finish(cur); ++st.finished; stack.pop_back();
            }
        }
    };
    if (forest) {for(node_t v=0;v<g.N();++v) if(g.valid(v)&&get(v)==dcolor::WHITE) visit_root(v);}
    else visit_root(src);
    st.aux_dynamic_peak_bytes=heap.peak; st.aux_peak_bytes=st.aux_fixed_bytes+heap.peak;
    return st;
}
} // namespace detail

// Byte-state baseline retained for comparison with the original measurements.
template<class Index=node_t,class Oracle,class OnDiscover,class OnFinish>
DfsStats baseline_dfs_explicit(const Oracle& g,node_t src,std::vector<uint8_t>& state,
                               OnDiscover&& discover,OnFinish&& finish,bool forest=false) {
    return detail::explicit_dfs<Index>(g,src,state.size(),state.capacity()*sizeof(uint8_t),
        [&](node_t v){return state[v];},[&](node_t v,uint8_t c){state[v]=c;},
        std::forward<OnDiscover>(discover),std::forward<OnFinish>(finish),forest);
}
// Stronger baseline: the same two-bit colors as reconstruction + explicit IDs.
template<class Index=node_t,class Oracle,class OnDiscover,class OnFinish>
DfsStats packed_dfs_explicit(const Oracle& g,node_t src,ChoiceDictionary& state,
                             OnDiscover&& discover,OnFinish&& finish,bool forest=false) {
    return detail::explicit_dfs<Index>(g,src,state.size(),state.resident_bytes(),
        [&](node_t v){return state.get(v);},[&](node_t v,uint8_t c){state.set(v,c);},
        std::forward<OnDiscover>(discover),std::forward<OnFinish>(finish),forest);
}
} // namespace sdbgt
