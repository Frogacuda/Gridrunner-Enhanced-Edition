package org.openminotaur.gridrunnerplusplus;

import org.libsdl.app.SDLActivity;
import android.util.Log;
import android.content.pm.ActivityInfo;
import android.view.KeyEvent;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public final class GridrunnerActivity extends SDLActivity {
    private static final int GAME_ORIENTATION = 0x8052;
    private volatile int gameOrientation = 3; // Auto respects the system rotation lock.

    private void applyGameOrientation() {
        final int request;
        switch (gameOrientation) {
            case 0: request = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT; break;
            case 1: request = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE; break;
            case 2: request = ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE; break;
            default: request = ActivityInfo.SCREEN_ORIENTATION_FULL_USER; break;
        }
        setRequestedOrientation(request);
    }

    @Override public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        // SDL window sizing must not override the player's rotation policy.
        applyGameOrientation();
    }

    @Override protected boolean onUnhandledMessage(int command, Object param) {
        if (command == GAME_ORIENTATION && param instanceof Integer) {
            final int mode = (Integer) param;
            if (mode >= 0 && mode <= 3) {
                gameOrientation = mode;
                applyGameOrientation();
            }
            return true;
        }
        return super.onUnhandledMessage(command, param);
    }

    @Override public void onBackPressed() {
        // Route the system Back gesture through the same pause/back input as keys.
        SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_BACK);
        SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_BACK);
    }
    @Override protected String[] getArguments() {
        try {
            // std::ifstream reads ordinary files; assets remain private to this app.
            File directory = new File(getFilesDir(), "game-assets-15");
            File complete = new File(directory, ".complete");
            if (!complete.isFile()) {
                if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Asset directory");
                String prefix = directory.getCanonicalPath() + File.separator;
                try (ZipInputStream zip = new ZipInputStream(getAssets().open("grpp-assets.zip"))) {
                    byte[] buffer = new byte[32768];
                    ZipEntry entry;
                    while ((entry = zip.getNextEntry()) != null) {
                        File target = new File(directory, entry.getName());
                        if (!target.getCanonicalPath().startsWith(prefix)) throw new IOException("Invalid asset path");
                        if (entry.isDirectory()) {
                            if (!target.isDirectory() && !target.mkdirs()) throw new IOException("Asset subdirectory");
                        } else {
                            File parent = target.getParentFile();
                            if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Asset parent");
                            try (FileOutputStream output = new FileOutputStream(target)) {
                                int count;
                                while ((count = zip.read(buffer)) != -1) output.write(buffer, 0, count);
                            }
                        }
                        zip.closeEntry();
                    }
                }
                try (FileOutputStream marker = new FileOutputStream(complete)) { marker.getFD().sync(); }
            }
            return new String[] { "--assets", directory.getAbsolutePath() };
        } catch (IOException exception) {
            Log.e("Gridrunner++", "Could not unpack game assets", exception);
            throw new IllegalStateException("Could not prepare game assets", exception);
        }
    }
}
