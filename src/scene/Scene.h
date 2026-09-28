#pragma once
#include "SceneNode.h"
#include "Environment.h"
#include "Player.h"
#include "Particles.h"
#include <functional>
#include <memory>
#include <unordered_map>

// How characters die. Classic = Roblox-style: the parts just fall apart.
// Ragdoll = the body goes limp and tumbles with real joints.
enum class DeathStyle { Classic, Ragdoll };
// Gore = what comes out: nothing, oil & bolts (robots), or blood & guts.
enum class GoreLevel  { Off, OilAndBolts, Blood };

// Rules and physics for the whole place (like Roblox's Workspace properties).
struct WorldSettings {
    float      gravity           = 22.0f;   // units / second²
    float      fallenPartsHeight = -50.0f;  // below this, parts are destroyed and players die

    DeathStyle deathStyle      = DeathStyle::Ragdoll;
    GoreLevel  gore            = GoreLevel::Blood;
    bool       dismemberment   = true;      // big hits can knock limbs off
    bool       fallDamage      = true;
    float      fallDamageSpeed = 20.0f;     // landing faster than this hurts
};

// Shown on the game's card in the Guts&BoltsPlayer app.
struct GameInfo {
    std::string title       = "My Game";
    std::string description = "A game made with Guts and Bolts.";
    std::string author      = "Builder";
};

class Scene {
public:
    Scene();

    SceneNode*     root()        { return m_root.get(); }
    SceneNode*     selected()    { return m_selected; }
    Environment&   environment() { return m_env; }
    WorldSettings& world()       { return m_world; }
    GameInfo&      info()        { return m_info; }
    Player*        player()      { return m_player.get(); }
    ParticleSystem& particles()  { return m_particles; }
    // Gore is shown only if the game allows it AND the player hasn't turned it off.
    bool           goreEnabled() const;
    GoreKind       goreKind() const;

    void select  (SceneNode* node);
    void deselect();

    // Create a Part directly under the Workspace.
    SceneNode* addNode(const std::string& name, PrimitiveType type, std::shared_ptr<Mesh> mesh);
    // Insert an already-built node (and its children) under `parent` (default: Workspace).
    SceneNode* insert(std::unique_ptr<SceneNode> node, SceneNode* parent = nullptr);
    void       removeNode(SceneNode* node);
    // Take a node out of the tree without destroying it (Lua: part.Parent = nil).
    std::unique_ptr<SceneNode> detach(SceneNode* node);
    // Move a node under a new parent, keeping where it is in the world.
    bool reparent(SceneNode* node, SceneNode* newParent);

    SceneNode* findById(uint64_t id);
    void       forEach(std::function<void(SceneNode*)> fn);

    // Nodes the user must not delete / move (Workspace and the character).
    bool isProtected(const SceneNode* node) const;
    // True for the character model and every part inside it.
    bool isCharacterPart(const SceneNode* node) const;

    // Wipe everything and build the default starting place.
    void buildDefault();
    // Replace the whole tree (used when loading a file / undoing).
    void replaceRoot(std::unique_ptr<SceneNode> root);
    void markDirty() { m_indexDirty = true; }

private:
    void walk(SceneNode* node, std::function<void(SceneNode*)>& fn);
    void rebuildIndex();

    std::unique_ptr<SceneNode> m_root;
    SceneNode*                 m_selected = nullptr;
    Environment                m_env;
    WorldSettings              m_world;
    GameInfo                   m_info;
    ParticleSystem             m_particles;
    std::unique_ptr<Player>    m_player;

    std::unordered_map<uint64_t, SceneNode*> m_index;
    bool                                     m_indexDirty = true;
};
