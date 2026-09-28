#pragma once
// Internal glue shared by the Lua binding files. Not needed outside scripting/.
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <cstdint>
#include <glm/glm.hpp>

class ScriptEngine;
class SceneNode;

// Events a script can :Connect() to.
enum class SignalKind : int {
    Heartbeat,     // RunService.Heartbeat            (dt)
    InputBegan,    // UserInputService.InputBegan     (input, gameProcessed)
    InputEnded,    // UserInputService.InputEnded     (input, gameProcessed)
    Touched,       // part.Touched                    (otherPart)
    Clicked,       // part.Clicked                    ()
    Died,          // Humanoid.Died                   ()   (id = character)
    PlayerAdded,   // Players.PlayerAdded             (player)
    PlayerRemoving,// Players.PlayerRemoving          (player)
    AttributeChanged, // obj.AttributeChanged         (name)
    TagAdded,      // CollectionService (id 0)        (object, tag)
    TagRemoved,    // CollectionService (id 0)        (object, tag)
    Activated,     // tool.Activated                  ()   (the player clicked while holding it)
    Deactivated,   // tool.Deactivated                ()
    Equipped,      // tool.Equipped                   ()
    Unequipped,    // tool.Unequipped                 ()
};

namespace LuaApi {

ScriptEngine* engine(lua_State* L);

// Value types (LuaTypes.cpp)
void       registerTypes(lua_State* L);
void       pushVector3(lua_State* L, const glm::vec3& v);
glm::vec3* toVector3  (lua_State* L, int idx);            // null if not a Vector3
glm::vec3  checkVector3(lua_State* L, int idx);
void       pushColor3 (lua_State* L, const glm::vec3& c);
glm::vec3  checkColor3(lua_State* L, int idx);
glm::vec3* toColor3   (lua_State* L, int idx);            // null if not a Color3
void       pushCFrame (lua_State* L, const glm::mat4& m);
glm::mat4* toCFrame   (lua_State* L, int idx);
glm::mat4  checkCFrame(lua_State* L, int idx);

// Objects (LuaInstance.cpp)
void       registerInstance(lua_State* L);
void       pushInstance(lua_State* L, uint64_t id);        // pushes nil for 0
SceneNode* checkNode   (lua_State* L, int idx);            // errors if destroyed
void       pushSignal  (lua_State* L, SignalKind kind, uint64_t id);
void       pushHumanoid(lua_State* L, uint64_t characterRootId);
void       pushLighting(lua_State* L);

} // namespace LuaApi
