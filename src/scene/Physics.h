#pragma once
#include <cstdint>
#include <set>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

class Scene;
class SceneNode;

struct AABB {
    glm::vec3 min{0.0f}, max{0.0f};
    bool overlaps(const AABB& o, float eps = 1e-4f) const {
        return min.x < o.max.x - eps && max.x > o.min.x + eps &&
               min.y < o.max.y - eps && max.y > o.min.y + eps &&
               min.z < o.max.z - eps && max.z > o.min.z + eps;
    }
    AABB inflated(float d) const { return {min - glm::vec3(d), max + glm::vec3(d)}; }
};

// A box that can be rotated any way (used for tilted parts, ramps, etc.).
struct OBB {
    glm::vec3 center{0.0f};
    glm::vec3 axis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    glm::vec3 half{0.5f};
    static OBB fromAABB(const AABB& b) {
        OBB o;
        o.center = (b.min + b.max) * 0.5f;
        o.half = (b.max - b.min) * 0.5f;
        return o;
    }
};

// "part was touched by other" — fed to the Touched event in scripts.
struct TouchEvent {
    uint64_t partId;
    uint64_t otherId;
};

// Very small physics world used in Play mode:
//  * the character is a box that walks, climbs small steps and lands on parts
//  * unanchored parts fall with gravity and rest on whatever is below them
//  * overlaps are reported as Touched events
// Parts are treated as axis-aligned boxes (rotated parts use their bounding box).
class Physics {
public:
    struct MoveResult {
        glm::vec3 position;
        bool      grounded   = false;
        bool      hitCeiling = false;
        uint64_t  groundId   = 0;    // the part we are standing on
    };

    static constexpr float kCharHalfWidth = 0.5f;
    static constexpr float kCharHeight    = 2.6f;
    static constexpr float kStepHeight    = 0.55f;

    static AABB worldBounds(const SceneNode* node);
    static OBB  worldOBB(const SceneNode* node);
    // Separating-axis test. On overlap, `normal` is the direction to push `a`
    // out of `b` and `depth` how far.
    static bool obbOverlap(const OBB& a, const OBB& b, glm::vec3& normal, float& depth);
    // First visible part hit by a ray (skipping `ignore` and everything inside it).
    static SceneNode* raycast(Scene& scene, const glm::vec3& origin, const glm::vec3& dir,
                              float* distance = nullptr, const SceneNode* ignore = nullptr);
    static AABB characterBox(const glm::vec3& feet);

    void reset();                       // forget touch state (on Play / Stop)
    void gather(Scene& scene);          // collect this frame's parts

    MoveResult moveCharacter(const glm::vec3& feet, const glm::vec3& delta, bool wasGrounded) const;
    // Unanchored parts: gravity + collision. Ids of parts that fell out of the
    // world are appended to `fallen`.
    void stepParts(Scene& scene, float dt, std::vector<uint64_t>& fallen);
    // How far `box` must move up to rest on top of a solid part (0 = free).
    float pushUp(const AABB& box) const;
    // Is this point inside any solid part?
    bool  solidAt(const glm::vec3& p) const;
    // Push a sphere out of solid parts. Returns true (and the push normal) on contact.
    bool  resolveSphere(glm::vec3& center, float radius, glm::vec3* normal = nullptr) const;
    // Everything the physics world knows about (for explosions etc.).
    template <typename F> void forEachCollider(F&& f) const {
        for (const auto& c : m_colliders) f(c.node, c.box, c.dynamic);
    }
    // New touches since the last call (parts vs character, unanchored vs others).
    void collectTouches(Scene& scene, std::vector<TouchEvent>& out);

private:
    struct Collider {
        SceneNode* node;
        AABB       box;
        bool       solid;     // CanCollide
        bool       dynamic;   // unanchored
        bool       rotated;   // not lined up with the world axes: use `obb`
        OBB        obb;
    };
    static OBB charOBB(const glm::vec3& feet);
    bool blocked(const AABB& box) const;

    std::vector<Collider>                  m_colliders;
    std::set<std::pair<uint64_t, uint64_t>> m_touching;
};
