// sdbg_report.cpp — one command: traverse + check + benchmark a real succinct
// de Bruijn graph, and emit a self-contained HTML report (aux space + runtime).
//
// Correctness: se_bfs/se_dfs vs textbook baselines on the SAME graph (+ an
// InMemory BFS cross-check when small). Benchmark: each of the 4 methods is run
// in its OWN child process (this same binary, --bench-one) so its peak RSS
// (VmHWM from /proc) is isolated and not polluted by the others.
//
// Build: `cmake -B build && cmake --build build` (megahit vendored at libs/megahit).
// Manual (from repo root; needs gnu++17/-D_GNU_SOURCE for popen):
//   MH=libs/megahit/src
//   g++ -std=gnu++17 -O2 -march=native -pthread -DSDBGT_WITH_MEGAHIT
//     -Iinclude -Ibackends -I$MH bench/sdbg_report.cpp
//     $MH/sdbg/sdbg_meta.cpp $MH/sdbg/sdbg_raw_content.cpp -o sdbg_report
//
// Use:
//   sdbg_report <graph_prefix> [out.html]          # full report
//   sdbg_report --bench-one <graph_prefix> <mode>  # internal (one method)
#define SDBGT_WITH_MEGAHIT 1
#include "sdbg_traverse/nav_oracle.hpp"
#include "sdbg_traverse/choice_dictionary.hpp"
#include "sdbg_traverse/bfs.hpp"
#include "sdbg_traverse/dfs.hpp"
#include "megahit_oracle.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <map>
#include <vector>
#include <sstream>
#include <fstream>
#include <chrono>

using namespace sdbgt;
using clk = std::chrono::steady_clock;

static long status_kb(const char* key) {
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256]; long val = -1; size_t klen = std::strlen(key);
    while (std::fgets(line, sizeof line, f))
        if (std::strncmp(line, key, klen) == 0) { std::sscanf(line + klen, " %ld", &val); break; }
    std::fclose(f);
    return val;
}
static node_t first_valid(const MegaHitOracle& o) {
    for (node_t v = 0; v < o.N(); ++v) if (o.valid(v)) return v;
    return kNoNode;
}

// ---------- child: benchmark exactly one method, print one metrics line -------
static int bench_one(const char* prefix, const std::string& mode) {
    SDBG g; g.LoadFromFile(prefix);
    MegaHitOracle o(g);
    const size_t N = o.N();
    node_t src = first_valid(o);
    long graph_rss = status_kb("VmRSS:");
    long aux_fixed = 0, aux_alive = -1; unsigned long long visited = 0;
    auto t0 = clk::now();
    if (mode == "bfs-se") {
        ChoiceDictionary cd(N); aux_fixed = (long)cd.resident_bytes();
        visited = se_bfs(o, src, cd, [](node_t,uint64_t){}).visited;
        aux_alive = status_kb("VmRSS:");
    } else if (mode == "bfs-base") {
        aux_fixed = (long)(N * sizeof(uint32_t));
        visited = baseline_bfs(o, src, [](node_t,uint64_t){}).visited;
    } else if (mode == "dfs-se") {
        ChoiceDictionary vis(N); TwoBitArray ps(N);
        aux_fixed = (long)(vis.resident_bytes() + ps.resident_bytes());
        visited = se_dfs(o, src, vis, ps, [](node_t,node_t){}, [](node_t){}).discovered;
        aux_alive = status_kb("VmRSS:");
    } else if (mode == "dfs-base") {
        std::vector<uint8_t> state(N, 0); aux_fixed = (long)state.size();
        visited = baseline_dfs_explicit(o, src, state, [](node_t,node_t){}, [](node_t){}).discovered;
        aux_alive = status_kb("VmRSS:");
    } else { std::fprintf(stderr, "bad mode\n"); return 2; }
    double wall = std::chrono::duration<double,std::milli>(clk::now()-t0).count();
    std::printf("mode=%s N=%zu k=%u visited=%llu graph_rss_kb=%ld aux_fixed_bytes=%ld "
                "aux_alive_rss_kb=%ld peak_hwm_kb=%ld wall_ms=%.1f\n",
                mode.c_str(), N, g.k(), visited, graph_rss, aux_fixed, aux_alive,
                status_kb("VmHWM:"), wall);
    return 0;
}

struct Metrics { double aux_fixed=0, graph_rss=0, peak=0, wall=0, vis=0; bool ok=false; };
static Metrics run_child(const char* self, const char* prefix, const char* mode) {
    Metrics m;
    std::string cmd = std::string("\"") + self + "\" --bench-one \"" + prefix + "\" " + mode;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return m;
    char line[512] = {0};
    if (std::fgets(line, sizeof line, p)) {
        std::istringstream is(line); std::string tok;
        while (is >> tok) {
            auto eq = tok.find('=');
            if (eq == std::string::npos) continue;
            std::string k = tok.substr(0, eq); double v = std::atof(tok.c_str()+eq+1);
            if (k=="aux_fixed_bytes") m.aux_fixed=v;
            else if (k=="graph_rss_kb") m.graph_rss=v;
            else if (k=="peak_hwm_kb") m.peak=v;
            else if (k=="wall_ms") m.wall=v;
            else if (k=="visited") m.vis=v;
        }
        m.ok = true;
    }
    pclose(p);
    return m;
}

// ---------- HTML (self-contained; same look as the published report) ----------
static const char* CSS = R"CSS(
:root{--bg:#F3F4F1;--panel:#FFFFFF;--ink:#171A20;--muted:#5C6472;--line:#DCDFD8;--accent:#9A6510;--accent-soft:#B5771F;--pass:#1E7A46;--warn:#B4531B;--term-bg:#12151B;--term-ink:#D7DCE4;--term-dim:#79828F;--term-green:#5BC98A;--term-amber:#E3A94A;--sans:ui-sans-serif,system-ui,-apple-system,"Segoe UI",Helvetica,Arial,sans-serif;--mono:ui-monospace,"SF Mono","JetBrains Mono",Consolas,monospace;--maxw:960px}
@media (prefers-color-scheme:dark){:root{--bg:#0E1116;--panel:#161A22;--ink:#E7EAF0;--muted:#8C95A4;--line:#262C36;--accent:#E3A94A;--accent-soft:#E3A94A;--pass:#4CC585;--warn:#E0894A}}
:root[data-theme="dark"]{--bg:#0E1116;--panel:#161A22;--ink:#E7EAF0;--muted:#8C95A4;--line:#262C36;--accent:#E3A94A;--accent-soft:#E3A94A;--pass:#4CC585;--warn:#E0894A}
:root[data-theme="light"]{--bg:#F3F4F1;--panel:#FFFFFF;--ink:#171A20;--muted:#5C6472;--line:#DCDFD8;--accent:#9A6510;--accent-soft:#B5771F;--pass:#1E7A46;--warn:#B4531B}
*{box-sizing:border-box}body{background:var(--bg);color:var(--ink);font-family:var(--sans);line-height:1.6;margin:0;-webkit-font-smoothing:antialiased}
.wrap{max-width:var(--maxw);margin:0 auto;padding:clamp(1.2rem,4vw,3rem) clamp(1rem,4vw,2rem)}
h1,h2{text-wrap:balance;line-height:1.15;font-weight:680;letter-spacing:-.01em;margin:0}
h1{font-size:clamp(1.6rem,4vw,2.3rem)}h2{font-size:clamp(1.15rem,2.5vw,1.45rem);margin-bottom:.3rem}
p{margin:.6rem 0}.prose{max-width:66ch}.num{font-family:var(--mono);font-variant-numeric:tabular-nums}
.eyebrow{font-family:var(--mono);font-size:.72rem;letter-spacing:.16em;text-transform:uppercase;color:var(--accent-soft);display:flex;gap:.6rem;align-items:center;border-top:1px solid var(--line);padding-top:.4rem;margin-bottom:.9rem}
.eyebrow::before{content:"";width:26px;height:2px;background:var(--accent-soft)}
.sub{color:var(--muted);margin-top:.6rem}section{margin-top:clamp(2rem,5vw,3.2rem)}
.stats{display:flex;flex-wrap:wrap;gap:.9rem;margin-top:1.4rem}
.stat{flex:1 1 150px;background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:.85rem .95rem}
.stat .k{font-family:var(--mono);font-size:1.45rem;font-variant-numeric:tabular-nums;letter-spacing:-.02em}
.stat .k b{color:var(--accent);font-weight:680}.stat .l{font-size:.78rem;color:var(--muted);margin-top:.15rem}
.chip{display:inline-flex;align-items:center;gap:.4rem;font-family:var(--mono);font-size:.74rem;padding:.18rem .55rem;border-radius:999px;border:1px solid var(--line);color:var(--muted);background:var(--panel)}
.chip.pass{color:var(--pass);border-color:color-mix(in srgb,var(--pass) 40%,var(--line))}.chip.pass::before{content:"";width:7px;height:7px;border-radius:50%;background:var(--pass)}
.chip.fail{color:var(--warn);border-color:color-mix(in srgb,var(--warn) 40%,var(--line))}
.scroll{overflow-x:auto;border:1px solid var(--line);border-radius:10px;background:var(--panel);margin-top:1rem}
table{border-collapse:collapse;width:100%;font-size:.86rem;min-width:520px}
th,td{text-align:right;padding:.55rem .8rem;border-bottom:1px solid var(--line)}th:first-child,td:first-child{text-align:left}
thead th{font-family:var(--mono);font-size:.72rem;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);font-weight:600}
tbody tr:last-child td{border-bottom:none}.mono{font-family:var(--mono);font-variant-numeric:tabular-nums}
.ours{color:var(--pass);font-weight:600}.them{color:var(--muted)}
.term{background:var(--term-bg);color:var(--term-ink);border-radius:10px;padding:1rem 1.1rem;font-family:var(--mono);font-size:.8rem;line-height:1.65;overflow-x:auto;border:1px solid #232a35;white-space:pre-wrap;margin-top:1rem}
.term .ok{color:var(--term-green)}.term .am{color:var(--term-amber)}.term .p{color:var(--term-dim)}
.bars{display:flex;flex-direction:column;gap:.9rem;margin-top:1.2rem}
.barrow{display:grid;grid-template-columns:150px 1fr;gap:.8rem;align-items:center}
.barrow .lab{font-family:var(--mono);font-size:.76rem;color:var(--muted)}
.track{position:relative;background:color-mix(in srgb,var(--line) 55%,transparent);border-radius:6px;height:26px}
.bar{height:100%;border-radius:6px;display:flex;align-items:center;justify-content:flex-end;padding-right:.5rem;font-family:var(--mono);font-size:.74rem;font-variant-numeric:tabular-nums;color:#fff;min-width:46px}
.bar.se{background:var(--pass)}.bar.base{background:color-mix(in srgb,var(--warn) 78%,#000 4%)}
.floor{position:absolute;top:-4px;bottom:-4px;border-left:2px dashed var(--muted);opacity:.7}
.callout{border-left:3px solid var(--warn);background:color-mix(in srgb,var(--warn) 9%,var(--panel));padding:.8rem 1rem;border-radius:0 8px 8px 0;margin-top:1.2rem}
.callout .t{font-family:var(--mono);font-size:.74rem;text-transform:uppercase;letter-spacing:.08em;color:var(--warn)}
footer{margin-top:3rem;border-top:1px solid var(--line);padding-top:1rem;color:var(--muted);font-size:.78rem;font-family:var(--mono)}
)CSS";

static std::string fmt_mb(double bytes) { char b[32]; std::snprintf(b,sizeof b,"%.2f MB", bytes/1048576.0); return b; }
static std::string fmt_mb_kb(double kb) { char b[32]; std::snprintf(b,sizeof b,"%.1f MB", kb/1024.0); return b; }
static std::string fmt_ms(double ms) { char b[32]; std::snprintf(b,sizeof b,"%.0f ms", ms); return b; }

int main(int argc, char** argv) {
    if (argc >= 4 && std::strcmp(argv[1], "--bench-one") == 0)
        return bench_one(argv[2], argv[3]);
    if (argc < 2) { std::fprintf(stderr, "usage: sdbg_report <graph_prefix> [out.html]\n"); return 2; }
    const char* prefix = argv[1];
    std::string out = argc > 2 ? argv[2] : "sdbg_report.html";

    // ---- load + correctness (in parent) ----
    SDBG g; g.LoadFromFile(prefix);
    MegaHitOracle o(g);
    const size_t N = o.N();
    size_t valid = 0; node_t src = kNoNode;
    for (node_t v = 0; v < N; ++v) if (o.valid(v)) { ++valid; if (src==kNoNode) src=v; }
    std::printf("loaded '%s': N=%zu k=%u valid=%zu\n", prefix, N, g.k(), valid);

    std::map<node_t,uint64_t> bfs_se, bfs_base; bool bfs_ok=false, bfs_x=false, bfs_x_run=false;
    {
        ChoiceDictionary cd(N);
        se_bfs(o, src, cd, [&](node_t v,uint64_t d){ bfs_se[v]=d; });
        baseline_bfs(o, src, [&](node_t v,uint64_t d){ bfs_base[v]=d; });
        bfs_ok = (bfs_se == bfs_base);
        if (N <= 5000000) {
            bfs_x_run = true;
            std::vector<std::pair<node_t,node_t>> e; node_t nb[kSigma];
            for (node_t v=0; v<N; ++v) if (o.valid(v)) { int d=o.outgoing(v,nb); for(int i=0;i<d;++i) e.push_back({v,nb[i]}); }
            InMemoryOracle ref(N, e); std::map<node_t,uint64_t> bfs_ref;
            baseline_bfs(ref, src, [&](node_t v,uint64_t d){ bfs_ref[v]=d; });
            bfs_x = (bfs_se == bfs_ref);
        }
    }
    bool dfs_okd=false, dfs_okf=false, dfs_okp=false; double dfs_tax=0; size_t reach=0;
    {
        std::vector<node_t> da,fa,pa(N,kNoNode),db,fb,pb(N,kNoNode);
        ChoiceDictionary vis(N); TwoBitArray ps(N);
        auto s = se_dfs(o, src, vis, ps, [&](node_t v,node_t p){da.push_back(v);pa[v]=p;}, [&](node_t v){fa.push_back(v);});
        std::vector<uint8_t> st(N,0);
        baseline_dfs_explicit(o, src, st, [&](node_t v,node_t p){db.push_back(v);pb[v]=p;}, [&](node_t v){fb.push_back(v);});
        dfs_okd=(da==db); dfs_okf=(fa==fb); dfs_okp=(pa==pb);
        dfs_tax = s.finished ? double(s.recover_probes)/s.finished : 0; reach = s.discovered;
    }
    bool all_ok = bfs_ok && dfs_okd && dfs_okf && dfs_okp && (!bfs_x_run || bfs_x);

    // ---- benchmark (isolated child per method) ----
    Metrics bse = run_child(argv[0], prefix, "bfs-se");
    Metrics bba = run_child(argv[0], prefix, "bfs-base");
    Metrics dse = run_child(argv[0], prefix, "dfs-se");
    Metrics dba = run_child(argv[0], prefix, "dfs-base");
    double maxpeak = std::max(std::max(bse.peak,bba.peak), std::max(dse.peak,dba.peak));
    if (maxpeak <= 0) maxpeak = 1;
    double floorpct = 100.0 * bse.graph_rss / maxpeak;
    auto W = [&](double peak){ char b[16]; std::snprintf(b,sizeof b,"%.1f", 100.0*peak/maxpeak); return std::string(b); };

    // basename of graph
    std::string gp = prefix; auto sl = gp.find_last_of("/\\"); std::string gname = sl==std::string::npos?gp:gp.substr(sl+1);

    // ---- emit HTML ----
    std::ostringstream h;
    h << "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
      << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      << "<title>sdbg-traverse report — " << gname << "</title><style>" << CSS << "</style></head><body><div class=\"wrap\">";

    // header
    h << "<div class=\"eyebrow\">sdbg-traverse report</div>"
      << "<h1>Space-efficient traversal on a real succinct de Bruijn graph</h1>"
      << "<p class=\"sub\">Graph <span class=\"num\">" << gname << "</span> &middot; "
      << "<span class=\"num\">" << N << "</span> nodes &middot; k=" << g.k()
      << " &middot; reachable from source: <span class=\"num\">" << reach << "</span></p>"
      << "<div class=\"stats\">"
      << "<div class=\"stat\"><div class=\"k\"><b>" << N << "</b></div><div class=\"l\">nodes (edges) in graph</div></div>"
      << "<div class=\"stat\"><div class=\"k\"><b>" << fmt_mb_kb(bse.peak) << "</b></div><div class=\"l\">peak RAM, our BFS vs " << fmt_mb_kb(bba.peak) << " textbook</div></div>"
      << "<div class=\"stat\"><div class=\"k\"><b>" << (bba.aux_fixed>0?bba.aux_fixed/ (bse.aux_fixed>0?bse.aux_fixed:1):0) << "×</b></div><div class=\"l\">smaller BFS bookkeeping</div></div>";
    if (all_ok) h << "<div class=\"stat\"><div class=\"k\" style=\"color:var(--pass)\">PASS</div><div class=\"l\">exact match to textbook</div></div>";
    else        h << "<div class=\"stat\"><div class=\"k\" style=\"color:var(--warn)\">FAIL</div><div class=\"l\">mismatch — see below</div></div>";
    h << "</div>";

    // correctness
    auto YN = [](bool b){ return b ? "<span class=\"ok\">YES</span>" : "<span class=\"am\">NO</span>"; };
    auto OK = [](bool b){ return b ? "<span class=\"ok\">OK</span>" : "<span class=\"am\">MISMATCH</span>"; };
    h << "<section><div class=\"eyebrow\">Correctness</div><h2>Exact match to textbook traversals</h2>"
      << "<p class=\"prose\" style=\"color:var(--muted)\">Space-efficient and textbook traversals run on the same graph; BFS distances and DFS discovery/finish/parent maps must be identical.</p>"
      << "<div class=\"term\">"
      << "<span class=\"p\">$</span> sdbg_report " << gname << "\n"
      << "SDBG loaded: <span class=\"am\">N=" << N << "</span> edges, k=" << g.k() << "\n"
      << "[BFS] se vs baseline distances match: " << YN(bfs_ok) << "\n";
    if (bfs_x_run) h << "[BFS] cross-check vs InMemory reference: " << YN(bfs_x) << "\n";
    else h << "[BFS] InMemory cross-check skipped (N too large)\n";
    h << "[DFS] discovery:" << OK(dfs_okd) << " finish:" << OK(dfs_okf) << " parents:" << OK(dfs_okp) << "\n"
      << "[DFS] reconstruction tax = <span class=\"am\">" << dfs_tax << "</span> in-neighbour probes / finish\n"
      << "RESULT: " << (all_ok?"<span class=\"ok\">ALL CHECKS PASSED</span>":"<span class=\"am\">CHECK(S) FAILED</span>") << " (on a real MEGAHIT SDBG)"
      << "</div></section>";

    // benchmark table
    h << "<section><div class=\"eyebrow\">Measurement</div><h2>Auxiliary memory &amp; runtime</h2>"
      << "<p class=\"prose\" style=\"color:var(--muted)\">Peak RAM is each method's process high-water mark (VmHWM, /proc), measured in its own process. Graph floor &asymp; " << fmt_mb_kb(bse.graph_rss) << ".</p>"
      << "<div class=\"scroll\"><table><thead><tr><th>Method</th><th>Aux memory</th><th>Peak RAM</th><th>Wall time</th></tr></thead><tbody>"
      << "<tr><td><span class=\"ours\">BFS — space-efficient</span></td><td class=\"mono ours\">" << fmt_mb(bse.aux_fixed) << "</td><td class=\"mono ours\">" << fmt_mb_kb(bse.peak) << "</td><td class=\"mono\">" << fmt_ms(bse.wall) << "</td></tr>"
      << "<tr><td><span class=\"them\">BFS — textbook</span></td><td class=\"mono them\">" << fmt_mb(bba.aux_fixed) << "</td><td class=\"mono them\">" << fmt_mb_kb(bba.peak) << "</td><td class=\"mono\">" << fmt_ms(bba.wall) << "</td></tr>"
      << "<tr><td><span class=\"ours\">DFS — space-efficient</span></td><td class=\"mono ours\">" << fmt_mb(dse.aux_fixed) << "</td><td class=\"mono ours\">" << fmt_mb_kb(dse.peak) << "</td><td class=\"mono\">" << fmt_ms(dse.wall) << "</td></tr>"
      << "<tr><td><span class=\"them\">DFS — textbook</span></td><td class=\"mono them\">" << fmt_mb(dba.aux_fixed) << "</td><td class=\"mono them\">" << fmt_mb_kb(dba.peak) << "</td><td class=\"mono\">" << fmt_ms(dba.wall) << "</td></tr>"
      << "</tbody></table></div>";

    // bars
    h << "<div class=\"bars\">"
      << "<div class=\"barrow\"><div class=\"lab\">BFS &middot; ours</div><div class=\"track\"><div class=\"floor\" style=\"left:" << floorpct << "%\"></div><div class=\"bar se\" style=\"width:" << W(bse.peak) << "%\">" << fmt_mb_kb(bse.peak) << "</div></div></div>"
      << "<div class=\"barrow\"><div class=\"lab\">BFS &middot; textbook</div><div class=\"track\"><div class=\"floor\" style=\"left:" << floorpct << "%\"></div><div class=\"bar base\" style=\"width:" << W(bba.peak) << "%\">" << fmt_mb_kb(bba.peak) << "</div></div></div>"
      << "<div class=\"barrow\"><div class=\"lab\">DFS &middot; ours</div><div class=\"track\"><div class=\"floor\" style=\"left:" << floorpct << "%\"></div><div class=\"bar se\" style=\"width:" << W(dse.peak) << "%\">" << fmt_mb_kb(dse.peak) << "</div></div></div>"
      << "<div class=\"barrow\"><div class=\"lab\">DFS &middot; textbook</div><div class=\"track\"><div class=\"floor\" style=\"left:" << floorpct << "%\"></div><div class=\"bar base\" style=\"width:" << W(dba.peak) << "%\">" << fmt_mb_kb(dba.peak) << "</div></div></div>"
      << "</div>";

    // auto caveat if bfs-se is much slower
    if (bba.wall > 0 && bse.wall > 10 * bba.wall)
        h << "<div class=\"callout\"><div class=\"t\">Time caveat (measured)</div><p style=\"margin:.4rem 0 0\">Space-efficient BFS ran in <b class=\"num\">" << fmt_ms(bse.wall) << "</b> vs <b class=\"num\">" << fmt_ms(bba.wall) << "</b> textbook on this graph — the v1 choice dictionary rescans the bit-array per BFS level, which is costly on high-diameter (chain-like) graphs. Fix: a constant-time frontier structure (Kammer–Sajenko).</p></div>";
    h << "</section>";

    h << "<footer>sdbg-traverse &middot; MEGAHIT SDBG backend &middot; peak RAM = VmHWM per isolated process &middot; single-source. "
      << "All figures are captured program output.</footer>";
    h << "</div></body></html>";

    std::ofstream f(out);
    if (!f) { std::fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
    f << h.str(); f.close();
    std::printf("wrote report: %s  (correctness: %s)\n", out.c_str(), all_ok ? "ALL PASSED" : "FAILED");
    return all_ok ? 0 : 1;
}
