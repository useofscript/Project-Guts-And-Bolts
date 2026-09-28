#pragma once
#include <vector>

// The currently active manipulation tool, shared between the toolbar (which
// sets it) and the viewport (which draws the matching gizmo).
enum class GizmoTool { Select, Translate, Rotate, Scale };

// What Studio is doing right now, a bit like Blender's modes:
//   Build    - normal editing: place, move and change objects
//   Modeling - reshape one part's mesh: its corners, edges and faces
//   Simulate - physics and scripts run live, you fly the camera around
//   Play     - a real playtest with your character
enum class StudioMode { Build, Modeling, Simulate, Play };
inline const char* const kStudioModeNames[4] = {"Build", "Modeling", "Simulate", "Play"};

// Modeling mode: which part is being reshaped and what's picked on it.
struct ModelingState {
    unsigned long long node = 0;
    int               selectMode = 0;   // 0 = vertices, 1 = edges, 2 = faces
    std::vector<char> sel;              // one flag per vertex
    bool              xray = false;     // show (and pick) the back too
};

// Editor-wide UI state shared across panels.
struct EditorState {
    GizmoTool tool        = GizmoTool::Translate;
    bool      gizmoLocal  = true;   // gizmo orientation: local vs. world space
    bool      snapEnabled = false;

    StudioMode    mode = StudioMode::Build;
    ModelingState modeling;
    bool          simPaused = false;  // Simulate / Play: physics and scripts frozen
    bool          simStep   = false;  // run one frame while paused

    // "Connect" tool: click one part, then another, to join them.
    int   connectTool = -1;         // ConstraintType, or -1 when off; 5 = hinge + motor
    unsigned long long connectFirst = 0;
    float connectPoint[3] = {0, 0, 0};

    float snapTranslate = 0.5f;     // world units
    float snapRotate    = 15.0f;    // degrees
    float snapScale     = 0.25f;    // factor
};
