// Shared JSON protocol: hash/event I/O is excluded from measurement runs.
#pragma once
#include "sdbg_traverse/bfs.hpp"
#include "sdbg_traverse/dfs.hpp"
#include "sdbg_traverse/validation.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif
#if __has_include("sega_build_info.hpp")
#include "sega_build_info.hpp"
#else
#define SEGA_REVISION "unknown"
#define SEGA_MEGAHIT_REVISION "unknown"
#define SEGA_BUILD_FLAGS "unknown (not built with CMake)"
#define SEGA_SOURCE_SHA256 "unknown"
#endif
namespace sega_bench {
using namespace sdbgt;
using Clock = std::chrono::steady_clock;
inline double milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}
inline std::string quote(const std::string &s) {
  std::ostringstream o;
  o << '"';
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      o << "\\\"";
      break;
    case '\\':
      o << "\\\\";
      break;
    case '\n':
      o << "\\n";
      break;
    case '\r':
      o << "\\r";
      break;
    case '\t':
      o << "\\t";
      break;
    default:
      if (c < 32)
        o << "\\u" << std::hex << std::setw(4) << std::setfill('0')
          << unsigned(c) << std::dec;
      else
        o << c;
    }
  }
  o << '"';
  return o.str();
}
struct Json {
  std::ostringstream out;
  bool first = true;
  Json() { out << '{' << std::setprecision(17); }
  void key(const char *k) {
    if (!first)
      out << ',';
    first = false;
    out << quote(k) << ':';
  }
  template <class T> void number(const char *k, T v) {
    key(k);
    out << v;
  }
  void string(const char *k, const std::string &v) {
    key(k);
    out << quote(v);
  }
  void boolean(const char *k, bool v) {
    key(k);
    out << (v ? "true" : "false");
  }
  void raw(const char *k, const std::string &v) {
    key(k);
    out << v;
  }
  std::string str() const { return out.str() + "}"; }
};
inline uint64_t uint_arg(const std::string &v, const char *name) {
  if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos)
    throw std::invalid_argument(std::string(name) +
                                " must be an unsigned integer");
  size_t used = 0;
  auto n = std::stoull(v, &used);
  if (used != v.size())
    throw std::invalid_argument(name);
  return n;
}
inline bool known_method(const std::string &m) {
  for (const char *s : {"bfs-se", "bfs-se32", "bfs-base", "bfs-base32",
                        "bfs-stream", "bfs-stream32", "dfs-se", "dfs-base",
                        "dfs-base32", "dfs-packed", "dfs-packed32"})
    if (m == s)
      return true;
  return false;
}
struct Options {
  std::string input, method, events;
  bool forest = false, hash = true, audit = false, explicit_source = false;
  node_t source = kNoNode;
  uint64_t nodes = 10000, seed = 1;
};
inline Options parse(int argc, char **argv, bool synthetic) {
  if (argc < 3)
    throw std::invalid_argument(
        std::string("usage: ") + argv[0] +
        (synthetic ? " <path|wide|cycle|disconnected|random>"
                   : " <graph_prefix>") +
        " <method> [--source first|ID | --forest] [--verify hash|none] "
        "[--events FILE] [--audit]" +
        (synthetic ? " [--nodes N] [--seed N]" : ""));
  Options o;
  o.input = argv[1];
  o.method = argv[2];
  if (!known_method(o.method))
    throw std::invalid_argument("unknown method: " + o.method);
  for (int i = 3; i < argc; ++i) {
    std::string a = argv[i];
    auto value = [&]() {
      if (++i >= argc)
        throw std::invalid_argument("missing value for " + a);
      return std::string(argv[i]);
    };
    if (a == "--forest")
      o.forest = true;
    else if (a == "--source") {
      o.explicit_source = true;
      auto v = value();
      o.source = v == "first" ? kNoNode : uint_arg(v, "source");
      if (v != "first" && o.source == kNoNode)
        throw std::invalid_argument("numeric source equals reserved root sentinel");
    } else if (a == "--verify") {
      auto v = value();
      if (v != "hash" && v != "none")
        throw std::invalid_argument("verify must be hash or none");
      o.hash = v == "hash";
    } else if (a == "--events")
      o.events = value();
    else if (a == "--audit")
      o.audit = true;
    else if (a == "--nodes" && synthetic)
      o.nodes = uint_arg(value(), "nodes");
    else if (a == "--seed" && synthetic)
      o.seed = uint_arg(value(), "seed");
    else
      throw std::invalid_argument("unknown option: " + a);
  }
  if (o.forest && o.explicit_source)
    throw std::invalid_argument("--forest and --source are mutually exclusive");
  if (!o.hash && (!o.events.empty() || o.audit))
    throw std::invalid_argument("--events and --audit require --verify hash");
  return o;
}
inline long status_kb(const char *key) {
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line))
    if (line.compare(0, std::char_traits<char>::length(key), key) == 0) {
      std::istringstream is(line.substr(std::char_traits<char>::length(key)));
      long v;
      if (is >> v)
        return v;
    }
  return -1;
}
inline long rss_kb() {
#if defined(__APPLE__)
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&info), &n) == KERN_SUCCESS)
    return long(info.resident_size / 1024);
  return -1;
#else
  return status_kb("VmRSS:");
#endif
}
inline long peak_kb() {
  long proc = status_kb("VmHWM:");
  if (proc >= 0)
    return proc;
  rusage u{};
  if (getrusage(RUSAGE_SELF, &u))
    return -1;
#if defined(__APPLE__)
  return u.ru_maxrss / 1024;
#else
  return u.ru_maxrss;
#endif
}
inline double cpu_ms(const rusage &u) {
  return (u.ru_utime.tv_sec + u.ru_stime.tv_sec) * 1000.0 +
         (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1000.0;
}
// Constant-space adjacency audit, separately timed from traversal.
template <class Oracle> std::string audit_graph(const Oracle &g) {
  auto start = Clock::now();
  Sha256 digest;
  const char domain[] = "SEGA-TOPOLOGY-v1";
  digest.update(domain, sizeof(domain) - 1);
  auto add = [&](uint64_t v) {
    uint8_t b[8];
    for (unsigned i = 0; i < 8; ++i)
      b[i] = uint8_t(v >> (8 * i));
    digest.update(b, 8);
  };
  uint64_t valid = 0, transitions = 0;
  std::array<uint64_t, 5> ih{}, oh{};
  add(g.N());
  for (node_t v = 0; v < g.N(); ++v) {
    if (!g.valid(v))
      continue;
    ++valid;
    add(v);
    node_t out[4], in[4], again[4];
    int no = g.outgoing(v, out), ni = g.incoming(v, in);
    if (no < 0 || no > 4 || ni < 0 || ni > 4)
      throw std::runtime_error("audit: degree outside [0,4]");
    if (g.outdeg(v) != no || g.indeg(v) != ni)
      throw std::runtime_error("audit: degree disagrees with enumeration");
    if (g.outgoing(v, again) != no || !std::equal(out, out + no, again))
      throw std::runtime_error("audit: unstable outgoing order");
    if (g.incoming(v, again) != ni || !std::equal(in, in + ni, again))
      throw std::runtime_error("audit: unstable incoming order");
    ++oh[no];
    ++ih[ni];
    transitions += no;
    add(no);
    for (int i = 0; i < no; ++i) {
      node_t w = out[i];
      if (w >= g.N() || !g.valid(w))
        throw std::runtime_error("audit: invalid outgoing neighbor");
      if (std::find(out, out + i, w) != out + i)
        throw std::runtime_error("audit: duplicate outgoing neighbor");
      node_t reverse[4];
      int n = g.incoming(w, reverse);
      if (n < 0 || n > 4 || std::find(reverse, reverse + n, v) == reverse + n)
        throw std::runtime_error("audit: missing reverse incoming arc");
      add(w);
    }
    add(ni);
    for (int i = 0; i < ni; ++i) {
      node_t w = in[i];
      if (w >= g.N() || !g.valid(w))
        throw std::runtime_error("audit: invalid incoming neighbor");
      if (std::find(in, in + i, w) != in + i)
        throw std::runtime_error("audit: duplicate incoming neighbor");
      node_t reverse[4];
      int n = g.outgoing(w, reverse);
      if (n < 0 || n > 4 || std::find(reverse, reverse + n, v) == reverse + n)
        throw std::runtime_error("audit: missing reverse outgoing arc");
      add(w);
    }
  }
  Json j;
  j.string("status", "ok");
  j.number("valid_nodes", valid);
  j.number("transitions", transitions);
  j.number("wall_ms", milliseconds(start));
  j.string("topology_sha256", digest.hex_digest());
  auto arr = [](const std::array<uint64_t, 5> &x) {
    std::ostringstream o;
    o << '[';
    for (int i = 0; i < 5; ++i) {
      if (i)
        o << ',';
      o << x[i];
    }
    o << ']';
    return o.str();
  };
  j.raw("indegree_histogram", arr(ih));
  j.raw("outdegree_histogram", arr(oh));
  return j.str();
}
struct Metrics {
  uint64_t visited = 0, roots = 0, events = 0, aux_fixed = 0, aux_dynamic = 0,
           aux_peak = 0, id_bits = 0;
  uint64_t edges = 0, rounds = 0, frontier = 0, pending = 0, depth = 0,
           finished = 0, child_scans = 0, recoveries = 0, probes = 0,
           incoming_calls = 0;
};
inline Metrics convert(const BfsStats &s) {
  Metrics m;
  m.visited = s.visited;
  m.roots = s.roots;
  m.events = s.visited;
  m.aux_fixed = s.aux_fixed_bytes;
  m.aux_dynamic = s.aux_dynamic_peak_bytes;
  m.aux_peak = s.aux_peak_bytes;
  m.id_bits = s.id_bits;
  m.edges = s.edges_scanned;
  m.rounds = s.rounds;
  m.frontier = s.max_frontier;
  m.pending = s.max_pending;
  return m;
}
inline Metrics convert(const DfsStats &s) {
  Metrics m;
  m.visited = s.discovered;
  m.roots = s.roots;
  m.events = 2 * s.discovered;
  m.aux_fixed = s.aux_fixed_bytes;
  m.aux_dynamic = s.aux_dynamic_peak_bytes;
  m.aux_peak = s.aux_peak_bytes;
  m.id_bits = s.id_bits;
  m.depth = s.max_depth;
  m.finished = s.finished;
  m.child_scans = s.child_scans;
  m.recoveries = s.parent_recoveries;
  m.probes = s.recover_probes;
  m.incoming_calls = s.incoming_calls;
  return m;
}
template <class Oracle, class B, class D, class F>
Metrics run_algorithm(const Oracle &g, const Options &o, B bfs, D discover,
                      F finish) {
  if (o.method == "bfs-se")
    return convert(se_bfs(g, o.source, bfs, o.forest));
  if (o.method == "bfs-se32")
    return convert(se_bfs<uint32_t>(g, o.source, bfs, o.forest));
  if (o.method == "bfs-base")
    return convert(baseline_bfs(g, o.source, bfs, o.forest));
  if (o.method == "bfs-base32")
    return convert(baseline_bfs<uint32_t>(g, o.source, bfs, o.forest));
  if (o.method == "bfs-stream")
    return convert(streaming_bfs(g, o.source, bfs, o.forest));
  if (o.method == "bfs-stream32")
    return convert(streaming_bfs<uint32_t>(g, o.source, bfs, o.forest));
  if (o.method == "dfs-se") {
    ChoiceDictionary vis(g.N());
    TwoBitArray parent(g.N());
    return convert(
        se_dfs(g, o.source, vis, parent, discover, finish, o.forest));
  }
  if (o.method == "dfs-base" || o.method == "dfs-base32") {
    std::vector<uint8_t> vis(g.N(), 0);
    return o.method == "dfs-base"
               ? convert(baseline_dfs_explicit(g, o.source, vis, discover,
                                               finish, o.forest))
               : convert(baseline_dfs_explicit<uint32_t>(
                     g, o.source, vis, discover, finish, o.forest));
  }
  ChoiceDictionary vis(g.N());
  return o.method == "dfs-packed"
             ? convert(packed_dfs_explicit(g, o.source, vis, discover, finish,
                                           o.forest))
             : convert(packed_dfs_explicit<uint32_t>(g, o.source, vis, discover,
                                                     finish, o.forest));
}
template <class Oracle>
int benchmark(const Oracle &g, Options o, const std::string &backend,
              uint32_t k, double load_ms) {
  auto scan_start = Clock::now();
  uint64_t valid = 0;
  node_t first = kNoNode;
  for (node_t v = 0; v < g.N(); ++v)
    if (g.valid(v)) {
      ++valid;
      if (first == kNoNode)
        first = v;
    }
  double scan_ms = milliseconds(scan_start);
  if (!o.forest && o.source == kNoNode)
    o.source = first;
  if (!o.forest && (o.source >= g.N() || !g.valid(o.source)))
    throw std::invalid_argument("source is invalid or graph has no valid "
                                "nodes; use --forest for an empty graph");
  long graph_rss = rss_kb(), graph_peak = peak_kb();
  std::string audit = o.audit ? audit_graph(g) : "null";
  Metrics m;
  double wall;
  std::string digest;
  uint64_t output_bytes = 0;
  rusage before{}, after{};
  getrusage(RUSAGE_SELF, &before);
  if (o.hash) {
    EventRecorder events(o.events, true, g.N());
    auto start = Clock::now();
    m = run_algorithm(
        g, o, [&](node_t v, uint64_t d) { events.bfs(v, d); },
        [&](node_t v, node_t p) { events.discover(v, p); },
        [&](node_t v) { events.finish(v); });
    events.close();
    wall = milliseconds(start);
    digest = events.digest_hex();
    output_bytes = events.output_bytes();
    if (events.event_count() != m.events)
      throw std::runtime_error(
          "callback count disagrees with traversal statistics");
    if (o.method.compare(0, 3, "dfs") == 0 && events.max_depth() != m.depth)
      throw std::runtime_error(
          "callback depth disagrees with traversal statistics");
  } else {
    auto start = Clock::now();
    m = run_algorithm(
        g, o, [](node_t, uint64_t) {}, [](node_t, node_t) {}, [](node_t) {});
    wall = milliseconds(start);
  }
  getrusage(RUSAGE_SELF, &after);
  if (o.forest && m.visited != valid)
    throw std::runtime_error("forest did not visit every valid identifier");
  if (m.visited > valid || m.roots > m.visited)
    throw std::runtime_error("invalid traversal counts");
  if (o.method.compare(0, 3, "dfs") == 0 && m.finished != m.visited)
    throw std::runtime_error("DFS discovery/finish counts differ");
  Json j;
  j.number("schema_version", 1);
  j.string("status", "ok");
  j.string("backend", backend);
  j.string("input", o.input);
  j.string("method", o.method);
  j.string("family", o.method.substr(0, 3));
  j.number("N", g.N());
  j.number("valid_nodes", valid);
  j.number("k", k);
  j.boolean("forest", o.forest);
  if (o.forest)
    j.raw("source", "null");
  else
    j.number("source", o.source);
  j.number("seed", o.seed);
  j.number("visited", m.visited);
  j.number("roots", m.roots);
  j.number("event_count", m.events);
  j.string("verification", o.hash ? "sha256" : "none");
  j.raw("digest_sha256", o.hash ? quote(digest) : "null");
  j.number("event_file_bytes", output_bytes);
  j.raw("audit", audit);
  j.number("load_ms", load_ms);
  j.number("valid_scan_ms", scan_ms);
  j.number("wall_ms", wall);
  j.number("cpu_ms", cpu_ms(after) - cpu_ms(before));
  j.number("major_faults", after.ru_majflt - before.ru_majflt);
  j.number("minor_faults", after.ru_minflt - before.ru_minflt);
  j.number("graph_rss_kb", graph_rss);
  j.number("graph_peak_rss_kb", graph_peak);
  j.number("peak_rss_kb", peak_kb());
  j.number("rss_after_kb", rss_kb());
  j.number("swap_kb", status_kb("VmSwap:"));
  j.number("aux_fixed_bytes", m.aux_fixed);
  j.number("aux_dynamic_peak_bytes", m.aux_dynamic);
  j.number("aux_peak_bytes", m.aux_peak);
  j.string("aux_accounting", "requested_heap_bytes_including_reallocation_"
                             "overlap_excludes_allocator_metadata_and_output");
  j.number("id_bits", m.id_bits);
  j.number("edges_scanned", m.edges);
  j.number("rounds", m.rounds);
  j.number("max_frontier", m.frontier);
  j.number("max_pending", m.pending);
  j.number("max_depth", m.depth);
  j.number("finished", m.finished);
  j.number("child_scans", m.child_scans);
  j.number("parent_recoveries", m.recoveries);
  j.number("recover_probes", m.probes);
  j.number("incoming_calls", m.incoming_calls);
  j.string("compiler", __VERSION__);
  j.string("build_revision", SEGA_REVISION);
  j.string("build_source_sha256", SEGA_SOURCE_SHA256);
  j.string("megahit_revision", SEGA_MEGAHIT_REVISION);
  j.string("build_flags", SEGA_BUILD_FLAGS);
  std::cout << j.str() << '\n';
  return 0;
}
inline int report_error(const std::exception &e) {
  Json j;
  j.number("schema_version", 1);
  j.string("status", "error");
  j.string("error", e.what());
  std::cout << j.str() << '\n';
  std::cerr << "sega: " << e.what() << '\n';
  return 1;
}
} // namespace sega_bench
