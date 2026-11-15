"""Copy the exporter scripts the extractor needs from D:\\ai\\rm8\\scripts (+ port\\tools\\make_t2s.py) into extractor\\exporters.

The exporters themselves are NOT modified or forked: this only snapshots them (with sha256 values in exporters\\MANIFEST.json) so that
a packaged extractor is reproducible and does not change while other people edit scripts\\.  The import closure is computed
from the root list below.   usage: python -I tools\\sync_exporters.py [--scripts DIR] [--check]
"""
import argparse, ast, hashlib, json, os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXT = os.path.dirname(HERE)
ROOT = os.path.dirname(EXT)

ROOTS = ['export_scene', 'export_ui', 'export_roles', 'export_particles', 'export_cards', 'export_stocks', 'export_npcs',
         'export_stages', 'export_news', 'export_questions', 'export_introduce', 'export_event_arts', 'export_minigame', 'export_story', 'convert_videos',
         'export_save_big5', 'export_save_terra_order', 'export_wonders']
NOT_EXTRACTED = set()      # export_* scripts that are deliberately not part of the extraction
EXTRA = {'make_t2s.py': os.path.join(ROOT, 'port', 'tools', 'make_t2s.py')}


def closure(scripts):
    local = {f[:-3] for f in os.listdir(scripts) if f.endswith('.py')}
    seen, todo = [], list(ROOTS)
    while todo:
        m = todo.pop()
        if m in seen:
            continue
        seen.append(m)
        t = ast.parse(open(os.path.join(scripts, m + '.py'), encoding='utf-8').read())
        for n in ast.walk(t):
            names = []
            if isinstance(n, ast.Import): names = [a.name for a in n.names]
            elif isinstance(n, ast.ImportFrom) and n.module: names = [n.module]
            for x in names:
                x = x.split('.')[0]
                if x in local and x not in seen:
                    todo.append(x)
    return sorted(seen)


def sha(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--scripts', default=os.path.join(ROOT, 'scripts'))
    ap.add_argument('--check', action='store_true', help='only report whether extractor\\exporters is up to date')
    a = ap.parse_args()
    dst = os.path.join(EXT, 'exporters')
    files = {m + '.py': os.path.join(a.scripts, m + '.py') for m in closure(a.scripts)}
    files.update(EXTRA)
    # every scripts/export_*.py must be wired in (ROOTS) or listed as deliberately not part of the extraction (NOT_EXTRACTED)
    unwired = sorted(f for f in os.listdir(a.scripts) if f.startswith('export_') and f.endswith('.py') and f[:-3] not in ROOTS and f[:-3] not in NOT_EXTRACTED)
    if unwired:
        print('NEW exporter scripts not wired into the extractor (add to ROOTS in sync_exporters.py and to pipeline.py, or to NOT_EXTRACTED): ' + ', '.join(unwired))
        if a.check:
            return 1
    stale = [n for n, p in files.items() if not os.path.exists(os.path.join(dst, n)) or sha(p) != sha(os.path.join(dst, n))]
    if a.check:
        print('up to date' if not stale else 'stale: ' + ', '.join(stale)); return 1 if stale else 0
    os.makedirs(dst, exist_ok=True)
    for n, p in sorted(files.items()):
        shutil.copyfile(p, os.path.join(dst, n))
    man = {n: {'sha256': sha(os.path.join(dst, n)), 'source': os.path.relpath(p, ROOT).replace('\\', '/')} for n, p in sorted(files.items())}
    json.dump(man, open(os.path.join(dst, 'MANIFEST.json'), 'w', encoding='utf-8', newline='\n'), indent=1, sort_keys=True)
    print('synced %d files (%d changed) -> %s' % (len(files), len(stale), dst))
    return 0


if __name__ == '__main__':
    sys.exit(main())
