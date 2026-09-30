#pragma once
#include <string>
#include "../net/Socket.h"

class Editor;

// Lets AI apps on this computer (Claude Desktop, Claude Code, Cursor, anything
// that speaks MCP, the Model Context Protocol) use Studio's tools: look at the
// game, build, write scripts, playtest, take screenshots. Off until you switch
// it on in the Assistant tab. Only programs on this computer can connect.
//
// Two ways in:
//  * HTTP: http://127.0.0.1:<port>/mcp (e.g. `claude mcp add --transport http guts-and-bolts http://127.0.0.1:44755/mcp`)
//  * stdio: the app runs `GutsAndBolts --mcp`, which passes messages on to the Studio that's open.
class StudioMcp {
public:
    static constexpr int kDefaultPort = 44755;

    bool start(int port, std::string& error);
    void stop();
    bool running() const { return m_http.isOpen(); }
    int  port() const { return m_port; }
    void update(Editor& editor);                     // every frame: answer what's arrived
    int  callsServed() const { return m_calls; }
    const std::string& lastTool() const { return m_lastTool; }

    // `GutsAndBolts --mcp`: pass stdin <-> the running Studio until stdin closes.
    static int runStdioBridge(int port);

private:
    std::string handle(Editor& editor, const std::string& body, bool& anyReply);
    Net::HttpServer m_http;
    int         m_port = kDefaultPort;
    int         m_calls = 0;
    std::string m_lastTool;
};
