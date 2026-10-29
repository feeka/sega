#include "benchmark_driver.hpp"
#include <random>

// Controlled bounded-degree graphs for frontier width/depth experiments.
static sdbgt::InMemoryOracle synthetic(const sega_bench::Options &o) {
  using namespace sdbgt;
  if (o.nodes > std::numeric_limits<size_t>::max())
    throw std::overflow_error("nodes exceeds size_t");
  const size_t n = size_t(o.nodes);
  std::vector<std::pair<node_t, node_t>> edges;
  if (o.input == "path" || o.input == "cycle" || o.input == "disconnected") {
    for (node_t v = 1; v < n; ++v)
      if (o.input != "disconnected" || v % 17 != 0)
        edges.emplace_back(v - 1, v);
    if (o.input == "cycle" && n)
      edges.emplace_back(n - 1, 0);
  } else if (o.input == "wide") {
    for (node_t v = 1; v < n; ++v)
      edges.emplace_back((v - 1) / 4, v);
  } else if (o.input == "random") {
    std::mt19937_64 rng(o.seed);
    std::vector<uint8_t> in(n, 0), out(n, 0);
    if (n > std::numeric_limits<size_t>::max() / 3)
      throw std::overflow_error("graph too large");
    for (size_t i = 0; n && i < n * 3; ++i) {
      node_t u = rng() % n, v = rng() % n;
      if (in[v] < 4 && out[u] < 4) {
        edges.emplace_back(u, v);
        ++in[v];
        ++out[u];
      }
    }
  } else
    throw std::invalid_argument("unknown synthetic shape: " + o.input);
  return InMemoryOracle(n, edges);
}
int main(int argc, char **argv) {
  try {
    auto o = sega_bench::parse(argc, argv, true);
    auto start = sega_bench::Clock::now();
    auto g = synthetic(o);
    return sega_bench::benchmark(g, o, "synthetic", 0,
                                 sega_bench::milliseconds(start));
  } catch (const std::exception &e) {
    return sega_bench::report_error(e);
  }
}
