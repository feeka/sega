// Portable, constant-space event recording for cross-implementation validation.
// Equality of SHA-256 digests is strong evidence of identical event streams, not
// a mathematical proof. Use binary event files plus byte comparison for exact
// equality. Hashing/I/O time belongs to validation, not traversal-only timing.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace sdbgt {

class Sha256 {
public:
    void update(const void* data, size_t n) {
        const auto* p = static_cast<const uint8_t*>(data);
        if (n > (std::numeric_limits<uint64_t>::max() / 8) - bytes_)
            throw std::overflow_error("SHA-256 message length exceeds 64-bit bit count");
        bytes_ += n;
        while (n) {
            size_t take = n < 64 - used_ ? n : 64 - used_;
            for (size_t i = 0; i < take; ++i) block_[used_ + i] = p[i];
            used_ += take; p += take; n -= take;
            if (used_ == 64) { transform(); used_ = 0; }
        }
    }
    std::string hex_digest() const {
        Sha256 copy = *this;
        const uint64_t bits = bytes_ * 8;
        copy.block_[copy.used_++] = 0x80;
        if (copy.used_ > 56) {
            while (copy.used_ < 64) copy.block_[copy.used_++] = 0;
            copy.transform(); copy.used_ = 0;
        }
        while (copy.used_ < 56) copy.block_[copy.used_++] = 0;
        for (int i = 7; i >= 0; --i) copy.block_[copy.used_++] = uint8_t(bits >> (8 * i));
        copy.transform();
        std::ostringstream os;
        os.imbue(std::locale::classic());
        os << std::hex << std::setfill('0');
        for (uint32_t word : copy.state_) os << std::setw(8) << word;
        return os.str();
    }
private:
    static uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
    void transform() {
        static constexpr uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];
        for (size_t i = 0; i < 16; ++i)
            w[i] = (uint32_t(block_[4*i]) << 24) | (uint32_t(block_[4*i+1]) << 16)
                 | (uint32_t(block_[4*i+2]) << 8) | block_[4*i+3];
        for (size_t i = 16; i < 64; ++i) {
            const uint32_t a = w[i-15], b = w[i-2];
            const uint32_t s0 = rotr(a,7) ^ rotr(a,18) ^ (a >> 3);
            const uint32_t s1 = rotr(b,17) ^ rotr(b,19) ^ (b >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for (size_t i = 0; i < 64; ++i) {
            uint32_t s1=rotr(e,6)^rotr(e,11)^rotr(e,25), ch=(e&f)^(~e&g);
            uint32_t t1=h+s1+ch+k[i]+w[i];
            uint32_t s0=rotr(a,2)^rotr(a,13)^rotr(a,22), maj=(a&b)^(a&c)^(b&c);
            uint32_t t2=s0+maj;
            h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;
        state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
    }
    std::array<uint32_t,8> state_{{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
    std::array<uint8_t,64> block_{};
    uint64_t bytes_ = 0;
    size_t used_ = 0;
};

class EventRecorder {
public:
    // Format: ASCII "SEGA-EVENTS-v1\n", then B/D + two uint64 little-endian
    // words or F + one uint64 little-endian word. UINT64_MAX denotes a DFS root
    // parent. No native struct layout, host endian, padding, or text locale.
    explicit EventRecorder(const std::string& path = "", bool hash_enabled = true,
                           uint64_t universe = std::numeric_limits<uint64_t>::max())
        : hash_enabled_(hash_enabled), writing_(!path.empty()), universe_(universe) {
        if (writing_) {
            out_.exceptions(std::ios::badbit | std::ios::failbit);
            out_.open(path, std::ios::binary | std::ios::trunc);
        }
        static constexpr char domain[] = "SEGA-EVENTS-v1\n";
        append(domain, sizeof(domain)-1);
    }
    EventRecorder(const EventRecorder&) = delete;
    EventRecorder& operator=(const EventRecorder&) = delete;
    void bfs(uint64_t v, uint64_t distance) {
        check_id(v); event('B',v,distance,true);
    }
    void discover(uint64_t v, uint64_t parent) {
        check_id(v);
        if (parent != std::numeric_limits<uint64_t>::max()) check_id(parent);
        if (parent == std::numeric_limits<uint64_t>::max() && depth_ != 0)
            throw std::runtime_error("DFS root discovered before prior tree finished");
        event('D',v,parent,true);
        ++depth_; if (depth_ > max_depth_) max_depth_ = depth_;
    }
    void finish(uint64_t v) {
        check_id(v);
        if (depth_ == 0) throw std::runtime_error("DFS finish without discovery");
        event('F',v,0,false); --depth_;
    }
    std::string digest_hex() const { return hash_enabled_ ? sha_.hex_digest() : ""; }
    uint64_t event_count() const { return events_; }
    uint64_t output_bytes() const { return writing_ ? encoded_bytes_ : 0; }
    uint64_t encoded_bytes() const { return encoded_bytes_; }
    uint64_t max_depth() const { return max_depth_; }
    uint64_t open_depth() const { return depth_; }
    void close() {
        if (closed_) return;
        if (depth_ != 0) throw std::runtime_error("DFS event stream has unfinished discoveries");
        if (writing_) { out_.flush(); out_.close(); }
        closed_ = true;
    }
private:
    void check_id(uint64_t v) const {
        if (v >= universe_) throw std::out_of_range("event vertex outside graph identifier universe");
    }
    static void le64(uint8_t* p, uint64_t v) {
        for (size_t i=0;i<8;++i) p[i]=uint8_t(v >> (8*i));
    }
    void event(uint8_t tag, uint64_t first, uint64_t second, bool pair) {
        uint8_t bytes[17]; bytes[0]=tag; le64(bytes+1,first);
        if (pair) le64(bytes+9,second);
        append(bytes,pair?17:9); ++events_;
    }
    void append(const void* p, size_t n) {
        if (closed_) throw std::logic_error("event stream is already closed");
        if (hash_enabled_) sha_.update(p,n);
        if (writing_) out_.write(static_cast<const char*>(p),static_cast<std::streamsize>(n));
        encoded_bytes_ += n;
    }
    Sha256 sha_;
    std::ofstream out_;
    bool hash_enabled_, writing_, closed_ = false;
    uint64_t universe_, events_=0, encoded_bytes_=0, depth_=0, max_depth_=0;
};
} // namespace sdbgt
