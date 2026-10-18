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
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.security.MessageDigest;

// The first run: the player's ROM and ct_assets.dat (made from that ROM by tools/setup.py on a computer) are copied
// into the app's data folder through the system's file picker; the APK never carries them. Once both are there the
// game starts at once on every launch. They can also be copied by hand (adb push) -- docs/ANDROID.md.
// Each file is checked before it is kept, with the checks the engine makes (src/rom_verify.c CtRomVerify, src/assets.c
// CtAssetsOpen): a file that fails is dropped and the one already there stays, so the game never starts on one.
public class SetupActivity extends Activity {
    static final String ROM = "Chrono_Trigger_USA_SNES.sfc";
    static final String ASSETS = "ct_assets.dat";
    static final String SRAM = "ct_sram.srm";
    static final int ROM_BYTES = 4 * 1024 * 1024;
    static final String ROM_SHA1 = "de5822f4f2f7a55acb8926d4c0eaa63d5d989312";   // src/rom_verify.h CT_ROM_SHA1
    static final int SRAM_BYTES = 8192;
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
        return romProblem(new File(d, ROM)) == null && assetsProblem(new File(d, ASSETS)) == null;
    }

    // null = the supported dump (CtRomVerify: 4 MiB, or 512 bytes more with a copier header, and the image's SHA-1)
    static String romProblem(File f) {
        if (!f.isFile()) return "missing";
        long n = f.length();
        if (n != ROM_BYTES && n != ROM_BYTES + 512)
            return "it has " + n + " bytes; the US ROM has 4,194,304 (or 512 more with a copier header)";
        try (InputStream in = new FileInputStream(f)) {
            if (n == ROM_BYTES + 512 && in.skip(512) != 512) return "it could not be read";
            MessageDigest md = MessageDigest.getInstance("SHA-1");
            byte[] buf = new byte[1 << 16];
            for (int k; (k = in.read(buf)) > 0; ) md.update(buf, 0, k);
            StringBuilder hex = new StringBuilder();
            for (byte b : md.digest()) hex.append(String.format("%02x", b & 0xFF));
            if (!hex.toString().equals(ROM_SHA1))
                return "it is not the Chrono Trigger (USA) dump: SHA-1 " + hex + ", the supported one is " + ROM_SHA1;
        } catch (Exception e) {
            return "it could not be read (" + e.getMessage() + ")";
        }
        return null;
    }

    // null = a container the engine opens (CtAssetsOpen: "CTAS", the directory and every blob inside the file)
    static String assetsProblem(File f) {
        if (!f.isFile()) return "missing";
        long n = f.length();
        if (n < 12) return "it is not a ct_assets.dat (too short)";
        try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
            byte[] head = new byte[12];
            r.readFully(head);
            if (head[0] != 'C' || head[1] != 'T' || head[2] != 'A' || head[3] != 'S')
                return "it is not a ct_assets.dat (made by tools/setup.py from your ROM)";
            long count = le32(head, 8);
            if (12 + count * 16 > n) return "it is cut short (its directory runs past the end)";
            byte[] dir = new byte[(int) (count * 16)];
            r.readFully(dir);
            for (int i = 0; i < count; i++) {
                long off = le32(dir, i * 16 + 8), len = le32(dir, i * 16 + 12);
                if (off + len > n) return "it is cut short (an entry runs past the end)";
            }
        } catch (Exception e) {
            return "it could not be read (" + e.getMessage() + ")";
        }
        return null;
    }

    static String sramProblem(File f) {
        if (!f.isFile()) return "missing";
        return f.length() == SRAM_BYTES ? null
             : "it has " + f.length() + " bytes; a Chrono Trigger battery save has " + SRAM_BYTES;
    }

    private static long le32(byte[] b, int i) {
        return (b[i] & 0xFFL) | (b[i + 1] & 0xFFL) << 8 | (b[i + 2] & 0xFFL) << 16 | (b[i + 3] & 0xFFL) << 24;
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
                + "and the " + ASSETS + " made from it on a computer: Belthasar for Windows or Linux makes it when "
                + "you drop the ROM on it (it is in its data folder, e.g. %APPDATA%\\BelthasarProject\\Belthasar\\), "
                + "or tools/setup.py. Each file is checked before it is kept. They are copied into:\n"
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
        String rp = romProblem(rom), ap = assetsProblem(assets);
        status.setText("ROM:        " + (rp == null ? "verified" : rp) + "\n"
                     + "ct_assets:  " + (ap == null ? assets.length() + " bytes" : ap) + "\n"
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
        File to = new File(dataDir(), name), tmp = new File(to.getPath() + ".part");
        try {
            copy(data.getData(), tmp);
            String problem = request == PICK_ROM ? romProblem(tmp) : request == PICK_ASSETS ? assetsProblem(tmp)
                           : sramProblem(tmp);
            if (problem != null) {
                tmp.delete();
                message("That file was not kept: " + problem + ".");
            } else {
                if (to.exists() && !to.delete()) throw new Exception("cannot replace " + to);
                if (!tmp.renameTo(to)) throw new Exception("cannot write " + to);
            }
        } catch (Exception e) {
            tmp.delete();
            message("Could not copy the file: " + e.getMessage());
        }
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
