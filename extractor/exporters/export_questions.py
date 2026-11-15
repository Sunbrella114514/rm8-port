"""Export the question-and-answer data of the AskPoint event (event 2) for the port: port/scenes/questions.txt.
usage: export_questions.py <game_dir> <out questions.txt>

[V] Question_LoadNormal @0x4c0ee0 loads "<game>\\questions_normal.ini" (Question_LoadFromINI @0x4bf970: sections "[n]" with
    Question / AnswerA..D / answer); Config\\questions_normal.ini is the plain-text source (cp950), the .bin is its encrypted twin.
    All 201 records have answer=0: answer A is the right one; the game shuffles the four answers when it shows them
    (Question_ShuffleAnswers @0x4bd960).
Line format (UTF-8, one question per line, tab separated):  <question> <A (correct)> <B> <C> <D>
"""
import sys, os, re

game, out = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rm8_edition
ini = os.path.join(game, 'Config', 'questions_normal.ini')
if os.path.exists(ini):		# old install: the plain-text source next to the .bin
    txt = open(ini, 'rb').read().decode('cp950', errors='replace')
else:		# Steam install: only the encrypted .bin twin (same text; edition key, GBK)
    txt = rm8_edition.decode(game, rm8_edition.decrypt(game, open(os.path.join(game, 'Config', 'questions_normal.bin'), 'rb').read()))
lines = []
for m in re.finditer(r'\[(\d+)\]\r?\n(.*?)(?=\r?\n\[|\Z)', txt, re.S):
    kv = dict(l.split('=', 1) for l in m.group(2).splitlines() if '=' in l)
    q = [kv.get(k, '').strip() for k in ('Question', 'AnswerA', 'AnswerB', 'AnswerC', 'AnswerD')]
    ans = int(kv.get('answer', '0'))
    assert ans == 0, (m.group(1), ans)
    if not all(q):
        print('skipping incomplete question', m.group(1)); continue
    lines.append('\t'.join(q))
with open(out, 'w', encoding='utf-8', newline='\n') as f:
    f.write('\n'.join(lines) + '\n')
print('%d questions -> %s' % (len(lines), out))
