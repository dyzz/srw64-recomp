package org.srw64.game;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

// The launcher (docs/design/android-port.md): unpacks the bundled fonts and dialogue
// text once per app version, asks for the ROM through the system file picker and copies
// it into the app's own files, then starts the game. The host needs real paths: it
// imports the ROM and walks the dialogue folders with std::filesystem.
//
// It also installs the HD pack, the same Marchwind64-HD-<hd version>.zip as on computers: its hd/
// folder is unpacked into the user directory (files/user/hd), where the host looks for it
// (src/native/app/launch.cpp). Three ways in: offered once after the ROM is chosen, the
// launcher icon's "Import HD pack" shortcut (res/xml/shortcuts.xml), and opening the zip
// with this app from a file manager or browser.
public class SetupActivity extends Activity {
    static final String IMPORT_HD = "org.srw64.game.IMPORT_HD";
    private static final int PICK_ROM = 1, PICK_HD = 2;
    private LinearLayout page;
    private TextView message;
    private boolean offerHd;

    static File romFile(Activity activity) { return new File(activity.getFilesDir(), "rom.z64"); }
    static File resourceDir(Activity activity) { return new File(activity.getFilesDir(), "resources"); }
    static File userDir(Activity activity) { return new File(activity.getFilesDir(), "user"); }
    static File hdDir(Activity activity) { return new File(userDir(activity), "hd"); }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setGravity(Gravity.CENTER);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        page.setPadding(pad, pad, pad, pad);
        message = new TextView(this);
        message.setGravity(Gravity.CENTER);
        message.setTextSize(20);
        page.addView(message);
        setContentView(page);
        show("正在准备…\nPreparing…");
        new Thread(() -> {
            try {
                unpackResources();
            } catch (IOException error) {
                runOnUiThread(() -> show("无法解出资源：" + error));
                return;
            }
            runOnUiThread(this::begin);
        }).start();
    }

    // What the launcher was opened for: an HD pack handed over, the shortcut, or the game.
    private void begin() {
        Intent intent = getIntent();
        String action = intent.getAction();
        Uri handed = null;
        if (Intent.ACTION_VIEW.equals(action)) handed = intent.getData();
        else if (Intent.ACTION_SEND.equals(action)) handed = intent.getParcelableExtra(Intent.EXTRA_STREAM);
        if (handed != null) { importHd(handed); return; }
        if (IMPORT_HD.equals(action)) { pickHd(); return; }
        next();
    }

    private void next() {
        if (romFile(this).isFile()) {
            if (offerHd && !hdDir(this).isDirectory()) { askHd(); return; }
            Intent game = new Intent(this, SRW64Activity.class);
            // Development: `adb shell am start ... --esa args a,b` passes extra host arguments.
            if (getIntent().hasExtra("args")) game.putExtra("args", getIntent().getStringArrayExtra("args"));
            if (getIntent().hasExtra("debug")) game.putExtra("debug", getIntent().getBooleanExtra("debug", false));
            if (getIntent().hasExtra("env")) game.putExtra("env", getIntent().getStringArrayExtra("env"));
            startActivity(game);
            finish();
            return;
        }
        show("请选择超级机器人大战 64 的 ROM（日版 Rev 0，.z64）\nChoose your Super Robot Taisen 64 ROM (Japan, Rev 0)");
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        startActivityForResult(pick, PICK_ROM);
    }

    // Once, right after the ROM: the HD pack is optional and can come later.
    private void askHd() {
        offerHd = false;
        show("要导入 HD 美术包吗？可以跳过，以后长按应用图标选「导入 HD 包」。\n"
                + "Import the HD art pack? You can skip this and later long-press the app icon for “Import HD pack”.");
        button("选择 HD 包（Marchwind64-HD-….zip）\nChoose the HD pack", this::pickHd);
        button("跳过，开始游戏\nSkip and play", this::next);
    }

    private void pickHd() {
        show("请选择 HD 美术包（Marchwind64-HD-….zip）\nChoose the HD art pack (Marchwind64-HD-….zip)");
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        startActivityForResult(pick, PICK_HD);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        Uri uri = result == RESULT_OK && data != null ? data.getData() : null;
        if (request == PICK_HD) {
            if (uri == null) {
                // Not now: carry on to the game if there is a ROM, else stop here.
                if (romFile(this).isFile()) next();
                else show("没有选择 HD 包。\nNo HD pack chosen.");
                return;
            }
            importHd(uri);
            return;
        }
        if (request != PICK_ROM) return;
        if (uri == null) {
            show("没有选择 ROM。重新打开应用再选一次。\nNo ROM chosen; open the app again to choose one.");
            return;
        }
        show("正在复制 ROM…\nCopying the ROM…");
        new Thread(() -> {
            File target = romFile(this), partial = new File(getFilesDir(), "rom.z64.part");
            try (InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(partial)) {
                copy(in, out);
            } catch (IOException error) {
                partial.delete();
                runOnUiThread(() -> show("无法读取 ROM：" + error));
                return;
            }
            // The host checks the ROM itself and says when it is not the supported one.
            if (!partial.renameTo(target)) {
                runOnUiThread(() -> show("无法保存 ROM\nCould not save the ROM"));
                return;
            }
            offerHd = true;
            runOnUiThread(this::next);
        }).start();
    }

    // Unpacks the zip's hd/ folder beside the old one, then swaps them, so a failed or
    // interrupted import leaves the installed pack as it was.
    private void importHd(Uri uri) {
        if (SRW64Activity.running) {
            show("游戏正在运行。请先关闭游戏（在最近任务里划掉），再导入 HD 包。\n"
                    + "The game is running. Close it first (swipe it away in recent apps), then import the HD pack.");
            return;
        }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        long size = sizeOf(uri);
        File user = userDir(this), staging = new File(user, "hd.new"), target = hdDir(this);
        if (size > 0 && user.getParentFile().getUsableSpace() < size * 11 / 10) {
            show("存储空间不够：HD 包约 " + size / (1 << 20) + " MB，请先腾出空间。\n"
                    + "Not enough storage: the HD pack needs about " + size / (1 << 20) + " MB.");
            return;
        }
        show("正在导入 HD 包…\nImporting the HD pack…");
        new Thread(() -> {
            String failure = null;
            try {
                if (!user.mkdirs() && !user.isDirectory()) throw new IOException("cannot create " + user);
                delete(staging);
                long[] done = { 0 };
                try (InputStream raw = getContentResolver().openInputStream(uri);
                     CountingStream counted = new CountingStream(raw, done);
                     ZipInputStream zip = new ZipInputStream(counted)) {
                    String root = staging.getCanonicalPath() + File.separator;
                    long shown = 0;
                    for (ZipEntry entry; (entry = zip.getNextEntry()) != null;) {
                        String name = entry.getName();
                        if (!name.startsWith("hd/") || name.length() == 3) continue;
                        File out = new File(staging, name.substring(3));
                        // Nothing may land outside the staging folder.
                        if (!out.getCanonicalPath().startsWith(root)) throw new IOException("bad entry " + name);
                        if (entry.isDirectory()) { out.mkdirs(); continue; }
                        out.getParentFile().mkdirs();
                        try (OutputStream file = new FileOutputStream(out)) { copy(zip, file); }
                        if (size > 0 && done[0] - shown > size / 100) {
                            shown = done[0];
                            int percent = (int) (100 * done[0] / size);
                            runOnUiThread(() -> show("正在导入 HD 包… " + percent + "%\nImporting the HD pack… " + percent + "%"));
                        }
                    }
                }
                if (!new File(staging, "hd.json").isFile()) {
                    failure = "这不是 HD 美术包：压缩包里没有 hd/hd.json。\nThis is not the HD art pack: there is no hd/hd.json in it.";
                } else {
                    File old = new File(user, "hd.old");
                    delete(old);
                    if (target.exists() && !target.renameTo(old)) throw new IOException("cannot replace " + target);
                    if (!staging.renameTo(target)) throw new IOException("cannot install " + target);
                    delete(old);
                }
            } catch (IOException error) {
                failure = "导入失败：" + error + "\nImport failed.";
            }
            if (failure != null) delete(staging);
            String result = failure;
            runOnUiThread(() -> {
                getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                if (result != null) { show(result); return; }
                show("HD 包已导入。游戏的设置里可以切换原版／HD。\nHD pack imported. Switch between original and HD in the game's settings.");
                button("开始游戏\nPlay", this::next);
            });
        }).start();
    }

    private long sizeOf(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri, new String[] { OpenableColumns.SIZE }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) return cursor.getLong(0);
        } catch (RuntimeException ignored) {
            // Some providers cannot tell; the import then runs without a percentage.
        }
        return -1;
    }

    private void show(String text) {
        page.removeAllViews();
        message.setText(text);
        page.addView(message);
    }

    private void button(String label, Runnable action) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setOnClickListener(view -> action.run());
        LinearLayout.LayoutParams layout = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        layout.topMargin = (int) (16 * getResources().getDisplayMetrics().density);
        page.addView(button, layout);
    }

    private void unpackResources() throws IOException {
        PackageInfo info;
        try {
            info = getPackageManager().getPackageInfo(getPackageName(), 0);
        } catch (Exception error) {
            throw new IOException(error);
        }
        String version = info.versionCode + ":" + info.lastUpdateTime;
        File root = resourceDir(this), stamp = new File(getFilesDir(), "resources.version");
        if (root.isDirectory() && stamp.isFile()
                && new String(Files.readAllBytes(stamp.toPath()), StandardCharsets.UTF_8).equals(version)) return;
        File staging = new File(getFilesDir(), "resources.new");
        delete(staging);
        unpack("resources", staging);
        delete(root);
        if (!staging.renameTo(root)) throw new IOException("cannot replace " + root);
        Files.write(stamp.toPath(), version.getBytes(StandardCharsets.UTF_8));
    }

    private void unpack(String asset, File target) throws IOException {
        String[] children = getAssets().list(asset);
        if (children != null && children.length > 0) {
            if (!target.mkdirs() && !target.isDirectory()) throw new IOException("cannot create " + target);
            for (String child : children) unpack(asset + "/" + child, new File(target, child));
            return;
        }
        try (InputStream in = getAssets().open(asset); OutputStream out = new FileOutputStream(target)) {
            copy(in, out);
        }
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[1 << 16];
        for (int n; (n = in.read(buffer)) > 0;) out.write(buffer, 0, n);
    }

    private static void delete(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) delete(child);
        file.delete();
    }

    // Counts the compressed bytes read, for the import's percentage.
    private static final class CountingStream extends java.io.FilterInputStream {
        private final long[] count;
        CountingStream(InputStream in, long[] count) { super(in); this.count = count; }
        @Override public int read() throws IOException { int b = super.read(); if (b >= 0) count[0]++; return b; }
        @Override public int read(byte[] b, int off, int len) throws IOException { int n = super.read(b, off, len); if (n > 0) count[0] += n; return n; }
    }
}
