// Minimal ownership-safe C ABI for the experimental Python Sarand adapter.
#include "sega/assembly_graph.hpp"
#include <cstdlib>
#include <cstring>
#include <iomanip>

namespace {
thread_local std::string error;
std::string quote(const std::string& s) {
    std::ostringstream out; out << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32 || c >= 127) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else out << c;
    }
    out << '"'; return out.str();
}
}
extern "C" {
const char* sega_assembly_error() { return error.c_str(); }
void* sega_assembly_open(const char* path) {
    error.clear();
    try { if (!path) throw std::invalid_argument("null path"); return new sega::AssemblyGraph(path); }
    catch (const std::exception& e) { error = e.what(); return nullptr; }
}
void sega_assembly_close(void* graph) { delete static_cast<sega::AssemblyGraph*>(graph); }
void sega_assembly_free(void* text) { std::free(text); }
char* sega_assembly_request(void* graph, const char* operation, const char* argument, uint64_t v, int reverse) {
    error.clear();
    try {
        if (!graph || !operation) throw std::invalid_argument("null graph or operation");
        const auto& g = *static_cast<sega::AssemblyGraph*>(graph);
        const std::string op(operation);
        std::ostringstream out;
        if (op == "lookup") {
            if (!argument) throw std::invalid_argument("null name");
            out << g.lookup(argument);
        } else if (op == "node") {
            out << "{\"name\":" << quote(g.name(v)) << ",\"sequence\":" << quote(g.sequence(v))
                << ",\"KC\":" << g.coverage(v) << '}';
        } else if (op == "neighbors") {
            out << '['; bool first = true;
            for (auto edge : g.neighbors(v, reverse != 0)) {
                if (!first) out << ',';
                first = false;
                out << '[' << edge.node << ',' << edge.overlap << ',' << edge.weight << ']';
            }
            out << ']';
        } else if (op == "stats") {
            out << "{\"segments\":" << g.num_vertices()/2 << ",\"vertices\":" << g.num_vertices()
                << ",\"arcs\":" << g.num_edges() << ",\"declared_links\":" << g.declared_links()
                << ",\"sequence_bases\":" << g.num_bases() << ",\"packed_dna_bytes\":" << g.packed_dna_bytes()
                << ",\"topology_serialized_bytes\":" << g.topology_storage().total()
                << ",\"reverse_adjacency_bytes\":" << g.topology_storage().incoming
                << ",\"numeric_metadata_bytes\":" << g.numeric_metadata_bytes()
                << ",\"note\":\"Component sizes exclude name strings, hash map, allocator and builder scratch; not RSS.\"}";
        } else throw std::invalid_argument("unknown operation");
        const auto value = out.str();
        auto result = static_cast<char*>(std::malloc(value.size() + 1));
        if (!result) throw std::bad_alloc();
        std::memcpy(result, value.c_str(), value.size() + 1); return result;
    } catch (const std::exception& e) { error = e.what(); return nullptr; }
}
}
