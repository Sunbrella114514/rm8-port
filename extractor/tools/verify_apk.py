"""Check a personal APK made by rm8_extract: structure, our own v2 verification and (when the Android SDK build-tools are available) the real
zipalign / apksigner / aapt2.

  python -I verify_apk.py <apk> [--data-dir <extraction output folder>] [--build-tools <dir>] [--jdk <dir>]

With --data-dir every file listed in assets/rm8data/manifest.txt is also compared (SHA-256) with the extraction folder."""
import argparse, hashlib, os, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from rm8x import apk as apkmod  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('apk'); ap.add_argument('--data-dir'); ap.add_argument('--build-tools'); ap.add_argument('--jdk')
    a = ap.parse_args()
    bad = 0
    z = zipfile.ZipFile(a.apk)
    print('CRC of every entry:', 'ok' if z.testzip() is None else 'BAD')
    names = set(z.namelist())
    for need in ('AndroidManifest.xml', 'classes.dex', 'lib/arm64-v8a/libmain.so', 'lib/arm64-v8a/libSDL2.so', 'assets/rm8data/manifest.txt'):
        if need not in names:
            print('MISSING', need); bad += 1
    lines = [l for l in z.read('assets/rm8data/manifest.txt').decode().split('\n') if l]
    print(lines[0], '-', len(lines) - 1, 'files listed;', sum(1 for l in lines if '	import_saves/' in l), 'migrated save / profile files (import_saves/)')
    for l in lines[1:]:
        size, rel = l.split('\t')
        zi = z.getinfo('assets/rm8data/' + rel) if 'assets/rm8data/' + rel in names else None
        if zi is None or zi.file_size != int(size):
            print('BAD entry', rel); bad += 1
        elif a.data_dir and rel != 'manifest.json':
            ref = os.path.join(a.data_dir, 'saves', rel[len('import_saves/'):]) if rel.startswith('import_saves/') else os.path.join(a.data_dir, rel)      # migrated saves are staged in <out>/saves
            if hashlib.sha256(z.read(zi)).hexdigest() != hashlib.sha256(open(ref, 'rb').read()).hexdigest():
                print('DIFFERENT from the extraction:', rel); bad += 1
    if any(n.startswith('META-INF/') for n in names):
        print('old v1 signature files are still inside'); bad += 1
    try:
        cert = apkmod.verify_v2(a.apk)
        print('own v2 verification: ok, certificate SHA-256', apkmod.rsa.fingerprint(cert)[:23] + '...')
    except apkmod.ApkError as e:
        print('own v2 verification FAILED:', e); bad += 1
    bt = os.path.abspath(a.build_tools) if a.build_tools else os.path.join(r'<tools>\android-sdk', 'build-tools', '36.1.0')
    if os.path.isdir(bt):
        env = dict(os.environ)
        jdk = a.jdk or r'<tools>\jdk-21.0.13+9'
        if os.path.isdir(jdk):
            env['JAVA_HOME'] = jdk; env['PATH'] = os.path.join(jdk, 'bin') + os.pathsep + env['PATH']
        for cmd in ([os.path.join(bt, 'zipalign.exe'), '-c', '-P', '16', '4', a.apk],
                    [os.path.join(bt, 'apksigner.bat'), 'verify', '--verbose', '--min-sdk-version', '24', a.apk]):
            r = subprocess.run(cmd, capture_output=True, text=True, env=env)
            print('%s rc=%d %s' % (os.path.basename(cmd[0]), r.returncode, ' | '.join(r.stdout.strip().splitlines()[:3])))
            bad += 1 if r.returncode else 0
    print('RESULT:', 'OK' if not bad else '%d problem(s)' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
