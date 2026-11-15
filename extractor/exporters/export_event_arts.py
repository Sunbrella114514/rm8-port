"""Export the 3D event presenters (bank clerk, card-shop machine, question host, ...) for the port: port/scenes/event_arts.txt.
usage: export_event_arts.py <game_dir> <out event_arts.txt>

[V] The event task loaders create these with CArticleDFF_Load(art id) and play role.bin animations with
    Article_SelectByTypeAndSetProperties(n, 1, 0): bank 0x419 (CUi_bank__LoadUI @0x4c1590), card shop 0x41F (@0x4e5aa0),
    AskPoint 0x417 (@0x4bd1f0), lottery 0x41B (@0x4c71a0 / letouErnie @0x4c8bb0), court judge 0x411, magic house 0x412, power company 0x416,
    ticket center 0x414, auction 0x415, plus the neighbouring NPC ids 0x411..0x420.
Line formats:
  bart <art id> <dff key> <texture dir key> <index>:<anm key> ...        (same as the scene.txt "bart" lines)
  face <art> <anim> <time> <part> <texture key>                          role.bin sub-list 3: the "face" texture swaps keyed to an animation and
                                                                         clip time (Article_Update @0x448540 -> 0x44bdc0) = the open / closed mouth frames
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_scene as es
from dump_role_anims import records

NUL = bytes([0])
game, out = sys.argv[1], sys.argv[2]
t = es.art_table(game)
lines = []
for a in list(range(0x411, 0x421)) + [0x27c]:	# 0x27c = the auction's "empty plot" model (BK\Bk000, CUi_vendue__vf16__Init @0x4e2fc0)
    rec = t.get(a)
    if not rec: continue
    lines.append('bart %d %s %s %s' % (a, rec[0].lower(), rec[1].lower(), ' '.join('%d:%s' % (k, v.lower()) for k, v in sorted(rec[4].items())) or '-'))
nface = 0
for body, subs in records(game):
    aid = struct.unpack_from('<I', body, 0)[0]
    if not (0x411 <= aid < 0x421): continue
    for sx in subs[2]:
        a2, ai = struct.unpack_from('<II', sx, 0)
        tm = struct.unpack_from('<f', sx, 8)[0]
        part = sx[0x0C:0x20].split(NUL)[0].decode('latin1')
        fn = sx[0x20:0x120].split(NUL)[0].decode('latin1')
        d2 = sx[0x120:0x220].split(NUL)[0].decode('latin1')
        lines.append('face %d %d %g %s article\\%s%s' % (aid, ai, tm, part or '-', d2, fn))
        nface += 1
with open(out, 'w', encoding='utf-8', newline='\n') as f:
    f.write('\n'.join(lines) + '\n')
print('%d presenters, %d face swaps -> %s' % (len(lines) - nface, nface, out))
