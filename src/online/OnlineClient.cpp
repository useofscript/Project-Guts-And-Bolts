#include "OnlineClient.h"
#include "Protocol.h"
#include "../core/Account.h"
#include "../game/Profile.h"
#include "../net/Socket.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>

using json = nlohmann::json;

namespace Online {

namespace {

struct Done { Reply callback; json reply; };

struct State {
    std::mutex       lock;
    std::deque<Done> done;              // finished requests, waiting for update()
    std::atomic<int> running{0};
    Status           status = Status::Off;
    std::string      statusText = "Offline";
    json             me = json::object();
    json             server = json::object();
    double           retryAt = 0;       // seconds (steady clock) to try connecting again
    bool             helloOut = false;
};

State& S() {
    // Never destroyed: a request still running in the background when the app
    // quits can finish safely.
    static State* s = new State;
    return *s;
}

double clockSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool splitAddress(const std::string& address, std::string& host, int& port) {
    host = address;
    port = kDefaultPort;
    // "[::1]:7780" isn't supported; "host:port" and plain "host" are.
    auto colon = address.rfind(':');
    if (colon != std::string::npos && address.find(':') == colon) {
        host = address.substr(0, colon);
        port = std::atoi(address.c_str() + colon + 1);
    }
    while (!host.empty() && host.back() == ' ') host.pop_back();
    while (!host.empty() && host.front() == ' ') host.erase(host.begin());
    return !host.empty() && port > 0 && port < 65536;
}

void finish(Reply cb, json reply) {
    std::lock_guard<std::mutex> g(S().lock);
    S().done.push_back({std::move(cb), std::move(reply)});
}

} // namespace

// ---------------------------------------------------------------------------

std::string serverAddress() {
    if (const char* e = std::getenv("GB_SERVER"); e && *e) return e;
    return Profile::get().server;
}

void setServerAddress(const std::string& address) {
    Profile& p = Profile::get();
    p.server = cleanText(address, 120);
    p.save();
    S().me = json::object();
    S().status = Status::Off;
    S().statusText = "Offline";
    if (configured()) connect();
}

bool configured() { return !serverAddress().empty(); }
Status status() { return S().status; }
const std::string& statusText() { return S().statusText; }
bool online() { return S().status == Status::Online; }
const json& me() { return S().me; }
const json& serverInfo() { return S().server; }
bool verified() { return online() && S().me.value("verified", false); }
bool staff() { return online() && S().me.value("staff", false); }
long long bolts() { return S().me.value("bolts", 0LL); }
int pending() { return S().running.load(); }

bool owns(const std::string& id) {
    const json& o = S().me.contains("owned") ? S().me["owned"] : json::array();
    for (const auto& x : o) if (x.is_string() && x.get<std::string>() == id) return true;
    return false;
}

void takeMe(const json& reply) {
    if (!reply.contains("me") || !reply["me"].is_object()) return;
    S().me = reply["me"];
    // Badges the server knows about go into the profile too, so they also show
    // (and can be checked) in multiplayer games.
    if (S().me.contains("grants")) {
        Profile& p = Profile::get();
        bool changed = false;
        for (const auto& g : S().me["grants"]) {
            if (!g.is_array() || g.size() != 2 || !g[0].is_string() || !g[1].is_string()) continue;
            std::string k = g[0], sig = g[1];
            bool have = false;
            for (auto& [pk, ps] : p.grants) if (pk == k) { have = true; if (ps != sig) { ps = sig; changed = true; } }
            if (!have) { p.grants.push_back({k, sig}); changed = true; }
        }
        if (changed) p.save();
    }
}

void request(const std::string& op, const json& args, Reply done, int timeoutSeconds) {
    std::string address = serverAddress();
    if (address.empty()) {
        finish(std::move(done), json{{"ok", false}, {"error", "Not connected to a Guts&Bolts server."}});
        return;
    }
    // Sign here, on the main thread (the key lives here); the network part runs in the background.
    long long t = unixNow();
    std::string nonce = Account::randomHex(8);
    json req = {{"op", op}, {"account", Account::id()}, {"time", t}, {"nonce", nonce}, {"args", args},
                {"sig", Account::sign(requestText(op, Account::id(), t, nonce, args))}};
    std::string text = req.dump();
    S().running++;
    std::thread([address, text, done, timeoutSeconds]() {
        json reply;
        std::string host, err;
        int port = 0;
        if (!splitAddress(address, host, port)) {
            reply = {{"ok", false}, {"error", "That server address doesn't look right (use host or host:port)."}};
        } else if (auto conn = Net::Connection::connectTo(host, port, err, 5000)) {
            conn->send(text);
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
            std::string msg;
            bool got = false;
            while (std::chrono::steady_clock::now() < until) {
                bool alive = conn->poll();
                if (conn->pop(msg)) { got = true; break; }
                if (!alive) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (got) {
                reply = json::parse(msg, nullptr, false);
                if (!reply.is_object()) reply = {{"ok", false}, {"error", "The server sent back something odd."}};
            } else {
                reply = {{"ok", false}, {"error", "The server didn't answer in time."}};
            }
        } else {
            reply = {{"ok", false}, {"error", "Couldn't reach the server at " + address + " (" + err + ")."}};
        }
        if (!reply.contains("ok")) reply["ok"] = false;
        finish(done, std::move(reply));
        S().running--;
    }).detach();
}

void connect() {
    if (!configured() || S().helloOut) return;
    S().helloOut = true;
    if (S().status != Status::Online) { S().status = Status::Connecting; S().statusText = "Connecting..."; }
    Profile& p = Profile::get();
    json grants = json::array();
    for (const auto& [k, sig] : p.grants) grants.push_back({k, sig});
    request("hello", {{"name", p.name}, {"grants", grants}, {"protocol", kProtocol}}, [](const json& r) {
        S().helloOut = false;
        if (r.value("ok", false)) {
            takeMe(r);
            if (r.contains("server")) S().server = r["server"];
            S().status = Status::Online;
            S().statusText = "Online: " + S().server.value("name", std::string("Guts&Bolts"));
        } else {
            S().status = Status::Failed;
            S().statusText = r.value("error", std::string("Couldn't connect."));
            S().retryAt = clockSeconds() + 30.0;
        }
    }, 10);
}

void update() {
    std::deque<Done> ready;
    {
        std::lock_guard<std::mutex> g(S().lock);
        ready.swap(S().done);
    }
    for (Done& d : ready) {
        // A reply saying our account changed (Bolts, badges...) keeps me() fresh for everyone.
        if (d.reply.value("ok", false)) takeMe(d.reply);
        if (d.callback) d.callback(d.reply);
    }
    if (configured() && S().status == Status::Off) connect();
    if (S().status == Status::Failed && clockSeconds() > S().retryAt) connect();
}

void finishAll(int timeoutMs) {
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    auto waiting = [] { std::lock_guard<std::mutex> g(S().lock); return !S().done.empty(); };
    while ((S().running.load() > 0 || waiting()) && std::chrono::steady_clock::now() < until) {
        update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    update();
}

} // namespace Online
