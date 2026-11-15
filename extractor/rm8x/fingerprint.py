"""Is this folder a genuine, supported RichMan 8 install?  Compares file sizes and SHA-256 values with the reference installs in data/references/*.json
(recorded by tools/make_reference.py from known-good installs).

Reference file roles:
  core     read by the extractor (exporters or raw copy). Must exist; size + hash must match for a 'supported' install.
  video    the two Bink movies; needed only when converting videos.
  info     recorded for information only (RichMan8.exe, DLLs): the port never reads them; a different exe just produces a note.
  mutable  the user's own settings / profile (Config/init.txt, Config/gamedate.bin): never part of the fingerprint.
Several references can exist (editions): the best matching one is chosen by comparing the hashes of all small core files and the sizes of the big ones.
Variant result: 'reference' (all core hashes equal to that reference) or 'unknown' (something differs; refused unless --allow-unknown).
"""
import glob
import hashlib
import json
import os

from .util import app_dir, human, sha256_file

STATUS_ORDER = {'primary': 0, 'supported': 1, 'legacy': 2}
SMALL = 16 << 20       # files up to this size are hashed for the edition scoring


class InstallError(Exception):
    pass


class UnsupportedVersion(InstallError):
    """The folder is a RichMan 8 install, but not (exactly) one of the supported editions."""


def load_references():
    out = []
    for p in sorted(glob.glob(os.path.join(app_dir(), 'data', 'references', '*.json'))):
        if p.endswith('_outputs.json'):
            continue
        r = json.load(open(p, encoding='utf-8'))
        r['_file'] = os.path.basename(p)
        out.append(r)
    out.sort(key=lambda r: (STATUS_ORDER.get(r.get('status', 'supported'), 1), r['id']))      # the primary edition first (also wins score ties)
    return out


def find_case(game, rel):
    """Case-insensitive lookup of a relative path (the Steam install has lower-case folder names); returns the real path or None."""
    p = game
    for part in rel.split('/'):
        try:
            names = os.listdir(p)
        except OSError:
            return None
        hit = part if part in names else next((n for n in names if n.lower() == part.lower()), None)
        if hit is None:
            return None
        p = os.path.join(p, hit)
    return p


def _score(game, ref, cache):
    """(small-file hash matches, big-file size matches, -missing core files)."""
    hs = bs = miss = 0
    for rel, r in ref['files'].items():
        if r['role'] != 'core':
            continue
        p = find_case(game, rel)
        if p is None:
            miss += 1; continue
        size = os.path.getsize(p)
        if size != r['size']:
            continue
        if size <= SMALL:
            if p not in cache:
                cache[p] = sha256_file(p)
            hs += cache[p] == r['sha256']
        else:
            bs += 1
    return (hs, bs, -miss)


def verify_install(game, log, need_video=True, allow_unknown=False, quick=False, use_reference=True):
    if not os.path.isdir(game):
        raise InstallError('找不到游戏文件夹：%s\n请检查路径有没有输错；如果游戏装在移动硬盘 / 另一块磁盘上，请先把它接好。' % game)
    if not (find_case(game, 'Clump') and find_case(game, 'Config')):
        from . import detect
        fix = detect.suggest_fix(game)
        hint = ('\n看起来你要选的是这个文件夹：%s' % fix) if fix else ''
        raise InstallError('这个文件夹里没有找到《大富翁8》：%s\n'
                           '请选择装有游戏主程序 RichMan8.exe 的那个文件夹（里面应该有 Clump、Texture、Config、Map、Sound、Music、Pysrc 等子文件夹）。\n'
                           'Steam 版通常在 steamapps\\common\\Richman 8（在 Steam 里右键游戏 → 管理 → 浏览本地文件，可以直接打开它）。%s' % (game, hint))
    refs = load_references() if use_reference else []
    warnings = []
    cache = {}
    if refs:
        scored = sorted(((_score(game, r, cache), -i) for i, r in enumerate(refs)), reverse=True)      # best score; ties -> earlier (primary) reference
        ref = refs[-scored[0][1]]
        log.detail('edition scores: %s' % ', '.join('%s=%s' % (refs[-i]['id'], s) for s, i in scored))
    else:
        log.warn('没有参考文件，只做结构检查')
        ref = {'files': {}, 'name': 'unreferenced', 'id': 'none', 'raw_copy': []}
    files = ref['files']
    missing, mismatched, sized_ok, source_files = [], [], [], {}
    for rel, r in sorted(files.items()):
        role = r['role']
        if role == 'mutable' or (role == 'video' and not need_video):
            continue
        p = find_case(game, rel)
        if p is None:
            if role in ('core', 'video'):
                missing.append(rel)
            continue
        size = os.path.getsize(p)
        if size != r['size']:
            if role in ('core', 'video'):
                mismatched.append('%s（大小 %d，期望 %d）' % (rel, size, r['size']))
            else:
                warnings.append('%s 与参考版本不同（大小 %d，参考 %d）' % (rel, size, r['size']))
            source_files[rel] = {'size': size, 'sha256': None if quick else sha256_file(p), 'role': role}
            continue
        sized_ok.append((rel, p, r))
    if missing:
        raise InstallError('游戏文件不完整，缺少这些文件：\n  %s%s\n请确认选择的是完整安装（不是只有安装程序、精简版或没下载完）。\n'
                           'Steam 版可以在 Steam 里右键游戏 → 属性 → 已安装文件 → “验证游戏文件的完整性”，让它补齐后再试。'
                           %('\n  '.join(missing[:20]), '\n  ...' if len(missing) > 20 else ''))
    total = sum(r['size'] for _, p, r in sized_ok if p not in cache)
    if not quick and total:
        log.info('  校验 %d 个文件的 SHA-256（共 %s）...' % (len(sized_ok), human(total)))
    done = [0]
    nextmark = [1 << 28]
    for rel, p, r in sized_ok:
        if quick:
            digest = cache.get(p) or r['sha256']          # trusting the size only (small files were hashed for the scoring anyway)
            if p in cache and cache[p] != r['sha256']:
                digest = cache[p]
        elif p in cache:
            digest = cache[p]
        else:
            def prog(n):
                done[0] += n
                if done[0] >= nextmark[0]:
                    log.info('    ... %s / %s' % (human(done[0]), human(total))); nextmark[0] += 1 << 28
            digest = sha256_file(p, prog)
        source_files[rel] = {'size': r['size'], 'sha256': digest, 'role': r['role']}
        if digest != r['sha256']:
            if r['role'] in ('core', 'video'):
                mismatched.append('%s（内容与参考版本不同）' % rel)
            else:
                warnings.append('%s 的内容与参考版本不同（不影响提取；通常说明是另一个小版本或被补丁过）' % rel)
    variant = 'reference' if (refs and not mismatched) else 'unknown'
    if refs and mismatched:
        others = '；'.join('%s%s' % (r['name'], '【主要支持】' if r.get('status') == 'primary' else '') for r in refs)
        msg = ('这个安装与受支持的版本不一致（最接近：%s）。下面这些文件和我们记录的原版不一样：\n  %s%s\n本工具支持的版本：%s。\n'
               '常见原因：游戏被改动过（汉化补丁、修改器、mod）、是其它语言 / 其它版本的游戏、或者文件没有复制完整。\n'
               'Steam 版可以先“验证游戏文件的完整性”恢复原样；别的版本目前不能保证能用。'
               % (ref['name'], '\n  '.join(mismatched[:12]), '\n  ...' if len(mismatched) > 12 else '', others))
        if not allow_unknown:
            raise UnsupportedVersion(msg + '\n如果你确定要试一试（结果可能不正确，甚至无法运行），请加 --allow-unknown；交互运行时程序会问你。')
        warnings.append('未识别的游戏版本，已按 --allow-unknown 继续：' + '; '.join(mismatched[:6]))
        log.warn(msg.split('\n')[0])
    raw_files = []
    for rel in ref.get('raw_copy', []):
        info = source_files.get(rel)
        if info and info.get('sha256'):
            raw_files.append({'path': rel, 'size': info['size'], 'sha256': info['sha256']})
    exe = source_files.get('RichMan8.exe')
    if exe and files.get('RichMan8.exe') and exe['sha256'] and exe['sha256'] != files['RichMan8.exe']['sha256']:
        warnings.append('RichMan8.exe 与参考版本不同（可能是破解/补丁/兼容性修复版）；移植版不使用该文件，不影响提取')
    fp_src = '\n'.join('%s:%s' % (k, v['sha256']) for k, v in sorted(source_files.items()) if v['role'] in ('core', 'video') and v['sha256'])
    fingerprint = hashlib.sha256(fp_src.encode()).hexdigest()[:16]
    if variant == 'reference':
        log.info('  已识别：%s（核心文件 %d 个全部一致，指纹 %s）' % (ref['name'], len(sized_ok), fingerprint))
    mapdir = find_case(game, 'Map')
    if mapdir and files:
        known = {os.path.basename(k)[:-4].lower() for k in files if k.lower().startswith('map/') and k.lower().endswith('.map')}
        extra = sorted({f[:-4] for f in os.listdir(mapdir) if f.lower().endswith('.map')} - known)
        if extra:
            warnings.append('发现参考版本里没有的额外地图：%s（会一并导出）' % ' '.join(extra))
    return {'fingerprint': fingerprint, 'identity': ref['name'], 'edition': ref.get('edition', ref['id']), 'reference_id': ref['id'],
            'variant': variant, 'source_files': source_files, 'raw_files': raw_files, 'warnings': warnings}
