package com.gutsandbolts.player;

import android.content.pm.ActivityInfo;

import org.libsdl.app.SDLActivity;

// The Android app is SDL's activity running our C++ Player (libmain.so).
public class GutsActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", "main" };
    }

    // Called from C++: games are landscape; the site follows how you hold the phone.
    public static void setGameOrientation(final boolean landscape) {
        final SDLActivity activity = mSingleton;
        if (activity == null) return;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                activity.setRequestedOrientation(landscape
                        ? ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                        : ActivityInfo.SCREEN_ORIENTATION_FULL_USER);
            }
        });
    }
}
