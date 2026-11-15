#!/usr/bin/env python3
"""Download the third-party dependencies of the port from their official sources, verify them, and set them up in port/extern/<name>.

Standard library only (Python 3.8+).  Nothing here is committed: port/extern/ is git-ignored except this script and its README; our own
files that used to live inside extern/ are tracked in port/deps_local/ and copied in (cpython24).

  python extern/fetch_deps.py            fetch whatever is missing or does not match the pins (downloads are cached in extern/.cache)
  python extern/fetch_deps.py --force    fetch everything again
  python extern/fetch_deps.py --verify   only check what is there against the pins (no network)
  python extern/fetch_deps.py --offline  use extern/.cache only
  python extern/fetch_deps.py --record   print the pins of the current download (maintainers: after changing a version below)

Every download is checked twice: the SHA-256 of the file itself (where the publisher serves a stable file; GitHub's generated source
archives are not byte-stable, so those are pinned by commit and checked by the hash of the extracted tree), and the hash of the final tree
(sorted "relative path <TAB> sha256" lines, hashed).  A mismatch deletes the download and stops.  Archives are unpacked member by member
(no absolute paths, no "..", regular files only).

What each entry is and its license: see THIRD_PARTY_NOTICES.md at the repository root.
"""
import argparse, hashlib, io, json, os, shutil, subprocess, sys, tarfile, time, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))           # port/extern
PORT = os.path.dirname(HERE)
CACHE = os.environ.get('RM8_DEPS_CACHE') or os.path.join(HERE, '.cache')      # RM8_DEPS_CACHE: share the download cache between builds
MARK = '.fetched.json'

GH = 'https://raw.githubusercontent.com/'
DEPS = [
    {'name': 'SDL2-2.32.10', 'kind': 'archive', 'strip': 1,
     'url': 'https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.zip',
     'sha256': '12b2dc2eb8f2836100a7916b5d394a0c82f1f7e32693f95f98305403af242f08',
     'tree': '52767e534c9cce84d1cc767ecb6e5afaa287d1da53efd8f0c4f11c07874d5446', 'license': 'zlib'},
    {'name': 'glfw-3.5.1', 'kind': 'archive', 'strip': 1,
     'url': 'https://github.com/glfw/glfw/releases/download/3.5.1/glfw-3.5.1.zip',
     'sha256': 'ea79bc5feffc254c87291980c2d0bce9acebb68c4983b79f961dcd2cb8a611a0',
     'tree': 'd78658ba11675377d02dd32b4ed8b4912f9b2f3d882bccfea880cf63820dfbe8', 'license': 'zlib'},
    {'name': 'librw', 'kind': 'archive', 'strip': 1,
     'url': 'https://github.com/aap/librw/archive/18532c2e13efbc1aa43f0b00b4459831f9414de0.zip',
     'sha256': None, 'tree_pristine': '9298a4787a568cd13eb699a0f84f82edfe9c74629ef494109b3d0fbdca3b784d',
     'patch': ['patches', 'apply_librw_android.py'],
     'tree': 'fabb8841c023aaf8a09bca760560e763b166e7ebdfdb722fae713df5264238b6', 'license': 'MIT'},
    {'name': 'cpython24', 'kind': 'archive', 'strip': 1,
     'url': 'https://www.python.org/ftp/python/2.4.6/Python-2.4.6.tgz',
     'sha256': 'b03f269e826927f05c966cf4f4414f3c93ee2314960859e7f8375e24e82f8b02',      # python.org lists md5 7564b2b142b1b8345cd5358b7aaaa482 for this file
     'only_list': ['deps_local', 'cpython24', 'files.txt'], 'overlay': ['deps_local', 'cpython24'],
     'tree': '006504d8f662b2298c8f8755e87a5458532a88b10abefe26593986063ecdfc9f', 'license': 'PSF'},
    {'name': 'dr_libs', 'kind': 'files', 'license': 'public domain / MIT-0 (contains minimp3, CC0)', 'tree': '8f0f0269c6e5a19a423da741c5b11f29e62d3a93ce77dea6e4d44e63a41d3de9',
     'files': [('dr_mp3.h', GH + 'mackron/dr_libs/cfdfbf6bb7271a792030ebbb6c2a15c3f2fd001e/dr_mp3.h',
                '7a6cc38b0afdb55cfc9eec991d9169d67beb8fd910be2995121510cdaa99c356')]},
    {'name': 'pl_mpeg', 'kind': 'files', 'license': 'MIT', 'tree': 'f70f5b09b3b039add53d3b52925c099b37eb9154f585c53072dada21766ebffc',
     'files': [('pl_mpeg.h', GH + 'phoboslab/pl_mpeg/c871f2be022ece7ef4f64230b4fb8e1fb9eb6023/pl_mpeg.h',
                '3a8cb30c83c2a1147719c30fe0c8b93da2987aa43140077c575b39aaa75fc2c9')]},
    {'name': 'stb', 'kind': 'files', 'license': 'public domain / MIT', 'tree': '0a524bcffd25c5803a6fcb66978f6a9a495e641a942a8c3e13f13dae47811f81',
     'files': [('stb_truetype.h', GH + 'nothings/stb/9f755b604db3492df2cbeb39bf84b81e3e3932e3/stb_truetype.h',
                '428cfe93520f2aed5e413a220b0db5bba5b17e2ebe5a64e4a8efa9eeeb67ec5f')]},
    {'name': 'opencc_dict', 'kind': 'files', 'license': 'Apache-2.0', 'tree': 'a82c6678d03e341f672712f5a5f77d2437eb193b443dba4ef896a12855399fd9',
     'files': [('TSCharacters.txt', GH + 'BYVoid/OpenCC/5c764a5a886f46eb365656ed91a0410d7162fac6/data/dictionary/TSCharacters.txt',
                '9ff46a7d30e5765375eb13d33f2b03a34d298913caf2b120380679f33ae1642d'),
               ('TSPhrases.txt', GH + 'BYVoid/OpenCC/9e9d730659fc7db366b43b4437b307444d87e23e/data/dictionary/TSPhrases.txt',
                '1b1e60a3d50492bc088bcd4372152570b5f0ccbe2ded0b66c90b6b8eeb5f849f'),
               ('LICENSE', GH + 'BYVoid/OpenCC/c56f6c73281a5e70a46938d1e7f7ca2ec8818a3f/LICENSE',
                'b534e465949558eec2597b04f5092b5e161236a68dfbfd04d547592ac3964308')]},
]


class FetchError(Exception):
    pass


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


def sha256_file(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def tree_hash(root):
    lines = []
    for dp, dns, fs in os.walk(root):
        dns[:] = [d for d in dns if d != '.git']
        for f in fs:
            if f == MARK:
                continue
            p = os.path.join(dp, f)
            lines.append('%s\t%s' % (os.path.relpath(p, root).replace('\\', '/'), sha256_file(p)))
    return hashlib.sha256('\n'.join(sorted(lines)).encode('utf-8')).hexdigest()


def download(url, want_sha, offline, label):
    """Return the bytes of url (cached in extern/.cache by file name + pin); verifies want_sha when given."""
    os.makedirs(CACHE, exist_ok=True)
    key = hashlib.sha256(url.encode()).hexdigest()[:12] + '_' + os.path.basename(url)
    path = os.path.join(CACHE, key)
    if os.path.exists(path):
        data = open(path, 'rb').read()
        if want_sha is None or sha256_bytes(data) == want_sha:
            return data
        os.remove(path)
    if offline:
        raise FetchError('%s: not in the cache and --offline given' % label)
    print('  downloading %s' % url, flush=True)
    err = None
    data = None
    curl = shutil.which('curl')           # every Windows 10/11 has curl.exe; on some machines it is many times faster than urllib
    if curl:
        tmpf = path + '.curl'
        r = subprocess.run([curl, '-sSL', '--fail', '--retry', '5', '--retry-delay', '3', '-A', 'rm8-fetch-deps', '-o', tmpf, url], capture_output=True, text=True)
        if r.returncode == 0 and os.path.exists(tmpf):
            data = open(tmpf, 'rb').read()
        if os.path.exists(tmpf):
            os.remove(tmpf)
    for attempt in range(5 if data is None else 0):
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'rm8-fetch-deps'})
            with urllib.request.urlopen(req, timeout=120) as r:
                data = r.read()
            break
        except Exception as e:           # network hiccups / 502 from the CDN
            err = e
            time.sleep(3 * (attempt + 1))
    if data is None:
        raise FetchError('%s: download failed: %s' % (label, err))
    if want_sha is not None and sha256_bytes(data) != want_sha:
        raise FetchError('%s: SHA-256 mismatch (got %s, pinned %s); download discarded' % (label, sha256_bytes(data), want_sha))
    with open(path, 'wb') as f:
        f.write(data)
    return data


def safe_rel(name, strip):
    parts = [p for p in name.replace('\\', '/').split('/') if p not in ('', '.')]
    if len(parts) <= strip:
        return None
    parts = parts[strip:]
    if any(p == '..' or ':' in p for p in parts):
        raise FetchError('unsafe path in archive: %r' % name)
    return '/'.join(parts)


def unpack(data, url, dest, strip, only):
    n = 0
    if url.endswith('.zip'):
        z = zipfile.ZipFile(io.BytesIO(data))
        items = [(i.filename, (lambda i=i: z.read(i))) for i in z.infolist() if not i.filename.endswith('/')]
    else:
        t = tarfile.open(fileobj=io.BytesIO(data))
        items = [(m.name, (lambda m=m: t.extractfile(m).read())) for m in t.getmembers() if m.isfile()]
    for name, reader in items:
        rel = safe_rel(name, strip)
        if rel is None or (only is not None and rel not in only):
            continue
        out = os.path.join(dest, *rel.split('/'))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, 'wb') as f:
            f.write(reader())
        n += 1
    return n


def run_script(script, args):
    """Run a Python script and return (exit code, output).  A frozen executable (rm8_extract.exe calls this module in-process) has no
    interpreter to start, so the script is executed with runpy instead."""
    if getattr(sys, 'frozen', False):
        import contextlib, runpy
        buf, old_argv = io.StringIO(), sys.argv
        sys.argv = [script] + list(args)
        rc = 0
        try:
            with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
                runpy.run_path(script, run_name='__main__')
        except SystemExit as e:
            rc = e.code if isinstance(e.code, int) else (0 if e.code is None else 1)
        except BaseException as e:
            rc = 1
            buf.write(repr(e))
        finally:
            sys.argv = old_argv
        return rc, buf.getvalue()
    r = subprocess.run([sys.executable, script] + list(args), capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def fetch_one(d, a):
    name = d['name']
    dest = os.path.join(HERE, name)
    pins = json.dumps({k: d.get(k) for k in ('url', 'sha256', 'tree', 'tree_pristine', 'files')}, sort_keys=True, default=str)
    marker = os.path.join(dest, MARK)
    if not a.force and os.path.exists(marker) and open(marker).read() == pins and not a.record:
        print('%-14s up to date' % name)
        return {}
    tmp = dest + '.part'
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    rec = {}
    try:
        if d['kind'] == 'archive':
            data = download(d['url'], d['sha256'], a.offline, name)
            rec['sha256'] = sha256_bytes(data)
            only = None
            if d.get('only_list'):
                only = set(l.strip() for l in open(os.path.join(PORT, *d['only_list']), encoding='utf-8') if l.strip())
            n = unpack(data, d['url'], tmp, d['strip'], only)
            if only is not None and n != len(only):
                raise FetchError('%s: expected %d files from the archive, found %d' % (name, len(only), n))
            if d.get('tree_pristine') or a.record:
                rec['tree_pristine'] = tree_hash(tmp)
                if d.get('tree_pristine') and rec['tree_pristine'] != d['tree_pristine'] and not a.record:
                    raise FetchError('%s: unpacked tree does not match the pin (%s)' % (name, rec['tree_pristine']))
            if d.get('patch'):
                script = os.path.join(PORT, *d['patch'])
                rc, out = run_script(script, [tmp])
                if rc:
                    raise FetchError('%s: patch script failed:\n%s' % (name, out[-3000:]))
                print('  patched %s with %s' % (name, d['patch'][-1]))
            if d.get('overlay'):
                src = os.path.join(PORT, *d['overlay'])
                for f in sorted(os.listdir(src)):
                    if f != 'files.txt':
                        shutil.copyfile(os.path.join(src, f), os.path.join(tmp, f))
                print('  added our own files from %s: %s' % (os.path.relpath(src, PORT), ', '.join(f for f in sorted(os.listdir(src)) if f != 'files.txt')))
        else:
            for fname, url, sha in d['files']:
                data = download(url, sha, a.offline, '%s/%s' % (name, fname))
                with open(os.path.join(tmp, fname), 'wb') as f:
                    f.write(data)
                rec[fname] = sha256_bytes(data)
        rec['tree'] = tree_hash(tmp)
        if d.get('tree') and rec['tree'] != d['tree'] and not a.record:
            raise FetchError('%s: final tree does not match the pin (got %s, pinned %s)' % (name, rec['tree'], d['tree']))
        with open(os.path.join(tmp, MARK), 'w') as f:
            f.write(pins)
        shutil.rmtree(dest, ignore_errors=True)
        os.rename(tmp, dest)
        print('%-14s ok  (%s, tree %s)' % (name, d['license'], rec['tree'][:12]))
        return rec
    except Exception:
        shutil.rmtree(tmp, ignore_errors=True)
        raise


def verify(a):
    bad = 0
    for d in DEPS:
        dest = os.path.join(HERE, d['name'])
        if not os.path.isdir(dest):
            print('%-14s MISSING' % d['name']); bad += 1; continue
        t = tree_hash(dest)
        ok = (d.get('tree') is None) or t == d['tree']
        print('%-14s %s (tree %s)' % (d['name'], 'ok' if ok else 'MISMATCH', t[:12]))
        bad += 0 if ok else 1
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--force', action='store_true'); ap.add_argument('--verify', action='store_true')
    ap.add_argument('--offline', action='store_true'); ap.add_argument('--record', action='store_true')
    a = ap.parse_args()
    if a.verify:
        return 1 if verify(a) else 0
    recs = {}
    try:
        for d in DEPS:
            recs[d['name']] = fetch_one(d, a)
    except FetchError as e:
        print('ERROR:', e, file=sys.stderr)
        return 1
    if a.record:
        print(json.dumps(recs, indent=1))
    else:
        print('\nAll dependencies are in place under %s.' % HERE)
    return 0


if __name__ == '__main__':
    sys.exit(main())
