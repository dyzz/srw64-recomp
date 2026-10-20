package org.srw64.game;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

// The supported ROM, checked by the launcher before the game runs. The host refuses any
// other file and stops before it draws anything (src/native/app/launch.cpp), which on
// Android only closes the game: a wrong file kept as rom.z64 looked like a crash at every
// start, with no way to choose again (a player, 0.4.9). The launcher now refuses it itself
// and asks again.
//
// Any of the three N64 byte orders is accepted and stored as .z64 (src/native/app/rom_order.hpp
// does the same on computers). SIZE and SHA256 are config/srw64-jp-rev0.json's
// (tests/test_android_rom_check.py keeps them equal).
final class RomCheck {
    static final long SIZE = 33554432L;
    static final String SHA256 = "ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e";

    // The same four refusals as rom_order.hpp's RomProblem, found in the same order: no file
    // to read, not an N64 dump, the wrong size, another or a patched game.
    enum Result { OK, MISSING, NOT_N64, WRONG_SIZE, WRONG_GAME }

    private RomCheck() { }

    // Copies a dump to out in .z64 order and tells whether it is the supported ROM. The
    // caller keeps out only on OK.
    static Result copy(InputStream in, OutputStream out) throws IOException {
        byte[] block = new byte[1 << 16];
        int first = fill(in, block);
        if (first < 4) return Result.NOT_N64;
        int order = order(block);
        if (order < 0) return Result.NOT_N64;
        MessageDigest digest = sha256();
        long total = 0;
        for (int n = first; n > 0; n = fill(in, block)) {
            total += n;
            if (total > SIZE) return Result.WRONG_SIZE;
            if (n % 4 != 0) return Result.WRONG_SIZE;
            swap(block, n, order);
            digest.update(block, 0, n);
            out.write(block, 0, n);
        }
        if (total != SIZE) return Result.WRONG_SIZE;
        return hex(digest.digest()).equals(SHA256) ? Result.OK : Result.WRONG_GAME;
    }

    // Whether a kept rom.z64 is the supported ROM. Before 0.4.10 the launcher copied the file
    // as it was, so a .v64 or .n64 dump (which the host turns into .z64 itself) still counts.
    static Result installed(File rom) {
        try (InputStream in = new FileInputStream(rom)) {
            // Not OutputStream.nullOutputStream(): Android has it only from 13.
            return copy(in, new OutputStream() {
                @Override public void write(int b) { }
                @Override public void write(byte[] b, int off, int len) { }
            });
        } catch (IOException error) {
            return Result.MISSING;
        }
    }

    // What the player is told, in the launcher's two languages: word for word the Chinese and
    // English of rom_refused_message (src/native/app/rom_order.hpp), so a refused ROM reads the
    // same on every platform (tests/test_android_rom_check.py compares them).
    static String explain(Result result) {
        switch (result) {
            case MISSING:
                return "找不到 ROM 文件，或者无法读取它。请确认文件没有被移动或删除。\n"
                        + "The ROM file is missing or cannot be read; check that it was not moved or deleted.";
            case NOT_N64:
                return "这不是 N64 ROM 文件。如果是 .zip、.7z 等压缩包，请先解压，再用里面的 .z64／.v64／.n64 文件。\n"
                        + "This is not an N64 ROM. If you have a .zip or .7z archive, extract it first and use the .z64, .v64 or .n64 file inside.";
            case WRONG_SIZE:
                return "ROM 大小不对（应为 32 MB），文件可能不完整或被改过。\n"
                        + "The ROM has the wrong size (it should be 32 MB); the file may be incomplete or modified.";
            default:
                return "这不是支持的 ROM：需要未修改的超级机器人大战 64 日版 Rev 0（不能是汉化版、补丁版或其他游戏）。\n"
                        + "This is not the supported ROM: it must be an unmodified Super Robot Taisen 64 (Japan, Rev 0), not a translated or patched one, nor another game.";
        }
    }

    // 0 for .z64 (80 37 12 40), 2 for .v64 (37 80 40 12), 4 for .n64 (40 12 37 80), -1 for none.
    private static int order(byte[] b) {
        int h = (b[0] & 0xFF) << 24 | (b[1] & 0xFF) << 16 | (b[2] & 0xFF) << 8 | (b[3] & 0xFF);
        if (h == 0x80371240) return 0;
        if (h == 0x37804012) return 2;
        if (h == 0x40123780) return 4;
        return -1;
    }

    private static void swap(byte[] b, int n, int order) {
        if (order == 2) {
            for (int i = 0; i < n; i += 2) { byte t = b[i]; b[i] = b[i + 1]; b[i + 1] = t; }
        } else if (order == 4) {
            for (int i = 0; i < n; i += 4) {
                byte t = b[i]; b[i] = b[i + 3]; b[i + 3] = t;
                t = b[i + 1]; b[i + 1] = b[i + 2]; b[i + 2] = t;
            }
        }
    }

    // Reads until the block is full or the stream ends: providers may return short reads,
    // which would split a swapped word across blocks.
    private static int fill(InputStream in, byte[] block) throws IOException {
        int n = 0;
        while (n < block.length) {
            int got = in.read(block, n, block.length - n);
            if (got < 0) break;
            n += got;
        }
        return n;
    }

    private static MessageDigest sha256() {
        try {
            return MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException error) {
            throw new IllegalStateException(error);
        }
    }

    private static String hex(byte[] bytes) {
        StringBuilder text = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) text.append(String.format("%02x", b & 0xFF));
        return text.toString();
    }
}
