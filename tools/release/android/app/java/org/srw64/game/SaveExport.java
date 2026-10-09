package org.srw64.game;

import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

// A snapshot taken before opening the picker: the game may resume while the user
// chooses a destination. Never read live saves while writing to a document provider.
final class SaveExport {
    static final String[] NAMES = { "srw64-ares.ram", "srw64-project64.sra",
        "srw64-mupen64plus.sra", "srw64-retroarch.srm" };
    private final byte[][] files;

    SaveExport(File directory) throws IOException {
        files = new byte[NAMES.length][];
        for (int i = 0; i < NAMES.length; ++i) {
            File file = new File(directory, NAMES[i]);
            if (!file.isFile() || file.length() == 0 || file.length() > 1024 * 1024)
                throw new IOException("Missing or invalid save export: " + NAMES[i]);
            files[i] = Files.readAllBytes(file.toPath());
        }
    }

    void write(OutputStream destination) throws IOException {
        try (ZipOutputStream zip = new ZipOutputStream(destination)) {
            for (int i = 0; i < NAMES.length; ++i) {
                zip.putNextEntry(new ZipEntry(NAMES[i]));
                zip.write(files[i]);
                zip.closeEntry();
            }
        }
    }
}
