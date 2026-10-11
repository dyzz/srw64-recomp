package org.srw64.game;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.net.Uri;
import android.provider.MediaStore;

import java.io.IOException;
import java.io.OutputStream;

// Android 10+, without a player's folder (PlayerFolder): files the app adds to
// Documents/Marchwind64 need no storage permission. Keep a new export hidden until the
// writer AND destination close; remove failed/partial files.
final class PublicExports {
    interface Writer { void write(OutputStream stream) throws IOException; }

    static String directory(String kind) {
        return PlayerFolder.SUGGESTED + "/" + kind;
    }

    static String write(Context context, String kind, String name, Writer writer) throws IOException {
        ContentResolver resolver = context.getContentResolver();
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
        values.put(MediaStore.MediaColumns.MIME_TYPE, "application/zip");
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, directory(kind) + "/");
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);
        Uri uri = resolver.insert(MediaStore.Files.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY), values);
        if (uri == null) throw new IOException("Cannot create the export");
        try {
            try (OutputStream stream = resolver.openOutputStream(uri, "w")) {
                if (stream == null) throw new IOException("Cannot open the export");
                writer.write(stream);
            }
            values.clear();
            values.put(MediaStore.MediaColumns.IS_PENDING, 0);
            if (resolver.update(uri, values, null, null) != 1)
                throw new IOException("Cannot publish the export");
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
