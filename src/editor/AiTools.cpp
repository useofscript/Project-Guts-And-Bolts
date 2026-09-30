// Studio's AI tools (see AiTools.h): look at the game, change it, write
// scripts, run Lua, read the Output, playtest and take screenshots.
#include "AiTools.h"
#include "Editor.h"
#include "panels/ViewportPanel.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scene/Serializer.h"
#include "../core/Log.h"

#include <algorithm>
#include <sstream>

using nlohmann::json;

namespace {

json schema(json props, std::vector<std::string> required) {
    return {{"type", "object"}, {"properties", std::move(props)}, {"required", required}, {"additionalProperties", false}};
}
const json kObject = {{"type", "string"},
                      {"description", "Which object: its id like \"#42\", or a path like \"Workspace.Castle.Door\""}};

const char* classOf(const SceneNode* n, const SceneNode* root) {
    if (n == root) return "Workspace";
    switch (n->kind) {
        case NodeKind::Model:  return "Model";
        case NodeKind::Script: return n->isModule ? "ModuleScript" : "Script";
        case NodeKind::Light:  return n->lightType == LightType::Spot ? "SpotLight" : "PointLight";
        case NodeKind::ForceField: return "ForceField";
        case NodeKind::Tool:       return "Tool";
        case NodeKind::Value:      return n->valueClass();
        case NodeKind::Decal:      return "Decal";
        case NodeKind::Animation:  return "Animation";
        case NodeKind::FluidSystem:  return "FluidSystem";
        case NodeKind::FluidEmitter: return "FluidEmitter";
        case NodeKind::Gui:        return kGuiClassNames[(int)n->gui.type];
        case NodeKind::Sound:      return "Sound";
        case NodeKind::Attachment: return "Attachment";
        case NodeKind::Constraint: return "Constraint";
        default: return n->primitiveType == PrimitiveType::Mesh ? "MeshPart" : "Part";
    }
}

std::string luaQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') {}
        else out += c;
    }
    return out + "\"";
}

// A JSON value as Lua: numbers, true/false, strings; [x,y,z] becomes a
// Vector3 (or a Color3 for colour properties, 0-1 each).
std::string luaValue(const json& v, const std::string& property) {
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number()) { std::ostringstream o; o << v.get<double>(); return o.str(); }
    if (v.is_string()) return luaQuote(v.get<std::string>());
    if (v.is_array() && v.size() == 3) {
        std::ostringstream o;
        bool color = property.find("Color") != std::string::npos;
        o << (color ? "Color3.new(" : "Vector3.new(") << v[0].get<double>() << ", " << v[1].get<double>() << ", " << v[2].get<double>() << ")";
        return o.str();
    }
    if (v.is_null()) return "nil";
    return luaQuote(v.dump());
}

} // namespace

namespace AiTools {

const char* guide() {
    return "You are helping someone build a game in Guts and Bolts Studio, a Roblox-like engine. "
           "Games are made of objects in a tree under Workspace (Parts, Models, Scripts, lights, UI...). "
           "Scripts are Lua 5.4 with a Roblox-style API (Instance.new, part.Touched, game:GetService, "
           "RunService.Heartbeat, TweenService-free animation with Heartbeat, wait(), Players.LocalPlayer, "
           "humanoid:MoveTo, PathfindingService, Effects.Explosion/Blood, Sounds.Play). Luau-only syntax such as "
           "`+=`, type annotations and `continue` does not work: write plain Lua. Units: 1 unit = 2 Roblox studs; "
           "the character is about 2.6 units tall and walks at 6 units a second. "
           "Look before you change things (get_game_tree, get_object), make changes with the tools, check "
           "get_output after running code or playtesting, and use screenshot to see the result. "
           "Everything you change can be undone with the undo tool. Keep explanations short and friendly: "
           "the person may be new to making games.";
}

const json& list() {
    static const json tools = json::array({
        {{"name", "get_game_tree"},
         {"description", "List the objects in the game as an indented tree: name, class and id (#number). "
                         "Start here to see what exists."},
         {"input_schema", schema({{"root", kObject}, {"depth", {{"type", "integer"}, {"description", "How many levels deep (default 4)"}}}}, {})}},
        {{"name", "get_object"},
         {"description", "All the properties of one object (position, size, color, material, script source...), as JSON."},
         {"input_schema", schema({{"object", kObject}}, {"object"})}},
        {{"name", "set_property"},
         {"description", "Change one property of an object, like Roblox: Position [x,y,z], Size [x,y,z], "
                         "Color [r,g,b] (0-1), Transparency, Anchored, CanCollide, Material (\"Neon\"), Name, Parent (an object)..."},
         {"input_schema", schema({{"object", kObject}, {"property", {{"type", "string"}}},
                                  {"value", {{"description", "number, boolean, string, or [x, y, z]"}}}}, {"object", "property", "value"})}},
        {{"name", "insert_object"},
         {"description", "Add a new object. kind is a class or ready-made thing: Part, Sphere, Cylinder, Wedge, Model, Folder, "
                         "Script, LocalScript, ModuleScript, PointLight, SpotLight, Sound, SpawnLocation, TrussPart, Water, "
                         "WaterSource, ScreenGui, Frame, TextLabel, TextButton, ImageLabel, Tool, ... "
                         "Returns the new object's id."},
         {"input_schema", schema({{"kind", {{"type", "string"}}}, {"parent", kObject}, {"name", {{"type", "string"}}}}, {"kind"})}},
        {{"name", "delete_object"},
         {"description", "Delete an object (and everything inside it)."},
         {"input_schema", schema({{"object", kObject}}, {"object"})}},
        {{"name", "create_script"},
         {"description", "Make a Script (runs when the game starts) or ModuleScript with the given Lua source inside an object."},
         {"input_schema", schema({{"parent", kObject}, {"name", {{"type", "string"}}}, {"source", {{"type", "string"}}},
                                  {"type", {{"type", "string"}, {"enum", {"Script", "ModuleScript"}}}}}, {"source"})}},
        {{"name", "read_script"},
         {"description", "The Lua source of a script."},
         {"input_schema", schema({{"object", kObject}}, {"object"})}},
        {{"name", "edit_script"},
         {"description", "Change a script: replace `old` with `new` (old must appear exactly once), or give `source` to replace all of it."},
         {"input_schema", schema({{"object", kObject}, {"old", {{"type", "string"}}}, {"new", {{"type", "string"}}},
                                  {"source", {{"type", "string"}}}}, {"object"})}},
        {{"name", "run_lua"},
         {"description", "Run Lua in Studio right now, like the Command Bar (inside the game while playtesting). "
                         "Great for building many things at once. Returns what it printed and any error."},
         {"input_schema", schema({{"code", {{"type", "string"}}}}, {"code"})}},
        {{"name", "get_output"},
         {"description", "The latest lines of the Output window (prints, warnings, script errors)."},
         {"input_schema", schema({{"lines", {{"type", "integer"}, {"description", "How many (default 40)"}}}}, {})}},
        {{"name", "playtest"},
         {"description", "Start or stop playtesting (like pressing Play / Stop). Stopping puts the game back how it was."},
         {"input_schema", schema({{"action", {{"type", "string"}, {"enum", {"start", "stop"}}}}}, {"action"})}},
        {{"name", "screenshot"},
         {"description", "A picture of what Studio's 3D view shows right now."},
         {"input_schema", schema(json::object(), {})}},
        {{"name", "select"},
         {"description", "Select objects in Studio (so the person can see them), or with no objects, clear the selection."},
         {"input_schema", schema({{"objects", {{"type", "array"}, {"items", kObject}}}}, {})}},
        {{"name", "undo"},
         {"description", "Undo the last change."},
         {"input_schema", schema(json::object(), {})}},
    });
    return tools;
}

} // namespace AiTools

// ---------------------------------------------------------------------------

SceneNode* Editor::aiFind(const std::string& ref) {
    if (ref.empty()) return nullptr;
    if (ref[0] == '#') {
        try { return m_scene->findById(std::stoull(ref.substr(1))); } catch (...) { return nullptr; }
    }
    std::stringstream ss(ref);
    std::string part;
    SceneNode* n = m_scene->root();
    bool first = true;
    while (std::getline(ss, part, '.')) {
        if (first && (part == "Workspace" || part == "workspace" || part == "game")) { first = false; continue; }
        if (first && part == "game") continue;
        first = false;
        SceneNode* c = n->findChild(part);
        if (!c) return nullptr;
        n = c;
    }
    return n;
}

AiToolResult Editor::runAiTool(const std::string& name, const json& args) {
    auto fail = [](const std::string& why) { return AiToolResult{why, "", true}; };
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    auto need = [&](const char* k, SceneNode*& out) -> bool {
        out = aiFind(str(k));
        return out != nullptr;
    };
    auto label = [&](const SceneNode* n) {
        return n->name + " (" + classOf(n, m_scene->root()) + ") #" + std::to_string(n->id);
    };
    // Run Lua and collect what it printed.
    auto lua = [&](const std::string& code) {
        size_t before = Log::entries().size();
        runCommand(code);
        std::string out;
        bool err = false;
        const auto& e = Log::entries();
        for (size_t i = before; i < e.size(); ++i) {
            if (i == before && e[i].text.rfind("> ", 0) == 0) continue;   // the echo of the command
            if (e[i].level == Log::Level::Error) err = true;
            out += (e[i].level == Log::Level::Error ? "ERROR: " : e[i].level == Log::Level::Warn ? "warning: " : "") + e[i].text + "\n";
        }
        return AiToolResult{out.empty() ? "Done (nothing printed)." : out, "", err};
    };

    if (name == "get_game_tree") {
        SceneNode* root = args.contains("root") ? aiFind(str("root")) : m_scene->root();
        if (!root) return fail("No object called " + str("root"));
        int depth = args.value("depth", 4);
        std::string out;
        int count = 0;
        std::function<void(SceneNode*, int)> walk = [&](SceneNode* n, int d) {
            if (n->internal || count > 1500) return;
            out += std::string((size_t)d * 2, ' ') + label(n) + "\n";
            ++count;
            if (d >= depth) {
                int kids = 0;
                for (auto& c : n->children) if (!c->internal) ++kids;
                if (kids) out += std::string((size_t)(d + 1) * 2, ' ') + "... " + std::to_string(kids) + " more inside\n";
                return;
            }
            for (auto& c : n->children) walk(c.get(), d + 1);
        };
        walk(root, 0);
        return {out};
    }
    if (name == "get_object") {
        SceneNode* n;
        if (!need("object", n)) return fail("No object called " + str("object"));
        json j = json::parse(Serializer::nodeToString(*n), nullptr, false);
        if (j.is_object()) {
            j.erase("children");
            j.erase("mesh");            // big model data isn't useful here
            j.erase("editMesh");
            j["className"] = classOf(n, m_scene->root());
            json kids = json::array();
            for (auto& c : n->children) if (!c->internal) kids.push_back(label(c.get()));
            j["childrenList"] = kids;
        }
        std::string s = j.dump(1);
        if (s.size() > 60000) s = s.substr(0, 60000) + "\n... (cut short)";
        return {s};
    }
    if (name == "set_property") {
        SceneNode* n;
        if (!need("object", n)) return fail("No object called " + str("object"));
        std::string prop = str("property");
        if (prop.empty() || prop.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
            return fail("That isn't a property name.");
        json value = args.contains("value") ? args["value"] : json();
        std::string v = prop == "Parent" && value.is_string() && aiFind(value.get<std::string>())
                      ? "__gb_byId(" + std::to_string(aiFind(value.get<std::string>())->id) + ")" : luaValue(value, prop);
        AiToolResult r = lua("local o = __gb_byId(" + std::to_string(n->id) + ")\no." + prop + " = " + v);
        if (!r.error) r.text = "Set " + prop + " of " + label(n) + ".";
        return r;
    }
    if (name == "insert_object") {
        SceneNode* parent = args.contains("parent") ? aiFind(str("parent")) : nullptr;
        if (args.contains("parent") && !parent) return fail("No object called " + str("parent"));
        std::vector<uint64_t> before;
        m_scene->forEach([&](SceneNode* n) { before.push_back(n->id); });
        std::sort(before.begin(), before.end());
        insertObject(str("kind"), parent);
        SceneNode* made = nullptr;
        m_scene->forEach([&](SceneNode* n) {
            if (!made && !std::binary_search(before.begin(), before.end(), n->id) && !n->internal) made = n;
        });
        if (!made) return fail("Couldn't insert a \"" + str("kind") + "\". Try a class name like Part, Model or Script.");
        if (!str("name").empty()) made->name = str("name");
        return {"Added " + label(made) + " inside " + (made->parent ? label(made->parent) : std::string("the game")) + "."};
    }
    if (name == "delete_object") {
        SceneNode* n;
        if (!need("object", n)) return fail("No object called " + str("object"));
        if (m_scene->isProtected(n)) return fail("That can't be deleted.");
        std::string what = label(n);
        m_scene->deselect();
        m_scene->removeNode(n);
        return {"Deleted " + what + "."};
    }
    if (name == "create_script") {
        SceneNode* parent = args.contains("parent") ? aiFind(str("parent")) : m_scene->root();
        if (!parent) return fail("No object called " + str("parent"));
        auto s = std::make_unique<SceneNode>(str("name").empty() ? (str("type") == "ModuleScript" ? "ModuleScript" : "Script") : str("name"),
                                             NodeKind::Script);
        s->source = str("source");
        s->isModule = str("type") == "ModuleScript";
        SceneNode* made = m_scene->insert(std::move(s), parent);
        return {"Made " + label(made) + " inside " + label(parent) + "."};
    }
    if (name == "read_script") {
        SceneNode* n;
        if (!need("object", n)) return fail("No object called " + str("object"));
        if (!n->isScript()) return fail(label(n) + " isn't a script.");
        return {n->source.empty() ? "(empty)" : n->source};
    }
    if (name == "edit_script") {
        SceneNode* n;
        if (!need("object", n)) return fail("No object called " + str("object"));
        if (!n->isScript()) return fail(label(n) + " isn't a script.");
        if (args.contains("source")) { n->source = str("source"); return {"Replaced the source of " + label(n) + "."}; }
        std::string old = str("old");
        if (old.empty()) return fail("Give `old` and `new` (or `source`).");
        size_t at = n->source.find(old);
        if (at == std::string::npos) return fail("`old` isn't in the script (check spaces and line breaks).");
        if (n->source.find(old, at + 1) != std::string::npos) return fail("`old` appears more than once: include more of the lines around it.");
        n->source.replace(at, old.size(), str("new"));
        return {"Edited " + label(n) + "."};
    }
    if (name == "run_lua") return lua(str("code"));
    if (name == "get_output") {
        int want = std::clamp(args.value("lines", 40), 1, 400);
        const auto& e = Log::entries();
        std::string out;
        for (size_t i = e.size() > (size_t)want ? e.size() - (size_t)want : 0; i < e.size(); ++i)
            out += e[i].time + " " + (e[i].level == Log::Level::Error ? "ERROR: " : e[i].level == Log::Level::Warn ? "warning: " : "") + e[i].text + "\n";
        return {out.empty() ? "(the Output is empty)" : out};
    }
    if (name == "playtest") {
        bool start = str("action") == "start";
        if (start == m_playing) return {start ? "Already playtesting." : "Not playtesting."};
        togglePlay();
        return {start ? "Playtest started. Use get_output and screenshot to see how it goes, then stop it."
                      : "Stopped. The game is back how it was before Play."};
    }
    if (name == "screenshot") {
        AiToolResult r{"The 3D view right now."};
        r.png = m_viewport->snapshotPng(960, 540, true);
        if (r.png.empty()) return fail("Couldn't take a picture.");
        return r;
    }
    if (name == "select") {
        m_scene->deselect();
        int n = 0;
        if (args.contains("objects") && args["objects"].is_array())
            for (const auto& o : args["objects"])
                if (o.is_string())
                    if (SceneNode* f = aiFind(o.get<std::string>())) { m_scene->addToSelection(f); ++n; }
        return {n ? "Selected " + std::to_string(n) + " object(s)." : "Cleared the selection."};
    }
    if (name == "undo") {
        if (m_playing) return fail("Stop playtesting first.");
        trackChanges();
        if (m_undo.empty()) return fail("There's nothing to undo.");
        undo();
        return {"Undone."};
    }
    return fail("There's no tool called " + name + ".");
}
