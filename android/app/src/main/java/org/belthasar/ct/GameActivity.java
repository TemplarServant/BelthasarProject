package org.belthasar.ct;

import org.libsdl.app.SDLActivity;

// SDL's activity, running libmain.so's SDL_main (src/field.c's main). Its data folder is getExternalFilesDir(null),
// where SetupActivity put the player's files (src/android.c reads the same folder).
public class GameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
