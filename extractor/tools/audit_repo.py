"""Audit a source tree for material that must not be redistributed: files identical to original game files, files that are derived from them,
and text that was copied from the game's string tables.   (A technical aid, not a legal opinion; see docs/assets_legal.md.)

usage: python -I audit_repo.py --game <install> [--game <install2>] --corpus <extracted scenes dir> [--corpus ...] --root <dir> [--root ...] [--skip <dirname> ...]

1. byte-identical copies:   every file under --root whose sha256 equals a file of an install
2. derived-data lookalikes: file names / folders that the exporters produce (scene.txt, lm_*.png, roles.txt ...)
3. copied text:             string literals / lines with >= MINCJK CJK characters in source files (.cpp .h .java .py .md .txt .cmake) that occur verbatim
                            in the extracted original text corpus (ui/strings.txt, cards, questions, roles, news, stocks, story ... of --corpus)
"""
import argparse, hashlib, os, re, sys, collections

SRC_EXT = ('.cpp', '.h', '.hpp', '.c', '.inc', '.java', '.py', '.md', '.txt', '.cmake', '.xml', '.sh', '.cs', '.json')
DERIVED_NAMES = {'scene.txt', 'roles.txt', 'cards.txt', 'stocks.txt', 'npcs.txt', 'particles.txt', 'questions.txt', 'introduce.txt', 'news.txt', 'stages.txt', 'story.txt',
                 'movie_arts.txt', 'event_arts.txt', 'uvlist.txt', 'strings.txt', 'sprites.txt', 'zh_t2s.txt'}
CJK = re.compile(r'[㐀-鿿]')


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 22), b''):
            h.update(b)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', action='append', required=True)
    ap.add_argument('--corpus', action='append', default=[])
    ap.add_argument('--root', action='append', required=True)
    ap.add_argument('--skip', action='append', default=[])
    ap.add_argument('--mincjk', type=int, default=4)
    a = ap.parse_args()
    skip = set(s.lower() for s in a.skip)

    gsizes = collections.defaultdict(list)
    for g in a.game:
        for dp, _, fs in os.walk(g):
            for f in fs:
                p = os.path.join(dp, f)
                try:
                    sz = os.path.getsize(p)
                except OSError:
                    continue
                if sz >= 16:
                    gsizes[sz].append(p)
    ghash = {}
    def game_hashes(sz):
        if sz not in ghash:
            ghash[sz] = {sha256(p): p for p in gsizes[sz]}
        return ghash[sz]

    # corpus of original text lines
    corpus = set()
    names = ('strings.txt', 'cards.txt', 'questions.txt', 'introduce.txt', 'roles.txt', 'news.txt', 'stocks.txt', 'story.txt', 'stages.txt', 'npcs.txt')
    for c in a.corpus:
        for dp, _, fs in os.walk(c):
            for f in fs:
                if f in names:
                    for line in open(os.path.join(dp, f), encoding='utf-8', errors='replace'):
                        for field in re.split(r'[\t ]+', line.strip()):
                            for run in re.findall(r'[㐀-鿿]+', field):
                                if len(run) >= a.mincjk:
                                    corpus.add(run)
                        for run in re.findall(r'[㐀-鿿][㐀-鿿，。！？、：；“”‘’…·\w%]*', line):
                            if len(run) >= a.mincjk:
                                corpus.add(run)
    print('corpus: %d distinct CJK runs (>= %d chars)' % (len(corpus), a.mincjk))

    ident, derived, copied = [], [], collections.defaultdict(list)
    nfiles = 0
    for root in a.root:
        for dp, dns, fs in os.walk(root):
            dns[:] = [d for d in dns if d.lower() not in skip]
            for f in fs:
                p = os.path.join(dp, f)
                nfiles += 1
                try:
                    sz = os.path.getsize(p)
                except OSError:
                    continue
                if sz in gsizes and sz >= 16:
                    h = sha256(p)
                    if h in game_hashes(sz):
                        ident.append((p, game_hashes(sz)[h]))
                if f in DERIVED_NAMES or re.match(r'(lm|alm)_\d+\.png$', f):
                    derived.append(p)
                if f.lower().endswith(SRC_EXT) and sz < 4 << 20:
                    try:
                        text = open(p, encoding='utf-8').read()
                    except UnicodeDecodeError:
                        continue
                    for ln, line in enumerate(text.split('\n'), 1):
                        if not CJK.search(line):
                            continue
                        hits = [r for r in re.findall(r'[㐀-鿿]+', line) if len(r) >= a.mincjk and r in corpus]
                        if hits:
                            copied[p].append((ln, hits[0]))
    print('scanned %d files' % nfiles)
    print('\n== 1. byte-identical to an original game file: %d' % len(ident))
    for p, g in ident[:100]:
        print('  %s\n      == %s' % (p, g))
    print('\n== 2. derived-data files (exporter outputs): %d' % len(derived))
    byd = collections.Counter(os.path.dirname(p) for p in derived)
    for d, n in sorted(byd.items())[:60]:
        print('  %5d  %s' % (n, d))
    print('\n== 3. source/text files containing verbatim original text (>= %d CJK chars): %d files' % (a.mincjk, len(copied)))
    for p, hs in sorted(copied.items(), key=lambda kv: -len(kv[1]))[:80]:
        print('  %4d hits  %s   e.g. line %d: %s' % (len(hs), p, hs[0][0], hs[0][1]))


if __name__ == '__main__':
    main()
