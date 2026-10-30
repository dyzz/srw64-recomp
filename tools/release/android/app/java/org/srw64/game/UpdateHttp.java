package org.srw64.game;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLConnection;

// The update check's GET on Android (src/host/update_http.cpp, over JNI from the check's own
// thread): HttpURLConnection, so the system's TLS and certificates, as the desktop builds use
// theirs. The whole body comes back at once; the host writes it where it goes.
final class UpdateHttp {
    // More than any text pack or latest.json; the host's own limits are tighter.
    private static final int LIMIT = 64 << 20;

    private UpdateHttp() {}

    // {error, body}: error null and the body's bytes on a 200, else the reason and null.
    static Object[] get(String address, String userAgent, int timeoutSeconds) {
        URLConnection connection = null;
        try {
            connection = new URL(address).openConnection();
            connection.setConnectTimeout(timeoutSeconds * 1000);
            // Per wait for data, so a long download is fine while it moves.
            connection.setReadTimeout(timeoutSeconds * 1000);
            connection.setRequestProperty("User-Agent", userAgent);
            // A file:// URL (a test) has no status.
            if (connection instanceof HttpURLConnection) {
                int code = ((HttpURLConnection) connection).getResponseCode();
                if (code != 200) return new Object[] {"HTTP " + code, null};
            }
            ByteArrayOutputStream body = new ByteArrayOutputStream();
            byte[] chunk = new byte[16384];
            try (InputStream in = connection.getInputStream()) {
                for (int n; (n = in.read(chunk)) > 0; ) {
                    if (body.size() + n > LIMIT) return new Object[] {"response too large", null};
                    body.write(chunk, 0, n);
                }
            }
            return new Object[] {null, body.toByteArray()};
        } catch (IOException | RuntimeException error) {
            String message = error.getMessage();
            return new Object[] {message == null ? error.getClass().getSimpleName() : message, null};
        } finally {
            if (connection instanceof HttpURLConnection) ((HttpURLConnection) connection).disconnect();
        }
    }
}
