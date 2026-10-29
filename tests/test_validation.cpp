#include "sdbg_traverse/validation.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <vector>

using namespace sdbgt;
static unsigned failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
}
template<class F> static void throws(F&& fn,const char* what) {
    try { fn(); check(false,what); } catch (const std::exception&) {}
}
static std::string hash(const std::string& s) { Sha256 h; h.update(s.data(),s.size()); return h.hex_digest(); }
int main() {
    check(hash("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA256 empty");
    check(hash("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA256 abc");
    check(hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA256 two blocks");
    std::string million(1000000,'a');
    check(hash(million)=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA256 million a");
    Sha256 chunks;
    for (size_t start=0;start<million.size();start+=37) chunks.update(million.data()+start,std::min(size_t(37),million.size()-start));
    check(chunks.hex_digest()==hash(million),"chunked SHA256");
    check(chunks.hex_digest()==chunks.hex_digest(),"digest nondestructive");
    const std::string prefix="SEGA-EVENTS-v1\n";
    auto file=std::filesystem::temp_directory_path()/
        ("sega-events-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count())+".bin");
    {
        EventRecorder r(file.string(),true,257); r.bfs(0,0);r.bfs(256,1);r.close();
        check(r.event_count()==2,"event count");
        check(r.output_bytes()==prefix.size()+34,"file byte count");
        std::ifstream f(file,std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
        std::string expected=prefix;
        expected.push_back('B');expected.append(16,'\0');
        expected.push_back('B');expected.push_back('\0');expected.push_back('\1');expected.append(6,'\0');
        expected.push_back('\1');expected.append(7,'\0');
        check(data==expected,"portable event bytes");
        check(r.digest_hex()==hash(data),"file checksum equals streamed checksum");
        check(r.digest_hex()=="36ae17bb9571aac9efb32321421a9f3f2c479336a8d26133195877c99e016bb9", "canonical BFS SHA256 independent Python vector");
        throws([&]{r.bfs(0,0);},"write after close fails");
    }
    std::filesystem::remove(file);
    {
        EventRecorder r("",true,3);r.discover(0,UINT64_MAX);r.discover(1,0);r.finish(1);r.finish(0);r.close();
        check(r.event_count()==4 && r.max_depth()==2 && r.open_depth()==0,"DFS counters");
        check(r.output_bytes()==0,"no file bytes in digest-only mode");
        check(r.digest_hex()=="818f087d325d065c4eb240d04a6c3183e7e0651d18400044cf5678e90863c240", "canonical DFS SHA256 independent Python vector");
    }
    {
        EventRecorder r("",false,2);r.bfs(0,0);r.close();
        check(r.digest_hex().empty(),"hash-disabled mode");
    }
    {
        EventRecorder a,b; a.bfs(0,0);a.bfs(1,1);b.bfs(0,0);b.bfs(1,2);a.close();b.close();
        check(a.event_count()==b.event_count() && a.digest_hex()!=b.digest_hex(),"equal counts with different distances produce different digests");
    }
    throws([]{EventRecorder r("",true,1);r.bfs(1,0);},"out-of-universe event fails");
    throws([]{EventRecorder r;r.finish(0);},"unbalanced finish fails");
    throws([]{EventRecorder r;r.discover(0,UINT64_MAX);r.close();},"unfinished DFS fails");
    throws([]{EventRecorder r;r.discover(0,UINT64_MAX);r.discover(1,UINT64_MAX);},"overlapping DFS roots fail");
    throws([]{EventRecorder r("/nonexistent-sega-event-directory/log.bin");},"file open failure propagates");
#ifdef __linux__
    throws([]{EventRecorder r("/dev/full");r.bfs(0,0);r.close();},"file write failure propagates");
#endif
    std::cout << (failures?"VALIDATION TESTS FAILED":"VALIDATION TESTS PASSED") << '\n';
    return failures?1:0;
}
