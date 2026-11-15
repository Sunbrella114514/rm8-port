"""Install the viewer on an Android phone over USB, copy the game data, start it, and collect log + screenshot.
usage: python -I deploy.py <game_dir> [--scenes port/scenes] [--apk port/rm8_viewer.apk] [--no-data] [--wait 20]
Needs: phone with USB debugging enabled and authorised (adb devices shows it as 'device').
Writes port/results/device_log.txt and port/results/device_shot.png."""
import os, sys, subprocess, argparse, time, glob

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
ap = argparse.ArgumentParser()
ap.add_argument('game')
ap.add_argument('--scenes', default=os.path.join(PORT, 'scenes'))
ap.add_argument('--apk', default=os.path.join(PORT, 'rm8_viewer.apk'))
ap.add_argument('--adb', default=r'<tools>\android-sdk\platform-tools\adb.exe')
ap.add_argument('--no-data', action='store_true')
ap.add_argument('--wait', type=int, default=20)
ap.add_argument('--no-launch', action='store_true')	# install + push only (the user may be using the phone)
a = ap.parse_args()
PKG = 'com.rm8.port'
BASE = '/sdcard/Android/data/%s/files' % PKG

def adb(*args, check=True, binary=False):
    r = subprocess.run([a.adb, *args], capture_output=True)
    if check and r.returncode != 0:
        raise SystemExit('adb %s failed: %s' % (' '.join(args), (r.stderr or r.stdout).decode('utf-8', 'replace')))
    return r.stdout if binary else r.stdout.decode('utf-8', 'replace')

devs = [l.split('\t') for l in adb('devices').splitlines()[1:] if '\t' in l]
if len(devs) != 1 or devs[0][1] != 'device':
    raise SystemExit('need exactly one authorised device, got %s\n'
                     '-> enable Developer options + USB debugging on the phone, connect USB, accept the prompt' % devs)
info = {k: adb('shell', 'getprop', k).strip() for k in ('ro.product.model', 'ro.build.version.release', 'ro.product.cpu.abi', 'ro.hardware.egl')}
print('device:', info)
if 'arm64' not in info['ro.product.cpu.abi']:
    print('warning: the APK only contains arm64-v8a code')

print('install', a.apk); print(adb('install', '-r', a.apk).strip())
adb('shell', 'mkdir', '-p', BASE + '/game/Clump', BASE + '/game/Texture', BASE + '/scenes')

def push(local, remote):
    size = os.path.getsize(local); mt = int(os.path.getmtime(local))
    rs = adb('shell', 'stat', '-c', "'%s %Y'", remote, check=False).strip().split()
    # same size and modification time (adb push keeps the local mtime): unchanged. Size alone missed edits that kept the length
    if len(rs) == 2 and rs[0] == str(size) and abs(int(rs[1]) - mt) <= 2: return 0
    adb('push', local, remote); return size

if not a.no_data:
    total = 0
    sys.path.insert(0, HERE); import data_files	# one list for deploy.py and build_apk.py --with-data
    files = [(l, BASE + '/' + r) for l, r in data_files.data_files(a.game, a.scenes)]
    for d in sorted({r.rsplit('/', 1)[0] for _, r in files}): adb('shell', 'mkdir', '-p', d)
    t0 = time.time()
    for i, (l, r) in enumerate(files):
        total += push(l, r)
        if i % 100 == 0: print('  %d/%d files' % (i + 1, len(files)))
    print('data pushed: %.1f MB new in %.0fs (%d files)' % (total / 1e6, time.time() - t0, len(files)))

# Directories created by adb belong to the shell user (uid 2000, mode 2770), which the app cannot enter.
# They are ours to chmod: give "others" (the app uid) read + traverse. The files themselves are 0666.
adb('shell', 'find %s/game %s/scenes -type d -exec chmod 0775 {} +' % (BASE, BASE))

if a.no_launch:
    print('installed, not launched'); raise SystemExit(0)
adb('logcat', '-c')
adb('shell', 'am', 'force-stop', PKG)
adb('shell', 'am', 'start', '-n', PKG + '/.Rm8Activity')
print('running for %ds ...' % a.wait); time.sleep(a.wait)
os.makedirs(os.path.join(PORT, 'results'), exist_ok=True)
png = adb('exec-out', 'screencap', '-p', binary=True)
open(os.path.join(PORT, 'results', 'device_shot.png'), 'wb').write(png)
log = adb('logcat', '-d', '-v', 'time', 'rm8:V', 'SDL:V', 'SDL/APP:V', 'AndroidRuntime:E', 'DEBUG:V', 'libc:F', '*:S')
open(os.path.join(PORT, 'results', 'device_log.txt'), 'w', encoding='utf-8').write('device: %s\n%s' % (info, log))
print(log[-3000:])
print('saved results/device_log.txt and results/device_shot.png')
