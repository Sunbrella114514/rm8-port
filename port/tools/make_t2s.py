"""Build the port's Traditional -> Simplified table from the OpenCC dictionaries (Apache-2.0, extern/opencc_dict):
TSPhrases (phrases first, longest match) + TSCharacters (single characters, first candidate).
usage: python -I make_t2s.py <opencc_dict dir> <out scenes/zh_t2s.txt>"""
import os, sys

src, out = sys.argv[1], sys.argv[2]
rows = {}
for name in ('TSCharacters.txt', 'TSPhrases.txt'):
    for line in open(os.path.join(src, name), encoding='utf-8'):
        if not line.strip() or line.startswith('#'):
            continue
        k, v = line.rstrip('\n').split('\t', 1)
        v = v.split(' ')[0]
        if k != v:
            rows[k] = v
with open(out, 'w', encoding='utf-8', newline='\n') as f:
    f.write('# Traditional -> Simplified (OpenCC TSPhrases + TSCharacters, Apache-2.0); key<TAB>value\n')
    for k in sorted(rows):
        f.write('%s\t%s\n' % (k, rows[k]))
print(len(rows), 'entries ->', out)
