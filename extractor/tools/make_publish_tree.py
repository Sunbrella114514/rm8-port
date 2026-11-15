"""Build the publishable tree (see PUBLISH.md): copy the whitelist, replace original game text in the documentation by string-table ids, and audit the result.

  python -I make_publish_tree.py --out <new empty folder> [--corpus <scenes dir> ...] [--game <install> ...]
  python -I make_publish_tree.py --docs-only <folder>        only write the de-quoted docs (e.g. D:\\ai\\rm8\\docs_public)

De-quoting: every run of >= MINCJK Chinese characters in a .md file that occurs inside a text of the original string tables (string.bin, cards, stocks,
roles / npcs, story dialogue, questions, stage names -- both the Traditional and the Simplified edition) is replaced by <table>#<id>.  Names of 3 or fewer
characters (people, places) are kept: they are identifiers, not quotations.  The private originals under docs\\ are never modified.
Exit status 1 when the audit of the finished tree finds anything."""
import argparse, bisect, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
EXT = os.path.dirname(HERE)
ROOT = os.path.dirname(EXT)
sys.path.insert(0, HERE)
import list_source_quotes as lsq  # noqa: E402

PRIVATE_DOCS = {'code_map.md', 'naming_review_v2.md', 'exe_internals.md', 're_context.md', 'workflow.md'}
ROOT_FILES = ['LICENSE', 'NOTICE.md', 'THIRD_PARTY_NOTICES.md', 'README.md', 'PUBLISH.md', '.gitignore']
PORT_FILES = ['CMakeLists.txt', 'README.md', 'LICENSE', 'NOTICE.md', 'THIRD_PARTY_NOTICES.md', '.gitignore']
PORT_DIRS = ['app', 'cmake', 'patches', 'viewer', 'loadtest', 'mgtest', 'deps_local']      # third-party sources are NOT published: extern/fetch_deps.py downloads them
PORT_ANDROID_FILES = ['AndroidManifest.xml', 'build_apk.py', 'deploy.py', 'data_files.py']
EXTRACTOR_FILES = ['rm8_extract.py', 'README.md', '.gitignore']
EXTRACTOR_DIRS = ['rm8x', 'exporters', 'data', 'tools', 'android']
SKIP_NAMES = {'__pycache__', '.git', 'premake5.exe', 'test', 'tests', 'examples'}      # third-party test / example folders are not needed to build
SKIP_EXT = ('.pyc', '.exe', '.apk', '.idsig', '.keystore', '.dll', '.mpg', '.bik')
CJK_RUN = re.compile(r'[㐀-鿿]+')
TERM_FREQ = 6      # a phrase seen this often in the documentation is a game term, not a quotation
LONG = 6           # runs of at least this many characters inside an original text are always replaced


class Scrubber:
    def __init__(self, corpora, mincjk=4, alltext=''):
        self.min = mincjk
        idx = lsq.load_tables(corpora)
        self.texts = [t for t in idx if CJK_RUN.search(t)]
        self.hit = {t: idx[t] for t in self.texts}
        self.joined = '\x00'.join(self.texts)
        self.starts, o = [], 0
        for t in self.texts:
            self.starts.append(o); o += len(t) + 1
        runs = {r for t in self.texts for r in CJK_RUN.findall(t) if len(r) >= mincjk}
        self.runs = sorted(runs, key=len, reverse=True)
        self.rx = re.compile('|'.join(re.escape(r) for r in self.runs)) if self.runs else None
        self.count = 0
        self.freq = {}
        self.alltext = alltext

    def _token(self, text):
        tab, i = sorted(set(self.hit[text]), key=lambda h: (h[0] != 'string.bin', h))[0]
        return '〔%s#%s〕' % (tab, i.replace(' ', '_'))

    def _is_term(self, run):
        """A phrase that the documentation uses over and over (a game term such as a mode or menu name), not a quotation."""
        if run not in self.freq:
            self.freq[run] = self.alltext.count(run)
        return self.freq[run] >= TERM_FREQ

    def _sub(self, mm):
        run = mm.group(0)
        if self._is_term(run):
            return run
        q = self.joined.find(run)
        self.count += 1
        return self._token(self.texts[bisect.bisect_right(self.starts, q) - 1]) if q >= 0 else run

    def _run(self, m):
        r = m.group(0)
        if len(r) < self.min:
            return r
        p = self.joined.find(r)
        if len(r) >= LONG and p >= 0 and not self._is_term(r):          # a long run inside one original text: a quotation
            self.count += 1
            return self._token(self.texts[bisect.bisect_right(self.starts, p) - 1])
        return self.rx.sub(self._sub, r) if self.rx else r               # shorter runs: only those that are whole original phrases and not common terms

    def text(self, s):
        return CJK_RUN.sub(self._run, s)


def write_docs(src_docs, dst, scrub):
    n = 0
    for dp, _, fs in os.walk(src_docs):
        for f in fs:
            if f in PRIVATE_DOCS and os.path.dirname(os.path.join(dp, f)) == src_docs:
                continue
            p = os.path.join(dp, f)
            q = os.path.join(dst, os.path.relpath(p, src_docs))
            os.makedirs(os.path.dirname(q), exist_ok=True)
            if f.endswith('.md'):
                open(q, 'w', encoding='utf-8', newline='\n').write(scrub.text(open(p, encoding='utf-8').read()))
            else:
                shutil.copyfile(p, q)
            n += 1
    return n


def copy_tree(src, dst):
    for dp, dns, fs in os.walk(src):
        dns[:] = [d for d in dns if d not in SKIP_NAMES]
        for f in fs:
            if f in SKIP_NAMES or f.lower().endswith(SKIP_EXT):
                continue
            p = os.path.join(dp, f)
            q = os.path.join(dst, os.path.relpath(p, src))
            os.makedirs(os.path.dirname(q), exist_ok=True)
            shutil.copyfile(p, q)


# Development-machine details that must not be published (user name, local paths, device serials, account ids):
# rewritten in every text file of the tree; audit fails if any is left. The table is applied to this file's own copy too.
import base64 as _b64, json as _json
_SAN = _json.loads(_b64.b64decode('eyJyIjogW1siQzpcXFVzZXJzXFxXaW5kcnVzdHkiLCAiJVVTRVJQUk9GSUxFJSJdLCBbIkM6L1VzZXJzL1dpbmRydXN0eSIsICIlVVNFUlBST0ZJTEUlIl0sIFsiV2luZHJ1c3R5IiwgIjx1c2VyPiJdLCBbIkU6XFxib290bGVnX2dhbWVzXFxSaWNoTWFuOCIsICI8b2xkLWVkaXRpb24tZGlyPiJdLCBbIkU6L2Jvb3RsZWdfZ2FtZXMvUmljaE1hbjgiLCAiPG9sZC1lZGl0aW9uLWRpcj4iXSwgWyJib290bGVnX2dhbWVzIiwgImdhbWVzIl0sIFsiRDpcXGFpXFxybTgiLCAiPHdvcmtzcGFjZT4iXSwgWyJEOi9haS9ybTgiLCAiPHdvcmtzcGFjZT4iXSwgWyJEOlxcYWlcXHRvb2xzIiwgIjx0b29scz4iXSwgWyJEOi9haS90b29scyIsICI8dG9vbHM+Il0sIFsiRDpcXGFpIiwgIjxkZXY+Il0sIFsiRDovYWkiLCAiPGRldj4iXSwgWyJEOlxcU3RlYW1MaWJyYXJ5XFxzdGVhbWFwcHNcXGNvbW1vblxcUmljaG1hbiA4IiwgIjxzdGVhbS1lZGl0aW9uLWRpcj4iXSwgWyJEOi9TdGVhbUxpYnJhcnkvc3RlYW1hcHBzL2NvbW1vbi9SaWNobWFuIDgiLCAiPHN0ZWFtLWVkaXRpb24tZGlyPiJdLCBbIkhBMlFHNkZSIiwgIjxkZXZpY2U+Il0sIFsiNzBkNWNjYTgiLCAiPGRldmljZT4iXSwgWyI4Mzg0NzUxNzEiLCAiPHN0ZWFtLWFjY291bnQ+Il0sIFsi5LiA5YqgIFBKWjExMO+8jCIsICIiXSwgWyJQSloxMTAiLCAiPHBob25lPiJdXSwgImwiOiBbIldpbmRydXN0eSIsICJib290bGVnIiwgIkQ6XFxhaSIsICJEOi9haSIsICJIQTJRRzZGUiIsICI3MGQ1Y2NhOCIsICI4Mzg0NzUxNzEiLCAiUEpaMTEwIiwgIlN0ZWFtTGlicmFyeSJdfQ==').decode('utf-8'))	# base64 so that this file itself carries none of them
SANITIZE = [tuple(x) for x in _SAN['r']]
SANITIZE_LEFT = tuple(_SAN['l'])
TEXT_EXT = ('.md', '.txt', '.py', '.cpp', '.h', '.c', '.inc', '.cmake', '.json', '.xml', '.java', '.sh', '.bat', '.ui', '.cfg', '.ini', '.toml', '.yml', '.yaml')


def sanitize(out):
    n = 0
    for dp, _, fs in os.walk(out):
        for f in fs:
            if not (f.lower().endswith(TEXT_EXT) or f in ('CMakeLists.txt', '.gitignore', 'LICENSE')):
                continue
            p = os.path.join(dp, f)
            raw = open(p, 'rb').read()
            try:
                t = raw.decode('utf-8')
            except UnicodeDecodeError:
                continue
            t2 = t
            for a, b in SANITIZE:
                t2 = t2.replace(a, b)
            if t2 != t:
                open(p, 'wb').write(t2.encode('utf-8')); n += 1
    left = []
    for dp, _, fs in os.walk(out):
        for f in fs:
            try:
                t = open(os.path.join(dp, f), encoding='utf-8').read()
            except (UnicodeDecodeError, OSError):
                continue
            left += [os.path.relpath(os.path.join(dp, f), out) for k in SANITIZE_LEFT if k in t][:1]
    return n, left


def build(out, scrub):
    def put(rel_src, rel_dst=None):
        s = os.path.join(ROOT, rel_src)
        d = os.path.join(out, rel_dst or rel_src)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        if rel_src.endswith('.md'):
            open(d, 'w', encoding='utf-8', newline='\n').write(scrub.text(open(s, encoding='utf-8').read()))
        else:
            shutil.copyfile(s, d)
    for f in ROOT_FILES:
        put(f)
    for f in PORT_FILES:
        put(os.path.join('port', f))
    put(os.path.join('port', 'tools', 'make_t2s.py'))
    put(os.path.join('port', 'extern', 'fetch_deps.py'))
    put(os.path.join('port', 'extern', 'README.md'))
    for f in PORT_ANDROID_FILES:
        put(os.path.join('port', 'android', f))
    copy_tree(os.path.join(ROOT, 'port', 'android', 'src'), os.path.join(out, 'port', 'android', 'src'))
    for d in PORT_DIRS:
        copy_tree(os.path.join(ROOT, 'port', d), os.path.join(out, 'port', d))
    for f in EXTRACTOR_FILES:
        put(os.path.join('extractor', f))
    for d in EXTRACTOR_DIRS:
        copy_tree(os.path.join(EXT, d), os.path.join(out, 'extractor', d))
    # .md files copied wholesale (extern READMEs, data READMEs) must be de-quoted too
    for dp, _, fs in os.walk(out):
        for f in fs:
            if f.endswith('.md') and f not in ROOT_FILES:
                p = os.path.join(dp, f)
                t = open(p, encoding='utf-8', errors='replace').read()
                t2 = scrub.text(t)
                if t2 != t:
                    open(p, 'w', encoding='utf-8', newline='\n').write(t2)
    return write_docs(os.path.join(ROOT, 'docs'), os.path.join(out, 'docs'), scrub)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out'); ap.add_argument('--docs-only')
    ap.add_argument('--zip', help='also write the finished tree as a source package (the release asset rm8-port-src-<tag>.zip that rm8_extract --source accepts) and print its SHA-256')
    ap.add_argument('--corpus', action='append'); ap.add_argument('--game', action='append')
    ap.add_argument('--mincjk', type=int, default=4)
    ap.add_argument('--audit-mincjk', type=int, default=6, help='length of the original-text runs that fail the audit (shorter whole phrases that are game terms are kept)')
    a = ap.parse_args()
    corpora = a.corpus or [os.path.join(ROOT, 'out', 'extract_test', 'tc', 'scenes'), os.path.join(ROOT, 'out', 'extract_test', 'steam', 'scenes')]
    games = a.game or [r'<old-edition-dir>', r'<steam-edition-dir>']
    for c in corpora:
        if not os.path.isdir(c):
            sys.exit('corpus (an extraction\'s scenes folder) not found: %s' % c)
    alltext = '\n'.join(open(os.path.join(dp, f), encoding='utf-8', errors='replace').read()
                        for dp, _, fs in os.walk(os.path.join(ROOT, 'docs')) for f in fs if f.endswith('.md'))
    scrub = Scrubber(corpora, a.mincjk, alltext)
    if a.docs_only:
        if os.path.exists(a.docs_only):
            shutil.rmtree(a.docs_only)
        n = write_docs(os.path.join(ROOT, 'docs'), a.docs_only, scrub)
        print('%d doc files -> %s (%d quotes replaced)' % (n, a.docs_only, scrub.count))
        return 0
    if not a.out:
        sys.exit('--out or --docs-only is required')
    if os.path.exists(a.out) and os.listdir(a.out):
        sys.exit('--out must be new or empty')
    n = build(a.out, scrub)
    for g in ('GUIDE.md',):	# agent working notes of the minigame test bench
        gp = os.path.join(a.out, 'port', 'mgtest', g)
        if os.path.exists(gp): os.remove(gp)
    gi = os.path.join(a.out, '.gitignore')	# in the published tree docs/ is the de-quoted documentation: let git track it
    if os.path.exists(gi):
        t = open(gi, encoding='utf-8').read()
        if '!/docs/' not in t: open(gi, 'w', encoding='utf-8', newline='\n').write(t.replace('!/docs_public/\n', '!/docs_public/\n!/docs/\n', 1))
    ns, left = sanitize(a.out)
    print('sanitized dev-machine details in %d files; left: %d %s' % (ns, len(left), left[:5]))
    size = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(a.out) for f in fs)
    print('publish tree: %s (%d doc files, %d quotes replaced, %.1f MB)' % (a.out, n, scrub.count, size / 1e6))
    cmd = [sys.executable, '-I', os.path.join(HERE, 'audit_repo.py'), '--root', a.out, '--mincjk', str(a.audit_mincjk)]
    for g in games:
        if os.path.isdir(g):
            cmd += ['--game', g]
    for c in corpora:
        cmd += ['--corpus', c]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace')
    sys.stdout.write(r.stdout[-3000:])
    m1 = re.search(r'== 1\..*?: (\d+)', r.stdout); m2 = re.search(r'== 2\..*?: (\d+)', r.stdout); m3 = re.search(r'== 3\..*?: (\d+) files', r.stdout)
    bad = [int(m.group(1)) if m else -1 for m in (m1, m2, m3)]
    # forbidden things by name
    forb = [os.path.relpath(os.path.join(dp, f), a.out) for dp, _, fs in os.walk(a.out) for f in fs
            if f.lower().endswith(('.apk', '.dat', '.bik', '.mpg', '.keystore', '.dll', '.exe', '.pyc')) or f in ('manifest.json', 'extract.log')]
    print('audit: identical-to-game=%d derived-data=%d files-with-original-text=%d; forbidden file types=%d %s' % (bad[0], bad[1], bad[2], len(forb), forb[:5]))
    # every private path of PUBLISH.md must be absent; every download must be listed in THIRD_PARTY_NOTICES.md
    private = [d for d in ('private', 'mod', 'out', 'ghidra_proj', 'ghidra_scripts', 'scripts', 'tools', 'ref') if os.path.exists(os.path.join(a.out, d))]
    private += [os.path.join('docs', f) for f in sorted(PRIVATE_DOCS) if os.path.exists(os.path.join(a.out, 'docs', f))]
    private += [os.path.relpath(os.path.join(dp, d), a.out) for dp, dns, _ in os.walk(a.out) for d in dns if d in ('private', 'ghidra_proj', '.venv', 'build', 'port_scenes', '__pycache__')]
    print('private paths in the tree: %d %s' % (len(private), private[:5]))
    rn = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'check_notices.py'), '--root', a.out], capture_output=True, text=True, encoding='utf-8', errors='replace')
    sys.stdout.write(rn.stdout[-800:])
    private += [os.path.relpath(os.path.join(dp, d), a.out) for dp, dns, _ in os.walk(a.out) for d in dns if d == '__pycache__']      # nothing may have been generated inside the tree by the checks
    if a.zip:
        import zipfile, hashlib
        with zipfile.ZipFile(a.zip, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for dp, _, fs in os.walk(a.out):
                for f in sorted(fs):
                    p = os.path.join(dp, f)
                    z.write(p, os.path.relpath(p, a.out).replace('\\', '/'))
        print('source package: %s (%.1f MB) sha256 %s' % (a.zip, os.path.getsize(a.zip) / 1e6, hashlib.sha256(open(a.zip, 'rb').read()).hexdigest()))
    return 1 if any(b != 0 for b in bad) or forb or private or rn.returncode or left else 0


if __name__ == '__main__':
    sys.exit(main())
