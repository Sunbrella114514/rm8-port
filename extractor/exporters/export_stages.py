"""Export the stage list for the front-end menus: port/scenes/stages.txt (NEW file; does not touch other scene files).
Source: out/config_utf8/stage.bin.txt (decrypted Config\\stage.bin, section name = city, id / mapfile / clime / wonder).
Line format: stage <id> <mapdir> <clime> <name>      (mapdir = map file name without ".map", e.g. 01)
[V] Map_LoadMapList @0x4a8ce0 reads these keys; the download maps (ids 0x15.., Map_LoadDownloadMap @0x4a9020) are found in the
scene folders at run time (download2NN), their menu name is the file name without ".map".
usage: export_stages.py [stage.bin.txt] [out]"""
import sys, re, os
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'out', 'config_utf8', 'stage.bin.txt')
dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, 'port', 'scenes', 'stages.txt')
secs = []
cur = None
for line in open(src, encoding='utf-8'):
    line = line.strip()
    m = re.match(r'\[(.+)\]$', line)
    if m:
        cur = {'name': m.group(1)}; secs.append(cur)
    elif '=' in line and cur is not None:
        k, v = line.split('=', 1); cur[k.strip().lower()] = v.strip()
with open(dst, 'w', encoding='utf-8', newline='\n') as f:
    for s in secs:
        mapdir = re.sub(r'\.map$', '', s['mapfile'], flags=re.I)
        f.write('stage %s %s %s %s\n' % (s['id'], mapdir, s.get('clime', '1'), s['name'].replace(' ', '_')))
print('wrote', dst, len(secs), 'stages')
