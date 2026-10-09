package org.srw64.game;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public final class SaveExportTest {
    public static void main(String[] args) throws Exception {
        File root = new File(args[0]);
        byte[][] originals = new byte[SaveExport.NAMES.length][];
        for (int i = 0; i < originals.length; ++i) {
            originals[i] = new byte[32768 + i];
            Arrays.fill(originals[i], (byte) (i + 1));
            Files.write(new File(root, SaveExport.NAMES[i]).toPath(), originals[i]);
        }
        SaveExport snapshot = new SaveExport(root);
        // Opening the picker must freeze the exports, even if a second export changes them.
        for (String name : SaveExport.NAMES) Files.write(new File(root, name).toPath(), new byte[] { 9 });
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        snapshot.write(bytes);
        try (ZipInputStream zip = new ZipInputStream(new ByteArrayInputStream(bytes.toByteArray()))) {
            for (int i = 0; i < originals.length; ++i) {
                ZipEntry entry = zip.getNextEntry();
                if (entry == null || !entry.getName().equals(SaveExport.NAMES[i]) || !Arrays.equals(zip.readAllBytes(), originals[i]))
                    throw new AssertionError("Archive changed emulator file " + i);
            }
            if (zip.getNextEntry() != null) throw new AssertionError("Extra ZIP entry");
        }
        try {
            snapshot.write(new java.io.OutputStream() {
                public void write(int value) throws IOException { throw new IOException("Destination full"); }
            });
            throw new AssertionError("Failed output reported success");
        } catch (IOException expected) { }
        try {
            snapshot.write(new ByteArrayOutputStream() {
                public void close() throws IOException { throw new IOException("Destination close failed"); }
            });
            throw new AssertionError("Failed close reported success");
        } catch (IOException expected) { }
        Files.delete(new File(root, SaveExport.NAMES[0]).toPath());
        try { new SaveExport(root); throw new AssertionError("Missing export accepted"); }
        catch (IOException expected) { }
        Files.write(new File(root, SaveExport.NAMES[0]).toPath(), new byte[0]);
        try { new SaveExport(root); throw new AssertionError("Empty export accepted"); }
        catch (IOException expected) { }
        System.out.println("Android save ZIP: bytes, snapshot, write failure and missing files passed");
    }
}
