"""Package the Android build into an APK without Gradle: javac -> d8 -> aapt2 link -> add dex/.so -> zipalign -> apksigner.
usage: python -I build_apk.py [--build build-android] [--out rm8_viewer.apk]
Paths of the SDK/JDK default to <tools> (see port/README.md)."""
import os, sys, glob, shutil, subprocess, zipfile, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
ap = argparse.ArgumentParser()
ap.add_argument('--build', default=os.path.join(PORT, 'build-android'))
ap.add_argument('--out', default=None, help='default rm8_viewer.apk; rm8_personal_DO_NOT_SHARE.apk with --with-data (it holds the game data: personal use only)')
ap.add_argument('--sdk', default=r'<tools>\android-sdk')
ap.add_argument('--jdk', default=r'<tools>\jdk-21.0.13+9')
ap.add_argument('--abi', default='arm64-v8a')
ap.add_argument('--with-data', metavar='GAME_DIR', help='bundle the game data + scenes into assets/rm8data (unpacked on the first start)')
ap.add_argument('--scenes', default=os.path.join(PORT, 'scenes'))
ap.add_argument('--release', action='store_true', help='sign with the private release key (<workspace>/private, never published) instead of the debug key')
a = ap.parse_args()
if not a.out: a.out = os.path.join(PORT, 'rm8_personal_DO_NOT_SHARE.apk' if a.with_data else 'rm8_viewer.apk')

BT = os.path.join(a.sdk, 'build-tools', '36.1.0')
JAR = os.path.join(a.sdk, 'platforms', 'android-35', 'android.jar')
NDKBIN = os.path.join(a.sdk, 'ndk', 'r29', 'toolchains', 'llvm', 'prebuilt', 'windows-x86_64', 'bin')
SDLJAVA = os.path.join(PORT, 'extern', 'SDL2-2.32.10', 'android-project', 'app', 'src', 'main', 'java')
env = dict(os.environ, JAVA_HOME=a.jdk, PATH=os.path.join(a.jdk, 'bin') + os.pathsep + os.environ['PATH'])
tmp = os.path.join(a.build, 'apk_tmp')
shutil.rmtree(tmp, ignore_errors=True); os.makedirs(tmp)

def run(*cmd):
    print('>', ' '.join(os.path.basename(c) if i == 0 else c for i, c in enumerate(cmd))[:200])
    r = subprocess.run(list(cmd), env=env, capture_output=True, text=True, encoding='utf-8', errors='replace')
    if r.returncode != 0:
        print(r.stdout[-4000:], r.stderr[-4000:]); raise SystemExit('FAILED: ' + cmd[0])
    return r.stdout

# 1) Java -> classes -> dex
srcs = glob.glob(os.path.join(SDLJAVA, 'org', 'libsdl', 'app', '*.java')) + glob.glob(os.path.join(HERE, 'src', '**', '*.java'), recursive=True)
cls = os.path.join(tmp, 'classes'); os.makedirs(cls)
run(os.path.join(a.jdk, 'bin', 'javac.exe'), '--release', '11', '-nowarn', '-encoding', 'UTF-8', '-cp', JAR, '-d', cls, *srcs)
classfiles = glob.glob(os.path.join(cls, '**', '*.class'), recursive=True)
dexdir = os.path.join(tmp, 'dex'); os.makedirs(dexdir)
run(os.path.join(BT, 'd8.bat'), '--release', '--min-api', '24', '--lib', JAR, '--output', dexdir, *classfiles)

# 2) manifest -> base apk
base = os.path.join(tmp, 'base.apk')
run(os.path.join(BT, 'aapt2.exe'), 'link', '-o', base, '-I', JAR, '--manifest', os.path.join(HERE, 'AndroidManifest.xml'),
    '--min-sdk-version', '24', '--target-sdk-version', '35')

# 3) add dex + stripped .so (stored, so they can be page aligned)
libs = {'libSDL2.so': os.path.join(a.build, 'sdl2', 'libSDL2.so'), 'libmain.so': os.path.join(a.build, 'libmain.so')}
unsigned = os.path.join(tmp, 'unsigned.apk')
with zipfile.ZipFile(base) as zin, zipfile.ZipFile(unsigned, 'w') as zout:
    for item in zin.infolist():
        zout.writestr(item, zin.read(item.filename))
    zout.write(os.path.join(dexdir, 'classes.dex'), 'classes.dex', compress_type=zipfile.ZIP_DEFLATED)
    for name, path in libs.items():
        stripped = os.path.join(tmp, name)
        if name == 'libmain.so':	# keep the unstripped library by build id (symbolizing phone crash backtraces); out/ is private
            try:
                bid = run(os.path.join(NDKBIN, 'llvm-readelf.exe'), '-n', path).split('Build ID:')[1].split()[0]
                sym = os.path.join(os.path.dirname(os.path.dirname(PORT)), 'rm8', 'out', 'symbols')
                os.makedirs(sym, exist_ok=True); shutil.copy2(path, os.path.join(sym, 'libmain_%s.so' % bid))
            except Exception as e: print('symbols not kept:', e)
        run(os.path.join(NDKBIN, 'llvm-strip.exe'), '--strip-unneeded', '-o', stripped, path)
        zout.write(stripped, 'lib/%s/%s' % (a.abi, name), compress_type=zipfile.ZIP_STORED)
    if a.with_data:	# [A] a self-contained test APK: assets/rm8data/<rel> + manifest (version, size, path); app/rm8_app.cpp unpacks it
        sys.path.insert(0, HERE); import data_files, hashlib
        files = data_files.data_files(a.with_data, a.scenes)
        ver = hashlib.sha1(''.join('%s %d %d' % (r, os.path.getsize(l), int(os.path.getmtime(l))) for l, r in files).encode()).hexdigest()[:16]
        man = ['version %s' % ver] + ['%d\t%s' % (os.path.getsize(l), r) for l, r in files]
        zout.writestr('assets/rm8data/manifest.txt', '\n'.join(man) + '\n')
        total = 0
        for l, r in files:
            stored = r.endswith(('.mp3', '.mpg', '.png'))
            zout.write(l, 'assets/rm8data/' + r, compress_type=zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED)
            total += os.path.getsize(l)
        print('bundled %d data files, %.1f MB, version %s' % (len(files), total / 1e6, ver))

# 4) align (16 KiB pages for .so) and sign with a local debug key
aligned = os.path.join(tmp, 'aligned.apk')
run(os.path.join(BT, 'zipalign.exe'), '-P', '16', '-f', '4', unsigned, aligned)
ks = os.path.join(HERE, 'debug.keystore')
if not os.path.exists(ks):
    run(os.path.join(a.jdk, 'bin', 'keytool.exe'), '-genkeypair', '-keystore', ks, '-storepass', 'android', '-keypass', 'android',
        '-alias', 'androiddebugkey', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000', '-dname', 'CN=Android Debug,O=Android,C=US')
if a.release:	# the release key lives outside the published tree (<workspace>/private: keystore + password file)
    priv = os.path.join(os.path.dirname(PORT), 'private')
    rks = os.path.join(priv, 'rm8_release.keystore'); rpw = open(os.path.join(priv, 'release_keystore_pass.txt'), encoding='utf-8').read().strip()
    run(os.path.join(BT, 'apksigner.bat'), 'sign', '--ks', rks, '--ks-key-alias', 'rm8release', '--ks-pass', 'pass:' + rpw, '--key-pass', 'pass:' + rpw, '--out', a.out, aligned)
else:
    run(os.path.join(BT, 'apksigner.bat'), 'sign', '--ks', ks, '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', a.out, aligned)
print(run(os.path.join(BT, 'apksigner.bat'), 'verify', '--verbose', a.out).splitlines()[:4])
print(run(os.path.join(BT, 'zipalign.exe'), '-c', '-P', '16', '4', a.out).strip().splitlines()[-1:])
print('APK:', a.out, os.path.getsize(a.out), 'bytes')
