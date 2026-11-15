"""Export the news system data for the port: port/scenes/news.txt (docs/news.md).
usage: export_news.py <game_dir> <out news.txt>

Lines:
  nart <art id> <dff key> <texture dir key> <idx>:<anm key> ...   news illustrations (article\\news\\xNN: art 900 + news id; 922 colour bars, 960 snow),
                                                                   the anchor (art 1052 = article\\NPC\\N24D, anims 0..2) and the NPC news pictures (6201..6212)
  fest <block 0..5> <yyyymmdd> * 32                               Config\\festival.bin: 6 blocks of 32 dates (years 2003..2034) = the moving festivals
                                                                   (Festival_LoadTable @0x4a5ba0: 0 Chinese New Year, 1 Lantern, 2 Dragon Boat, 3 Qixi, 4 Mid-Autumn, 5 Double Ninth)
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_scene as es

game, out = sys.argv[1], sys.argv[2]
t = es.art_table(game)
ids = list(range(901, 959)) + [922, 960, 1052] + list(range(6201, 6213))
lines = []
for a in sorted(set(ids)):
    rec = t.get(a)
    if not rec: continue
    lines.append('nart %d %s %s %s' % (a, rec[0].lower(), rec[1].lower(), ' '.join('%d:%s' % (k, v.lower()) for k, v in sorted(rec[4].items())) or '-'))
# [V] Festival_LoadTable @0x4a5ba0: header lines kinds (6), years (32), base date (20030000); then kinds x years date lines (8 digits + CRLF = 10 bytes each)
raw = open(os.path.join(game, 'Config', 'festival.bin'), 'rb').read().decode('latin1').replace('\r', '').split('\n')
kinds, years, base = int(raw[0]), int(raw[1]), int(raw[2])
assert kinds == 6 and years == 32 and base == 20030000, (kinds, years, base)
body = [int(x) for x in raw[3:3 + kinds * years]]
for k in range(kinds):
    lines.append('fest %d %s' % (k, ' '.join(str(v) for v in body[k * years:(k + 1) * years])))
with open(out, 'w', encoding='utf-8', newline='\n') as f:
    f.write('\n'.join(lines) + '\n')
print('%d articles, %d festival blocks -> %s' % (len([l for l in lines if l.startswith('nart')]), kinds, out))
