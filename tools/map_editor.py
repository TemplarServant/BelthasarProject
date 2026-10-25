#!/usr/bin/env python3
"""Belthasar's map editor -- paint the world maps' blocks, RPG Maker style (a mod's data: docs/WIDESCREEN.md).

    python tools/map_editor.py [--assets ct_assets.dat] [--edits ct_mapedits.dat] [--world 7]

It draws a world map from the player's own ct_assets.dat (its block grids, block definitions, tiles and colours -- nothing
of the ROM is in this file or in what it saves) and paints blocks into the cells the widescreen's sides draw past a map's
own patch: 1999 A.D.'s map (world 7) is a patch of the world round Truce and Guardia, and past it Belthasar shows 1000
A.D.'s geography in 1999's tiles (the scene adjustments, src/field.c). Those cells are the editable ones; the patch itself
(the ROM's map, the game's playfield) is drawn but locked.

The edits go to ct_mapedits.dat (b"CTME", u32 version 1, u32 count, per edit: u8 world, u8 layer (0 BG1, 1 BG2), u8 x,
u8 y (the 96 x 64 block grid), u8 block, u8 0). Belthasar reads it from CT_MAPEDITS, else the working folder, else the
executable's, and reads it again when it changes -- paint here with the game open and the sides follow.

Mouse: left paints with the selected block (drag to paint more), right picks the block under the cursor, middle drags the
view; wheel scrolls (Shift: sideways, Ctrl: zoom). Tools (keys): P pencil, R rectangle, F fill (bucket), E erase (the cell
goes back to 1000 A.D.'s own block). Ctrl+Z undo, Ctrl+Y redo, Ctrl+S save, G grid, B the patch's border, H highlight the
painted cells, 1 / 2 the layer painted (BG1 overlay / BG2 ground), V the other layer shown or hidden.
"""
import argparse, os, struct, sys
import tkinter as tk
from tkinter import messagebox
from PIL import Image, ImageDraw, ImageTk

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
CT_ASSET_WORLD = 17
GW, GH = 96, 64                     # the block grid (16 x 16 px blocks): 1536 x 1024 px
EXT_WORLDS = {7: 0}                 # a patch world -> the world whose geography goes on past it (src/field.c worldInit)


# ------------------------------------------------------------------------------------------------------------- the data
def read_assets(path):
    d = open(path, 'rb').read()
    if d[:4] != b'CTAS':
        sys.exit('map-editor: %s is not a ct_assets.dat' % path)
    n = struct.unpack_from('<I', d, 8)[0]
    out = {}
    for i in range(n):
        t, ident, off, ln = struct.unpack_from('<IIII', d, 12 + 16 * i)
        out[(t, ident)] = d[off:off + ln]
    return out


class World:
    """One world's blob (src/field.c worldInit): the grids $4000 (BG1) / $5800 (BG2), the block definitions $3000 / $3800,
    the colours $7F:8C00, the tiles at VRAM $4000 (tile t at 32 t)."""
    def __init__(self, blob):
        p = 6
        self.grid = [bytearray(blob[p:p + 6144]), bytearray(blob[p + 6144:p + 12288])]; p += 12288
        self.defs = [blob[p:p + 2048], blob[p + 2048:p + 4096]]; p += 4096
        pal = blob[p:p + 512]; p += 512
        p += 512 + 3072 + 16
        nrec = blob[p]; p += 1 + 7 * nrec
        self.tiles = blob[p:p + 28672]
        self.rgb = []
        for c in range(256):
            v = pal[2 * c] | pal[2 * c + 1] << 8
            self.rgb.append(tuple(((v >> s) & 31) * 255 // 31 for s in (0, 5, 10)))

    def block_image(self, layer, block):
        """The block's 16 x 16 picture (RGBA; BG1's colour 0 see-through, BG2's opaque)."""
        im = Image.new('RGBA', (16, 16), (0, 0, 0, 0)); px = im.load()
        d = self.defs[layer]
        for corner in range(4):
            o = block * 8 + corner * 2
            chip = d[o] | d[o + 1] << 8
            t, pb = chip & 0x3FF, ((chip >> 10) & 7) * 16
            ox, oy = (corner & 1) * 8, (corner >> 1) * 8
            for ty in range(8):
                yy = 7 - ty if chip & 0x8000 else ty
                b = t * 32 + yy * 2
                if b + 17 >= len(self.tiles):
                    continue
                for tx in range(8):
                    xx = 7 - tx if chip & 0x4000 else tx; k = 7 - xx
                    v = ((self.tiles[b] >> k) & 1) | (((self.tiles[b + 1] >> k) & 1) << 1) | \
                        (((self.tiles[b + 16] >> k) & 1) << 2) | (((self.tiles[b + 17] >> k) & 1) << 3)
                    if v == 0 and layer == 0:
                        continue
                    px[ox + tx, oy + ty] = self.rgb[pb + v] + (255,)
        return im


def read_edits(path):
    edits = {}
    if os.path.exists(path):
        d = open(path, 'rb').read()
        if d[:4] == b'CTME' and len(d) >= 12:
            n = struct.unpack_from('<I', d, 8)[0]
            for i in range(n):
                o = 12 + 6 * i
                if o + 6 > len(d): break
                w, layer, x, y, blk = d[o:o + 5]
                edits[(w, layer, x, y)] = blk
    return edits


def write_edits(path, edits):
    items = sorted(edits.items())
    with open(path + '.tmp', 'wb') as f:
        f.write(b'CTME' + struct.pack('<II', 1, len(items)))
        for (w, layer, x, y), blk in items:
            f.write(bytes([w, layer, x, y, blk, 0]))
    os.replace(path + '.tmp', path)


# ----------------------------------------------------------------------------------------------------------- the editor
class Editor:
    ZOOMS = (1, 2, 3, 4)

    def __init__(self, root, assets, edits_path, world):
        self.root, self.assets, self.edits_path = root, assets, edits_path
        self.edits = read_edits(edits_path)
        self.undo, self.redo = [], []
        self.layer, self.tool, self.sel = 1, 'pencil', 0
        self.show = [True, True]; self.grid_on = False; self.border_on = True; self.hl_on = False
        self.zoom = 2; self.dirty = False; self.rect0 = None; self.stroke = None
        self.build_ui()
        self.load_world(world)

    # -- the world and its picture
    def load_world(self, wid):
        blob = self.assets.get((CT_ASSET_WORLD, wid))
        if not blob:
            messagebox.showerror('map editor', 'world %d is not in the assets' % wid); return
        self.wid, self.w = wid, World(blob)
        src = EXT_WORLDS.get(wid)
        self.ext = World(self.assets[(CT_ASSET_WORLD, src)]).grid if src is not None else None
        self.cache = {}
        self.world_var.set(wid)
        self.compose_all(); self.draw_palette(); self.status()

    def editable(self, x, y):
        """A cell past the patch (the world's BG2 grid empty there): the sides' own, not the ROM's playfield."""
        return self.ext is not None and self.w.grid[1][y * GW + x] == 0

    def block_at(self, layer, x, y):
        o = y * GW + x
        if self.editable(x, y):
            return self.edits.get((self.wid, layer, x, y), self.ext[layer][o])
        return self.w.grid[layer][o]

    def bimg(self, layer, block):
        k = (layer, block)
        if k not in self.cache: self.cache[k] = self.w.block_image(layer, block)
        return self.cache[k]

    def compose_cell(self, x, y):
        cell = Image.new('RGBA', (16, 16), (0, 0, 0, 255))
        for layer in (1, 0):
            if self.show[layer]:
                cell.alpha_composite(self.bimg(layer, self.block_at(layer, x, y)))
        if self.hl_on and any((self.wid, l, x, y) in self.edits for l in (0, 1)):
            cell.alpha_composite(Image.new('RGBA', (16, 16), (255, 230, 0, 70)))
        self.map_img.paste(cell, (x * 16, y * 16))

    def compose_all(self):
        self.map_img = Image.new('RGBA', (GW * 16, GH * 16), (0, 0, 0, 255))
        for y in range(GH):
            for x in range(GW):
                self.compose_cell(x, y)
        self.refresh_view()

    def refresh_view(self):
        z = self.zoom
        im = self.map_img.resize((GW * 16 * z, GH * 16 * z), Image.NEAREST)
        d = ImageDraw.Draw(im)
        if self.grid_on:
            for x in range(0, GW * 16 * z, 16 * z): d.line([(x, 0), (x, GH * 16 * z)], fill=(80, 80, 80, 255))
            for y in range(0, GH * 16 * z, 16 * z): d.line([(0, y), (GW * 16 * z, y)], fill=(80, 80, 80, 255))
        if self.border_on and self.ext is not None:          # the patch's edge: cells whose neighbour is editable
            for y in range(GH):
                for x in range(GW):
                    if self.editable(x, y): continue
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < GW and 0 <= ny < GH and self.editable(nx, ny):
                            x0, y0 = (x * 16 + (16 if dx == 1 else 0)) * z, (y * 16 + (16 if dy == 1 else 0)) * z
                            if dx: d.line([(x0, y * 16 * z), (x0, (y + 1) * 16 * z)], fill=(255, 40, 40, 255), width=2)
                            else: d.line([(x * 16 * z, y0), ((x + 1) * 16 * z, y0)], fill=(255, 40, 40, 255), width=2)
        self.tkimg = ImageTk.PhotoImage(im)
        self.canvas.itemconfigure(self.canvas_img, image=self.tkimg)
        self.canvas.configure(scrollregion=(0, 0, GW * 16 * z, GH * 16 * z))

    def refresh_cells(self, cells):
        for x, y in cells: self.compose_cell(x, y)
        self.refresh_view()

    # -- the palette
    def draw_palette(self):
        cols = 8; z = 2
        im = Image.new('RGBA', (cols * 16 * z + cols + 1, 32 * (16 * z + 1) + 1), (40, 40, 48, 255))
        for b in range(256):
            bi = self.bimg(self.layer, b)
            if self.layer == 0:                               # (BG1: on a check so the see-through shows)
                bg = Image.new('RGBA', (16, 16), (70, 70, 80, 255)); bg.alpha_composite(bi); bi = bg
            im.paste(bi.resize((16 * z, 16 * z), Image.NEAREST), (1 + (b % cols) * (16 * z + 1), 1 + (b // cols) * (16 * z + 1)))
        d = ImageDraw.Draw(im)
        sx, sy = 1 + (self.sel % cols) * (16 * z + 1), 1 + (self.sel // cols) * (16 * z + 1)
        d.rectangle([sx - 1, sy - 1, sx + 16 * z, sy + 16 * z], outline=(255, 220, 0, 255), width=2)
        self.pal_img = ImageTk.PhotoImage(im)
        self.palette.itemconfigure(self.pal_item, image=self.pal_img)
        self.palette.configure(scrollregion=(0, 0, im.width, im.height))
        self.sel_preview()

    def sel_preview(self):
        im = Image.new('RGBA', (16, 16), (70, 70, 80, 255)); im.alpha_composite(self.bimg(self.layer, self.sel))
        self.sel_img = ImageTk.PhotoImage(im.resize((64, 64), Image.NEAREST))
        self.sel_label.configure(image=self.sel_img, text='  block %d (%s)' % (self.sel, 'BG1' if self.layer == 0 else 'BG2'))

    def pick_palette(self, e):
        z = 2; cols = 8
        x, y = self.palette.canvasx(e.x), self.palette.canvasy(e.y)
        b = int((y - 1) // (16 * z + 1)) * cols + int((x - 1) // (16 * z + 1))
        if 0 <= b < 256 and 0 <= (x - 1) // (16 * z + 1) < cols:
            self.sel = b; self.draw_palette()

    # -- painting
    def cell_of(self, e):
        z = self.zoom
        x, y = int(self.canvas.canvasx(e.x) // (16 * z)), int(self.canvas.canvasy(e.y) // (16 * z))
        return (x, y) if 0 <= x < GW and 0 <= y < GH else None

    def set_cell(self, x, y, blk, change):
        """blk None = back to the world's own (1000 A.D.'s) block."""
        if not self.editable(x, y): return False
        k = (self.wid, self.layer, x, y)
        old = self.edits.get(k)
        new = None if blk is None or blk == self.ext[self.layer][y * GW + x] else blk
        if old == new: return False
        change.append((k, old, new))
        if new is None: self.edits.pop(k, None)
        else: self.edits[k] = new
        return True

    def commit(self, change):
        if change:
            self.undo.append(change); self.redo.clear(); self.dirty = True; self.status()

    def on_down(self, e):
        c = self.cell_of(e)
        if not c: return
        if self.tool == 'rect':
            self.rect0 = c; return
        if self.tool == 'fill':
            self.flood(c); return
        self.stroke = []
        self.paint_at(c)

    def paint_at(self, c):
        blk = None if self.tool == 'erase' else self.sel
        if self.set_cell(c[0], c[1], blk, self.stroke): self.refresh_cells([c])

    def on_drag(self, e):
        c = self.cell_of(e)
        if not c: return
        if self.tool in ('pencil', 'erase') and self.stroke is not None:
            self.paint_at(c)
        elif self.tool == 'rect' and self.rect0:
            self.canvas.delete('rect'); z = self.zoom
            (x0, y0), (x1, y1) = self.rect0, c
            self.canvas.create_rectangle(min(x0, x1) * 16 * z, min(y0, y1) * 16 * z, (max(x0, x1) + 1) * 16 * z,
                                         (max(y0, y1) + 1) * 16 * z, outline='yellow', width=2, tags='rect')

    def on_up(self, e):
        if self.tool == 'rect' and self.rect0:
            c = self.cell_of(e) or self.rect0
            (x0, y0), (x1, y1) = self.rect0, c
            change, cells = [], []
            for y in range(min(y0, y1), max(y0, y1) + 1):
                for x in range(min(x0, x1), max(x0, x1) + 1):
                    if self.set_cell(x, y, self.sel, change): cells.append((x, y))
            self.canvas.delete('rect'); self.rect0 = None
            self.commit(change); self.refresh_cells(cells)
        elif self.stroke is not None:
            self.commit(self.stroke); self.stroke = None

    def flood(self, c):
        target = self.block_at(self.layer, *c)
        if target == self.sel or not self.editable(*c): return
        change, cells, todo, seen = [], [], [c], {c}
        while todo:
            x, y = todo.pop()
            if not self.editable(x, y) or self.block_at(self.layer, x, y) != target: continue
            if self.set_cell(x, y, self.sel, change): cells.append((x, y))
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if 0 <= nx < GW and 0 <= ny < GH and (nx, ny) not in seen:
                    seen.add((nx, ny)); todo.append((nx, ny))
        self.commit(change); self.refresh_cells(cells)

    def pick(self, e):
        c = self.cell_of(e)
        if c:
            self.sel = self.block_at(self.layer, *c); self.draw_palette()

    def do_undo(self, redo=False):
        src, dst = (self.redo, self.undo) if redo else (self.undo, self.redo)
        if not src: return
        change = src.pop(); cells = []
        for k, old, new in (change if redo else reversed(change)):
            v = new if redo else old
            if v is None: self.edits.pop(k, None)
            else: self.edits[k] = v
            cells.append((k[2], k[3]))
        dst.append(change); self.dirty = True; self.refresh_cells(cells); self.status()

    def save(self):
        write_edits(self.edits_path, self.edits); self.dirty = False; self.status()

    # -- the window
    def build_ui(self):
        r = self.root
        r.title('Belthasar map editor')
        bar = tk.Frame(r); bar.pack(side='top', fill='x')
        tk.Label(bar, text='World').pack(side='left')
        self.world_var = tk.IntVar(value=7)
        tk.OptionMenu(bar, self.world_var, *range(8), command=lambda v: self.load_world(int(v))).pack(side='left')
        self.tool_var = tk.StringVar(value='pencil')
        for t, label in (('pencil', 'Pencil (P)'), ('rect', 'Rectangle (R)'), ('fill', 'Fill (F)'), ('erase', 'Erase (E)')):
            tk.Radiobutton(bar, text=label, value=t, variable=self.tool_var, indicatoron=0, padx=6,
                           command=lambda: setattr(self, 'tool', self.tool_var.get())).pack(side='left')
        self.layer_var = tk.IntVar(value=1)
        for v, label in ((1, 'BG2 ground (2)'), (0, 'BG1 overlay (1)')):
            tk.Radiobutton(bar, text=label, value=v, variable=self.layer_var, indicatoron=0, padx=6,
                           command=self.set_layer).pack(side='left', padx=(8 if v == 1 else 0, 0))
        tk.Button(bar, text='Save (Ctrl+S)', command=self.save).pack(side='right')
        for label, attr in (('Grid (G)', 'grid_on'), ('Border (B)', 'border_on'), ('Painted (H)', 'hl_on')):
            tk.Button(bar, text=label, command=lambda a=attr: self.toggle(a)).pack(side='right')
        tk.Button(bar, text='Zoom -', command=lambda: self.set_zoom(-1)).pack(side='right')
        tk.Button(bar, text='Zoom +', command=lambda: self.set_zoom(1)).pack(side='right')

        body = tk.Frame(r); body.pack(side='top', fill='both', expand=True)
        side = tk.Frame(body); side.pack(side='right', fill='y')
        self.sel_label = tk.Label(side, compound='left', anchor='w'); self.sel_label.pack(side='top', fill='x')
        pv = tk.Scrollbar(side, orient='vertical'); pv.pack(side='right', fill='y')
        self.palette = tk.Canvas(side, width=8 * 33 + 2, bg='#282830', yscrollcommand=pv.set, highlightthickness=0)
        self.palette.pack(side='left', fill='y'); pv.configure(command=self.palette.yview)
        self.pal_item = self.palette.create_image(0, 0, anchor='nw')
        self.palette.bind('<Button-1>', self.pick_palette)
        self.palette.bind('<MouseWheel>', lambda e: self.palette.yview_scroll(-e.delta // 120, 'units'))

        hs = tk.Scrollbar(body, orient='horizontal'); hs.pack(side='bottom', fill='x')
        vs = tk.Scrollbar(body, orient='vertical'); vs.pack(side='right', fill='y')
        self.canvas = tk.Canvas(body, bg='black', width=1100, height=720, xscrollcommand=hs.set, yscrollcommand=vs.set,
                                highlightthickness=0)
        self.canvas.pack(side='left', fill='both', expand=True)
        hs.configure(command=self.canvas.xview); vs.configure(command=self.canvas.yview)
        self.canvas_img = self.canvas.create_image(0, 0, anchor='nw')
        self.canvas.bind('<ButtonPress-1>', self.on_down)
        self.canvas.bind('<B1-Motion>', self.on_drag)
        self.canvas.bind('<ButtonRelease-1>', self.on_up)
        self.canvas.bind('<Button-3>', self.pick)
        self.canvas.bind('<ButtonPress-2>', lambda e: self.canvas.scan_mark(e.x, e.y))
        self.canvas.bind('<B2-Motion>', lambda e: self.canvas.scan_dragto(e.x, e.y, gain=1))
        self.canvas.bind('<MouseWheel>', self.wheel)
        self.canvas.bind('<Motion>', self.hover)

        self.stat = tk.Label(r, anchor='w'); self.stat.pack(side='bottom', fill='x')
        keys = {'p': 'pencil', 'r': 'rect', 'f': 'fill', 'e': 'erase'}
        for k, t in keys.items(): r.bind(k, lambda e, t=t: self.set_tool(t))
        r.bind('1', lambda e: self.set_layer(0)); r.bind('2', lambda e: self.set_layer(1))
        r.bind('g', lambda e: self.toggle('grid_on')); r.bind('b', lambda e: self.toggle('border_on'))
        r.bind('h', lambda e: self.toggle('hl_on')); r.bind('v', lambda e: self.toggle_other())
        r.bind('<Control-z>', lambda e: self.do_undo()); r.bind('<Control-y>', lambda e: self.do_undo(redo=True))
        r.bind('<Control-s>', lambda e: self.save())
        r.protocol('WM_DELETE_WINDOW', self.quit)

    def set_tool(self, t): self.tool = t; self.tool_var.set(t)

    def set_layer(self, v=None):
        if v is not None: self.layer_var.set(v)
        self.layer = self.layer_var.get(); self.draw_palette(); self.status()

    def toggle(self, attr):
        setattr(self, attr, not getattr(self, attr))
        if attr == 'hl_on': self.compose_all()
        else: self.refresh_view()

    def toggle_other(self):
        o = 1 - self.layer; self.show[o] = not self.show[o]; self.compose_all()

    def set_zoom(self, d):
        i = self.ZOOMS.index(self.zoom) + d
        if 0 <= i < len(self.ZOOMS): self.zoom = self.ZOOMS[i]; self.refresh_view()

    def wheel(self, e):
        if e.state & 0x4: self.set_zoom(1 if e.delta > 0 else -1)
        elif e.state & 0x1: self.canvas.xview_scroll(-e.delta // 120 * 3, 'units')
        else: self.canvas.yview_scroll(-e.delta // 120 * 3, 'units')

    def hover(self, e):
        c = self.cell_of(e)
        if c:
            self.status('cell %d,%d  BG2 %d  BG1 %d  %s' % (c[0], c[1], self.block_at(1, *c), self.block_at(0, *c),
                        'editable' if self.editable(*c) else 'the ROM\'s patch (locked)'))

    def status(self, extra=''):
        n = sum(1 for k in self.edits if k[0] == self.wid)
        self.stat.configure(text='world %d  %s  layer %s  %d painted cell(s)%s  -> %s   %s' % (
            self.wid, self.tool, 'BG1' if self.layer == 0 else 'BG2', n, ' (unsaved)' if self.dirty else '',
            self.edits_path, extra))

    def quit(self):
        if self.dirty and messagebox.askyesno('map editor', 'Save the painted cells before closing?'): self.save()
        self.root.destroy()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--assets', default=os.path.join(ROOT, 'ct_assets.dat'))
    ap.add_argument('--edits', default=os.environ.get('CT_MAPEDITS') or os.path.join(ROOT, 'ct_mapedits.dat'))
    ap.add_argument('--world', type=int, default=7)
    a = ap.parse_args()
    if not os.path.exists(a.assets):
        sys.exit('map-editor: no %s (the setup makes it from your ROM)' % a.assets)
    root = tk.Tk()
    Editor(root, read_assets(a.assets), a.edits, a.world)
    root.mainloop()


if __name__ == '__main__':
    main()
