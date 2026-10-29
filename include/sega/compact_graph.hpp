// Outgoing-only CSR32 and blocked unsigned-LEB128 gap lists. No SDSL dependency.
#pragma once
#include "graph_types.hpp"
#include "sdbg_traverse/validation.hpp"
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <type_traits>
#include <fcntl.h>
#include <unistd.h>

namespace sega {
namespace graph_io {
inline void put(uint8_t* b, uint64_t v, unsigned width=8) {
    for(unsigned i=0;i<width;++i) b[i]=uint8_t(v>>(8*i));
}
inline uint64_t get(const uint8_t* b, unsigned width=8) {
    uint64_t v=0; for(unsigned i=0;i<width;++i) v|=uint64_t(b[i])<<(8*i); return v;
}
// Exclusive creation prevents accidental replacement, including symlinks.
// Failed writes remove their partial file; successful save closes before return.
struct Writer {
    std::string path; FILE* file=nullptr; sdbgt::Sha256 hash; bool committed=false;
    explicit Writer(const std::string& p):path(p) {
        int fd=::open(p.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0) throw std::runtime_error("cannot create new graph file: "+p);
        file=::fdopen(fd,"wb");
        if(!file){::close(fd);::unlink(p.c_str());throw std::runtime_error("cannot open graph stream");}
    }
    ~Writer(){if(file)std::fclose(file);if(!committed)::unlink(path.c_str());}
    void write(const void* p,size_t n){
        if(std::fwrite(p,1,n,file)!=n)throw std::runtime_error("graph write failed");
        hash.update(p,n);
    }
    void finish(){
        const auto digest=hash.hex_digest();
        if(std::fwrite(digest.data(),1,digest.size(),file)!=digest.size())throw std::runtime_error("graph checksum write failed");
        FILE* closing=file;file=nullptr;
        if(std::fclose(closing))throw std::runtime_error("graph close failed");
        committed=true;
    }
};
struct Reader {
    std::ifstream file; sdbgt::Sha256 hash;
    explicit Reader(const std::string& p):file(p,std::ios::binary){if(!file)throw std::runtime_error("cannot open graph file: "+p);}
    void read(void* p,size_t n){
        if(!file.read(static_cast<char*>(p),std::streamsize(n)))throw std::runtime_error("truncated graph file");
        hash.update(p,n);
    }
    void finish(){
        std::array<char,64> digest{};
        if(!file.read(digest.data(),digest.size()) || std::string(digest.data(),digest.size())!=hash.hex_digest())
            throw std::runtime_error("graph SHA-256 mismatch");
        if(file.peek()!=std::char_traits<char>::eof())throw std::runtime_error("trailing graph bytes");
    }
};
template<class T> void write_vector(Writer& out,const std::vector<T>& values){
    std::array<uint8_t,65536> buffer{};
    for(size_t p=0;p<values.size();){
        const size_t count=std::min(values.size()-p,buffer.size()/sizeof(T));
        for(size_t j=0;j<count;++j)put(buffer.data()+j*sizeof(T),values[p+j],sizeof(T));
        out.write(buffer.data(),count*sizeof(T));p+=count;
    }
}
template<class T> void read_vector(Reader& in,std::vector<T>& values){
    std::array<uint8_t,65536> buffer{};
    for(size_t p=0;p<values.size();){
        const size_t count=std::min(values.size()-p,buffer.size()/sizeof(T));
        in.read(buffer.data(),count*sizeof(T));
        for(size_t j=0;j<count;++j)values[p+j]=T(get(buffer.data()+j*sizeof(T),sizeof(T)));
        p+=count;
    }
}
} // namespace graph_io

// Dense 32-bit vertex IDs, 64-bit row positions, static simple directed graphs.
// Byte-coded rows restart with an absolute ID every 128 neighbors. No block
// directory is needed for sequential traversal; this is not random-access seek.
template<bool ByteCoded> class CompactGraph {
    using Item=std::conditional_t<ByteCoded,uint8_t,uint32_t>;
    uint64_t n_=0,m_=0;
    std::vector<uint64_t> offsets_;
    std::vector<Item> data_;
    struct RawCursor { uint64_t position,end; };
    struct ByteCursor { uint64_t position,end,state=0; }; // previous ID + block ordinal
    CompactGraph()=default;
    static unsigned encoded_length(uint64_t x){unsigned k=1;while(x>=128){x>>=7;++k;}return k;}
    void append(uint64_t x){
        if constexpr(ByteCoded){
            while(x>=128){data_.push_back(uint8_t(x)|128);x>>=7;}
            data_.push_back(uint8_t(x));
        }else data_.push_back(uint32_t(x));
    }
    void validate() const {
        if(offsets_.empty() || offsets_[0]!=0 || offsets_.back()!=data_.size())throw std::runtime_error("invalid graph row boundaries");
        uint64_t count=0;
        for(Vertex u=0;u<n_;++u){
            if(offsets_[u]>offsets_[u+1] || offsets_[u+1]>data_.size())throw std::runtime_error("invalid graph row offset");
            auto c=neighbors(u);Vertex v,previous=no_vertex;
            while(next(c,v)){
                if(v>=n_ || (previous!=no_vertex && v<=previous))throw std::runtime_error("invalid or unsorted graph target");
                previous=v;++count;
            }
        }
        if(count!=m_)throw std::runtime_error("graph edge count mismatch");
    }
public:
    using Cursor=std::conditional_t<ByteCoded,ByteCursor,RawCursor>;
    static constexpr uint64_t codec=ByteCoded?2:1;
    CompactGraph(uint64_t n,std::vector<Edge> edges):n_(n){
        if(n>uint64_t(UINT32_MAX)+1)throw std::overflow_error("compact graph requires 32-bit vertex IDs");
        edges=normalize_edges(n,std::move(edges));m_=edges.size();offsets_.resize(n+1);
        // Two passes allocate exact payload capacity instead of retaining growth slack.
        uint64_t bytes=0,previous=0,ordinal=0,last_source=no_vertex;
        for(auto e:edges){
            if(e.first!=last_source){ordinal=0;previous=0;last_source=e.first;}
            bytes+=ByteCoded?encoded_length(ordinal%128?e.second-previous:e.second):1;
            previous=e.second;++ordinal;
        }
        data_.reserve(bytes);size_t e=0;
        for(Vertex u=0;u<n;++u){
            offsets_[u]=data_.size();previous=0;ordinal=0;
            while(e<edges.size() && edges[e].first==u){
                const Vertex v=edges[e++].second;
                if constexpr(ByteCoded)append(ordinal%128?v-previous:v);else append(v);
                previous=v;++ordinal;
            }
        }
        offsets_[n]=data_.size();
    }
    bool valid(Vertex v) const{return v<n_;}
    uint64_t num_vertices() const{return n_;}
    uint64_t num_edges() const{return m_;}
    Cursor neighbors(Vertex v) const{
        if(!valid(v))throw std::out_of_range("invalid vertex");
        return Cursor{offsets_[v],offsets_[v+1]};
    }
    bool next(Cursor& c,Vertex& v) const{
        if(c.position==c.end)return false;
        if constexpr(!ByteCoded){v=data_[c.position++];}
        else{
            uint64_t x=0;unsigned shift=0;uint8_t b;
            do{
                if(c.position>=c.end || shift>28)throw std::runtime_error("invalid graph varint");
                b=data_[c.position++];x|=uint64_t(b&127)<<shift;shift+=7;
            }while(b&128);
            if(x>UINT32_MAX || (shift>7 && b==0))throw std::runtime_error("overflow or noncanonical graph varint");
            const auto ordinal=c.state>>32;
            v=ordinal?uint32_t(c.state)+x:x;
            if(v>UINT32_MAX)throw std::runtime_error("graph target overflow");
            c.state=(((ordinal+1)%128)<<32)|v;
        }
        return true;
    }
    Storage storage() const{return {data_.size()*sizeof(Item),offsets_.size()*8,0,120};}
    uint64_t resident_payload_bytes() const{return data_.capacity()*sizeof(Item)+offsets_.capacity()*8;}
    void save(const std::string& path) const{
        graph_io::Writer out(path);std::array<uint8_t,56> header{};
        std::copy_n("SEGACG01",8,header.data());
        const uint64_t fields[]={1,codec,n_,m_,data_.size()*sizeof(Item),ByteCoded?128u:0u};
        for(unsigned i=0;i<6;++i)graph_io::put(header.data()+8+i*8,fields[i]);
        out.write(header.data(),header.size());graph_io::write_vector(out,offsets_);graph_io::write_vector(out,data_);out.finish();
    }
    static CompactGraph load(const std::string& path){
        const auto length=std::filesystem::file_size(path);
        if(length<128)throw std::runtime_error("graph file too short");
        graph_io::Reader in(path);std::array<uint8_t,56> h{};in.read(h.data(),h.size());
        if(!std::equal(h.begin(),h.begin()+8,"SEGACG01") || graph_io::get(h.data()+8)!=1 || graph_io::get(h.data()+16)!=codec)
            throw std::runtime_error("unsupported graph format/version/backend");
        CompactGraph g;g.n_=graph_io::get(h.data()+24);g.m_=graph_io::get(h.data()+32);
        const auto bytes=graph_io::get(h.data()+40),block=graph_io::get(h.data()+48);
        if(g.n_>uint64_t(UINT32_MAX)+1 || block!=(ByteCoded?128:0) || g.n_>(length-128)/8)
            throw std::runtime_error("invalid graph dimensions");
        const auto offset_bytes=(g.n_+1)*8;
        if(bytes!=length-120-offset_bytes || bytes%sizeof(Item) ||
           (!ByteCoded && g.m_!=bytes/4) || (ByteCoded && (g.m_>bytes || g.m_<bytes/5+(bytes%5!=0))))
            throw std::runtime_error("invalid graph payload length");
        g.offsets_.resize(g.n_+1);g.data_.resize(bytes/sizeof(Item));
        graph_io::read_vector(in,g.offsets_);graph_io::read_vector(in,g.data_);in.finish();g.validate();return g;
    }
};
using OutCsr32=CompactGraph<false>;
using ByteGraph32=CompactGraph<true>;
} // namespace sega
