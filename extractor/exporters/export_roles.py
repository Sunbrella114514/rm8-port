"""Export the 16 player characters for the port: port/scenes/roles.txt.
usage: export_roles.py <game_dir> <out roles.txt>

[V] roleinfo.bin (Role config loader, sscanf formats): scale "%f,%f,%f" (+0x338; "1.3,1.6,1,3" -> 1.3 1.6 1),
    offset (+0x344, draw-time translation, Role_DrawModelPasses), artId1..3 (+0x308 + set*0x10: 4 models in facing
    order fl fr br bl), dicePos1..3 (+0x350), wlkSndId (+0x374), speed1..3 (+0x380), strId (+0x38c).
[V] role.bin record: +0x234 dff, +0x334 texture dir; sub-list 2 = animations (index, path);
    sub-list 3 (0x224 bytes) = material texture swaps keyed to an animation:
    +0x00 art id, +0x04 animation index, +0x08 float time, +0x0C part name (material user-data "name", e.g. face),
    +0x20 texture file, +0x120 texture dir (applied by Article_Update @0x448540 -> 0x44bdc0 when the clip time passes).
Line formats:
  role <id> <sx sy sz> <ox oy oz> <speed1 2 3> <12 art ids: set0 fl fr br bl, set1 ..., set2 ...> <dice 3x3> <sex> <name>
  model <art> <dff key> <texture dir key>
  anim <art> <index> <anm key>
  face <art> <anim index> <time> <part> <texture key>
  snd <role id> <wlkSndId walk> <motorbike> <car>
  fxev <art> <action> <time> <particle effect> <hook> <type>   (effect articles: role.bin timed particle events)
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_scene as es
from dump_role_anims import records

game, out = sys.argv[1], sys.argv[2]
roles = es.read_cfg(game, 'roleinfo.bin')
lines = []; need = set()
f3 = lambda s: [float(x) for x in s.split(',')[:3]]
for name, r in roles:
    rid = int(r['Id'])
    arts = []
    for k in (1, 2, 3):
        a = [int(x) for x in r['artId%d' % k].split(',')]; assert len(a) == 4; arts += a
    dice = []
    for k in (1, 2, 3): dice += [int(x) for x in r['dicePos%d' % k].split(',')[:3]]
    sc, off, sp = f3(r['scale']), f3(r['offset']), [int(x) for x in r['speed'].split(',')[:3]]
    lines.append('role %d %s %s %s %s %s %s %s %s' % (rid, ' '.join('%g' % v for v in sc), ' '.join('%g' % v for v in off),
                 ' '.join(map(str, sp)), ' '.join(map(str, arts)), ' '.join(map(str, dice)), r.get('sex', '0'), r.get('picName', '-'), r.get('AI', '-')) + ' ' + name.strip())
    need.update(arts)
    # [V] outfit 4 (the 泳裝 / extra outfit): u_Role_CopyTrfToolValues @0x491200 adds 0xC0 to every artId of the role when outfit == 4 (A%02dx models, arts 193..384)
    need.update(a + 192 for a in arts)
    wl = [int(x) for x in r['wlkSndId'].split(',')[:3]]; assert len(wl) == 3
    lines.append('snd %d %s' % (rid, ' '.join(map(str, wl))))	# [V] roleinfo +0x374 wlkSndId[3] (walking loop sample per vehicle, CPlayerTag vf20 @0x48b1f0)
found = set()
# [V] follower models of the classes 0x5e27d8 (伊格魯: the monster A13b, DAT_006044ec = 499 500 498 497, Cls_5e27d8_CreateMovieActor @0x47e1e0) and 0x5e2918
# (莎拉's bodyguard: gods\D46a, DAT_006044fc = 521 522 520 519, Path_Create @0x480260); order fl fr br bl as the role models
need.update((497, 498, 499, 500, 519, 520, 521, 522))
for body, subs in records(game):
    aid = struct.unpack_from('<I', body, 0)[0]
    if aid not in need: continue
    found.add(aid)
    dff = body[0x234:0x334].split(b'\0')[0].decode('latin1'); tdir = body[0x334:0x434].split(b'\0')[0].decode('latin1')
    lines.append('model %d article\\%s article\\%s' % (aid, dff, tdir))
    for s in subs[1]:
        idx = struct.unpack_from('<I', s, 0)[0]; p = s[8:0x108].split(b'\0')[0].decode('latin1')
        lines.append('anim %d %d article\\%s' % (aid, idx, p))
    for s in subs[2]:
        a2, ai = struct.unpack_from('<II', s, 0); t = struct.unpack_from('<f', s, 8)[0]
        part = s[0x0C:0x20].split(b'\0')[0].decode('latin1'); fn = s[0x20:0x120].split(b'\0')[0].decode('latin1')
        d2 = s[0x120:0x220].split(b'\0')[0].decode('latin1')
        assert a2 == aid, (a2, aid)
        lines.append('face %d %d %g %s article\\%s%s' % (aid, ai, t, part or '-', d2, fn))	# empty part name: matches nothing in the game
miss = need - found
assert not miss, 'art ids without role.bin record: %s' % sorted(miss)
# [V] turn arrow (docs/turn_flow.md 3.1): art 6114 eff\Eff_08.dff with animation 0, looped
at = es.art_table(game)[6114]
lines.append('arrow %s %s %s' % (at[0], at[1], at[4].get(0, '-')))
# ---- presentation (port/app/present.cpp, docs/presentation.md)
# [V] roleinfo strId (+0x38c): the role's string.bin block; speech class n = string strId + n (OnNpcInteract @0x490f40)
for name, r in roles:
    lines.append('rstr %d %d' % (int(r['Id']), int(r['strId'])))
# [V] Config_LoadNpcSpeech @0x494e70: config\speech\sp_%02d.txt (owner = roleinfo Id), sections pId / num / rlId<k> / text<k>.
# u_Role_QueueTagEntryMsgs @0x48ca30 plays the first entry whose pId = the other role's Id once per game.
nsp = 0
for rid in sorted(int(r['Id']) for _, r in roles):
    try: secs = es.read_cfg(game, 'speech/sp_%02d.bin' % rid)
    except OSError: continue
    for _, sec in secs:
        n = int(sec.get('num', 0))
        lines.append('sp %d %d %d' % (rid, int(sec.get('pId', 0)), n))
        for k in range(1, n + 1):
            lines.append('spl %d %s' % (int(sec.get('rlId%d' % k, 0)), sec.get('text%d' % k, '').strip()))
        nsp += 1
# [V] effect articles (role.bin 6000..6399: eff, destiny, mend, gods): fxart <art> <dff|-> <texdir|-> <particle effect id or -1> <uva|-> <idx>:<anm> ...
# particle arts (role.bin body +0x10 == 1, effect id at +0x64C, docs/particles.md) have no model; fxhook <art> <hook> <effect>: a child particle on the model's [@hook] frame
at_all = es.art_table(game)
pe, ph = es.prt_table(game)
_, fxev = es.art_action_events(game)
nfx = 0
# [V] arts 0x181..0x1A0 (384 + Id, 400 + Id for the extra outfit): the 'dc' debut models of Role_SpawnCharacterModel @0x48b410 (game start introduction)
# [V] arts 0x8DF / 0x8F6 / 0x9C5 (GS-1 / GS-3 / GS-2, article\eff): the story effects of messages 0x52 / 0x2C (CPlayerTag__OnParkEvent @0x48d310, the stage-end clip @0x4a1390)
STORY_FX = (0x8DF, 0x8F6, 0x9C5)
for a in sorted(set(k for k in at_all if 6000 <= k < 6400 or 385 <= k <= 416 or k in STORY_FX) | set(k for k in pe if 6000 <= k < 6400)):
    rec = at_all.get(a)
    dff = rec[0] if rec else '-'; td = rec[1] if rec else '-'; uva = rec[3] if rec else '-'
    an = ' '.join('%d:%s' % (i, v) for i, v in sorted(rec[4].items())) if rec else ''
    lines.append('fxart %d %s %s %d %s %s' % (a, dff, td, pe.get(a, -1), uva, an))
    for hook, eff in ph.get(a, []): lines.append('fxhook %d %d %d' % (a, hook, eff))
    # [V] role.bin sub-list 4 (Article_Update @0x448540): timed particle events of the article's actions: at `time` of `action` particle effect `effect` starts on the [@hook] frame
    # (6001 the builders: smoke 10 at 0.9 s; 6002 the demolition: 10 at 0; 6004 the monster: 10 at 0.192; 6008 the missile: 12 at 1.3; 6027 the tornado: 11 looping)
    for act, t, eff, hook, typ in fxev.get(a, []): lines.append('fxev %d %d %g %d %d %d' % (a, act, t, eff, hook, typ))
    nfx += 1
print('%d speech entries, %d effect arts' % (nsp, nfx))
os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
open(out, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
print('%d roles, %d models, %d lines -> %s' % (len(roles), len(found), len(lines), out))
