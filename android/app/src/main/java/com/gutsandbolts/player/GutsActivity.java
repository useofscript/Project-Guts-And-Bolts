package com.gutsandbolts.player;

import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Bundle;

import org.libsdl.app.SDLActivity;

// The Android app is SDL's activity running our C++ Player (libmain.so).
public class GutsActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", "main" };
    }

    // gutsandbolts://play/... links (the website's Play button) that opened the app.
    private static volatile String sLaunchLink = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        keepLink(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        keepLink(intent);
    }

    private static void keepLink(Intent intent) {
        if (intent != null && intent.getData() != null) sLaunchLink = intent.getDataString();
    }

    // Called from C++ (LaunchLink::poll): the newest link, once.
    public static String takeLaunchLink() {
        String link = sLaunchLink;
        sLaunchLink = null;
        return link;
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
