#include "Csg.h"

#include <algorithm>
#include <memory>

namespace Csg {

namespace {

constexpr double kEps = 1e-5;   // how close to a plane counts as "on" it

struct Plane {
    glm::dvec3 n{0.0};
    double     w = 0.0;
    bool       ok = false;
    static Plane from(const Polygon& p) {
        Plane pl;
        // Newell's method: a steady normal even for long thin polygons.
        glm::dvec3 n(0.0);
        for (size_t i = 0; i < p.verts.size(); ++i) {
            const glm::dvec3& a = p.verts[i];
            const glm::dvec3& b = p.verts[(i + 1) % p.verts.size()];
            n.x += (a.y - b.y) * (a.z + b.z);
            n.y += (a.z - b.z) * (a.x + b.x);
            n.z += (a.x - b.x) * (a.y + b.y);
        }
        double len = glm::length(n);
        if (len < 1e-12) return pl;
        pl.n = n / len;
        pl.w = glm::dot(pl.n, p.verts[0]);
        pl.ok = true;
        return pl;
    }
    void flip() { n = -n; w = -w; }
};

struct Poly {
    Polygon poly;
    Plane   plane;
    void flip() {
        std::reverse(poly.verts.begin(), poly.verts.end());
        plane.flip();
    }
};

// Sort `p` against `plane`: whole into one of the lists, or cut in two.
void split(const Plane& plane, const Poly& p, std::vector<Poly>& coFront, std::vector<Poly>& coBack,
           std::vector<Poly>& front, std::vector<Poly>& back) {
    enum { Coplanar = 0, Front = 1, Back = 2, Spanning = 3 };
    int type = 0;
    std::vector<int> types(p.poly.verts.size());
    for (size_t i = 0; i < p.poly.verts.size(); ++i) {
        double t = glm::dot(plane.n, p.poly.verts[i]) - plane.w;
        types[i] = t < -kEps ? Back : t > kEps ? Front : Coplanar;
        type |= types[i];
    }
    switch (type) {
        case Coplanar: (glm::dot(plane.n, p.plane.n) > 0 ? coFront : coBack).push_back(p); break;
        case Front: front.push_back(p); break;
        case Back: back.push_back(p); break;
        default: {
            Poly f, b;
            f.plane = b.plane = p.plane;
            const auto& v = p.poly.verts;
            for (size_t i = 0; i < v.size(); ++i) {
                size_t j = (i + 1) % v.size();
                int ti = types[i], tj = types[j];
                if (ti != Back) f.poly.verts.push_back(v[i]);
                if (ti != Front) b.poly.verts.push_back(v[i]);
                if ((ti | tj) == Spanning) {
                    double t = (plane.w - glm::dot(plane.n, v[i])) / glm::dot(plane.n, v[j] - v[i]);
                    glm::dvec3 m = v[i] + (v[j] - v[i]) * t;
                    f.poly.verts.push_back(m);
                    b.poly.verts.push_back(m);
                }
            }
            if (f.poly.verts.size() >= 3) front.push_back(std::move(f));
            if (b.poly.verts.size() >= 3) back.push_back(std::move(b));
        }
    }
}

// A BSP tree of polygons.
struct Node {
    Plane                 plane;
    std::unique_ptr<Node> front, back;
    std::vector<Poly>     polys;

    void invert() {
        for (Poly& p : polys) p.flip();
        plane.flip();
        if (front) front->invert();
        if (back) back->invert();
        std::swap(front, back);
    }
    // Remove the parts of `list` that are inside this solid.
    std::vector<Poly> clip(const std::vector<Poly>& list) const {
        if (!plane.ok) return list;
        std::vector<Poly> f, b;
        for (const Poly& p : list) split(plane, p, f, b, f, b);
        f = front ? front->clip(f) : f;
        b = back ? back->clip(b) : std::vector<Poly>();
        f.insert(f.end(), b.begin(), b.end());
        return f;
    }
    void clipTo(const Node& other) {
        polys = other.clip(polys);
        if (front) front->clipTo(other);
        if (back) back->clipTo(other);
    }
    void all(std::vector<Poly>& out) const {
        out.insert(out.end(), polys.begin(), polys.end());
        if (front) front->all(out);
        if (back) back->all(out);
    }
    void build(const std::vector<Poly>& list) {
        if (list.empty()) return;
        if (!plane.ok) plane = list[0].plane;
        std::vector<Poly> f, b;
        for (const Poly& p : list) split(plane, p, polys, polys, f, b);
        if (!f.empty()) { if (!front) front = std::make_unique<Node>(); front->build(f); }
        if (!b.empty()) { if (!back) back = std::make_unique<Node>(); back->build(b); }
    }
};

std::vector<Poly> toPolys(const Solid& s) {
    std::vector<Poly> out;
    for (const Polygon& p : s) {
        if (p.verts.size() < 3) continue;
        Poly q{p, Plane::from(p)};
        if (q.plane.ok) out.push_back(std::move(q));
    }
    return out;
}

Solid toSolid(const Node& n) {
    std::vector<Poly> all;
    n.all(all);
    Solid s;
    for (Poly& p : all) s.push_back(std::move(p.poly));
    return s;
}

} // namespace

Solid unite(const Solid& sa, const Solid& sb) {
    Node a, b;
    a.build(toPolys(sa));
    b.build(toPolys(sb));
    a.clipTo(b);
    b.clipTo(a);
    b.invert(); b.clipTo(a); b.invert();
    std::vector<Poly> rest;
    b.all(rest);
    a.build(rest);
    return toSolid(a);
}

Solid subtract(const Solid& sa, const Solid& sb) {
    Node a, b;
    a.build(toPolys(sa));
    b.build(toPolys(sb));
    a.invert();
    a.clipTo(b);
    b.clipTo(a);
    b.invert(); b.clipTo(a); b.invert();
    std::vector<Poly> rest;
    b.all(rest);
    a.build(rest);
    a.invert();
    return toSolid(a);
}

Solid intersect(const Solid& sa, const Solid& sb) {
    Node a, b;
    a.build(toPolys(sa));
    b.build(toPolys(sb));
    a.invert();
    b.clipTo(a);
    b.invert();
    a.clipTo(b);
    b.clipTo(a);
    std::vector<Poly> rest;
    b.all(rest);
    a.build(rest);
    a.invert();
    return toSolid(a);
}

} // namespace Csg
