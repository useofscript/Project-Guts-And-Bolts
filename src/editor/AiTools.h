#pragma once
#include <string>
#include <nlohmann/json.hpp>

// What AI helpers can do in Studio. The same tools serve Studio's Assistant tab
// and outside AI apps connected through MCP (the Model Context Protocol) such as
// Claude Desktop or Claude Code. Everything they change is undoable (Ctrl+Z).
struct AiToolResult {
    std::string text;          // what happened (or what went wrong)
    std::string png;           // a picture, for the screenshot tool
    bool        error = false;
};

namespace AiTools {
// Every tool: [{name, description, input_schema}] (the shape both MCP and Claude use).
const nlohmann::json& list();
// Instructions for an AI working in Studio (the Assistant's system prompt, MCP's server instructions).
const char* guide();
}
