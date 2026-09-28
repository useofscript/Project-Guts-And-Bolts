#pragma once

// The currently active manipulation tool, shared between the toolbar (which
// sets it) and the viewport (which draws the matching gizmo).
enum class GizmoTool { Select, Translate, Rotate, Scale };

// Editor-wide UI state shared across panels.
struct EditorState {
    GizmoTool tool        = GizmoTool::Translate;
    bool      gizmoLocal  = true;   // gizmo orientation: local vs. world space
    bool      snapEnabled = false;

    // "Connect" tool: click one part, then another, to join them.
    int   connectTool = -1;         // ConstraintType, or -1 when off; 5 = hinge + motor
    unsigned long long connectFirst = 0;
    float connectPoint[3] = {0, 0, 0};

    float snapTranslate = 0.5f;     // world units
    float snapRotate    = 15.0f;    // degrees
    float snapScale     = 0.25f;    // factor
};
