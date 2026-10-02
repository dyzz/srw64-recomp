package org.srw64.game;

import android.content.pm.ActivityInfo;
import android.os.Build;
import android.os.Bundle;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.system.ErrnoException;
import android.system.Os;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.util.ArrayList;

// The game: SDL3's Java glue loads SDL3, sdl2-compat (libSDL2.so) and libmain.so, then
// runs the host's main with the arguments srw64.sh passes on Linux.
public class SRW64Activity extends SDLActivity {
    @Override
    protected void onCreate(Bundle state) {
        try {
            // bundled_resource() reads this (src/native/app/runtime.cpp).
            Os.setenv("SRW64_RESOURCE_DIR", SetupActivity.resourceDir(this).getPath(), true);
            // HOME is /data, which an app cannot write; RT64 keeps its settings under it.
            File home = new File(getFilesDir(), "home");
            home.mkdirs();
            Os.setenv("HOME", home.getPath(), true);
            Os.setenv("TMPDIR", getCacheDir().getPath(), true);
            // Development: `am start ... --ez debug true` opens the debug interface
            // (src/host/debug_server.cpp, tools/release/android/attach.py).
            if (getIntent().getBooleanExtra("debug", false)) {
                Os.setenv("SRW64_DEBUG", "1", true);
                // With it, `--esa env KEY=VALUE,...` sets development variables (SRW64_* only).
                String[] variables = getIntent().getStringArrayExtra("env");
                if (variables != null) {
                    for (String variable : variables) {
                        int equals = variable.indexOf('=');
                        if (equals > 0 && variable.startsWith("SRW64_"))
                            Os.setenv(variable.substring(0, equals), variable.substring(equals + 1), true);
                    }
                }
            }
        } catch (ErrnoException error) {
            throw new RuntimeException(error);
        }
        super.onCreate(state);
    }

    // Immersive: no status or navigation bar over the game; a swipe shows them for a moment.
    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus && Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
    }

    // Landscape only: SDL lets a resizable window take any orientation, overriding the manifest.
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "SDL2", "main" };
    }

    @Override
    protected String getMainFunction() {
        return "main";
    }

    @Override
    protected String[] getArguments() {
        File user = new File(getFilesDir(), "user");
        ArrayList<String> arguments = new ArrayList<>();
        arguments.add("--play");
        arguments.add("--rom");
        arguments.add(SetupActivity.romFile(this).getPath());
        arguments.add("--user-dir");
        arguments.add(user.getPath());
        if (!new File(user, "presentation.json").isFile()) {
            arguments.add("--language");
            arguments.add("zh-Hans");
        }
        String[] extra = getIntent().getStringArrayExtra("args");
        if (extra != null) for (String argument : extra) arguments.add(argument);
        // The Seeker's Mali-G615 MC2 is the target (docs/design/android-port.md): 2x.
        if (!arguments.contains("--resolution-scale")) {
            arguments.add("--resolution-scale");
            arguments.add("2");
        }
        return arguments.toArray(new String[0]);
    }
}
