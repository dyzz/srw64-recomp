package org.srw64.game;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import java.io.File;
import java.io.IOException;
import java.util.List;

// A game that crashes while it starts drawing starts the next time with the framebuffer
// sync off: the Adreno 6xx drivers crash in its compute dispatches (Adreno 640/650,
// issue #7; rt64_android_patches.py), and a GPU no one has tried yet should not need a
// new build to start. The game writes a marker before it starts and the render thread
// deletes it after 300 display lists (src/host/stall_watchdog.cpp); a marker still there
// at the next start, after a crash where Android keeps the reason (11 and later), turns
// the sync off for this version of the game. A new version tries again.
final class GraphicsGuard {
    private static final String PREFS = "graphics_guard";
    private static final int STARTED_FRAMES = 300;

    private GraphicsGuard() { }

    // Before the game starts: sets SRW64_FRAMEBUFFER_SYNC=0 when an earlier start crashed,
    // and SRW64_START_MARKER for the render thread.
    static void prepare(Context context) throws ErrnoException {
        File marker = new File(context.getFilesDir(), "render-start");
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        long version = version(context);
        if (prefs.getLong("sync_off_version", -1) != version && marker.exists() && crashed(context)) {
            prefs.edit().putLong("sync_off_version", version).apply();
            Log.w("SRW64", "SRW64_GRAPHICS_GUARD the last start crashed before drawing " + STARTED_FRAMES
                    + " display lists: framebuffer sync off for this version");
        }
        if (prefs.getLong("sync_off_version", -1) == version)
            Os.setenv("SRW64_FRAMEBUFFER_SYNC", "0", true);
        try {
            marker.createNewFile();
            Os.setenv("SRW64_START_MARKER", marker.getPath(), true);
        } catch (IOException error) {
            Log.w("SRW64", "SRW64_GRAPHICS_GUARD no start marker: " + error);
        }
    }

    // Whether the app's last process ended in a crash; before Android 11 there is no
    // record, and a marker left behind counts as one.
    private static boolean crashed(Context context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return true;
        ActivityManager manager = context.getSystemService(ActivityManager.class);
        List<ApplicationExitInfo> exits = manager.getHistoricalProcessExitReasons(context.getPackageName(), 0, 1);
        if (exits.isEmpty()) return false;
        int reason = exits.get(0).getReason();
        return reason == ApplicationExitInfo.REASON_CRASH_NATIVE || reason == ApplicationExitInfo.REASON_CRASH;
    }

    private static long version(Context context) {
        try {
            return context.getPackageManager().getPackageInfo(context.getPackageName(), 0).getLongVersionCode();
        } catch (PackageManager.NameNotFoundException error) {
            return 0;
        }
    }
}
