"""Export the board NPCs for the port: port/scenes/npcs.txt.
usage: export_npcs.py <game_dir> <out npcs.txt>

[V] Config/npcinfo.bin (NPC_LoadFromINI @0x49d520, sscanf): Id (+0x204), type (+0x200, "%d"), date (+0x228, possession / stay days),
    good (+0x22c), sound (+0x230), scale "%f,%f,%f" (+0x21c), artNum (+0x208), artId "%d,%d,.." (+0x20c.., 1..4 models; 4 = facing
    models fl fr br bl, like a role), ImgFName (+0x100, unused here).
[V] role.bin records of those arts (same layout as export_roles.py): dff, texture dir, animations (index -> .anm).
Line formats:
  npc <Id> <type> <date> <good> <sound> <sx> <sy> <sz> <artNum> <art ids...> <name>
  model <art> <dff key> <texture dir key>
  anim <art> <index> <anm key>
  arrow <sector 0..7> <art>        the 8 ground arrows (CPlayerTag +0x3A0 table DAT_005e1d00 @0x4913c0), selected by the facing sector
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_scene as es

game, out = sys.argv[1], sys.argv[2]
secs = es.read_cfg(game, 'npcinfo.bin')
at = es.art_table(game)
lines = []; need = []
for name, r in secs:
    nid = int(r['Id']); typ = int(r['type']); date = int(r['date']); good = int(r['good'])
    snd = int(r['sound']); sc = [float(x) for x in r['scale'].split(',')[:3]]
    n = int(r['artNum']); arts = [int(x) for x in r['artId'].split(',')][:n]
    lines.append('npc %d %d %d %d %d %g %g %g %d %s %s' % (nid, typ, date, good, snd, sc[0], sc[1], sc[2], n, ' '.join(map(str, arts)), name.strip()))
    # [V] only ids 1..15 and the puzzle 17 have models (Npc_Factory @0x49e6c0; 23 has its own class); 16 / 17 / 100.. use the role art 15, 16 (not drawn)
    if nid <= 15 or nid == 17:
        for a in arts:
            if a not in need: need.append(a)
# [V] CPlayerTag__InitializeNpcModels @0x4913c0: DAT_005e1d00 = 0x18a5 0x18a4 0x18a2 0x18aa 0x18a9 0x18a8 0x18a7 0x18a6
ARROWS = [0x18a5, 0x18a4, 0x18a2, 0x18aa, 0x18a9, 0x18a8, 0x18a7, 0x18a6]
# [V] NPC_CreateRandom_Type1 @0x4a4510 / Type2 @0x4a45f0: the police car (art 0x526 = 1318, element\Mys118) / ambulance (0x527 = 1319, Mys119) of the prison / hospital
# transfer (Stock_InitializeObject_Type1 @0x4a83c0 / Type2 @0x4a84f0)
for a in (0x526, 0x527):
    if a not in need: need.append(a)
for i, a in enumerate(ARROWS):
    lines.append('arrow %d %d' % (i, a))
    if a not in need: need.append(a)
pe, ph = es.prt_table(game)
for a in need:
    for hook, eff in ph.get(a, []): lines.append('hook %d %d %d' % (a, hook, eff))
for a in need:
    rec = at.get(a)
    if not rec:
        print('warning: art %d has no model' % a); continue
    lines.append('model %d %s %s' % (a, rec[0], rec[1]))
    for k, v in sorted(rec[4].items()):
        lines.append('anim %d %d %s' % (a, k, v))
os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
open(out, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
print('%d npcs, %d models -> %s' % (len(secs), len(need), out))
