"""Migrating the original game's saves and profile into the port (rm8_extract --saves).

What is migrated (everything is READ ONLY on the game / Steam side):
  * Save\\record_NN.sav (story slots 01..10) and Save\\record_mNN.sav (free-game slots)       -> <data dir>\\save\\<same name>
    Format check: version > 6 and the "r8recrd" magic, like Save_ReadSlotHeader @0x4062a0 (port/app/save.cpp rm8SaveSlotInfo).  The port reads the
    original's record files as they are (docs/save_port.md), so the copy is byte-for-byte.  record_temp.sav (the autosave of the running board) is not migrated.
  * Config\\gamedate.bin (181 int32 = 724 bytes: story progress, unlocked roles, puzzle pieces)   -> <data dir>\\rm8_gamedate.bin
    The port's profile file has exactly the original layout and magic numbers (port/app/collect.cpp loadGameDate / saveGameDate = GameDate_Load @0x4a99e0 /
    GameDate_Save @0x4a9d60), so a plain copy is compatible; the codec below only validates the file and summarises it.
  * The story "storage box" has no file of its own in the original (it lives inside the story records); the port's rm8_box.bin is a port-only file, so there is nothing to migrate.

Where the files come from: <game>\\Save and <game>\\Config (the Steam release keeps them in the install folder: Steam Auto-Cloud syncs that root, see
steam_autocloud.vdf), plus, for completeness, <Steam>\\userdata\\<id>\\2185870\\remote\\** when such a cloud copy exists.  For each name the newest valid file wins.
"""
import hashlib
import json
import os
import re
import shutil
import struct
import time

STEAM_APPID = '2185870'
SLOT_RE = re.compile(r'^record_(m?)(\d\d)\.sav$', re.I)
MAX_SAVE = 4 << 20

# magic tables of RichMan8.exe (a boolean is stored as a specific number), copied from port/app/collect.cpp
T_UNLOCK = {11: 4576, 12: 938, 13: 63, 14: 5162, 15: 5632, 16: 12976}
T_CLEAR = [0, 7349, 9027, 764, 62, 176, 93, 5377, 888, 1245, 4344, 8356, 9, 333, 4576, 1432, 8634]
T_STAGE = [[0], [87, 1674, 9387, 740, 12783], [4376, 874, 194, 90, 505], [6235, 18, 937, 5624, 8843], [562, 42, 157, 4735, 12901],
           [9235, 7235, 54, 124, 452], [9237, 264, 3556, 2, 666], [5238, 937, 5623, 41, 9190], [1765, 452, 6348, 7356, 3], [937, 1376, 547, 819, 43201],
           [23, 8376, 6325, 391, 653], [9375, 54, 2138, 3876, 72], [7364, 376, 2847, 219, 321], [9876, 658, 46, 285, 1079], [935, 5482, 28, 2647, 2213],
           [8376, 352, 2312, 219, 91], [645, 4319, 298, 777, 54]]


class Cand:
    def __init__(self, path, kind, name, source):
        self.path, self.kind, self.name, self.source = path, kind, name, source
        self.size = os.path.getsize(path)
        self.mtime = os.path.getmtime(path)
        self.sha256 = hashlib.sha256(open(path, 'rb').read()).hexdigest()
        self.info = ''


def check_save(path):
    """-> (ok, note).  Same test as the port's rm8SaveSlotInfo."""
    try:
        d = open(path, 'rb').read(64)
        size = os.path.getsize(path)
    except OSError as e:
        return False, str(e)
    if size < 12 + 0x28 + 12 * 4 + 36 or size > MAX_SAVE:
        return False, '大小不对（%d 字节）' % size
    ver = struct.unpack_from('<i', d, 0)[0]
    if ver <= 6 or d[4:11] != b'r8recrd':
        return False, '不是大富翁8 的存档（版本 %d）' % ver
    y, mo, _, day, h, mi = struct.unpack_from('<6H', d, 12)
    return True, '%d-%02d-%02d %02d:%02d' % (y, mo, day, h, mi)


def check_gamedate(path):
    """-> (ok, summary) for a gamedate.bin (the port loads it only when it is exactly 724 bytes)."""
    try:
        data = open(path, 'rb').read()
    except OSError as e:
        return False, str(e)
    if len(data) != 181 * 4:
        return False, '大小不对（%d 字节，应为 724）' % len(data)
    v = struct.unpack('<181i', data)
    p = 0
    unlocked = sum(1 for r in range(11, 17) if v[p + r - 11] == T_UNLOCK[r]) + 10
    p = 6
    pieces = levels = 0
    for r in range(1, 17):
        if v[p] in T_STAGE[r]:
            levels = max(levels, T_STAGE[r].index(v[p]))
        pieces += sum(1 for k in range(1, 10) if v[p + k] == T_CLEAR[r])
        p += 10
    return True, '解锁角色 %d 个、拼图 %d 块' % (unlocked, pieces)


# ------------------------------------------------------------------------------------------------ discovery
def _find_ci(base, *parts):
    p = base
    for part in parts:
        try:
            names = os.listdir(p)
        except OSError:
            return None
        hit = part if part in names else next((n for n in names if n.lower() == part.lower()), None)
        if hit is None:
            return None
        p = os.path.join(p, hit)
    return p


def steam_roots(game):
    """Possible Steam client folders (the ones that contain userdata\\)."""
    roots = []
    try:
        import winreg
        for hive, sub in ((winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam'), (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Valve\Steam'),
                          (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Valve\Steam')):
            try:
                h = winreg.OpenKey(hive, sub)
                for n in ('SteamPath', 'InstallPath'):
                    try:
                        roots.append(winreg.QueryValueEx(h, n)[0])
                    except OSError:
                        pass
            except OSError:
                pass
    except ImportError:
        pass
    g = os.path.abspath(game)
    parts = g.replace('\\', '/').split('/')
    if 'steamapps' in [p.lower() for p in parts]:
        i = [p.lower() for p in parts].index('steamapps')
        lib = os.sep.join(parts[:i]) if i else ''
        if lib:
            lf = os.path.join(lib, 'libraryfolder.vdf')
            try:
                m = re.search(r'"launcher"\s+"([^"]+)"', open(lf, encoding='utf-8', errors='replace').read())
                if m:
                    roots.append(os.path.dirname(m.group(1).replace('\\\\', '\\')))
            except OSError:
                pass
            roots.append(lib)
    seen, out = set(), []
    for r in roots:
        r = os.path.normpath(r)
        if os.path.normcase(r) not in seen and os.path.isdir(os.path.join(r, 'userdata')):
            seen.add(os.path.normcase(r))
            out.append(r)
    return out


def find(game, log, extra_dirs=()):
    """Collect candidates. Returns {'saves': {name: Cand}, 'gamedate': Cand|None, 'skipped': [(path, why)], 'sources': [str]}."""
    cands, skipped, sources = [], [], []
    sd = _find_ci(game, 'Save')
    if sd and os.path.isdir(sd):
        sources.append(sd)
        for f in sorted(os.listdir(sd)):
            p = os.path.join(sd, f)
            if SLOT_RE.match(f) and os.path.isfile(p):
                cands.append((p, 'save', f.lower(), sd))
            elif f.lower() == 'record_temp.sav':
                skipped.append((p, '自动存档（正在进行的牌局），不迁移'))
    gd = _find_ci(game, 'Config', 'gamedate.bin')
    if gd:
        cands.append((gd, 'gamedate', 'rm8_gamedate.bin', os.path.dirname(gd)))
    userdirs = []
    steam_game = 'steamapps' in os.path.abspath(game).replace(os.sep, '/').lower().split('/')      # only a Steam install has a Steam cloud folder of its own
    for sr in (steam_roots(game) if steam_game else []):
        ud = os.path.join(sr, 'userdata')
        try:
            for acct in sorted(os.listdir(ud)):
                d = os.path.join(ud, acct, STEAM_APPID)
                if os.path.isdir(d):
                    userdirs.append(d)
        except OSError:
            pass
    userdirs += [d for d in extra_dirs if os.path.isdir(d)]
    for d in userdirs:
        sources.append(d + '（Steam 云存档目录，只读）')
        for dp, _, fs in os.walk(d):
            for f in sorted(fs):
                p = os.path.join(dp, f)
                if SLOT_RE.match(f):
                    cands.append((p, 'save', f.lower(), d))
                elif f.lower() == 'gamedate.bin':
                    cands.append((p, 'gamedate', 'rm8_gamedate.bin', d))
    best = {}
    for path, kind, name, src in cands:
        try:
            c = Cand(path, kind, name, src)
        except OSError as e:
            skipped.append((path, str(e)))
            continue
        ok, note = check_save(path) if kind == 'save' else check_gamedate(path)
        if not ok:
            skipped.append((path, note))
            continue
        c.info = note
        old = best.get(name)
        if old is None or c.mtime > old.mtime:
            if old is not None and old.sha256 != c.sha256:
                log.detail('%s: newer copy at %s replaces %s' % (name, path, old.path))
            best[name] = c
        elif old.sha256 != c.sha256:
            log.detail('%s: ignoring older copy at %s' % (name, path))
    saves = {n: c for n, c in best.items() if c.kind == 'save'}
    return {'saves': saves, 'gamedate': best.get('rm8_gamedate.bin'), 'skipped': skipped, 'sources': sources}


def describe(found, log):
    log.info('  读取存档（只读）：%s' % ('；'.join(found['sources']) or '没有找到存档文件夹'))
    for n in sorted(found['saves']):
        c = found['saves'][n]
        log.info('    %s  %s  %d 字节  存于 %s' % (n, c.info, c.size, time.strftime('%Y-%m-%d %H:%M', time.localtime(c.mtime))))
    g = found['gamedate']
    if g:
        log.info('    进度 gamedate.bin：%s（%s）' % (g.info, g.path))
    for p, why in found['skipped']:
        log.info('    跳过 %s：%s' % (p, why))
    return bool(found['saves'] or g)


# ------------------------------------------------------------------------------------------------ staging
def stage(out_dir, found, log):
    """Copy the found files into <out>\\saves\\ (save\\<name>.sav + rm8_gamedate.bin + saves.json). Returns the staged dir, or None when empty."""
    d = os.path.join(out_dir, 'saves')
    shutil.rmtree(d, ignore_errors=True)
    files = []
    for n, c in sorted(found['saves'].items()):
        files.append(('save/' + n, c))
    if found['gamedate']:
        files.append(('rm8_gamedate.bin', found['gamedate']))
    if not files:
        return None
    for rel, c in files:
        dst = os.path.join(d, *rel.split('/'))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(c.path, dst)
        if hashlib.sha256(open(dst, 'rb').read()).hexdigest() != c.sha256:
            raise OSError('复制 %s 后校验不符' % rel)
    json.dump({'files': {rel: {'sha256': c.sha256, 'size': c.size, 'source': c.path, 'info': c.info} for rel, c in files}, 'created': time.strftime('%Y-%m-%d %H:%M:%S')},
              open(os.path.join(d, 'saves.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    return d


def staged_files(saves_dir):
    """[(abs path, rel path)] of a staged folder (rel = 'save/record_01.sav' or 'rm8_gamedate.bin')."""
    out = []
    sd = os.path.join(saves_dir, 'save')
    if os.path.isdir(sd):
        out += [(os.path.join(sd, f), 'save/' + f) for f in sorted(os.listdir(sd)) if SLOT_RE.match(f)]
    g = os.path.join(saves_dir, 'rm8_gamedate.bin')
    if os.path.isfile(g):
        out.append((g, 'rm8_gamedate.bin'))
    return out


# ------------------------------------------------------------------------------------------------ desktop
def desktop_data_dir(explicit=None):
    if explicit:
        return os.path.abspath(explicit)
    env = os.environ.get('RM8_DATA_DIR')
    if env:
        return os.path.abspath(env)
    ap = os.environ.get('APPDATA')
    return os.path.join(ap, 'RM8') if ap else os.path.join(os.path.expanduser('~'), '.config', 'RM8')


def install_desktop(saves_dir, data_dir, interactive, log):
    """Put the staged files where the desktop app reads them.  Never overwrites without asking: interactive = ask per file ([y/N/a]),
    otherwise the existing file is kept and logged.  Returns (copied, kept)."""
    copied = kept = 0
    all_yes = False
    log.info('  迁移到移植版的数据文件夹：%s' % data_dir)
    for src, rel in staged_files(saves_dir):
        dst = os.path.join(data_dir, *rel.split('/'))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.exists(dst):
            if hashlib.sha256(open(dst, 'rb').read()).hexdigest() == hashlib.sha256(open(src, 'rb').read()).hexdigest():
                log.info('    %s：已经是一样的，跳过' % rel)
                continue
            yes = all_yes
            if not yes and interactive:
                try:
                    ans = input('    移植版里已经有不同的 %s（%s）。用原版的覆盖它吗？旧文件会先备份。[y=是 / N=否 / a=全部是] ' % (rel, time.strftime('%Y-%m-%d %H:%M', time.localtime(os.path.getmtime(dst))))).strip().lower()
                except EOFError:
                    ans = ''
                if ans == 'a':
                    all_yes = yes = True
                else:
                    yes = ans in ('y', 'yes')
            if not yes:
                log.info('    %s：移植版里已有不同的文件，保留不动%s' % (rel, '' if interactive else '（非交互运行不覆盖；要覆盖请手动处理或交互运行）'))
                kept += 1
                continue
            bak = dst + '.bak-' + time.strftime('%Y%m%d%H%M%S')
            shutil.copyfile(dst, bak)
            log.info('    %s：旧文件已备份为 %s' % (rel, os.path.basename(bak)))
        tmp = dst + '.part'
        shutil.copyfile(src, tmp)
        os.replace(tmp, dst)
        copied += 1
        log.info('    %s：已复制' % rel)
    return copied, kept
