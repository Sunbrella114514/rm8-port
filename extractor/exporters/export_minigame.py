"""Export the minigame sprite / model articles for the port: <out>/sprites.txt
The minigame DLLs create objects by article id through the host (CMiniGameHost vf20 -> CArticleMgr vf16 @0x43c470 ->
CArticleDFF_Load @0x455bd0). Article records come from Config\\newOut\\role.bin (docs/rolebin_format.md); for a sprite article
(record +0xc == 1) CArticleDFF_Load calls Sprite_Init(x = rec+0x640, y = rec+0x644, colour -1, uvlist id = rec+0x638, rec+0x648),
the picture is a uvlisttable.tb record (docs/prt_format.md).
  spr <artId> <uvlId> <w> <h> <loopStart> <loopCount> <ntex> <tex...> <nframes> (u0 v0 u1 v1 ms texidx)*   ('?' = space in names)
  mdl <artId> <dff> <texdir> <nanim> (index path)*                                                       3D article (dff + anm)
usage: export_minigame.py <game_dir> <out_dir>"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prteff
from dump_role_anims import records

game, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
_, uvl, _ = prteff.load_tables()
lines = []
ns = nm = 0
for body, subs in records(game):
	aid = struct.unpack_from('<I', body, 0)[0]
	if not (1500 <= aid <= 2200):	# 0x5dd .. 0x860 and the 0x6xx/0x7xx/0x8xx ranges used by ltlgame_18..23
		continue
	if struct.unpack_from('<I', body, 0xc)[0] == 1:
		u = struct.unpack_from('<I', body, 0x638)[0]
		r = uvl.get(u)
		if r is None:
			print('art %d: uvlist %d missing' % (aid, u)); continue
		tex = [t['path'].replace(' ', '?') for t in r['textures']] or ['-']
		fr = ' '.join('%.9g %.9g %.9g %.9g %d %d' % (x['u0'], x['v0'], x['u1'], x['v1'], x['duration_ms'], x['texture_index']) for x in r['frames'])
		lines.append('spr %d %d %g %g %d %d %d %s %d %s' % (aid, u, r['width'], r['height'], r['loop_start_frame'], r['loop_count'], len(tex), ' '.join(tex), len(r['frames']), fr))
		ns += 1
	else:
		dff = body[0x234:0x334].split(b'\0')[0].decode('latin1')
		td = body[0x334:0x434].split(b'\0')[0].decode('latin1')
		an = [(struct.unpack_from('<I', s, 0)[0], s[8:].split(b'\0')[0].decode('latin1')) for s in subs[1]]
		lines.append('mdl %d %s %s %d %s' % (aid, dff, td or '-', len(an), ' '.join('%d %s' % a for a in an)))
		nm += 1
open(os.path.join(out, 'sprites.txt'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
print('%d sprites, %d models -> %s' % (ns, nm, out))
