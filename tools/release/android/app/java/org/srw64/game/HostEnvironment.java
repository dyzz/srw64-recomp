package org.srw64.game;

import android.system.ErrnoException;
import android.system.Os;

import java.util.LinkedHashMap;
import java.util.Map;

// The variables the app sets for the game, kept so the game can also ask for them
// (SRW64Activity.hostEnvironment, src/host/host.cpp). The arm64 game run by ARM
// translation on an x86 device or emulator has a C library of its own whose environment
// never sees Os.setenv: without this it found no resource folder and stopped on the fonts.
final class HostEnvironment {
    private static final Map<String, String> values = new LinkedHashMap<>();

    private HostEnvironment() { }

    static synchronized void set(String key, String value) throws ErrnoException {
        Os.setenv(key, value, true);
        values.put(key, value);
    }

    static synchronized String[] pairs() {
        String[] pairs = new String[values.size()];
        int i = 0;
        for (Map.Entry<String, String> entry : values.entrySet()) pairs[i++] = entry.getKey() + "=" + entry.getValue();
        return pairs;
    }
}
