#include "benchmark_driver.hpp"
#include "megahit_oracle.hpp"
#include "sdbg/sdbg_item.h"
#include <filesystem>
#include <map>
#include <set>

// MEGAHIT's loader assumes valid framing; preflight files in Release too.
static void check_files(const std::string &prefix) {
  std::ifstream in(prefix + ".sdbg_info");
  if (!in)
    throw std::runtime_error("cannot read graph metadata: " + prefix +
                             ".sdbg_info");
  auto field = [&](const char *expected) {
    std::string key, value;
    if (!(in >> key >> value) || key != expected)
      throw std::runtime_error(std::string("invalid SDBG metadata field: ") +
                               expected);
    return sega_bench::uint_arg(value, expected);
  };
  auto k = field("k"), words = field("words_per_tip_label"),
       buckets = field("num_buckets"), files = field("num_files");
  if (k == 0 || k > kMaxK || words != (k + 15) / 16 || buckets < 4 ||
      buckets % 4 || files == 0)
    throw std::runtime_error("unsupported or empty SDBG metadata");
  std::map<uint64_t, uint64_t> end_offsets;
  std::set<uint64_t> seen_buckets;
  uint64_t items = 0;
  auto sum = [](uint64_t a, uint64_t b) {
    if (a > UINT64_MAX - b)
      throw std::overflow_error("SDBG size overflow");
    return a + b;
  };
  auto mul = [](uint64_t a, uint64_t b) {
    if (b && a > UINT64_MAX / b)
      throw std::overflow_error("SDBG size overflow");
    return a * b;
  };
  for (uint64_t i = 0; i < buckets; ++i) {
    std::string values[6];
    uint64_t v[6];
    for (int j = 0; j < 6; ++j) {
      if (!(in >> values[j]))
        throw std::runtime_error("truncated SDBG metadata");
      v[j] = sega_bench::uint_arg(values[j], "bucket record");
    }
    if ((v[0] == UINT64_MAX) != (v[1] == UINT64_MAX))
      throw std::runtime_error("partially null SDBG bucket record");
    if (v[0] == UINT64_MAX) {
      if (v[2] || v[3] || v[4] || v[5])
        throw std::runtime_error("nonempty null SDBG bucket record");
      continue;
    }
    if (v[0] >= buckets || v[1] >= files || v[4] > v[3] || v[5] > v[3] ||
        !seen_buckets.insert(v[0]).second)
      throw std::runtime_error("invalid SDBG bucket record");
    uint64_t length = sum(
        mul(v[3], sizeof(SdbgItem)),
        sum(mul(v[4], words * sizeof(label_word_t)), mul(v[5], sizeof(mul_t))));
    if (v[2] != end_offsets[v[1]])
      throw std::runtime_error("noncontiguous SDBG file offsets");
    end_offsets[v[1]] = sum(v[2], length);
    items = sum(items, v[3]);
  }
  std::string extra;
  if (in >> extra)
    throw std::runtime_error("unexpected trailing SDBG metadata");
  if (!items)
    throw std::runtime_error("SDBG has no stored items");
  for (const auto &item : end_offsets) {
    const auto path = prefix + ".sdbg." + std::to_string(item.first);
    if (!std::filesystem::is_regular_file(path) ||
        std::filesystem::file_size(path) != item.second)
      throw std::runtime_error(
          "SDBG file missing or size disagrees with metadata: " + path);
  }
}
int main(int argc, char **argv) {
  try {
    auto o = sega_bench::parse(argc, argv, false);
    const auto start = sega_bench::Clock::now();
    check_files(o.input);
    SDBG graph;
    graph.LoadFromFile(o.input.c_str());
    sdbgt::MegaHitOracle oracle(graph);
    return sega_bench::benchmark(oracle, o, "megahit", graph.k(),
                                 sega_bench::milliseconds(start));
  } catch (const std::exception &e) {
    return sega_bench::report_error(e);
  }
}
