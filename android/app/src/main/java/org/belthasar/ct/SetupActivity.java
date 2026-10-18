package org.belthasar.ct;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

// The first run: the player's ROM and ct_assets.dat (made from that ROM by tools/setup.py on a computer) are copied
// into the app's data folder through the system's file picker; the APK never carries them. Once both are there the
// game starts at once on every launch. They can also be copied by hand (adb push) -- docs/ANDROID.md.
public class SetupActivity extends Activity {
    static final String ROM = "Chrono_Trigger_USA_SNES.sfc";
    static final String ASSETS = "ct_assets.dat";
    static final String SRAM = "ct_sram.srm";
    static final int ROM_BYTES = 4 * 1024 * 1024;
    private static final int PICK_ROM = 1, PICK_ASSETS = 2, PICK_SRAM = 3;
    private TextView status;
    private Button start;

    private File dataDir() {
        File d = getExternalFilesDir(null);
        if (d == null) d = getFilesDir();
        d.mkdirs();
        return d;
    }

    private boolean ready() {
        File d = dataDir();
        File rom = new File(d, ROM), assets = new File(d, ASSETS);
        return rom.isFile() && rom.length() >= ROM_BYTES && assets.isFile() && assets.length() > 0;
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (ready()) { startGame(); return; }
        buildUi();
    }

    private void startGame() {
        startActivity(new Intent(this, GameActivity.class));
        finish();
    }

    private Button button(LinearLayout parent, String text, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setOnClickListener(l);
        parent.addView(b, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                                                        LinearLayout.LayoutParams.WRAP_CONTENT));
        return b;
    }

    private void buildUi() {
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(pad, pad, pad, pad);
        TextView title = new TextView(this);
        title.setText("Belthasar");
        title.setTextSize(22);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        col.addView(title);
        TextView info = new TextView(this);
        info.setText("This app does not include the game. Choose your own dump of the US ROM (" + ROM + ", 4 MB) "
                + "and the " + ASSETS + " made from it by tools/setup.py on a computer. They are copied into:\n"
                + dataDir().getAbsolutePath() + "\n(you can also copy them there by hand with adb push).");
        info.setPadding(0, pad / 2, 0, pad / 2);
        col.addView(info);
        status = new TextView(this);
        status.setTypeface(Typeface.MONOSPACE);
        status.setPadding(0, 0, 0, pad / 2);
        col.addView(status);
        button(col, "Choose the ROM (.sfc)", v -> pick(PICK_ROM));
        button(col, "Choose " + ASSETS, v -> pick(PICK_ASSETS));
        button(col, "Import a save (" + SRAM + ", optional)", v -> pick(PICK_SRAM));
        start = button(col, "Start the game", v -> startGame());
        ScrollView sv = new ScrollView(this);
        sv.addView(col);
        setContentView(sv);
        refresh();
    }

    private void refresh() {
        File d = dataDir();
        File rom = new File(d, ROM), assets = new File(d, ASSETS), sram = new File(d, SRAM);
        status.setText("ROM:        " + (rom.isFile() ? rom.length() + " bytes" : "missing") + "\n"
                     + "ct_assets:  " + (assets.isFile() ? assets.length() + " bytes" : "missing") + "\n"
                     + "save:       " + (sram.isFile() ? sram.length() + " bytes" : "none"));
        start.setEnabled(ready());
    }

    private void pick(int what) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        startActivityForResult(i, what);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        String name = request == PICK_ROM ? ROM : request == PICK_ASSETS ? ASSETS : SRAM;
        try {
            long n = copy(data.getData(), new File(dataDir(), name));
            if (request == PICK_ROM && n != ROM_BYTES && n != ROM_BYTES + 512)
                message("That file has " + n + " bytes; the US ROM has 4,194,304 (or 512 more with a copier header).");
            if (request == PICK_SRAM && n != 8192)
                message("That file has " + n + " bytes; a Chrono Trigger battery save has 8192.");
        } catch (Exception e) {
            message("Could not copy the file: " + e);
        }
        refresh();
    }

    private long copy(Uri from, File to) throws Exception {
        File tmp = new File(to.getPath() + ".part");
        long n = 0;
        try (InputStream in = getContentResolver().openInputStream(from); OutputStream out = new FileOutputStream(tmp)) {
            if (in == null) throw new Exception("no stream");
            byte[] buf = new byte[1 << 16];
            for (int k; (k = in.read(buf)) > 0; ) { out.write(buf, 0, k); n += k; }
        }
        if (to.exists() && !to.delete()) throw new Exception("cannot replace " + to);
        if (!tmp.renameTo(to)) throw new Exception("cannot write " + to);
        return n;
    }

    private void message(String m) {
        new AlertDialog.Builder(this).setMessage(m).setPositiveButton(android.R.string.ok, null).show();
    }
}
