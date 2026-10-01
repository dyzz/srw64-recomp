package org.srw64.probe;

import org.libsdl.app.SDLActivity;

// SDL3's Java glue loads SDL3, then sdl2-compat (libSDL2.so), then libmain.so, whose
// SDL_main is the Plume clear probe (src/android/clear_probe.cpp).
public class ProbeActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "SDL2", "main" };
    }
}
