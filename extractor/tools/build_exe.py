"""Build the standalone Windows executable of the extractor with PyInstaller (one file, no Python needed on the user's PC).

  python -I tools\\build_exe.py [--venv build\\venv]        output: build\\dist\\rm8_extract.exe  (+ build\\dist\\SHA256SUMS.txt)

The build venv (extractor\\build\\venv) is separate from the project .venv: python -m venv build\\venv, then
  uv pip install --python build\\venv\\Scripts\\python.exe numpy==2.5.4 pillow==13.0.0 pyinstaller
The exporter scripts are bundled as data files (exporters\\) and run through runpy from a temp copy, so their imports are not visible to
PyInstaller; the list below is computed from them (ast) and passed as hidden imports."""
import argparse, ast, hashlib, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXT = os.path.dirname(HERE)


def third_party_imports():
    local = {f[:-3] for f in os.listdir(os.path.join(EXT, 'exporters')) if f.endswith('.py')}
    mods = set()
    for f in os.listdir(os.path.join(EXT, 'exporters')):
        if not f.endswith('.py'):
            continue
        t = ast.parse(open(os.path.join(EXT, 'exporters', f), encoding='utf-8').read())
        for n in ast.walk(t):
            if isinstance(n, ast.Import):
                mods.update(a.name for a in n.names)
            elif isinstance(n, ast.ImportFrom) and n.module:
                mods.add(n.module)
                mods.update(n.module + '.' + a.name for a in n.names)      # 'from PIL import ImageDraw' needs the submodule
    stdlib_names = set(sys.stdlib_module_names)
    return sorted(m for m in mods if m.split('.')[0] not in local and not (m.split('.')[0] in stdlib_names and m.count('.') > 0 and m.rsplit('.', 1)[1][:1].islower() and m.split('.')[0] in ('os', 'sys', 'math', 'struct', 're', 'json', 'collections')))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--venv', default=os.path.join(EXT, 'build', 'venv'))
    ap.add_argument('--dist', default=os.path.join(EXT, 'build', 'dist'), help='output folder (another one when the old exe is still running)')
    a = ap.parse_args()
    chk = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'sync_exporters.py'), '--check'], capture_output=True, text=True)
    if chk.returncode:
        sys.exit('exporters\\ is stale (%s): run tools\\sync_exporters.py and tools\\refresh_references.py first' % chk.stdout.strip())
    py = os.path.join(a.venv, 'Scripts', 'python.exe')
    hidden = third_party_imports() + ['PIL.Image', 'PIL.PngImagePlugin', 'PIL.TgaImagePlugin', 'PIL.BmpImagePlugin', 'tkinter', 'tkinter.filedialog']
    cmd = [py, '-m', 'PyInstaller', '--noconfirm', '--clean', '--onefile', '--console', '--name', 'rm8_extract',
           '--distpath', a.dist, '--workpath', os.path.join(EXT, 'build', 'work'), '--specpath', os.path.join(EXT, 'build'),
           '--paths', EXT,
           '--add-data', os.path.join(EXT, 'data') + ';data', '--add-data', os.path.join(EXT, 'exporters') + ';exporters']
    for m in sorted(set(hidden)):
        cmd += ['--hidden-import', m]
    cmd.append(os.path.join(EXT, 'rm8_extract.py'))
    print(' '.join(cmd[:12]), '... (%d hidden imports)' % len(set(hidden)))
    r = subprocess.run(cmd)
    if r.returncode:
        return r.returncode
    exe = os.path.join(a.dist, 'rm8_extract.exe')
    h = hashlib.sha256(open(exe, 'rb').read()).hexdigest()
    open(os.path.join(a.dist, 'SHA256SUMS.txt'), 'w').write('%s  rm8_extract.exe\n' % h)
    print('built %s (%.1f MB) sha256 %s' % (exe, os.path.getsize(exe) / 1e6, h))
    return 0


if __name__ == '__main__':
    sys.exit(main())
