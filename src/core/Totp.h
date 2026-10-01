#pragma once
// Authenticator apps (TOTP, RFC 6238): a 6-digit code that changes every 30 seconds,
// made from a secret shared once with the app. worker/server.js does exactly the same.
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Totp {

inline std::array<uint8_t, 20> sha1(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> m(in);
    const uint64_t bits = (uint64_t)in.size() * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back((uint8_t)(bits >> (i * 8)));
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    for (size_t b = 0; b < m.size(); b += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t)m[b + i * 4] << 24 | (uint32_t)m[b + i * 4 + 1] << 16 | (uint32_t)m[b + i * 4 + 2] << 8 | m[b + i * 4 + 3];
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (bb & c) | (~bb & d);          k = 0x5A827999u; }
            else if (i < 40) { f = bb ^ c ^ d;                    k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = bb ^ c ^ d;                    k = 0xCA62C1D6u; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(bb, 30); bb = a; a = t;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
    }
    std::array<uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j) out[(size_t)(i * 4 + j)] = (uint8_t)(h[i] >> (24 - j * 8));
    return out;
}

inline std::array<uint8_t, 20> hmacSha1(std::vector<uint8_t> key, const std::vector<uint8_t>& msg) {
    if (key.size() > 64) { auto k = sha1(key); key.assign(k.begin(), k.end()); }
    key.resize(64, 0);
    std::vector<uint8_t> inner(64), outer(64);
    for (int i = 0; i < 64; ++i) { inner[(size_t)i] = key[(size_t)i] ^ 0x36; outer[(size_t)i] = key[(size_t)i] ^ 0x5c; }
    inner.insert(inner.end(), msg.begin(), msg.end());
    auto ih = sha1(inner);
    outer.insert(outer.end(), ih.begin(), ih.end());
    return sha1(outer);
}

inline const char* kB32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

inline std::string base32(const std::vector<uint8_t>& bytes) {
    std::string out;
    int bits = 0;
    uint32_t val = 0;
    for (uint8_t b : bytes) {
        val = (val << 8) | b; bits += 8;
        while (bits >= 5) { out += kB32[(val >> (bits - 5)) & 31]; bits -= 5; }
    }
    if (bits > 0) out += kB32[(val << (5 - bits)) & 31];
    return out;
}

inline std::vector<uint8_t> unbase32(const std::string& text) {
    std::vector<uint8_t> out;
    int bits = 0;
    uint32_t val = 0;
    for (char ch : text) {
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        const char* at = std::strchr(kB32, ch);
        if (!at || !*at) continue;
        val = (val << 5) | (uint32_t)(at - kB32); bits += 5;
        if (bits >= 8) { out.push_back((uint8_t)((val >> (bits - 8)) & 255)); bits -= 8; }
    }
    return out;
}

inline std::string code(const std::string& secret, long long step) {
    std::vector<uint8_t> msg(8);
    for (int i = 7; i >= 0; --i) { msg[(size_t)i] = (uint8_t)(step & 255); step >>= 8; }
    auto h = hmacSha1(unbase32(secret), msg);
    const int o = h[19] & 15;
    const uint32_t bin = (uint32_t)(h[(size_t)o] & 127) << 24 | (uint32_t)h[(size_t)o + 1] << 16 |
                         (uint32_t)h[(size_t)o + 2] << 8 | h[(size_t)o + 3];
    std::string s = std::to_string(bin % 1000000);
    return std::string(6 - s.size(), '0') + s;
}

// The 30-second step the code belongs to (a step early or late is fine: clocks drift)
// if it's right and newer than `lastStep`; -1 if not.
inline long long check(const std::string& secret, std::string typed, long long unixTime, long long lastStep = -1) {
    std::string c;
    for (char ch : typed) if (ch >= '0' && ch <= '9') c += ch;
    if (c.size() != 6 || typed.size() > 12) return -1;
    const long long step = unixTime / 30;
    for (long long d : {0LL, -1LL, 1LL})
        if (step + d > lastStep && code(secret, step + d) == c) return step + d;
    return -1;
}

} // namespace Totp
