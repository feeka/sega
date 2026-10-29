// Static simple directed graphs: packed targets and either packed or
// Elias-Fano row offsets. Input IDs are preserved; no hidden reverse graph.
#pragma once
#include "compact_graph.hpp"
#include "elias_fano.hpp"

namespace sega {
template<bool EfOffsets> class PackedGraph {
    using Offsets=std::conditional_t<EfOffsets,EliasFano,PackedArray>;
    uint64_t n_=0,m_=0;
    PackedArray targets_;
    Offsets offsets_;
    PackedGraph()=default;
    static unsigned target_width(uint64_t n){return n?bits_required(n-1):0;}
    static uint64_t offset_bytes(uint64_t n,uint64_t m){
        if constexpr(EfOffsets)return EliasFano::encoded_bytes(n+1,m);
        else return PackedArray::word_count(n+1,bits_required(m))*8;
    }
    void validate() const{
        if(offsets_.get(0)!=0 || offsets_.get(n_)!=m_)throw std::runtime_error("invalid packed graph boundaries");
        for(Vertex u=0;u<n_;++u){
            const auto begin=offsets_.get(u),end=offsets_.get(u+1);
            if(begin>end || end>m_)throw std::runtime_error("invalid packed graph offset");
            Vertex previous=no_vertex;
            for(auto p=begin;p<end;++p){
                const auto v=targets_.get(p);
                if(v>=n_ || (previous!=no_vertex && v<=previous))throw std::runtime_error("invalid packed graph target");
                previous=v;
            }
        }
    }
public:
    struct Cursor{uint64_t position,end;};
    static constexpr uint64_t codec=EfOffsets?4:3;
    PackedGraph(uint64_t n,std::vector<Edge> edges):n_(n){
        if(n>uint64_t(UINT32_MAX)+1)throw std::overflow_error("packed graph requires 32-bit vertex IDs");
        edges=normalize_edges(n,std::move(edges));m_=edges.size();
        targets_=PackedArray(m_,target_width(n));
        std::vector<uint64_t> rows(n+1);uint64_t p=0;
        for(Vertex u=0;u<n;++u){
            rows[u]=p;
            while(p<m_ && edges[p].first==u){targets_.set(p,edges[p].second);++p;}
        }
        rows[n]=m_;
        if constexpr(EfOffsets)offsets_=EliasFano(rows);
        else{offsets_=PackedArray(n+1,bits_required(m_));for(uint64_t i=0;i<=n;++i)offsets_.set(i,rows[i]);}
    }
    bool valid(Vertex v) const{return v<n_;}
    uint64_t num_vertices() const{return n_;}
    uint64_t num_edges() const{return m_;}
    Cursor neighbors(Vertex v) const{
        if(!valid(v))throw std::out_of_range("invalid vertex");
        return {offsets_.get(v),offsets_.get(v+1)};
    }
    bool next(Cursor& c,Vertex& v) const{
        if(c.position==c.end)return false;
        v=targets_.get(c.position++);return true;
    }
    Storage storage() const{return {targets_.bytes(),offsets_.bytes(),0,120};}
    uint64_t resident_payload_bytes() const{return targets_.capacity_bytes()+offsets_.capacity_bytes();}
    void save(const std::string& path) const{
        graph_io::Writer out(path);std::array<uint8_t,56> header{};
        std::copy_n("SEGAPG01",8,header.data());
        const uint64_t fields[]={1,codec,n_,m_,targets_.bytes(),offsets_.bytes()};
        for(unsigned i=0;i<6;++i)graph_io::put(header.data()+8+i*8,fields[i]);
        out.write(header.data(),header.size());
        auto write=[&](const auto& words){graph_io::write_vector(out,words);};
        targets_.write(write);offsets_.write(write);out.finish();
    }
    static PackedGraph load(const std::string& path){
        const auto length=std::filesystem::file_size(path);
        if(length<120)throw std::runtime_error("packed graph file too short");
        graph_io::Reader in(path);std::array<uint8_t,56> header{};in.read(header.data(),header.size());
        if(!std::equal(header.begin(),header.begin()+8,"SEGAPG01") || graph_io::get(header.data()+8)!=1 || graph_io::get(header.data()+16)!=codec)
            throw std::runtime_error("unsupported packed graph format/version/backend");
        PackedGraph g;g.n_=graph_io::get(header.data()+24);g.m_=graph_io::get(header.data()+32);
        // Check dimensions and exact length BEFORE allocating anything from the file.
        if(g.n_>uint64_t(UINT32_MAX)+1 || (!g.n_ && g.m_) ||
           (g.n_ && g.m_/g.n_>g.n_) || (g.n_<uint64_t(UINT32_MAX)+1 && g.m_>g.n_*g.n_))
            throw std::runtime_error("invalid packed graph dimensions");
        const auto targets=PackedArray::word_count(g.m_,target_width(g.n_))*8;
        const auto offsets=offset_bytes(g.n_,g.m_);
        if(targets>length-120 || offsets!=length-120-targets ||
           graph_io::get(header.data()+40)!=targets || graph_io::get(header.data()+48)!=offsets)
            throw std::runtime_error("invalid packed graph length");
        g.targets_=PackedArray(g.m_,target_width(g.n_));
        if constexpr(EfOffsets)g.offsets_=EliasFano(g.n_+1,g.m_);
        else g.offsets_=PackedArray(g.n_+1,bits_required(g.m_));
        auto read=[&](auto& words){graph_io::read_vector(in,words);};
        g.targets_.read(read);g.offsets_.read(read);in.finish();g.validate();return g;
    }
};
using PackedCsr32=PackedGraph<false>;
using EfCsr32=PackedGraph<true>;
} // namespace sega
