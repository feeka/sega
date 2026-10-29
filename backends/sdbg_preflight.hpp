#pragma once
#include "sdbg/sdbg_item.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace sega {
inline uint64_t sdbg_uint(const std::string& s) {
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("expected unsigned SDBG integer");
    return std::stoull(s);
}
inline std::string sdbg_prefix(std::string path) {
    if (std::filesystem::is_directory(path)) {
        std::string found;
        for (const auto& e : std::filesystem::directory_iterator(path)) {
            if (e.path().extension() == ".sdbg_info") {
                if (!found.empty()) throw std::invalid_argument("multiple SDBG prefixes in directory; specify one");
                found = e.path().string();
            }
        }
        if (found.empty()) throw std::invalid_argument("directory contains no .sdbg_info file: " + path);
        path = found;
    }
    const std::string suffix = ".sdbg_info";
    if (path.size() >= suffix.size() && path.compare(path.size()-suffix.size(), suffix.size(), suffix) == 0)
        path.resize(path.size()-suffix.size());
    return path;
}
// Validate framing before the upstream loader, which assumes valid files.
// This is not a validator for arbitrary corrupt BOSS bitstreams.
inline void check_sdbg_files(const std::string& prefix) {
    std::ifstream in(prefix + ".sdbg_info");
    if (!in) throw std::runtime_error("cannot read MEGAHIT metadata: " + prefix + ".sdbg_info");
    auto field = [&](const char* expected) {
        std::string key, value;
        if (!(in >> key >> value) || key != expected) throw std::runtime_error("invalid SDBG metadata field");
        return sdbg_uint(value);
    };
    const auto k = field("k"), words = field("words_per_tip_label"), buckets = field("num_buckets"), files = field("num_files");
    uint64_t power = 4;
    while (power < buckets && power <= (uint64_t(1) << 60)) power *= 4;
    if (!k || k > kMaxK || words != (k+15)/16 || buckets != power || !files ||
        buckets > (uint64_t(1) << (2*std::min<uint64_t>(k, 16))))
        throw std::runtime_error("unsupported SDBG metadata dimensions");
    auto sum = [](uint64_t a, uint64_t b) { if (a > UINT64_MAX-b) throw std::overflow_error("SDBG size overflow"); return a+b; };
    auto mul = [](uint64_t a, uint64_t b) { if (b && a > UINT64_MAX/b) throw std::overflow_error("SDBG size overflow"); return a*b; };
    std::map<uint64_t,uint64_t> ends;
    std::set<uint64_t> seen;
    uint64_t items = 0;
    for (uint64_t i=0; i<buckets; ++i) {
        uint64_t v[6]; std::string token;
        for (auto& x : v) { if (!(in >> token)) throw std::runtime_error("truncated SDBG metadata"); x=sdbg_uint(token); }
        if ((v[0]==UINT64_MAX) != (v[1]==UINT64_MAX)) throw std::runtime_error("partially null SDBG bucket");
        if (v[0]==UINT64_MAX) {
            if (v[2] || v[3] || v[4] || v[5]) throw std::runtime_error("nonempty null bucket");
            continue;
        }
        if (v[0]>=buckets || v[1]>=files || v[4]>v[3] || v[5]>v[3] || !seen.insert(v[0]).second)
            throw std::runtime_error("invalid SDBG bucket");
        auto bytes = sum(mul(v[3],sizeof(SdbgItem)), sum(mul(v[4],words*sizeof(label_word_t)),mul(v[5],sizeof(mul_t))));
        if (v[2] != ends[v[1]]) throw std::runtime_error("noncontiguous SDBG offsets");
        ends[v[1]] = sum(v[2],bytes); items=sum(items,v[3]);
    }
    std::string extra;
    if ((in >> extra) || !items) throw std::runtime_error("trailing or empty SDBG metadata");
    for (auto pair : ends) {
        auto path=prefix+".sdbg."+std::to_string(pair.first);
        if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path)!=pair.second)
            throw std::runtime_error("SDBG shard missing or size mismatch: "+path);
    }
}
} // namespace sega
