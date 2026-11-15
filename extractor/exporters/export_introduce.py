"""Export the names and introductions of the map articles for the port: port/scenes/introduce.txt.
usage: export_introduce.py <game_dir> <out introduce.txt>

[V] Clicking / hovering a map article (Game_OnTileClickWithCard @0x4a4a10 case 5, docs/infoscreens.md 6):
    CArticleMgr vf32 @0x43ecc0 = Registry_FindByInt(article id) over the introduce texts that CArticleMgr vf0 @0x43ac90 loaded from Config\\introduce.txt
    (INI_LoadIntroduceTexts @0x4436e0: every key's value, backslash -> line break, the number after '#' = the article id, stored at +0x100) -> UI_ShowIntroduction (intro.ui);
    without a text vf31 @0x43ebb0 = the role.bin record (version 0x65 body) +0x128 string = the article's name, cut at the first '[' -> UI_ShowPlayerInfo (ginfo.ui).
Line format (UTF-8, tab separated):  <article dff key as scene.txt uses it (article\\<dff>)> TAB <name> TAB <introduction, backslash = line break; empty if none>
Only articles with a text or a non-ASCII (real) name are written; several article ids may use one model (the last one wins).
"""
import sys, os, re, struct
game, out = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rm8_edition

def u32(d, o): return struct.unpack_from('<I', d, o)[0]
d = open(os.path.join(game, 'Config', 'newOut', 'role.bin'), 'rb').read()
ver, n = struct.unpack_from('<II', d, 0); o = 8
sizes = (0xC, 0x128 if ver == 0x65 else 0x124, 0x224, 0x1C, 0x18)
arts = {}
for _ in range(n):
    body = d[o:o + 0x650]; o += 0x650
    for sz in sizes: o += 4 + u32(d, o) * sz
    aid = u32(body, 0)
    dff = body[0x234:0x334].split(b'\0')[0].decode('latin1')
    name = rm8_edition.decode(game, body[0x128:0x228].split(b'\0')[0]).strip()
    name = name.split('[')[0]	# the click code cuts the name at the first '['
    if dff: arts[aid] = (dff, name)
txt = rm8_edition.decode(game, rm8_edition.decrypt(game, open(os.path.join(game, 'Config', 'introduce.bin'), 'rb').read()))
intro = {}
for m in re.finditer(r'^(\d+)=(.*)$', txt, re.M):
    v = m.group(2).rstrip('\r')
    k = v.rfind('#')
    if k < 0: continue
    intro[int(v[k + 1:].strip())] = v[:k]
lines = {}
for aid, (dff, name) in sorted(arts.items()):
    t = intro.get(aid, '')
    if not t and (not name or name.isascii()): continue
    key = 'article\\' + dff
    lines[key] = '%s\t%s\t%s' % (key, name, t)
missing = [a for a in intro if a not in arts]
if missing: print('introduction ids without a role.bin article:', missing)
with open(out, 'w', encoding='utf-8', newline='\n') as f:
    f.write('\n'.join(lines.values()) + '\n')
print('%d articles (%d with an introduction) -> %s' % (len(lines), sum(1 for v in lines.values() if v.split('\t')[2]), out))
