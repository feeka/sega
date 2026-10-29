// Fixed-width unsigned integers. No external dependencies.
#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace sega {
inline unsigned bits_required(uint64_t maximum) {
    unsigned width=0; while(maximum){++width; maximum>>=1;} return width;
}
inline uint64_t low_mask(unsigned width) {
    return width==64 ? UINT64_MAX : (uint64_t(1)<<width)-1;
}
class PackedArray {
    uint64_t size_=0;
    unsigned width_=0;
    std::vector<uint64_t> words_;
public:
    static uint64_t word_count(uint64_t count,unsigned width) {
        if(width>64)throw std::invalid_argument("packed width exceeds 64");
        if(width && count>UINT64_MAX/width)throw std::length_error("packed bit count overflow");
        const uint64_t bits=count*width,words=bits/64+(bits%64!=0);
        if(words>std::numeric_limits<size_t>::max()/8)throw std::length_error("packed allocation overflow");
        return words;
    }
    PackedArray()=default;
    PackedArray(uint64_t count,unsigned width):size_(count),width_(width),words_(word_count(count,width),0){}
    uint64_t size() const{return size_;}
    unsigned width() const{return width_;}
    uint64_t get(uint64_t i) const {
        if(i>=size_)throw std::out_of_range("packed index");
        if(!width_)return 0;
        const uint64_t bit=i*width_,word=bit/64;const unsigned shift=bit%64;
        uint64_t value=words_[word]>>shift;
        if(shift && width_>64-shift)value|=words_[word+1]<<(64-shift);
        return value&low_mask(width_);
    }
    void set(uint64_t i,uint64_t value) {
        if(i>=size_)throw std::out_of_range("packed index");
        if(value>low_mask(width_))throw std::overflow_error("value does not fit packed width");
        if(!width_)return;
        const uint64_t bit=i*width_,word=bit/64;const unsigned shift=bit%64;
        words_[word]=(words_[word]&~(low_mask(width_)<<shift))|(value<<shift);
        if(shift && width_>64-shift){
            const unsigned remaining=width_-(64-shift);
            words_[word+1]=(words_[word+1]&~low_mask(remaining))|(value>>(64-shift));
        }
    }
    uint64_t bytes() const{return words_.size()*8;}
    uint64_t capacity_bytes() const{return words_.capacity()*8;}
    template<class Write> void write(Write&& write) const{write(words_);}
    template<class Read> void read(Read&& read){
        read(words_);
        const auto used=(size_*width_)%64;
        if(used && (words_.back()&~low_mask(unsigned(used))))throw std::runtime_error("nonzero packed padding");
    }
};
} // namespace sega
