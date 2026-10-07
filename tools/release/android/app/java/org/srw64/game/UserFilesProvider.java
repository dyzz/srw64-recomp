package org.srw64.game;

import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.provider.DocumentsContract.Root;
import android.provider.DocumentsProvider;
import android.webkit.MimeTypeMap;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

// The player's data folder (files/user: saves, filters, bezels, the HD pack) as a place in
// the system Files app and in any app's file picker, so a player can copy RetroArch
// filters and bezels in, or back up their saves (docs/native/bezels-and-filters.md). The
// app's own files are otherwise out of reach without root. Document ids are "user" for
// the folder and "user/<path inside it>" below it.
public class UserFilesProvider extends DocumentsProvider {
    static final String AUTHORITY = "org.srw64.game.user";
    static final String ROOT = "user";
    private static final String[] ROOT_COLUMNS = {Root.COLUMN_ROOT_ID, Root.COLUMN_FLAGS, Root.COLUMN_TITLE,
        Root.COLUMN_SUMMARY, Root.COLUMN_DOCUMENT_ID, Root.COLUMN_ICON, Root.COLUMN_AVAILABLE_BYTES};
    private static final String[] DOCUMENT_COLUMNS = {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
        Document.COLUMN_MIME_TYPE, Document.COLUMN_FLAGS, Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED};

    private File base;

    @Override
    public boolean onCreate() {
        base = new File(getContext().getFilesDir(), "user");
        base.mkdirs();
        return true;
    }

    // The URI of a folder inside the data folder ("" for the folder itself), for the Files app.
    static Uri folderUri(String relative) {
        return DocumentsContract.buildDocumentUri(AUTHORITY, relative.isEmpty() ? ROOT : ROOT + "/" + relative);
    }

    // build_game.py compiles the Java without generated resource ids (no R class).
    private String text(String name) {
        Context context = getContext();
        int id = context.getResources().getIdentifier(name, "string", context.getPackageName());
        return id != 0 ? context.getString(id) : name;
    }

    private File file(String id) throws FileNotFoundException {
        try {
            if (!id.equals(ROOT) && !id.startsWith(ROOT + "/")) throw new FileNotFoundException(id);
            File target = id.equals(ROOT) ? base : new File(base, id.substring(ROOT.length() + 1));
            String root = base.getCanonicalPath(), path = target.getCanonicalPath();
            if (!path.equals(root) && !path.startsWith(root + File.separator)) throw new FileNotFoundException(id);
            return target;
        } catch (IOException error) {
            throw new FileNotFoundException(id);
        }
    }

    private String id(File file) throws FileNotFoundException {
        try {
            String root = base.getCanonicalPath(), path = file.getCanonicalPath();
            return path.equals(root) ? ROOT : ROOT + "/" + path.substring(root.length() + 1);
        } catch (IOException error) {
            throw new FileNotFoundException(file.getPath());
        }
    }

    private static String mime(File file) {
        if (file.isDirectory()) return Document.MIME_TYPE_DIR;
        String name = file.getName();
        int dot = name.lastIndexOf('.');
        if (dot >= 0) {
            String type = MimeTypeMap.getSingleton().getMimeTypeFromExtension(name.substring(dot + 1).toLowerCase());
            if (type != null) return type;
        }
        return "application/octet-stream";
    }

    private void row(MatrixCursor cursor, File file) throws FileNotFoundException {
        String id = id(file);
        int flags = file.isDirectory() ? Document.FLAG_DIR_SUPPORTS_CREATE : Document.FLAG_SUPPORTS_WRITE;
        if (!id.equals(ROOT)) flags |= Document.FLAG_SUPPORTS_DELETE | Document.FLAG_SUPPORTS_RENAME;
        String name = id.equals(ROOT) ? text("app_name") : file.getName();
        cursor.newRow()
            .add(Document.COLUMN_DOCUMENT_ID, id)
            .add(Document.COLUMN_DISPLAY_NAME, name)
            .add(Document.COLUMN_MIME_TYPE, mime(file))
            .add(Document.COLUMN_FLAGS, flags)
            .add(Document.COLUMN_SIZE, file.isDirectory() ? null : file.length())
            .add(Document.COLUMN_LAST_MODIFIED, file.lastModified());
    }

    @Override
    public Cursor queryRoots(String[] projection) {
        MatrixCursor cursor = new MatrixCursor(projection != null ? projection : ROOT_COLUMNS);
        Context context = getContext();
        cursor.newRow()
            .add(Root.COLUMN_ROOT_ID, ROOT)
            .add(Root.COLUMN_FLAGS, Root.FLAG_SUPPORTS_CREATE | Root.FLAG_SUPPORTS_IS_CHILD | Root.FLAG_LOCAL_ONLY)
            .add(Root.COLUMN_TITLE, text("app_name"))
            .add(Root.COLUMN_SUMMARY, text("user_files_summary"))
            .add(Root.COLUMN_DOCUMENT_ID, ROOT)
            .add(Root.COLUMN_ICON, context.getApplicationInfo().icon)
            .add(Root.COLUMN_AVAILABLE_BYTES, base.getFreeSpace());
        return cursor;
    }

    @Override
    public Cursor queryDocument(String documentId, String[] projection) throws FileNotFoundException {
        MatrixCursor cursor = new MatrixCursor(projection != null ? projection : DOCUMENT_COLUMNS);
        File file = file(documentId);
        if (!file.exists()) throw new FileNotFoundException(documentId);
        row(cursor, file);
        return cursor;
    }

    @Override
    public Cursor queryChildDocuments(String parentId, String[] projection, String sortOrder) throws FileNotFoundException {
        MatrixCursor cursor = new MatrixCursor(projection != null ? projection : DOCUMENT_COLUMNS);
        File[] children = file(parentId).listFiles();
        if (children != null) for (File child : children) row(cursor, child);
        return cursor;
    }

    @Override
    public ParcelFileDescriptor openDocument(String documentId, String mode, CancellationSignal signal) throws FileNotFoundException {
        return ParcelFileDescriptor.open(file(documentId), ParcelFileDescriptor.parseMode(mode));
    }

    @Override
    public String createDocument(String parentId, String mimeType, String displayName) throws FileNotFoundException {
        File parent = file(parentId);
        File target = new File(parent, displayName);
        // The Files app asks again with the same name for a copy; keep both, as it would.
        for (int n = 1; target.exists(); ++n) {
            int dot = displayName.lastIndexOf('.');
            String name = Document.MIME_TYPE_DIR.equals(mimeType) || dot <= 0
                ? displayName + " (" + n + ")" : displayName.substring(0, dot) + " (" + n + ")" + displayName.substring(dot);
            target = new File(parent, name);
        }
        try {
            boolean made = Document.MIME_TYPE_DIR.equals(mimeType) ? target.mkdir() : target.createNewFile();
            if (!made) throw new FileNotFoundException("cannot create " + displayName);
        } catch (IOException error) {
            throw new FileNotFoundException(error.getMessage());
        }
        return id(target);
    }

    @Override
    public String renameDocument(String documentId, String displayName) throws FileNotFoundException {
        File file = file(documentId), target = new File(file.getParentFile(), displayName);
        if (documentId.equals(ROOT) || target.exists() || !file.renameTo(target)) throw new FileNotFoundException("cannot rename " + documentId);
        return id(target);
    }

    @Override
    public void deleteDocument(String documentId) throws FileNotFoundException {
        if (documentId.equals(ROOT) || !delete(file(documentId))) throw new FileNotFoundException("cannot delete " + documentId);
    }

    private static boolean delete(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) delete(child);
        return file.delete();
    }

    // The Files app asks for the chain of folders down to a document it is told to open.
    @Override
    public DocumentsContract.Path findDocumentPath(String parentId, String childId) throws FileNotFoundException {
        file(childId);
        String top = parentId != null ? parentId : ROOT;
        if (!childId.equals(top) && !isChildDocument(top, childId)) throw new FileNotFoundException(childId);
        List<String> chain = new ArrayList<>();
        chain.add(top);
        if (!childId.equals(top)) {
            String id = top;
            for (String part : childId.substring(top.length() + 1).split("/")) {
                id += "/" + part;
                chain.add(id);
            }
        }
        return new DocumentsContract.Path(parentId == null ? ROOT : null, chain);
    }

    @Override
    public String getDocumentType(String documentId) throws FileNotFoundException {
        return mime(file(documentId));
    }

    @Override
    public boolean isChildDocument(String parentId, String documentId) {
        return documentId.startsWith(parentId + "/");
    }
}
