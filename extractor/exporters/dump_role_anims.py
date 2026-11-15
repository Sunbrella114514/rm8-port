"""Dump role.bin animation lists for the art ids placed on a map (inspection helper)."""
import struct, sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_scene as es

def records(game):
    d = open(os.path.join(game, 'Config', 'newOut', 'role.bin'), 'rb').read()
    ver, n = struct.unpack_from('<II', d, 0); o = 8
    sizes = (0xC, 0x128 if ver == 0x65 else 0x124, 0x224, 0x1C, 0x18)
    for _ in range(n):
        body = d[o:o + 0x650]; o += 0x650; subs = []
        for sz in sizes:
            c = struct.unpack_from('<I', d, o)[0]; subs.append([d[o + 4 + i * sz:o + 4 + (i + 1) * sz] for i in range(c)]); o += 4 + c * sz
        yield body, subs

if __name__ == '__main__':
    game, mid = sys.argv[1], sys.argv[2]
    m = es.load_map(os.path.join(game, 'Map', mid + '.map'))
    used = collections.Counter(a['art'] for a in m['arts'])
    for body, subs in records(game):
        aid = struct.unpack_from('<I', body, 0)[0]
        if aid not in used: continue
        dff = body[0x234:0x334].split(b'\0')[0].decode('latin1')
        anims = [(struct.unpack_from('<II', s, 0), s[8:].split(b'\0')[0].decode('latin1')) for s in subs[1]]
        c3 = [struct.unpack_from('<IIIf', s, 0) for s in subs[2]]
        print('%5d x%-2d %-34s anims=%s c3=%s c1=%d c4=%d c5=%d' % (aid, used[aid], dff, anims, c3[:4], len(subs[0]), len(subs[3]), len(subs[4])))
