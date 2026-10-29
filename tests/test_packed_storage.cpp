#include "sega/elias_fano.hpp"
#include "graph_reference.hpp"

template<class Structure> void roundtrip(Structure& original,Structure copy){
    std::vector<std::vector<uint64_t>> chunks;
    original.write([&](const auto& words){chunks.emplace_back(words);});
    size_t p=0;copy.read([&](auto& words){check(words.size()==chunks.at(p).size(),"serialized layout size");words=chunks.at(p++);});
    for(uint64_t i=0;i<original.size();++i)check(original.get(i)==copy.get(i),"primitive serialization roundtrip");
}
void monotone(const std::vector<uint64_t>& values){
    EliasFano ef(values);
    check(ef.bytes()==EliasFano::encoded_bytes(values.size(),values.empty()?0:values.back()),"EF exact byte prediction including directory");
    for(uint64_t i=0;i<values.size();++i)check(ef.get(i)==values[i],"EF independently stored values");
    throws([&]{ef.get(values.size());},"EF index boundary");
    roundtrip(ef,EliasFano(values.size(),values.empty()?0:values.back()));
}
int main(){
    std::mt19937_64 rng(123456);
    for(unsigned width=0;width<=64;++width){
        PackedArray packed(257,width);std::vector<uint64_t> values(257);
        for(size_t i=0;i<values.size();++i){values[i]=rng()&low_mask(width);packed.set(i,values[i]);}
        for(size_t i=0;i<values.size();++i)check(packed.get(i)==values[i],"all widths, crossing word boundaries");
        // Overwriting must clear previous bits without corrupting adjacent values.
        for(size_t i=0;i<values.size();i+=3){packed.set(i,0);values[i]=0;}
        for(size_t i=0;i<values.size();++i)check(packed.get(i)==values[i],"packed overwrite preserves neighbors");
        roundtrip(packed,PackedArray(257,width));
        if(width<64)throws([&]{packed.set(0,uint64_t(1)<<width);},"packed value overflow");
    }
    throws([]{PackedArray p(3,65);},"invalid bit width");
    throws([]{PackedArray p(UINT64_MAX,64);},"size multiplication overflow");
    throws([]{PackedArray p(0,1);p.set(0,1);},"empty set");
    throws([]{PackedArray p(1,1);p.read([](auto& w){w[0]=2;});},"padding must be zero");
    monotone({});monotone({0});monotone({UINT64_MAX});monotone({0,UINT64_MAX});
    monotone({5,8,8,15,32});monotone({0,0,0,0,0});
    throws([]{EliasFano e(std::vector<uint64_t>{2,1,3});},"nonmonotone EF rejected");
    throws([]{EliasFano e(UINT64_MAX,UINT64_MAX);},"EF allocation overflow");
    // Repeated values, sparse high bits, long empty rank blocks, many list sizes.
    for(size_t n:{size_t(1),size_t(63),size_t(64),size_t(65),size_t(511),size_t(512),size_t(513),size_t(4097)}){
        std::vector<uint64_t> values(n,0);monotone(values);
        values.back()=uint64_t(1)<<50;monotone(values);
        for(auto& v:values)v=rng()&((uint64_t(1)<<48)-1);
        std::sort(values.begin(),values.end());monotone(values);
    }
    for(unsigned trial=0;trial<200;++trial){
        std::vector<uint64_t> values(rng()%2048);uint64_t last=0;
        for(auto& v:values){last+=rng()%17;v=last;}monotone(values);
    }
    {EliasFano ef(std::vector<uint64_t>{0,1,1,7});size_t chunk=0;
     throws([&]{ef.read([&](auto& words){if(chunk++==2)words[0]=99;});},"invalid select directory rejected");}
    std::cout<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<"}\n";return failures?1:0;
}
