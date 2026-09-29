#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

class Scene;
class SceneNode;

// Real water for parts that are water (see Player::isWater): while the game
// runs, each one gets a moving surface.
//
//  * The surface is a grid of little columns of water. Each column is pulled
//    towards the level of its neighbours, which makes waves that spread out,
//    bounce off the sides and slowly calm down (the "wave equation").
//  * Anything that falls in pushes the surface down (a splash and rings of
//    ripples); people and boats moving through it leave a wake.
//  * The physics asks it "how high is the water here?" so things float on the
//    waves (see buoyancy in RigidBodies.cpp) and swimmers bob up and down.
//
// A water part can have attributes: "Waves" (how tall the ocean swell is, in
// units) and "Flow" (a Vector3: a current that carries things along).
class WaterSystem {
public:
    struct Body {
        uint64_t  id = 0;
        glm::vec3 min{0.0f}, max{0.0f};   // the water's box in the world
        int       nx = 0, nz = 0;         // grid points across x and z
        float     cell = 1.0f;            // distance between grid points
        std::vector<float> h, v;          // how far each point is above / below the still level, and its speed
        float     swell = 0.0f;           // "Waves" attribute
        glm::vec3 flow{0.0f};             // "Flow" attribute
        glm::vec3 color{0.2f, 0.45f, 0.7f};
        float     transparency = 0.4f;
    };

    void begin(Scene& scene);   // Play pressed: find every water part
    void end();
    bool active() const { return m_active; }
    void update(float dt, Scene& scene);

    // Is `p` in water? Gives the height of the surface above it and the current.
    bool at(const glm::vec3& p, float* surface = nullptr, glm::vec3* flow = nullptr) const;
    // The surface height of the water part `id` at (x, z) (its top if it isn't simulated).
    float surfaceOf(const SceneNode* water, float x, float z) const;
    // Push the surface down (amount > 0) or up around `p`: makes ripples.
    void disturb(const glm::vec3& p, float amount, float radius);
    // Something hit the water: ripples, spray and a sound. `size` ~ how big it is.
    void splash(Scene& scene, const glm::vec3& p, float speed, float size);
    // A physics body is in the water this frame (for splashes when it first goes in).
    void touching(Scene& scene, uint64_t id, const glm::vec3& p, float downSpeed, float size);

    const std::vector<Body>& bodies() const { return m_bodies; }
    float surface(const Body& b, float x, float z) const { return b.max.y + heightAt(b, x, z) + swellAt(b, x, z); }
    const Body* find(uint64_t id) const;
    float time() const { return m_time; }

private:
    void scan(Scene& scene);
    Body* bodyAt(const glm::vec3& p, float pad = 0.0f);
    const Body* bodyAt(const glm::vec3& p, float pad = 0.0f) const;
    float heightAt(const Body& b, float x, float z) const;   // grid part only
    float swellAt(const Body& b, float x, float z) const;

    std::vector<Body> m_bodies;
    bool  m_active = false;
    float m_time = 0.0f;
    std::unordered_map<uint64_t, float> m_lastWet;           // when each body was last in the water
    std::unordered_map<uint64_t, glm::vec3> m_charPrev;      // characters' last positions (for wakes)
    std::unordered_map<uint64_t, bool> m_charWet;
    float m_splashSound = 0.0f;
    float m_scanTime = 0.25f;
};
