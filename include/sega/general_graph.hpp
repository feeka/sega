// Experimental static simple graphs. SDSL is GPL-3.0-or-later; see THIRD_PARTY.md.
#pragma once
#include "graph_types.hpp"
#include <sdsl/wt_int.hpp>
#include <sdsl/rrr_vector.hpp>
#include <sdsl/int_vector_buffer.hpp>
#include <sdsl/io.hpp>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sega {
template<class Index = uint32_t>
class CsrGraph {
    uint64_t n_;
    std::vector<uint64_t> offsets_, in_offsets_;
    std::vector<Index> targets_, sources_;
    void vertex(Vertex v) const { if (v >= n_) throw std::out_of_range("invalid vertex"); }
    void edge(uint64_t e) const { if (e >= targets_.size()) throw std::out_of_range("invalid edge position"); }
public:
    CsrGraph(uint64_t n, std::vector<Edge> edges) : n_(n) {
        if (n && n - 1 > std::numeric_limits<Index>::max())
            throw std::overflow_error("vertex IDs do not fit CSR identifier width");
        edges = normalize_edges(n, std::move(edges));
        offsets_.assign(n + 1, 0); in_offsets_.assign(n + 1, 0);
        for (auto e : edges) { ++offsets_[e.first + 1]; ++in_offsets_[e.second + 1]; }
        for (uint64_t v = 0; v < n; ++v) { offsets_[v+1] += offsets_[v]; in_offsets_[v+1] += in_offsets_[v]; }
        targets_.reserve(edges.size()); sources_.resize(edges.size());
        auto cursor = in_offsets_;
        for (auto e : edges) { targets_.push_back(Index(e.second)); sources_[cursor[e.second]++] = Index(e.first); }
    }
    struct Cursor { uint64_t next, end; };
    bool valid(Vertex v) const { return v < n_; }
    Cursor neighbors(Vertex v) const { return {out_begin(v), out_end(v)}; }
    bool next(Cursor& cursor, Vertex& v) const {
        if (cursor.next == cursor.end) return false;
        v = target(cursor.next++); return true;
    }
    uint64_t num_vertices() const { return n_; }
    uint64_t num_edges() const { return targets_.size(); }
    uint64_t out_begin(Vertex v) const { vertex(v); return offsets_[v]; }
    uint64_t out_end(Vertex v) const { vertex(v); return offsets_[v+1]; }
    uint64_t outdegree(Vertex v) const { return out_end(v) - out_begin(v); }
    Vertex target(uint64_t e) const { edge(e); return targets_[e]; }
    Vertex source(uint64_t e) const {
        edge(e); return uint64_t(std::upper_bound(offsets_.begin(), offsets_.end(), e) - offsets_.begin() - 1);
    }
    uint64_t indegree(Vertex v) const { vertex(v); return in_offsets_[v+1] - in_offsets_[v]; }
    Vertex incoming(Vertex v, uint64_t k) const {
        if (k >= indegree(v)) throw std::out_of_range("invalid incoming-neighbor ordinal");
        return sources_[in_offsets_[v] + k];
    }
    uint64_t incoming_rank(uint64_t e) const {
        const Vertex v = target(e), u = source(e);
        auto first = sources_.begin() + in_offsets_[v], last = sources_.begin() + in_offsets_[v+1];
        return uint64_t(std::lower_bound(first, last, Index(u)) - first);
    }
    Storage storage() const {
        return {sdsl::size_in_bytes(targets_), sdsl::size_in_bytes(offsets_),
                sdsl::size_in_bytes(sources_) + sdsl::size_in_bytes(in_offsets_), 8};
    }
};

// false: plain wavelet bitvectors; true: RRR-compressed wavelet bitvectors.
// Boundaries use ordinary bits + succinct rank/select in both variants.
// No reverse adjacency array and no degree-sized neighbor buffers are retained.
template<bool Compressed = false>
class WaveletGraph {
    using Bits = std::conditional_t<Compressed, sdsl::rrr_vector<63>, sdsl::bit_vector>;
    using Wavelet = sdsl::wt_int<Bits>;
    uint64_t n_ = 0;
    Wavelet targets_;
    sdsl::bit_vector boundaries_;
    sdsl::rank_support_v<1> rank1_;
    sdsl::select_support_mcl<1> select1_;
    sdsl::select_support_mcl<0> select0_;
    void bind() { rank1_.set_vector(&boundaries_); select1_.set_vector(&boundaries_); select0_.set_vector(&boundaries_); }
    void vertex(Vertex v) const { if (v >= n_) throw std::out_of_range("invalid vertex"); }
    void edge(uint64_t e) const { if (e >= num_edges()) throw std::out_of_range("invalid edge position"); }
    struct RamInput {
        std::string path = sdsl::ram_file_name("sega_wt_" + sdsl::util::to_string(sdsl::util::pid()) + "_" + sdsl::util::to_string(sdsl::util::id()));
        ~RamInput() { sdsl::remove(path); }
    };
public:
    WaveletGraph(uint64_t n, std::vector<Edge> edges) : n_(n) {
        edges = normalize_edges(n, std::move(edges));
        const uint64_t m = edges.size();
        // Upstream wt_int constructs m*levels bits and uses a 64-bit shift.
        const uint8_t width = n > 1 ? uint8_t(sdsl::bits::hi(n - 1) + 1) : 1;
        if (m > uint64_t(INT64_MAX) / width) throw std::length_error("wavelet construction bit length overflow");
        boundaries_ = sdsl::bit_vector(n + m + 1, 0);
        uint64_t i = 0;
        for (uint64_t v = 0; v < n; ++v) {
            boundaries_[v + i] = 1;
            while (i < m && edges[i].first == v) ++i;
        }
        boundaries_[n + m] = 1;
        sdsl::util::init_support(rank1_, &boundaries_);
        sdsl::util::init_support(select1_, &boundaries_);
        sdsl::util::init_support(select0_, &boundaries_);
        if (m) {
            // Use SDSL's existing constructor without suffix-array dependencies.
            // This is an IN-MEMORY builder, not an external-memory claim.
            RamInput tmp;
            {
                sdsl::int_vector_buffer<> values(tmp.path, std::ios::out, 4096, width);
                for (auto e : edges) values.push_back(e.second);
            }
            sdsl::int_vector_buffer<> values(tmp.path, std::ios::in, 4096);
            Wavelet built(values, m);
            targets_.swap(built);
        }
    }
    // Supports contain pointers to their owning bitvector: explicitly rebind
    // them on copy/move, rather than copying dangling pointers by default.
    WaveletGraph(const WaveletGraph& g) : n_(g.n_), targets_(g.targets_), boundaries_(g.boundaries_),
        rank1_(g.rank1_), select1_(g.select1_), select0_(g.select0_) { bind(); }
    WaveletGraph(WaveletGraph&& g) : n_(g.n_), targets_(std::move(g.targets_)), boundaries_(std::move(g.boundaries_)),
        rank1_(std::move(g.rank1_)), select1_(std::move(g.select1_)), select0_(std::move(g.select0_)) { bind(); }
    WaveletGraph& operator=(const WaveletGraph& g) {
        if (this != &g) { WaveletGraph copy(g); *this = std::move(copy); } return *this;
    }
    WaveletGraph& operator=(WaveletGraph&& g) {
        if (this != &g) { n_=g.n_; targets_=std::move(g.targets_); boundaries_=std::move(g.boundaries_);
            rank1_=std::move(g.rank1_); select1_=std::move(g.select1_); select0_=std::move(g.select0_); bind(); }
        return *this;
    }
    struct Cursor { uint64_t next, end; };
    bool valid(Vertex v) const { return v < n_; }
    Cursor neighbors(Vertex v) const { return {out_begin(v), out_end(v)}; }
    bool next(Cursor& cursor, Vertex& v) const {
        if (cursor.next == cursor.end) return false;
        v = target(cursor.next++); return true;
    }
    uint64_t num_vertices() const { return n_; }
    uint64_t num_edges() const { return targets_.size(); }
    uint64_t out_begin(Vertex v) const { vertex(v); return select1_(v + 1) - v; }
    uint64_t out_end(Vertex v) const { vertex(v); return select1_(v + 2) - v - 1; }
    uint64_t outdegree(Vertex v) const { return out_end(v) - out_begin(v); }
    Vertex target(uint64_t e) const { edge(e); return targets_[e]; }
    Vertex source(uint64_t e) const { edge(e); return rank1_(select0_(e + 1)) - 1; }
    uint64_t indegree(Vertex v) const { vertex(v); return num_edges() ? targets_.rank(num_edges(), v) : 0; }
    Vertex incoming(Vertex v, uint64_t k) const {
        return source(incoming_edge(v, k));
    }
    // Position in the single forward edge sequence: attached overlap/label
    // arrays can be shared by both navigation directions.
    uint64_t incoming_edge(Vertex v, uint64_t k) const {
        if (k >= indegree(v)) throw std::out_of_range("invalid incoming-neighbor ordinal");
        return targets_.select(k + 1, v);
    }
    uint64_t incoming_rank(uint64_t e) const { edge(e); return targets_.rank(e, targets_[e]); }
    Storage storage() const {
        return {sdsl::size_in_bytes(targets_), sdsl::size_in_bytes(boundaries_) + sdsl::size_in_bytes(rank1_) +
                sdsl::size_in_bytes(select1_) + sdsl::size_in_bytes(select0_), 0, 8};
    }
};
} // namespace sega
