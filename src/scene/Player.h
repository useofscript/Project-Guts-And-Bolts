#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include "SceneNode.h"
#include "Ragdoll.h"

class Scene;
class Physics;

// Roblox-style Humanoid: the tunable properties of a character.
struct Humanoid {
    float walkSpeed  = 6.0f;    // units / second
    float jumpPower  = 8.5f;    // launch velocity
    float health     = 100.0f;
    float maxHealth  = 100.0f;
    bool  autoRotate = true;    // face the direction of travel
};

// The six body colours of an R6 character.
struct BodyColors {
    glm::vec3 head, torso, leftArm, rightArm, leftLeg, rightLeg;
};

enum class HatStyle { None, TopHat, Cap, Crown };

// A Roblox-style R6 character: HumanoidRootPart, Torso, Head (with a smiley
// face), two arms and two legs, plus an optional hat. In Play mode it walks
// with swinging limbs, jumps, stands on parts, and falls apart when it dies.
//
// The character's parts live in the normal scene tree (so they can be saved,
// undone and picked); this class only remembers the id of the "Player" model.
class Player {
public:
    explicit Player(Scene* scene) : m_scene(scene) {}

    void build();                     // create a fresh rig at the spawn point
    void resetSettings();             // default Humanoid values
    SceneNode* root() const;          // the "Player" model node (may be null)
    uint64_t   rootId() const { return m_rootId; }
    void       setRootId(uint64_t id) { m_rootId = id; }

    // --- Play mode ---------------------------------------------------------
    void beginPlay();                 // remember the pose, move to the spawn
    void endPlay();
    // One simulation step. Fills `touched` with ids of parts the body touches.
    void update(float dt, const glm::vec3& moveDir, bool jump, Physics& physics);
    bool consumeDied();               // true once, right after dying
    // Kill the character. `force` 0..1 = how violent (1 = limbs fly off),
    // `impulse` = which way the body gets thrown.
    void kill(float force = 0.0f, const glm::vec3& impulse = glm::vec3(0.0f));
    // Damage with a direction (used by explosions); kills if health runs out.
    void hurt(float damage, float force, const glm::vec3& impulse);
    bool isDead() const { return m_dead; }
    float respawnIn() const { return m_dead ? m_respawnDelay - m_deadTime : 0.0f; }

    void      setSpawn(const glm::vec3& p) { m_spawn = p; }
    glm::vec3 spawn() const { return m_spawn; }
    glm::vec3 position() const;
    glm::vec3 focusPoint() const;     // where the play camera should look

    Humanoid& humanoid() { return m_humanoid; }
    glm::vec3 velocity() const { return m_velocity; }
    void      launch(const glm::vec3& v) { m_velocity = v; m_grounded = false; }   // jump pads etc.

    // --- Appearance ----------------------------------------------------------
    BodyColors bodyColors() const;
    void       setBodyColors(const BodyColors& c);
    HatStyle   hat() const { return m_hat; }
    void       setHat(HatStyle style);
    void       rememberHat(HatStyle style) { m_hat = style; }   // no rebuild (loading)

    static const char* hatName(HatStyle s);
    static std::vector<std::pair<const char*, BodyColors>> colorPresets();

private:
    SceneNode* part(const char* name) const;
    void animate(float dt, bool moving, bool grounded);
    void startDeath();
    void bleed(float damage);
    void updateDeath(float dt, Physics& physics);
    void respawn();

    Scene*   m_scene  = nullptr;
    uint64_t m_rootId = 0;
    Humanoid m_humanoid;
    HatStyle m_hat = HatStyle::None;

    glm::vec3 m_spawn{0.0f};
    glm::vec3 m_velocity{0.0f};
    bool      m_grounded = false;

    // Moving platforms carry the character.
    uint64_t  m_groundId = 0;
    glm::vec3 m_groundPrev{0.0f};

    // Walk-cycle animation.
    float m_walkPhase = 0.0f;
    float m_swing     = 0.0f;   // current limb swing amplitude (degrees)
    float m_airBlend  = 0.0f;   // 0 = on ground, 1 = jump pose

    // Rest pose captured when Play starts (local transforms by node id).
    std::unordered_map<uint64_t, Transform> m_rest;
    Transform m_rootRest;

    // Death ("oof") — parts tumble apart, then the character respawns.
    bool  m_dead = false, m_diedFlag = false;
    float m_deadTime = 0.0f;
    const float m_respawnDelay = 4.0f;
    struct Debris { uint64_t id; glm::vec3 vel; glm::vec3 spin; };
    std::vector<Debris> m_debris;

    Ragdoll   m_ragdoll;
    float     m_pendingForce = 0.0f;
    glm::vec3 m_pendingImpulse{0.0f};
    float     m_lastHealth = 100.0f;
};
