package org.srw64.game;

import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.os.Bundle;
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

// The game: SDL3's Java glue loads SDL3, sdl2-compat (libSDL2.so) and libmain.so, then
// runs the host's main with the arguments srw64.sh passes on Linux.
public class SRW64Activity extends SDLActivity {
    // The host is not re-entrant and runs once per process; the launcher will not swap
    // the HD folder under it (SetupActivity.importHd).
    static volatile boolean running;
    private static final int EXPORT_DOCUMENT = 6401;
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
            SaveExport snapshot = new SaveExport(directory);
            String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss-SSS", Locale.ROOT).format(new Date());
            saveExport = new Export("saves", "srw64-saves-" + stamp + ".zip", snapshot::write);
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
            // bug_report writes unique, closed archives; no live log is read here.
            reportExport = new Export("reports", report.getName(), stream -> Files.copy(report.toPath(), stream));
            startExport(reportExport);
            return true;
        } catch (IOException | RuntimeException error) { return false; }
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

    // The settings' "open this folder" (src/native/ui/frontend.cpp, over JNI): the system
    // Files app at a folder of the data folder, through UserFilesProvider. False for a
    // folder outside it or when no app can show it.
    public boolean openUserFolder(String path) {
        if (path.equals(PublicExports.directory("reports")) || path.equals(PublicExports.directory("saves"))) {
            Intent intent = new Intent(Intent.ACTION_VIEW);
            Uri uri = DocumentsContract.buildDocumentUri("com.android.externalstorage.documents", "primary:" + path);
            intent.setDataAndType(uri, DocumentsContract.Document.MIME_TYPE_DIR);
            try { startActivity(intent); return true; }
            catch (ActivityNotFoundException | SecurityException error) { return false; }
        }
        String relative;
        try {
            String base = SetupActivity.userDir(this).getCanonicalPath(), folder = new File(path).getCanonicalPath();
            if (folder.equals(base)) relative = "";
            else if (folder.startsWith(base + File.separator)) relative = folder.substring(base.length() + 1);
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
