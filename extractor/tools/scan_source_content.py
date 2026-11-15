"""Source content scan for the legal inventory: (a) CJK string literals in code, with a flag when they occur in the original text corpus,
(b) large static data tables, (c) long base64/hex blobs.   usage: python -I scan_source_content.py --corpus <scenes dir> [--corpus ...] --root <dir> ..."""
import argparse, collections, os, re, sys

CJK = re.compile(r'[㐀-鿿]')
STR = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def strip_comment(line, ext):
    if ext in ('.py',):
        i = line.find('#')
        return line if i < 0 or line[:i].count('"') % 2 else line[:i]
    i = line.find('//')
    while i >= 0 and line[:i].count('"') % 2:
        i = line.find('//', i + 2)
    return line if i < 0 else line[:i]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--corpus', action='append', required=True)
    ap.add_argument('--root', action='append', required=True)
    a = ap.parse_args()
    lines_corpus = set()
    runs = set()
    for c in a.corpus:
        for dp, _, fs in os.walk(c):
            for f in fs:
                if f in ('strings.txt', 'cards.txt', 'questions.txt', 'introduce.txt', 'roles.txt', 'news.txt', 'stocks.txt', 'story.txt', 'stages.txt', 'npcs.txt', 'scene.txt'):
                    for line in open(os.path.join(dp, f), encoding='utf-8', errors='replace'):
                        for r in re.findall(r'[㐀-鿿]+', line):
                            if len(r) >= 2: runs.add(r)
    print('corpus runs:', len(runs))
    tot = collections.Counter(); per = collections.defaultdict(list)
    tables = []; blobs = []
    for root in a.root:
        for dp, dns, fs in os.walk(root):
            for f in fs:
                ext = os.path.splitext(f)[1]
                if ext not in ('.cpp', '.h', '.inc', '.py', '.java', '.c'):
                    continue
                p = os.path.join(dp, f)
                try:
                    text = open(p, encoding='utf-8').read()
                except UnicodeDecodeError:
                    continue
                lines = text.split('\n')
                for ln, line in enumerate(lines, 1):
                    code = strip_comment(line, ext)
                    for m in STR.finditer(code):
                        s = m.group(1)
                        if CJK.search(s):
                            rs = re.findall(r'[㐀-鿿]+', s)
                            verbatim = any(r in runs for r in rs if len(r) >= 3) or s.strip('%sd\\n ') in runs
                            tot['cjk literals'] += 1
                            if verbatim:
                                tot['  of which in the original text'] += 1
                                per[p].append((ln, s))
                    if re.search(r'[A-Za-z0-9+/=]{200,}', code):
                        blobs.append((p, ln))
                # tables: initialiser lists with many numbers
                for m in re.finditer(r'(?:static\s+)?(?:const\s+)?[\w:<> ]+\s+(\w+)\s*\[[^\]]*\]\s*(?:\[[^\]]*\])?\s*=\s*\{([^;]*?)\}\s*;', text, re.S):
                    body = m.group(2)
                    n = len(re.findall(r'-?\d+(?:\.\d+)?', body))
                    if n >= 40:
                        tables.append((n, p, m.group(1), text[:m.start()].count('\n') + 1, len(re.findall(r'"', body)) // 2))
                if ext == '.inc':
                    n = len(re.findall(r'0x[0-9a-fA-F]+|-?\b\d+\b', text))
                    if n >= 40:
                        tables.append((n, p, '(whole .inc file)', 1, 0))
    print(dict(tot))
    print('\n-- CJK literals that also occur in the original text, by file')
    for p, v in sorted(per.items(), key=lambda kv: -len(kv[1])):
        print('%3d  %s' % (len(v), p))
        for ln, s in v[:4]:
            print('       %d: %s' % (ln, s[:60]))
    print('\n-- tables with >= 40 numbers')
    for n, p, name, ln, nstr in sorted(tables, reverse=True)[:60]:
        print('%6d  %s:%d  %s%s' % (n, p, ln, name, '  (%d strings)' % nstr if nstr else ''))
    print('\n-- long base64/hex-like runs (>= 200 chars):', len(blobs), blobs[:5])


if __name__ == '__main__':
    main()
