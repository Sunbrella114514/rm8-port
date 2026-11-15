"""List every place in the SOURCE tree that quotes original game text, with the string-table id to use instead (private report: it quotes the text itself).

usage: python -I list_source_quotes.py --corpus <tc scenes dir> --corpus <sc scenes dir> --root port\\app [--root ...] --out report.md [--mincjk 4]

A hit = a run of >= MINCJK CJK characters (in a string literal or in a comment) that occurs in the original text tables of the extracted data
(ui\\strings.txt = string.bin ids, cards.txt, stocks.txt, roles.txt / npcs.txt names, story.txt dialogue, questions.txt, stages.txt).
For each hit the report gives file:line, whether it is inside a string literal ('code') or a comment, the table + id where the text lives and a suggested replacement."""
import argparse, collections, os, re, sys

CJK_RUN = re.compile(r'[㐀-鿿]+')


def load_tables(corpora):
    """run -> [(table, id, kind)] for the Traditional (tc) and Simplified (sc) corpora."""
    idx = collections.defaultdict(list)
    for c in corpora:
        def read(rel):
            p = os.path.join(c, rel)
            return open(p, encoding='utf-8', errors='replace').read().split('\n') if os.path.exists(p) else []
        for line in read('ui/strings.txt'):
            if '\t' in line:
                i, t = line.split('\t', 1)
                idx[t].append(('string.bin', i))
        for n, line in enumerate(read('cards.txt'), 1):
            f = line.split('\t')
            if f[0] == 'card' and len(f) > 12:
                idx[f[2]].append(('card.bin', f[1] + ' name')); idx[f[12]].append(('card.bin', f[1] + ' desc'))
        for line in read('stocks.txt'):
            f = line.split('\t')
            if f[0] == 'stock' and len(f) > 6:
                idx[f[6]].append(('stock.bin', 'id %s' % f[2]))
        for line in read('roles.txt') + read('npcs.txt'):
            f = line.split(' ')
            if f[0] == 'spl' and len(f) > 2:         # speech lines of the characters (Config/speech/sp_NN.bin, in order)
                idx[line.split(' ', 2)[2]].append(('sp_NN.bin', 'spl %s' % f[1]))
            if f[0] in ('role', 'npc') and len(f) > 3:
                idx[f[-1]].append((f[0] + 'info.bin', 'Id %s' % f[1]))
        for line in read('story.txt'):
            if line.startswith('dl '):
                idx[line.split(' ', 2)[2]].append(('drama.bin', 'dl rlId %s' % line.split(' ', 2)[1]))
        for n, line in enumerate(read('questions.txt'), 1):
            for part in line.split('\t'):
                idx[part].append(('questions_normal', 'question %d' % n))
        for line in read('stages.txt'):
            f = line.split(' ')
            if f[0] == 'stage' and len(f) > 4:
                idx[f[4]].append(('stage.bin', 'id %s' % f[1]))
    return idx


def strip_comment(line):
    i = line.find('//')
    while i >= 0 and line[:i].count('"') % 2:
        i = line.find('//', i + 2)
    return (line, '') if i < 0 else (line[:i], line[i:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--corpus', action='append', required=True)
    ap.add_argument('--root', action='append', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--mincjk', type=int, default=4)
    a = ap.parse_args()
    idx = load_tables(a.corpus)
    texts = list(idx.keys())
    rows = []
    for root in a.root:
        files = [root] if os.path.isfile(root) else [os.path.join(dp, f) for dp, _, fs in os.walk(root) for f in fs]
        for p in sorted(files):
            if not p.endswith(('.cpp', '.h', '.inc', '.java', '.py', '.md', '.txt')):
                continue
            try:
                lines = open(p, encoding='utf-8').read().split('\n')
            except UnicodeDecodeError:
                continue
            for ln, line in enumerate(lines, 1):
                if not CJK_RUN.search(line):
                    continue
                code, comment = strip_comment(line) if p.endswith(('.cpp', '.h', '.inc', '.java')) else (line, '')
                for where, part in (('code', code), ('comment', comment)):
                    for run in CJK_RUN.findall(part):
                        if len(run) < a.mincjk:
                            continue
                        hits = [h for t in texts if run in t for h in idx[t]]
                        if hits:
                            rows.append((p, ln, where, run, sorted(set(hits))[:3]))
    by = collections.defaultdict(list)
    for r in rows:
        by[r[0]].append(r)
    with open(a.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# 源码里引用原版文字的位置（私有报告，本身含原文，不要发布）\n\n共 %d 处，%d 个文件。判定：连续 >= %d 个汉字，且出现在提取数据的原文表里。\n\n' % (len(rows), len(by), a.mincjk))
        f.write('建议：`code` = 字符串字面量 → 运行时从提取数据取（string.bin 用 `rm8Str(id)`；卡片 / 股票 / 角色 / 关卡名等读各自的 `.txt`）；'
                '`comment` = 注释里的引文 → 改写成 id 或转述，不要贴原句。\n\n')
        for p, rs in sorted(by.items(), key=lambda kv: -len(kv[1])):
            f.write('## %s（%d）\n\n| 行 | 位置 | 引用的文字 | 原文表 / id |\n|---|---|---|---|\n' % (p.replace('\\', '/'), len(rs)))
            for _, ln, where, run, hits in rs:
                f.write('| %d | %s | %s | %s |\n' % (ln, where, run[:30], '; '.join('%s %s' % h for h in hits)))
            f.write('\n')
    print('%d hits in %d files -> %s' % (len(rows), len(by), a.out))
    print('of which in code (string literals): %d, in comments: %d' % (sum(1 for r in rows if r[2] == 'code'), sum(1 for r in rows if r[2] == 'comment')))


if __name__ == '__main__':
    main()
