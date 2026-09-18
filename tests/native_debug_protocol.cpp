#include "debug_protocol.hpp"
#include <cassert>
#include <cstdio>
#include <thread>

using namespace srw64::debug;

int main() {
    // Key names: chords, aliases, case, and loud failure on typos.
    assert(parse_keys("e+return")==(bit(E)|bit(Return)));
    assert(parse_keys("E+Enter")==(bit(E)|bit(Return)));
    assert(parse_keys("esc")==bit(Escape));
    assert(parse_keys("f7")==bit(F7));
    bool threw=false;
    try{parse_keys("e+retrun");}catch(const std::invalid_argument&){threw=true;}
    assert(threw);
    threw=false;
    try{parse_keys("");}catch(const std::invalid_argument&){threw=true;}
    assert(threw);
    assert(key_list(bit(Z)|bit(F8))==nlohmann::json({"z","f8"}));

    // Only fresh presses become edges; holding a key does not repeat it.
    VirtualKeyboard keys;
    keys.down(bit(E)|bit(Z));
    keys.down(bit(E)|bit(F6));
    assert(keys.held()==(bit(E)|bit(Z)|bit(F6)));
    auto presses=keys.take_presses();
    assert((presses==std::vector<Key>{Z,E,F6}));
    assert(keys.take_presses().empty());
    keys.up(bit(E));
    assert(!keys.holds(E) && keys.holds(Z));
    // The name page's release gate sees virtual keys by macOS key code.
    assert(keys.holds_mac_key(6) && !keys.holds_mac_key(14));
    keys.release_all();
    assert(keys.held()==0 && !keys.holds_mac_key(6));

    // Screenshots: the requester gets the matching completion, and a request
    // that nothing serves times out without leaving a stale one behind.
    Screenshots shots;
    std::thread render([&] {
        std::optional<Screenshots::Pending> pending;
        while(!(pending=shots.take()))std::this_thread::yield();
        assert(pending->path=="shot-1.png");
        shots.finish(pending->id,{{"path",pending->path.string()},{"width",960}});
    });
    auto result=shots.capture("shot-1.png",std::chrono::seconds(5));
    render.join();
    assert(result && (*result)["width"]==960);
    assert(!shots.capture("shot-2.png",std::chrono::milliseconds(20)));
    assert(!shots.take());

    // JSON-RPC framing.
    auto call=parse_call(R"({"jsonrpc":"2.0","id":7,"method":"keys","params":{"press":"i"}})");
    assert(call.id==7 && call.method=="keys" && call.params["press"]=="i");
    call=parse_call(R"({"jsonrpc":"2.0","id":"a","method":"status"})");
    assert(call.params.is_object() && call.params.empty());
    auto code=[](const char* line){try{parse_call(line);}catch(const RpcError& error){return error.code;}return 0;};
    assert(code("{")==ParseError);
    assert(code(R"({"id":1,"method":"status"})")==InvalidRequest);
    assert(code(R"({"jsonrpc":"2.0","id":1,"method":"status","params":[1]})")==InvalidParams);
    const auto ok=nlohmann::json::parse(reply(3,{{"vi",60}}));
    assert(ok["id"]==3 && ok["result"]["vi"]==60);
    const auto bad=nlohmann::json::parse(failure(4,MethodNotFound,"nope"));
    assert(bad["error"]["code"]==MethodNotFound);
    std::puts("debug protocol: keys, virtual keyboard, screenshots and JSON-RPC framing passed");
}
