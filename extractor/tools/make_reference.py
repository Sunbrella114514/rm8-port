"""Record the reference data of a known-good install + its extraction (run on the dev machine, commit the JSON files).

  python -I tools\\make_reference.py --id tc --name "..." --exe-note "..." --game <install> --extracted <output of rm8_extract.py --no-reference --no-video --no-copy-game>
        [--canon-from data\\references\\tc.json]     (use the file name spelling of an earlier reference where names equal case-insensitively)

Writes data\\references\\<id>.json (sizes + sha256 of every file the extractor reads/copies, plus identifying info files) and
data\\references\\<id>_outputs.json (line counts of the derived text files, the map list, resource-reference statistics)."""
import argparse, datetime, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXT = os.path.dirname(HERE)
sys.path.insert(0, EXT)
from rm8x.util import sha256_file  # noqa: E402
from rm8x import validate  # noqa: E402

INFO_FILES = ['RichMan8.exe', 'Python24.dll', 'Python27.dll', 'Richman.dll', 'Binkw32.dll', 'Mss32.dll', 'Msvcr71.dll', 'GameInfo.inf']
MUTABLE = ['Config/init.txt', 'Config/gamedate.bin']
RAW_PACKS = ['Clump/clump0000.dat', 'Texture/tex0000.dat', 'Texture/tex0001.dat', 'Sound/snd0000.dat', 'Music/mus0000.dat']
VIDEO = ['Bink/Start.bik', 'Bink/Staff.bik']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--id', required=True)
    ap.add_argument('--name', required=True)
    ap.add_argument('--exe-note', default='')
    ap.add_argument('--status', default='supported', choices=['primary', 'supported', 'legacy'], help='primary = the edition we support first (Steam); legacy = kept working for now')
    ap.add_argument('--game', required=True)
    ap.add_argument('--extracted', required=True)
    ap.add_argument('--canon-from', action='append', default=[])
    a = ap.parse_args()
    game = a.game
    manifest = json.load(open(os.path.join(a.extracted, 'manifest.json'), encoding='utf-8'))
    canon = {}
    for c in a.canon_from:
        for rel in json.load(open(c, encoding='utf-8'))['files']:
            canon[rel.lower()] = rel
    actual = {}
    for dp, _, fs in os.walk(game):
        for f in fs:
            rel = os.path.relpath(os.path.join(dp, f), game).replace('\\', '/')
            actual[rel.lower()] = rel
    name_of = lambda low: canon.get(low.lower(), actual[low.lower()])
    core = set(name_of(r) for r in manifest['game_files_read_by_exporters']) | set(name_of(r) for r in RAW_PACKS)
    pys = sorted(name_of(v) for k, v in actual.items() if k.startswith('pysrc/') and k.endswith('.pyc'))
    core.update(pys)
    files = {}
    def add(rel, role):
        src = actual[rel.lower()]
        files[rel] = {'size': os.path.getsize(os.path.join(game, src)), 'sha256': sha256_file(os.path.join(game, src)), 'role': role}
        print(role, rel)
    for rel in sorted(core): add(rel, 'core')
    for rel in VIDEO: add(name_of(rel), 'video')
    for rel in INFO_FILES:
        if rel.lower() in actual: add(name_of(rel), 'info')
    for rel in MUTABLE:
        if rel.lower() in actual: files[name_of(rel)] = {'size': 0, 'sha256': '', 'role': 'mutable'}
    ref = {
        'id': a.id, 'edition': a.id, 'name': a.name, 'exe_note': a.exe_note, 'status': a.status,
        'recorded': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d'),
        'raw_copy': [name_of(r) for r in RAW_PACKS] + pys,
        'files': files,
    }
    d = os.path.join(EXT, 'data', 'references')
    os.makedirs(d, exist_ok=True)
    json.dump(ref, open(os.path.join(d, a.id + '.json'), 'w', encoding='utf-8', newline='\n'), ensure_ascii=False, indent=1, sort_keys=True)
    lc = {}
    sc = os.path.join(a.extracted, 'scenes')
    for dp, _, fs in os.walk(sc):
        for f in fs:
            if f.endswith('.txt'):
                p = os.path.join(dp, f)
                lc[os.path.relpath(p, a.extracted).replace('\\', '/')] = validate.line_count(p)
    outs = {'maps': manifest['maps'], 'line_counts': lc, 'counts': {}, 'exporters_digest': manifest['exporters_digest']}
    class L:
        info = warn = detail = staticmethod(print)
    r = validate.validate_output(a.extracted, game, manifest['game_files'], manifest['maps'], {'status': 'skipped'}, L(), copied=False, strict=False)
    outs['counts'] = {k: v for k, v in r['counts'].items() if not k.startswith('scenes/')}
    json.dump(outs, open(os.path.join(d, a.id + '_outputs.json'), 'w', encoding='utf-8', newline='\n'), indent=1, sort_keys=True)
    print('wrote reference %s: %d install files, %d text outputs, counts %s' % (a.id, len(files), len(lc), outs['counts']))


if __name__ == '__main__':
    main()
