"""RichMan8 resource archive tool (clump%04d.dat / tex%04d.dat / snd%04d.dat / mus%04d.dat)
Format A (clump/tex):  u32 tag, u32 0, u32 filesize, u32 count, u32 unk,
    count * {u32 namelen, u32 offset, u32 size, u32 flag}
    at offset: name[namelen] (ascii path, no NUL) then data[size]
Format B (snd/mus):  char[4] 'snd%'|'mus%', u32 unk, u32 count, count*{u32 id, u32 offset, u32 size, u32 flag}
usage: rm8pak.py list <dat> | rm8pak.py extract <dat> <outdir> [filter-substring]
"""
import sys, os, struct
def read_index(path):
    f = open(path, 'rb'); h = f.read(20)
    if h[:3] in (b'snd', b'mus'):
        _, unk, cnt = struct.unpack_from('<4sII', h)
        f.seek(12); raw = f.read(cnt * 16); ents = []
        for i in range(cnt):
            id_, off, size, flag = struct.unpack_from('<IIII', raw, i * 16)
            if size == 0 and off == 0: continue
            f.seek(off); magic = f.read(4)
            ext = {b'RIFF': '.wav', b'OggS': '.ogg', b'ID3\x03': '.mp3', b'ID3\x04': '.mp3'}.get(magic, '.mp3' if magic[:2] in (b'\xff\xfb', b'\xff\xf3', b'\xff\xfa') else '.bin')
            ents.append(dict(name='%s%05d%s' % (h[:3].decode(), id_, ext), off=off, size=size, flag=flag, id=id_))
        return f, ents, dict(kind='B', unk=unk, count=cnt)
    tag, z, fsize, cnt, unk = struct.unpack('<IIIII', h)
    raw = f.read(cnt * 16); ents = []
    for i in range(cnt):
        nl, off, size, flag = struct.unpack_from('<IIII', raw, i * 16)
        f.seek(off); name = f.read(nl).decode('cp950', errors='replace')
        ents.append(dict(name=name, off=off + nl, size=size, flag=flag))
    return f, ents, dict(kind='A', tag=hex(tag), fsize=fsize, count=cnt, unk=unk)
if __name__ == '__main__':
    cmd, path = sys.argv[1], sys.argv[2]
    f, ents, hdr = read_index(path)
    if cmd == 'list':
        print('#', hdr)
        for e in ents: print('%10d %10d %d %s' % (e['off'], e['size'], e['flag'], e['name']))
    elif cmd == 'extract':
        out = sys.argv[3]; flt = sys.argv[4] if len(sys.argv) > 4 else ''
        n = 0
        for e in ents:
            if flt and flt.lower() not in e['name'].lower(): continue
            p = os.path.join(out, e['name'].replace(chr(92), os.sep)); os.makedirs(os.path.dirname(p) or '.', exist_ok=True)
            f.seek(e['off']); open(p, 'wb').write(f.read(e['size'])); n += 1
        print('extracted', n)

def pack_a(src_dat, srcdir, out_dat):
    """Rebuild a format-A archive using src_dat's entry order and header fields;
    file contents are taken from srcdir if present, otherwise copied from src_dat."""
    f, ents, hdr = read_index(src_dat)
    f.seek(0); h = f.read(20); tag, z, _, cnt, unk = struct.unpack('<IIIII', h)
    blobs = []
    for e in ents:
        p = os.path.join(srcdir, e['name'].replace(chr(92), os.sep)) if srcdir else ''
        if p and os.path.isfile(p): data = open(p, 'rb').read()
        else: f.seek(e['off']); data = f.read(e['size'])
        blobs.append((e['name'].encode('cp950'), data))
    off = 20 + 16 * len(blobs); idx = []; body = []
    for name, data in blobs:
        idx.append(struct.pack('<IIII', len(name), off, len(data), 0)); body += [name, data]; off += len(name) + len(data)
    with open(out_dat, 'wb') as o:
        o.write(struct.pack('<IIIII', tag, z, off, len(blobs), unk)); o.write(b''.join(idx))
        for b in body: o.write(b)
    print('packed', len(blobs), 'entries ->', out_dat, off, 'bytes')

def pack_b(src_dat, srcdir, out_dat):
    f, ents, hdr = read_index(src_dat)
    f.seek(0); h = f.read(12); magic, unk, cnt = struct.unpack('<4sII', h)
    f.seek(12); raw = f.read(cnt * 16)
    rows = [struct.unpack_from('<IIII', raw, i * 16) for i in range(cnt)]
    byid = {e['id']: e for e in ents}
    off = 12 + 16 * cnt; idx = []; body = []
    for id_, o0, s0, flag in rows:
        if o0 == 0 and s0 == 0: idx.append(struct.pack('<IIII', id_, 0, 0, flag)); continue
        e = byid[id_]; p = os.path.join(srcdir, e['name']) if srcdir else ''
        if p and os.path.isfile(p): data = open(p, 'rb').read()
        else: f.seek(e['off']); data = f.read(e['size'])
        idx.append(struct.pack('<IIII', id_, off, len(data), flag)); body.append(data); off += len(data)
    with open(out_dat, 'wb') as o:
        o.write(struct.pack('<4sII', magic, unk, cnt)); o.write(b''.join(idx))
        for b in body: o.write(b)
    print('packed', cnt, 'entries ->', out_dat, off, 'bytes')

if __name__ == '__main__' and sys.argv[1] == 'pack':
    # rm8pak.py pack <original.dat> <replacement_dir|-> <out.dat>
    src, d, out = sys.argv[2], sys.argv[3], sys.argv[4]
    d = '' if d == '-' else d
    with open(src, 'rb') as t: m = t.read(3)
    (pack_b if m in (b'snd', b'mus') else pack_a)(src, d, out)
