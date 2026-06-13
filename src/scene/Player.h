#pragma once
#include <glm/glm.hpp>

class Scene;
class SceneNode;

// Roblox-style Humanoid: the tunable properties of a character.
struct Humanoid {
    float walkSpeed  = 4.0f;    // units / second
    float jumpPower  = 6.0f;    // launch velocity
    float health     = 100.0f;
    float maxHealth  = 100.0f;
    bool  autoRotate = true;    // face the direction of travel
};

// A simple R6-style character: HumanoidRootPart, Torso, Head (with a smiley
// face), two arms and two legs, plus a tiny kinematic controller used in
// playtest mode (gravity, jumping and ground collision).
class Player {
public:
    void build(Scene* scene);                                   // create the rig
    void update(float dt, const glm::vec3& moveDir, bool jump); // playtest step
    void reset();                                               // back to spawn

    void setSpawn(const glm::vec3& p) { m_spawn = p; }
    glm::vec3 position() const;

    SceneNode* root()     const { return m_root; }
    Humanoid&  humanoid()       { return m_humanoid; }

private:
    Scene*     m_root_scene = nullptr;
    SceneNode* m_root       = nullptr;   // "Player" node, origin at the feet
    Humanoid   m_humanoid;
    glm::vec3  m_velocity{0.0f};
    glm::vec3  m_spawn{0.0f, 0.0f, 0.0f};
    bool       m_grounded = false;
};
