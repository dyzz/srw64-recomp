package org.srw64.game;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.net.Uri;
import android.os.Environment;
import android.provider.MediaStore;

import java.io.IOException;
import java.io.OutputStream;

// Android 10+: app-owned downloads need no storage permission. Keep a new export
// hidden until the writer AND destination close; remove failed/partial downloads.
final class PublicExports {
    interface Writer { void write(OutputStream stream) throws IOException; }

    static String directory(String kind) {
        return Environment.DIRECTORY_DOWNLOADS + "/Marchwind64/" + kind;
    }

    static String write(Context context, String kind, String name, Writer writer) throws IOException {
        ContentResolver resolver = context.getContentResolver();
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
        values.put(MediaStore.MediaColumns.MIME_TYPE, "application/zip");
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, directory(kind) + "/");
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);
        Uri uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        if (uri == null) throw new IOException("Cannot create download");
        try {
            try (OutputStream stream = resolver.openOutputStream(uri, "w")) {
                if (stream == null) throw new IOException("Cannot open download");
                writer.write(stream);
            }
            values.clear();
            values.put(MediaStore.MediaColumns.IS_PENDING, 0);
            if (resolver.update(uri, values, null, null) != 1)
                throw new IOException("Cannot publish download");
            // MediaStore may adjust duplicate names; report the actual destination.
            try (android.database.Cursor cursor = resolver.query(uri,
                    new String[] { MediaStore.MediaColumns.DISPLAY_NAME }, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) name = cursor.getString(0);
            }
            return directory(kind) + "/" + name;
        } catch (IOException | RuntimeException error) {
            try { resolver.delete(uri, null, null); } catch (RuntimeException ignored) { }
            throw error;
        }
    }
}
