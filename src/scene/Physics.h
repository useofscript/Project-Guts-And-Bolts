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

// A point on a walking route (PathfindingService).
struct PathPoint {
    glm::vec3 pos;    // where the feet go
    bool      jump;   // jump to get here
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
        std::vector<std::pair<SceneNode*, glm::vec3>> pushed;   // loose parts we walked into
    };

    // The character's body, like Roblox's R6: 2 studs wide and 1 deep (the torso;
    // arms and legs don't collide), from the feet to the top of the head.
    static constexpr float kCharHalfWidth = 0.5f;
    static constexpr float kCharHalfDepth = 0.25f;
    static constexpr float kCharHeight    = 2.65f;
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

    // `yaw` (degrees) = which way the character faces: its box turns with it.
    // `self`: the moving character's root id, so it doesn't bump into itself
    // (other characters are solid when the game has Player Collisions on).
    MoveResult moveCharacter(const glm::vec3& feet, const glm::vec3& delta, bool wasGrounded, float yaw = 0.0f,
                             uint64_t self = 0) const;
    static OBB  charOBB(const glm::vec3& feet, float yaw);   // the character's body box
    // Loose parts moving into the character: which way each one shoves it and how
    // hard (the speed it gives), heavier and faster parts shoving harder.
    struct Shove { glm::vec3 dir; float speed; };
    std::vector<Shove> shoves(const glm::vec3& feet, float yaw, float characterMass) const;
    static AABB bounds(const OBB& o);                       // a box around a turned box
    // Unanchored parts: rigid-body physics (tumbling, stacking, bouncing) and
    // constraints (ropes, rods, springs, welds, hinges). Ids of parts that fell
    // out of the world are appended to `fallen`. See RigidBodies.cpp.
    void stepParts(Scene& scene, float dt, std::vector<uint64_t>& fallen);
    static void wake(SceneNode* n);   // make a sleeping part move again
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
    // A walking route for a character from `start` to `goal` (feet positions): around
    // walls, up steps, and up ledges no higher than `jumpHeight` (jumping). The first
    // point is the start. False if there's no way there (or it's too far to search).
    bool findPath(const glm::vec3& start, const glm::vec3& goal, float jumpHeight,
                  std::vector<PathPoint>& out) const;
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
    bool blocked(const AABB& box) const;

    std::vector<Collider>                  m_colliders;
    std::vector<Collider>                  m_bodies;   // characters' body boxes (players bumping into players)
    std::set<std::pair<uint64_t, uint64_t>> m_touching;
};
