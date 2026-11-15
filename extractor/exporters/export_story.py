"""Export the story mode (闖關模式) data for the port: port/scenes/story.txt and port/scenes/movie_arts.txt.
usage: export_story.py <game_dir> <out story.txt> <out movie_arts.txt>   (docs/story_mode.md)

story.txt line formats ([V] = verified in the decompile, see docs/story_mode.md):
  pass <role> <stage> <level> <opp1> <opp2> <opp3> <card1> <card2> <cash> <fund> <ticket> <pic1> <pic2> <rcash> <rticket> <rcard1..4> <rpic1> <rpic2>
        [V] Config_LoadPassConfig @0x4950e0: pass_NN.bin sections = entries of 0x50 bytes: +0 stage (map id), +4 level (setup+8), +8/+0xC/+0x10 role
        (opponents), +0x14/+0x18 start_card, +0x1C start_cash, +0x20 start_fund, +0x24 start_ticket, +0x28/+0x2C start_pic, +0x30 reward_cash,
        +0x34 reward_tichet, +0x38..+0x44 reward_card, +0x48/+0x4C reward_pic. Entries keep the file order (= the candidate order of the map menu).
  movie <role id> start <n> <script names> end <m> <script names>      [V] Movie_LoadConfig @0x5152a0: movie.bin sections in roleinfo order
  mission <role id> <level> <piece> <condition>                          [V] Role_CheckMissionsByCharacter @0x4835e0 (out/story/missions.json, scripts/story_missions.py)
  drama <id> <n>  / dl <rlId> <text>                                     [V] speech\\drama.bin (Role_QueueNpcDialog @0x48c700)
movie_arts.txt: the art records used by Movie.ArtCreate (all arts of the da*.pyc scripts, out/story/movie_stats.json) plus the host art 1048;
  mart <art id> <dff key> <texture dir key> <uva key or -> <index>:<anm key> ...   (like event_arts.txt `bart` plus the UV animation dictionary)
"""
import sys, os, json, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_scene as es

game, out_story, out_arts = sys.argv[1], sys.argv[2], sys.argv[3]
roles = es.read_cfg(game, 'roleinfo.bin')
role_ids = [int(r['Id']) for _, r in roles]
lines = []

def ints(s, n):
    v = [int(x) for x in s.split(',') if x.strip() != '']
    return (v + [0] * n)[:n]

# ---- pass_XX
npass = 0
for rid in range(1, 17):
    secs = es.read_cfg(game, 'pass/pass_%02d.bin' % rid)
    for name, d in secs:
        opp = ints(d['role'], 3); sc = ints(d['start_card'], 2); sp = ints(d.get('start_pic', '0,0'), 2)
        rc = ints(d['reward_card'], 4); rp = ints(d['reward_pic'], 2)
        row = [rid, d['stage'], d['level']] + opp + sc + [d['start_cash'], d['start_fund'], d['start_ticket']] + sp + [d['reward_cash'], d['reward_tichet']] + rc + rp
        lines.append('pass ' + ' '.join(str(x) for x in row))
        npass += 1

# ---- movie.bin (sections in roleinfo order)
mv = es.read_cfg(game, 'movie.bin')
assert len(mv) == len(roles), (len(mv), len(roles))
for (name, m), rid in zip(mv, role_ids):
    st = [m['start_%d' % i] for i in range(1, int(m['start_num']) + 1)]
    en = [m['end_%d' % i] for i in range(1, int(m['end_num']) + 1)]
    lines.append('movie %d start %d %s end %d %s' % (rid, len(st), ' '.join(st), len(en), ' '.join(en)))

# ---- puzzle missions
base = os.path.dirname(os.path.abspath(__file__))
mj = json.load(open(os.path.join(base, '..', 'out', 'story', 'missions.json'), encoding='utf-8'))
nm = 0
for r in sorted(mj, key=int):
    for lv in sorted(mj[r], key=int):
        for piece, cond in mj[r][lv]:
            lines.append('mission %s %s %d %d' % (r, lv, piece, cond)); nm += 1

# ---- drama.bin (speech\drama.bin: decrypted like a .bin)
d = open(os.path.join(game, 'Config', 'speech', 'drama.bin'), 'rb').read()
import rm8_edition
d = rm8_edition.decrypt(game, d)	# key of the install's edition (old Big5 install: b'tiep8jdg', Steam: b'seowk48h')
nd = 0
for m in re.finditer(r'\[(\d+)\]\r?\n(.*?)(?=\r?\n\[|\Z)', rm8_edition.decode(game, d), re.S):	# cp950 / gbk
    kv = dict(l.split('=', 1) for l in m.group(2).splitlines() if '=' in l)
    n = int(kv['num'])
    have = [i for i in range(1, n + 1) if 'rlId%d' % i in kv and 'text%d' % i in kv]	# [V data] drama 16 says num=4 but has one line
    lines.append('drama %s %d' % (kv['Id'], len(have)))
    for i in have: lines.append('dl %s %s' % (kv['rlId%d' % i], kv['text%d' % i].strip()))
    nd += 1
with open(out_story, 'w', encoding='utf-8', newline='\n') as f:
    f.write('\n'.join(lines) + '\n')
print('%d pass entries, %d missions, %d dramas -> %s' % (npass, nm, nd, out_story))

# ---- movie arts
st = json.load(open(os.path.join(base, '..', 'out', 'story', 'movie_stats.json'), encoding='utf-8'))
want = sorted(set(int(a) for a in st['artCreate_ids']) | {1048})
t = es.art_table(game)
al = []
for a in want:
    rec = t.get(a)
    if not rec: print('art %d: no record / no model' % a); continue
    al.append('mart %d %s %s %s %s' % (a, rec[0].lower(), rec[1].lower(), rec[3].lower(), ' '.join('%d:%s' % (k, v.lower()) for k, v in sorted(rec[4].items())) or '-'))
with open(out_arts, 'w', encoding='utf-8', newline='\n') as f:
    f.write('\n'.join(al) + '\n')
print('%d movie arts -> %s' % (len(al), out_arts))
