// Rigid-body physics for unanchored parts, plus constraints (ropes, rods,
// springs, welds, hinges with motors).
//
// How it works, in short:
//  * every unanchored part becomes a "body" with mass, velocity and spin;
//    anchored parts are bodies that never move
//  * boxes and balls that overlap produce contact points
//  * a "sequential impulse" solver nudges velocities, over and over, until
//    contacts stop pushing into each other and constraints are satisfied
//  * finally positions and rotations move by the velocities
#include "Physics.h"
#include "Scene.h"
#include "SceneNode.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>

namespace {

struct MaterialPhysics { float density, friction, elasticity; };

MaterialPhysics materialPhysics(Material m) {
    switch (m) {
        case Material::Metal:    return {3.0f, 0.35f, 0.15f};
        case Material::Wood:     return {0.7f, 0.50f, 0.25f};
        case Material::Glass:    return {2.5f, 0.25f, 0.20f};
        case Material::Concrete: return {2.4f, 0.70f, 0.10f};
        case Material::Ice:      return {0.9f, 0.03f, 0.10f};
        case Material::Neon:     return {1.0f, 0.40f, 0.20f};
        default:                 return {1.0f, 0.50f, 0.30f};   // Plastic
    }
}

struct Body {
    SceneNode* node = nullptr;
    bool       dynamic = false;
    bool       sphere = false;
    bool       solid = true;
    glm::vec3  pos{0.0f};
    glm::quat  rot{1, 0, 0, 0};
    glm::vec3  half{0.5f};
    glm::vec3  scale{1.0f};
    float      radius = 0.5f;
    glm::vec3  v{0.0f}, w{0.0f};
    float      invMass = 0.0f;
    glm::vec3  invInertiaLocal{0.0f};
    float      friction = 0.5f, elasticity = 0.3f;
    bool       awake = true;
    glm::vec3  aabbMin{0.0f}, aabbMax{0.0f};

    glm::mat3 R() const { return glm::mat3_cast(rot); }
    glm::mat3 invInertia() const {
        glm::mat3 r = R();
        return r * glm::mat3(glm::vec3(invInertiaLocal.x, 0, 0), glm::vec3(0, invInertiaLocal.y, 0),
                             glm::vec3(0, 0, invInertiaLocal.z)) * glm::transpose(r);
    }
    OBB obb() const {
        OBB o;
        glm::mat3 r = R();
        o.center = pos;
        o.axis[0] = r[0]; o.axis[1] = r[1]; o.axis[2] = r[2];
        o.half = half;
        return o;
    }
    glm::vec3 velocityAt(const glm::vec3& p) const { return v + glm::cross(w, p - pos); }
};

struct Contact {
    int a, b;               // a is pushed along +n
    glm::vec3 p, n;
    float depth;
    glm::vec3 ra, rb, t1, t2;
    float kN, kT1, kT2, bias, jn = 0, jt1 = 0, jt2 = 0, mu;
};

void applyImpulse(Body& b, const glm::vec3& r, const glm::vec3& j) {
    if (!b.dynamic) return;
    b.v += j * b.invMass;
    b.w += b.invInertia() * glm::cross(r, j);
}

float effMass(const Body& a, const Body& b, const glm::vec3& ra, const glm::vec3& rb, const glm::vec3& d) {
    float k = a.invMass + b.invMass;
    if (a.dynamic) k += glm::dot(d, glm::cross(a.invInertia() * glm::cross(ra, d), ra));
    if (b.dynamic) k += glm::dot(d, glm::cross(b.invInertia() * glm::cross(rb, d), rb));
    return k > 1e-9f ? k : 1e-9f;
}

// --- contact generation -------------------------------------------------------

void boxCorners(const OBB& o, glm::vec3 out[8]) {
    for (int i = 0; i < 8; ++i)
        out[i] = o.center + o.axis[0] * o.half.x * ((i & 1) ? 1.f : -1.f) +
                 o.axis[1] * o.half.y * ((i & 2) ? 1.f : -1.f) + o.axis[2] * o.half.z * ((i & 4) ? 1.f : -1.f);
}

bool inside(const OBB& o, const glm::vec3& p, float tol) {
    glm::vec3 d = p - o.center;
    for (int i = 0; i < 3; ++i)
        if (std::abs(glm::dot(d, o.axis[i])) > o.half[i] + tol) return false;
    return true;
}

glm::vec3 support(const OBB& o, const glm::vec3& dir) {
    glm::vec3 p = o.center;
    for (int i = 0; i < 3; ++i) p += o.axis[i] * o.half[i] * (glm::dot(dir, o.axis[i]) >= 0 ? 1.f : -1.f);
    return p;
}

void boxBox(int ia, int ib, const Body& A, const Body& B, std::vector<Contact>& out) {
    OBB a = A.obb(), b = B.obb();
    glm::vec3 n; float depth;
    if (!Physics::obbOverlap(a, b, n, depth)) return;
    glm::vec3 ca[8], cb[8];
    boxCorners(a, ca);
    boxCorners(b, cb);
    size_t before = out.size();
    for (auto& p : ca)
        if (inside(b, p, 0.02f)) out.push_back({ia, ib, p, n, depth});
    for (auto& p : cb)
        if (inside(a, p, 0.02f)) out.push_back({ia, ib, p, n, depth});
    if (out.size() == before) {                     // edge against edge
        glm::vec3 p = (support(a, -n) + support(b, n)) * 0.5f;
        out.push_back({ia, ib, p, n, depth});
    }
}

void sphereBox(int is, int ib, const Body& S, const Body& B, std::vector<Contact>& out, bool sphereIsA) {
    OBB o = B.obb();
    glm::vec3 rel = S.pos - o.center;
    glm::vec3 local(glm::dot(rel, o.axis[0]), glm::dot(rel, o.axis[1]), glm::dot(rel, o.axis[2]));
    glm::vec3 cl = glm::clamp(local, -o.half, o.half);
    glm::vec3 closest = o.center + o.axis[0] * cl.x + o.axis[1] * cl.y + o.axis[2] * cl.z;
    glm::vec3 d = S.pos - closest;
    float dist = glm::length(d);
    glm::vec3 n; float depth;
    if (dist > 1e-5f) {
        if (dist >= S.radius) return;
        n = d / dist;
        depth = S.radius - dist;
    } else {
        glm::vec3 room = o.half - glm::abs(local);
        int ax = room.x < room.y ? (room.x < room.z ? 0 : 2) : (room.y < room.z ? 1 : 2);
        n = o.axis[ax] * (local[ax] >= 0 ? 1.f : -1.f);
        depth = room[ax] + S.radius;
    }
    if (sphereIsA) out.push_back({is, ib, closest, n, depth});
    else           out.push_back({ib, is, closest, -n, depth});
}

void sphereSphere(int ia, int ib, const Body& A, const Body& B, std::vector<Contact>& out) {
    glm::vec3 d = A.pos - B.pos;
    float dist = glm::length(d);
    if (dist >= A.radius + B.radius) return;
    glm::vec3 n = dist > 1e-5f ? d / dist : glm::vec3(0, 1, 0);
    out.push_back({ia, ib, B.pos + n * B.radius, n, A.radius + B.radius - dist});
}

// --- building bodies from the scene ---------------------------------------------

void localBoundsFor(PrimitiveType t, glm::vec3& mn, glm::vec3& mx) {
    if (t == PrimitiveType::Plane) { mn = {-0.5f, -0.02f, -0.5f}; mx = {0.5f, 0.02f, 0.5f}; }
    else                           { mn = glm::vec3(-0.5f); mx = glm::vec3(0.5f); }
}

Body makeBody(SceneNode* n, bool dynamic) {
    Body b;
    b.node = n;
    b.dynamic = dynamic;
    b.solid = n->canCollide;
    glm::mat4 m = n->worldMatrix();
    glm::vec3 mn, mx;
    localBoundsFor(n->primitiveType, mn, mx);
    glm::mat3 r;
    for (int i = 0; i < 3; ++i) {
        glm::vec3 c(m[i]);
        float len = glm::length(c);
        b.scale[i] = len;
        r[i] = len > 1e-8f ? c / len : glm::vec3(i == 0, i == 1, i == 2);
    }
    b.pos = glm::vec3(m[3]);
    b.rot = glm::normalize(glm::quat_cast(r));
    b.half = b.scale * (mx - mn) * 0.5f;
    b.sphere = n->primitiveType == PrimitiveType::Sphere;
    b.radius = b.sphere ? (b.half.x + b.half.y + b.half.z) / 3.0f : 0.0f;

    MaterialPhysics mp = materialPhysics(n->material);
    b.friction   = n->friction   >= 0 ? n->friction   : mp.friction;
    b.elasticity = n->elasticity >= 0 ? n->elasticity : mp.elasticity;
    if (dynamic) {
        float density = n->density >= 0 ? std::max(0.01f, n->density) : mp.density;
        glm::vec3 e = b.half * 2.0f;
        float mass, ix, iy, iz;
        if (b.sphere) {
            mass = density * 4.18879f * b.radius * b.radius * b.radius;
            ix = iy = iz = 0.4f * mass * b.radius * b.radius;
        } else {
            mass = density * std::max(1e-4f, e.x * e.y * e.z);
            ix = mass / 12.0f * (e.y * e.y + e.z * e.z);
            iy = mass / 12.0f * (e.x * e.x + e.z * e.z);
            iz = mass / 12.0f * (e.x * e.x + e.y * e.y);
        }
        b.invMass = 1.0f / mass;
        b.invInertiaLocal = {1.0f / std::max(ix, 1e-6f), 1.0f / std::max(iy, 1e-6f), 1.0f / std::max(iz, 1e-6f)};
        b.v = n->velocity;
        b.w = n->angularVelocity;
        b.awake = n->sleepTime < 0.6f;
    }
    // World-space bounding box for the broad phase.
    OBB o = b.obb();
    glm::vec3 ext(0.0f);
    for (int i = 0; i < 3; ++i) ext += glm::abs(o.axis[i]) * o.half[i];
    if (b.sphere) ext = glm::vec3(b.radius);
    b.aabbMin = b.pos - ext;
    b.aabbMax = b.pos + ext;
    return b;
}

void writeBack(Body& b) {
    SceneNode* n = b.node;
    n->velocity = b.v;
    n->angularVelocity = b.w;
    glm::mat4 world = glm::translate(glm::mat4(1.0f), b.pos) * glm::mat4_cast(b.rot);
    glm::mat4 local = n->parent ? glm::inverse(n->parent->worldMatrix()) * world : world;
    glm::mat3 r(local);
    for (int i = 0; i < 3; ++i) r[i] = glm::normalize(r[i]);
    float z, y, x;
    glm::extractEulerAngleZYX(glm::mat4(r), z, y, x);
    n->transform.position = glm::vec3(local[3]);
    n->transform.rotation = glm::degrees(glm::vec3(x, y, z));
}

bool hasDynamicAncestorPart(const SceneNode* n) {
    for (const SceneNode* p = n->parent; p; p = p->parent)
        if (p->isPart() && !p->anchored) return true;
    return false;
}

// --- joints ------------------------------------------------------------------------

struct Joint {
    SceneNode*     node;
    ConstraintType type;
    int            a = -1, b = -1;          // bodies
    glm::vec3      la{0.0f}, lb{0.0f};      // anchors in each body's own space
    glm::vec3      axisA{1, 0, 0}, axisB{1, 0, 0};   // hinge axes in body space
    glm::quat      rel{1, 0, 0, 0};         // weld: B's rotation relative to A
    float          length = 0.0f;
    float          accMotor = 0.0f;
};

} // namespace

// ============================================================================

void Physics::stepParts(Scene& scene, float dt, std::vector<uint64_t>& fallen) {
    // ---- 1. Bodies ------------------------------------------------------------
    std::vector<Body> bodies;
    std::unordered_map<uint64_t, int> index;
    std::vector<SceneNode*> constraintNodes;
    std::vector<SceneNode*> stack{scene.root()};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        // "Visible" only hides drawing for constraints; hidden parts are switched off.
        if (n->isConstraint()) { if (n->enabled) constraintNodes.push_back(n); continue; }
        if (!n->visible) continue;
        if (scene.isCharacterPart(n)) continue;
        for (auto& c : n->children) stack.push_back(c.get());
        if (!n->isPart() || n->internal) continue;
        bool dynamic = !n->anchored && !hasDynamicAncestorPart(n);
        if (!dynamic && !n->canCollide) {
            // Only matters as an anchor for constraints.
            bool used = false;
            for (auto& c : n->children) if (c->isAttachment()) used = true;
            if (!used) continue;
        }
        index[n->id] = (int)bodies.size();
        bodies.push_back(makeBody(n, dynamic));
    }
    bool anyDynamic = false;
    for (auto& b : bodies) if (b.dynamic) { anyDynamic = true; break; }
    if (!anyDynamic) return;

    // ---- 2. Joints --------------------------------------------------------------
    auto bodyOfAttachment = [&](uint64_t attId, glm::vec3& worldPos, glm::vec3& worldAxis) -> int {
        SceneNode* att = scene.findById(attId);
        if (!att || !att->parent) return -1;
        glm::mat4 m = att->worldMatrix();
        worldPos = glm::vec3(m[3]);
        worldAxis = glm::normalize(glm::vec3(m[0]) + glm::vec3(1e-9f));
        auto it = index.find(att->parent->id);
        return it == index.end() ? -1 : it->second;
    };
    std::vector<Joint> joints;
    std::set<std::pair<int, int>> noCollide;
    for (SceneNode* cn : constraintNodes) {
        Joint j;
        j.node = cn;
        j.type = cn->constraintType;
        glm::vec3 pa, pb, axa, axb;
        if (j.type == ConstraintType::Weld) {
            auto ia = index.find(cn->ref0), ib = index.find(cn->ref1);
            if (ia == index.end() || ib == index.end() || ia->second == ib->second) continue;
            j.a = ia->second; j.b = ib->second;
            Body &A = bodies[j.a], &B = bodies[j.b];
            if (!cn->jointReady) {
                // Remember how the two parts sit relative to each other right now.
                glm::vec3 mid = (A.pos + B.pos) * 0.5f;
                cn->jointA = glm::inverse(A.rot) * (mid - A.pos);
                cn->jointB = glm::inverse(B.rot) * (mid - B.pos);
                glm::quat q = glm::inverse(A.rot) * B.rot;
                cn->jointRot = {q.x, q.y, q.z, q.w};
                cn->jointReady = true;
            }
            j.la = cn->jointA; j.lb = cn->jointB;
            j.rel = glm::quat(cn->jointRot.w, cn->jointRot.x, cn->jointRot.y, cn->jointRot.z);
        } else {
            j.a = bodyOfAttachment(cn->ref0, pa, axa);
            j.b = bodyOfAttachment(cn->ref1, pb, axb);
            if (j.a < 0 || j.b < 0 || j.a == j.b) continue;
            Body &A = bodies[j.a], &B = bodies[j.b];
            j.la = glm::inverse(A.rot) * (pa - A.pos);
            j.lb = glm::inverse(B.rot) * (pb - B.pos);
            j.axisA = glm::inverse(A.rot) * axa;
            j.axisB = glm::inverse(B.rot) * axb;
            if (cn->length < 0.0f) cn->length = glm::length(pb - pa);   // "as placed"
            j.length = cn->length;
        }
        if (!bodies[j.a].dynamic && !bodies[j.b].dynamic) continue;
        if (j.type == ConstraintType::Weld || j.type == ConstraintType::Hinge)
            noCollide.insert({std::min(j.a, j.b), std::max(j.a, j.b)});
        // A running motor keeps things awake; a connected body wakes its partner.
        bool motorOn = j.type == ConstraintType::Hinge && cn->motorTorque > 0.0f && cn->motorSpeed != 0.0f;
        if (motorOn || bodies[j.a].awake || bodies[j.b].awake) {
            bodies[j.a].awake = bodies[j.b].awake = true;
        }
        joints.push_back(j);
    }

    const float g = scene.world().gravity;
    const int   substeps = 2;
    const float h = dt / substeps;

    for (int step = 0; step < substeps; ++step) {
        // ---- 3. Gravity + springs ----------------------------------------------
        for (auto& b : bodies)
            if (b.dynamic && b.awake) {
                b.v.y -= g * h;
                b.w *= std::max(0.0f, 1.0f - 0.05f * h);        // a little rolling resistance
            }
        for (auto& j : joints) {
            if (j.type != ConstraintType::Spring) continue;
            Body &A = bodies[j.a], &B = bodies[j.b];
            glm::vec3 ra = A.rot * j.la, rb = B.rot * j.lb;
            glm::vec3 d = (B.pos + rb) - (A.pos + ra);
            float len = glm::length(d);
            if (len < 1e-5f) continue;
            glm::vec3 dir = d / len;
            float relV = glm::dot(B.velocityAt(B.pos + rb) - A.velocityAt(A.pos + ra), dir);
            float f = j.node->stiffness * (len - j.length) + j.node->damping * relV;
            glm::vec3 imp = dir * f * h;
            applyImpulse(A, ra, imp);
            applyImpulse(B, rb, -imp);
        }

        // ---- 4. Contacts ----------------------------------------------------------
        std::vector<Contact> contacts;
        for (int i = 0; i < (int)bodies.size(); ++i) {
            if (!bodies[i].dynamic || !bodies[i].solid) continue;
            for (int k = 0; k < (int)bodies.size(); ++k) {
                if (k == i || !bodies[k].solid) continue;
                if (bodies[k].dynamic && k < i) continue;              // each dynamic pair once
                const Body &A = bodies[i], &B = bodies[k];
                if (!A.awake && (!B.dynamic || !B.awake)) continue;
                if (A.aabbMax.x < B.aabbMin.x || A.aabbMin.x > B.aabbMax.x || A.aabbMax.y < B.aabbMin.y ||
                    A.aabbMin.y > B.aabbMax.y || A.aabbMax.z < B.aabbMin.z || A.aabbMin.z > B.aabbMax.z)
                    continue;
                if (noCollide.count({std::min(i, k), std::max(i, k)})) continue;
                if (A.sphere && B.sphere)      sphereSphere(i, k, A, B, contacts);
                else if (A.sphere)             sphereBox(i, k, A, B, contacts, true);
                else if (B.sphere)             sphereBox(k, i, B, A, contacts, false);
                else                           boxBox(i, k, A, B, contacts);
            }
        }
        for (auto& c : contacts) {
            Body &A = bodies[c.a], &B = bodies[c.b];
            if (B.dynamic && !B.awake && A.awake) B.awake = true;   // bumped: wake up
            if (A.dynamic && !A.awake && B.awake && B.dynamic) A.awake = true;
            c.ra = c.p - A.pos;
            c.rb = c.p - B.pos;
            c.t1 = std::abs(c.n.x) < 0.9f ? glm::normalize(glm::cross(c.n, glm::vec3(1, 0, 0)))
                                          : glm::normalize(glm::cross(c.n, glm::vec3(0, 1, 0)));
            c.t2 = glm::cross(c.n, c.t1);
            c.kN  = effMass(A, B, c.ra, c.rb, c.n);
            c.kT1 = effMass(A, B, c.ra, c.rb, c.t1);
            c.kT2 = effMass(A, B, c.ra, c.rb, c.t2);
            c.mu  = std::sqrt(A.friction * B.friction);
            float vn = glm::dot(A.velocityAt(c.p) - B.velocityAt(c.p), c.n);
            float e = std::max(A.elasticity, B.elasticity);
            c.bias = 0.25f / h * std::max(c.depth - 0.01f, 0.0f);
            if (vn < -1.5f) c.bias = std::max(c.bias, -e * vn);
        }

        // ---- 5. Solve (sequential impulses) ------------------------------------
        for (int iter = 0; iter < 12; ++iter) {
            for (auto& c : contacts) {
                Body &A = bodies[c.a], &B = bodies[c.b];
                glm::vec3 vrel = A.velocityAt(c.p) - B.velocityAt(c.p);
                float vn = glm::dot(vrel, c.n);
                float dj = (c.bias - vn) / c.kN;
                float old = c.jn;
                c.jn = std::max(old + dj, 0.0f);
                dj = c.jn - old;
                applyImpulse(A, c.ra, c.n * dj);
                applyImpulse(B, c.rb, -c.n * dj);

                float maxF = c.mu * c.jn;
                for (int t = 0; t < 2; ++t) {
                    glm::vec3 tn = t == 0 ? c.t1 : c.t2;
                    float& acc = t == 0 ? c.jt1 : c.jt2;
                    float k = t == 0 ? c.kT1 : c.kT2;
                    vrel = A.velocityAt(c.p) - B.velocityAt(c.p);
                    float djt = -glm::dot(vrel, tn) / k;
                    float o2 = acc;
                    acc = std::clamp(o2 + djt, -maxF, maxF);
                    djt = acc - o2;
                    applyImpulse(A, c.ra, tn * djt);
                    applyImpulse(B, c.rb, -tn * djt);
                }
            }

            for (auto& j : joints) {
                Body &A = bodies[j.a], &B = bodies[j.b];
                glm::vec3 ra = A.rot * j.la, rb = B.rot * j.lb;
                glm::vec3 pa = A.pos + ra, pb = B.pos + rb;
                const float beta = 0.2f / h;

                if (j.type == ConstraintType::Rope || j.type == ConstraintType::Rod) {
                    glm::vec3 d = pb - pa;
                    float len = glm::length(d);
                    if (len < 1e-5f) continue;
                    glm::vec3 dir = d / len;
                    float C = len - j.length;
                    if (j.type == ConstraintType::Rope && C <= 0.0f) continue;   // slack rope does nothing
                    float cdot = glm::dot(B.velocityAt(pb) - A.velocityAt(pa), dir);
                    float lambda = -(cdot + beta * C) / effMass(A, B, ra, rb, dir);
                    if (j.type == ConstraintType::Rope) lambda = std::min(lambda, 0.0f);   // ropes only pull
                    applyImpulse(A, ra, -dir * lambda);
                    applyImpulse(B, rb, dir * lambda);
                    continue;
                }
                if (j.type == ConstraintType::Spring) continue;

                // Weld & hinge: the two anchor points stay together (a ball joint)...
                for (int ax = 0; ax < 3; ++ax) {
                    glm::vec3 e(0.0f);
                    e[ax] = 1.0f;
                    float C = glm::dot(pb - pa, e);
                    float cdot = glm::dot(B.velocityAt(pb) - A.velocityAt(pa), e);
                    float lambda = -(cdot + beta * C) / effMass(A, B, ra, rb, e);
                    applyImpulse(A, ra, -e * lambda);
                    applyImpulse(B, rb, e * lambda);
                }

                // ...and their rotation is locked (weld) or free around one axis (hinge).
                glm::mat3 IA = A.dynamic ? A.invInertia() : glm::mat3(0.0f);
                glm::mat3 IB = B.dynamic ? B.invInertia() : glm::mat3(0.0f);
                auto angularRow = [&](const glm::vec3& u, float error) {
                    float k = glm::dot(u, IA * u) + glm::dot(u, IB * u);
                    if (k < 1e-9f) return;
                    float cdot = glm::dot(B.w - A.w, u);
                    float lambda = -(cdot + beta * error) / k;
                    if (A.dynamic) A.w -= IA * u * lambda;
                    if (B.dynamic) B.w += IB * u * lambda;
                };
                if (j.type == ConstraintType::Weld) {
                    glm::quat target = A.rot * j.rel;
                    glm::quat err = B.rot * glm::inverse(target);
                    if (err.w < 0) err = -err;
                    glm::vec3 e = 2.0f * glm::vec3(err.x, err.y, err.z);
                    for (int ax = 0; ax < 3; ++ax) {
                        glm::vec3 u(0.0f);
                        u[ax] = 1.0f;
                        angularRow(u, glm::dot(e, u));
                    }
                } else {   // hinge
                    glm::vec3 axA = glm::normalize(A.rot * j.axisA);
                    glm::vec3 axB = glm::normalize(B.rot * j.axisB);
                    glm::vec3 e = glm::cross(axA, axB);
                    glm::vec3 p1 = std::abs(axA.x) < 0.9f ? glm::normalize(glm::cross(axA, glm::vec3(1, 0, 0)))
                                                          : glm::normalize(glm::cross(axA, glm::vec3(0, 1, 0)));
                    glm::vec3 p2 = glm::cross(axA, p1);
                    angularRow(p1, glm::dot(e, p1));
                    angularRow(p2, glm::dot(e, p2));

                    // Motor: spin B around the axis at the chosen speed.
                    if (j.node->motorTorque > 0.0f) {
                        float k = glm::dot(axA, IA * axA) + glm::dot(axA, IB * axA);
                        if (k > 1e-9f) {
                            float cdot = glm::dot(B.w - A.w, axA) - j.node->motorSpeed;
                            float lambda = -cdot / k;
                            float maxImp = j.node->motorTorque * h;
                            float old = j.accMotor;
                            j.accMotor = std::clamp(old + lambda, -maxImp, maxImp);
                            lambda = j.accMotor - old;
                            if (A.dynamic) A.w -= IA * axA * lambda;
                            if (B.dynamic) B.w += IB * axA * lambda;
                        }
                    }
                }
            }
        }
        for (auto& j : joints) j.accMotor = 0.0f;

        // ---- 6. Move ------------------------------------------------------------------
        for (auto& b : bodies) {
            if (!b.dynamic || !b.awake) continue;
            // Keep things sane even if a script launches something at warp speed.
            float sp = glm::length(b.v);
            if (sp > 200.0f) b.v *= 200.0f / sp;
            float ws = glm::length(b.w);
            if (ws > 60.0f) b.w *= 60.0f / ws;
            b.pos += b.v * h;
            glm::quat spin(0.0f, b.w.x, b.w.y, b.w.z);
            b.rot = glm::normalize(b.rot + (spin * b.rot) * (0.5f * h));
            // Update the broad-phase box.
            OBB o = b.obb();
            glm::vec3 ext(0.0f);
            for (int i = 0; i < 3; ++i) ext += glm::abs(o.axis[i]) * o.half[i];
            if (b.sphere) ext = glm::vec3(b.radius);
            b.aabbMin = b.pos - ext;
            b.aabbMax = b.pos + ext;
        }
    }

    // ---- 7. Back into the scene (and fall asleep when still) --------------------
    for (auto& b : bodies) {
        if (!b.dynamic) continue;
        SceneNode* n = b.node;
        bool still = glm::length(b.v) < 0.08f && glm::length(b.w) < 0.1f;
        n->sleepTime = (b.awake && still) ? n->sleepTime + dt : (b.awake ? 0.0f : n->sleepTime);
        if (!b.awake) continue;
        if (n->sleepTime > 0.6f) { b.v = glm::vec3(0.0f); b.w = glm::vec3(0.0f); }
        writeBack(b);
        if (b.pos.y < scene.world().fallenPartsHeight) fallen.push_back(n->id);
    }
}

void Physics::wake(SceneNode* n) { if (n) n->sleepTime = 0.0f; }
