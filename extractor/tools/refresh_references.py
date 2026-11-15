"""Re-record data\\references\\*.json after the exporter scripts changed (they keep growing: new data files, new files read from the game).

  python -I tools\\refresh_references.py --tc <old Big5 install> --sc <Steam install> --work <empty scratch folder>

For every install: run the extractor with --no-reference (no video, nothing copied) and record the reference from the result.
Run tools\\sync_exporters.py first.  The reference then holds: sha256 of every file the exporters read, and the derived outputs'
line counts for exactly this set of exporter scripts (manifest 'exporters_digest')."""
import argparse, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXT = os.path.dirname(HERE)

# id -> (name, exe note, status).  Add a line here (and a --<id> option below) for a new edition, see data\references\README.md.
NAMES = {'sc': ('大富翁8 简体中文版（Steam 正版，GBK）', 'Richman8.exe（Steam 版，2,631,384 字节，带 python27.dll）', 'primary'),
         'tc': ('大富翁8 繁体中文版（旧版安装，Big5；开发参考，将换成干净的旧版）', 'RichMan8.exe FileVersion 1.0.0.1117，编译时间 2008-01-11', 'legacy')}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tc'); ap.add_argument('--sc'); ap.add_argument('--work', required=True)
    a = ap.parse_args()
    py = [sys.executable, '-I']
    canon = []
    for ed in ('tc', 'sc'):         # tc first: sc takes its file-name spelling (canonical case) from it
        game = getattr(a, ed)
        if not game:
            continue
        out = os.path.join(a.work, ed)
        subprocess.run(py + [os.path.join(EXT, 'rm8_extract.py'), game, out, '--no-reference', '--no-video', '--no-copy-game', '--quick'], check=True)
        name, note, status = NAMES[ed]
        cmd = py + [os.path.join(HERE, 'make_reference.py'), '--id', ed, '--name', name, '--exe-note', note, '--status', status, '--game', game, '--extracted', out]
        for c in canon:
            cmd += ['--canon-from', c]
        subprocess.run(cmd, check=True)
        canon.append(os.path.join(EXT, 'data', 'references', ed + '.json'))


if __name__ == '__main__':
    main()
