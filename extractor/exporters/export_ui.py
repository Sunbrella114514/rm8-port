"""Export the UI data for the port: port/scenes/ui/
  <name>.ui      decrypted .ui files (key "rich8ui", docs/ui_system.md 3.1), converted to UTF-8 (placeholder CapText is GBK)
  strings.txt    string.bin as "id<TAB>text" (Big5 -> UTF-8; values are single lines, '\\' is the game's line break)
  uvlist.txt     uvlisttable.tb records used as UI pictures: "uvl id w h tex... | frames u0 v0 u1 v1 ms texidx ..."
usage: export_ui.py <game_dir> <out_dir>"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_scene as es
import prteff

game, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
key = b'rich8ui'
n = 0
uidir = os.path.join(game, 'Config', 'UI')
for root, dirs, files in os.walk(uidir):
    for f in files:
        if not f.lower().endswith('.ui'): continue
        d = open(os.path.join(root, f), 'rb').read()
        plain = bytes((b - key[i % 7]) & 255 for i, b in enumerate(d))
        rel = os.path.relpath(os.path.join(root, f), uidir)
        dst = os.path.join(out, rel.replace(os.sep, '__'))
        open(dst, 'w', encoding='utf-8', newline='\n').write(plain.decode('gbk', errors='replace'))
        n += 1
lines = []
for name, sec in es.read_cfg(game, 'string.bin'):
    for k, v in sec.items():
        if k.strip().isdigit(): lines.append('%s\t%s' % (k.strip(), v.rstrip('\r')))
open(os.path.join(out, 'strings.txt'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
_, uvl, _ = prteff.load_tables()
ul = []
for i in sorted(uvl):
    r = uvl[i]
    tex = [t['path'].replace(' ', '?') for t in r['textures']] or ['-']
    fr = ' '.join('%.9g %.9g %.9g %.9g %d %d' % (x['u0'], x['v0'], x['u1'], x['v1'], x['duration_ms'], x['texture_index']) for x in r['frames'])
    ul.append('uvl %d %g %g %d %s %d %s' % (i, r['width'], r['height'], len(tex), ' '.join(tex), len(r['frames']), fr))
open(os.path.join(out, 'uvlist.txt'), 'w', encoding='utf-8', newline='\n').write('\n'.join(ul) + '\n')
print('%d ui files, %d strings, %d uvlist records -> %s' % (n, len(lines), len(ul), out))
