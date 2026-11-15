#!/usr/bin/env python
"""
Parser for RichMan8 particle configuration files (Config/prt/*.tb).

  PrtEffTable.tb  loader FUN_0046df60  (called from the particle system init FUN_00404400)
  uvlisttable.tb  loader FUN_00467650  (wrappers: FUN_00467ac0 -> FUN_0046e2b0 <- FUN_00404400)

Every field below is annotated with the loader function and the decompiled
variable that reads it.  The parsers walk the file exactly the way the game
loader does; "remaining_bytes" is len(file) - (position reached by the walk),
never derived from sizes stored in the file.  Both files parse to 0 remaining
bytes (hard-checked: ValueError otherwise).

Usage:
  python prtfmt.py <file.tb>          summary JSON
  python prtfmt.py <file.tb> --full   JSON including every record
"""

import struct
import sys
import json
from pathlib import Path


class Reader:
    """Sequential little-endian reader that tracks how far the walk got."""

    def __init__(self, data, pos=0):
        self.data = data
        self.pos = pos

    def _take(self, n):
        if n < 0 or self.pos + n > len(self.data):
            raise ValueError("read of %d bytes at 0x%x overruns file (size 0x%x)"
                             % (n, self.pos, len(self.data)))
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b

    def raw(self, n):
        return self._take(n)

    def u16(self):
        return struct.unpack("<H", self._take(2))[0]

    def u32(self):
        return struct.unpack("<I", self._take(4))[0]

    def f32(self):
        return struct.unpack("<f", self._take(4))[0]


def _text(b):
    return b.split(b"\0", 1)[0].decode("gbk", errors="replace")


# ---------------------------------------------------------------------------
# PrtEffTable.tb   -- loader FUN_0046df60 (this = particle-effect table object)
# ---------------------------------------------------------------------------
#
# File layout (as walked by FUN_0046df60):
#   +0x00  char[4]  magic          memcpy(local_2c, file, 0xc); strcmp(local_2c, PTR_PTR_005e1524) -> "PRT\0"
#   +0x04  u16      record_count   local_28; loop bound `local_48 < local_28`
#   +0x06  u16      max_id         local_26; stored at this+4, id->record pointer array is operator_new(local_26*4+4)
#   +0x08  u32      data_size      local_24; index starts at file+0xc+data_size (local_30/local_1c)
#   +0x0c  record_count x record   walked sequentially from file+0xc (local_20 / local_40)
#            u32 kind                   memcpy(obj+4, p, 4)            (obj = operator_new(0x1c))
#            kind==0: 0x160 bytes       operator_new(0x160); memcpy(obj+8, p+4, 0x160); next = p+0x164
#            kind==1: 0xd8  bytes       operator_new(0xd8);  memcpy(obj+8, p+4, 0xd8);  next = p+0xdc
#            other kind: nothing copied, next = p+4  (loader falls through with pvVar5 = p+4)
#   file+0xc+data_size: record_count x 12-byte index entry
#            u32 (+0)  not read by loader
#            u32 (+4)  not read by loader
#            u16 (+8)  id        `*(ushort*)(local_1c + 8 + i*0xc)`; obj+0 = id; table[id] = obj
#            u16 (+10) not read by loader
#
# The loader only memcpy's the payload, so the internal layout of the 0x160
# (or 0xd8) payload belongs to the particle code that consumes it, not to the
# file loader.  It is exposed here as little-endian 32-bit words.

PRT_KIND_PAYLOAD = {0: 0x160, 1: 0xd8}   # FUN_0046df60: operator_new(0x160) / operator_new(0xd8)


def parse_prt_eff_table(data):
    r = Reader(data)
    magic = r.raw(4)                       # FUN_0046df60 memcpy(local_2c, file, 0xc)
    if magic != b"PRT\x00":                # strcmp(local_2c, "PRT")
        raise ValueError("Invalid magic for PrtEffTable.tb")
    record_count = r.u16()                 # local_28 (offset 4)
    max_id = r.u16()                       # local_26 (offset 6)
    data_size = r.u32()                    # local_24 (offset 8)
    assert r.pos == 0xc

    index_start = 0xc + data_size          # local_30 = local_20 + local_24
    result = {
        "type": "PrtEffTable",
        "magic": "PRT",
        "record_count": record_count,
        "max_id": max_id,
        "data_size": data_size,
        "index_offset": index_start,
        "records": [],
        "index": [],
    }

    for i in range(record_count):          # for (local_48 = 0; local_48 < local_28; ...)
        start = r.pos
        kind = r.u32()                     # memcpy(obj+4, local_40, 4)
        size = PRT_KIND_PAYLOAD.get(kind, 0)
        payload = r.raw(size)              # memcpy(obj+8, local_40+4, 0x160 / 0xd8)
        words = list(struct.unpack("<%dI" % (size // 4), payload))
        result["records"].append({
            "seq": i, "offset": start, "kind": kind,
            "payload_size": size, "payload_u32": words,
        })

    if r.pos != index_start:
        raise ValueError("record walk ended at 0x%x but header data_size puts the index at 0x%x"
                         % (r.pos, index_start))

    for i in range(record_count):          # reads *(ushort*)(index + 8 + i*0xc) for i < local_28
        off = r.u32()                      # +0: not read by FUN_0046df60
        size = r.u32()                     # +4: not read by FUN_0046df60
        rid = r.u16()                      # +8: id, FUN_0046df60
        pad = r.u16()                      # +10: not read by FUN_0046df60
        if rid > max_id:
            raise ValueError("id %d > max_id %d" % (rid, max_id))
        rec = result["records"][i]
        result["index"].append({
            "seq": i, "id": rid,
            "stored_offset": off, "stored_size": size, "stored_pad": pad,
            # cross-check only (the game ignores these two fields)
            "matches_walk": off == rec["offset"] and size == 4 + rec["payload_size"],
        })
        rec["id"] = rid

    result["remaining_bytes"] = len(data) - r.pos
    if result["remaining_bytes"] != 0:
        raise ValueError("%d unparsed bytes" % result["remaining_bytes"])
    return result


# ---------------------------------------------------------------------------
# uvlisttable.tb   -- loader FUN_00467650 (this = UV-list table, ushort[2] + ptr)
# ---------------------------------------------------------------------------
#
# File layout (as walked by FUN_00467650):
#   +0x00  char[4]  magic        memcpy(local_24, file, 0x10); strcmp(local_24, "UVL")
#   +0x04  u16      record_count local_20 -> this+2 (in_ECX[1]); loop `local_48 < in_ECX[1]`
#   +0x06  u16      max_id       local_1e -> this+0 (*in_ECX); pointer array = operator_new(max_id*4+4)
#   +0x08  char[4]  version      local_1c..local_19 compared byte-wise with sprintf("%s", DAT_005e147c) = "1.01"
#   +0x0c  u32      data_size    local_18; index start = file + 0x10 + data_size (local_28/local_10)
#   +0x10  record_count x record (walked sequentially, local_44 cursor)
#   file+0x10+data_size: record_count x 12-byte index entry
#
# record (obj = operator_new(0x24), stored at table[id]):
#   header, memcpy(&local_60, p, 0x18), 6 x u32:
#     +0x00 u32  loop_start_frame  -> obj[2]   frame index restarted from at the end (FUN_00468090: local_20 = obj[2])
#     +0x04 u32  loop_count        -> obj[3]   0 = repeat forever, N = play N times (FUN_00468090: obj[3]-1 / ==0 test)
#     +0x08 f32  width             -> obj[0]   FUN_00467d40: param_2[10] = obj[0]
#     +0x0c f32  height            -> obj[1]   FUN_00467d40: param_2[0xb] = obj[1]
#     +0x10 u32  frame_count       -> obj[4]
#     +0x14 u32  texture_count     -> obj[6]
#   frame_count x frame (0x18 bytes each, memcpy'd to obj[5] = operator_new(frame_count*0x18)):
#     +0x00 f32 u0, +0x04 f32 u1, +0x08 f32 v0, +0x0c f32 v1
#                 FUN_00467d40 / FUN_00468090 copy dwords 0..3 into the sprite state (param_2[0..3])
#     +0x10 u32 duration_ms   FUN_00468090: `frame[+0x10] < (uint)(timeGetTime() - frame_start)` -> advance
#                 (timeGetTime is passed in by FUN_00466c10)
#     +0x14 u32 texture_index  index into the record's texture list: FUN_00467d40 checks `< obj[6]`
#                 and uses `*(obj[7] + frame[+0x14]*0xc)` as the texture of the frame
#   texture_count x texture entry (obj[7] = operator_new(texture_count*0xc), runtime slot 0 set to 0):
#     u32 use_mask_name   memcpy(obj[7]+4+i*0xc, p, 4): FUN_00467d40: 0 -> FUN_0051bf10(path, 0),
#                         non-zero -> FUN_0051bf10(path, path)  (second argument = mask name, same as path)
#     u32 path_len        local_6c: byte count, includes the terminating NUL
#     char[path_len]      operator_new(local_6c); memcpy: texture path passed to FUN_0051bf10
#   obj[8] = 0 is a runtime reference counter (not in file).
#
# index entry (12 bytes):
#   u32 (+0)  not read by loader  (observed: absolute file offset of the record)
#   u32 (+4)  not read by loader  (observed: record byte size)
#   u16 (+8)  id    `*(ushort*)(local_10 + 8 + i*0xc)`: table[id] = record number i
#   u16 (+10) not read by loader  (observed: 0)
# Records are paired with index entries by position (i-th record <-> i-th entry).


def parse_uvlist_table(data):
    r = Reader(data)
    magic = r.raw(4)                       # strcmp(local_24, "UVL")
    if magic != b"UVL\x00":
        raise ValueError("Invalid magic for uvlisttable.tb")
    record_count = r.u16()                 # local_20 (offset 4)
    max_id = r.u16()                       # local_1e (offset 6)
    version = r.raw(4)                     # local_1c..local_19 (offset 8), must equal "1.01"
    if version != b"1.01":
        raise ValueError("Unsupported version %r" % version)
    data_size = r.u32()                    # local_18 (offset 0xc)
    assert r.pos == 0x10
    index_start = 0x10 + data_size         # local_28 = local_14 + local_18

    result = {
        "type": "uvlisttable",
        "magic": "UVL",
        "record_count": record_count,
        "max_id": max_id,
        "version": version.decode("ascii"),
        "data_size": data_size,
        "index_offset": index_start,
        "records": [],
        "index": [],
    }

    for i in range(record_count):          # for (local_48 = 0; local_48 < in_ECX[1]; ...)
        start = r.pos
        loop_start_frame = r.u32()         # local_60 -> obj[2]
        loop_count = r.u32()               # local_5c -> obj[3]
        width = r.f32()                    # local_58 -> obj[0]
        height = r.f32()                   # local_54 -> obj[1]
        frame_count = r.u32()              # local_50 -> obj[4]
        texture_count = r.u32()            # local_4c -> obj[6]

        frames = []
        for _ in range(frame_count):       # memcpy(obj[5], p, frame_count*0x18)
            u0, u1, v0, v1 = struct.unpack("<4f", r.raw(16))
            duration_ms = r.u32()          # FUN_00468090: frame[+0x10]
            texture_index = r.u32()        # FUN_00467d40: frame[+0x14]
            frames.append({
                "u0": u0, "u1": u1, "v0": v0, "v1": v1,
                "duration_ms": duration_ms, "texture_index": texture_index,
            })

        textures = []
        for _ in range(texture_count):     # for (local_68 = 0; local_68 < obj[6]; ...)
            use_mask_name = r.u32()        # memcpy(obj[7]+4+i*0xc, p, 4)
            path_len = r.u32()             # local_6c
            path = r.raw(path_len)         # memcpy(operator_new(local_6c), p, local_6c)
            textures.append({
                "use_mask_name": use_mask_name,
                "path_len": path_len,
                "path": _text(path),
                "nul_terminated": path.endswith(b"\0"),
            })

        result["records"].append({
            "seq": i, "offset": start, "size": r.pos - start,
            "loop_start_frame": loop_start_frame, "loop_count": loop_count,
            "width": width, "height": height,
            "frame_count": frame_count, "texture_count": texture_count,
            "frames": frames, "textures": textures,
        })

    if r.pos != index_start:
        raise ValueError("record walk ended at 0x%x but header data_size puts the index at 0x%x"
                         % (r.pos, index_start))

    seen = set()
    for i in range(record_count):          # reads *(ushort*)(local_10 + 8 + i*0xc)
        off = r.u32()                      # +0: not read by FUN_00467650
        size = r.u32()                     # +4: not read by FUN_00467650
        rid = r.u16()                      # +8: id, FUN_00467650
        pad = r.u16()                      # +10: not read by FUN_00467650
        if rid > max_id:
            raise ValueError("id %d > max_id %d" % (rid, max_id))
        if rid in seen:
            raise ValueError("duplicate id %d" % rid)
        seen.add(rid)
        rec = result["records"][i]
        rec["id"] = rid
        result["index"].append({
            "seq": i, "id": rid,
            "stored_offset": off, "stored_size": size, "stored_pad": pad,
            # cross-check only (the game ignores these two fields)
            "matches_walk": off == rec["offset"] and size == rec["size"],
        })

    result["remaining_bytes"] = len(data) - r.pos
    if result["remaining_bytes"] != 0:
        raise ValueError("%d unparsed bytes" % result["remaining_bytes"])
    return result


def summarize(res):
    s = {k: v for k, v in res.items() if k not in ("records", "index")}
    s["records_parsed"] = len(res["records"])
    s["index_entries_parsed"] = len(res["index"])
    s["index_matches_walk"] = all(e["matches_walk"] for e in res["index"])
    if res["type"] == "uvlisttable":
        s["total_frames"] = sum(x["frame_count"] for x in res["records"])
        s["total_textures"] = sum(x["texture_count"] for x in res["records"])
        s["ids_unique_and_dense"] = sorted(e["id"] for e in res["index"]) == list(range(res["max_id"] + 1))
    else:
        s["kinds"] = sorted({x["kind"] for x in res["records"]})
    s["first_records"] = res["records"][:2]
    return s


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    full = "--full" in sys.argv
    if not args:
        print("Usage: python prtfmt.py <file.tb> [--full]")
        sys.exit(1)

    filepath = Path(args[0])
    if not filepath.exists():
        print("Error: File not found: %s" % filepath)
        sys.exit(1)

    data = filepath.read_bytes()
    magic = data[0:4]
    if magic == b"PRT\x00":
        result = parse_prt_eff_table(data)
    elif magic == b"UVL\x00":
        result = parse_uvlist_table(data)
    else:
        print("Error: Unknown file format. Magic: %r" % magic)
        sys.exit(1)

    print(json.dumps(result if full else summarize(result), indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
