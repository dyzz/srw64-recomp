package org.srw64.game;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;

// A start in a new process (ProcessPhoenix's way), from a process of its own
// (AndroidManifest.xml): the host runs once per process, so after the game closed for an HD
// import (SRW64Activity.importHdAfterQuit) its process ends here and the launcher opens in
// a fresh one, where SetupActivity.importHd may swap the pack.
public class Relaunch extends Activity {
    private static final String PID = "org.srw64.game.PID", NEXT = "org.srw64.game.NEXT";

    static void start(Activity from, Intent next) {
        from.startActivity(new Intent(from, Relaunch.class)
                .putExtra(PID, Process.myPid())
                .putExtra(NEXT, next)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        // Never -1, which signals every process of this app, this one too.
        int pid = getIntent().getIntExtra(PID, 0);
        if (pid > 0 && pid != Process.myPid()) Process.killProcess(pid);
        Intent next = getIntent().getParcelableExtra(NEXT);
        if (next != null) startActivity(next.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
        finish();
        Runtime.getRuntime().exit(0);
    }
}
