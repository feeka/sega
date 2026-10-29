// Read-only unitig view over MEGAHIT BOSS. No copied adjacency or stored DNA.
#pragma once
#include "megahit_oracle.hpp"
#include "sdbg_preflight.hpp"
#include "sdbg_traverse/validation.hpp"
#include <array>
#include <cstdio>
#include <fcntl.h>
#include <tuple>
#include <sstream>
#include <unistd.h>

namespace sega {
class MegaHitUnitigs {
public:
    struct Unitig { uint64_t begin, end, rbegin, rend, length, depth; bool cycle; };
    struct Arc { uint64_t node, overlap, weight; };
private:
    SDBG graph_;
    std::vector<Unitig> unitigs_;
    // Endpoint indexes contain no adjacency. They map native edge IDs to
    // oriented unitig IDs; palindromes deliberately have two oriented aliases.
    using Boundary = std::pair<uint64_t,uint64_t>;
    std::vector<Boundary> starts_, ends_;
    uint64_t valid_edges_ = 0, arcs_ = 0, cycles_ = 0, palindromes_ = 0;
    uint64_t scratch_bytes_ = 0;
    void vertex(uint64_t v) const { if (v >= num_vertices()) throw std::out_of_range("unitig ID out of range"); }
    uint64_t rc(uint64_t e) const {
        auto r = graph_.EdgeReverseComplement(e);
        if (r == SDBG::kNullID || r >= graph_.size() || !graph_.IsValidEdge(r))
            throw std::runtime_error("MEGAHIT graph is missing a reverse-complement edge");
        return r;
    }
    std::pair<uint64_t,uint64_t> endpoints(uint64_t v) const {
        vertex(v); const auto& u=unitigs_[v/2];
        return v%2 ? std::make_pair(u.rbegin,u.rend) : std::make_pair(u.begin,u.end);
    }
    static std::pair<uint64_t,uint64_t> order_key(uint64_t u,uint64_t v) {
        return std::min(std::make_pair(u,v),std::make_pair(v^1,u^1));
    }
public:
    explicit MegaHitUnitigs(const std::string& input) {
        auto prefix=sdbg_prefix(input); check_sdbg_files(prefix);
        graph_.LoadFromFile(prefix.c_str());
        std::vector<uint64_t> seen((graph_.size()+63)/64,0);
        scratch_bytes_=seen.size()*8;
        auto marked=[&](uint64_t e){return (seen[e/64]>>(e%64))&1;};
        auto mark=[&](uint64_t e){seen[e/64]|=uint64_t(1)<<(e%64);};
        auto add=[&](uint64_t begin,bool cycle) {
            uint64_t cur=begin,end=begin,length=0,depth=0;
            do {
                if (cur>=graph_.size() || !graph_.IsValidEdge(cur) || marked(cur))
                    throw std::runtime_error("non-simple or overlapping unitig construction");
                mark(cur); end=cur; ++length;
                const auto count=graph_.EdgeMultiplicity(cur);
                if (depth>UINT64_MAX-count) throw std::overflow_error("unitig coverage overflow");
                depth+=count;
                cur=graph_.NextSimplePathEdge(cur);
            } while (cur!=SDBG::kNullID && cur!=begin);
            if (cycle != (cur==begin)) throw std::runtime_error("unexpected unitig cycle boundary");
            const auto rbegin=rc(end),rend=rc(begin);
            uint64_t reverse_depth=0; cur=rbegin;
            for (uint64_t j=0;j<length;++j) {
                if (cur==SDBG::kNullID || cur>=graph_.size() || !graph_.IsValidEdge(cur))
                    throw std::runtime_error("broken reverse unitig");
                mark(cur); reverse_depth+=graph_.EdgeMultiplicity(cur);
                if (j+1<length) cur=graph_.NextSimplePathEdge(cur);
            }
            if (cur!=rend || reverse_depth!=depth) throw std::runtime_error("reverse unitig length/coverage mismatch");
            unitigs_.push_back({begin,end,rbegin,rend,length,depth,cycle});
            cycles_+=cycle; palindromes_+=begin==rbegin && end==rend;
        };
        for (uint64_t e=0;e<graph_.size();++e) {
            if (!graph_.IsValidEdge(e)) continue;
            ++valid_edges_;
            if (!marked(e) && graph_.PrevSimplePathEdge(e)==SDBG::kNullID) add(e,false);
        }
        for (uint64_t e=0;e<graph_.size();++e)
            if (graph_.IsValidEdge(e) && !marked(e)) add(e,true);
        unitigs_.shrink_to_fit(); starts_.reserve(num_vertices()); ends_.reserve(num_vertices());
        for (uint64_t v=0;v<num_vertices();++v) {
            auto pair=endpoints(v); starts_.emplace_back(pair.first,v); ends_.emplace_back(pair.second,v);
        }
        std::sort(starts_.begin(),starts_.end()); std::sort(ends_.begin(),ends_.end());
        // Multiplicities have been summed into each unitig. Release the per-edge
        // multiplicity storage, exactly as MEGAHIT's own unitig builder does.
        graph_.FreeMultiplicity();
        for (uint64_t v=0;v<num_vertices();++v) arcs_+=neighbors(v,false).size();
    }
    uint64_t num_vertices() const { return unitigs_.size()*2; }
    uint64_t num_edges() const { return arcs_; }
    uint64_t stored_edges() const { return graph_.size(); }
    uint64_t valid_edges() const { return valid_edges_; }
    uint64_t cycles() const { return cycles_; }
    uint64_t palindromes() const { return palindromes_; }
    uint64_t k() const { return graph_.k(); }
    uint64_t scratch_bytes() const { return scratch_bytes_; }
    uint64_t index_bytes() const { return unitigs_.capacity()*sizeof(Unitig)+(starts_.capacity()+ends_.capacity())*sizeof(Boundary); }
    const Unitig& unitig(uint64_t v) const { vertex(v); return unitigs_[v/2]; }
    uint64_t length(uint64_t v) const { return k()+unitig(v).length; }
    uint64_t coverage(uint64_t v) const { return unitig(v).depth; }
    std::string name(uint64_t v) const { vertex(v); return std::to_string(v/2)+(v%2?"-":"+"); }
    uint64_t lookup(const std::string& name) const {
        if (name.size()<2 || (name.back()!='+' && name.back()!='-')) throw std::invalid_argument("expected oriented unitig name");
        auto id=sdbg_uint(name.substr(0,name.size()-1));
        if (id>=unitigs_.size()) throw std::out_of_range("unitig name out of range");
        return id*2+(name.back()=='-');
    }
    std::string edge_sequence(uint64_t edge) const {
        if (edge>=graph_.size() || !graph_.IsValidEdge(edge)) throw std::out_of_range("invalid native edge");
        std::array<uint8_t,kMaxK+1> label{}; graph_.GetLabel(edge,label.data());
        std::string result; result.reserve(k()+1);
        for (uint64_t j=0;j<k();++j) {
            if (label[j]<1 || label[j]>4) throw std::runtime_error("invalid native DNA label");
            result.push_back("ACGT"[label[j]-1]);
        }
        auto w=graph_.GetW(edge); if (w>4) w-=4;
        if (!w || w>4) throw std::runtime_error("invalid native edge character");
        result.push_back("ACGT"[w-1]); return result;
    }
    std::string sequence(uint64_t v) const {
        auto pair=endpoints(v); auto current=pair.first;
        std::string s=edge_sequence(current); s.reserve(length(v));
        for (uint64_t j=1;j<unitig(v).length;++j) {
            current=graph_.NextSimplePathEdge(current);
            if (current==SDBG::kNullID) throw std::runtime_error("broken sequence path");
            auto w=graph_.GetW(current); if (w>4) w-=4;
            if (!w || w>4) throw std::runtime_error("invalid sequence symbol");
            s.push_back("ACGT"[w-1]);
        }
        if (current!=pair.second) throw std::runtime_error("unitig endpoint mismatch while spelling DNA");
        return s;
    }
    std::vector<Arc> neighbors(uint64_t v,bool reverse=false) const {
        auto pair=endpoints(v); uint64_t edges[4];
        sdbgt::MegaHitOracle oracle(graph_);
        const auto count=reverse?oracle.incoming(pair.first,edges):oracle.outgoing(pair.second,edges);
        const auto& index=reverse?ends_:starts_;
        std::vector<Arc> result;
        for (int i=0;i<count;++i) {
            auto it=std::lower_bound(index.begin(),index.end(),Boundary{edges[i],0});
            if (it==index.end() || it->first!=edges[i]) throw std::runtime_error("native adjacency does not meet a unitig boundary");
            for (;it!=index.end() && it->first==edges[i];++it)
                result.push_back({it->second,k(),length(it->second)-k()});
        }
        std::sort(result.begin(),result.end(),[&](auto a,auto b) {
            return reverse?a.node<b.node:order_key(v,a.node)<order_key(v,b.node);
        });
        return result;
    }
    // Audit/export helpers stream rows; no global GFA or adjacency is retained.
    std::vector<uint64_t> native_path(uint64_t v) const {
        auto pair=endpoints(v); std::vector<uint64_t> result;
        auto cur=pair.first;
        for (uint64_t i=0;i<unitig(v).length;++i) {
            result.push_back(cur); if (i+1<unitig(v).length) cur=graph_.NextSimplePathEdge(cur);
        }
        return result;
    }
    std::vector<uint64_t> native_neighbors(uint64_t e,bool reverse) const {
        if (e>=graph_.size() || !graph_.IsValidEdge(e)) throw std::out_of_range("invalid native edge");
        uint64_t buf[4]; sdbgt::MegaHitOracle o(graph_);
        auto n=reverse?o.incoming(e,buf):o.outgoing(e,buf); return {buf,buf+n};
    }
    template<class Emit> void gfa_records(Emit emit) const {
        emit(std::string("H\tVN:Z:1.0\n"));
        for (uint64_t v=0;v<num_vertices();v+=2)
            emit("S\t"+std::to_string(v/2)+"\t"+sequence(v)+"\tKC:i:"+std::to_string(coverage(v))+"\n");
        for (uint64_t v=0;v<num_vertices();++v) for (auto e:neighbors(v)) {
            if (std::make_pair(v,e.node)!=order_key(v,e.node)) continue;
            emit("L\t"+std::to_string(v/2)+"\t"+(v%2?"-":"+")+"\t"+std::to_string(e.node/2)+"\t"+
                 (e.node%2?"-":"+")+"\t"+std::to_string(k())+"M\n");
        }
    }
    std::string gfa_digest() const {
        sdbgt::Sha256 hash;
        gfa_records([&](const auto& record){hash.update(record.data(),record.size());});
        return hash.hex_digest();
    }
    void export_gfa(const std::string& path) const {
        int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
        if (fd<0) throw std::runtime_error("cannot create new GFA: "+path);
        FILE* f=fdopen(fd,"w"); if (!f) {::close(fd);::unlink(path.c_str());throw std::runtime_error("GFA stream open failed");}
        try {
            gfa_records([&](const auto& record) {
                if (std::fwrite(record.data(),1,record.size(),f)!=record.size()) throw std::runtime_error("GFA write failed");
            });
            auto closing=f; f=nullptr; if (std::fclose(closing)) throw std::runtime_error("GFA close failed");
        } catch (...) { if (f) std::fclose(f); ::unlink(path.c_str()); throw; }
    }
};
} // namespace sega
