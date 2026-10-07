#pragma once
#include <string>
#include <nlohmann/json.hpp>

// What AI helpers can do in Studio. The same tools serve Studio's Assistant tab
// and outside AI apps connected through MCP (the Model Context Protocol) such as
// Claude Desktop or Claude Code. Everything they change is undoable (Ctrl+Z).
//
// Every tool answers with structured JSON (`data`), so an AI can check what really
// happened instead of guessing:
//   {"success": true, "operation": "set_property", "object_id": "#42", "resulting_state": {...}, "warnings": [...]}
//   {"success": false, "operation": "...", "error": {"code": "OBJECT_NOT_FOUND", "message": "...",
//    "recoverable": true, "suggested_action": "..."}}
struct AiToolResult {
    std::string    text;          // what happened (the JSON below, written out)
    std::string    png;           // a picture, for the screenshot tool
    bool           error = false;
    nlohmann::json data;          // the same result as structured JSON
};

namespace AiTools {
// Every tool: [{name, description, input_schema}] (the shape Claude's API uses).
const nlohmann::json& list();
// The same tools for MCP: {name, title, description, inputSchema, annotations, _meta}.
// annotations say which tools only look (readOnlyHint) and which delete things
// (destructiveHint); _meta has the engine area ("gutsbolts/domain") and a risk level.
const nlohmann::json& mcpTools();
// Instructions for an AI working in Studio (the Assistant's system prompt, MCP's server instructions).
const char* guide();
}
