#!/usr/bin/env python
"""大富翁8 粒子特效：PrtEffTable.tb / uvlisttable.tb 字段解析 + 参考模拟器 + 预览图。

文档见 docs/particles.md。每个字段名都对应到读取它的游戏代码（见 PAYLOAD_FIELDS 的 ref 列）。
文件结构的逐字节遍历复用 prtfmt.py（已验证剩余 0 字节），这里只解释 0x160 字节 payload。

用法（在 D:\\ai\\rm8 下，python 指 .venv\\Scripts\\python.exe -I）：
  python scripts/prteff.py dump [--json] [--id N]        打印每个效果的命名字段
  python scripts/prteff.py table                         输出 26 个效果的 Markdown 参数表
  python scripts/prteff.py sim ID [--dt 0.016] [--frames N | --until T] [--every K]
                               [--prewarm 1.0] [--matrix r0 r1 r2 u0 u1 u2 a0 a1 a2 p0 p1 p2]
                               [--dts file]              按游戏算法逐帧模拟，输出 JSON 行
  python scripts/prteff.py preview ID --out DIR [--times 0.5,1,2,4] [--dt 0.016]
                                                         正交侧视预览 PNG（需要 numpy、Pillow）

模拟器语义（详见 docs/particles.md 第 3 节）：
  * 随机数：RW 3.7 prtstd 的 LCG  seed = seed*0xBB38435 + 0x3619636B (mod 2^32)，
    r = 2*seed/2^32 - 1 ∈ [-1,1)；新建发射器 seed = 1（0x5A02E0）。
  * 每帧顺序：[游戏层 0x46B010 持续时间] → BEGINUPDATE(0x5A0570, currTime+=dt)
    → 粒子更新（多段颜色 0x5A3F10 → UV 步进 0x5A3780 → 标准 0x59E4F0）
    → 发射（标准 0x59F0E0 → 球形 0x5A2D60 → 多段颜色 0x5A4140 → UV 步进 0x5A3870）
    → ENDUPDATE(0x5A0630, 安排下次发射) → [游戏层 0x46D470 超时关发射]
  * 输出的 color/size/uv/rot 是写进 PTank（最终画出来）的值；颜色是截断后的字节。
"""

import argparse
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prtfmt  # noqa: E402

GAME = r"<old-edition-dir>"
PRT_TABLE = os.path.join(GAME, "Config", "prt", "PrtEffTable.tb")
UVL_TABLE = os.path.join(GAME, "Config", "prt", "uvlisttable.tb")
TEX_DAT = os.path.join(GAME, "Texture", "tex0000.dat")

# ---------------------------------------------------------------------------
# payload 0x160 字节的字段表：(偏移, 类型, 名字, 读取它的代码)
#   类型: f=f32 i=i32 I=u32 H=u16 B4=4 字节 RGBA  3f=3 个 f32  2f=2 个 f32
#   "ref" 只写最直接的读取点；BuildRender=0x468540 (游戏把 payload 翻译成 RW 发射器属性)，
#   UpdateRender=0x469900 (链式子发射器用，同样的翻译)，CopyData=0x4682B0，PlayAnimation=0x46AB80
# ---------------------------------------------------------------------------
PAYLOAD_FIELDS = [
    (0x00, "I", "shape", "BuildRender switch(*p): 0=盒 1=圆环(0x1000007) 2=球(0x1000008) 3=无形状属性"),
    (0x04, "H", "uvlist_id", "BuildRender *(ushort*)(p+4) -> uvlisttable[id]; PlayAnimation Article_BuildMesh(id)"),
    (0x06, "H", "pad_06", "无代码读取（全 0）"),
    (0x08, "I", "simple_mode", "BuildRender p[2]: 0=带颜色/UV 属性+游戏自建 PTank；1=只用标准属性、RW 自动建 PTank"),
    (0x0C, "I", "vertex_alpha", "PlayAnimation -> PTank+0xAC (VERTEXALPHAENABLE)"),
    (0x10, "I", "src_blend", "PlayAnimation -> PTank+0xA4 (rwRENDERSTATESRCBLEND 值)"),
    (0x14, "I", "dst_blend", "PlayAnimation -> PTank+0xA8 (rwRENDERSTATEDESTBLEND 值)"),
    (0x18, "f", "duration", "PlayAnimation obj[0]; Article_UpdateDuration 0x46B010; <=0.001 表示无限"),
    (0x1C, "3f", "emt_size", "CopyData -> std+0x28 emtSize（盒半边长）"),
    (0x28, "i", "max_prt", "CopyData -> std+4；SetPClass 批容量；GetLightEffect PTank 容量"),
    (0x2C, "i", "emit_num", "CopyData -> std+0x44 emtPrtEmit"),
    (0x30, "i", "emit_num_bias", "CopyData -> std+0x48 emtPrtEmitBias"),
    (0x34, "f", "emit_gap", "CopyData -> std+0x34 emtEmitGap（秒）"),
    (0x38, "f", "emit_gap_bias", "CopyData -> std+0x38 emtEmitGapBias"),
    (0x3C, "f", "life", "CopyData -> std+0x54 prtLife（秒）"),
    (0x40, "f", "life_bias", "CopyData -> std+0x58 prtLifeBias"),
    (0x44, "f", "vel", "CopyData -> std+0x4C prtInitVel（单位/秒）"),
    (0x48, "f", "vel_bias", "CopyData -> std+0x50 prtInitVelBias"),
    (0x4C, "3f", "dir", "CopyData -> std+0x5C prtInitDir"),
    (0x58, "3f", "dir_bias", "CopyData -> std+0x68 prtInitDirBias"),
    (0x64, "3f", "force", "CopyData -> std+0x10 force（加速度，单位/秒²）"),
    (0x70, "B4", "const_color", "BuildRender 仅 simple_mode=1：std+0x7C prtColor (R,G,B,A)"),
    (0x74, "I", "unk_74", "无代码读取"),
    (0x78, "2f", "size_start", "BuildRender std+0x74 prtSize 或 size 属性[0,1]"),
    (0x80, "2f", "size_end", "BuildRender size 属性[4,5]"),
    (0x88, "2f", "size_start_bias", "BuildRender size 属性[2,3]"),
    (0x90, "2f", "size_end_bias", "BuildRender size 属性[6,7]"),
    (0x98, "f", "rot_start", "BuildRender 2D 旋转属性[0]（弧度）；无旋转属性时 -> std+0x80"),
    (0x9C, "f", "rot_start_bias", "BuildRender 2D 旋转属性[1]"),
    (0xA0, "f", "rot_end", "BuildRender 2D 旋转属性[2]"),
    (0xA4, "f", "rot_end_bias", "BuildRender 2D 旋转属性[3]"),
    (0xA8, "f", "circle_radius", "BuildRender shape=1 属性[0]"),
    (0xAC, "f", "circle_radius_gap", "BuildRender shape=1 属性[1]"),
    (0xB0, "f", "circle_height", "BuildRender shape=1 属性[2]"),
    (0xB4, "f", "circle_dir_rotation", "BuildRender shape=1 属性[4]"),
    (0xB8, "I", "circle_use_emission", "BuildRender shape=1 属性[3] = (byte)p[0x2E]"),
    (0xBC, "f", "sphere_radius", "BuildRender shape=2 属性[0]"),
    (0xC0, "f", "sphere_radius_gap", "BuildRender shape=2 属性[1]"),
    (0xC4, "I", "sphere_use_emission", "BuildRender shape=2 属性[2] = (byte)p[0x31]"),
    (0xC8, "i", "chain_effect", "BuildRender p[0x32] != -1 时挂子发射器（效果编号）"),
    (0xCC, "f", "chain_time", "BuildRender 0x1000001 属性[0]；0x5A5B60 粒子开始子发射的时间"),
    (0xD0, "f", "chain_time_bias", "同上 [1]"),
    (0xD4, "f", "chain_gap", "同上 [2]；子发射间隔"),
    (0xD8, "f", "chain_gap_bias", "同上 [3]"),
    (0xDC, "I", "color_key_count", "BuildRender p[0x37]：<2 无颜色属性，2 起止色，>=3 多段（0x1000002）"),
]
COLOR_KEYS_OFF = 0xE0   # 8 个 16 字节关键帧：time f32, time_bias f32, rgba u8x4, rgba_bias u8x4
MAX_KEYS = 8

BLEND_NAMES = {1: "ZERO", 2: "ONE", 3: "SRCCOLOR", 4: "INVSRCCOLOR", 5: "SRCALPHA", 6: "INVSRCALPHA",
               7: "DESTALPHA", 8: "INVDESTALPHA", 9: "DESTCOLOR", 10: "INVDESTCOLOR", 11: "SRCALPHASAT"}
SHAPE_NAMES = {0: "box", 1: "circle", 2: "sphere", 3: "none"}


def f32(x):
    """把 Python float 按 IEEE 单精度舍入（模拟写回 float 字段）。"""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def decode_payload(p):
    assert len(p) == 0x160
    out = {}
    for off, ty, name, _ in PAYLOAD_FIELDS:
        if ty == "f":
            v = struct.unpack_from("<f", p, off)[0]
        elif ty == "i":
            v = struct.unpack_from("<i", p, off)[0]
        elif ty == "I":
            v = struct.unpack_from("<I", p, off)[0]
        elif ty == "H":
            v = struct.unpack_from("<H", p, off)[0]
        elif ty == "B4":
            v = list(p[off:off + 4])
        elif ty == "3f":
            v = list(struct.unpack_from("<3f", p, off))
        elif ty == "2f":
            v = list(struct.unpack_from("<2f", p, off))
        else:
            raise ValueError(ty)
        out[name] = v
    keys = []
    for k in range(MAX_KEYS):
        b = COLOR_KEYS_OFF + 16 * k
        t, tb = struct.unpack_from("<2f", p, b)
        keys.append({"time": t, "time_bias": tb, "rgba": list(p[b + 8:b + 12]), "rgba_bias": list(p[b + 12:b + 16])})
    out["color_keys_all"] = keys
    out["color_keys"] = keys[:min(out["color_key_count"], MAX_KEYS)]
    # 派生：游戏用来决定是否建立 size / 2D 旋转属性的比较（0x469840 / 0x4698B0）
    out["has_size_prop"] = (out["size_end"] != out["size_start"]) or (out["size_end_bias"] != out["size_start_bias"])
    out["has_rot_prop"] = (out["rot_end"] != out["rot_start"]) or (out["rot_end_bias"] != out["rot_start_bias"])
    return out


def load_tables(prt_path=PRT_TABLE, uvl_path=UVL_TABLE):
    raw = open(prt_path, "rb").read()
    t = prtfmt.parse_prt_eff_table(raw)          # 硬校验：剩余 0 字节、索引与遍历一致
    effects = {}
    for rec in t["records"]:
        if rec["kind"] != 0:
            raise ValueError("kind %d 的记录（0xD8 字节 payload）未分析" % rec["kind"])
        off = rec["offset"] + 4
        e = decode_payload(raw[off:off + 0x160])
        e["id"] = rec["id"]
        effects[rec["id"]] = e
    if len(effects) != t["record_count"]:
        raise ValueError("effect id 重复")
    u = prtfmt.parse_uvlist_table(open(uvl_path, "rb").read())
    uvl = {}
    for rec, ix in zip(u["records"], u["index"]):
        uvl[ix["id"]] = rec
    return effects, uvl, t


# ---------------------------------------------------------------------------
# 参考模拟器
# ---------------------------------------------------------------------------
LCG_A, LCG_C = 0xBB38435, 0x3619636B
PI_F = 3.1415927       # 0x59F0E0 绕角用
PI_SPHERE = 3.1415925  # 0x5A2D60 球面角用


class Rng:
    def __init__(self, seed=1):
        self.seed = seed & 0xFFFFFFFF

    @staticmethod
    def adv(s):
        return (s * LCG_A + LCG_C) & 0xFFFFFFFF

    def next(self):
        """推进一步，返回 [-1,1) 的值（fild u32 * 2^-32 * 2 - 1）。"""
        self.seed = self.adv(self.seed)
        return self.seed * 2.3283064365386963e-10 * 2.0 - 1.0

    def next_signed01(self):
        """推进一步，返回 (int32)seed * 2^-32 + 0.5 ∈ [0,1)（球/圆环半径用）。"""
        self.seed = self.adv(self.seed)
        s = self.seed - 0x100000000 if self.seed >= 0x80000000 else self.seed
        return s * 2.3283064365386963e-10 + 0.5


def normalize(v):
    l2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2]
    if l2 == 0.0:          # RwV3dNormalize(0x520970)：长度 0 时结果为 0 向量
        return [0.0, 0.0, 0.0]
    k = 1.0 / math.sqrt(l2)   # 游戏用查表近似 1/sqrt（约 12 位精度），这里用精确值
    return [v[0] * k, v[1] * k, v[2] * k]


class Matrix:
    """RW 矩阵（行向量约定）：world = local.x*right + local.y*up + local.z*at + pos。"""

    def __init__(self, right=(1, 0, 0), up=(0, 1, 0), at=(0, 0, 1), pos=(0, 0, 0)):
        self.right, self.up, self.at, self.pos = list(right), list(up), list(at), list(pos)

    def vec(self, v):
        return [v[0] * self.right[i] + v[1] * self.up[i] + v[2] * self.at[i] for i in range(3)]

    def point(self, v):
        w = self.vec(v)
        return [w[i] + self.pos[i] for i in range(3)]


def trunc_byte(x):
    """fistp（控制字 |0xC00 截断）后取低 8 位，0x59F0E0/0x59E4F0 写 PTank 颜色。"""
    return int(math.trunc(x)) & 0xFF


class Particle:
    __slots__ = ("cur", "end", "inv", "vel", "pos", "size_start", "size_delta", "size",
                 "col_start", "col_delta", "color", "rot_start", "rot_delta", "rot",
                 "uv_start", "uv_delta", "uv", "mc", "st", "frame", "serial")


class EmitterSim:
    """一个 kind=0、simple_mode=0 的效果实例（游戏里所有 26 条都是这种）。"""

    def __init__(self, eff, uvl, ltm=None, seed=1):
        if eff["simple_mode"] != 0:
            raise NotImplementedError("simple_mode=1 未实现（数据中不存在）")
        if eff["shape"] not in (0, 2, 3):
            raise NotImplementedError("shape=%d 未实现（数据中不存在）" % eff["shape"])
        if eff["chain_effect"] != -1:
            raise NotImplementedError("链式子发射器未实现（数据中不存在）")
        self.e = eff
        self.ltm = ltm or Matrix()
        self.rng = Rng(seed)
        self.cur_time = 0.0      # std+0x08
        self.emit_time = 0.0     # std+0x40
        self.prev_emit = 0.0     # std+0x3C
        self.emit_on = True      # 发射器 flags & 8
        self.duration = eff["duration"]
        self.elapsed = 0.0
        self.particles = []
        self.serial = 0
        self.time = 0.0
        # uvlist -> 纹理坐标属性 / UV 步进表（BuildRender 0x468540 末段）
        rec = uvl[eff["uvlist_id"]]
        fr = rec["frames"]
        self.frames = fr
        self.texture = rec["textures"][fr[0]["texture_index"]]["path"] if rec["textures"] else None
        n_step = len(fr) - 1                       # local_28 = frame_count - 1
        self.tex_start = [fr[0]["u0"], fr[0]["v0"], fr[0]["u1"], fr[0]["v1"]]
        self.tex_end = [fr[n_step]["u0"], fr[n_step]["v0"], fr[n_step]["u1"], fr[n_step]["v1"]]
        self.step_items = []
        if n_step > 0:
            total = sum(fr[i]["duration_ms"] for i in range(n_step))
            acc = 0
            for i in range(n_step):
                self.step_items.append((f32(acc / total), [fr[i]["u0"], fr[i]["v0"]], [fr[i]["u1"], fr[i]["v1"]]))
                acc += fr[i]["duration_ms"]
        # 颜色（2=起止色，>=3 多段；起止色的 bias 只有多段时才写入）
        nk = eff["color_key_count"]
        keys = eff["color_keys_all"]
        self.has_color = nk >= 2
        if self.has_color:
            k0, kl = keys[0], keys[nk - 1]
            self.col_s = [float(c) for c in k0["rgba"]]
            self.col_e = [float(c) for c in kl["rgba"]]
            if nk >= 3:
                self.col_sb = [float(c) for c in k0["rgba_bias"]]
                self.col_eb = [float(c) for c in kl["rgba_bias"]]
            else:
                self.col_sb = [0.0] * 4
                self.col_eb = [0.0] * 4
            self.mc_items = [(keys[i]["time"], keys[i]["time_bias"], [float(c) for c in keys[i]["rgba"]],
                              [float(c) for c in keys[i]["rgba_bias"]]) for i in range(1, nk - 1)]
        else:
            self.mc_items = []

    # ---- 一帧 ----
    def step(self, dt):
        e = self.e
        dt = f32(dt)
        # 游戏层 Article_UpdateDuration 0x46B010
        if self.duration > 0.001:
            if self.elapsed <= self.duration:
                self.elapsed = f32(self.elapsed + dt)
            else:
                self.emit_on = False
        # BEGINUPDATE 0x5A0570
        self.cur_time = f32(self.cur_time + dt)
        # 粒子更新：多段颜色 -> UV 步进 -> 标准（死亡的粒子在标准更新里被丢弃）
        alive = []
        for p in self.particles:
            if not (p.cur < p.end):
                continue
            nt = dt + p.cur
            if p.mc is not None:                               # 0x5A3F10
                idx, times, starts, deltas = p.mc
                if idx < len(times) and times[idx] <= nt:
                    p.col_start, p.col_delta = list(starts[idx]), list(deltas[idx])
                    p.mc[0] = idx + 1
            if p.st is not None:                               # 0x5A3780
                idx, times, uv0s, uv1s = p.st
                if idx < len(times) and times[idx] <= nt:
                    p.uv_start = [uv0s[idx][0], uv0s[idx][1], uv1s[idx][0], uv1s[idx][1]]
                    p.st[0] = idx + 1
                    p.frame = idx
            # 标准 0x59E4F0
            t = nt * p.inv
            p.cur = f32(nt)
            if t > 1.0:
                t = 1.0
            F = e["force"]
            p.vel = [f32(dt * F[i] + p.vel[i]) for i in range(3)]
            p.pos = [f32(dt * p.vel[i] + p.pos[i]) for i in range(3)]
            if p.size_delta is not None:
                p.size = [f32(t * p.size_delta[i] + p.size_start[i]) for i in range(2)]
            if p.col_delta is not None:
                p.color = [trunc_byte(f32(t * p.col_delta[i] + p.col_start[i])) for i in range(4)]
            p.uv = [f32(t * p.uv_delta[i] + p.uv_start[i]) for i in range(4)]
            if p.rot_delta is not None:
                p.rot = f32(t * p.rot_delta + p.rot_start)
            alive.append(p)
        self.particles = alive
        # EMIT
        if self.emit_on and self.emit_time <= self.cur_time:
            new = self._emit_standard()
            if new:
                if e["shape"] == 2:
                    self._emit_sphere(new)
                if self.mc_items:
                    self._emit_multicolor(new)
                if self.step_items:
                    self._emit_uvstep(new)
        # ENDUPDATE 0x5A0630
        if self.emit_time <= self.cur_time:
            self.prev_emit = self.cur_time
            r = self.rng.next()
            self.emit_time = f32(r * e["emit_gap_bias"] + e["emit_gap"] + self.cur_time)
        # 游戏层 Article_UpdateEffect 0x46D470：超时后关闭发射
        if self.duration > 0.001 and self.duration < self.elapsed:
            self.emit_on = False
        self.time += dt

    def finished(self):
        """有限时长的效果：发射关闭且粒子数为 0 时游戏回收它（0x46D470）。"""
        return (not self.emit_on) and not self.particles

    # ---- 发射 ----
    def _emit_standard(self):                                  # 0x59F0E0
        e, rng = self.e, self.rng
        s0 = rng.seed
        r = rng.next()
        n = e["emit_num"] + int(math.trunc(f32(r * float(e["emit_num_bias"]))))
        room = e["max_prt"] - len(self.particles)              # PTank 容量 = max_prt
        if n > room:
            n = room
        if n <= 0:
            rng.seed = s0       # 不发射时种子不写回
            return []
        new = []
        ltm = self.ltm
        for _ in range(n):
            p = Particle()
            p.serial = self.serial
            self.serial += 1
            p.cur = 0.0
            p.end = f32(rng.next() * e["life_bias"] + e["life"])
            p.inv = f32(1.0 / p.end) if p.end != 0.0 else float("inf")
            d = [f32(rng.next() * e["dir_bias"][i] + e["dir"][i]) for i in range(3)]
            d = ltm.vec(normalize(d))
            sp = rng.next() * e["vel_bias"] + e["vel"]
            p.vel = [f32(sp * d[i]) for i in range(3)]
            lp = [f32(rng.next() * e["emt_size"][i] + 0.0) for i in range(3)]     # emtPos 未设置 = 0
            p.pos = [f32(c) for c in ltm.point(lp)]
            if e["has_size_prop"]:
                ss = [f32(rng.next() * e["size_start_bias"][i] + e["size_start"][i]) for i in range(2)]
                se = [f32(rng.next() * e["size_end_bias"][i] + e["size_end"][i]) for i in range(2)]
                p.size_start, p.size_delta = ss, [f32(se[i] - ss[i]) for i in range(2)]
                p.size = list(ss)
            else:
                p.size_start, p.size_delta = None, None
                p.size = [f32(e["size_start"][0]), f32(e["size_start"][1])]
            if self.has_color:
                cs = [f32(rng.next() * self.col_sb[i] + self.col_s[i]) for i in range(4)]
                ce = [f32(rng.next() * self.col_eb[i] + self.col_e[i]) for i in range(4)]
                p.col_start, p.col_delta = cs, [f32(ce[i] - cs[i]) for i in range(4)]
                p.color = [trunc_byte(c) for c in cs]
            else:
                p.col_start = p.col_delta = None
                p.color = [255, 255, 255, 255]   # 游戏里此时写入的是未初始化栈值（数据中不出现）
            if e["has_rot_prop"]:
                rs = f32(rng.next() * e["rot_start_bias"] + e["rot_start"])
                if rs > PI_F or rs < -PI_F:
                    if rs > PI_F:
                        rs = f32(rs - 6.2831855)
                    if rs < -PI_F:
                        rs = f32(rs + 6.2831855)
                re_ = f32(rng.next() * e["rot_end_bias"] + e["rot_end"])
                p.rot_start, p.rot_delta, p.rot = rs, f32(re_ - rs), rs
            else:
                p.rot_start = p.rot_delta = None
                p.rot = 0.0
            ts, te = self.tex_start, self.tex_end
            us = [f32(rng.next() * 0.0 + ts[i]) for i in range(4)]       # bias 全 0，但仍消耗随机数
            ue = [f32(rng.next() * 0.0 + te[i]) for i in range(4)]
            p.uv_start, p.uv_delta, p.uv = us, [f32(ue[i] - us[i]) for i in range(4)], list(us)
            p.mc = None
            p.st = None
            p.frame = 0
            new.append(p)
        self.particles.extend(new)
        return new

    def _emit_sphere(self, new):                               # 0x5A2D60
        e, rng, ltm = self.e, self.rng, self.ltm
        for p in new:
            phi = rng.next() * PI_SPHERE
            z = rng.next()
            s = math.sqrt(max(0.0, 1.0 - z * z))
            q = rng.next_signed01()
            R = q * e["sphere_radius_gap"] + e["sphere_radius"]
            lp = [R * math.cos(phi) * s, R * math.sin(phi) * s, R * z]
            p.pos = [f32(c) for c in ltm.point(lp)]
            if e["sphere_use_emission"] & 0xFF == 1:
                d = ltm.vec(normalize([f32(c) for c in lp]))
                sp = rng.next() * e["vel_bias"] + e["vel"]
                p.vel = [f32(sp * d[i]) for i in range(3)]

    def _emit_multicolor(self, new):                           # 0x5A4140
        rng = self.rng
        rng.next()                       # 函数入口先推进一次（不使用）
        items = self.mc_items
        n = len(items)
        for p in new:
            t_prev = rng.next() * items[0][1] + items[0][0]
            c_prev = [rng.next() * items[0][3][i] + items[0][2][i] for i in range(4)]
            t0, c0 = t_prev, c_prev
            times, starts, deltas = [], [], []
            for k in range(1, n):
                tk = rng.next() * items[k][1] + items[k][0]
                times.append(f32(t_prev * p.end))
                inv = 1.0 / (tk - t_prev) if tk - t_prev > 0.0 else 0.0
                ck = [rng.next() * items[k][3][i] + items[k][2][i] for i in range(4)]
                slope = [(ck[i] - c_prev[i]) * inv for i in range(4)]
                starts.append([f32(c_prev[i] - t_prev * slope[i]) for i in range(4)])
                deltas.append([f32(s) for s in slope])
                t_prev, c_prev = tk, ck
            times.append(f32(t_prev * p.end))
            inv = 1.0 / (1.0 - t_prev) if 1.0 - t_prev > 0.0 else 0.0
            endc = [p.col_start[i] + p.col_delta[i] for i in range(4)]
            slope = [(endc[i] - c_prev[i]) * inv for i in range(4)]
            starts.append([f32(c_prev[i] - t_prev * slope[i]) for i in range(4)])
            deltas.append([f32(s) for s in slope])
            # 第一段：从起始色到第一个中间关键帧（起始色不变）
            p.col_delta = [f32((c0[i] - p.col_start[i]) * (1.0 / t0)) for i in range(4)]
            p.mc = [0, times, starts, deltas]

    def _emit_uvstep(self, new):                               # 0x5A3870
        rng = self.rng
        rng.next()                       # 函数入口先推进一次（不使用）
        items = self.step_items
        for p in new:
            times, uv0s, uv1s = [], [], []
            for (t, uv0, uv1) in items:
                tt = rng.next() * 0.0 + t
                a = [rng.next() * 0.0 + uv0[0], rng.next() * 0.0 + uv0[1]]
                b = [rng.next() * 0.0 + uv1[0], rng.next() * 0.0 + uv1[1]]
                times.append(f32(tt * p.end))
                uv0s.append([f32(x) for x in a])
                uv1s.append([f32(x) for x in b])
            p.st = [0, times, uv0s, uv1s]
            p.uv_delta = [0.0, 0.0, 0.0, 0.0]

    # ---- 输出 ----
    def snapshot(self):
        out = []
        for p in self.particles:
            out.append({
                "serial": p.serial, "age": p.cur, "life": p.end,
                "pos": p.pos, "vel": p.vel, "size": p.size, "color": p.color,
                "uv": p.uv, "frame": p.frame, "rot": p.rot,
            })
        return out


# ---------------------------------------------------------------------------
# 预览（正交侧视：屏幕右 = 世界 +X，屏幕上 = 世界 +Y，视线沿 -Z）
# ---------------------------------------------------------------------------
def load_texture_rgba(name):
    import io
    import numpy as np
    from PIL import Image
    import rm8pak
    f, ents, _ = rm8pak.read_index(TEX_DAT)
    want = ("prt\\" + name).lower()
    for ent in ents:
        if ent["name"].lower() == want:
            f.seek(ent["off"])
            img = Image.open(io.BytesIO(f.read(ent["size"]))).convert("RGBA")
            return np.asarray(img).astype(np.float32) / 255.0
    raise FileNotFoundError(want)


def render_ortho(sim, tex, scale, center, wh, blend):
    import numpy as np
    W, H = wh
    img = np.zeros((H, W, 3), np.float32)
    th, tw = tex.shape[:2]
    src, dst = blend
    for p in sim.particles:
        cx = (p.pos[0] - center[0]) * scale + W / 2
        cy = H / 2 - (p.pos[1] - center[1]) * scale
        c, s = math.cos(p.rot), math.sin(p.rot)
        # 0x5953F0：X' = cosθ·X − sinθ·Y，Y' = cosθ·Y + sinθ·X（X=屏幕右, Y=屏幕上）
        # 即在 (右,上) 坐标里 X'=(c,−s)、Y'=(s,c)：θ>0 时在屏幕上顺时针转
        ax = np.array([c, -s]) * p.size[0] * scale
        ay = np.array([s, c]) * p.size[1] * scale
        ext = (abs(ax[0]) + abs(ay[0])) / 2 + 1, (abs(ax[1]) + abs(ay[1])) / 2 + 1
        x0, x1 = int(max(0, cx - ext[0])), int(min(W, cx + ext[0] + 1))
        y0, y1 = int(max(0, cy - ext[1])), int(min(H, cy + ext[1] + 1))
        if x0 >= x1 or y0 >= y1:
            continue
        ys, xs = np.mgrid[y0:y1, x0:x1]
        dx = xs + 0.5 - cx
        dy = cy - (ys + 0.5)
        m = np.array([[ax[0], ay[0]], [ax[1], ay[1]]])
        if abs(np.linalg.det(m)) < 1e-9:
            continue
        inv = np.linalg.inv(m)
        a = inv[0, 0] * dx + inv[0, 1] * dy
        b = inv[1, 0] * dx + inv[1, 1] * dy
        inside = (np.abs(a) <= 0.5) & (np.abs(b) <= 0.5)
        if not inside.any():
            continue
        u0, v0, u1, v1 = p.uv
        u = u0 + (a + 0.5) * (u1 - u0)                    # 左下顶点 (u0,v1)，右上 (u1,v0)
        v = v1 + (b + 0.5) * (v0 - v1)
        tx = np.clip((u * tw).astype(int), 0, tw - 1)
        ty = np.clip((v * th).astype(int), 0, th - 1)
        t = tex[ty, tx]
        vc = np.array(p.color, np.float32) / 255.0
        rgb = t[..., :3] * vc[:3]
        alpha = (t[..., 3] * vc[3])[..., None]
        d = img[y0:y1, x0:x1]
        fs = {2: 1.0, 5: alpha, 3: rgb, 1: 0.0}.get(src, alpha)
        fd = {2: 1.0, 6: 1.0 - alpha, 4: 1.0 - rgb, 1: 0.0}.get(dst, 1.0)
        res = rgb * fs + d * fd
        img[y0:y1, x0:x1] = np.where(inside[..., None], res, d)
    return img


def cmd_preview(args, effects, uvl):
    import numpy as np
    from PIL import Image, ImageDraw
    e = effects[args.id]
    sim = EmitterSim(e, uvl)
    tex = load_texture_rgba(sim.texture)
    times = [float(x) for x in args.times.split(",")]
    W, H, scale = args.width, args.height, args.scale
    center = [0.0, args.center_y, 0.0]
    panels = []
    if args.prewarm:
        sim.step(args.prewarm)
    for T in times:
        while sim.time + 1e-9 < T:
            sim.step(args.dt)
        img = render_ortho(sim, tex, scale, center, (W, H), (e["src_blend"], e["dst_blend"]))
        im = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
        dr = ImageDraw.Draw(im)
        # 发射盒（emt_size 是半边长）与原点
        hx, hy = e["emt_size"][0] * scale, e["emt_size"][1] * scale
        ox, oy = W / 2, H / 2 + center[1] * scale
        dr.rectangle([ox - hx, oy - hy - 1, ox + hx, oy + hy + 1], outline=(0, 160, 0))
        dr.text((4, 4), "eff %d  t=%.2fs  n=%d" % (args.id, sim.time, len(sim.particles)), fill=(255, 255, 0))
        panels.append(im)
    sheet = Image.new("RGB", (W * len(panels), H), (0, 0, 0))
    for i, im in enumerate(panels):
        sheet.paste(im, (i * W, 0))
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, "eff%02d_preview.png" % args.id)
    sheet.save(path)
    print(path)


# ---------------------------------------------------------------------------
def fmt_v(v):
    if isinstance(v, float):
        return "%g" % v
    if isinstance(v, list):
        return "(" + ",".join(fmt_v(x) for x in v) + ")"
    return str(v)


def cmd_dump(args, effects, uvl):
    for eid in sorted(effects):
        if args.id is not None and eid != args.id:
            continue
        e = effects[eid]
        if args.json:
            d = dict(e)
            d.pop("color_keys_all")
            print(json.dumps(d, ensure_ascii=False))
            continue
        rec = uvl[e["uvlist_id"]]
        print("=== effect %d  (uvlist %d: %d 帧, 贴图 %s)" % (eid, e["uvlist_id"], rec["frame_count"],
                                                         [t["path"] for t in rec["textures"]]))
        for off, ty, name, ref in PAYLOAD_FIELDS:
            v = e[name]
            extra = ""
            if name in ("src_blend", "dst_blend"):
                extra = " (%s)" % BLEND_NAMES.get(v, "?")
            if name == "shape":
                extra = " (%s)" % SHAPE_NAMES.get(v, "?")
            print("  +0x%03X %-22s = %s%s" % (off, name, fmt_v(v), extra))
        for k, key in enumerate(e["color_keys"]):
            print("  +0x%03X key%d time=%g bias=%g rgba=%s rgba_bias=%s" % (
                COLOR_KEYS_OFF + 16 * k, k, key["time"], key["time_bias"], key["rgba"], key["rgba_bias"]))
        print("  派生: has_size_prop=%s has_rot_prop=%s" % (e["has_size_prop"], e["has_rot_prop"]))


def cmd_table(args, effects, uvl):
    print("| id | 形状 | uvl(帧) | 贴图 | 混合 src/dst | 时长 | max | 每次发射 | 间隔 s | 寿命 s | 速度 | 方向 ± 偏差 | 力 | 尺寸 起→止 | 旋转 起→止 | 颜色键 |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for eid in sorted(effects):
        e = effects[eid]
        rec = uvl[e["uvlist_id"]]
        tex = rec["textures"][rec["frames"][0]["texture_index"]]["path"]
        shape = SHAPE_NAMES[e["shape"]]
        if e["shape"] == 2:
            shape += " r=%g+%g" % (e["sphere_radius"], e["sphere_radius_gap"])
        size = "%s" % fmt_v(e["size_start"])
        if e["has_size_prop"]:
            size += "→%s" % fmt_v(e["size_end"])
        rot = "0"
        if e["has_rot_prop"]:
            rot = "%g±%g→%g±%g" % (e["rot_start"], e["rot_start_bias"], e["rot_end"], e["rot_end_bias"])
        keys = ";".join("%g:%s" % (k["time"], ",".join(str(c) for c in k["rgba"])) for k in e["color_keys"])
        print("| %d | %s | %d(%d) | %s | %s/%s | %s | %d | %d±%d | %g±%g | %g±%g | %g±%g | %s±%s | %s | %s | %s | %s |" % (
            eid, shape, e["uvlist_id"], rec["frame_count"], tex,
            BLEND_NAMES.get(e["src_blend"]), BLEND_NAMES.get(e["dst_blend"]),
            ("%g" % e["duration"]) if e["duration"] > 0.001 else "∞",
            e["max_prt"], e["emit_num"], e["emit_num_bias"], e["emit_gap"], e["emit_gap_bias"],
            e["life"], e["life_bias"], e["vel"], e["vel_bias"], fmt_v(e["dir"]), fmt_v(e["dir_bias"]),
            fmt_v(e["force"]), size, rot, keys))


def cmd_sim(args, effects, uvl):
    e = effects[args.id]
    ltm = None
    if args.matrix:
        m = args.matrix
        ltm = Matrix(m[0:3], m[3:6], m[6:9], m[9:12])
    sim = EmitterSim(e, uvl, ltm=ltm, seed=args.seed)
    if args.dts:
        dts = [float(x) for x in open(args.dts).read().split()]
    else:
        n = args.frames if args.frames else int(math.ceil(args.until / args.dt))
        dts = [args.dt] * n
    if args.prewarm:
        dts = [args.prewarm] + dts
    for i, dt in enumerate(dts):
        sim.step(dt)
        if (i + 1) % args.every == 0 or i == len(dts) - 1:
            print(json.dumps({"frame": i + 1, "t": round(sim.time, 6), "seed": sim.rng.seed,
                              "emit_time": sim.emit_time, "n": len(sim.particles),
                              "particles": sim.snapshot()}, ensure_ascii=False))


def cmd_selfcheck(args, effects, uvl):
    """机器验收：字段覆盖 0x160 字节且不重叠；26 条全部解码；未读字段的取值；模拟器可跑且确定。"""
    size = {"f": 4, "i": 4, "I": 4, "H": 2, "B4": 4, "3f": 12, "2f": 8}
    cover = [0] * 0x160
    for off, ty, name, _ in PAYLOAD_FIELDS:
        for b in range(off, off + size[ty]):
            cover[b] += 1
    for b in range(COLOR_KEYS_OFF, COLOR_KEYS_OFF + 16 * MAX_KEYS):
        cover[b] += 1
    bad = [i for i, c in enumerate(cover) if c != 1]
    print("payload 覆盖：%s" % ("每字节恰好 1 个字段" if not bad else "错误 %s" % bad[:10]))
    ok = not bad
    print("效果条数：%d（id %d..%d）" % (len(effects), min(effects), max(effects)))
    ok &= sorted(effects) == list(range(26))
    stats = {}
    for name in ("shape", "simple_mode", "vertex_alpha", "src_blend", "dst_blend", "pad_06", "unk_74",
                 "chain_effect", "color_key_count", "circle_use_emission", "sphere_use_emission"):
        stats[name] = sorted(set(e[name] for e in effects.values()))
    for k, v in stats.items():
        print("  %-20s 取值 %s" % (k, v))
    tails = set()
    for e in effects.values():
        for key in e["color_keys_all"][e["color_key_count"]:]:
            tails.add((key["time"], key["time_bias"], tuple(key["rgba"]), tuple(key["rgba_bias"])))
    print("  未用关键帧槽的内容：%s" % sorted(tails))
    for eid in sorted(effects):
        outs = []
        for _ in range(2):
            sim = EmitterSim(effects[eid], uvl)
            for _ in range(600):
                sim.step(1.0 / 60)
            snap = sim.snapshot()
            for p in snap:
                assert all(math.isfinite(x) for x in p["pos"]), (eid, p)
                assert all(0 <= c <= 255 for c in p["color"]), (eid, p)
            outs.append(json.dumps(snap, sort_keys=True))
        if outs[0] != outs[1]:
            print("  效果 %d 模拟不确定！" % eid)
            ok = False
    print("模拟器：26 个效果各跑 10 秒（dt=1/60），两次结果逐位一致" if ok else "模拟器检查失败")
    print("SELFCHECK %s" % ("PASS" if ok else "FAIL"))
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("dump")
    d.add_argument("--json", action="store_true")
    d.add_argument("--id", type=int)
    sub.add_parser("table")
    sub.add_parser("selfcheck")
    s = sub.add_parser("sim")
    s.add_argument("id", type=int)
    s.add_argument("--dt", type=float, default=1.0 / 60)
    s.add_argument("--frames", type=int)
    s.add_argument("--until", type=float, default=5.0)
    s.add_argument("--every", type=int, default=1)
    s.add_argument("--prewarm", type=float, default=0.0, help="先走一帧该步长（地图加载时游戏走 1.0）")
    s.add_argument("--seed", type=int, default=1)
    s.add_argument("--dts", help="每行/空白分隔的 dt 序列文件，覆盖 --dt/--frames")
    s.add_argument("--matrix", type=float, nargs=12, help="发射器 LTM：right up at pos 共 12 个数")
    p = sub.add_parser("preview")
    p.add_argument("id", type=int)
    p.add_argument("--out", required=True)
    p.add_argument("--times", default="0.5,1,2,3,6")
    p.add_argument("--dt", type=float, default=1.0 / 60)
    p.add_argument("--prewarm", type=float, default=0.0)
    p.add_argument("--width", type=int, default=240)
    p.add_argument("--height", type=int, default=320)
    p.add_argument("--scale", type=float, default=5.0, help="像素/世界单位")
    p.add_argument("--center-y", type=float, default=20.0)
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    effects, uvl, _ = load_tables()
    {"dump": cmd_dump, "table": cmd_table, "sim": cmd_sim, "preview": cmd_preview,
     "selfcheck": cmd_selfcheck}[args.cmd](args, effects, uvl)


if __name__ == "__main__":
    main()
