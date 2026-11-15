"""Self-check: does an extraction contain every data file the port needs?  (so that a data file added to the app later cannot be forgotten)

  python -I tools\\check_app_data.py --out <extraction output folder> [--port D:\\ai\\rm8\\port]

1. runs port\\android\\data_files.py (the list android\\deploy.py / build_apk.py --with-data use) against the extraction and compares the file lists;
2. scans port\\app\\*.cpp / *.h for string literals that look like data file names and checks that each exists somewhere in the extraction
   (names on the IGNORE list are user data, test hooks or output files of the app itself)."""
import argparse, os, re, runpy, sys, collections

IGNORE = {'rm8_settings.txt', 'rm8_gamedate.bin', 'rm8_box.bin', 'camfile.txt', 'init.txt', 'gamedate.bin', 'manifest.txt', 'migrated_from_old_location.txt', 'rm8_env.txt'}      # user data, dev test hooks, the APK bundle's own manifest, a system font
IGNORE_RE = re.compile(r'^(menu_|movie_|shot|out|dump|mg\d*_|day)|%[0-9]*[sdgf]|^\.|\.(cpp|h|exe|apk|log|png|ttf|ttc|otf)$|^(Config|Sound|Music|Texture|Clump|Pysrc)$', re.I)
LIT = re.compile(r'"([A-Za-z0-9_./\\%-]+\.(?:txt|dat|ui|bin|pyc|mpg|ttf|ttc|png))"')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--port', default=os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'port'))
    a = ap.parse_args()
    game, scenes = os.path.join(a.out, 'game'), os.path.join(a.out, 'scenes')
    have = set()
    for root in (game, scenes):
        for dp, _, fs in os.walk(root):
            for f in fs:
                have.add(os.path.relpath(os.path.join(dp, f), a.out).replace('\\', '/'))
    bad = 0
    # 1) the deploy list
    df = os.path.join(a.port, 'android', 'data_files.py')
    if os.path.exists(df):
        mod = runpy.run_path(df)
        try:
            lst = mod['data_files'](game, scenes)
            dest = {d for _, d in lst}
        except SystemExit as e:
            print('FAIL  data_files.py: %s' % e); dest = set(); bad += 1
        miss = sorted(d for d in dest if d not in have)
        extra = sorted(h for h in have if h not in dest)
        print('data_files.py lists %d files; the extraction has %d' % (len(dest), len(have)))
        print('  in the app list but not extracted: %d %s' % (len(miss), miss[:10]))
        bydir = collections.Counter(os.path.dirname(e) or '.' for e in extra)
        print('  extracted but not in the app list: %d %s' % (len(extra), dict(bydir.most_common(8))))
        bad += len(miss) + len(extra)        # an extracted file that the deploy list does not know would never reach the phone / the APK
    else:
        print('SKIP  no data_files.py at %s' % df)
    # 2) names in the sources
    names = collections.defaultdict(set)
    for dp, _, fs in os.walk(os.path.join(a.port, 'app')):
        for f in fs:
            if f.endswith(('.cpp', '.h', '.inc')):
                for ln, line in enumerate(open(os.path.join(dp, f), encoding='utf-8', errors='replace'), 1):
                    code = line.split('//')[0]
                    for m in LIT.finditer(code):
                        names[os.path.basename(m.group(1).replace('\\', '/'))].add('%s:%d' % (f, ln))
    basenames = {os.path.basename(h) for h in have}
    unknown = {n: sorted(w)[:2] for n, w in names.items() if n not in basenames and n not in IGNORE and not IGNORE_RE.search(n)}
    print('source scan: %d distinct file-name literals; not found in the extraction:' % len(names))
    for n, w in sorted(unknown.items()):
        print('  ', n, w)
    bad += len(unknown)
    print('RESULT:', 'OK' if not bad else '%d problem(s)' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
