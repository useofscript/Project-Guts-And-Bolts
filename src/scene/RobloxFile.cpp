#include "RobloxFile.h"
#include "Guis.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Environment.h"
#include "Serializer.h"
#include "../renderer/MeshLibrary.h"
#include "../scripting/Luau.h"

#include <zstd.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>

namespace RobloxFile {

namespace {

constexpr float kImportScale = 0.5f;          // Roblox characters are twice our size
constexpr float kExportScale = 1.0f / kImportScale;
constexpr float kRobloxGravity = 196.2f;

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool endsWith(const std::string& s, const std::string& e) {
    return s.size() >= e.size() && lower(s.substr(s.size() - e.size())) == e;
}

// ===========================================================================
// A Roblox object as it is in the file, before converting it.
// ===========================================================================

struct Value {
    enum Kind { None, Str, Bool, Num, Vec3, CFrame, Color, Ref, Token, UDim, UDim2, Vec2 } kind = None;
    std::string s;
    bool        b = false;
    double      n = 0.0;
    glm::vec3   v{0.0f};          // Vector3 / Color3 (0..1) / CFrame position
    glm::mat3   r{1.0f};          // CFrame rotation (columns = right, up, back)
    int64_t     ref = -1;
    glm::vec4   q{0.0f};          // UDim2 (x scale, x offset, y scale, y offset), UDim (scale, offset), Vector2 (x, y)
};

struct Inst {
    std::string className;
    int64_t     referent = -1;
    std::string xmlRef;
    std::map<std::string, Value> props;   // keys in lower case
    std::vector<Inst*> children;
    Inst*       parent = nullptr;

    const Value* get(const char* name) const {
        auto it = props.find(lower(name));
        return it == props.end() ? nullptr : &it->second;
    }
    std::string str(const char* name, const std::string& fb = "") const {
        const Value* v = get(name);
        return v && v->kind == Value::Str ? v->s : fb;
    }
    double num(const char* name, double fb) const {
        const Value* v = get(name);
        if (!v) return fb;
        if (v->kind == Value::Num || v->kind == Value::Token) return v->n;
        if (v->kind == Value::Bool) return v->b ? 1.0 : 0.0;
        return fb;
    }
    bool flag(const char* name, bool fb) const {
        const Value* v = get(name);
        return v && v->kind == Value::Bool ? v->b : fb;
    }
};

struct Document {
    std::vector<std::unique_ptr<Inst>> all;
    std::vector<Inst*> roots;
    std::string error;
};

// ===========================================================================
// Binary format (.rbxl / .rbxm)
// ===========================================================================

struct Reader {
    const uint8_t* p;
    size_t size, pos = 0;
    bool   bad = false;
    Reader(const uint8_t* d, size_t n) : p(d), size(n) {}
    bool has(size_t n) { if (pos + n > size) { bad = true; return false; } return true; }
    uint8_t  u8()  { if (!has(1)) return 0; return p[pos++]; }
    uint32_t u32() { if (!has(4)) return 0; uint32_t v; std::memcpy(&v, p + pos, 4); pos += 4; return v; }
    float    f32() { if (!has(4)) return 0; float v; std::memcpy(&v, p + pos, 4); pos += 4; return v; }
    double   f64() { if (!has(8)) return 0; double v; std::memcpy(&v, p + pos, 8); pos += 8; return v; }
    std::string str() {
        uint32_t n = u32();
        if (!has(n)) return {};
        std::string s((const char*)p + pos, n);
        pos += n;
        return s;
    }
    const uint8_t* take(size_t n) { if (!has(n)) return nullptr; const uint8_t* r = p + pos; pos += n; return r; }
};

// LZ4 block format (what Roblox used for years).
bool lz4Decompress(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t outLen) {
    out.clear();
    out.reserve(outLen);
    size_t i = 0;
    while (i < srcLen) {
        uint8_t token = src[i++];
        size_t lit = token >> 4;
        if (lit == 15) { uint8_t b; do { if (i >= srcLen) return false; b = src[i++]; lit += b; } while (b == 255); }
        if (i + lit > srcLen) return false;
        out.insert(out.end(), src + i, src + i + lit);
        i += lit;
        if (i >= srcLen) break;                      // last sequence has no match
        if (i + 2 > srcLen) return false;
        size_t offset = src[i] | (src[i + 1] << 8);
        i += 2;
        if (offset == 0 || offset > out.size()) return false;
        size_t len = (token & 15);
        if (len == 15) { uint8_t b; do { if (i >= srcLen) return false; b = src[i++]; len += b; } while (b == 255); }
        len += 4;
        size_t start = out.size() - offset;
        for (size_t k = 0; k < len; ++k) out.push_back(out[start + k]);   // may overlap: copy byte by byte
    }
    return out.size() == outLen;
}

// Arrays in the binary format are "interleaved": all first bytes, then all
// second bytes, ... (compresses better). Values are big-endian.
std::vector<uint32_t> interleaved32(Reader& r, size_t count) {
    std::vector<uint32_t> out(count, 0);
    const uint8_t* d = r.take(count * 4);
    if (!d) return out;
    for (size_t i = 0; i < count; ++i)
        out[i] = (uint32_t)d[i] << 24 | (uint32_t)d[count + i] << 16 | (uint32_t)d[2 * count + i] << 8 | d[3 * count + i];
    return out;
}
std::vector<int64_t> interleaved64(Reader& r, size_t count) {
    std::vector<int64_t> out(count, 0);
    const uint8_t* d = r.take(count * 8);
    if (!d) return out;
    for (size_t i = 0; i < count; ++i) {
        uint64_t v = 0;
        for (int b = 0; b < 8; ++b) v = (v << 8) | d[b * count + i];
        out[i] = (int64_t)((v >> 1) ^ (~(v & 1) + 1));   // zigzag
    }
    return out;
}
int32_t unzigzag(uint32_t v) { return (int32_t)((v >> 1) ^ (uint32_t)(-(int32_t)(v & 1))); }
float   robloxFloat(uint32_t v) { v = (v >> 1) | (v << 31); float f; std::memcpy(&f, &v, 4); return f; }

std::vector<int32_t> ints(Reader& r, size_t count) {
    auto raw = interleaved32(r, count);
    std::vector<int32_t> out(count);
    for (size_t i = 0; i < count; ++i) out[i] = unzigzag(raw[i]);
    return out;
}
std::vector<float> floats(Reader& r, size_t count) {
    auto raw = interleaved32(r, count);
    std::vector<float> out(count);
    for (size_t i = 0; i < count; ++i) out[i] = robloxFloat(raw[i]);
    return out;
}
std::vector<int64_t> referents(Reader& r, size_t count) {
    auto d = ints(r, count);
    std::vector<int64_t> out(count);
    int64_t acc = 0;
    for (size_t i = 0; i < count; ++i) { acc += d[i]; out[i] = acc; }
    return out;
}

// The 24 "tidy" rotations Roblox stores as one byte: axis ids 0..5 are
// +X +Y +Z -X -Y -Z (Roblox's NormalId order: Right, Top, Back, Left, Bottom, Front).
glm::mat3 rotationFromId(uint8_t id) {
    static const glm::vec3 axes[6] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}, {0, -1, 0}, {0, 0, -1}};
    int x = (id - 1) / 6, y = (id - 1) % 6;
    if (x < 0 || x > 5) return glm::mat3(1.0f);
    glm::vec3 right = axes[x], up = axes[y];
    return glm::mat3(right, up, glm::cross(right, up));
}

// Roblox's row-major R00..R22 -> a matrix whose columns are right, up, back.
glm::mat3 fromRows(const float m[9]) {
    return glm::mat3(glm::vec3(m[0], m[3], m[6]), glm::vec3(m[1], m[4], m[7]), glm::vec3(m[2], m[5], m[8]));
}

struct ClassInfo {
    std::string name;
    std::vector<Inst*> objects;
};

bool readBinary(const std::vector<uint8_t>& data, Document& doc) {
    Reader head(data.data(), data.size());
    if (data.size() < 32 || std::memcmp(data.data(), "<roblox!", 8) != 0) { doc.error = "not a Roblox file"; return false; }
    head.pos = 32;
    std::map<uint32_t, ClassInfo> classes;
    std::unordered_map<int64_t, Inst*> byRef;
    std::vector<std::string> shared;

    while (head.pos + 16 <= data.size()) {
        char name[5] = {0};
        std::memcpy(name, data.data() + head.pos, 4);
        head.pos += 4;
        uint32_t clen = head.u32(), ulen = head.u32();
        head.u32();
        std::vector<uint8_t> chunk;
        if (clen == 0) {
            const uint8_t* d = head.take(ulen);
            if (!d) break;
            chunk.assign(d, d + ulen);
        } else {
            const uint8_t* d = head.take(clen);
            if (!d) break;
            if (clen >= 4 && d[0] == 0x28 && d[1] == 0xB5 && d[2] == 0x2F && d[3] == 0xFD) {
                chunk.resize(ulen);
                size_t got = ZSTD_decompress(chunk.data(), ulen, d, clen);
                if (ZSTD_isError(got) || got != ulen) { doc.error = "couldn't unpack part of the file (zstd)"; return false; }
            } else if (!lz4Decompress(d, clen, chunk, ulen)) {
                doc.error = "couldn't unpack part of the file (lz4)";
                return false;
            }
        }
        Reader r(chunk.data(), chunk.size());
        std::string kind(name);

        if (kind == "SSTR") {
            r.u32();                                   // version
            uint32_t n = r.u32();
            for (uint32_t i = 0; i < n && !r.bad; ++i) { r.take(16); shared.push_back(r.str()); }
        } else if (kind == "INST") {
            uint32_t classId = r.u32();
            ClassInfo& ci = classes[classId];
            ci.name = r.str();
            uint8_t isService = r.u8();
            uint32_t count = r.u32();
            auto refs = referents(r, count);
            for (uint32_t i = 0; i < count; ++i) {
                auto inst = std::make_unique<Inst>();
                inst->className = ci.name;
                inst->referent = refs[i];
                byRef[refs[i]] = inst.get();
                ci.objects.push_back(inst.get());
                doc.all.push_back(std::move(inst));
            }
            (void)isService;
        } else if (kind == "PROP") {
            uint32_t classId = r.u32();
            std::string prop = lower(r.str());
            uint8_t type = r.u8();
            auto it = classes.find(classId);
            if (it == classes.end()) continue;
            auto& objs = it->second.objects;
            size_t n = objs.size();
            auto set = [&](size_t i, Value v) { if (i < n) objs[i]->props[prop] = std::move(v); };
            switch (type) {
            case 0x01:                                             // String
            case 0x1D:                                             // Bytecode
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Str; v.s = r.str(); set(i, v); }
                break;
            case 0x02:                                             // Bool
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Bool; v.b = r.u8() != 0; set(i, v); }
                break;
            case 0x03: {                                           // Int32
                auto a = ints(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Num; v.n = a[i]; set(i, v); }
                break;
            }
            case 0x04: {                                           // Float32
                auto a = floats(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Num; v.n = a[i]; set(i, v); }
                break;
            }
            case 0x05:                                             // Float64
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Num; v.n = r.f64(); set(i, v); }
                break;
            case 0x06: {                                           // UDim
                auto S = floats(r, n); auto O = ints(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::UDim; v.q = {S[i], (float)O[i], 0, 0}; set(i, v); }
                break;
            }
            case 0x07: {                                           // UDim2
                auto SX = floats(r, n), SY = floats(r, n); auto OX = ints(r, n), OY = ints(r, n);
                for (size_t i = 0; i < n; ++i) {
                    Value v; v.kind = Value::UDim2; v.q = {SX[i], (float)OX[i], SY[i], (float)OY[i]}; set(i, v);
                }
                break;
            }
            case 0x0D: {                                           // Vector2
                auto X = floats(r, n), Y = floats(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Vec2; v.q = {X[i], Y[i], 0, 0}; set(i, v); }
                break;
            }
            case 0x0C: {                                           // Color3
                auto R = floats(r, n), G = floats(r, n), B = floats(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Color; v.v = {R[i], G[i], B[i]}; set(i, v); }
                break;
            }
            case 0x0E: {                                           // Vector3
                auto X = floats(r, n), Y = floats(r, n), Z = floats(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Vec3; v.v = {X[i], Y[i], Z[i]}; set(i, v); }
                break;
            }
            case 0x10: {                                           // CFrame
                std::vector<glm::mat3> rots(n, glm::mat3(1.0f));
                for (size_t i = 0; i < n && !r.bad; ++i) {
                    uint8_t id = r.u8();
                    if (id == 0) { float m[9]; for (float& f : m) f = r.f32(); rots[i] = fromRows(m); }
                    else rots[i] = rotationFromId(id);
                }
                auto X = floats(r, n), Y = floats(r, n), Z = floats(r, n);
                for (size_t i = 0; i < n; ++i) {
                    Value v; v.kind = Value::CFrame; v.v = {X[i], Y[i], Z[i]}; v.r = rots[i]; set(i, v);
                }
                break;
            }
            case 0x12: {                                           // Enum
                auto a = interleaved32(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Token; v.n = a[i]; set(i, v); }
                break;
            }
            case 0x13: {                                           // Referent
                auto a = referents(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Ref; v.ref = a[i]; set(i, v); }
                break;
            }
            case 0x1A: {                                           // Color3uint8
                const uint8_t* R = r.take(n); const uint8_t* G = r.take(n); const uint8_t* B = r.take(n);
                if (!R || !G || !B) break;
                for (size_t i = 0; i < n; ++i) {
                    Value v; v.kind = Value::Color; v.v = glm::vec3(R[i], G[i], B[i]) / 255.0f; set(i, v);
                }
                break;
            }
            case 0x1B: {                                           // Int64
                auto a = interleaved64(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Num; v.n = (double)a[i]; set(i, v); }
                break;
            }
            case 0x1C: {                                           // SharedString
                auto a = interleaved32(r, n);
                for (size_t i = 0; i < n; ++i) {
                    Value v; v.kind = Value::Str; if (a[i] < shared.size()) v.s = shared[a[i]]; set(i, v);
                }
                break;
            }
            default: break;                                         // a type we don't need
            }
        } else if (kind == "PRNT") {
            r.u8();
            uint32_t n = r.u32();
            auto kids = referents(r, n), parents = referents(r, n);
            for (uint32_t i = 0; i < n; ++i) {
                Inst* c = byRef.count(kids[i]) ? byRef[kids[i]] : nullptr;
                if (!c) continue;
                if (parents[i] >= 0 && byRef.count(parents[i])) {
                    c->parent = byRef[parents[i]];
                    c->parent->children.push_back(c);
                } else {
                    doc.roots.push_back(c);
                }
            }
        } else if (kind == "END") {
            break;
        }
    }
    return true;
}

// ===========================================================================
// XML format (.rbxlx / .rbxmx)
// ===========================================================================

struct XmlNode {
    std::string tag, text;
    std::map<std::string, std::string> attrs;
    std::vector<std::unique_ptr<XmlNode>> kids;
    const XmlNode* child(const char* t) const {
        for (auto& k : kids) if (k->tag == t) return k.get();
        return nullptr;
    }
};

std::string xmlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos) { out += s[i]; continue; }
        std::string e = s.substr(i + 1, semi - i - 1);
        if (e == "lt") out += '<'; else if (e == "gt") out += '>'; else if (e == "amp") out += '&';
        else if (e == "quot") out += '"'; else if (e == "apos") out += '\'';
        else if (!e.empty() && e[0] == '#') {
            unsigned long c = e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? std::strtoul(e.c_str() + 2, nullptr, 16)
                                                                            : std::strtoul(e.c_str() + 1, nullptr, 10);
            if (c < 0x80) out += (char)c;
            else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 63)); }
            else { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 63)); out += (char)(0x80 | (c & 63)); }
        } else { out += '&' + e + ';'; }
        i = semi;
    }
    return out;
}

// A small XML reader: elements, attributes, text, CDATA. Enough for Roblox files.
std::unique_ptr<XmlNode> parseXml(const std::string& s, std::string& error) {
    auto root = std::make_unique<XmlNode>();
    std::vector<XmlNode*> stack{root.get()};
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] != '<') {
            size_t e = s.find('<', i);
            if (e == std::string::npos) e = s.size();
            stack.back()->text += xmlDecode(s.substr(i, e - i));
            i = e;
            continue;
        }
        if (s.compare(i, 9, "<![CDATA[") == 0) {
            size_t e = s.find("]]>", i + 9);
            if (e == std::string::npos) { error = "unfinished CDATA"; return nullptr; }
            stack.back()->text += s.substr(i + 9, e - i - 9);
            i = e + 3;
            continue;
        }
        if (s.compare(i, 4, "<!--") == 0) { size_t e = s.find("-->", i); i = e == std::string::npos ? s.size() : e + 3; continue; }
        if (s.compare(i, 2, "<?") == 0 || s.compare(i, 2, "<!") == 0) { size_t e = s.find('>', i); i = e == std::string::npos ? s.size() : e + 1; continue; }
        if (s.compare(i, 2, "</") == 0) {
            size_t e = s.find('>', i);
            if (stack.size() > 1) stack.pop_back();
            i = e == std::string::npos ? s.size() : e + 1;
            continue;
        }
        // Opening tag.
        size_t j = i + 1;
        while (j < s.size() && !std::isspace((unsigned char)s[j]) && s[j] != '>' && s[j] != '/') ++j;
        auto node = std::make_unique<XmlNode>();
        node->tag = s.substr(i + 1, j - i - 1);
        bool selfClose = false;
        while (j < s.size() && s[j] != '>') {
            if (s[j] == '/') { selfClose = true; ++j; continue; }
            if (std::isspace((unsigned char)s[j])) { ++j; continue; }
            size_t eq = s.find('=', j);
            if (eq == std::string::npos) break;
            std::string key = s.substr(j, eq - j);
            while (!key.empty() && std::isspace((unsigned char)key.back())) key.pop_back();
            size_t q = eq + 1;
            while (q < s.size() && std::isspace((unsigned char)s[q])) ++q;
            char quote = s[q];
            size_t qe = s.find(quote, q + 1);
            if (qe == std::string::npos) break;
            node->attrs[key] = xmlDecode(s.substr(q + 1, qe - q - 1));
            j = qe + 1;
        }
        i = j + 1;
        XmlNode* raw = node.get();
        stack.back()->kids.push_back(std::move(node));
        if (!selfClose) stack.push_back(raw);
    }
    return root;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::vector<uint8_t> base64Decode(const std::string& in) {
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> out;
    uint32_t buf = 0; int bits = 0;
    for (char c : in) {
        size_t v = chars.find(c);
        if (v == std::string::npos) continue;
        buf = (buf << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t)(buf >> bits)); }
    }
    return out;
}

float fnum(const XmlNode* n) { return n ? (float)std::atof(trim(n->text).c_str()) : 0.0f; }

void readXmlItem(const XmlNode& x, Inst* parent, Document& doc, std::map<std::string, Inst*>& refs,
                 const std::map<std::string, std::string>& shared) {
    auto inst = std::make_unique<Inst>();
    inst->className = x.attrs.count("class") ? x.attrs.at("class") : "";
    inst->xmlRef = x.attrs.count("referent") ? x.attrs.at("referent") : "";
    Inst* raw = inst.get();
    if (!inst->xmlRef.empty()) refs[inst->xmlRef] = raw;
    if (const XmlNode* props = x.child("Properties")) {
        for (auto& p : props->kids) {
            std::string name = lower(p->attrs.count("name") ? p->attrs.at("name") : "");
            if (name.empty()) continue;
            Value v;
            const std::string& t = p->tag;
            std::string text = trim(p->text);
            if (t == "string" || t == "ProtectedString" || t == "Content" || t == "url") {
                v.kind = Value::Str; v.s = t == "ProtectedString" ? p->text : text;
                if (t == "Content") if (const XmlNode* u = p->child("url")) v.s = trim(u->text);
            } else if (t == "BinaryString") {
                auto bytes = base64Decode(text);
                v.kind = Value::Str; v.s.assign(bytes.begin(), bytes.end());
            } else if (t == "SharedString") {
                v.kind = Value::Str;
                auto it = shared.find(text);
                if (it != shared.end()) v.s = it->second;
            } else if (t == "bool") {
                v.kind = Value::Bool; v.b = text == "true";
            } else if (t == "int" || t == "int64" || t == "float" || t == "double") {
                v.kind = Value::Num; v.n = std::atof(text.c_str());
            } else if (t == "token") {
                v.kind = Value::Token; v.n = std::atof(text.c_str());
            } else if (t == "Vector3") {
                v.kind = Value::Vec3; v.v = {fnum(p->child("X")), fnum(p->child("Y")), fnum(p->child("Z"))};
            } else if (t == "UDim2") {
                v.kind = Value::UDim2; v.q = {fnum(p->child("XS")), fnum(p->child("XO")), fnum(p->child("YS")), fnum(p->child("YO"))};
            } else if (t == "UDim") {
                v.kind = Value::UDim; v.q = {fnum(p->child("S")), fnum(p->child("O")), 0, 0};
            } else if (t == "Vector2") {
                v.kind = Value::Vec2; v.q = {fnum(p->child("X")), fnum(p->child("Y")), 0, 0};
            } else if (t == "Color3") {
                v.kind = Value::Color;
                if (p->child("R")) v.v = {fnum(p->child("R")), fnum(p->child("G")), fnum(p->child("B"))};
                else { uint32_t c = (uint32_t)std::strtoul(text.c_str(), nullptr, 10);
                       v.v = glm::vec3((c >> 16) & 255, (c >> 8) & 255, c & 255) / 255.0f; }
            } else if (t == "Color3uint8") {
                uint32_t c = (uint32_t)std::strtoul(text.c_str(), nullptr, 10);
                v.kind = Value::Color; v.v = glm::vec3((c >> 16) & 255, (c >> 8) & 255, c & 255) / 255.0f;
            } else if (t == "CoordinateFrame" || t == "CFrame") {
                v.kind = Value::CFrame;
                v.v = {fnum(p->child("X")), fnum(p->child("Y")), fnum(p->child("Z"))};
                float m[9];
                const char* names[9] = {"R00", "R01", "R02", "R10", "R11", "R12", "R20", "R21", "R22"};
                for (int k = 0; k < 9; ++k) m[k] = p->child(names[k]) ? fnum(p->child(names[k])) : (k % 4 == 0 ? 1.0f : 0.0f);
                v.r = fromRows(m);
            } else if (t == "Ref") {
                v.kind = Value::Ref; v.s = text;
            } else {
                continue;
            }
            inst->props[name] = v;
        }
    }
    if (parent) { inst->parent = parent; parent->children.push_back(raw); }
    else doc.roots.push_back(raw);
    doc.all.push_back(std::move(inst));
    for (auto& k : x.kids)
        if (k->tag == "Item") readXmlItem(*k, raw, doc, refs, shared);
}

bool readXml(const std::string& text, Document& doc) {
    auto xml = parseXml(text, doc.error);
    if (!xml) return false;
    const XmlNode* roblox = xml->child("roblox");
    if (!roblox) { doc.error = "not a Roblox XML file"; return false; }
    std::map<std::string, std::string> shared;
    if (const XmlNode* ss = roblox->child("SharedStrings"))
        for (auto& k : ss->kids) if (k->attrs.count("md5")) {
            auto bytes = base64Decode(trim(k->text));
            shared[k->attrs.at("md5")] = std::string(bytes.begin(), bytes.end());
        }
    std::map<std::string, Inst*> refs;
    for (auto& k : roblox->kids) if (k->tag == "Item") readXmlItem(*k, nullptr, doc, refs, shared);
    // XML references are names ("RBX..."); give every object a number like the binary format.
    int64_t next = 1;
    std::map<std::string, int64_t> ids;
    for (auto& [name, inst] : refs) { inst->referent = next; ids[name] = next++; }
    for (auto& inst : doc.all)
        for (auto& [k, v] : inst->props)
            if (v.kind == Value::Ref) v.ref = ids.count(v.s) ? ids[v.s] : -1;
    return true;
}

bool readFile(const std::string& path, Document& doc) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { doc.error = "couldn't open the file"; return false; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() >= 8 && std::memcmp(data.data(), "<roblox!", 8) == 0) return readBinary(data, doc);
    return readXml(std::string(data.begin(), data.end()), doc);
}

// ===========================================================================
// Roblox objects -> Guts and Bolts objects
// ===========================================================================

Material materialFromRoblox(int m) {
    switch (m) {
        case 288:                                    return Material::Neon;
        case 512: case 528:                          return Material::Wood;
        case 1040: case 1056: case 1072: case 1088:  return Material::Metal;
        case 1536: case 1552:                        return Material::Ice;
        case 1568:                                   return Material::Glass;
        case 784: case 788: case 800: case 816: case 820: case 832: case 836: case 848:
        case 864: case 880: case 896: case 912: case 1376:
                                                     return Material::Concrete;
        default:                                     return Material::Plastic;
    }
}
int materialToRoblox(Material m) {
    switch (m) {
        case Material::Neon:     return 288;
        case Material::Wood:     return 512;
        case Material::Metal:    return 1088;
        case Material::Ice:      return 1536;
        case Material::Glass:    return 1568;
        case Material::Concrete: return 816;
        default:                 return 256;
    }
}

// Roblox attributes are stored as a small binary blob.
void readAttributes(const std::string& blob, SceneNode& node) {
    if (blob.size() < 4) return;
    Reader r((const uint8_t*)blob.data(), blob.size());
    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count && !r.bad; ++i) {
        Attribute a;
        a.name = r.str();
        uint8_t type = r.u8();
        switch (type) {
            case 0x02: a.type = Attribute::String; a.s = r.str(); break;
            case 0x03: a.type = Attribute::Bool; a.b = r.u8() != 0; break;
            case 0x05: a.type = Attribute::Number; a.n = r.f32(); break;
            case 0x06: a.type = Attribute::Number; a.n = r.f64(); break;
            case 0x0F: a.type = Attribute::Color3; a.v = {r.f32(), r.f32(), r.f32()}; break;
            case 0x11: a.type = Attribute::Vector3; a.v = {r.f32(), r.f32(), r.f32()}; break;
            default: return;   // a kind we can't read: stop (we don't know how long it is)
        }
        if (!r.bad) node.attributes.push_back(a);
    }
}

struct Converter {
    Scene& scene;
    Report& report;
    std::unordered_map<const Inst*, SceneNode*> made;

    static bool isPartClass(const std::string& c) {
        static const char* names[] = {"Part", "SpawnLocation", "Seat", "VehicleSeat", "TrussPart", "WedgePart",
                                      "CornerWedgePart", "MeshPart", "UnionOperation", "IntersectOperation",
                                      "SkateboardPlatform", "FlagStand", "PartOperation"};
        for (const char* n : names) if (c == n) return true;
        return false;
    }

    void note(const std::string& n) {
        if (std::find(report.notes.begin(), report.notes.end(), n) == report.notes.end() && report.notes.size() < 30)
            report.notes.push_back(n);
    }

    // World CFrame -> a node placed under `parent` (our transforms are relative).
    static void place(SceneNode& n, SceneNode* parent, glm::vec3 pos, glm::mat3 rot, glm::vec3 size) {
        glm::mat4 world(1.0f);
        world[0] = glm::vec4(rot[0] * size.x, 0);
        world[1] = glm::vec4(rot[1] * size.y, 0);
        world[2] = glm::vec4(rot[2] * size.z, 0);
        world[3] = glm::vec4(pos, 1);
        glm::mat4 local = parent ? glm::inverse(parent->worldMatrix()) * world : world;
        glm::vec3 s(glm::length(glm::vec3(local[0])), glm::length(glm::vec3(local[1])), glm::length(glm::vec3(local[2])));
        glm::mat4 r(1.0f);
        r[0] = glm::vec4(glm::vec3(local[0]) / std::max(s.x, 1e-6f), 0);
        r[1] = glm::vec4(glm::vec3(local[1]) / std::max(s.y, 1e-6f), 0);
        r[2] = glm::vec4(glm::vec3(local[2]) / std::max(s.z, 1e-6f), 0);
        float z, y, x;
        glm::extractEulerAngleZYX(r, z, y, x);          // our rotation is Rz * Ry * Rx
        n.transform.position = glm::vec3(local[3]);
        n.transform.rotation = glm::degrees(glm::vec3(x, y, z));
        n.transform.scale = s;
    }

    SceneNode* convert(const Inst& in, SceneNode* parent) {
        const std::string& c = in.className;
        std::string name = in.str("Name", c);
        std::unique_ptr<SceneNode> node;

        if (isPartClass(c)) {
            node = std::make_unique<SceneNode>(name, NodeKind::Part);
            int shape = (int)in.num("shape", in.num("Shape", 1));
            PrimitiveType prim = PrimitiveType::Cube;
            if (c == "Part" || c == "SpawnLocation" || c == "Seat") {
                if (shape == 0) prim = PrimitiveType::Sphere;
                else if (shape == 2) prim = PrimitiveType::Cylinder;
                else if (shape == 3 || shape == 4) note("Wedges come in as blocks.");
            }
            if (c == "WedgePart" || c == "CornerWedgePart") note("Wedges come in as blocks.");
            if (c == "Seat" || c == "VehicleSeat") {   // you sit on it (whatever it's called)
                node->tags.push_back("Seat");
                if (in.flag("Disabled", false)) {
                    Attribute a; a.name = "Disabled"; a.type = Attribute::Bool; a.b = true;
                    node->attributes.push_back(a);
                }
            }
            if (c == "MeshPart" || c.find("Operation") != std::string::npos)
                note("Meshes and unions come in as blocks of the same size.");
            node->primitiveType = prim;
            node->mesh = MeshLibrary::get(prim);
            glm::vec3 size(4, 1, 2);
            if (const Value* v = in.get("size")) size = v->v;
            glm::vec3 pos(0.0f);
            glm::mat3 rot(1.0f);
            if (const Value* v = in.get("CFrame")) { pos = v->v; rot = v->r; }
            size *= kImportScale;
            pos *= kImportScale;
            if (prim == PrimitiveType::Sphere) size = glm::vec3(std::min({size.x, size.y, size.z}));
            if (prim == PrimitiveType::Cylinder) {
                // Roblox cylinders lie along X; ours stand along Y.
                rot = rot * glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0, 0, 1)));
                size = glm::vec3(size.y, size.x, size.z);
            }
            place(*node, parent, pos, rot, size);
            if (const Value* v = in.get("Color3uint8")) node->color = v->v;
            else if (const Value* v2 = in.get("Color")) node->color = v2->v;
            node->transparency = (float)in.num("Transparency", 0.0);
            node->anchored = in.flag("Anchored", false);
            node->canCollide = in.flag("CanCollide", true);
            node->castShadow = in.flag("CastShadow", true);
            node->locked = in.flag("Locked", false);
            node->material = materialFromRoblox((int)in.num("Material", 256));
            ++report.parts;
        } else if (c == "IntValue" || c == "NumberValue" || c == "StringValue" || c == "BoolValue") {
            node = std::make_unique<SceneNode>(name, NodeKind::Value);
            node->intValue = c == "IntValue";
            node->value.type = c == "StringValue" ? Attribute::String : c == "BoolValue" ? Attribute::Bool : Attribute::Number;
            node->value.n = in.num("Value", 0.0);
            node->value.b = in.num("Value", 0.0) != 0.0;
            node->value.s = in.str("Value");
        } else if (GuiType gt; Guis::typeFromName(c, gt) || c == "BillboardGui" || c == "SurfaceGui") {
            // Game UI (ScreenGui, Frame, TextLabel, TextButton, ImageLabel, ImageButton, UICorner, UIStroke).
            if (c == "BillboardGui" || c == "SurfaceGui") {
                note(c + "s aren't supported yet (only ScreenGui UI is).");
                return nullptr;
            }
            node = std::make_unique<SceneNode>(name, NodeKind::Gui);
            node->gui.type = gt;
            Guis::setDefaults(*node);
            GuiProps& g = node->gui;
            auto u2 = [&](const char* k, UDim2& out) {
                if (const Value* v = in.get(k); v && v->kind == Value::UDim2) out = {v->q.x, v->q.y, v->q.z, v->q.w};
            };
            auto col = [&](const char* k, glm::vec3& out) { if (const Value* v = in.get(k)) out = v->v; };
            auto f = [&](const char* k, float& out) { if (in.get(k)) out = (float)in.num(k, out); };
            u2("Position", g.pos);
            u2("Size", g.size);
            if (const Value* v = in.get("AnchorPoint"); v && v->kind == Value::Vec2) g.anchor = {v->q.x, v->q.y};
            col("BackgroundColor3", g.bg);
            f("BackgroundTransparency", g.bgTransparency);
            col("BorderColor3", g.borderColor);
            g.border = (int)in.num("BorderSizePixel", g.border);
            g.zIndex = (int)in.num("ZIndex", g.zIndex);
            g.clips = in.flag("ClipsDescendants", false);
            node->visible = in.flag("Visible", true);
            if (gt == GuiType::TextLabel || gt == GuiType::TextButton) {
                g.text = in.str("Text");
                col("TextColor3", g.textColor);
                f("TextSize", g.textSize);
                g.textScaled = in.flag("TextScaled", false);
                g.textWrapped = in.flag("TextWrapped", false);
                int xa = (int)in.num("TextXAlignment", 2), ya = (int)in.num("TextYAlignment", 1);   // Roblox: Left 0, Right 1, Center 2
                g.xAlign = xa == 0 ? 0 : xa == 1 ? 2 : 1;
                g.yAlign = std::clamp(ya, 0, 2);
                f("TextTransparency", g.textTransparency);
                col("TextStrokeColor3", g.strokeColor);
                f("TextStrokeTransparency", g.strokeTransparency);
            }
            if (gt == GuiType::ImageLabel || gt == GuiType::ImageButton) {
                g.image = in.str("Image");   // rbxassetid:// pictures won't load here, but are kept
                col("ImageColor3", g.imageColor);
                f("ImageTransparency", g.imageTransparency);
                if (g.image.find("rbxasset") != std::string::npos) note("Roblox pictures can't be downloaded; pick one for " + name + ".");
            }
            if (gt == GuiType::TextButton || gt == GuiType::ImageButton) g.autoButtonColor = in.flag("AutoButtonColor", true);
            if (gt == GuiType::ScreenGui) { node->enabled = in.flag("Enabled", true); g.displayOrder = (int)in.num("DisplayOrder", 0); }
            if (gt == GuiType::UICorner)
                if (const Value* v = in.get("CornerRadius"); v && v->kind == Value::UDim) g.corner = {v->q.x, v->q.y, 0, 0};
            if (gt == GuiType::UIStroke) {
                col("Color", g.borderColor);
                f("Thickness", g.thickness);
                g.bgTransparency = (float)in.num("Transparency", 0.0);
                node->enabled = in.flag("Enabled", true);
            }
        } else if (c == "Decal" || c == "Texture") {
            node = std::make_unique<SceneNode>(name, NodeKind::Decal);
            node->texture = in.str("Texture");   // rbxassetid:// links won't load here, but are kept
            int f = (int)in.num("Face", 5);
            node->face = (Face)(f >= 0 && f < 6 ? f : 5);
            if (const Value* v = in.get("Color3")) node->color = v->v;
            else node->color = {1, 1, 1};
            node->transparency = (float)in.num("Transparency", 0.0);
        } else if (c == "Tool" || c == "HopperBin") {
            node = std::make_unique<SceneNode>(name, NodeKind::Tool);
            node->enabled = in.flag("Enabled", true);
            node->canBeDropped = in.flag("CanBeDropped", true);
            node->toolTip = in.str("ToolTip");
            ++report.models;
        } else if (c == "Model" || c == "Folder" || c == "Configuration" || c == "Accessory") {
            node = std::make_unique<SceneNode>(name, NodeKind::Model);
            ++report.models;
        } else if (c == "Script" || c == "LocalScript" || c == "ModuleScript") {
            node = std::make_unique<SceneNode>(name, NodeKind::Script);
            std::vector<std::string> notes;
            node->source = Luau::toLua(in.str("Source"), &notes);
            for (auto& n : notes) note(name + ": " + n);
            node->isModule = c == "ModuleScript";
            node->enabled = !in.flag("Disabled", false) && in.flag("Enabled", true);
            if (c == "LocalScript") note("LocalScripts run as normal scripts.");
            ++report.scripts;
        } else if (c == "PointLight" || c == "SpotLight" || c == "SurfaceLight") {
            node = std::make_unique<SceneNode>(name, NodeKind::Light);
            node->lightType = c == "PointLight" ? LightType::Point : LightType::Spot;
            if (const Value* v = in.get("Color")) node->color = v->v;
            node->brightness = (float)in.num("Brightness", 1.0) * 2.0f;
            node->range = (float)in.num("Range", 8.0) * kImportScale;
            node->spotAngle = (float)in.num("Angle", 90.0);
            node->enabled = in.flag("Enabled", true);
            ++report.lights;
        } else if (c == "Sound") {
            node = std::make_unique<SceneNode>(name, NodeKind::Sound);
            std::string id = in.str("SoundId");
            node->soundId = id.find("rbxasset") != std::string::npos ? "" : id;
            if (!id.empty() && node->soundId.empty()) note("Roblox sound ids can't be downloaded; pick a sound in Properties.");
            node->volume = (float)in.num("Volume", 0.5);
            node->pitch = (float)in.num("PlaybackSpeed", 1.0);
            node->looped = in.flag("Looped", false);
            node->autoplay = in.flag("Playing", false);
            ++report.sounds;
        } else if (c == "Attachment") {
            node = std::make_unique<SceneNode>(name, NodeKind::Attachment);
            glm::vec3 pos(0.0f);
            glm::mat3 rot(1.0f);
            if (const Value* v = in.get("CFrame")) { pos = v->v * kImportScale; rot = v->r; }
            // Attachments are placed relative to their part (in studs).
            glm::vec3 psize = parent ? parent->transform.scale : glm::vec3(1.0f);
            node->transform.position = pos / glm::max(psize, glm::vec3(1e-4f));
            float z, y, x;
            glm::extractEulerAngleZYX(glm::mat4(rot), z, y, x);
            node->transform.rotation = glm::degrees(glm::vec3(x, y, z));
            ++report.constraints;
        } else if (c == "RopeConstraint" || c == "RodConstraint" || c == "SpringConstraint" || c == "HingeConstraint" ||
                   c == "WeldConstraint" || c == "Weld" || c == "ManualWeld" || c == "Snap") {
            node = std::make_unique<SceneNode>(name, NodeKind::Constraint);
            if (c == "RopeConstraint")   node->constraintType = ConstraintType::Rope;
            if (c == "RodConstraint")    node->constraintType = ConstraintType::Rod;
            if (c == "SpringConstraint") node->constraintType = ConstraintType::Spring;
            if (c == "HingeConstraint")  node->constraintType = ConstraintType::Hinge;
            if (c == "WeldConstraint" || c == "Weld" || c == "ManualWeld" || c == "Snap") node->constraintType = ConstraintType::Weld;
            node->length = c == "SpringConstraint" ? (float)in.num("FreeLength", 1.0) * kImportScale
                                                   : (float)in.num("Length", -2.0) * kImportScale;
            node->stiffness = (float)in.num("Stiffness", 200.0);
            node->damping = (float)in.num("Damping", 5.0);
            node->thickness = (float)in.num("Thickness", 0.2) * kImportScale;
            if (c == "HingeConstraint" && (int)in.num("ActuatorType", 0) == 1) {
                node->motorSpeed = (float)in.num("AngularVelocity", 0.0);
                node->motorTorque = std::min(20000.0f, (float)in.num("MotorMaxTorque", 1000.0));
            }
            node->enabled = in.flag("Enabled", true);
            node->visible = in.flag("Visible", c == "RopeConstraint" || c == "RodConstraint" || c == "SpringConstraint");
            node->color = {0.45f, 0.32f, 0.2f};
            ++report.constraints;
        } else if (c == "Humanoid" || c == "Decal" || c == "Texture" || c == "SpecialMesh" || c == "TouchTransmitter" ||
                   c.find("Value") != std::string::npos || c == "Camera" || c == "Terrain") {
            if (c == "Decal" || c == "Texture") note("Decals and textures aren't supported yet.");
            if (c.find("Value") != std::string::npos && c != "Value") {
                // IntValue / StringValue ... become attributes on the parent.
                if (parent) {
                    Attribute a;
                    a.name = name;
                    const Value* v = in.get("Value");
                    if (v && v->kind == Value::Str) { a.type = Attribute::String; a.s = v->s; }
                    else if (v && v->kind == Value::Bool) { a.type = Attribute::Bool; a.b = v->b; }
                    else if (v && (v->kind == Value::Num || v->kind == Value::Token)) { a.type = Attribute::Number; a.n = v->n; }
                    else if (v && v->kind == Value::Vec3) { a.type = Attribute::Vector3; a.v = v->v; }
                    else if (v && v->kind == Value::Color) { a.type = Attribute::Color3; a.v = v->v; }
                    else return nullptr;
                    parent->attributes.push_back(a);
                    note("Value objects (IntValue, ...) become attributes on their parent.");
                }
            }
            ++report.other;
            return nullptr;
        } else {
            ++report.other;
            // Unknown containers still keep their children (e.g. a Folder-like service).
            if (in.children.empty()) return nullptr;
            node = std::make_unique<SceneNode>(name, NodeKind::Model);
        }

        if (const Value* t = in.get("Tags"); t && t->kind == Value::Str) {
            std::string cur;
            for (char ch : t->s) { if (ch == '\0') { if (!cur.empty()) node->tags.push_back(cur); cur.clear(); } else cur += ch; }
            if (!cur.empty()) node->tags.push_back(cur);
        }
        if (const Value* a = in.get("AttributesSerialize"); a && a->kind == Value::Str) readAttributes(a->s, *node);

        SceneNode* raw = parent ? parent->addChild(std::move(node)) : scene.insert(std::move(node));
        made[&in] = raw;
        for (const Inst* k : in.children) convert(*k, raw);
        return raw;
    }

    // Constraints point at attachments / parts: fill in the ids once everything exists.
    void linkRefs(const Document& doc) {
        std::unordered_map<int64_t, SceneNode*> byRef;
        for (auto& [inst, node] : made) byRef[inst->referent] = node;
        for (auto& [inst, node] : made) {
            if (!node->isConstraint()) continue;
            auto ref = [&](const char* a, const char* b) -> uint64_t {
                const Value* v = inst->get(a);
                if (!v) v = inst->get(b);
                if (!v || v->kind != Value::Ref) return 0;
                auto it = byRef.find(v->ref);
                return it == byRef.end() ? 0 : it->second->id;
            };
            if (node->constraintType == ConstraintType::Weld) { node->ref0 = ref("Part0", "part0"); node->ref1 = ref("Part1", "part1"); }
            else { node->ref0 = ref("Attachment0", "attachment0"); node->ref1 = ref("Attachment1", "attachment1"); }
        }
        (void)doc;
    }
};

const Inst* findChild(const std::vector<Inst*>& list, const char* cls) {
    for (const Inst* i : list) if (i->className == cls) return i;
    return nullptr;
}

// ===========================================================================
// Guts and Bolts objects -> Roblox XML
// ===========================================================================

struct XmlWriter {
    std::ostringstream o;
    Scene& scene;
    explicit XmlWriter(Scene& s) : scene(s) {}

    static std::string esc(const std::string& s) {
        std::string out;
        for (char c : s) {
            switch (c) {
                case '<': out += "&lt;"; break; case '>': out += "&gt;"; break;
                case '&': out += "&amp;"; break; case '"': out += "&quot;"; break;
                default: out += c;
            }
        }
        return out;
    }
    static std::string ref(uint64_t id) {
        char b[40]; std::snprintf(b, sizeof(b), "RBX%016llX", (unsigned long long)id); return b;
    }
    void str(const char* n, const std::string& v) { o << "<string name=\"" << n << "\">" << esc(v) << "</string>\n"; }
    void boolean(const char* n, bool v) { o << "<bool name=\"" << n << "\">" << (v ? "true" : "false") << "</bool>\n"; }
    void flt(const char* n, double v) { o << "<float name=\"" << n << "\">" << v << "</float>\n"; }
    void token(const char* n, int v) { o << "<token name=\"" << n << "\">" << v << "</token>\n"; }
    void vec3(const char* n, glm::vec3 v) {
        o << "<Vector3 name=\"" << n << "\"><X>" << v.x << "</X><Y>" << v.y << "</Y><Z>" << v.z << "</Z></Vector3>\n";
    }
    void udim2(const char* n, const UDim2& u) {
        o << "<UDim2 name=\"" << n << "\"><XS>" << u.xs << "</XS><XO>" << (int)std::lround(u.xo) << "</XO><YS>" << u.ys
          << "</YS><YO>" << (int)std::lround(u.yo) << "</YO></UDim2>\n";
    }
    void vec2(const char* n, glm::vec2 v) {
        o << "<Vector2 name=\"" << n << "\"><X>" << v.x << "</X><Y>" << v.y << "</Y></Vector2>\n";
    }
    void color3(const char* n, glm::vec3 c) {
        o << "<Color3 name=\"" << n << "\"><R>" << c.r << "</R><G>" << c.g << "</G><B>" << c.b << "</B></Color3>\n";
    }
    void color3u8(const char* n, glm::vec3 c) {
        auto b = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
        uint32_t v = 0xFF000000u | b(c.r) << 16 | b(c.g) << 8 | b(c.b);
        o << "<Color3uint8 name=\"" << n << "\">" << v << "</Color3uint8>\n";
    }
    void cframe(const char* n, glm::vec3 p, glm::mat3 r) {
        o << "<CoordinateFrame name=\"" << n << "\"><X>" << p.x << "</X><Y>" << p.y << "</Y><Z>" << p.z << "</Z>";
        const char* names[9] = {"R00", "R01", "R02", "R10", "R11", "R12", "R20", "R21", "R22"};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col) o << "<" << names[row * 3 + col] << ">" << r[col][row] << "</" << names[row * 3 + col] << ">";
        o << "</CoordinateFrame>\n";
    }
    void refProp(const char* n, uint64_t id) {
        o << "<Ref name=\"" << n << "\">" << (id ? ref(id) : std::string("null")) << "</Ref>\n";
    }
    void binary(const char* n, const std::string& data) {
        static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        size_t i = 0;
        for (; i + 2 < data.size(); i += 3) {
            uint32_t v = (uint8_t)data[i] << 16 | (uint8_t)data[i + 1] << 8 | (uint8_t)data[i + 2];
            out += chars[v >> 18]; out += chars[(v >> 12) & 63]; out += chars[(v >> 6) & 63]; out += chars[v & 63];
        }
        if (i < data.size()) {
            uint32_t v = (uint8_t)data[i] << 16 | (i + 1 < data.size() ? (uint8_t)data[i + 1] << 8 : 0);
            out += chars[v >> 18]; out += chars[(v >> 12) & 63];
            out += i + 1 < data.size() ? chars[(v >> 6) & 63] : '=';
            out += '=';
        }
        o << "<BinaryString name=\"" << n << "\">" << out << "</BinaryString>\n";
    }

    static std::string attributesBlob(const SceneNode& n) {
        std::string b;
        auto u32 = [&](uint32_t v) { b.append((const char*)&v, 4); };
        auto f32 = [&](float v) { b.append((const char*)&v, 4); };
        u32((uint32_t)n.attributes.size());
        for (const Attribute& a : n.attributes) {
            u32((uint32_t)a.name.size()); b += a.name;
            switch (a.type) {
                case Attribute::String:  b += (char)0x02; u32((uint32_t)a.s.size()); b += a.s; break;
                case Attribute::Bool:    b += (char)0x03; b += (char)(a.b ? 1 : 0); break;
                case Attribute::Number:  b += (char)0x06; b.append((const char*)&a.n, 8); break;
                case Attribute::Color3:  b += (char)0x0F; f32(a.v.x); f32(a.v.y); f32(a.v.z); break;
                case Attribute::Vector3: b += (char)0x11; f32(a.v.x); f32(a.v.y); f32(a.v.z); break;
            }
        }
        return b;
    }

    void common(const SceneNode& n) {
        str("Name", n.name);
        if (!n.attributes.empty()) binary("AttributesSerialize", attributesBlob(n));
        if (!n.tags.empty()) {
            std::string t;
            for (size_t i = 0; i < n.tags.size(); ++i) { if (i) t += '\0'; t += n.tags[i]; }
            binary("Tags", t);
        }
    }

    void item(const SceneNode& n) {
        if (n.internal || scene.isCharacterPart(&n) || scene.isProtected(&n)) return;
        const char* cls = nullptr;
        switch (n.kind) {
            case NodeKind::Part:       cls = n.name == "SpawnLocation" ? "SpawnLocation" : "Part"; break;
            case NodeKind::Model:      cls = "Model"; break;
            case NodeKind::Script:     cls = n.isModule ? "ModuleScript" : "Script"; break;
            case NodeKind::Light:      cls = n.lightType == LightType::Spot ? "SpotLight" : "PointLight"; break;
            case NodeKind::Sound:      cls = "Sound"; break;
            case NodeKind::Attachment: cls = "Attachment"; break;
            case NodeKind::ForceField: cls = "ForceField"; break;
            case NodeKind::Tool:       cls = "Tool"; break;
            case NodeKind::Decal:      cls = "Decal"; break;
            case NodeKind::Animation:  cls = nullptr; break;   // (Roblox keeps animations online)
            case NodeKind::FluidSystem: case NodeKind::FluidEmitter: cls = nullptr; break;   // (Guts&Bolts only)
            case NodeKind::Gui:        cls = kGuiClassNames[(int)n.gui.type]; break;
            case NodeKind::Value:
                cls = n.value.type == Attribute::Vector3 || n.value.type == Attribute::Color3 ? nullptr : n.valueClass();
                break;
            case NodeKind::Constraint: {
                static const char* names[] = {"RopeConstraint", "RodConstraint", "SpringConstraint", "WeldConstraint", "HingeConstraint"};
                cls = names[(int)n.constraintType];
                break;
            }
        }
        if (!cls) return;
        o << "<Item class=\"" << cls << "\" referent=\"" << ref(n.id) << "\">\n<Properties>\n";
        common(n);
        switch (n.kind) {
        case NodeKind::Gui: {
            const GuiProps& g = n.gui;
            if (g.type == GuiType::ScreenGui) { boolean("Enabled", n.enabled); o << "<int name=\"DisplayOrder\">" << g.displayOrder << "</int>\n"; boolean("ResetOnSpawn", false); break; }
            if (g.type == GuiType::UICorner) {
                o << "<UDim name=\"CornerRadius\"><S>" << g.corner.xs << "</S><O>" << (int)std::lround(g.corner.xo) << "</O></UDim>\n";
                break;
            }
            if (g.type == GuiType::UIStroke) { color3("Color", g.borderColor); flt("Thickness", g.thickness); flt("Transparency", g.bgTransparency); boolean("Enabled", n.enabled); break; }
            udim2("Position", g.pos);
            udim2("Size", g.size);
            vec2("AnchorPoint", g.anchor);
            color3("BackgroundColor3", g.bg);
            flt("BackgroundTransparency", g.bgTransparency);
            color3("BorderColor3", g.borderColor);
            o << "<int name=\"BorderSizePixel\">" << g.border << "</int>\n";
            o << "<int name=\"ZIndex\">" << g.zIndex << "</int>\n";
            boolean("ClipsDescendants", g.clips);
            boolean("Visible", n.visible);
            if (g.type == GuiType::TextLabel || g.type == GuiType::TextButton) {
                str("Text", g.text);
                color3("TextColor3", g.textColor);
                flt("TextSize", g.textSize);
                boolean("TextScaled", g.textScaled);
                boolean("TextWrapped", g.textWrapped);
                token("TextXAlignment", g.xAlign == 0 ? 0 : g.xAlign == 2 ? 1 : 2);
                token("TextYAlignment", g.yAlign);
                flt("TextTransparency", g.textTransparency);
                color3("TextStrokeColor3", g.strokeColor);
                flt("TextStrokeTransparency", g.strokeTransparency);
                token("Font", g.bold ? 4 : 3);   // SourceSansBold / SourceSans
            }
            if (g.type == GuiType::ImageLabel || g.type == GuiType::ImageButton) {
                o << "<Content name=\"Image\"><url>" << esc(g.image) << "</url></Content>\n";
                color3("ImageColor3", g.imageColor);
                flt("ImageTransparency", g.imageTransparency);
            }
            if (g.type == GuiType::TextButton || g.type == GuiType::ImageButton) boolean("AutoButtonColor", g.autoButtonColor);
            break;
        }
        case NodeKind::Decal:
            o << "<Content name=\"Texture\"><url>" << n.texture << "</url></Content>\n";
            o << "<token name=\"Face\">" << (int)n.face << "</token>\n";
            flt("Transparency", n.transparency);
            break;
        case NodeKind::Value:
            if (n.value.type == Attribute::String) str("Value", n.value.s);
            else if (n.value.type == Attribute::Bool) boolean("Value", n.value.b);
            else if (n.intValue) o << "<int64 name=\"Value\">" << (long long)n.value.n << "</int64>\n";
            else o << "<double name=\"Value\">" << n.value.n << "</double>\n";
            break;
        case NodeKind::Part: {
            glm::mat4 w = n.worldMatrix();
            glm::vec3 size(glm::length(glm::vec3(w[0])), glm::length(glm::vec3(w[1])), glm::length(glm::vec3(w[2])));
            glm::mat3 rot(glm::vec3(w[0]) / std::max(size.x, 1e-6f), glm::vec3(w[1]) / std::max(size.y, 1e-6f),
                          glm::vec3(w[2]) / std::max(size.z, 1e-6f));
            int shape = 1;
            if (n.primitiveType == PrimitiveType::Sphere) shape = 0;
            if (n.primitiveType == PrimitiveType::Cylinder) {
                shape = 2;
                rot = rot * glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1)));
                size = glm::vec3(size.y, size.x, size.z);
            }
            if (n.primitiveType == PrimitiveType::Plane) size.y = std::max(size.y, 0.05f);
            cframe("CFrame", glm::vec3(w[3]) * kExportScale, rot);
            vec3("size", size * kExportScale);
            color3u8("Color3uint8", n.color);
            flt("Transparency", n.transparency);
            boolean("Anchored", n.anchored);
            boolean("CanCollide", n.canCollide);
            boolean("CastShadow", n.castShadow);
            boolean("Locked", n.locked);
            token("Material", materialToRoblox(n.material));
            token("shape", shape);
            token("TopSurface", 0);
            token("BottomSurface", 0);
            break;
        }
        case NodeKind::Script:
            boolean("Disabled", !n.enabled);
            {
                // ]]> can't appear inside CDATA: split it.
                std::string src = n.source;
                std::string safe;
                for (size_t i = 0; i < src.size(); ++i) {
                    if (src.compare(i, 3, "]]>") == 0) { safe += "]]]]><![CDATA[>"; i += 2; }
                    else safe += src[i];
                }
                o << "<ProtectedString name=\"Source\"><![CDATA[" << safe << "]]></ProtectedString>\n";
            }
            break;
        case NodeKind::Light:
            color3("Color", n.color);
            flt("Brightness", n.brightness / 2.0f);
            flt("Range", n.range * kExportScale);
            flt("Angle", n.spotAngle);
            boolean("Enabled", n.enabled);
            break;
        case NodeKind::Sound:
            o << "<Content name=\"SoundId\"><url>" << esc(n.soundId) << "</url></Content>\n";
            flt("Volume", n.volume);
            flt("PlaybackSpeed", n.pitch);
            boolean("Looped", n.looped);
            boolean("Playing", n.autoplay);
            break;
        case NodeKind::Attachment: {
            glm::vec3 psize = n.parent ? n.parent->transform.scale : glm::vec3(1.0f);
            glm::mat4 r = glm::eulerAngleZYX(glm::radians(n.transform.rotation.z), glm::radians(n.transform.rotation.y),
                                             glm::radians(n.transform.rotation.x));
            cframe("CFrame", n.transform.position * psize * kExportScale, glm::mat3(r));
            break;
        }
        case NodeKind::Constraint:
            if (n.constraintType == ConstraintType::Weld) { refProp("Part0", n.ref0); refProp("Part1", n.ref1); }
            else { refProp("Attachment0", n.ref0); refProp("Attachment1", n.ref1); }
            if (n.constraintType == ConstraintType::Rope || n.constraintType == ConstraintType::Rod)
                flt("Length", std::max(0.0f, n.length) * kExportScale);
            if (n.constraintType == ConstraintType::Spring) {
                flt("FreeLength", std::max(0.0f, n.length) * kExportScale);
                flt("Stiffness", n.stiffness);
                flt("Damping", n.damping);
            }
            if (n.constraintType == ConstraintType::Hinge && n.motorTorque > 0.0f) {
                token("ActuatorType", 1);
                flt("AngularVelocity", n.motorSpeed);
                flt("MotorMaxTorque", n.motorTorque);
            }
            flt("Thickness", n.thickness * kExportScale);
            boolean("Visible", n.visible);
            boolean("Enabled", n.enabled);
            break;
        default: break;
        }
        o << "</Properties>\n";
        for (auto& c : n.children) item(*c);
        o << "</Item>\n";
    }

    void begin() {
        o << "<roblox xmlns:xmime=\"http://www.w3.org/2005/05/xmlmime\" "
             "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
             "xsi:noNamespaceSchemaLocation=\"http://www.roblox.com/roblox.xsd\" version=\"4\">\n"
             "<External>null</External>\n<External>nil</External>\n";
    }
    void end() { o << "</roblox>\n"; }
};

bool writeText(const std::string& path, const std::string& text, std::string& error) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { error = "couldn't write " + path; return false; }
    f << text;
    return true;
}

} // namespace

// ===========================================================================

std::string Report::summary() const {
    std::string s = std::to_string(parts) + " parts, " + std::to_string(models) + " models, " +
                    std::to_string(scripts) + " scripts";
    if (lights) s += ", " + std::to_string(lights) + " lights";
    if (sounds) s += ", " + std::to_string(sounds) + " sounds";
    if (constraints) s += ", " + std::to_string(constraints) + " attachments/constraints";
    return s;
}

bool isRobloxFile(const std::string& path) {
    return endsWith(path, ".rbxl") || endsWith(path, ".rbxlx") || endsWith(path, ".rbxm") || endsWith(path, ".rbxmx");
}
bool isPlace(const std::string& path) { return endsWith(path, ".rbxl") || endsWith(path, ".rbxlx"); }

bool importPlace(Scene& scene, const std::string& path, Report& report, std::string& error) {
    Document doc;
    if (!readFile(path, doc)) { error = doc.error; return false; }
    const Inst* ws = findChild(doc.roots, "Workspace");
    if (!ws) { error = "this file has no Workspace (is it a model? use Insert from File)"; return false; }

    // Start from an empty world (keeping the player).
    scene.buildDefault();
    std::vector<SceneNode*> old;
    for (auto& c : scene.root()->children) if (!scene.isProtected(c.get())) old.push_back(c.get());
    for (SceneNode* n : old) scene.removeNode(n);

    Converter conv{scene, report, {}};
    for (const Inst* k : ws->children) conv.convert(*k, nullptr);
    // Scripts that run on the server live in ServerScriptService in Roblox.
    for (const char* svc : {"StarterGui", "ServerScriptService", "ReplicatedStorage", "ServerStorage", "StarterPlayer", "ReplicatedFirst"}) {
        const Inst* s = findChild(doc.roots, svc);
        if (!s || s->children.empty()) continue;
        auto folder = std::make_unique<SceneNode>(svc, NodeKind::Model);
        bool storage = std::string(svc) == "ReplicatedStorage" || std::string(svc) == "ServerStorage";
        if (storage) folder->visible = false;   // templates: hidden, not in the world
        SceneNode* f = scene.insert(std::move(folder));
        conv.made[s] = f;
        for (const Inst* k : s->children) conv.convert(*k, f);
    }
    conv.linkRefs(doc);

    // Workspace + Lighting settings.
    double g = ws->num("Gravity", kRobloxGravity);
    scene.world().gravity = (float)(22.0 * g / kRobloxGravity);
    if (const Inst* light = findChild(doc.roots, "Lighting")) {
        Environment& env = scene.environment();
        EnvironmentPresets::applyTimeOfDay(env, (float)light->num("ClockTime", 14.0));
        env.clockTime = (float)light->num("ClockTime", 14.0);
        double fogEnd = light->num("FogEnd", 100000.0);
        if (fogEnd < 5000.0) {
            env.fogEnabled = true;
            env.fogDensity = (float)std::clamp(2.0 / (fogEnd * kImportScale), 0.0005, 0.2);
            if (const Value* c = light->get("FogColor")) env.fogColor = c->v;
        }
        env.shadows = light->flag("GlobalShadows", true);
        if (const Value* a = light->get("AttributesSerialize"); a && a->kind == Value::Str) {
            SceneNode tmp("Lighting", NodeKind::Model);
            readAttributes(a->s, tmp);
            if (const Attribute* saved = tmp.findAttribute("GutsAndBoltsLighting"); saved && saved->type == Attribute::String)
                Serializer::environmentFromString(env, saved->s);   // made by Guts and Bolts: exact lighting
        }
    }
    scene.info().title = std::filesystem::path(path).stem().string();
    scene.markDirty();
    return true;
}

std::vector<SceneNode*> importModel(Scene& scene, SceneNode* parent, const std::string& path, Report& report,
                                    std::string& error) {
    std::vector<SceneNode*> out;
    Document doc;
    if (!readFile(path, doc)) { error = doc.error; return out; }
    std::vector<Inst*> roots = doc.roots;
    if (const Inst* ws = findChild(doc.roots, "Workspace")) roots = ws->children;   // a place: take its Workspace
    Converter conv{scene, report, {}};
    for (const Inst* r : roots)
        if (SceneNode* n = conv.convert(*r, parent)) out.push_back(n);
    conv.linkRefs(doc);
    scene.markDirty();
    if (out.empty()) error = "nothing in this file could be imported";
    return out;
}

bool exportPlace(Scene& scene, const std::string& path, std::string& error) {
    XmlWriter w(scene);
    w.begin();
    w.o << "<Item class=\"Workspace\" referent=\"RBXWORKSPACE\">\n<Properties>\n";
    w.str("Name", "Workspace");
    w.flt("Gravity", scene.world().gravity / 22.0f * kRobloxGravity);
    w.o << "</Properties>\n";
    const SceneNode* ui = scene.root()->findChild("StarterGui");
    if (ui && ui->kind != NodeKind::Model) ui = nullptr;
    for (auto& c : scene.root()->children) if (c.get() != ui) w.item(*c);
    w.o << "</Item>\n";
    if (ui) {   // our StarterGui folder is Roblox's StarterGui service
        w.o << "<Item class=\"StarterGui\" referent=\"RBXSTARTERGUI\">\n<Properties>\n";
        w.str("Name", "StarterGui");
        w.o << "</Properties>\n";
        for (auto& c : ui->children) w.item(*c);
        w.o << "</Item>\n";
    }
    w.o << "<Item class=\"Lighting\" referent=\"RBXLIGHTING\">\n<Properties>\n";
    w.str("Name", "Lighting");
    {
        // Our full lighting setup rides along as an attribute, so a game that
        // goes to Roblox and back keeps its exact look.
        SceneNode holder("Lighting", NodeKind::Model);
        Attribute a;
        a.name = "GutsAndBoltsLighting";
        a.type = Attribute::String;
        a.s = Serializer::environmentToString(scene.environment());
        holder.attributes.push_back(a);
        w.binary("AttributesSerialize", XmlWriter::attributesBlob(holder));
    }
    w.flt("ClockTime", scene.environment().clockTime);
    w.boolean("GlobalShadows", scene.environment().shadows);
    w.o << "</Properties>\n</Item>\n";
    w.end();
    return writeText(path, w.o.str(), error);
}

bool exportModel(Scene& scene, const std::vector<SceneNode*>& nodes, const std::string& path, std::string& error) {
    XmlWriter w(scene);
    w.begin();
    for (SceneNode* n : nodes) w.item(*n);
    w.end();
    return writeText(path, w.o.str(), error);
}

} // namespace RobloxFile
