// Monotone sequence, including duplicates. Select uses binary search over
// 512-bit rank blocks and at most eight word probes: O(log(number of blocks)),
// NOT a claim of the theoretical constant-time succinct select bound.
#pragma once
#include "packed_array.hpp"
#include <algorithm>

namespace sega {
inline unsigned popcount64(uint64_t x){
    x=x-((x>>1)&0x5555555555555555ULL);
    x=(x&0x3333333333333333ULL)+((x>>2)&0x3333333333333333ULL);
    x=(x+(x>>4))&0x0f0f0f0f0f0f0f0fULL;
    return unsigned((x*0x0101010101010101ULL)>>56);
}
class EliasFano {
    uint64_t size_=0,maximum_=0,high_bits_=0;
    PackedArray low_;
    std::vector<uint64_t> high_,ranks_;
    static uint64_t high_length(uint64_t count,uint64_t maximum,unsigned width){
        const auto zeros=maximum>>width;
        if(zeros==UINT64_MAX || count>UINT64_MAX-zeros-1)throw std::length_error("Elias-Fano size overflow");
        return count+zeros+1;
    }
    static unsigned low_width(uint64_t count,uint64_t maximum){
        const auto ratio=count?maximum/count:0;
        return ratio?bits_required(ratio)-1:0;
    }
    uint64_t select(uint64_t i) const {
        const auto it=std::upper_bound(ranks_.begin(),ranks_.end(),i);
        const uint64_t block=uint64_t(it-ranks_.begin()-1);
        uint64_t remaining=i-ranks_[block];
        for(uint64_t w=block*8;w<std::min<uint64_t>(high_.size(),(block+1)*8);++w){
            uint64_t bits=high_[w];const auto count=popcount64(bits);
            if(remaining>=count){remaining-=count;continue;}
            unsigned position=0;
            for(unsigned half=32;half;half/=2){
                const auto left=popcount64(bits&low_mask(half));
                if(remaining>=left){remaining-=left;bits>>=half;position+=half;}
                else bits&=low_mask(half);
            }
            return w*64+position;
        }
        throw std::runtime_error("invalid Elias-Fano select index");
    }
    void build_ranks(){
        uint64_t sum=0;
        for(size_t w=0;w<high_.size();++w){if(w%8==0)ranks_[w/8]=sum;sum+=popcount64(high_[w]);}
        ranks_.back()=sum;
    }
public:
    EliasFano()=default;
    // Allocate a layout for loading; read() validates before it can be queried.
    EliasFano(uint64_t count,uint64_t maximum):size_(count),maximum_(maximum),
        high_bits_(high_length(count,maximum,low_width(count,maximum))),
        low_(count,low_width(count,maximum)),high_(PackedArray::word_count(high_bits_,1),0),
        ranks_((high_.size()+7)/8+1,0){}
    explicit EliasFano(const std::vector<uint64_t>& values):EliasFano(values.size(),values.empty()?0:values.back()){
        uint64_t previous=0;
        for(uint64_t i=0;i<size_;++i){
            const auto value=values[i];
            if(value<previous || value>maximum_)throw std::invalid_argument("Elias-Fano input is not monotone");
            low_.set(i,value&low_mask(low_.width()));
            const auto bit=(value>>low_.width())+i;high_[bit/64]|=uint64_t(1)<<(bit%64);previous=value;
        }
        build_ranks();
    }
    static uint64_t encoded_bytes(uint64_t count,uint64_t maximum){
        const auto width=low_width(count,maximum);
        const auto low_words=PackedArray::word_count(count,width);
        const auto high_words=PackedArray::word_count(high_length(count,maximum,width),1);
        const auto rank_words=(high_words+7)/8+1;
        if(low_words>UINT64_MAX/8-high_words-rank_words)throw std::length_error("Elias-Fano byte count overflow");
        return (low_words+high_words+rank_words)*8;
    }
    uint64_t size() const{return size_;}
    uint64_t get(uint64_t i) const{
        if(i>=size_)throw std::out_of_range("Elias-Fano index");
        const auto high=select(i)-i;
        if(high>(UINT64_MAX>>low_.width()))throw std::runtime_error("Elias-Fano value overflow");
        return (high<<low_.width())|low_.get(i);
    }
    uint64_t bytes() const{return low_.bytes()+8*(high_.size()+ranks_.size());}
    uint64_t capacity_bytes() const{return low_.capacity_bytes()+8*(high_.capacity()+ranks_.capacity());}
    template<class Write> void write(Write&& write) const{low_.write(write);write(high_);write(ranks_);}
    template<class Read> void read(Read&& read){
        low_.read(read);read(high_);read(ranks_);
        uint64_t sum=0;
        for(size_t w=0;w<high_.size();++w){
            if(w%8==0 && ranks_[w/8]!=sum)throw std::runtime_error("invalid Elias-Fano rank directory");
            sum+=popcount64(high_[w]);
        }
        if(sum!=size_ || ranks_.back()!=sum)throw std::runtime_error("invalid Elias-Fano one count");
        if(high_bits_%64 && (high_.back()&~low_mask(unsigned(high_bits_%64))))throw std::runtime_error("nonzero Elias-Fano padding");
        uint64_t previous=0;
        for(uint64_t i=0;i<size_;++i){
            const auto value=get(i);
            if(value<previous || value>maximum_)throw std::runtime_error("invalid Elias-Fano values");
            previous=value;
        }
    }
};
} // namespace sega
