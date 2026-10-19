package org.srw64.game;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.util.Arrays;

// tests/test_android_rom_check.py: the launcher's ROM check without Android. args[0] is a
// scratch folder, args[1] the supported .z64 when there is one.
public final class RomCheckTest {
    public static void main(String[] args) throws Exception {
        File scratch = new File(args[0]);
        // No ROM needed: an archive, a short file and a full-size dump of something else.
        expect(RomCheck.Result.NOT_N64, new byte[] { 'P', 'K', 3, 4, 0, 0, 0, 0 }, "zip");
        byte[] other = new byte[(int) RomCheck.SIZE];
        other[0] = (byte) 0x80; other[1] = 0x37; other[2] = 0x12; other[3] = 0x40;
        expect(RomCheck.Result.WRONG_SIZE, Arrays.copyOf(other, 1 << 20), "short");
        expect(RomCheck.Result.WRONG_GAME, other, "other game");
        expect(RomCheck.Result.WRONG_SIZE, Arrays.copyOf(other, other.length + 4), "long");
        if (args.length < 2) return;

        byte[] z64 = Files.readAllBytes(new File(args[1]).toPath());
        byte[] v64 = z64.clone(), n64 = z64.clone();
        for (int i = 0; i < v64.length; i += 2) { byte t = v64[i]; v64[i] = v64[i + 1]; v64[i + 1] = t; }
        for (int i = 0; i < n64.length; i += 4) {
            byte t = n64[i]; n64[i] = n64[i + 3]; n64[i + 3] = t;
            t = n64[i + 1]; n64[i + 1] = n64[i + 2]; n64[i + 2] = t;
        }
        for (byte[] dump : new byte[][] { z64, v64, n64 }) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            // A provider that hands the bytes over in odd pieces.
            InputStream ragged = new FilterInputStream(new ByteArrayInputStream(dump)) {
                @Override public int read(byte[] b, int off, int len) throws IOException { return super.read(b, off, Math.min(len, 4093)); }
            };
            RomCheck.Result result = RomCheck.copy(ragged, out);
            if (result != RomCheck.Result.OK) throw new AssertionError("dump refused: " + result);
            if (!Arrays.equals(out.toByteArray(), z64)) throw new AssertionError("not stored as .z64");
        }
        byte[] patched = z64.clone();
        patched[0x20] ^= 1;
        expect(RomCheck.Result.WRONG_GAME, patched, "patched");
        // A kept file: the .z64, and a .v64 kept by an older launcher, count; a patched one does not.
        File kept = new File(scratch, "rom.z64");
        for (byte[] dump : new byte[][] { z64, v64 }) {
            Files.write(kept.toPath(), dump);
            if (RomCheck.installed(kept) != RomCheck.Result.OK) throw new AssertionError("kept ROM refused");
        }
        Files.write(kept.toPath(), patched);
        if (RomCheck.installed(kept) != RomCheck.Result.WRONG_GAME) throw new AssertionError("kept patched ROM accepted");
    }

    private static void expect(RomCheck.Result want, byte[] bytes, String label) throws IOException {
        RomCheck.Result got = RomCheck.copy(new ByteArrayInputStream(bytes), new ByteArrayOutputStream());
        if (got != want) throw new AssertionError(label + ": " + got + " instead of " + want);
    }
}
