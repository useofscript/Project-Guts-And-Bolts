package com.gutsandbolts.player;

import org.libsdl.app.SDLActivity;

// The Android app is SDL's activity running our C++ Player (libmain.so).
public class GutsActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", "main" };
    }
}
