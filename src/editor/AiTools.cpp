// Studio's AI tools (see AiTools.h): what each one is for, written for an AI that
// has never seen Guts and Bolts before, and the guide it reads first. The tools
// themselves run in AiToolsRun.cpp.
#include "AiTools.h"

#include <string>
#include <vector>

using nlohmann::json;

namespace {

json schema(json props, std::vector<std::string> required) {
    return {{"type", "object"}, {"properties", std::move(props)}, {"required", required}, {"additionalProperties", false}};
}
const json kObject = {{"type", "string"},
                      {"description", "Which object. Best: its object_id like \"#42\" (stable, never ambiguous; every tool "
                                      "returns them). Also works: a path like \"Workspace.Castle.Door\"."}};

// How risky a tool is (spec: none < low < medium < high < critical).
enum class Risk { None, Low, Medium, High, Critical };
const char* riskName(Risk r) {
    switch (r) {
        case Risk::None: return "none";
        case Risk::Low: return "low";
        case Risk::Medium: return "medium";
        case Risk::High: return "high";
        default: return "critical";
    }
}

// One tool's description, in the same sections for every tool so an AI knows
// where to look: what it does, when (and when not) to pick it, what goes in and
// comes out, what must be true before and after, and what usually comes next.
struct Doc {
    const char* purpose;
    const char* use;        // user intents and phrases that mean this tool
    const char* dont;       // when another tool is the right one
    const char* inputs;
    const char* output;
    const char* before;     // preconditions
    const char* after;      // postconditions / side effects
    const char* workflow;   // what usually comes before and after
    const char* examples;   // requests that should pick this tool
};

struct Tool {
    const char* name;
    const char* title;
    const char* domain;     // SCENE, OBJECTS, SCRIPTING, RUNTIME, DEBUG, VIEW, HISTORY
    bool        readOnly;
    bool        destructive;
    bool        idempotent;
    Risk        risk;
    Doc         doc;
    json        input;
};

std::string describe(const Tool& t) {
    std::string s = t.doc.purpose;
    auto add = [&](const char* label, const char* text) {
        if (text && *text) s += std::string("\n\n") + label + ": " + text;
    };
    add("WHEN TO USE", t.doc.use);
    add("DO NOT USE", t.doc.dont);
    add("INPUTS", t.doc.inputs);
    add("OUTPUT", t.doc.output);
    add("PRECONDITIONS", t.doc.before);
    add("POSTCONDITIONS", t.doc.after);
    add("COMMON WORKFLOW", t.doc.workflow);
    add("EXAMPLES", t.doc.examples);
    s += std::string("\n\nDOMAIN: ") + t.domain + ". " + (t.readOnly ? "READ-ONLY (changes nothing)" : "CHANGES THE GAME") +
         (t.destructive ? ", DESTRUCTIVE" : "") + ". RISK: " + riskName(t.risk) + ".";
    return s;
}

const std::vector<Tool>& tools() {
    static const std::vector<Tool> all = {
        // ---------------------------------------------------------------- SCENE
        {"get_game_tree", "Show the game's object tree", "SCENE", true, false, true, Risk::None,
         {"Lists the objects in the game (the scene) as an indented tree: name, class and object_id (#number) for each. "
          "This is the real, current content of the game; never guess what is in it.",
          "Start of almost every task. \"What's in the game/scene?\", \"show me the map\", \"what objects are there\", "
          "\"find the door\" (when you don't know its name yet), before creating something to see where it should go.",
          "To read one object's properties use get_object. To search by name or class across a big game use find_objects.",
          "root (optional object, default the whole game: Workspace); depth (optional, levels to show, default 4).",
          "{tree: indented text, object_count, truncated}. Deeper levels are summarised as \"... N more inside\".",
          "None.", "Nothing changes.",
          "get_game_tree -> get_object / find_objects -> (change something) -> get_object to verify.",
          "\"What is in my game?\" \"Is there a spawn point?\" \"Show me what's inside the Castle model.\""},
         schema({{"root", kObject}, {"depth", {{"type", "integer"}, {"minimum", 0}, {"description", "How many levels deep (default 4)"}}}}, {})},
        {"find_objects", "Search for objects", "SCENE", true, false, true, Risk::None,
         {"Searches the whole game (or one part of it) for objects by name, class and/or tag, and returns their object_ids. "
          "Use the returned object_id in later calls: names can repeat, ids never do.",
          "\"Find all the barrels\", \"where are the scripts\", \"which parts are red/unanchored\" (find, then get_object), "
          "\"locate the enemy model\", any time you need the id of something you know by name.",
          "To see the overall layout use get_game_tree. To read one object's details use get_object.",
          "name (optional, case-insensitive text the name contains); class (optional, exact class like Part, Model, Script, "
          "PointLight, TextLabel); tag (optional, a CollectionService tag); under (optional object to search inside); "
          "limit (optional, default 50, max 500). Give at least one of name / class / tag.",
          "{matches: [{object_id, name, class, path}], count, truncated}.",
          "None.", "Nothing changes.",
          "find_objects -> get_object / diagnose_object -> set_property ...",
          "\"Find every part called Barrel.\" \"List all LocalScripts.\" \"Which objects are tagged Enemy?\""},
         schema({{"name", {{"type", "string"}}}, {"class", {{"type", "string"}}}, {"tag", {{"type", "string"}}}, {"under", kObject},
                 {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}}}}, {})},
        {"insert_object", "Create an object", "SCENE", false, false, false, Risk::Low,
         {"Creates a NEW object in the game and returns its object_id. kind is a class or a ready-made thing: Part, Sphere, "
          "Cylinder, Model, Folder, Script, LocalScript, ModuleScript, PointLight, SpotLight, Sound, SpawnLocation, "
          "TrussPart, Seat, Water, WaterSource, Attachment, ProximityPrompt, Highlight, Trail, Beam, Tool, Decal, Rig, "
          "RemoteEvent, RemoteFunction, IntValue/NumberValue/StringValue/BoolValue, ScreenGui, Frame, TextLabel, TextButton, "
          "TextBox, ImageLabel, ScrollingFrame, BillboardGui, SurfaceGui, UICorner, UIStroke, UIListLayout, BodyVelocity, "
          "AlignPosition, VectorForce..., and ready-made things by name: \"Kill Brick\", \"Coin\", \"Jump Pad\", \"Exploding Barrel\", "
          "\"Zombie\", \"Moving Platform\", \"Lamp Post\"... (the same list as Studio's Insert Object window). It only creates the object: "
          "it does not position it, colour it or give it behaviour.",
          "\"Create/add/spawn/place/insert/make a part (cube, ball, light, sound, button, script holder...)\".",
          "To change something that already exists, find it (get_game_tree / find_objects) and use set_property. "
          "For a script with code in it, create_script does both steps at once.",
          "kind (required, see above); parent (optional object to put it in, default Workspace); name (optional).",
          "{object_id, name, class, parent_id, resulting_state: the new object's key properties}.",
          "parent, if given, must exist. Scripts and character parts can't hold new objects (it goes in Workspace instead).",
          "The object exists, is selected in Studio, and has the class defaults (parts: anchored, 2x1x1 near the camera).",
          "insert_object -> set_property (Position, Size, Color, Anchored...) -> get_object to verify.",
          "\"Add a red ball.\" \"Put a light in the lamp.\" \"Make a button on the screen.\""},
         schema({{"kind", {{"type", "string"}}}, {"parent", kObject}, {"name", {{"type", "string"}}}}, {"kind"})},
        {"delete_object", "Delete an object", "SCENE", false, true, false, Risk::High,
         {"Deletes one object AND everything inside it. Can be undone with undo, but only while Studio stays open.",
          "Only when the person clearly asked to delete/remove/destroy/get rid of that specific thing.",
          "Never to \"clean up\" things that look unused or badly named, never to replace something (change it with "
          "set_property instead), never on a guess. If the request is ambiguous (\"remove the trees\" when there are "
          "many), ask first or list what you would delete.",
          "object (required).",
          "{deleted: {object_id, name, class}, affected_ids: it and everything inside it}.",
          "The object exists (inspect it first with get_object to be sure it's the right one). Workspace itself can't be deleted.",
          "The object and its descendants are gone. Scripts referring to it by name will no longer find it.",
          "find_objects / get_object (confirm) -> delete_object -> get_game_tree to verify.",
          "\"Delete the old wall.\" \"Remove Part #57.\""},
         schema({{"object", kObject}}, {"object"})},

        // -------------------------------------------------------------- OBJECTS
        {"get_object", "Read an object's properties", "OBJECTS", true, false, true, Risk::None,
         {"Returns everything about one object: object_id, class, path, parent, world position, and all saved "
          "properties (Position, Size, Color, Material, Anchored, CanCollide, Transparency, attributes, tags, script "
          "source...). While playtesting it also has runtime values (velocity, spin) under \"runtime\".",
          "Before changing an unfamiliar object, to answer \"what colour/size/where is X\", to check why something "
          "doesn't work, and AFTER a change to verify it took effect.",
          "To list many objects use get_game_tree / find_objects. For a ready-made list of likely problems use diagnose_object.",
          "object (required).",
          "{object_id, name, class, path, parent_id, children: [{object_id, name, class}], properties: {...}, runtime?}.",
          "The object exists.", "Nothing changes.",
          "find_objects -> get_object -> set_property -> get_object.",
          "\"How big is the platform?\" \"Is the door anchored?\" \"What's inside the Tool?\""},
         schema({{"object", kObject}}, {"object"})},
        {"set_property", "Change one property", "OBJECTS", false, false, true, Risk::Low,
         {"Changes ONE property of an existing object, exactly like a script would (obj.Property = value), then reads it "
          "back so you can see the value the engine really has. Common properties: Position [x,y,z], Size [x,y,z], "
          "Orientation [x,y,z] (degrees), Color [r,g,b] (0-1 each), Transparency (0-1), Anchored (false = falls with "
          "gravity in play), CanCollide, Material (\"Plastic\", \"Metal\", \"Neon\", \"Wood\", \"Glass\", \"Concrete\", \"Ice\"), "
          "Name, Parent (an object), Enabled, Brightness, Range, Text, Visible...",
          "\"Move/resize/recolour/rename X\", \"make it red/glow/see-through\", \"make it fall\" (Anchored false), "
          "\"make it solid / walk-through\" (CanCollide), \"put X inside Y\" (Parent).",
          "To create something new use insert_object. To change a script's code use edit_script. To change many objects "
          "at once, run_lua with a loop is faster.",
          "object (required); property (required, the Roblox-style name); value (required: number, true/false, text, or "
          "[x, y, z] for Vector3 / Color3).",
          "{object_id, changed_properties: {Property: {requested, now}}, resulting_state}. `now` is read back from the engine.",
          "The object exists and has that property (unknown properties give LUA_ERROR).",
          "The property is changed (undoable). Physics changes (Anchored...) only show their effect while playtesting.",
          "get_object -> set_property -> get_object / screenshot.",
          "\"Make the floor blue.\" \"Move the coin 5 units up.\" \"Let the crate fall.\""},
         schema({{"object", kObject}, {"property", {{"type", "string"}}},
                 {"value", {{"description", "number, boolean, string, or [x, y, z]"}}}}, {"object", "property", "value"})},
        {"select", "Select objects in Studio", "OBJECTS", false, false, true, Risk::None,
         {"Selects objects in Studio so the person can see them highlighted (or clears the selection). Doesn't change the game.",
          "\"Show me which one\", \"select the doors\", pointing the person at what you changed.",
          "Selection is only for the person; other tools never need it.",
          "objects (optional list of objects; empty or missing clears the selection).",
          "{selected: [object_id...], missing: [refs not found]}.", "None.", "The Studio selection changes; the game doesn't.",
          "find_objects -> select.", "\"Highlight the parts you just made.\""},
         schema({{"objects", {{"type", "array"}, {"items", kObject}}}}, {})},

        // ------------------------------------------------------------ SCRIPTING
        {"create_script", "Create a script with code", "SCRIPTING", false, false, false, Risk::Medium,
         {"Creates a Script, LocalScript or ModuleScript containing the Lua source you give. Script = runs on the game's "
          "server when the game starts. LocalScript = runs on each player's own computer (UI, input, camera; talks to "
          "Scripts with RemoteEvents). ModuleScript = only runs when require()d.",
          "When behaviour is needed that the engine's built-in objects and properties can't give (game rules, "
          "scoring, doors that open, enemies that chase), or when the person asks for a script.",
          "Don't write a script for things a property or built-in object already does (falling: Anchored = false; "
          "glowing: Material Neon or a PointLight; a sound: a Sound object). To change an existing script use edit_script.",
          "source (required, plain Lua 5.4: no Luau `+=`, `continue` or type annotations); parent (optional object, "
          "default Workspace; `script.Parent` is this object); name (optional); type (Script, LocalScript or ModuleScript).",
          "{object_id, name, class, parent_id, syntax: {ok, error?}}. A syntax error is reported but the script is still made.",
          "parent exists.", "The script exists. It does NOT run until a playtest starts (or the game is played).",
          "create_script -> diagnose_object (syntax) -> playtest start -> get_errors / get_output -> fix with edit_script -> playtest stop.",
          "\"Make the door open when touched.\" \"Add a score leaderboard.\""},
         schema({{"parent", kObject}, {"name", {{"type", "string"}}}, {"source", {{"type", "string"}}},
                 {"type", {{"type", "string"}, {"enum", {"Script", "LocalScript", "ModuleScript"}}}}}, {"source"})},
        {"read_script", "Read a script's code", "SCRIPTING", true, false, true, Risk::None,
         {"Returns the Lua source of a Script, LocalScript or ModuleScript.",
          "Before editing a script, when asked what a script does, when debugging an error that names a script.",
          "For non-script objects use get_object.",
          "object (required, a script).", "{object_id, class, source, lines}.", "The object is a script.", "Nothing changes.",
          "read_script -> edit_script -> diagnose_object.", "\"What does the KillBrick script do?\""},
         schema({{"object", kObject}}, {"object"})},
        {"edit_script", "Change a script's code", "SCRIPTING", false, false, false, Risk::Medium,
         {"Changes a script's code: replaces the text `old` with `new` (old must appear exactly once), or replaces "
          "the whole script with `source`. Checks the result for Lua syntax errors.",
          "Fixing a script error, changing what a script does, adding a feature to an existing script.",
          "To make a new script use create_script. Don't rewrite a whole script with `source` when a small `old`/`new` edit will do.",
          "object (required, a script); old + new (exact text swap) OR source (whole new code).",
          "{object_id, syntax: {ok, error?}}.",
          "The object is a script; `old` is copied exactly from read_script (spaces and line breaks matter).",
          "The script's source is changed (undoable). A running playtest keeps the old code until it is restarted.",
          "read_script -> edit_script -> playtest -> get_errors.", "\"Make the lava do 50 damage instead of 100.\""},
         schema({{"object", kObject}, {"old", {{"type", "string"}}}, {"new", {{"type", "string"}}},
                 {"source", {{"type", "string"}}}}, {"object"})},
        {"run_lua", "Run Lua now (Command Bar)", "SCRIPTING", false, false, false, Risk::Medium,
         {"Runs Lua code in Studio right now, like the Command Bar. Outside a playtest it changes the saved game "
          "(build things); during a playtest it runs inside the running game (runtime only: thrown away on stop). "
          "The full Roblox-style API is there: Instance.new, workspace, game:GetService, Effects, PathfindingService...",
          "Building or changing many things at once (a staircase, a grid of coins, recolour every part), reading values "
          "no other tool gives, testing something during a playtest.",
          "For one property use set_property; for one object use insert_object (they verify and report ids for you). "
          "Don't use it to fake physics or movement by teleporting parts every frame: use Anchored = false, forces and "
          "the engine's own systems.",
          "code (required, plain Lua 5.4).",
          "{output: what it printed, errors: [...]}. print() whatever you need to know back (e.g. print(part:GetFullName())).",
          "None. Code that waits (wait(), events) only keeps running during a playtest.",
          "Whatever the code did (undoable when not playtesting).",
          "run_lua -> get_game_tree / screenshot to verify.", "\"Build a 10-step staircase.\" \"Make every part in Trees green.\""},
         schema({{"code", {{"type", "string"}}}}, {"code"})},

        // -------------------------------------------------------------- RUNTIME
        {"playtest", "Start or stop a playtest", "RUNTIME", false, false, true, Risk::Medium,
         {"Starts or stops playtesting (Studio's Play / Stop). While playing, scripts run, physics simulates "
          "(unanchored parts fall, explosions push) and a character walks around. Stopping puts the whole game back "
          "exactly how it was before Play.",
          "\"Test it\", \"run the game\", \"try it out\", \"play\", \"does it work?\", to see scripts or physics in action, "
          "and \"stop\" when you have seen enough.",
          "Don't keep a playtest running while you build: changes made during a playtest are lost when it stops.",
          "action (required: start or stop).",
          "{playing: true/false}.",
          "None.", "start: the game is running. stop: the saved game is back exactly as before Play.",
          "playtest start -> get_output / get_errors / screenshot / get_object (runtime) -> playtest stop -> fix -> repeat.",
          "\"Play the game and tell me if the door works.\""},
         schema({{"action", {{"type", "string"}, {"enum", {"start", "stop"}}}}}, {"action"})},

        // ---------------------------------------------------------------- DEBUG
        {"get_output", "Read the Output window", "DEBUG", true, false, true, Risk::None,
         {"The latest lines of Studio's Output window: print() lines, warnings, script errors and Studio messages, with times.",
          "After running code or playtesting, to see what scripts printed.",
          "To see only problems use get_errors.",
          "lines (optional, default 40, max 400).", "{lines: [{time, level, text}]}.", "None.", "Nothing changes.",
          "playtest start -> get_output.", "\"What did the script print?\""},
         schema({{"lines", {{"type", "integer"}, {"minimum", 1}, {"maximum", 400}, {"description", "How many (default 40)"}}}}, {})},
        {"get_errors", "Read errors and warnings", "DEBUG", true, false, true, Risk::None,
         {"Only the errors (and warnings) from the Output window: script errors with their line numbers, failed loads, "
          "warn() calls.",
          "\"Why doesn't it work?\", \"is anything broken?\", after every playtest and every script change.",
          "For everything printed use get_output.",
          "lines (optional, how many recent Output lines to look through, default 200); include_warnings (optional, default true).",
          "{errors: [{time, text}], warnings: [{time, text}], error_count, warning_count}.", "None.", "Nothing changes.",
          "playtest start -> get_errors -> read_script -> edit_script.", "\"Are there any script errors?\""},
         schema({{"lines", {{"type", "integer"}, {"minimum", 1}, {"maximum", 2000}}}, {"include_warnings", {{"type", "boolean"}}}}, {})},
        {"diagnose_object", "Find problems with an object", "DEBUG", true, false, true, Risk::None,
         {"Inspects one object (and what's inside it) and lists likely problems with a suggested fix for each: "
          "anchored parts that are expected to fall, unanchored parts with CanCollide off (fall through the floor), "
          "invisible parts, Lua syntax errors, disabled scripts, constraints/beams/trails missing their attachments, "
          "lights/sounds/prompts/decals not inside a part, tools without a Handle, UI not inside a ScreenGui...",
          "\"Why doesn't X fall/move/show/work?\", \"check this\", and after building something, to validate it.",
          "For the whole game at once use validate_scene. It reports problems; it does not fix them.",
          "object (required); recursive (optional, also check everything inside, default true).",
          "{object_id, findings: [{object_id, severity: error|warning|info, code, message, fix}], error_count, warning_count}.",
          "The object exists.", "Nothing changes.",
          "find_objects -> diagnose_object -> set_property / edit_script -> diagnose_object again.",
          "\"The barrel doesn't fall.\" \"Why can't I see my sign?\""},
         schema({{"object", kObject}, {"recursive", {{"type", "boolean"}}}}, {"object"})},
        {"validate_scene", "Check the whole game for problems", "DEBUG", true, false, true, Risk::None,
         {"Runs diagnose_object over the whole game and adds up the results: script syntax errors, broken constraints, "
          "misplaced objects, invisible or falling-through parts, plus recent errors in the Output.",
          "\"Check my game\", \"is everything set up right?\", before and after big changes, before publishing.",
          "For one object use diagnose_object.",
          "include_info (optional, also list harmless info notes, default false); limit (optional, most findings to list, default 100).",
          "{object_count, script_count, findings: [...], error_count, warning_count, recent_output_errors}.",
          "None.", "Nothing changes.", "validate_scene -> fix -> validate_scene.", "\"Is my game ready to publish?\""},
         schema({{"include_info", {{"type", "boolean"}}}, {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}}}, {})},

        // ----------------------------------------------------------------- VIEW
        {"screenshot", "Take a picture of the 3D view", "VIEW", true, false, true, Risk::None,
         {"A picture of what Studio's 3D view shows right now (the editor camera, or the game camera while playtesting).",
          "To see what something looks like, to check a build visually, while playtesting to watch what happens.",
          "Don't use it to find objects or read properties: get_game_tree / get_object give exact data.",
          "None.", "An image plus {width, height}.", "None.", "Nothing changes.",
          "(change) -> screenshot.", "\"Show me what it looks like.\""},
         schema(json::object(), {})},

        // -------------------------------------------------------------- HISTORY
        {"undo", "Undo the last change", "HISTORY", false, false, false, Risk::Low,
         {"Undoes the last change to the game (like Ctrl+Z): a tool call, a script edit, a run_lua build...",
          "When a change you made was wrong or the person says \"undo that\" / \"put it back\".",
          "Doesn't work during a playtest (stop it first; stopping already throws away playtest changes).",
          "None.", "{undone: true, undo_steps_left}.", "Not playtesting, and there is something to undo.",
          "The game is back to how it was one step earlier.", "set_property -> (wrong) -> undo -> try again.",
          "\"Undo that.\""},
         schema(json::object(), {})},
    };
    return all;
}

} // namespace

namespace AiTools {

const char* guide() {
    return R"GUIDE(# Guts and Bolts Studio assistant

You are an AI assistant working INSIDE Guts and Bolts Studio, a game engine (Roblox-like: parts, models, Lua scripts, physics, characters) through its tools. Guts and Bolts is a game engine, not a file editor: the tools act directly on the real game that is open in Studio. Treat every tool as a real engine operation, never as an example.

Your loop: UNDERSTAND the request -> INSPECT the game -> PLAN -> ACT with tools -> VALIDATE the result -> REPORT what really happened.

## Core behaviour
- When asked to change the game, do it with the tools. Don't just explain how the person could do it themselves when a tool can.
- Look before you change: get_game_tree / find_objects / get_object show what really exists. Never invent object names, ids, classes, properties, scene contents or results.
- Refer to objects by object_id ("#42"). Names can repeat; ids never do. Every tool returns ids.
- Prefer small, checkable steps. After an important change, verify it (get_object, diagnose_object, screenshot).
- Never say something worked unless the tool returned "success": true. Tool results are JSON; read them.

## Picking tools (by what the person wants, not keywords)
- "What's in the game?" -> get_game_tree (never guess).
- "Make a red ball" -> insert_object Sphere, then set_property Color / Position.
- "Make it fall" / "add physics" / "make it react to forces" -> set_property Anchored = false (parts are anchored by default), check CanCollide is true, then playtest to see it.
- "Why doesn't it move/work?" -> diagnose_object first, then fix, then check again.
- "Make enemies walk to the player" -> the engine's PathfindingService / humanoid:PathfindTo in a Script, not hand-made movement code.
- "Make an explosion" -> the engine's Explosion (Instance.new("Explosion")) in a script or run_lua during a playtest, not moving parts by hand.
- Many similar changes at once -> run_lua with a loop.
- "Test it" -> playtest start, then get_errors / get_output / screenshot, then playtest stop.

## Use the engine's own systems
- Physics: Anchored = false, forces (part:ApplyImpulse, BodyVelocity, VectorForce...), constraints. DON'T fake physics by teleporting parts every frame.
- Navigation: PathfindingService and humanoid:PathfindTo. DON'T write your own path-walking code when it can do the job.
- Explosions: the Explosion object / Explode(). DON'T push every nearby part by hand.
- Only write scripts for behaviour the built-in objects and properties can't give, or when the person asks for code.

## Editor vs playtest
- Outside a playtest you edit the saved game. Physics doesn't run; scripts don't run.
- During a playtest (playtest start) scripts run and physics simulates. Everything that changes during a playtest is THROWN AWAY when it stops. Build outside playtests.

## Errors
When a tool fails, read error.code / message / suggested_action, fix the cause (wrong id, missing object, wrong order) and try a changed call. Don't repeat the same failing call unchanged. If it can't be done, say exactly what failed.

## Safety
- Each tool says its RISK. delete_object is HIGH: only delete what the person clearly asked to delete. Never delete things because they look unused or oddly named.
- If a destructive request is ambiguous ("remove the trees" when there are 40 trees and some are in a model), ask or list what you'd remove first.
- Everything can be undone with undo (outside playtests).

## Don't invent capabilities
A request mentioning something doesn't mean Guts and Bolts has it. Only use the tools you were given and the engine API that exists. Don't make up tools, classes, properties, paths or features. If no tool can do something, say the current tools can't do it.

## Quick facts
Scripts are Lua 5.4 with a Roblox-style API (Instance.new, part.Touched, game:GetService, RunService.Heartbeat, wait(), Players.LocalPlayer, humanoid:MoveTo, PathfindingService, Effects.Explosion/Blood, Sounds.Play). Luau-only syntax (`+=`, type annotations, `continue`) does not work: write plain Lua. Y is up. 1 unit = 2 Roblox studs; a character is about 2.6 units tall and walks 6 units a second.

## Talking to the person
Keep it short and friendly: they may be new to making games. Say what changed, not every tool call. If something couldn't be done, say what and why.)GUIDE";
}

const json& list() {
    static const json out = [] {
        json a = json::array();
        for (const Tool& t : tools()) a.push_back({{"name", t.name}, {"description", describe(t)}, {"input_schema", t.input}});
        return a;
    }();
    return out;
}

const json& mcpTools() {
    static const json out = [] {
        json a = json::array();
        for (const Tool& t : tools())
            a.push_back({{"name", t.name}, {"title", t.title}, {"description", describe(t)}, {"inputSchema", t.input},
                         {"annotations", {{"title", t.title}, {"readOnlyHint", t.readOnly}, {"destructiveHint", t.destructive},
                                          {"idempotentHint", t.idempotent}, {"openWorldHint", false}}},
                         {"_meta", {{"gutsbolts/domain", t.domain}, {"gutsbolts/read_only", t.readOnly},
                                    {"gutsbolts/mutates_state", !t.readOnly}, {"gutsbolts/destructive", t.destructive},
                                    {"gutsbolts/risk_level", riskName(t.risk)}}}});
        return a;
    }();
    return out;
}

} // namespace AiTools
