package org.belthasar.ct;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.Intent;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.List;

// The first run: the game's data the player's computer made from their own dump (Belthasar for Windows or Linux, or
// tools/setup.py) is copied into the app's data folder through the system's file picker; the APK never carries any of
// it. Once ct_assets.dat is there the game starts at once on every launch. The files can also be copied by hand (adb
// push) -- docs/ANDROID.md.
// One picker takes every file at once; each is known by its CONTENT, not its name (a cloud drive may rename it), and
// checked with the checks the engine makes (src/assets.c CtAssetsOpen, src/datapack.c CtDataPackFileHasTables,
// src/widext.c's header): a file that fails is dropped with the reason and the one already there stays.
public class SetupActivity extends Activity {
    static final String ASSETS = "ct_assets.dat";
    static final String TABLES = "ct_tables.dat";      // an earlier setup's separate table pack (today's are inside ASSETS)
    static final String WIDE = "ct_wide.dat";
    static final String SRAM = "ct_sram.srm";
    static final int SRAM_BYTES = 8192;
    static final int ROM_BYTES = 4 * 1024 * 1024;
    static final int T_ASSET_TABLE = 37;               // src/assets.h CT_ASSET_TABLE: the container is a ROM-free one
    static final int T_TABLES_HEADER = 64;             // tools/gen/pack_tables.py: the table pack's header entry
    private static final int PICK_FILES = 1;
    private TextView status;
    private Button start;

    private File dataDir() {
        File d = getExternalFilesDir(null);
        if (d == null) d = getFilesDir();
        d.mkdirs();
        return d;
    }

    // What a CTAS container holds: the entry types of its directory (null = not one the engine opens).
    static final class Ctas { boolean assets, romFree, tables; }

    static Ctas ctas(File f) {
        long n = f.length();
        if (!f.isFile() || n < 12) return null;
        try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
            byte[] head = new byte[12];
            r.readFully(head);
            if (head[0] != 'C' || head[1] != 'T' || head[2] != 'A' || head[3] != 'S') return null;
            long count = le32(head, 8);
            if (12 + count * 16 > n) return null;
            byte[] dir = new byte[(int) (count * 16)];
            r.readFully(dir);
            Ctas c = new Ctas();
            for (int i = 0; i < count; i++) {
                long type = le32(dir, i * 16), off = le32(dir, i * 16 + 8), len = le32(dir, i * 16 + 12);
                if (off + len > n) return null;            // cut short: an entry runs past the end
                if (type == T_TABLES_HEADER) c.tables = true;
                else if (type >= 64 && type <= 67) continue;
                else { c.assets = true; if (type == T_ASSET_TABLE) c.romFree = true; }
            }
            return c;
        } catch (Exception e) {
            return null;
        }
    }

    // null = a ct_assets.dat the game runs on: the engine opens it, it needs no ROM, the tables are inside or beside it.
    String assetsProblem() {
        File d = dataDir(), f = new File(d, ASSETS);
        if (!f.isFile()) return "missing";
        Ctas c = ctas(f);
        if (c == null || !c.assets) return "damaged, or not a " + ASSETS;
        if (!c.romFree) return "an older " + ASSETS + " that needs the ROM: make a new one with today's Belthasar";
        if (!c.tables) {
            Ctas t = ctas(new File(d, TABLES));
            if (t == null || !t.tables) return "it has no tables: make a new one with today's Belthasar";
        }
        return null;
    }

    private static boolean wideOk(File f) {             // src/widext.c: "CTWD", version 3
        if (!f.isFile() || f.length() < 20) return false;
        try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
            byte[] h = new byte[8];
            r.readFully(h);
            return h[0] == 'C' && h[1] == 'T' && h[2] == 'W' && h[3] == 'D' && le32(h, 4) == 3;
        } catch (Exception e) {
            return false;
        }
    }

    // The name a picked file is kept under, from what it holds; null with the reason in why[0] when it is none of them.
    private static String kind(File f, String[] why) {
        Ctas c = ctas(f);
        if (c != null && c.assets) {
            if (!c.romFree) { why[0] = "an older " + ASSETS + " that needs the ROM: make a new one with today's Belthasar"; return null; }
            return ASSETS;
        }
        if (c != null && c.tables) return TABLES;
        if (wideOk(f)) return WIDE;
        long n = f.length();
        if (n == SRAM_BYTES) return SRAM;
        if (n == ROM_BYTES || n == ROM_BYTES + 512)
            why[0] = "it looks like the ROM; the app takes the " + ASSETS + " a computer made from it";
        else
            why[0] = "it is not a " + ASSETS + ", a " + WIDE + " or an " + SRAM_BYTES + "-byte battery save";
        return null;
    }

    private static long le32(byte[] b, int i) {
        return (b[i] & 0xFFL) | (b[i + 1] & 0xFFL) << 8 | (b[i + 2] & 0xFFL) << 16 | (b[i + 3] & 0xFFL) << 24;
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (assetsProblem() == null) { startGame(); return; }
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
        info.setText("This app does not include the game. Its data is made on a computer from your own dump of the US "
                + "ROM: Belthasar for Windows or Linux makes it when you choose the ROM on its first screen. Copy from "
                + "its data folder (e.g. %APPDATA%\\BelthasarProject\\Belthasar\\ or "
                + "~/.local/share/BelthasarProject/Belthasar/) to this device:\n"
                + "  " + ASSETS + "  the game's data (needed)\n"
                + "  " + WIDE + "  the Steam release's widescreen margins (optional; made when Steam's copy is installed)\n"
                + "  " + SRAM + "  your battery save (optional)\n"
                + "then choose them below, all at once or one by one. Each file is checked before it is kept. They are "
                + "copied into:\n" + dataDir().getAbsolutePath() + "\n(you can also copy them there by hand with adb push).");
        info.setPadding(0, pad / 2, 0, pad / 2);
        col.addView(info);
        status = new TextView(this);
        status.setTypeface(Typeface.MONOSPACE);
        status.setPadding(0, 0, 0, pad / 2);
        col.addView(status);
        button(col, "Choose the game's files", v -> pick());
        start = button(col, "Start the game", v -> startGame());
        ScrollView sv = new ScrollView(this);
        sv.addView(col);
        setContentView(sv);
        refresh();
    }

    private void refresh() {
        File d = dataDir();
        File assets = new File(d, ASSETS), wide = new File(d, WIDE), sram = new File(d, SRAM);
        String ap = assetsProblem();
        status.setText("ct_assets:      " + (ap == null ? assets.length() + " bytes" : ap) + "\n"
                     + "Steam margins:  " + (wideOk(wide) ? wide.length() + " bytes" : "none (optional)") + "\n"
                     + "save:           " + (sram.isFile() ? sram.length() + " bytes" : "none"));
        start.setEnabled(ap == null);
    }

    private void pick() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        startActivityForResult(i, PICK_FILES);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_FILES || result != RESULT_OK || data == null) return;
        List<Uri> uris = new ArrayList<>();
        ClipData clip = data.getClipData();
        if (clip != null) for (int k = 0; k < clip.getItemCount(); k++) uris.add(clip.getItemAt(k).getUri());
        else if (data.getData() != null) uris.add(data.getData());
        StringBuilder report = new StringBuilder();
        for (int k = 0; k < uris.size(); k++) {
            File tmp = new File(dataDir(), "picked-" + k + ".part");
            try {
                copy(uris.get(k), tmp);
                String[] why = { null };
                String name = kind(tmp, why);
                if (name == null) {
                    tmp.delete();
                    report.append("Not kept: ").append(why[0]).append(".\n");
                    continue;
                }
                File to = new File(dataDir(), name);
                if (to.exists() && !to.delete()) throw new Exception("cannot replace " + to);
                if (!tmp.renameTo(to)) throw new Exception("cannot write " + to);
                report.append("Kept as ").append(name).append(".\n");
            } catch (Exception e) {
                tmp.delete();
                report.append("Could not copy a file: ").append(e.getMessage()).append("\n");
            }
        }
        if (report.length() > 0) message(report.toString().trim());
        refresh();
    }

    private void copy(Uri from, File tmp) throws Exception {
        try (InputStream in = getContentResolver().openInputStream(from); OutputStream out = new FileOutputStream(tmp)) {
            if (in == null) throw new Exception("no stream");
            byte[] buf = new byte[1 << 16];
            for (int k; (k = in.read(buf)) > 0; ) out.write(buf, 0, k);
        }
    }

    private void message(String m) {
        new AlertDialog.Builder(this).setMessage(m).setPositiveButton(android.R.string.ok, null).show();
    }
}
