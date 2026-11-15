"""Reimplementation of the game's cliff (terrain template) meshes.
[V] Cliff_LoadModels @0x434950: clump\\cliff\\cliff00..13.dff, each rotated by 0/90/180/270 degrees about +Y; the code of
    template i (i % 10) and rotation r is DAT_005e0aa0[i % 10][r]. Templates 10..13 replace 0..3 when the tile's land word
    low 16 bits == 1 (Cliff_FindById @0x434890, map+0x414), and then the texture is forced to 2 (Cliff_RenderFrame).
[V] Cliff_ComputeLight @0x4356b0: per-vertex corner weights = normalised inverse 3D distance to the template's corner
    vertices (weight 1 when the vertex is at a corner).
[V] Cliff_RenderFrame @0x434f70: template transformed to (tile centre x, (maxLevel-100)*12.5, tile centre z); for every
    template vertex lying on a tile corner (|dx|,|dz| < 1) the corner offset = real corner height - vertex y (the last such
    vertex per corner wins); every vertex y += sum(offset[k] * weight[k]).
    Texture = cliff list entry (texture\\cliff\\Cliff0..2); index = tile land word >> 16, or 2 when the low word == 1.
"""
import math, struct, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rm8pak

CODES = [[0x0200, 0x2000, 0x0020, 0x0002], [0x2200, 0x2020, 0x0022, 0x0202], [0x2220, 0x2022, 0x0222, 0x2202],
         [0xffff, 0x2002, 0x0220, 0xffff], [0x1200, 0x2010, 0x0021, 0x0102], [0x2100, 0x1020, 0x0012, 0x0201],
         [0x1202, 0x2210, 0x2021, 0x0122], [0x2120, 0x1022, 0x0212, 0x2201], [0x3212, 0x2231, 0x2123, 0x1322],
         [0x2321, 0x3122, 0x1232, 0x2213]]          # [V] DAT_005e0aa0
CORNERS = [(-25.0, -25.0), (25.0, -25.0), (-25.0, 25.0), (25.0, 25.0)]   # (x,y) (x+1,y) (x,y+1) (x+1,y+1)


def _chunks(d, o, end):
    while o + 12 <= end:
        t, sz, lib = struct.unpack_from('<III', d, o); yield t, o + 12, sz; o += 12 + sz


def frame_ltm(d):
    """LTM of the frame the first atomic hangs on (RW row vectors: v * M); frame list chunk 0x0E, atomic 0x14"""
    frames = []; atom_frame = 0
    for t, o, sz in _chunks(d, 0, len(d)):
        if t != 0x10: continue
        for t2, o2, sz2 in _chunks(d, o, o + sz):
            if t2 == 0x0E:
                st = next(_chunks(d, o2, o2 + sz2)); p = st[1]
                n = struct.unpack_from('<I', d, p)[0]; p += 4
                for i in range(n):
                    v = struct.unpack_from('<12f', d, p); parent = struct.unpack_from('<i', d, p + 48)[0]; p += 56
                    frames.append((v, parent))
            elif t2 == 0x14 and not atom_frame:
                st = next(_chunks(d, o2, o2 + sz2)); atom_frame = struct.unpack_from('<I', d, st[1])[0]
        break
    def mat(i):
        v, parent = frames[i]
        m = [list(v[0:3]), list(v[3:6]), list(v[6:9]), list(v[9:12])]   # right, up, at, pos
        if parent < 0: return m
        pm = mat(parent)
        out = [[sum(m[r][k] * pm[k][c] for k in range(3)) for c in range(3)] for r in range(3)]
        out.append([sum(m[3][k] * pm[k][c] for k in range(3)) + pm[3][c] for c in range(3)])
        return out
    return mat(atom_frame) if frames else [[1, 0, 0], [0, 1, 0], [0, 0, 1], [0, 0, 0]]


def xform(p, m, point=True):
    return tuple(p[0] * m[0][c] + p[1] * m[1][c] + p[2] * m[2][c] + (m[3][c] if point else 0) for c in range(3))


def read_geometry(d):
    """first geometry of a clump: positions, normals, prelit colours, uv0, triangles (v0,v1,v2)"""
    def find(o, end, depth):
        for t, oo, sz in _chunks(d, o, end):
            if t == 0x0F: return oo, sz
            if t in (0x10, 0x1A, 0x03) and depth < 4:
                r = find(oo, oo + sz, depth + 1)
                if r: return r
        return None
    go, gsz = find(0, len(d), 0)
    st = next(_chunks(d, go, go + gsz)); p = st[1]
    flags, ntri, nv, nmorph = struct.unpack_from('<IIII', d, p); p += 16
    nuv = (flags >> 16) & 0xFF or (2 if flags & 0x80 else (1 if flags & 0x04 else 0))
    cols = None
    if flags & 0x08: cols = list(struct.unpack_from('<%dI' % nv, d, p)); p += 4 * nv
    uvs = [struct.unpack_from('<ff', d, p + 8 * i) for i in range(nv)]; p += 8 * nv * nuv
    tris = []
    for i in range(ntri):
        v1, v0, mat, v2 = struct.unpack_from('<HHHH', d, p + 8 * i); tris.append((v0, v1, v2))
    p += 8 * ntri
    p += 16
    hasv, hasn = struct.unpack_from('<II', d, p); p += 8
    pos = [struct.unpack_from('<fff', d, p + 12 * i) for i in range(nv)]; p += 12 * nv
    nrm = [struct.unpack_from('<fff', d, p + 12 * i) for i in range(nv)] if hasn else [(0, 1, 0)] * nv
    return dict(pos=pos, nrm=nrm, uv=uvs, col=cols, tris=tris)


def corner_weights(pos):
    """[V] Cliff_ComputeLight"""
    ref = []
    for cx, cz in CORNERS:
        r = (cx, 0.0, cz)
        for p in pos:
            if abs(cx - p[0]) < 1 and abs(cz - p[2]) < 1: r = p; break
        ref.append(r)
    out = []
    for p in pos:
        d = [math.dist(p, r) for r in ref]
        w = None
        for k in range(4):
            if d[k] < 1: w = [0.0] * 4; w[k] = 1.0; break
        if w is None:
            mn = min(d); w = [mn / x for x in d]; s = sum(w); w = [x / s for x in w]
        out.append(w)
    return out


def rot_y(v, deg):
    """RwMatrixRotate about +Y (row vectors): x' = x cos + z sin, z' = -x sin + z cos"""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return (v[0] * c + v[2] * s, v[1], -v[0] * s + v[2] * c)


class Cliffs:
    def __init__(self, game):
        f, ents, _ = rm8pak.read_index(os.path.join(game, 'Clump', 'clump0000.dat'))
        idx = {e['name'].lower(): e for e in ents}
        self.tpl = {}     # (template i, rotation r) -> dict(code, pos, uv, tris, w)
        for i in range(14):
            e = idx['cliff\\cliff%02d.dff' % i]; f.seek(e['off'])
            raw = f.read(e['size'])
            g = read_geometry(raw)
            ltm = frame_ltm(raw)                      # templates 4..7 carry a rotation on their root frame
            g['pos'] = [xform(p, ltm) for p in g['pos']]
            for r in range(4):
                pos = [tuple(round(c, 4) for c in rot_y(p, r * 90)) for p in g['pos']]
                self.tpl[(i, r)] = dict(code=CODES[i % 10][r], pos=pos, uv=g['uv'], tris=g['tris'], w=corner_weights(pos))

    def find(self, code, alt):
        """[V] Cliff_FindById: templates 0..9 (0..3 -> 10..13 when alt), rotations 0..3, first code match"""
        for i in range(10):
            ii = 10 + i if (alt and i <= 3) else i
            for r in range(4):
                if self.tpl[(ii, r)]['code'] == code: return self.tpl[(ii, r)]
        return None

    def mesh(self, tpl, cx, cz, base_y, corner_y):
        """[V] Cliff_RenderFrame deformation; returns vertex list [(x,y,z,u,v)] and triangles"""
        P = [(p[0] + cx, p[1] + base_y, p[2] + cz) for p in tpl['pos']]
        off = [0.0] * 4
        for p in P:
            for k, (dx, dz) in enumerate(CORNERS):
                if abs(p[0] - (cx + dx)) < 1 and abs(p[2] - (cz + dz)) < 1:
                    off[k] = corner_y[k] - p[1]; break
        V = []
        for p, w, uv in zip(P, tpl['w'], tpl['uv']):
            V.append((p[0], p[1] + sum(o * ww for o, ww in zip(off, w)), p[2], uv[0], uv[1]))
        return V, tpl['tris']
