"""Copy an extraction (the folder rm8_extract produced) to an Android phone over adb: game\\ + scenes\\ + manifest.json
into /sdcard/Android/data/com.rm8.port/files/.   Needs USB debugging; the APK itself is installed separately.

  python push_to_phone.py <extraction folder> [--adb path\\to\\adb.exe] [--serial <id>] [--dry-run]

Files whose size already matches on the phone are skipped (so a re-run only sends what changed).  Folders created through adb belong to the
shell user, so they are made world-traversable (0775) for the app afterwards.  --dry-run only lists what would be sent and never starts adb.
(Without adb: the same folders can be copied by hand through the phone's USB 'file transfer' (MTP) mode into Android/data/com.rm8.port/files/.)"""
import argparse, os, shutil, subprocess, sys, time, json

PKG = 'com.rm8.port'
BASE = '/sdcard/Android/data/%s/files' % PKG


def find_adb(explicit):
    for c in (explicit, shutil.which('adb'), os.path.join(os.environ.get('ANDROID_HOME', ''), 'platform-tools', 'adb.exe'),
              os.path.join(os.environ.get('ANDROID_SDK_ROOT', ''), 'platform-tools', 'adb.exe'), r'<tools>\android-sdk\platform-tools\adb.exe'):
        if c and os.path.exists(c):
            return c
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out', help='folder produced by rm8_extract')
    ap.add_argument('--adb'); ap.add_argument('--serial'); ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    mp = os.path.join(a.out, 'manifest.json')
    if not os.path.exists(mp):
        sys.exit('这个文件夹里没有 manifest.json：请先运行 rm8_extract 提取数据。')
    man = json.load(open(mp, encoding='utf-8'))
    if man.get('options', {}).get('copy_game') == 'none':
        sys.exit('提取时用了 --no-copy-game，输出里没有 game\\ 文件夹，不能用于手机。请重新提取（不加 --no-copy-game）。')
    files = []
    for sub in ('game', 'scenes'):
        for dp, _, fs in os.walk(os.path.join(a.out, sub)):
            for f in sorted(fs):
                p = os.path.join(dp, f)
                files.append((p, BASE + '/' + os.path.relpath(p, a.out).replace('\\', '/')))
    files.append((mp, BASE + '/manifest.json'))
    total = sum(os.path.getsize(l) for l, _ in files)
    print('%d 个文件，共 %.0f MB -> %s' % (len(files), total / 1e6, BASE))
    if a.dry_run:
        for l, r in files[:5] + files[-3:]:
            print('  ', r)
        print('(--dry-run: adb 没有被调用)')
        return 0
    adb = find_adb(a.adb)
    if not adb:
        sys.exit('找不到 adb。请安装 Android platform-tools，或用 --adb 指定路径；也可以改用手动复制（见脚本开头的说明）。')
    cmd = [adb] + (['-s', a.serial] if a.serial else [])

    def run(*args, check=True):
        r = subprocess.run(cmd + list(args), capture_output=True)
        if check and r.returncode:
            sys.exit('adb %s 失败：%s' % (' '.join(args), (r.stderr or r.stdout).decode('utf-8', 'replace')))
        return r.stdout.decode('utf-8', 'replace')

    devs = [l.split('\t') for l in run('devices').splitlines()[1:] if '\t' in l]
    if not a.serial and len(devs) != 1:
        sys.exit('需要恰好一台已授权的设备（用 --serial 指定）。当前：%s' % devs)
    if any(d[1] != 'device' for d in devs if not a.serial or d[0] == a.serial):
        sys.exit('设备未授权：请在手机上允许 USB 调试。')
    dirs = sorted({r.rsplit('/', 1)[0] for _, r in files})
    for i in range(0, len(dirs), 40):
        run('shell', 'mkdir', '-p', *dirs[i:i + 40])
    sent = 0
    t0 = time.time()
    for i, (l, r) in enumerate(files):
        size = os.path.getsize(l)
        if run('shell', 'stat', '-c', '%s', r, check=False).strip() != str(size):
            run('push', l, r); sent += size
        if i % 200 == 0:
            print('  %d/%d' % (i + 1, len(files)), flush=True)
    run('shell', 'find %s -type d -exec chmod 0775 {} +' % BASE)
    print('完成：新传 %.0f MB，用时 %.0f 秒。现在可以在手机上打开“大富翁8”。' % (sent / 1e6, time.time() - t0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
