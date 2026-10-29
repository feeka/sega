#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace sega {
using Vertex = uint64_t;
using Edge = std::pair<Vertex, Vertex>;
constexpr Vertex no_vertex = UINT64_MAX;

// Canonical source/target order. Dense IDs include isolated vertices.
// Duplicate rejection is deliberate: this first API is a simple graph API.
inline std::vector<Edge> normalize_edges(uint64_t n, std::vector<Edge> edges) {
    if (n >= uint64_t(INT64_MAX)) throw std::length_error("vertex count exceeds supported 63-bit universe");
    for (auto e : edges)
        if (e.first >= n || e.second >= n) throw std::out_of_range("edge endpoint outside [0, vertices)");
    std::sort(edges.begin(), edges.end());
    if (std::adjacent_find(edges.begin(), edges.end()) != edges.end())
        throw std::invalid_argument("duplicate edge: simple graphs only (no implicit deduplication)");
    if (edges.size() > uint64_t(INT64_MAX) - n - 1)
        throw std::length_error("graph boundary bitvector is too large");
    return edges;
}

struct Storage {
    // Encoded component sizes, including component serialization headers and
    // navigation supports. Consistent size proxy, NOT heap allocation or RSS.
    uint64_t targets = 0, boundaries = 0, incoming = 0, metadata = 8;
    uint64_t total() const { return targets + boundaries + incoming + metadata; }
};

} // namespace sega
