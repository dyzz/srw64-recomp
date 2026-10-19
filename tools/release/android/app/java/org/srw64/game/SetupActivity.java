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
import android.widget.ScrollView;
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
    private static final int PICK_ROM = 1, PICK_HD = 2, PICK_FOLDER = 3;
    private LinearLayout page;
    private TextView message;
    private boolean offerHd, folderSynced;
    // Set when the kept rom.z64 was not the supported ROM and was removed (RomCheck).
    private String romRefused;

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
        // A long page (the save folder's) scrolls instead of pushing its buttons off a landscape screen.
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(page, new ScrollView.LayoutParams(ScrollView.LayoutParams.MATCH_PARENT, ScrollView.LayoutParams.WRAP_CONTENT));
        setContentView(scroll);
        show("正在准备…\nPreparing…");
        new Thread(() -> {
            try {
                unpackResources();
            } catch (IOException error) {
                runOnUiThread(() -> show("无法解出资源：" + error));
                return;
            }
            checkInstalledRom();
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
            if (!folderSynced) {
                PlayerFolder folder = PlayerFolder.open(this);
                if (folder != null) { syncFolder(folder); return; }
                if (!PlayerFolder.declined(this)) { askFolder(); return; }
            }
            Intent game = new Intent(this, SRW64Activity.class);
            // Development: `adb shell am start ... --esa args a,b` passes extra host arguments.
            if (getIntent().hasExtra("args")) game.putExtra("args", getIntent().getStringArrayExtra("args"));
            if (getIntent().hasExtra("debug")) game.putExtra("debug", getIntent().getBooleanExtra("debug", false));
            if (getIntent().hasExtra("env")) game.putExtra("env", getIntent().getStringArrayExtra("env"));
            startActivity(game);
            finish();
            return;
        }
        if (romRefused != null) {
            // The kept file was refused at this start: say why before the picker opens.
            String why = romRefused;
            romRefused = null;
            refuseRom(why);
            return;
        }
        pickRom();
    }

    private void pickRom() {
        show("请选择超级机器人大战 64 的 ROM（日版 Rev 0，.z64）\nChoose your Super Robot Taisen 64 ROM (Japan, Rev 0)");
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        try { startActivityForResult(pick, PICK_ROM); }
        catch (android.content.ActivityNotFoundException error) {
            show("这台设备没有可用的文件选择器，无法选择 ROM。\nThis device has no file picker to choose the ROM with.");
        }
    }

    private void refuseRom(String why) {
        show(why);
        button("重新选择 ROM\nChoose the ROM again", this::pickRom);
    }

    // A rom.z64 kept by an earlier version is checked once (then by its size and time):
    // a wrong one is removed, so the launcher asks again instead of the game closing.
    private void checkInstalledRom() {
        File rom = romFile(this), stamp = new File(getFilesDir(), "rom.checked");
        if (!rom.isFile()) return;
        String identity = rom.length() + ":" + rom.lastModified();
        try {
            if (stamp.isFile() && new String(Files.readAllBytes(stamp.toPath()), StandardCharsets.UTF_8).equals(identity)) return;
        } catch (IOException ignored) {
            // Checked again below.
        }
        RomCheck.Result checked = RomCheck.installed(rom);
        if (checked == RomCheck.Result.OK) {
            try { Files.write(stamp.toPath(), identity.getBytes(StandardCharsets.UTF_8)); }
            catch (IOException ignored) { /* checked again next time */ }
            return;
        }
        rom.delete();
        stamp.delete();
        romRefused = "之前选择的 ROM 不能用，游戏因此一启动就退出。请重新选择。\n"
                + "The ROM chosen before cannot be used, so the game closed at once. Please choose it again.\n\n"
                + RomCheck.explain(checked);
    }

    // Once, right after the ROM: the HD pack is optional and can come later.
    private void askHd() {
        offerHd = false;
        show("要导入 HD 美术包吗？可以跳过，以后长按应用图标选「导入 HD 包」。\n"
                + "Import the HD art pack? You can skip this and later long-press the app icon for “Import HD pack”.");
        button("选择 HD 包（Marchwind64-HD-….zip）\nChoose the HD pack", this::pickHd);
        button("跳过，开始游戏\nSkip and play", this::next);
    }

    // The player's folder (PlayerFolder): asked once, before the game; "Not now" is kept
    // and the settings' Saves page can choose it later.
    private void askFolder() {
        show("选择存档文件夹：存档、导入导出、问题报告、滤镜和边框放在「内部存储/" + PlayerFolder.SUGGESTED + "」，"
                + "文件管理器里能看到，卸载也不会丢。下一步点「使用此文件夹」，再点「允许」。\n"
                + "Choose the save folder: saves, import and export, reports, filters and bezels go in “" + PlayerFolder.SUGGESTED + "”, "
                + "visible to any file manager and kept after an uninstall. Next, tap “Use this folder”, then “Allow”.");
        button("选择文件夹\nChoose the folder", () -> {
            try { startActivityForResult(PlayerFolder.pick(), PICK_FOLDER); }
            catch (android.content.ActivityNotFoundException error) { PlayerFolder.decline(this); next(); }
        });
        button("以后再说（设置 → 存档里可以再选）\nNot now (Settings → Saves can choose it later)", () -> { PlayerFolder.decline(this); next(); });
    }

    // Before the game: the saves back after a reinstall, the private ones out, the
    // player's imports, filters and bezels in. A failure is shown but does not stop play.
    private void syncFolder(PlayerFolder folder) {
        show("正在同步存档文件夹…\nSyncing the save folder…");
        new Thread(() -> {
            String failure = null;
            try { folder.everything(userDir(this)); }
            catch (IOException | RuntimeException error) { failure = String.valueOf(error); }
            String result = failure;
            runOnUiThread(() -> {
                folderSynced = true;
                if (result == null) { next(); return; }
                show("存档文件夹同步失败：" + result + "\n存档仍保存在游戏内部。\n\nCould not sync the save folder; the saves are still kept inside the game.");
                button("继续\nContinue", this::next);
            });
        }).start();
    }

    private void pickHd() {
        show("请选择 HD 美术包（Marchwind64-HD-….zip）\nChoose the HD art pack (Marchwind64-HD-….zip)");
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        try { startActivityForResult(pick, PICK_HD); }
        catch (android.content.ActivityNotFoundException error) {
            show("这台设备没有可用的文件选择器。\nThis device has no file picker.");
            if (romFile(this).isFile()) button("开始游戏\nPlay", this::next);
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        Uri uri = result == RESULT_OK && data != null ? data.getData() : null;
        if (request == PICK_FOLDER) {
            if (uri == null) { PlayerFolder.decline(this); next(); return; }
            try { syncFolder(PlayerFolder.remember(this, uri)); }
            catch (IOException | RuntimeException error) {
                show("无法使用这个文件夹：" + error + "\nCould not use this folder.");
                button("重新选择\nChoose again", this::askFolder);
                button("以后再说\nNot now", () -> { PlayerFolder.decline(this); next(); });
            }
            return;
        }
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
            show("没有选择 ROM。\nNo ROM chosen.");
            button("选择 ROM\nChoose the ROM", this::pickRom);
            return;
        }
        show("正在检查并复制 ROM…\nChecking and copying the ROM…");
        new Thread(() -> {
            File target = romFile(this), partial = new File(getFilesDir(), "rom.z64.part");
            RomCheck.Result checked;
            try (InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(partial)) {
                if (in == null) throw new IOException("no data");
                checked = RomCheck.copy(in, out);
            } catch (IOException | RuntimeException error) {
                partial.delete();
                runOnUiThread(() -> {
                    show("无法读取 ROM：" + error + "\nCould not read the ROM.");
                    button("重新选择 ROM\nChoose the ROM again", this::pickRom);
                });
                return;
            }
            if (checked != RomCheck.Result.OK) {
                partial.delete();
                RomCheck.Result why = checked;
                runOnUiThread(() -> refuseRom(RomCheck.explain(why)));
                return;
            }
            if (!partial.renameTo(target)) {
                partial.delete();
                runOnUiThread(() -> show("无法保存 ROM\nCould not save the ROM"));
                return;
            }
            try {
                Files.write(new File(getFilesDir(), "rom.checked").toPath(),
                        (target.length() + ":" + target.lastModified()).getBytes(StandardCharsets.UTF_8));
            } catch (IOException ignored) {
                // Checked again at the next start.
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
