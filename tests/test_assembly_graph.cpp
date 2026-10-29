#include "sega/assembly_graph.hpp"
#include <cstdio>
#include <iostream>
#include <unistd.h>

struct Fixture {
    char path[64] = "/tmp/sega_assembly_XXXXXX";
    explicit Fixture(const std::string& text) {
        int fd = mkstemp(path);
        if (fd < 0) throw std::runtime_error("fixture create");
        FILE* file = fdopen(fd, "w");
        if (!file) { close(fd); throw std::runtime_error("fixture open"); }
        if (fwrite(text.data(), 1, text.size(), file) != text.size()) { fclose(file); throw std::runtime_error("fixture write"); }
        if (fclose(file)) throw std::runtime_error("fixture close");
    }
    ~Fixture() { unlink(path); }
};
void check(bool ok) { if (!ok) throw std::runtime_error("assembly check failed"); }
int main() {
    try {
        Fixture file("S\ta\tACGTA\tKC:i:10\nS\tb\tGTACCCT\tKC:i:12\nS\tc\tA\tKC:i:1\n"
                     "L\ta\t+\tb\t+\t2M\nL\tb\t-\ta\t-\t2M\nP\tscaffold\ta+,c+\t*\n");
        sega::AssemblyGraph g(file.path);
        check(g.num_vertices() == 6 && g.num_edges() == 2 && g.num_bases() == 13);
        check(g.packed_dna_bytes() == 4 && g.topology_storage().incoming == 0);
        check(g.sequence(g.lookup("a-")) == "TACGT" && g.sequence(g.lookup("b-")) == "AGGGTAC");
        check(g.sequence(g.lookup("c+")) == "A" && g.sequence(g.lookup("c-")) == "T");
        auto out = g.neighbors(g.lookup("a+"), false), in = g.neighbors(g.lookup("b+"), true);
        check(out.size() == 1 && in.size() == 1 && out[0].weight == 5 && in[0].weight == 3);
        check(out[0].overlap == in[0].overlap && g.coverage(g.lookup("b-")) == 12);
        check(g.neighbors(g.lookup("c+"), true).empty());
        for (unsigned repeat = 0; repeat < 100; ++repeat) {
            sega::AssemblyGraph again(file.path);
            check(again.neighbors(again.lookup("a+"), false)[0].node == again.lookup("b+"));
        }
        bool failed = false;
        try { g.neighbors(6, true); } catch (const std::out_of_range&) { failed = true; }
        check(failed);
        Fixture bad("S\ta\tACGT\tKC:i:1\nL\ta\t+\ta\t+\t5M\n");
        failed = false;
        try { sega::AssemblyGraph invalid(bad.path); } catch (const std::invalid_argument&) { failed = true; }
        check(failed);
        Fixture empty(""); sega::AssemblyGraph e(empty.path); check(e.num_vertices() == 0 && e.num_edges() == 0);
        std::cout << "Assembly navigation, reverse weights, packed DNA, lifecycle and rejection checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
