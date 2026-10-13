package org.srw64.game;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

// The player's folder (docs/design/save-slots-autosave.md §11): one folder the player
// grants through the system picker, Documents/Marchwind64 by default, holding what a
// player handles themselves where any file manager sees it and an uninstall leaves it:
//
//   saves/    the save library, a copy of the private one (the private copy wins)
//   import/   emulator saves to take in        export/   the four emulator files
//   reports/  problem reports                   filters/, bezels/   the player's own
//   logs/     the last runs' logs, for a game that cannot reach its settings to export a report
//
// The host keeps reading and writing the private user directory (it needs real paths);
// this mirrors the two. Saves go out, and come back only into an empty library (after a
// reinstall or cleared data); import/, filters/ and bezels/ come in; export/, reports/ and
// logs/ go out. One lock serialises every transfer.
final class PlayerFolder {
    static final String SUGGESTED = Environment.DIRECTORY_DOCUMENTS + "/Marchwind64";
    private static final String STORAGE = "com.android.externalstorage.documents";
    private static final String PREFS = "player_folder";
    private static final Object LOCK = new Object();
    // Not part of the mirrored library: they have their own folders at the top.
    private static final Set<String> SAVES_SKIP = new HashSet<>(Arrays.asList("import", "export"));
    // How many runs' logs logs/ keeps, as the problem report does (bug_report.cpp).
    private static final int LOGS_KEPT = 3;
    // The host writes this into the private filters/ and bezels/ (presentation_settings.cpp).
    private static final Set<String> LOOKS_KEEP = Collections.singleton("README.txt");

    private final Context context;
    private final ContentResolver resolver;
    private final Uri tree;
    private final String root;

    private PlayerFolder(Context context, Uri tree, String root) {
        this.context = context;
        this.resolver = context.getContentResolver();
        this.tree = tree;
        this.root = root;
    }

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    // The granted folder, or null: never asked, declined, or the grant is gone (an
    // uninstall drops it; the files stay).
    static PlayerFolder open(Context context) {
        SharedPreferences prefs = prefs(context);
        String tree = prefs.getString("tree", null), root = prefs.getString("root", null);
        if (tree == null || root == null) return null;
        Uri uri = Uri.parse(tree);
        for (UriPermission grant : context.getContentResolver().getPersistedUriPermissions())
            if (grant.getUri().equals(uri) && grant.isReadPermission() && grant.isWritePermission())
                return new PlayerFolder(context, uri, root);
        return null;
    }

    // "Not now" at the launcher: it does not ask again; the settings' Saves page can.
    static boolean declined(Context context) { return prefs(context).getBoolean("declined", false); }
    static void decline(Context context) { prefs(context).edit().putBoolean("declined", true).apply(); }

    // The system folder picker, opened at Documents/Marchwind64. Android 11+ lets an app
    // make that folder itself; elsewhere the picker starts at Documents.
    static Intent pick() {
        File suggested = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS), "Marchwind64");
        try { suggested.mkdirs(); } catch (RuntimeException ignored) { }
        String start = suggested.isDirectory() ? SUGGESTED : Environment.DIRECTORY_DOCUMENTS;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, DocumentsContract.buildDocumentUri(STORAGE, "primary:" + start));
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        return intent;
    }

    // Keeps the picker's answer. Documents itself gets a Marchwind64 folder inside it.
    static PlayerFolder remember(Context context, Uri tree) throws IOException {
        ContentResolver resolver = context.getContentResolver();
        try {
            resolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        } catch (SecurityException error) {
            throw new IOException(error);
        }
        String id = DocumentsContract.getTreeDocumentId(tree);
        PlayerFolder folder = new PlayerFolder(context, tree, id);
        if (STORAGE.equals(tree.getAuthority()) && id.endsWith(":" + Environment.DIRECTORY_DOCUMENTS)) {
            synchronized (LOCK) { folder = new PlayerFolder(context, tree, folder.directory(id, "Marchwind64", true)); }
        }
        for (UriPermission grant : resolver.getPersistedUriPermissions()) {
            if (grant.getUri().equals(tree)) continue;
            try { resolver.releasePersistableUriPermission(grant.getUri(), Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION); }
            catch (SecurityException ignored) { }
        }
        // "fresh": the first save transfer keeps what an earlier install left (saves()).
        prefs(context).edit().putString("tree", tree.toString()).putString("root", folder.root)
                .putBoolean("declined", false).putBoolean("fresh", true).apply();
        return folder;
    }

    // Where it is, as the settings show it: Documents/Marchwind64, or <volume>/<path>.
    String display() {
        int colon = root.indexOf(':');
        if (colon < 0) return root;
        String volume = root.substring(0, colon), path = root.substring(colon + 1);
        return volume.equals("primary") ? path : volume + "/" + path;
    }

    // --- What the launcher and the game do ----------------------------------------------

    // Before the game starts: the library back into an empty private one, then everything
    // in both directions.
    void everything(File user) throws IOException {
        synchronized (LOCK) {
            restore(user);
            saves(user);
            looks(user);
            incoming(user);
            logs(user);
        }
    }

    // The last runs' console.log (the host's output, with the stall watchdog's thread
    // dumps) out to logs/, named by when each run started: a game stuck on a black screen
    // never reaches the settings that export a problem report. Older ones this wrote go.
    void logs(File user) throws IOException {
        synchronized (LOCK) {
            File[] runs = new File(user, "sessions").listFiles(run -> new File(run, "run/console.log").isFile());
            if (runs == null) runs = new File[0];
            Arrays.sort(runs, (a, b) -> Long.compare(new File(b, "run/console.log").lastModified(), new File(a, "run/console.log").lastModified()));
            String folder = directory(root, "logs", true);
            Map<String, Entry> there = list(folder);
            Set<String> kept = new HashSet<>();
            for (int i = 0; i < Math.min(LOGS_KEPT, runs.length); ++i) {
                File log = new File(runs[i], "run/console.log");
                String name = logName(runs[i].getName());
                kept.add(name);
                kept.add(name.replace(".log", "-system.log"));
                Entry entry = there.get(name);
                if (entry == null || entry.directory || entry.size != log.length() || log.lastModified() > entry.modified)
                    put(folder, there, log, name);
            }
            // This run's own system log beside its console.log: what Android, the GPU driver
            // and the phone maker's services print in the game's process (window and buffer
            // changes, MIUI's game services), which the host's output never sees. An app may
            // read its own process's log without any permission.
            if (runs.length > 0 && SRW64Activity.running) {
                File system = new File(context.getCacheDir(), "system.log");
                try {
                    new ProcessBuilder("logcat", "-d", "-v", "threadtime", "--pid", String.valueOf(android.os.Process.myPid()))
                            .redirectErrorStream(true).redirectOutput(system).start().waitFor();
                    if (system.length() > 0) put(folder, there, system, logName(runs[0].getName()).replace(".log", "-system.log"));
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                } finally {
                    system.delete();
                }
            }
            for (Entry entry : there.values())
                if (!entry.directory && entry.name.startsWith("Marchwind64-") && entry.name.endsWith(".log") && !kept.contains(entry.name))
                    delete(entry);
        }
    }

    // A session is <start in microseconds, hex>-<random> (runtime.cpp token()).
    static String logName(String session) {
        try {
            long micros = Long.parseLong(session.substring(0, session.indexOf('-')), 16);
            return "Marchwind64-" + new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date(micros / 1000)) + ".log";
        } catch (RuntimeException error) {
            return "Marchwind64-" + session + ".log";
        }
    }

    // The newest run's log, as the game's periodic check compares it.
    static String logFingerprint(File user) {
        File[] runs = new File(user, "sessions").listFiles();
        long newest = 0, size = 0;
        if (runs != null) for (File run : runs) {
            File log = new File(run, "run/console.log");
            if (log.lastModified() > newest) { newest = log.lastModified(); size = log.length(); }
        }
        return newest + ":" + size;
    }

    // The private saves out. On the first transfer after a grant, saves already in the
    // folder that differ from the private card (an earlier install's) are renamed aside
    // to saves-<time>, not overwritten.
    void saves(File user) throws IOException {
        synchronized (LOCK) {
            File saves = new File(user, "saves");
            if (!hasLibrary(saves)) return;
            if (prefs(context).getBoolean("fresh", false)) {
                Entry there = list(root).get("saves");
                if (there != null && there.directory) {
                    Entry card = list(there.id).get("cartridge.sram");
                    if (card != null && !card.directory && !same(card, new File(saves, "cartridge.sram"))) {
                        String aside = "saves-" + new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date());
                        DocumentsContract.renameDocument(resolver, uri(there.id), aside);
                    }
                }
                prefs(context).edit().putBoolean("fresh", false).apply();
            }
            push(saves, directory(root, "saves", true), false, SAVES_SKIP, true);
        }
    }

    // An empty private library (a reinstall, cleared data) takes the folder's saves.
    boolean restore(File user) throws IOException {
        synchronized (LOCK) {
            File saves = new File(user, "saves");
            if (hasLibrary(saves)) return false;
            String there = directory(root, "saves", false);
            if (there == null) return false;
            Entry card = list(there).get("cartridge.sram");
            if (card == null || card.directory) return false;
            pull(there, saves, false, Collections.emptySet());
            prefs(context).edit().putBoolean("fresh", false).apply();
            return true;
        }
    }

    // The folder's import/ in, replacing the private one (save_store reads saves/import).
    void incoming(File user) throws IOException {
        synchronized (LOCK) {
            pull(directory(root, "import", true), new File(user, "saves/import"), true, Collections.emptySet());
        }
    }

    // The player's filters and bezels in. A folder without them yet first gets what the
    // private ones hold (players who added their own before this).
    void looks(File user) throws IOException {
        synchronized (LOCK) {
            for (String kind : new String[] { "filters", "bezels" }) {
                File mine = new File(user, kind);
                String there = directory(root, kind, false);
                if (there == null) {
                    there = directory(root, kind, true);
                    if (mine.isDirectory()) push(mine, there, false, Collections.emptySet(), false);
                }
                pull(there, mine, true, LOOKS_KEEP);
            }
        }
    }

    // A file out to export/ or reports/; returns where it went, as the player sees it.
    String send(String kind, File... files) throws IOException {
        synchronized (LOCK) {
            String folder = directory(root, kind, true);
            Map<String, Entry> there = list(folder);
            for (File file : files) put(folder, there, file, file.getName());
            return display() + "/" + kind + (files.length == 1 ? "/" + files[0].getName() : "");
        }
    }

    // The folder for a path the settings show or hand over: a private one (saves/import is
    // import/, saves/export is export/) or one under display(). Null when it maps to none.
    String place(File user, String path) {
        String sub = null;
        String shown = display();
        if (path.equals(shown)) sub = "";
        else if (path.startsWith(shown + "/")) sub = path.substring(shown.length() + 1);
        else {
            try {
                String base = user.getCanonicalPath(), folder = new File(path).getCanonicalPath();
                if (folder.startsWith(base + File.separator)) {
                    String relative = folder.substring(base.length() + 1);
                    if (relative.equals("saves/import") || relative.startsWith("saves/import/")) sub = "import" + relative.substring(12);
                    else if (relative.equals("saves/export") || relative.startsWith("saves/export/")) sub = "export" + relative.substring(12);
                    else for (String top : new String[] { "saves", "reports", "filters", "bezels" })
                        if (relative.equals(top) || relative.startsWith(top + "/")) sub = relative;
                }
            } catch (IOException ignored) { }
        }
        if (sub == null) return null;
        try {
            synchronized (LOCK) {
                String id = root;
                for (String part : sub.split("/")) if (!part.isEmpty()) id = directory(id, part, true);
                return id;
            }
        } catch (IOException error) {
            return null;
        }
    }

    // A folder of it in the Files app.
    Intent view(String id) {
        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setDataAndType(STORAGE.equals(tree.getAuthority()) ? DocumentsContract.buildDocumentUri(STORAGE, id) : uri(id),
                Document.MIME_TYPE_DIR);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        return intent;
    }

    // A cheap fingerprint of the private library, so the game's periodic check only goes
    // to the folder when something changed.
    static String fingerprint(File user) {
        StringBuilder out = new StringBuilder();
        fingerprint(new File(user, "saves"), "", out, true);
        return out.toString();
    }

    private static void fingerprint(File dir, String prefix, StringBuilder out, boolean top) {
        File[] files = dir.listFiles();
        if (files == null) return;
        Arrays.sort(files);
        for (File file : files) {
            String name = file.getName();
            if (skipped(name) || (top && SAVES_SKIP.contains(name))) continue;
            if (file.isDirectory()) fingerprint(file, prefix + name + "/", out, false);
            else out.append(prefix).append(name).append(':').append(file.length()).append(':').append(file.lastModified()).append('\n');
        }
    }

    // --- The transfers ------------------------------------------------------------------

    private static final class Entry {
        final String id, name;
        final boolean directory;
        final long size, modified;
        Entry(String id, String name, boolean directory, long size, long modified) {
            this.id = id; this.name = name; this.directory = directory; this.size = size; this.modified = modified;
        }
    }

    // The host's atomic writes (<name>.tmp-<token>) and this class's own partial copies.
    private static boolean skipped(String name) { return name.contains(".tmp-") || name.endsWith(".srw64-part"); }

    private static boolean hasLibrary(File saves) {
        if (new File(saves, "cartridge.sram").isFile()) return true;
        for (String kind : new String[] { "slots", "auto" }) {
            String[] names = new File(saves, kind).list();
            if (names != null && names.length > 0) return true;
        }
        return false;
    }

    private Uri uri(String id) { return DocumentsContract.buildDocumentUriUsingTree(tree, id); }

    private Map<String, Entry> list(String parent) throws IOException {
        Map<String, Entry> out = new HashMap<>();
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parent);
        String[] columns = { Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
                Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED };
        try (Cursor cursor = resolver.query(children, columns, null, null, null)) {
            if (cursor == null) throw new IOException("cannot list " + parent);
            while (cursor.moveToNext()) {
                String name = cursor.getString(1);
                out.put(name, new Entry(cursor.getString(0), name, Document.MIME_TYPE_DIR.equals(cursor.getString(2)),
                        cursor.isNull(3) ? -1 : cursor.getLong(3), cursor.isNull(4) ? 0 : cursor.getLong(4)));
            }
        } catch (RuntimeException error) {
            throw new IOException(error);
        }
        return out;
    }

    // A folder under parent; made when missing and make is set, else null.
    private String directory(String parent, String name, boolean make) throws IOException {
        Entry entry = list(parent).get(name);
        if (entry != null) {
            if (entry.directory) return entry.id;
            throw new IOException(name + " is a file");
        }
        if (!make) return null;
        try {
            Uri made = DocumentsContract.createDocument(resolver, uri(parent), Document.MIME_TYPE_DIR, name);
            if (made == null) throw new IOException("cannot make " + name);
            return DocumentsContract.getDocumentId(made);
        } catch (RuntimeException error) {
            throw new IOException(error);
        }
    }

    // A private file over the folder's file of that name, or as a new one beside them.
    private void put(String parent, Map<String, Entry> there, File source, String name) throws IOException {
        Entry entry = there.get(name);
        try {
            if (entry != null && entry.directory) { DocumentsContract.deleteDocument(resolver, uri(entry.id)); entry = null; }
            Uri target = entry != null ? uri(entry.id)
                    : DocumentsContract.createDocument(resolver, uri(parent), "application/octet-stream", name);
            if (target == null) throw new IOException("cannot make " + name);
            try (InputStream in = new FileInputStream(source); OutputStream out = resolver.openOutputStream(target, "wt")) {
                if (out == null) throw new IOException("cannot write " + name);
                copy(in, out);
            }
        } catch (RuntimeException error) {
            throw new IOException(error);
        }
    }

    // A file of the folder over the private one, through a partial copy renamed into place.
    private void get(Entry entry, File target) throws IOException {
        File partial = new File(target.getPath() + ".srw64-part");
        try (InputStream in = resolver.openInputStream(uri(entry.id)); OutputStream out = new FileOutputStream(partial)) {
            if (in == null) throw new IOException("cannot read " + entry.name);
            copy(in, out);
        } catch (IOException | RuntimeException error) {
            partial.delete();
            throw error instanceof IOException ? (IOException) error : new IOException(error);
        }
        if (!partial.renameTo(target)) { partial.delete(); throw new IOException("cannot replace " + target); }
        if (entry.modified > 0) target.setLastModified(entry.modified);
    }

    // Private to folder: a file goes when it is new, its size differs or it changed since
    // the folder's copy. With prune, the folder loses what the private side no longer has
    // (rotated autosaves, slots moved to the bin); below sets prune for the subfolders.
    private void push(File from, String to, boolean prune, Set<String> skip, boolean below) throws IOException {
        File[] files = from.listFiles();
        if (files == null) files = new File[0];
        Map<String, Entry> there = list(to);
        Set<String> here = new HashSet<>();
        for (File file : files) {
            String name = file.getName();
            if (skip.contains(name) || skipped(name)) continue;
            here.add(name);
            Entry entry = there.get(name);
            if (file.isDirectory()) {
                if (entry != null && !entry.directory) { delete(entry); entry = null; }
                push(file, entry != null ? entry.id : directory(to, name, true), below, Collections.emptySet(), below);
            } else if (entry == null || entry.directory || entry.size != file.length() || file.lastModified() > entry.modified) {
                put(to, there, file, name);
            }
        }
        if (prune) for (Entry entry : there.values()) if (!here.contains(entry.name)) delete(entry);
    }

    // Folder to private, the same test the other way; with prune the private side loses
    // what the folder no longer has, except the names in keep.
    private void pull(String from, File to, boolean prune, Set<String> keep) throws IOException {
        if (!to.isDirectory() && !to.mkdirs()) throw new IOException("cannot make " + to);
        Map<String, Entry> there = list(from);
        for (Entry entry : there.values()) {
            if (skipped(entry.name)) continue;
            File file = new File(to, entry.name);
            if (entry.directory) {
                if (file.isFile()) file.delete();
                pull(entry.id, file, prune, Collections.emptySet());
            } else if (!file.isFile() || file.length() != entry.size || entry.modified > file.lastModified()) {
                if (file.isDirectory()) deleteLocal(file);
                get(entry, file);
            }
        }
        if (!prune) return;
        File[] here = to.listFiles();
        if (here != null) for (File file : here)
            if (!there.containsKey(file.getName()) && !keep.contains(file.getName())) deleteLocal(file);
    }

    private boolean same(Entry entry, File file) throws IOException {
        if (!file.isFile() || file.length() != entry.size) return false;
        try (InputStream a = resolver.openInputStream(uri(entry.id)); InputStream b = new FileInputStream(file)) {
            if (a == null) return false;
            byte[] x = new byte[1 << 15], y = new byte[1 << 15];
            for (int n; (n = readFully(a, x)) > 0;) if (readFully(b, y) != n || !Arrays.equals(x, y)) return false;
            return b.read() < 0;
        }
    }

    private static int readFully(InputStream in, byte[] buffer) throws IOException {
        int total = 0;
        for (int n; total < buffer.length && (n = in.read(buffer, total, buffer.length - total)) > 0;) total += n;
        if (total < buffer.length) Arrays.fill(buffer, total, buffer.length, (byte) 0);
        return total;
    }

    private void delete(Entry entry) throws IOException {
        try { DocumentsContract.deleteDocument(resolver, uri(entry.id)); }
        catch (RuntimeException error) { throw new IOException(error); }
    }

    private static void deleteLocal(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteLocal(child);
        file.delete();
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[1 << 16];
        for (int n; (n = in.read(buffer)) > 0;) out.write(buffer, 0, n);
    }
}
