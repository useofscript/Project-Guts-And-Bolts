#pragma once

// Per-computer settings (not saved with games): performance and graphics
// quality. Stored in settings.json next to the program.
struct GraphicsSettings {
    enum Quality { Low, Medium, High, Ultra, Custom };

    // Display
    bool  vsync       = true;   // match the monitor's refresh rate (no tearing)
    int   fpsCap      = 0;      // 0 = unlimited (only used when VSync is off)
    bool  showFps     = true;

    // Content
    bool  allowGore   = true;   // false hides blood / gore in every game

    // Graphics
    int   quality       = High;
    int   shadowRes     = 2048; // 1024 / 2048 / 4096
    int   shadowQuality = 2;    // 0 hard, 1 soft, 2 contact-hardening (PCSS)
    bool  ssao          = true; // ambient occlusion
    bool  bloom         = true;
    bool  fxaa          = true; // anti-aliasing
    bool  postFx        = true; // tone mapping, colour grading, vignette
    float renderScale   = 1.0f; // 0.5 = faster, 1.0 = sharp
    int   maxLights     = 32;   // point / spot lights drawn at once

    void applyPreset(int q);

    static GraphicsSettings& get();
    void load();
    void save() const;
};
