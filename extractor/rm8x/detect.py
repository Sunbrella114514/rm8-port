"""Finding the user's game folder: the Steam install (registry + libraryfolders.vdf + appmanifest) and the usual places of an old (non-Steam) copy.

Everything here only LISTS folders (os.listdir / registry reads); nothing in a game folder is opened for writing.
"""
import os
import re

STEAM_APPID = '2185870'
STEAM_DIRNAMES = ('Richman 8', 'RichMan 8', 'Richman8', 'RichMan8')
OLD_NAMES = ('richman8', 'richman 8', 'rich8', 'rm8', '大富翁8', '大富翁 8', '大富翁八', '大富翁8繁體中文版', '大富翁8繁体中文版', '大富翁8简体中文版')
OLD_PARENTS = ('', 'Games', 'Game', 'games', '游戏', '遊戲', '游戏库', 'Program Files (x86)', 'Program Files', 'Softstar')


def _ci(base, name):
    """Case-insensitive child lookup."""
    try:
        names = os.listdir(base)
    except OSError:
        return None
    if name in names:
        return os.path.join(base, name)
    low = name.lower()
    hit = next((n for n in names if n.lower() == low), None)
    return os.path.join(base, hit) if hit else None


def looks_like_game(path):
    """True when `path` has the marks of a RichMan 8 install (RichMan8.exe + Clump + Config)."""
    try:
        return bool(path and os.path.isdir(path) and _ci(path, 'Clump') and _ci(path, 'Config') and _ci(path, 'RichMan8.exe'))
    except OSError:
        return False


def edition_guess(path):
    """A quick label for the user (the real check is fingerprint.verify_install)."""
    p = os.path.abspath(path).replace('\\', '/').lower().split('/')
    if 'steamapps' in p or _ci(path, 'steam_appid.txt') or _ci(path, 'Bink') and not _ci(path, 'GameInfo.inf'):
        return 'Steam 简体中文版'
    return '旧版（非 Steam 的安装）'


# ------------------------------------------------------------------------------------------------------ Steam
def _reg_values():
    out = []
    try:
        import winreg
    except ImportError:
        return out
    for hive, sub, names in ((winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam', ('SteamPath',)),
                             (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Valve\Steam', ('InstallPath',)),
                             (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Valve\Steam', ('InstallPath',))):
        try:
            h = winreg.OpenKey(hive, sub)
        except OSError:
            continue
        for n in names:
            try:
                out.append(winreg.QueryValueEx(h, n)[0])
            except OSError:
                pass
    return out


def steam_libraries():
    """All Steam library folders (each has a steamapps\\ inside): the Steam client folder itself + every "path" of libraryfolders.vdf."""
    roots = [os.path.normpath(r) for r in _reg_values() if r]
    libs = []
    for r in roots:
        libs.append(r)
        vdf = os.path.join(r, 'steamapps', 'libraryfolders.vdf')
        try:
            text = open(vdf, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        for m in re.finditer(r'"path"\s+"([^"]+)"', text):
            libs.append(os.path.normpath(m.group(1).replace('\\\\', '\\')))
    seen, out = set(), []
    for l in libs:
        if os.path.normcase(l) not in seen and os.path.isdir(os.path.join(l, 'steamapps')):
            seen.add(os.path.normcase(l))
            out.append(l)
    return out


def find_steam():
    """-> [path of the Steam install of app 2185870]."""
    found = []
    for lib in steam_libraries():
        sa = os.path.join(lib, 'steamapps')
        names = []
        try:
            acf = open(os.path.join(sa, 'appmanifest_%s.acf' % STEAM_APPID), encoding='utf-8', errors='replace').read()
            m = re.search(r'"installdir"\s+"([^"]+)"', acf)
            if m:
                names.append(m.group(1))
        except OSError:
            pass
        names += STEAM_DIRNAMES
        common = _ci(sa, 'common')
        for n in names:
            p = _ci(common, n) if common else None
            if p and looks_like_game(p) and os.path.normcase(p) not in {os.path.normcase(f) for f in found}:
                found.append(p)
    return found


# ------------------------------------------------------------------------------------------------------ old copy
def _drives():
    drives = []
    try:
        import ctypes
        mask = ctypes.windll.kernel32.GetLogicalDrives()
        for i in range(26):
            if mask & (1 << i):
                d = '%s:\\' % chr(65 + i)
                if ctypes.windll.kernel32.GetDriveTypeW(d) == 3:          # fixed disks only: never wake a CD / network drive
                    drives.append(d)
    except Exception:
        pass
    return drives


def _uninstall_entries():
    out = []
    try:
        import winreg
    except ImportError:
        return out
    for hive, sub in ((winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'),
                      (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall'),
                      (winreg.HKEY_CURRENT_USER, r'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall')):
        try:
            k = winreg.OpenKey(hive, sub)
        except OSError:
            continue
        for i in range(winreg.QueryInfoKey(k)[0]):
            try:
                sk = winreg.OpenKey(k, winreg.EnumKey(k, i))
                name = winreg.QueryValueEx(sk, 'DisplayName')[0]
                if re.search(r'大富翁\s*8|richman\s*8', name, re.I):
                    for v in ('InstallLocation', 'InstallDir'):
                        try:
                            loc = winreg.QueryValueEx(sk, v)[0]
                            if loc:
                                out.append(loc.strip('"'))
                        except OSError:
                            pass
            except OSError:
                pass
    return out


def find_old(extra_roots=()):
    found = []
    seen = set()

    def add(p):
        if p and os.path.normcase(os.path.abspath(p)) not in seen and looks_like_game(p):
            seen.add(os.path.normcase(os.path.abspath(p)))
            found.append(os.path.abspath(p))
    for p in _uninstall_entries():
        add(p)
    parents = []
    for d in _drives():
        for par in OLD_PARENTS:
            b = os.path.join(d, par) if par else d
            if os.path.isdir(b):
                parents.append(b)
    home = os.path.expanduser('~')
    for sub in ('Desktop', 'Downloads', 'Documents', '桌面', '下载'):
        parents.append(os.path.join(home, sub))
    parents += list(extra_roots)
    for b in parents:
        try:
            children = os.listdir(b)
        except OSError:
            continue
        for c in children:
            if c.lower() in OLD_NAMES or re.search(r'richman\s*8|大富翁\s*8', c, re.I):
                cp = os.path.join(b, c)
                add(cp)
                try:                                              # one more level: "<publisher>\RichMan8", or an unpacked "<name>\RichMan8"
                    for cc in os.listdir(cp):
                        if cc.lower() in OLD_NAMES:
                            add(os.path.join(cp, cc))
                except OSError:
                    pass
            elif b.lower().endswith(('program files', 'program files (x86)')) and re.search(r'softstar|大宇|dayu|interserv|松岗', c, re.I):
                try:
                    for cc in os.listdir(os.path.join(b, c)):
                        if cc.lower() in OLD_NAMES or re.search(r'richman|大富翁', cc, re.I):
                            add(os.path.join(b, c, cc))
                except OSError:
                    pass
    return found


def find_all():
    """-> [(path, label)], Steam first."""
    out = []
    seen = set()
    for p in find_steam() + find_old():
        k = os.path.normcase(os.path.abspath(p))
        if k not in seen:
            seen.add(k)
            out.append((p, edition_guess(p)))
    return out


# ------------------------------------------------------------------------------------------------------ helpers for a wrongly chosen folder
def suggest_fix(path):
    """The user picked something near the game folder: return the folder that is probably meant, or None."""
    if not path:
        return None
    p = os.path.abspath(path)
    if os.path.isfile(p):
        p = os.path.dirname(p)
    if looks_like_game(p):
        return p
    cur = p
    for _ in range(3):                                          # the Clump / Config / Save sub-folder or the exe was picked
        cur = os.path.dirname(cur)
        if looks_like_game(cur):
            return cur
    try:
        for n in os.listdir(p):
            c = os.path.join(p, n)
            if os.path.isdir(c) and (looks_like_game(c)):
                return c
        common = _ci(p, 'common')                               # "steamapps"
        if common:
            for n in STEAM_DIRNAMES:
                c = _ci(common, n)
                if c and looks_like_game(c):
                    return c
        sa = _ci(p, 'steamapps')
        if sa:
            return suggest_fix(sa)
    except OSError:
        pass
    return None
