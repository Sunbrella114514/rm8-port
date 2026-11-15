"""Export a RichMan8 map (.map + .ltm) into a render-ready scene for port/viewer (rm8_scene).
Everything marked [V] follows verified game code (see docs/map_format.md and port/README.md);
[A] marks an approximation that still needs reverse engineering.

usage: python -I export_scene.py <game_dir> <map_id e.g. 01> <out_dir>
out_dir/scene.txt + ground_LL.png (one per terrain quadtree leaf) + overview.png (top-down check image)
"""
import struct, sys, os, math, re
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rm8pak
import rm8_edition

def u32(d, o): return struct.unpack_from('<I', d, o)[0]

def load_map(path):
    d = open(path, 'rb').read()
    assert d[:7] == b'rch8map'
    W, H, nart = struct.unpack_from('<III', d, 0x0C)
    amb = struct.unpack_from('<4f', d, 0x18); dirc = struct.unpack_from('<4f', d, 0x28)
    pitch, yaw = struct.unpack_from('<2f', d, 0x38); wmode = struct.unpack_from('<i', d, 0x44)[0]
    o = 0x48; nv = (W + 1) * (H + 1)
    vt = np.frombuffer(d, dtype='<u4', count=nv * 5, offset=o).reshape(nv, 5); o += nv * 20
    heights = vt[:, 1].copy().view('<f4').reshape(H + 1, W + 1)          # [V] +0x04 height
    vcolor = vt[:, 4].reshape(H + 1, W + 1)                             # [V] +0x10 0xRRGGBB
    tl = np.frombuffer(d, dtype='<u4', count=W * H * 7, offset=o).reshape(H, W, 7).copy(); o += W * H * 28
    arts = []
    for i in range(nart):
        f = struct.unpack_from('<19I', d, o); g = struct.unpack_from('<19f', d, o); own = struct.unpack_from('<i', d, o + 0x44)[0]; o += 0x4C
        arts.append(dict(art=f[0], type=f[2], pos=(g[3], g[4] + g[10], g[5]), scale=(g[6], g[7], g[8]), roty=g[9], owner=own))	# owner: record [17] (+0x44), the role a story-only article belongs to (art 0x45b: James's chain-store sign, Game_ActivateCharacterBuilding @0x4a37d0), -1 = none
    water = None
    if wmode == 2:
        wt = struct.unpack_from('<i', d, o)[0]; wy = struct.unpack_from('<f', d, o + 4)[0]
        wc = struct.unpack_from('<4f', d, o + 8)
        # [V] CWater_LoadState @0x477230: +0x18 underwater light scale, +0x1C bump matrix value (stage 0 BUMPENVMAT00/11),
        # +0x20 flow direction, +0x2C flow speed (CWater_UpdateAnimation @0x4779a0)
        lscale, bump = struct.unpack_from('<2f', d, o + 0x18); fdir = struct.unpack_from('<3f', d, o + 0x20); fspeed = struct.unpack_from('<f', d, o + 0x2C)[0]
        water = dict(tex=wt, y=wy, color=wc, bump=bump, dir=fdir, speed=fspeed, lscale=lscale); o += 52
    elif wmode != 0: o += 4
    assert o == len(d), 'map not fully consumed'
    return dict(W=W, H=H, amb=amb, dir=dirc, pitch=pitch, yaw=yaw, heights=heights, vcolor=vcolor,
                levels=vt[:, 3].reshape(H + 1, W + 1),                    # [V] +0x0C height level (100 = 0)
                vtex=vt[:, 0].reshape(H + 1, W + 1),                      # [V] +0x00 vertex ground word: low byte texture, bits 8-15 variant
                road=tl[:, :, 0], event=tl[:, :, 1], plot=tl[:, :, 2], anchor=tl[:, :, 3], street=tl[:, :, 4],
                land=tl[:, :, 5] >> 16, landlo=tl[:, :, 5] & 0xFFFF, arts=arts, water=water)

def load_ltm_A(path):
    """[V] segment A: nA x {id, w, h, w*h lum, 16 f32 matrix}"""
    d = open(path, 'rb').read(); n = struct.unpack_from('<i', d, 0)[0]; o = 4; out = {}
    for _ in range(n):
        i, w, h = struct.unpack_from('<iii', d, o); o += 12
        px = np.frombuffer(d, dtype=np.uint8, count=w * h, offset=o).reshape(h, w); o += w * h
        m = struct.unpack_from('<16f', d, o); o += 64
        out[i] = (px, m)
    return out

def load_ltm_B(path):
    """[V] segment B (after segment A): nB x {article index, w, h, w*h lum, 16 f32 matrix}; CArticleMgr__vf14 @0x43bdf0
    stores the matrix at article+0x80, Render_CustomShader @0x43a830 applies it as a MODULATE2X stage."""
    d = open(path, 'rb').read(); n = struct.unpack_from('<i', d, 0)[0]; o = 4
    for _ in range(n):
        i, w, h = struct.unpack_from('<iii', d, o); o += 12 + w * h + 64
    out = {}
    if o >= len(d): return out
    n = struct.unpack_from('<i', d, o)[0]; o += 4
    for _ in range(n):
        i, w, h = struct.unpack_from('<iii', d, o); o += 12
        px = np.frombuffer(d, dtype=np.uint8, count=w * h, offset=o).reshape(h, w); o += w * h
        m = struct.unpack_from('<16f', d, o); o += 64
        out[i] = (px, m)
    assert o == len(d), 'ltm not fully consumed'
    return out

def quad_leaves(W, H):
    """[V] QuadTree_Build @0x42dfe0: preorder ids from 1, leaf when box diagonal <= 800,
    children built from (p0, p1) as (c..p1), (p0..c), (p0.x,c.z .. c.x,p1.z), (c.x,p0.z .. p1.x,c.z).
    [V] Map_InitGrid @0x420cb0 passes the root box REVERSED: p0 = (+W*25, 100, +H*25), p1 = (-W*25, -100, -H*25),
    so child 0 is the (x-, z-) quadrant. Returned boxes are normalised to (min, max)."""
    leaves = {}; cnt = [1]
    def build(p0, p1):
        i = cnt[0]; cnt[0] += 1
        if math.dist(p0, p1) <= 800:
            leaves[i] = (tuple(map(min, p0, p1)), tuple(map(max, p0, p1))); return
        cx = (p0[0] + p1[0]) / 2; cz = (p0[2] + p1[2]) / 2
        for a, b in (((cx, p0[1], cz), p1), (p0, (cx, p1[1], cz)), ((p0[0], p0[1], cz), (cx, p1[1], p1[2])), ((cx, p0[1], p0[2]), (p1[0], p1[1], cz))):
            build(a, b)
    build((W * 25.0, 100.0, H * 25.0), (-W * 25.0, -100.0, -H * 25.0))
    return leaves

def read_cfg(game, name):
    d = open(os.path.join(game, 'Config', name), 'rb').read()
    if name.endswith('.bin') and name != 'festival.bin':
        d = rm8_edition.decrypt(game, d)	# key of the install's edition (old Big5 install: b'tiep8jdg', Steam: b'seowk48h')
    secs = []
    for m in re.finditer(r'\[(.*?)\]\r?\n(.*?)(?=\r?\n\[|\Z)', rm8_edition.decode(game, d), re.S):	# cp950 (old install) / gbk (Steam)
        secs.append((m.group(1), dict(l.split('=', 1) for l in m.group(2).splitlines() if '=' in l)))
    return secs

def art_table(game):
    """[V] Config/newOut/role.bin record body: +0x00 art id, +0x234 dff path (clump\\article\\), +0x334 texture dir (texture\\article\\)
    second sub-list (0x128 bytes each in version 0x65): +0x00 anim index, +0x08 .anm path (clump\\article\\).
    [A] index 0 is the idle loop played on the map (Act02/Act03 are event actions)."""
    d = open(os.path.join(game, 'Config', 'newOut', 'role.bin'), 'rb').read()
    ver, n = struct.unpack_from('<II', d, 0); o = 8; t = {}
    sizes = (0xC, 0x128 if ver == 0x65 else 0x124, 0x224, 0x1C, 0x18)
    for _ in range(n):
        body = d[o:o + 0x650]; o += 0x650
        anims = {}
        for k, sz in enumerate(sizes):
            c = u32(d, o)
            if k == 1:
                for i in range(c):
                    rec = d[o + 4 + i * sz:o + 4 + (i + 1) * sz]
                    anims[u32(rec, 0)] = rec[8:].split(b'\0')[0].decode('latin1')
            o += 4 + c * sz
        aid = u32(body, 0)
        dff = body[0x234:0x334].split(b'\0')[0].decode('latin1'); tex = body[0x334:0x434].split(b'\0')[0].decode('latin1')
        anim = ('article\\' + anims[0]) if anims.get(0) else '-'
        # [V] INI loader (role.bin source text): +0x24 HaveUVAnim, +0x534 UVAnimPath (unused bytes are 0xCD fill)
        uva = ('article\\' + body[0x534:0x634].split(b'\0')[0].decode('latin1')) if u32(body, 0x24) == 1 else '-'
        t[aid] = ('article\\' + dff, 'article\\' + tex, anim, uva, {k: 'article\\' + v for k, v in anims.items()}) if dff else None   # None: record exists but has no model (invisible marker)
    assert o == len(d)
    return t

def art_sound_events(game):
    """[V] role.bin sub-list 4 (0x18-byte records {art id, action, 0, sound id, flag, time(float)}, 32 in the whole file):
    Article_Update @0x448540 hands {article, rec+8, rec+0xC, rec+0x10, time} to the article's callback (+0x2C8 = Vehicle_UpdatePos
    @0x43A280, installed by CArticleMgr_CreateArticleFromRecord @0x43CB90) when the animation passes `time`; the callback plays
    sample rec+0xC (Sound/snd0000.dat id 6401..6430). Returns {art id: [(action, sound id, flag, time)]}."""
    d = open(os.path.join(game, 'Config', 'newOut', 'role.bin'), 'rb').read()
    ver, n = struct.unpack_from('<II', d, 0); o = 8
    sizes = (0xC, 0x128 if ver == 0x65 else 0x124, 0x224, 0x1C, 0x18)
    ev = {}
    for _ in range(n):
        body = d[o:o + 0x650]; o += 0x650
        aid = u32(body, 0)
        for k, sz in enumerate(sizes):
            c = u32(d, o)
            if k == 4:
                for i in range(c):
                    a_, act, z, snd, flag, tm = struct.unpack_from('<iiiiif', d, o + 4 + i * sz)
                    ev.setdefault(aid, []).append((act, snd, flag, tm))
            o += 4 + c * sz
    return ev

def art_action_events(game):
    """[V] role.bin sub-lists 3 and 4 (index 2 / 3 of the file order): per-action timed events that Article_Update @0x448540 runs while the action plays.
    Index 2 (0x224-byte records {art, action, time(float), +0xC material name[0x14], +0x20 texture file[0x100], +0x120 texture dir[0x100]}):
    at `time` UI_LoadMaterial @0x44BDC0 loads <texture dir>\\<file> and sets it on the material of that name (the event icons switch to the
    glowing eve_xxx_d.tga when pressed and back to eve_xxx.tga when they pop up). Index 3 (0x1C-byte records {action, effect id, 0, time(float),
    type (1000 = once), hook, ...}): at `time` the article spawns particle effect <id> (PrtEffTable) on its [@hook] frame (the event icons: effect 14 at
    the end of action 0 = stopping on the icon, @0x4EE3C0). Returns ({art: [(action, time, material, dir + file)]}, {art: [(action, time, effect, hook, type)]})."""
    d = open(os.path.join(game, 'Config', 'newOut', 'role.bin'), 'rb').read()
    ver, n = struct.unpack_from('<II', d, 0); o = 8
    sizes = (0xC, 0x128 if ver == 0x65 else 0x124, 0x224, 0x1C, 0x18)
    tex, fx = {}, {}
    def s(rec, off, ln): return rec[off:off + ln].split(b'\0')[0].decode('latin1')
    for _ in range(n):
        body = d[o:o + 0x650]; o += 0x650
        aid = u32(body, 0)
        for k, sz in enumerate(sizes):
            c = u32(d, o)
            for i in range(c):
                rec = d[o + 4 + i * sz:o + 4 + (i + 1) * sz]
                if k == 2:
                    tex.setdefault(aid, []).append((u32(rec, 4), struct.unpack_from('<f', rec, 8)[0], s(rec, 0xC, 0x14), 'article\\' + s(rec, 0x120, 0x100) + s(rec, 0x20, 0x100)))
                elif k == 3:
                    fx.setdefault(aid, []).append((u32(rec, 0), struct.unpack_from('<f', rec, 12)[0], u32(rec, 4), u32(rec, 20), u32(rec, 16)))
            o += 4 + c * sz
    return tex, fx

def prt_table(game):
    """[V] docs/particles.md §4: role.bin body +0x10 == 1 marks a particle art whose PrtEffTable id is at body +0x64C;
    sub-list 1 (12-byte entries {hook, child art, -1}) attaches child arts to the parent's frame whose user data is
    "[@hook]...". Returns ({art: effect id}, {parent art: [(hook, effect id)]})."""
    d = open(os.path.join(game, 'Config', 'newOut', 'role.bin'), 'rb').read()
    ver, n = struct.unpack_from('<II', d, 0); o = 8
    sizes = (0xC, 0x128 if ver == 0x65 else 0x124, 0x224, 0x1C, 0x18)
    eff = {}; subs1 = {}
    for _ in range(n):
        body = d[o:o + 0x650]; o += 0x650
        aid = u32(body, 0)
        for k, sz in enumerate(sizes):
            c = u32(d, o)
            if k == 0 and c:
                subs1[aid] = [struct.unpack_from('<iii', d, o + 4 + i * sz) for i in range(c)]
            o += 4 + c * sz
        if u32(body, 0x10) == 1: eff[aid] = u32(body, 0x64C)
    assert o == len(d)
    hooks = {p: [(h, eff[c]) for h, c, _ in L if c in eff] for p, L in subs1.items()}
    return eff, {p: v for p, v in hooks.items() if v}

# [V] Route_FindTexCoord @0x436650 table at 0x604338 (19 rows x 4 rotations) and corner table in Route_RenderSegment @0x436810
ROUTE_TABLE = [[0x10c,0x103,0x100,0x100],[0x104,0x101,0x108,0x102],[0x106,0x105,0x109,0x10a],[0x10e,0x107,0x10d,0x10b],
 [0x10f]*4,[0x190,0x160,0x100,0x100],[0x110,0x120,0x180,0x140],[0x112,0x124,0x181,0x148],[0x122,0x184,0x141,0x118],
 [0x132,0x1a4,0x1c1,0x158],[0x1f0]*4,[0x1b0,0x1e0,0x1d0,0x170],[0x130,0x1a0,0x1c0,0x150],[0x185,0x149,0x11a,0x126],
 [0x60,0x90,0x90,0x90],[0x20,0x80,0x40,0x10],[0x30,0xa0,0xc0,0x50],[0x70,0xb0,0xe0,0xd0],[0xf0]*4]
CORNER_ROT = [0,2,3,1, 1,0,2,3, 2,3,1,0, 3,1,0,2]
def route_uv(code):
    for c, row in enumerate(ROUTE_TABLE):
        if code in row:
            r = row.index(code)
            cell = c if c < 1 else (c - 1 if c < 6 else (c - 2 if c < 8 else c - 3))
            col, rowi = cell % 4, cell // 4
            u0, u1 = (col * 64 + 0.5) / 256, ((col + 1) * 64 - 0.5) / 256
            if c == 8: u0, u1 = u1, u0
            v0, v1 = (rowi * 64 + 0.5) / 256, ((rowi + 1) * 64 - 0.5) / 256
            uvp = [u0, v0, u1, v1]
            return [(uvp[(CORNER_ROT[i * 4 + r] % 2) * 2], uvp[(CORNER_ROT[i * 4 + r] // 2) * 2 + 1]) for i in range(4)]
    return None

def route_code(road, x, y):
    """[V] tile route code (caller at 0x423f?? -> Route_RenderSegment); neighbours order: up, down, left, right, UL, DR, UR, DL.
    returns (code, road_type) or None. Only interior tiles (all 8 neighbours)."""
    H, W = road.shape
    if not (0 < x < W - 1 and 0 < y < H - 1): return None
    nb = [(x, y - 1), (x, y + 1), (x - 1, y), (x + 1, y), (x - 1, y - 1), (x + 1, y + 1), (x + 1, y - 1), (x - 1, y + 1)]
    isr = [int(road[b][a]) for a, b in nb]
    DIAG_BIT = {4: 4, 5: 7, 6: 5, 7: 6}
    t = int(road[y][x])
    if t:
        code = 0x100 | sum(1 << i for i in range(4) if isr[i])
        for c in range(4, 8):
            share = [b for b in range(4) if (nb[b][0] == nb[c][0] or nb[b][1] == nb[c][1]) and isr[b]]
            if not share and isr[c]: code |= 1 << DIAG_BIT[c]
        return code, t
    code = 0; typ = 0
    for c in range(4, 8):
        if isr[c] == 0:
            share = [b for b in range(4) if (nb[b][0] == nb[c][0] or nb[b][1] == nb[c][1]) and isr[b]]
            for b in share: typ = isr[b]
            if len(share) > 1: code |= 1 << DIAG_BIT[c]
    return (code, typ) if code and 0 < typ < 100 else None

def main():
    game, mid, out = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(out, exist_ok=True)
    m = load_map(os.path.join(game, 'Map', mid + '.map'))
    W, H = m['W'], m['H']; hgt = m['heights']
    lm = load_ltm_A(os.path.join(game, 'Map', mid + '.ltm'))
    leaves = quad_leaves(W, H)
    assert set(leaves) == set(lm), 'lightmap ids != quadtree leaves'
    texpack = rm8pak.read_index(os.path.join(game, 'Texture', 'tex0000.dat'))
    tf, tents, _ = texpack; tindex = {e['name'].lower(): e for e in tents}
    def tex(name):
        e = tindex[name.lower()]; tf.seek(e['off'])
        import io; return np.asarray(Image.open(io.BytesIO(tf.read(e['size']))).convert('RGBA'))
    # ---- ground (flat tiles) -- [V] Map_RebuildTileGroundMesh @0x422990 / Material_InitRender @0x433440:
    # each tile corner vertex has a ground word: low byte = landtex index (0-based, file order), bits 8-15 = variant.
    # Corner masks 1<<(3-i) (corners (x,y),(x+1,y),(x,y+1),(x+1,y+1)) are merged per texture into a list sorted by
    # texture index (PairList4_InsertMergeSorted @0x427580); each entry draws the 64x64 cell [mask] of that land atlas
    # (rect table DAT_00604138: cells 0..15 left half, 16..31 right half) with alpha, in list order. When all four
    # corners share one texture the cell is 15 + variant of corner 0 (full-tile variants).
    landcfg = read_cfg(game, 'landtex.bin')                       # [V] list index = section order
    landfiles = ['land\\%s.tga' % s['filename'] for _, s in landcfg]
    vtex = m['vtex']
    tiles = []
    for y in range(H):
        for x in range(W):
            ent = []
            for i, (cx, cy) in enumerate([(x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)]):
                t = int(vtex[cy][cx]) & 0xFF; bit = 1 << (3 - i)
                for e in ent:
                    if e[0] == t: e[1] |= bit; break
                else: ent.append([t, bit])
            ent.sort(key=lambda e: e[0])
            if len(ent) == 1: ent[0][1] = ((int(vtex[y][x]) >> 8) & 0xFF) + 15
            tiles.append(ent)
    # ---- cliffs: [V] Map_RebuildTileCliff @0x423580 -> Cliff_RenderFrame @0x434f70 (see scripts/cliffs.py).
    # A tile whose cliff code has a template gets the template mesh instead of the flat ground mesh.
    import plots, cliffs
    cgrid = plots.Grid(W, H, [int(v) for v in m['road'].flatten()], m['levels'], hgt, [False] * (W * H))
    ctpl = cliffs.Cliffs(game)
    def leaf_of(x, y):
        cx, cz = x * 50 - W * 25 + 25, y * 50 - H * 25 + 25
        for lid, (mn, mx) in leaves.items():
            if mn[0] <= cx < mx[0] and mn[2] <= cz < mx[2]: return lid
    cliff_lines = []; ncliff = 0; nnotpl = 0
    for y in range(H):
        for x in range(W):
            i = y * W + x
            code = cgrid.cliff_code(i)
            if code & 0xFFFF == 0: continue
            alt = int(m['landlo'][y][x]) == 1
            tpl = ctpl.find(code & 0xFFFF, alt)
            if tpl is None: nnotpl += 1; continue      # no template: the game keeps the flat ground mesh
            texi = 2 if alt else int(m['land'][y][x])
            cy = [hgt[b][a] for a, b in [(x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)]]
            V, T = ctpl.mesh(tpl, x * 50 - W * 25 + 25, y * 50 - H * 25 + 25, ((code >> 16) - 100) * 12.5, cy)
            cliff_lines.append('cliff %d cliff\\cliff%d.tga %d %d %s %s' % (leaf_of(x, y), texi, len(V), len(T),
                               ' '.join('%g %g %g %g %g' % v for v in V), ' '.join('%d %d %d' % t for t in T)))
            tiles[i] = []; ncliff += 1
    print('cliffs: %d tiles with templates, %d cliff codes without template (flat mesh kept)' % (ncliff, nnotpl))
    used = sorted({t for ent in tiles for t, _ in ent})
    lines = ['landtex %d %s' % (t, landfiles[t]) for t in used if t < len(landfiles)]
    for i, ent in enumerate(tiles):
        lines.append('tile %d %d %s' % (i % W, i // W, ' '.join('%d %d' % (t, c) for t, c in ent)))
    lines += cliff_lines
    # overview check image (16 px per tile), same composition rule
    cache = {}
    def cell(t, c):
        if (t, c) not in cache:
            img = tex(landfiles[t]).astype(np.float32)
            x0 = (c % 4) * 64 + (256 if c >= 16 else 0); y0 = ((c % 16) // 4) * 64
            cache[(t, c)] = img[y0:y0 + 64, x0:x0 + 64]
        return cache[(t, c)]
    ov = np.zeros((H * 16, W * 16, 3), np.uint8)
    for i, ent in enumerate(tiles):
        acc = np.zeros((64, 64, 3), np.float32)
        for t, c in ent:
            if t >= len(landfiles): continue
            cl = cell(t, c); a = cl[..., 3:4] / 255.0
            acc = acc * (1 - a) + cl[..., :3] * a
        ov[(i // W) * 16:(i // W + 1) * 16, (i % W) * 16:(i % W + 1) * 16] = np.asarray(Image.fromarray(acc.astype(np.uint8)).resize((16, 16)))
    # ---- leaf lightmaps: [V] .ltm segment A, u,v = p * M (RwV3dTransformPoint); applied MODULATE2X (Render_DrawObjectWithLight @0x40ee40)
    for lid, (mn, mx) in sorted(leaves.items()):
        px, mt = lm[lid]
        fn = 'lm_%02d.png' % lid
        Image.fromarray(px).save(os.path.join(out, fn))
        lines.append('leaf %d %g %g %g %g %s %s' % (lid, mn[0], mn[2], mx[0], mx[2], fn, ' '.join('%r' % v for v in mt)))

    out_lines = ['map %d %d' % (W, H),
                 'ambient %g %g %g %g' % tuple(m['amb']),
                 'directional %g %g %g %g %g %g' % (tuple(m['dir']) + (m['pitch'], m['yaw']))]
    out_lines += ['vrow %d %s' % (y, ' '.join('%g' % v for v in hgt[y])) for y in range(H + 1)]
    out_lines += ['vcrow %d %s' % (y, ' '.join('%x' % int(v) for v in m['vcolor'][y])) for y in range(H + 1)]   # [V] vertex colour 0xRRGGBB (terrain diffuse)
    out_lines += lines
    # ---- roads
    nroad = 0; routes = {int(s['id']): s['filename'] for _, s in read_cfg(game, 'route.bin')}
    roadmesh = [False] * (W * H)     # [V] tile +0x28: road tiles and fill tiles get a route mesh (Map_RebuildTileRoute)
    for y in range(H):
        for x in range(W):
            rc = route_code(m['road'], x, y)
            if not rc: continue
            if 0 < rc[1] < 100: roadmesh[y * W + x] = True
            uv = route_uv(rc[0])
            if not uv: continue
            corners = [(x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)]       # [V] corner order (x,y)(x+1,y)(x,y+1)(x+1,y+1)
            pts = ['%g %g %g %g %g' % (cx * 50 - W * 25, hgt[cy][cx] + 0.5, cy * 50 - H * 25, uv[i][0], uv[i][1]) for i, (cx, cy) in enumerate(corners)]
            out_lines.append('road route\\%s.tga %s' % (routes[rc[1]], ' '.join(pts))); nroad += 1
    at = art_table(game)
    # ---- plots (terra): pads, event icons, the buildings a new game starts with
    import plots, random
    grid = plots.Grid(W, H, [int(v) for v in m['road'].flatten()], m['levels'], hgt, roadmesh)
    words = [(int(m['plot'].flat[i]), int(m['anchor'].flat[i])) for i in range(W * H)]
    placed = grid.place_all(words)
    # [V] Land_CreateObject_f1320 / Unk_InitSprite_f00c0: every tile with a road word gets a road node (StPoint) at
    # (tile centre x, mean of the 4 corner heights, tile centre z); characters walk between 4-adjacent nodes.
    # The extra field is the tile event (7 prison, 8 hospital, ...).
    # [V] docs/rules_land.md 1.2/1.7: plots of terra type 1 (residential) and 2 (commercial) become Terra objects;
    # a road node links to the plot recorded on its tile (+0x40, plots.Grid.roadPlot). Street = street.bin ID of the
    # anchor tile (commercial plots: 0). Line: terra <idx> <type> <street id> <x y z> <yaw> <start level>
    terra_of = {}
    terras = []
    for p in placed:
        r = p['result']
        if r and r['type'] in (1, 2):
            terra_of[r['handle']] = len(terras); terras.append(r)
    # [V] BuildObj_CreateEvent @0x4f19e0: a tile without an event id whose plot word is a special building gets the event of that building:
    # 100 power company -> 0xF, 101 ticket center -> 0x10, 102 magic house -> 0xD, 103 court -> 0xE (the node triggers it, there is no icon / model)
    special = {p['anchor']: {100: 0xF, 101: 0x10, 102: 0xD, 103: 0xE}[p['type']] for p in placed if p['type'] in (100, 101, 102, 103)}
    for i in range(W * H):
        if int(m['road'].flat[i]) == 0: continue
        c = grid.center(i)
        ti = terra_of.get(grid.roadPlot[i], -1)
        ev = int(m['event'].flat[i])
        if ev < 1 and i in special: ev = special[i]
        out_lines.append('rnode %d %d %g %g %g %d %d %d' % (i % W, i // W, c[0], c[1], c[2], int(m['road'].flat[i]), ev, ti))
    bad = [p for p in placed if not p['result'] or p['result']['n'] != p['want_n']]
    npad = 0
    for p in placed:
        r = p['result']
        if not r: continue
        for q in r['pad']:	# trailing field: terra index (-1 = not a Terra plot) for the owner colour (Land_SetOwner)
            out_lines.append('pad %s %s %d' % (plots.PAD_TEX[r['type']], ' '.join('%g %g %g %g %g' % (x, y + 0.3, z, u, v) for x, y, z, u, v in q),
                                               terra_of.get(r['handle'], -1)))
            npad += 1
    cfg = {n: s for n, s in read_cfg(game, 'event.bin')}
    event_art = {int(s['id']): int(s['artId']) for s in cfg.values()}
    nicon = 0
    icon_tex, icon_fx = art_action_events(game)
    for i in range(W * H):
        ev = int(m['event'].flat[i])
        if ev <= 0 and (int(m['plot'].flat[i]) & 0xFF) in (100, 101, 102, 103):
            ev = {100: 15, 101: 16, 102: 13, 103: 14}[int(m['plot'].flat[i]) & 0xFF]   # [V] Unk_BuildObj_004f19e0
        rec = at.get(event_art.get(ev, -1))
        if ev <= 0 or not rec: continue
        c = grid.center(i)
        # [V] Unk_ObjectCreate_ee230: article at the road tile, vf34 rotation -45, vf23(animation 3, play count 1) = play once and hold
        out_lines.append('article %s %s %g %g %g 1 1 1 -45 %s %s once' % (rec[0].lower(), rec[1].lower(), c[0], c[1], c[2],
                         rec[4].get(3, rec[2]).lower(), rec[3].lower())); nicon += 1
        # [V] Role_StepAlongPath @0x485860: the road node (StPoint +8) owns this icon; Article_PlayApproachEffect @0x4ee3c0 /
        # Article_PlayDepartEffect @0x4ee4f0 switch it between the actions below (docs/boardfx.md).
        # Line (follows its article line): icon <tile x> <tile y> <action>:<anm> ...
        out_lines.append('icon %d %d %s' % (i % W, i // W, ' '.join('%d:%s' % (k, v.lower()) for k, v in sorted(rec[4].items()))))
        # [V] role.bin timed action events of the icon art (art_action_events): itex <action> <time> <material, spaces as _> <texture key>,
        # ifx <action> <time> <effect id> <hook> <type>; Article_Update @0x448540 runs them while the action plays (boardfx.cpp)
        ea = event_art[ev]
        for act, tm, mat, tx in icon_tex.get(ea, []): out_lines.append('itex %d %.4f %s %s' % (act, tm, mat.replace(' ', '_') or '-', tx.lower()))
        for act, tm, eff, hook, typ in icon_fx.get(ea, []): out_lines.append('ifx %d %.4f %d %d %d' % (act, tm, eff, hook, typ))
    # [V] Unk_GameLoop_f0d00 (new game): per street, a third of the residential plots get a house of level 1..3
    # (building.txt [住宅] Level<n>artId, +5 when the stage's clime > 1), vf23(animation 2, play count 0) = loop. [A] fixed random seed.
    clime = 1
    for _, s in read_cfg(game, 'stage.bin'):
        if s.get('mapfile', '').lower() == (mid + '.map').lower(): clime = int(s.get('clime', 1))
    house = next(s for _, s in read_cfg(game, 'building.bin') if int(s.get('Id', 0)) == 1)
    rng = random.Random(8)
    streets = {}
    level0 = [0] * len(terras)
    for k, r in enumerate(terras):
        if r['type'] == 1:
            st = int(m['street'].flat[r['roads'][0]])
            if st: streets.setdefault(st, []).append(k)
    nhouse = 0
    for st in sorted(streets):
        cand = list(streets[st])
        for _ in range(len(cand) // 3):
            k = cand.pop(rng.randrange(len(cand)))
            level0[k] = rng.randrange(3) + 1; nhouse += 1
    for k, r in enumerate(terras):
        st = int(m['street'].flat[r['roads'][0]]) if r['type'] == 1 else 0
        out_lines.append('terra %d %d %d %g %g %g %g %d' % (k, r['type'], st, *r['pos'], r['angle'], level0[k]))
    # building models (building.bin Level<n>artId, residential +5 when clime > 1) and the owner signs (art 1100 + role id)
    arts = set()
    for bname, b in read_cfg(game, 'building.bin'):
        out_lines.append('bname %s %s' % (b['Id'], bname.strip()))
        for lv in range(1, 6):
            a_ = int(b['Level%dartId' % lv]) + (5 if int(b['Id']) == 1 and clime > 1 else 0)
            out_lines.append('building %s %d %d' % (b['Id'], lv, a_)); arts.add(a_)
    for rid in range(1, 17): arts.add(1100 + rid)
    arts.update((6001, 6002, 6006))	# [V] board effects: build / demolish sparks (docs/rules_land.md 6.2), the bankruptcy ghost (OnMovieDrama @0x492F30)
    for a_ in sorted(arts):
        rec = at.get(a_)
        if rec: out_lines.append('bart %d %s %s %s' % (a_, rec[0].lower(), rec[1].lower(), ' '.join('%d:%s' % (k, v.lower()) for k, v in sorted(rec[4].items())) or '-'))
    for nm, sr in read_cfg(game, 'street.bin'):	# street <ID> <price %> <name (UTF-8, '-' if empty)>
        out_lines.append('street %s %s %s' % (sr['ID'], sr['price'], nm.strip().replace(' ', '_') or '-'))
    print('plots: %d anchors, %d placed, %d mismatched (n), pads=%d quads, event icons=%d, starting houses=%d' % (
        len(placed), sum(1 for p in placed if p['result']), len(bad), npad, nicon, nhouse))
    for p in bad[:8]: print('   unresolved anchor', p['anchor'], 'type', p['type'], 'n', p['want_n'], '->', p['result'] and p['result']['n'])
    # ---- articles (placed models)
    miss = set(); nomodel = set(); narts = 0; nalm = 0; nprt = 0
    alm = load_ltm_B(os.path.join(game, 'Map', mid + '.ltm'))
    prt_eff, prt_hooks = prt_table(game)
    snd_events = art_sound_events(game)
    for ai, a in enumerate(m['arts']):
        if a['art'] in prt_eff:	# [V] 0x43C470 (+0x68 == 1): effect at the map position, frame translated only
            out_lines.append('prtat %d %g %g %g' % (prt_eff[a['art']], *a['pos'])); nprt += 1
            continue
        if a['art'] not in at: miss.add(a['art']); continue
        if at[a['art']] is None: nomodel.add(a['art']); continue
        dff, td, anim, uva, _ = at[a['art']]
        extra = ''
        if ai in alm:      # baked article lightmap
            fn = 'alm_%03d.png' % ai; Image.fromarray(alm[ai][0]).save(os.path.join(out, fn)); nalm += 1
            extra = ' lm %s %s' % (fn, ' '.join('%r' % v for v in alm[ai][1]))
        out_lines.append('article %s %s %g %g %g %g %g %g %g %s %s loopr%s' % (dff.lower(), td.lower(), *a['pos'], *a['scale'], a['roty'], anim.lower(), uva.lower(), extra)); narts += 1
        for hook, eid in prt_hooks.get(a['art'], []):	# attached to the article just above
            out_lines.append('prt %d %d' % (eid, hook)); nprt += 1
        if a['owner'] >= 0: out_lines.append('aown %d' % a['owner'])	# [V] article record [17] = the owner role (3 / 4 / 5 / 10 on the sign art 1115 = sign\a015, one per map 01..11): only the story hero of that role gets the sign (docs/characters.md)
        # [V] CArticleMgr__vf29_RandomizeVariant @0x43E9C0 (a click on the article): action 1 + rand % (count - 1) played once, then action 0
        # again; with one action per row of role.bin sub-list 1. Lines follow their article: aact <action>:<anm> ... (actions >= 1),
        # asnd <action> <sound id> <flag> <time s> ([V] art_sound_events)
        acts = at[a['art']][4]
        if len(acts) > 1: out_lines.append('aact ' + ' '.join('%d:%s' % (k, v.lower()) for k, v in sorted(acts.items()) if k > 0))
        for act, snd, flag, tm in snd_events.get(a['art'], []):
            out_lines.append('asnd %d %d %d %g' % (act, snd, flag, tm))
    if m['water']:
        w = m['water']
        # [V] CWater_LoadState @0x477230: +0x00 id indexes the list that CWater_Initialize @0x476a30 fills with
        # "%s\texture\water\" + "entironment%d" (sky reflection maps); [A] drawn as a planar texture (reflection camera not reproduced)
        out_lines.append('water %g %g %g %g %g water\\entironment%d.tga %g %g %g %g %g %g' % ((w['y'],) + tuple(w['color']) + (w['tex'],) + (w['bump'],) + tuple(w['dir']) + (w['speed'], w['lscale'])))
    open(os.path.join(out, 'scene.txt'), 'w', encoding='utf-8', newline='\n').write('\n'.join(out_lines) + '\n')
    # overview with article positions
    ovi = Image.fromarray(ov)
    from PIL import ImageDraw
    dr = ImageDraw.Draw(ovi)
    for a in m['arts']:
        px_ = (a['pos'][0] + W * 25) / 50 * 16; pz_ = (a['pos'][2] + H * 25) / 50 * 16
        dr.ellipse([px_ - 6, pz_ - 6, px_ + 6, pz_ + 6], outline=(255, 0, 0), width=2)
    ovi.resize((1024, 1024)).save(os.path.join(out, 'overview.png'))
    print('map %s: %dx%d leaves=%d roads=%d articles=%d/%d particles=%d no_model_ids=%s missing_art_ids=%s water=%s' % (
        mid, W, H, len(leaves), nroad, narts, len(m['arts']), nprt, sorted(nomodel), sorted(miss), bool(m['water'])))

if __name__ == '__main__':
    main()
