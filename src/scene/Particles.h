#pragma once
#include <glm/glm.hpp>
#include <vector>

class Physics;

// Small flying bits used for gore and effects: blood / oil drops that splat
// and stick where they land, meat chunks, bolts, sparks, fire and smoke.
// Runtime only — never saved, cleared when Play stops.
struct Particle {
    enum Kind { Drop, Chunk, Bolt, Spark, Fire, Smoke, Splat, Spray };   // Spray: water droplets
    Kind      kind = Drop;
    glm::vec3 pos{0.0f}, vel{0.0f};
    glm::vec3 color{1.0f};
    glm::vec3 size{0.1f};
    glm::vec3 rot{0.0f}, spin{0.0f};    // degrees, degrees / second
    float     life = 1.0f, maxLife = 1.0f;
    bool      glossy = false;           // oil / bolts shine like metal
};

enum class GoreKind { Blood, Oil };

class ParticleSystem {
public:
    void update(float dt, float gravity, const Physics& physics);
    void clear() { m_items.clear(); }
    const std::vector<Particle>& items() const { return m_items; }

    // --- Effects --------------------------------------------------------------
    // A burst of drops flying out from `pos` (direction biased by `dir`).
    void spray(GoreKind kind, const glm::vec3& pos, const glm::vec3& dir, int count, float speed);
    // Chunks of meat / bolts and metal scraps.
    void gibs(GoreKind kind, const glm::vec3& pos, const glm::vec3& vel, int count);
    void sparks(const glm::vec3& pos, int count);
    void explosion(const glm::vec3& pos, float radius);
    // Water thrown up by a splash (droplets that vanish when they land).
    void waterSpray(const glm::vec3& pos, int count, float speed, const glm::vec3& color, float radius = 0.3f);   // radius: a ring around what fell in

    static constexpr int kMaxParticles = 2500;
    static constexpr int kMaxSplats    = 700;

private:
    void add(const Particle& p);
    std::vector<Particle> m_items;
    int m_splats = 0;
    float m_splatSound = 0.0f;
};
