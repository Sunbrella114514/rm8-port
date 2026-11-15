"""Reimplementation of the game's plot (terra) placement at map load, so plot pads / buildings can be drawn.
All rules follow the decompiled code; function addresses are noted per routine.

Tile memory fields used (see docs/map_format.md 1.4): +0x2C road, +0x44 plot word (on ROAD tiles: anchor<<16 | n<<8 | type),
+0x28 road-mesh handle, +0x20 cliff-mesh handle, +0x5C plot handle on footprint tiles, +0x40 plot handle on road tiles.
"""
import math

FLAT_CODES = {0x1122, 0x2121, 0x2211, 0x1212, 0x11, 0x1010, 0x1100, 0x101}   # [V] DAT_00604110
CAT1_ANGLE = [0, 180, 90, 270, 45, 225, 315, 135]                          # [V] Map_PlaceObjCat1 local_a8
TERRA_SIZE = {1: 1, 2: 2, 3: 2, 4: 3, 5: 4, 6: 3, 7: 4, 8: 3, 9: 4, 100: 2, 101: 2, 102: 2, 103: 2}   # terra.txt size
PAD_TEX = {1: 'terra\\dichange_h_0.tga', 2: 'terra\\dichange_b_0.tga', 4: 'terra\\dichange_q_0.tga', 5: 'terra\\dichange_q_0.tga',
           6: 'terra\\dichange_q_0.tga', 7: 'terra\\dichange_q_0.tga', 8: 'terra\\dichange_q_0.tga', 9: 'terra\\dichange_q_0.tga'}


class Grid:
    def __init__(self, W, H, road, levels, heights, roadmesh):
        self.W, self.H = W, H
        self.road = road            # flat list, +0x2C
        self.lev = levels           # (H+1)x(W+1) vertex levels
        self.hgt = heights          # (H+1)x(W+1) vertex heights
        self.roadmesh = roadmesh    # flat list of bool, +0x28 valid
        self.cliff = [self.cliff_code(i) & 0xFFFF != 0 for i in range(W * H)]   # +0x20 valid
        self.plotH = [None] * (W * H)   # +0x5C
        self.roadPlot = [None] * (W * H)  # +0x40
        self.nextHandle = 0

    def xy(self, i): return i % self.W, i // self.W

    def corners(self, i):
        x, y = self.xy(i)
        return [(x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)]

    def cliff_code(self, i):
        """[V] Map_CalcTileCliffCode @0x4233c0. [A] odd levels: only the +1 rounding is reproduced (the '3' marker
        that compares with neighbouring vertices is not)."""
        lv = []
        for (x, y) in self.corners(i):
            l = int(self.lev[y][x]); lv.append(l + 1 if l % 2 else l)
        mx = max(lv)
        code = 0
        for k, l in enumerate(lv): code |= ((mx - int(self.lev[self.corners(i)[k][1]][self.corners(i)[k][0]])) & 0xF) << (4 * k)
        return (mx << 16) | (0 if code in FLAT_CODES else code)

    def center(self, i):
        x, y = self.xy(i)
        cy = sum(self.hgt[b][a] for a, b in self.corners(i)) / 4
        return [x * 50.0 - self.W * 25 + 25, cy, y * 50.0 - self.H * 25 + 25]

    def max_height(self, i): return max(self.hgt[b][a] for a, b in self.corners(i))

    def nb8(self, i):
        """[V] TileGrid_GetNeighborTiles8 @0x41f950: up, down, left, right, UL, DR, UR, DL; None unless all 8 exist."""
        x, y = self.xy(i)
        if not (0 < x < self.W - 1 and 0 < y < self.H - 1): return None
        W = self.W
        return [i - W, i + W, i - 1, i + 1, i - W - 1, i + W + 1, i - W + 1, i + W - 1]

    def same_line(self, a, b): return a % self.W == b % self.W or a // self.W == b // self.W

    def common(self, a, b):
        """[V] TileGrid_GetCommonNeighbors @0x41fc00: [a, b] + common 8-neighbours in a's neighbour order."""
        out = [a, b]
        na, nb = self.nb8(a) or [], self.nb8(b) or []
        out += [t for t in na if t in nb]
        return out

    def free_plot(self, t, own): return self.plotH[t] is None or self.plotH[t] == own
    def is_route(self, t): return 0 < self.road[t] < 100

    def footprint_valid(self, L, k):
        """[V] TileGrid_CheckFootprintValid @0x4202f0"""
        if k > 1:
            if any(self.road[t] == 0 for t in L[:k]): return False
            if not self.same_line(L[0], L[1]):
                for i in range(k - 1):
                    for j in range(i + 1, k):
                        c = self.common(L[i], L[j])
                        if len(c) == 4 and (self.road[c[2]] != 0 or self.road[c[3]] != 0): return False
        return True

    def enum_blocks(self, L, k):
        """[V] TileGrid_EnumFootprintBlocks @0x41fcd0"""
        W, H = self.W, self.H; out = []
        xs = [t % W for t in L[:k]]; ys = [t // W for t in L[:k]]
        if xs[0] == xs[1]:
            for i in range(k):
                if xs[i] > 1: out += [L[i] - 1, L[i] - 2]
            for i in range(k):
                if xs[i] < W - 2: out += [L[i] + 1, L[i] + 2]
        elif ys[0] == ys[1]:
            for i in range(k):
                if ys[i] > 1: out += [L[i] - W, L[i] - 2 * W]
            for i in range(k):
                if ys[i] < H - 2: out += [L[i] + W, L[i] + 2 * W]
        else:
            nb = self.nb8(L[0])
            if nb:
                for a0 in (4, 6):
                    if any(L[j] in (nb[a0], nb[a0 + 1]) for j in range(k)): continue
                    for a8 in (a0, a0 + 1):
                        start = len(out)
                        out.append(nb[a8])
                        for b in range(1, k):
                            n2 = self.nb8(L[b])
                            if n2: out.append(n2[a8])
                        end = len(out)
                        for b8 in range(start, end - 1):
                            n7 = self.nb8(out[b8])
                            if not n7: continue
                            for bc in range(b8 + 1, end):
                                n9 = self.nb8(out[bc])
                                if not n9: continue
                                for c in n7:
                                    if c in n9 and c not in out: out.append(c)
        return out

    # ---------------------------------------------------------------- placement
    def calc_placement(self, block, roads):
        """[V] Map_CalcObjPlacement @0x425470 -> (pos, angle in degrees)"""
        P = [sum(self.center(t)[c] for t in block) / len(block) for c in range(3)]
        R = [sum(self.center(t)[c] for t in roads) / len(roads) for c in range(3)]
        dx, dz = P[0] - R[0], P[2] - R[2]; ln = math.hypot(dx, dz) or 1.0
        dx, dz = dx / ln, dz / ln
        a = int(math.degrees(math.acos(max(-1.0, min(1.0, -dz)))))   # _ftol truncation
        ang = float(((a + 22) // 45) * 45)
        if dx > 0: ang = -ang
        if not self.same_line(roads[0], roads[1]):
            P = [P[c] + (P[c] - R[c]) * 0.15 for c in range(3)]
        P[1] = max(self.max_height(t) for t in block)
        return P, ang

    def pad_xform(self, block, roads, pos, ang):
        """[V] Map_BuildObjPadMeshXform @0x426c90: u = x'/(nRoads*50) + .5, v = z'*0.01 + .5 after T(-pos) R_y(ang)."""
        tiles = list(block)
        if not self.same_line(roads[0], roads[1]):
            for t in block:
                nb = self.nb8(t)
                if nb:
                    for c in nb[:4]:
                        if c not in tiles: tiles.append(c)
        return self.quads(tiles, pos, ang, 1.0 / (len(roads) * 50.0), 0.01)

    def quads(self, tiles, C, ang, su, sv):
        c, s = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        verts = []
        for t in tiles:
            q = []
            for (x, y) in self.corners(t):
                px, py, pz = x * 50.0 - self.W * 25, self.hgt[y][x], y * 50.0 - self.H * 25
                X, Z = px - C[0], pz - C[2]
                # RwMatrixRotate about +Y by ang (row vectors): x' = x cos + z sin, z' = -x sin + z cos
                xr, zr = X * c + Z * s, -X * s + Z * c
                q.append((px, py, pz, xr * su + 0.5, zr * sv + 0.5))
            verts.append(q)
        return verts

    def create(self, block, roads, typ, cnt):
        """[V] Map_CreateTileObject @0x424e50 (pads only; model types 3/100..103 have no usable model)"""
        pos, ang = self.calc_placement(block, roads)
        h = self.nextHandle; self.nextHandle += 1
        for r in roads: self.roadPlot[r] = h
        for t in block: self.plotH[t] = h
        pad = self.pad_xform(block, roads, pos, ang) if typ in PAD_TEX else []
        return dict(handle=h, type=typ, n=cnt, pos=pos, angle=ang, roads=list(roads), block=list(block), pad=pad)

    def cat1(self, a, typ, n):
        """[V] Map_PlaceObjCat1 @0x425040"""
        nb = self.nb8(a)
        if not nb: return None
        own = self.roadPlot[a]; cnt = 0; chosen = None; chosen_cnt = 0
        for k in range(8):
            t = nb[k]
            if self.roadmesh[t] or self.cliff[t] or not self.free_plot(t, own): continue
            if k > 3:
                n2 = self.nb8(t)
                if n2:
                    ok = True
                    for m in range(4):
                        u = n2[m]
                        if not self.same_line(u, a) or u == a: continue
                        if self.road[u] == 0 and not self.cliff[u] and self.free_plot(u, own): continue
                        ok = False; break
                    if not ok: continue
            cnt += 1
            if cnt > 32 or cnt == n or chosen_cnt == 0:
                chosen, chosen_cnt = k, cnt
                if cnt > 32 or cnt == n: break
        if not chosen_cnt: return None
        t = nb[chosen]
        h = self.nextHandle; self.nextHandle += 1
        self.roadPlot[a] = h; self.plotH[t] = h
        pos = self.center(t)
        ang = float(CAT1_ANGLE[chosen])
        pad = []
        if typ in PAD_TEX:
            if self.same_line(t, a):     # [V] Map_BuildObjPadMesh: the plot tile, UV = corner (i%2, i/2)
                q = []
                for k, (x, y) in enumerate(self.corners(t)):
                    q.append((x * 50.0 - self.W * 25, self.hgt[y][x], y * 50.0 - self.H * 25, float(k % 2), float(k // 2)))
                pad = [q]
            else:                         # diagonal: common neighbours, centre at road + 0.8*(plot-road), R_y(45), scale 0.02
                P, R = self.center(t), self.center(a)
                C = [R[c] + 0.8 * (P[c] - R[c]) for c in range(3)]
                pad = self.quads(self.common(t, a), C, 45.0, 0.02, 0.02)
        return dict(handle=h, type=typ, n=chosen_cnt, pos=pos, angle=ang, roads=[a], block=[t], pad=pad)

    def multi(self, a, typ, n, size):
        """[V] Map_PlaceObjCat2 @0x425840 / Cat3 @0x425bb0 / Cat4 @0x4260a0"""
        nb = self.nb8(a)
        if not nb: return None
        own = self.roadPlot[a]; cnt = 0; best = None
        road_ok = lambda t: self.is_route(t) and (self.roadPlot[t] is None or self.roadPlot[t] == own)
        def scan(L, k_dir_diag, groups):
            nonlocal cnt, best
            blocks = self.enum_blocks(L, size)
            g = next((g for g in groups if len(blocks) % g == 0), None) if blocks else groups[0]
            if g is None: return 'skip'
            nb_blocks = len(blocks) // g
            for b in range(nb_blocks):
                blk = blocks[b * g:(b + 1) * g]
                if g in (4, 6, 8) and not (size == 2 and k_dir_diag):
                    ok = all(not self.roadmesh[t] and not self.cliff[t] and self.free_plot(t, own) for t in blk)
                else:
                    ok = all(self.road[t] == 0 and not self.cliff[t] and self.free_plot(t, own) for t in blk)
                if ok:
                    cnt += 1
                    if cnt > 32 or cnt == n or best is None:
                        best = (blk, list(L[:size]), cnt)
                        if cnt > 32 or cnt == n: return 'stop'
            return 'next'
        if size == 2:
            for k in range(8):
                t = nb[k]
                if not road_ok(t): continue
                L = [a, t]
                if not self.footprint_valid(L, 2): break
                r = scan(L, k >= 4, [4])
                if r == 'stop': break
        elif size == 3:
            for k in range(0, 8, 2):
                if not (road_ok(nb[k]) and road_ok(nb[k + 1])): continue
                L = [a, nb[k], nb[k + 1]]
                if not self.footprint_valid(L, 3): break
                r = scan(L, False, [6, 7])
                if r == 'stop': break
        else:
            k = 0
            while k < 8:
                if road_ok(nb[k]) and road_ok(nb[k + 1]):
                    L = [a, nb[k], nb[k + 1], None]
                    stop = False
                    for e in range(2):
                        n2 = self.nb8(L[e + 1])
                        if n2 is None: continue
                        nb = n2                      # the game overwrites its neighbour array here (kept on purpose)
                        if not road_ok(nb[k + e]): continue
                        L[3] = nb[k + e]
                        if not self.footprint_valid(L, 4): break
                        r = scan(L, False, [8, 10])
                        if r == 'stop': stop = True; break
                    if stop or (cnt > 32 or cnt == n): break
                k += 2
        if best is None: return None
        blk, roads, c = best
        return self.create(blk, roads, typ, c)

    def place_all(self, plot_words):
        """[V] Map_LoadFromStream_Build @0x420f90: anchors (road tiles with anchor == self) in tile order."""
        out = []
        for i, (pw, anchor) in enumerate(plot_words):
            if not pw or anchor != i: continue
            typ, n = pw & 0xFF, (pw >> 8) & 0xFF
            size = TERRA_SIZE.get(typ, 0)
            if size == 1: o = self.cat1(i, typ, n)
            elif size in (2, 3, 4): o = self.multi(i, typ, n, size)
            else: o = None
            out.append(dict(anchor=i, type=typ, want_n=n, result=o))
        return out
