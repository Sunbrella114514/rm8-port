"""Export the card data for the port: port/scenes/cards.txt
One line per card, in card.bin order (the game's card array index = line number - 1; Card_LoadCardBin @0x4adec0):
  card <TAB> id name type level ticket condition speak0 speak1 speak2 python texKey desc
The fields are the card object fields: +0x204 Id, +0x4 name, +0x208 type, +0x210 level, +0x214 ticket (points),
+0x20C condition, +0x218 speakNo[3], python (".py" stripped like the game), +0x104 texName -> texture key prt\\card\\<texName>.
desc = string.bin id 6500 + Id (Card_ShowInfo_8f40 @0x4e8f40 reads StringTable + 26000 + Id * 4).
usage: export_cards.py <game_dir> <out_file>"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_scene as es

game, out = sys.argv[1], sys.argv[2]
strings = {}
for name, sec in es.read_cfg(game, 'string.bin'):
    for k, v in sec.items():
        if k.strip().isdigit(): strings[int(k.strip())] = v.rstrip('\r')
lines = []
for name, sec in es.read_cfg(game, 'card.bin'):
    cid = int(sec['Id'])
    sp = [int(x) for x in sec.get('speakNo', '-1,-1,-1').split(',')]
    sp += [-1] * (3 - len(sp))
    py = sec.get('python', '-').strip()
    if py.lower().endswith('.py'): py = py[:-3]
    tex = sec.get('texName', '').strip().lower()
    key = ('prt\\card\\' + tex) if tex else '-'
    desc = strings.get(6500 + cid, '')
    f = [str(cid), name, sec['type'], sec['level'], sec['ticket'], sec['condition'], str(sp[0]), str(sp[1]), str(sp[2]), py or '-', key, desc]
    assert all('\t' not in x and '\n' not in x for x in f), f
    lines.append('card\t' + '\t'.join(f))
# possession days of the gods and the roadhog (Role_SetPossession @0x48b6e0 copies NPC +0x228 = npcinfo.bin date)
nn = 0
for name, sec in es.read_cfg(game, 'npcinfo.bin'):
    if 'Id' in sec and 'date' in sec:
        lines.append('npc\t%s\t%s\t%s' % (sec['Id'].strip(), sec['date'].strip(), sec.get('good', '0').strip())); nn += 1
open(out, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
print('%d cards, %d npc records -> %s' % (len(lines) - nn, nn, out))
