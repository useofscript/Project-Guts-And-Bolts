#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include "SceneNode.h"
#include "Player.h"
#include "Ragdoll.h"

class Scene;
class Physics;

// A computer-controlled character (a zombie, a shopkeeper, a guard...). Any
// Model in the world that is built like a character (HumanoidRootPart, Torso
// and Head inside) becomes one when the game starts, or as soon as a script
// puts one in the world. Scripts steer it through its Humanoid, like Roblox:
// humanoid:MoveTo(point), humanoid:Move(direction), humanoid.Jump = true.
struct Npc {
    uint64_t  rootId = 0;
    Humanoid  humanoid;
    glm::vec3 velocity{0.0f};
    bool      grounded = false;

    // Where it's walking to (MoveTo), or which way (Move).
    bool      hasTarget = false;
    glm::vec3 target{0.0f};
    float     targetTime = 0.0f;        // gives up after 8 seconds, like Roblox
    glm::vec3 moveDir{0.0f};
    bool      jump = false;             // Humanoid.Jump: jumps once, then goes back to false
    float     stuckTime = 0.0f;         // walking into a wall: hop

    // Walk cycle, like the player's.
    std::unordered_map<uint64_t, Transform> rest;
    float walkPhase = 0.0f, swing = 0.0f, air = 0.0f;
    bool  zombie = false;               // arms stretched out in front

    float     lastHealth = 100.0f;
    bool      dead = false;
    float     deadTime = 0.0f;
    float     pendingForce = 0.0f;
    glm::vec3 pendingImpulse{0.0f};
    Ragdoll   ragdoll;
    struct Debris { uint64_t id; glm::vec3 vel; glm::vec3 spin; };
    std::vector<Debris> debris;         // classic death: the pieces fly apart
};

// Every NPC in the game. Lives in the Scene; GameSession runs it while playing.
class NpcSystem {
public:
    void begin(Scene& scene);    // Play pressed: find every character-shaped Model
    void end();                  // Stop: forget them all
    bool active() const { return m_active; }

    // One step for every NPC. Died and MoveToFinished events pile up for the scripts.
    void update(float dt, Scene& scene, Physics& physics);

    // The NPC for this model. With `adopt`, a character-shaped Model that isn't
    // one yet (a script just made or cloned it) becomes one now.
    Npc* find(Scene& scene, uint64_t rootId, bool adopt = true);
    Npc* find(uint64_t rootId);
    void kill(uint64_t rootId, float force, const glm::vec3& impulse);
    const std::vector<std::unique_ptr<Npc>>& all() const { return m_npcs; }

    // Built like a character: a Model with HumanoidRootPart, Torso and Head,
    // out in the world (not hidden in storage), and not a player.
    static bool isRig(Scene& scene, const SceneNode* model);

    std::vector<uint64_t>                  died;       // Humanoid.Died to fire
    std::vector<std::pair<uint64_t, bool>> finished;   // Humanoid.MoveToFinished (reached?)

private:
    Npc* adopt(Scene& scene, SceneNode* model);
    void step(Npc& npc, SceneNode* root, float dt, Scene& scene, Physics& physics);
    void animate(Npc& npc, SceneNode* root, float dt, bool moving);
    void startDeath(Npc& npc, SceneNode* root, Scene& scene);
    void updateDeath(Npc& npc, SceneNode* root, float dt, Scene& scene, Physics& physics);

    std::vector<std::unique_ptr<Npc>> m_npcs;
    bool  m_active = false;
    float m_scanTime = 0.0f;
};
