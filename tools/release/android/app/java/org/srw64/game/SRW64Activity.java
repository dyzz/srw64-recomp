package org.srw64.game;

import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.system.ErrnoException;
import android.system.Os;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.ArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

// The game: SDL3's Java glue loads SDL3, sdl2-compat (libSDL2.so) and libmain.so, then
// runs the host's main with the arguments srw64.sh passes on Linux.
public class SRW64Activity extends SDLActivity {
    // The host is not re-entrant and runs once per process; the launcher will not swap
    // the HD folder under it (SetupActivity.importHd).
    static volatile boolean running;
    private static final int EXPORT_DOCUMENT = 6401, PICK_FOLDER = 6402;
    // The player's folder (PlayerFolder): one thread for every transfer; the saves go out
    // every few seconds when they changed and when the game leaves the screen.
    private final ExecutorService folderSync = Executors.newSingleThreadExecutor();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private volatile String savesSent = "", logsSent = "";
    private int ticks;
    private final Runnable savesTick = new Runnable() {
        @Override public void run() {
            queueSaves();
            // The log every 10 seconds: it grows all the time, and a stuck game's thread dumps
            // (written 5 seconds into a stall) should reach logs/ without a restart.
            if (++ticks % 3 == 0) queueLogs();
            handler.postDelayed(this, 3000);
        }
    };
    // A pending export is immutable, independent of saves changing while it is written.
    private static final class Export {
        final String kind, name;
        final PublicExports.Writer writer;
        volatile int status = 1; // pending, saved, cancelled, failed
        volatile String location = "";
        Export(String kind, String name, PublicExports.Writer writer) {
            this.kind = kind; this.name = name; this.writer = writer;
        }
    }
    private volatile Export saveExport, reportExport;
    private Export pendingDocument;

    public int saveExportStatus() { Export job = saveExport; return job == null ? 4 : job.status; }
    public String saveExportLocation() { Export job = saveExport; return job == null ? "" : job.location; }
    public int reportExportStatus() { Export job = reportExport; return job == null ? 4 : job.status; }
    public String reportExportLocation() { Export job = reportExport; return job == null ? "" : job.location; }

    private boolean exporting() { return saveExportStatus() == 1 || reportExportStatus() == 1; }

    // Called from the SDL thread after all four emulator formats have been written.
    public boolean exportSaves(String path) {
        if (exporting()) return false;
        try {
            File directory = new File(path);
            if (!directory.getCanonicalFile().equals(new File(SetupActivity.userDir(this), "saves/export").getCanonicalFile()))
                return false;
            PlayerFolder folder = PlayerFolder.open(this);
            if (folder != null) {
                // The four files as they are, into the folder's export/.
                File[] files = new File[SaveExport.NAMES.length];
                for (int i = 0; i < files.length; ++i) files[i] = new File(directory, SaveExport.NAMES[i]);
                saveExport = new Export("export", "", null);
                toFolder(saveExport, () -> folder.send("export", files));
                return true;
            }
            SaveExport snapshot = new SaveExport(directory);
            String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss-SSS", Locale.ROOT).format(new Date());
            saveExport = new Export("export", "srw64-saves-" + stamp + ".zip", snapshot::write);
            startExport(saveExport);
            return true;
        } catch (IOException | RuntimeException error) { return false; }
    }

    public boolean exportReport(String path) {
        if (exporting()) return false;
        try {
            File report = new File(path).getCanonicalFile();
            if (!report.isFile() || !report.getName().endsWith(".zip") ||
                !report.getParentFile().equals(new File(SetupActivity.userDir(this), "reports").getCanonicalFile()))
                return false;
            PlayerFolder folder = PlayerFolder.open(this);
            if (folder != null) {
                reportExport = new Export("reports", report.getName(), null);
                toFolder(reportExport, () -> folder.send("reports", report));
                return true;
            }
            // bug_report writes unique, closed archives; no live log is read here.
            reportExport = new Export("reports", report.getName(), stream -> Files.copy(report.toPath(), stream));
            startExport(reportExport);
            return true;
        } catch (IOException | RuntimeException error) { return false; }
    }

    private interface Send { String run() throws IOException; }
    private void toFolder(Export job, Send send) {
        folderSync.execute(() -> {
            try { job.location = send.run(); job.status = 2; }
            catch (IOException | RuntimeException error) { Log.w("SRW64", "player folder: " + error); job.status = 4; }
        });
    }

    // --- The player's folder, for the settings (src/native/ui/frontend.cpp, over JNI) ---

    // Where it is (Documents/Marchwind64), or empty without one.
    public String playerFolder() {
        PlayerFolder folder = PlayerFolder.open(this);
        return folder == null ? "" : folder.display();
    }

    // The Saves page's "Choose folder": the system picker; the answer comes to onActivityResult.
    public boolean choosePlayerFolder() {
        runOnUiThread(() -> {
            try { startActivityForResult(PlayerFolder.pick(), PICK_FOLDER); }
            catch (ActivityNotFoundException | SecurityException error) { Log.w("SRW64", "player folder: " + error); }
        });
        return true;
    }

    // Before the import list is read, and before the bezel or filter picker opens.
    public boolean syncImport() { return await(folder -> folder.incoming(SetupActivity.userDir(this))); }
    public boolean syncLooks() { return await(folder -> folder.looks(SetupActivity.userDir(this))); }

    private interface Task { void run(PlayerFolder folder) throws IOException; }
    private boolean await(Task task) {
        PlayerFolder folder = PlayerFolder.open(this);
        if (folder == null) return false;
        Future<?> job = folderSync.submit(() -> { task.run(folder); return null; });
        try { job.get(20, TimeUnit.SECONDS); return true; }
        catch (Exception error) { Log.w("SRW64", "player folder: " + error); return false; }
    }

    private void queueSaves() {
        folderSync.execute(() -> {
            PlayerFolder folder = PlayerFolder.open(this);
            if (folder == null) return;
            File user = SetupActivity.userDir(this);
            String now = PlayerFolder.fingerprint(user);
            if (now.equals(savesSent)) return;
            try { folder.saves(user); savesSent = now; }
            catch (IOException | RuntimeException error) { Log.w("SRW64", "player folder: " + error); }
        });
    }

    private void queueLogs() {
        folderSync.execute(() -> {
            PlayerFolder folder = PlayerFolder.open(this);
            if (folder == null) return;
            File user = SetupActivity.userDir(this);
            String now = PlayerFolder.logFingerprint(user);
            if (now.equals(logsSent)) return;
            try { folder.logs(user); logsSent = now; }
            catch (IOException | RuntimeException error) { Log.w("SRW64", "player folder: " + error); }
        });
    }

    @Override
    protected void onResume() {
        super.onResume();
        handler.removeCallbacks(savesTick);
        handler.post(savesTick);
        // Back from a file manager: what the player put in import/, filters/ or bezels/.
        folderSync.execute(() -> {
            PlayerFolder folder = PlayerFolder.open(this);
            if (folder == null) return;
            try { folder.incoming(SetupActivity.userDir(this)); folder.looks(SetupActivity.userDir(this)); }
            catch (IOException | RuntimeException error) { Log.w("SRW64", "player folder: " + error); }
        });
    }

    @Override
    protected void onPause() {
        handler.removeCallbacks(savesTick);
        queueSaves();
        queueLogs();
        super.onPause();
    }

    private void startExport(Export job) {
        if (Build.VERSION.SDK_INT >= 29) {
            new Thread(() -> {
                try {
                    job.location = PublicExports.write(this, job.kind, job.name, job.writer);
                    job.status = 2;
                } catch (IOException | RuntimeException error) { job.status = 4; }
            }, "srw64-public-export").start();
        } else {
            // Android 9: SAF also works without any storage permission.
            runOnUiThread(() -> {
                pendingDocument = job;
                Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("application/zip");
                intent.putExtra(Intent.EXTRA_TITLE, job.name);
                try { startActivityForResult(intent, EXPORT_DOCUMENT); }
                catch (ActivityNotFoundException | SecurityException error) {
                    pendingDocument = null;
                    job.status = 4;
                }
            });
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (request == PICK_FOLDER) {
            final Uri tree = result == RESULT_OK && data != null ? data.getData() : null;
            if (tree == null) return;
            folderSync.execute(() -> {
                try { PlayerFolder.remember(this, tree).everything(SetupActivity.userDir(this)); savesSent = ""; }
                catch (IOException | RuntimeException error) { Log.w("SRW64", "player folder: " + error); }
            });
            return;
        }
        if (request != EXPORT_DOCUMENT) { super.onActivityResult(request, result, data); return; }
        final Export job = pendingDocument;
        pendingDocument = null;
        if (job == null) return;
        if (result != RESULT_OK) { job.status = 3; return; }
        if (data == null || data.getData() == null) { job.status = 4; return; }
        final Uri uri = data.getData();
        new Thread(() -> {
            try (OutputStream stream = getContentResolver().openOutputStream(uri, "wt")) {
                if (stream == null) throw new IOException("Cannot open export destination");
                job.writer.write(stream);
            } catch (IOException | RuntimeException error) {
                try { DocumentsContract.deleteDocument(getContentResolver(), uri); }
                catch (Exception ignored) { }
                job.status = 4;
                return;
            }
            job.location = uri.toString();
            job.status = 2;
        }, "srw64-document-export").start();
    }

    @Override
    protected void onCreate(Bundle state) {
        running = true;
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
        // Before the first layout, so the game's surface starts at its full size: hidden only
        // on focus, the bars went after the swap chain was made, the surface grew and its
        // buffers were allocated again, where two Redmi K60s (Adreno 730, MIUI 14) hung for
        // good in Qualcomm's gralloc (docs/design/android-port.md).
        hideSystemBars();
        watchSize();
    }

    // Immersive: no status or navigation bar over the game; a swipe shows them for a moment.
    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    // Every size the window and the game's surface take, for the system log in logs/: a
    // change after the swap chain is made means its buffers are allocated again.
    private void watchSize() {
        android.view.View.OnLayoutChangeListener log = (view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
            if (right - left != oldRight - oldLeft || bottom - top != oldBottom - oldTop)
                Log.i("SRW64", "SRW64_WINDOW " + (view == mSurface ? "surface " : "window ") + (right - left) + "x" + (bottom - top)
                        + " was " + (oldRight - oldLeft) + "x" + (oldBottom - oldTop));
        };
        getWindow().getDecorView().addOnLayoutChangeListener(log);
        if (mSurface != null) mSurface.addOnLayoutChangeListener(log);
    }

    // Landscape only: SDL lets a resizable window take any orientation, overriding the manifest.
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    // The settings' "open this folder" (src/native/ui/frontend.cpp, over JNI): the system
    // Files app at the player's folder (its saves, import, export, reports, filters or
    // bezels), else at an export under Documents/Marchwind64, else at a folder of the data
    // folder through UserFilesProvider. False for a folder outside them or when no app can
    // show it.
    public boolean openUserFolder(String path) {
        PlayerFolder folder = PlayerFolder.open(this);
        String id = folder == null ? null : folder.place(SetupActivity.userDir(this), path);
        if (id != null) {
            try { startActivity(folder.view(id)); return true; }
            catch (ActivityNotFoundException | SecurityException error) { return false; }
        }
        if (path.startsWith(PublicExports.directory(""))) {
            Intent intent = new Intent(Intent.ACTION_VIEW);
            Uri uri = DocumentsContract.buildDocumentUri("com.android.externalstorage.documents", "primary:" + path);
            intent.setDataAndType(uri, DocumentsContract.Document.MIME_TYPE_DIR);
            try { startActivity(intent); return true; }
            catch (ActivityNotFoundException | SecurityException error) { return false; }
        }
        String relative;
        try {
            String base = SetupActivity.userDir(this).getCanonicalPath(), target = new File(path).getCanonicalPath();
            if (target.equals(base)) relative = "";
            else if (target.startsWith(base + File.separator)) relative = target.substring(base.length() + 1);
            else return false;
        } catch (java.io.IOException error) {
            return false;
        }
        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setDataAndType(UserFilesProvider.folderUri(relative), DocumentsContract.Document.MIME_TYPE_DIR);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        try {
            startActivity(intent);
            return true;
        } catch (ActivityNotFoundException error) {
            return false;
        }
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
        File user = SetupActivity.userDir(this);
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
