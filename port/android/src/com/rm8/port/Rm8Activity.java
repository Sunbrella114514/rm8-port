package com.rm8.port;

import org.libsdl.app.SDLActivity;

/** SDL2 activity; native code is libSDL2.so + libmain.so (port/app/rm8_app.cpp). */
public class Rm8Activity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
