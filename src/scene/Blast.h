#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class Scene;
class Physics;
class SceneNode;

// Explosions ("Liquid Assets & Explosive Results"). A blast is more than a puff of
// fire now:
//
//  * a shockwave: a pressure front that races outwards (it takes time to reach
//    things far away), breaking joints, flinging loose parts (light ones further),
//    knocking people over and shaking the camera as it passes;
//  * a fireball that rolls up into billowing smoke, and fires left burning;
//  * for big ones, a mushroom cloud: a column of smoke rising into a cap that
//    rolls over on itself, with a ring of dust rushing out along the ground;
//  * near water: a column of spray, a crater in the surface and rings of waves;
//    a big enough blast in big enough water sends out a tsunami (see Water.h).
//
// The smoke and fire are soft round "puffs" (SceneRenderer draws them, lit by the
// sun), not the little cube particles. Runtime only, cleared when Play stops.
struct BlastOptions {
    float radius = 8.0f;          // how far it reaches (BlastRadius)
    float power = 1.0f;           // how hard it hits (BlastPressure / 500000)
    bool  smoke = true;           // smoke left hanging in the air
    float fire = 0.0f;            // seconds fires keep burning around it (0 = none)
    int   mushroom = -1;          // -1 = only for really big blasts, 0 = never, 1 = always
    bool  destroy = false;        // rip anchored parts loose near the middle (craters)
    float jointBreak = 1.0f;      // breaks joints out to this much of the radius (DestroyJointRadiusPercent)
    bool  visible = true;         // false: no fire or smoke (the shockwave still hits)
    bool  hurts = true;           // harms characters
    // Make / read the short text that goes to other players in multiplayer.
    std::string encode() const;
    static BlastOptions decode(const std::string& text, float radius);
};

class BlastSystem {
public:
    // A soft ball of smoke, fire, dust or spray.
    struct Puff {
        glm::vec3 pos{0.0f}, vel{0.0f};
        glm::vec3 color{0.3f};
        float radius = 1.0f, grow = 0.0f;   // size now; how fast it swells (units / second)
        float life = 1.0f, maxLife = 1.0f;
        float alpha = 0.7f;                 // how thick
        float glow = 0.0f;                  // fire: light of its own (fades as it cools)
        float drag = 1.0f, lift = 0.0f;     // slows down; rises (hot) or falls (spray)
        float seed = 0.0f;                  // its own wisps
        glm::vec3 coolTo{-1.0f};            // fire turns into this smoke colour as it dies
        // Mushroom cap puffs ride around the cap's ring, rolling over and over.
        int   cloud = -1;
        float theta = 0.0f, phi = 0.0f, ring = 0.0f, tube = 0.0f;
    };
    // The shockwave: a thin bubble (and a ring of dust on the ground) growing outwards.
    struct Shock {
        glm::vec3 center{0.0f};
        float radius = 0.0f, maxRadius = 1.0f, speed = 50.0f;
        float groundY = -1e9f;              // the ground under it (no ring if there isn't any)
        float age = 0.0f;
    };

    // Set off a blast. `authority` = this machine runs the physics (single player or
    // the host); clients only show it. Gives back what it hit (for Explosion.Hit).
    struct Hit { SceneNode* part; float distance; };
    std::vector<Hit> start(Scene& scene, const glm::vec3& pos, const BlastOptions& opts, bool authority);
    void update(float dt, Scene& scene, const Physics& physics);
    void clear();

    const std::vector<Puff>&  puffs()  const { return m_puffs; }
    const std::vector<Shock>& shocks() const { return m_shocks; }
    // How hard the camera should shake right now (0 = still), for a camera at `at`.
    float shake(const glm::vec3& at) const;

    static constexpr int kMaxPuffs = 3000;

private:
    struct Live {   // a blast whose shockwave is still going out
        glm::vec3 center{0.0f};
        BlastOptions opts;
        float front = 0.0f, speed = 50.0f;
        bool authority = true;
        bool playerHit = false;
        std::unordered_set<uint64_t> done;   // parts it has already pushed
    };
    struct Fire { glm::vec3 pos; float life, size, timer = 0.0f; };
    struct Cloud {   // a mushroom cloud
        glm::vec3 ground{0.0f};
        float age = 0.0f, life = 40.0f, height = 40.0f, cap = 12.0f, timer = 0.0f, stemTimer = 0.0f;
        glm::vec3 capCenter{0.0f};
    };
    void add(const Puff& p);
    void fireball(const glm::vec3& c, const BlastOptions& o);
    void water(Scene& scene, const glm::vec3& c, const BlastOptions& o);
    void push(Scene& scene, Live& b, float from, float to, const Physics& physics);

    std::vector<Puff>  m_puffs;
    std::vector<Shock> m_shocks;
    std::vector<Live>  m_live;
    std::vector<Fire>  m_fires;
    std::vector<Cloud> m_clouds;
    struct Rumble { glm::vec3 center; float front, speed, power, radius, age; };
    std::vector<Rumble> m_rumbles;   // shockwaves for the camera shake (everyone sees these)
};
