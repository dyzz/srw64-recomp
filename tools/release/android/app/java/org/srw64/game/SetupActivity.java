package org.srw64.game;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

// The launcher (docs/design/android-port.md): unpacks the bundled fonts and dialogue
// text once per app version, asks for the ROM through the system file picker and copies
// it into the app's own files, then starts the game. The host needs real paths: it
// imports the ROM and walks the dialogue folders with std::filesystem.
public class SetupActivity extends Activity {
    private static final int PICK_ROM = 1;
    private TextView message;

    static File romFile(Activity activity) { return new File(activity.getFilesDir(), "rom.z64"); }
    static File resourceDir(Activity activity) { return new File(activity.getFilesDir(), "resources"); }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        message = new TextView(this);
        message.setGravity(Gravity.CENTER);
        message.setTextSize(20);
        setContentView(message);
        message.setText("正在准备…");
        new Thread(() -> {
            try {
                unpackResources();
            } catch (IOException error) {
                runOnUiThread(() -> message.setText("无法解出资源：" + error));
                return;
            }
            runOnUiThread(this::next);
        }).start();
    }

    private void next() {
        if (romFile(this).isFile()) {
            Intent game = new Intent(this, SRW64Activity.class);
            // Development: `adb shell am start ... --esa args a,b` passes extra host arguments.
            if (getIntent().hasExtra("args")) game.putExtra("args", getIntent().getStringArrayExtra("args"));
            if (getIntent().hasExtra("debug")) game.putExtra("debug", getIntent().getBooleanExtra("debug", false));
            if (getIntent().hasExtra("env")) game.putExtra("env", getIntent().getStringArrayExtra("env"));
            startActivity(game);
            finish();
            return;
        }
        message.setText("请选择超级机器人大战 64 的 ROM（日版 Rev 0，.z64）\nChoose your Super Robot Taisen 64 ROM (Japan, Rev 0)");
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        startActivityForResult(pick, PICK_ROM);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_ROM) return;
        if (result != RESULT_OK || data == null || data.getData() == null) {
            message.setText("没有选择 ROM。重新打开应用再选一次。\nNo ROM chosen; open the app again to choose one.");
            return;
        }
        Uri uri = data.getData();
        message.setText("正在复制 ROM…");
        new Thread(() -> {
            File target = romFile(this), partial = new File(getFilesDir(), "rom.z64.part");
            try (InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(partial)) {
                copy(in, out);
            } catch (IOException error) {
                partial.delete();
                runOnUiThread(() -> message.setText("无法读取 ROM：" + error));
                return;
            }
            // The host checks the ROM itself and says when it is not the supported one.
            if (!partial.renameTo(target)) {
                runOnUiThread(() -> message.setText("无法保存 ROM"));
                return;
            }
            runOnUiThread(this::next);
        }).start();
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
}
