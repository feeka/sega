// Experimental GFA1 assembly adapter over SEGA's RRR wavelet graph.
// One topology, packed forward DNA, on-demand reverse complements. Requires
// the optional GPL SDSL backend; this is not a MEGAHIT/BOSS file importer.
#pragma once
#include "general_graph.hpp"
#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_map>

namespace sega {
class AssemblyGraph {
    struct Link { Vertex u, v; uint64_t overlap, order; };
    std::unique_ptr<WaveletGraph<true>> topology_;
    std::vector<std::string> names_;
    std::unordered_map<std::string, uint64_t> ids_;
    std::vector<uint64_t> offsets_{0}, coverage_;
    std::vector<uint8_t> bases_;
    sdsl::int_vector<> overlaps_, order_;
    uint64_t bases_count_ = 0, declared_links_ = 0;

    static uint64_t number(const std::string& s) {
        if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("expected nonnegative integer: " + s);
        size_t used = 0;
        auto v = std::stoull(s, &used);
        if (used != s.size()) throw std::invalid_argument("invalid integer");
        return v;
    }
    static std::vector<std::string> fields(const std::string& s) {
        std::vector<std::string> result;
        size_t start = 0;
        do {
            auto stop = s.find('\t', start);
            result.push_back(s.substr(start, stop - start));
            if (stop == std::string::npos) break;
            start = stop + 1;
        } while (true);
        return result;
    }
    void vertex(Vertex v) const {
        if (v >= num_vertices()) throw std::out_of_range("assembly vertex outside graph");
    }
    char base(uint64_t i) const { return "ACGT"[(bases_[i / 4] >> (2 * (i % 4))) & 3]; }
    Vertex endpoint(const std::string& name, const std::string& orientation) const {
        if (orientation != "+" && orientation != "-") throw std::invalid_argument("invalid GFA orientation");
        auto found = ids_.find(name);
        if (found == ids_.end()) throw std::invalid_argument("GFA link refers to unknown segment: " + name);
        return 2 * found->second + (orientation == "-");
    }
public:
    struct Arc { Vertex node; uint64_t overlap, weight, order; };
    explicit AssemblyGraph(const std::string& path) {
        std::ifstream in(path);
        if (!in) throw std::runtime_error("cannot open GFA: " + path);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#' || line[0] != 'S') continue;
            auto f = fields(line);
            if (f.size() < 3 || f[0] != "S" || f[1].empty() || f[2].empty() || f[2] == "*")
                throw std::invalid_argument("GFA1 S record requires a name and explicit ACGT sequence");
            if (!ids_.emplace(f[1], names_.size()).second) throw std::invalid_argument("duplicate GFA segment");
            names_.push_back(f[1]);
            uint64_t kc = 0;
            bool has_kc = false;
            for (size_t i = 3; i < f.size(); ++i) if (f[i].compare(0, 5, "KC:i:") == 0) {
                if (has_kc) throw std::invalid_argument("duplicate KC tag");
                kc = number(f[i].substr(5)); has_kc = true;
            }
            // Sarand uses KC for coverage. Fail explicitly on unsupported input.
            if (!has_kc) throw std::invalid_argument("Sarand adapter requires a KC:i tag for each segment");
            coverage_.push_back(kc);
            for (char c : f[2]) {
                const auto index = std::string("ACGT").find(c);
                if (index == std::string::npos) throw std::invalid_argument("only uppercase ACGT is supported by this adapter");
                if (bases_count_ % 4 == 0) bases_.push_back(0);
                bases_.back() |= uint8_t(index << (2 * (bases_count_ % 4)));
                ++bases_count_;
            }
            offsets_.push_back(bases_count_);
        }
        if (!in.eof()) throw std::runtime_error("GFA read failed");
        in.clear(); in.seekg(0);
        std::vector<Link> links;
        uint64_t ordinal = 0;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            auto f = fields(line);
            if (f[0] == "S" || f[0] == "H" || f[0] == "P") continue;
            if (f[0] != "L" || f.size() < 6) throw std::invalid_argument("unsupported GFA record (only H/S/L/P accepted)");
            if (f[5].size() < 2 || f[5].back() != 'M') throw std::invalid_argument("overlap must be an integer followed by M");
            auto overlap = number(f[5].substr(0, f[5].size() - 1));
            auto u = endpoint(f[1], f[2]), v = endpoint(f[3], f[4]);
            if (overlap > length(u) || overlap > length(v)) throw std::invalid_argument("overlap exceeds segment length");
            links.push_back({u, v, overlap, ordinal++});
            links.push_back({v ^ 1, u ^ 1, overlap, ordinal++});
            ++declared_links_;
        }
        if (!in.eof()) throw std::runtime_error("GFA read failed");
        std::sort(links.begin(), links.end(), [](auto a, auto b) {
            return std::tie(a.u, a.v, a.order) < std::tie(b.u, b.v, b.order);
        });
        std::vector<Edge> edges;
        uint64_t maximum_overlap = 0;
        // Repeated identical links collapse as in a DiGraph. Conflicting
        // overlaps are rejected rather than silently selecting one.
        size_t write = 0;
        for (size_t i = 0; i < links.size(); ++i) {
            if (write && links[write-1].u == links[i].u && links[write-1].v == links[i].v) {
                if (links[write-1].overlap != links[i].overlap)
                    throw std::invalid_argument("conflicting overlaps for duplicate directed edge");
                continue;
            }
            links[write++] = links[i];
        }
        links.resize(write);
        for (auto e : links) { edges.emplace_back(e.u, e.v); maximum_overlap = std::max(maximum_overlap, e.overlap); }
        overlaps_ = sdsl::int_vector<>(links.size(), 0, maximum_overlap ? sdsl::bits::hi(maximum_overlap) + 1 : 1);
        order_ = sdsl::int_vector<>(links.size(), 0, ordinal ? sdsl::bits::hi(ordinal) + 1 : 1);
        for (size_t i = 0; i < links.size(); ++i) { overlaps_[i] = links[i].overlap; order_[i] = links[i].order; }
        // Builder scratch is temporary and included in process peak RSS.
        std::vector<Link>().swap(links);
        topology_ = std::make_unique<WaveletGraph<true>>(num_vertices(), std::move(edges));
        bases_.shrink_to_fit(); names_.shrink_to_fit(); offsets_.shrink_to_fit(); coverage_.shrink_to_fit();
    }
    uint64_t num_vertices() const { return names_.size() * 2; }
    uint64_t num_edges() const { return topology_->num_edges(); }
    uint64_t declared_links() const { return declared_links_; }
    uint64_t num_bases() const { return bases_count_; }
    uint64_t length(Vertex v) const { vertex(v); return offsets_[v / 2 + 1] - offsets_[v / 2]; }
    uint64_t coverage(Vertex v) const { vertex(v); return coverage_[v / 2]; }
    std::string name(Vertex v) const { vertex(v); return names_[v / 2] + (v % 2 ? "-" : "+"); }
    Vertex lookup(const std::string& name) const {
        if (name.size() < 2) throw std::invalid_argument("oriented segment name required");
        return endpoint(name.substr(0, name.size() - 1), name.substr(name.size() - 1));
    }
    std::string sequence(Vertex v) const {
        const auto size = length(v), start = offsets_[v / 2];
        std::string s(size, 'A');
        for (uint64_t i = 0; i < size; ++i) {
            if (v % 2) {
                auto b = (bases_[(start + size - i - 1) / 4] >> (2 * ((start + size - i - 1) % 4))) & 3;
                s[i] = "TGCA"[b];
            } else s[i] = base(start + i);
        }
        return s;
    }
    std::vector<Arc> neighbors(Vertex v, bool reverse) const {
        vertex(v);
        std::vector<Arc> out;
        if (reverse) {
            auto degree = topology_->indegree(v);
            out.reserve(degree);
            for (uint64_t i = 0; i < degree; ++i) {
                auto e = topology_->incoming_edge(v, i), u = topology_->source(e);
                out.push_back({u, overlaps_[e], length(u) - overlaps_[e], e});
            }
            // NetworkX reverse() visits original sources in node insertion
            // order. Wavelet select already returns precisely that order.
        } else {
            for (auto e = topology_->out_begin(v); e < topology_->out_end(v); ++e) {
                auto u = topology_->target(e);
                out.push_back({u, overlaps_[e], length(u) - overlaps_[e], order_[e]});
            }
            std::sort(out.begin(), out.end(), [](auto a, auto b) { return a.order < b.order; });
        }
        return out;
    }
    Storage topology_storage() const { return topology_->storage(); }
    uint64_t packed_dna_bytes() const { return bases_.size(); }
    uint64_t numeric_metadata_bytes() const {
        return (offsets_.size() + coverage_.size()) * 8 + sdsl::size_in_bytes(overlaps_) + sdsl::size_in_bytes(order_);
    }
};
} // namespace sega
