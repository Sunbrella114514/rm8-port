"""Checks of an extraction result (staging folder before it is swapped in, or an existing output folder)."""
import json
import os
import re
import struct

from .fingerprint import find_case
from .util import app_dir, sha256_file

SHARED_TEXT = ['cards.txt', 'event_arts.txt', 'movie_arts.txt', 'news.txt', 'npcs.txt', 'particles.txt', 'questions.txt', 'introduce.txt', 'roles.txt',
               'stages.txt', 'stocks.txt', 'story.txt', 'zh_t2s.txt', 'save_tables.txt', 'minigame/sprites.txt', 'ui/uvlist.txt', 'ui/strings.txt']
PNG_SIG = b'\x89PNG\r\n\x1a\n'


def hash_tree(root, subdirs):
    out = {}
    for sd in subdirs:
        base = os.path.join(root, sd)
        for dp, _, fs in os.walk(base):
            for f in sorted(fs):
                p = os.path.join(dp, f)
                out[os.path.relpath(p, root).replace('\\', '/')] = {'size': os.path.getsize(p), 'sha256': sha256_file(p)}
    return out


def check_manifest_files(out, manifest, log, quick=True):
    """Existence + size (and sha256 unless quick) of everything the manifest lists. Returns the list of problems."""
    bad = []
    for rel, info in manifest.get('outputs', {}).items():
        p = os.path.join(out, rel)
        if not os.path.isfile(p) or os.path.getsize(p) != info['size']:
            bad.append(rel)
        elif not quick and sha256_file(p) != info['sha256']:
            bad.append(rel)
    for rel, info in manifest.get('game_files', {}).items():
        p = os.path.join(out, 'game', rel)
        if not os.path.isfile(p) or os.path.getsize(p) != info['size']:
            bad.append('game/' + rel)
        elif not quick and sha256_file(p) != info['sha256']:
            bad.append('game/' + rel)
    return bad


def read_pack_names(path):
    """Entry names (lower case) of a clump/tex archive (format A of scripts/rm8pak.py)."""
    with open(path, 'rb') as f:
        tag, z, fsize, cnt, unk = struct.unpack('<IIIII', f.read(20))
        raw = f.read(cnt * 16)
        names = set()
        for i in range(cnt):
            nl, off, size, flag = struct.unpack_from('<IIII', raw, i * 16)
            f.seek(off)
            names.add(f.read(nl).decode('cp950', errors='replace').lower())
    return names


KEY_RE = re.compile(r'(?<![\w\\])((?:\d+:)?[A-Za-z0-9_\\\-\.]*\\[A-Za-z0-9_\\\-\.]*\.(?:dff|anm|uva|tga))', re.I)


def scene_keys(stage):
    """All model / animation / texture pack keys that the derived text files mention."""
    models, textures = set(), set()
    sc = os.path.join(stage, 'scenes')
    files = []
    for dp, _, fs in os.walk(sc):
        for f in fs:
            if f.endswith('.txt') and f not in ('zh_t2s.txt', 'strings.txt', 'questions.txt', 'introduce.txt', 'news_text.txt'):
                files.append(os.path.join(dp, f))
    for p in files:
        for line in open(p, encoding='utf-8', errors='replace'):
            if line.startswith(('vrow', 'vcrow', 'tile')):
                continue
            for tok in re.split(r'[\s\t]+', line):
                tok = tok.split(':', 1)[1] if re.match(r'^\d+:', tok) else tok
                tl = tok.lower()
                if '\\' not in tl:
                    continue
                if tl.startswith('minigame\\'):        # minigame/sprites.txt keys are relative to article\ (the app adds it)
                    tl = 'article\\' + tl
                if tl.endswith(('.dff', '.anm', '.uva')):
                    models.add(tl)
                elif tl.endswith('.tga'):
                    textures.add(tl)
    return models, textures


def line_count(p):
    with open(p, 'rb') as f:
        return sum(1 for _ in f)


def validate_output(stage, game, raw, maps, video, log, copied, strict=True, ref_id=None, exporters_digest=None):
    errors, warnings = [], []
    missing_tex_examples = []
    sc = os.path.join(stage, 'scenes')

    def need(rel, min_lines=1):
        p = os.path.join(stage, rel)
        if not os.path.isfile(p) or os.path.getsize(p) == 0:
            errors.append('缺少或为空：' + rel); return None
        n = line_count(p)
        if n < min_lines:
            errors.append('%s 内容太少（%d 行，至少应有 %d）' % (rel, n, min_lines))
        return n

    # 1) shared files
    counts = {}
    for rel in SHARED_TEXT:
        counts['scenes/' + rel] = need('scenes/' + rel)
    nui = len([f for f in os.listdir(os.path.join(sc, 'ui'))]) if os.path.isdir(os.path.join(sc, 'ui')) else 0
    if nui < 60:
        errors.append('scenes/ui 里只有 %d 个文件（应有 60 多个 .ui）' % nui)
    # 2) maps
    for m in maps:
        d = os.path.join(sc, m)
        sp = os.path.join(d, 'scene.txt')
        if not os.path.isfile(sp):
            errors.append('地图 %s 缺少 scene.txt' % m); continue
        if not os.path.isfile(os.path.join(d, 'wonders.txt')):
            errors.append('地图 %s 缺少 wonders.txt' % m)
        text = open(sp, encoding='utf-8').read().split('\n')
        if not text[0].startswith('map '):
            errors.append('地图 %s 的 scene.txt 格式不对' % m)
        nleaf = 0
        for line in text:
            if line.startswith('leaf '):
                nleaf += 1
                png = line.split(' ')[6]
                if not os.path.isfile(os.path.join(d, png)):
                    errors.append('地图 %s 缺少光照图 %s' % (m, png))
            elif line.startswith('article ') and False:
                pass
        if nleaf == 0:
            errors.append('地图 %s 没有地形叶块' % m)
        for f in os.listdir(d):
            if f.endswith('.png'):
                with open(os.path.join(d, f), 'rb') as fh:
                    if fh.read(8) != PNG_SIG:
                        errors.append('不是有效的 PNG：%s/%s' % (m, f))
    # 3) every model / animation / texture the data refers to exists in the game's packs
    packs = {}
    gdir = os.path.join(stage, 'game') if copied else game
    try:
        fc = lambda rel: find_case(gdir, rel)
        packs['clump'] = read_pack_names(fc('Clump/clump0000.dat'))
        packs['tex'] = read_pack_names(fc('Texture/tex0000.dat')) | read_pack_names(fc('Texture/tex0001.dat'))
        models, textures = scene_keys(stage)
        miss_m = sorted(k for k in models if k not in packs['clump'])
        miss_t = sorted(k for k in textures if k not in packs['tex'] and 'prt\\' + k not in packs['tex'])    # UI picture keys are relative to prt\
        counts['models_referenced'] = len(models); counts['textures_referenced'] = len(textures)
        counts['missing_models'] = len(miss_m); counts['missing_textures'] = len(miss_t)
        if miss_m:
            errors.append('数据引用了 %d 个资源包里不存在的模型/动画，例如 %s' % (len(miss_m), ', '.join(miss_m[:5])))
        missing_tex_examples = miss_t[:5]
    except (OSError, struct.error) as e:
        errors.append('读取资源包索引失败：%s' % e)
    # 4) the copied raw files
    if copied:
        for rel, info in raw.items():
            p = os.path.join(stage, 'game', rel)
            if not os.path.isfile(p) or os.path.getsize(p) != info['size']:
                errors.append('game/%s 大小不符' % rel)
            elif os.path.getsize(p) > 0 and strict and info.get('mode') != 'link' and sha256_file(p) != info['sha256']:
                errors.append('game/%s 复制后的 SHA-256 与源文件不同' % rel)
        for must in ('Clump/clump0000.dat', 'Texture/tex0000.dat', 'Texture/tex0001.dat', 'Sound/snd0000.dat', 'Music/mus0000.dat'):
            if must not in raw:
                errors.append('game/%s 没有复制' % must)
        if not any(k.startswith('Pysrc/') for k in raw):
            errors.append('game/Pysrc 里没有 .pyc')
    # 5) videos
    if video.get('status') == 'converted':
        for f in ('start.mpg', 'staff.mpg'):
            p = os.path.join(sc, 'video', f)
            if not os.path.isfile(p):
                errors.append('缺少影片 scenes/video/' + f)
            elif open(p, 'rb').read(4) != b'\x00\x00\x01\xba':
                errors.append('%s 不是 MPEG 节目流' % f)
    # 6) compare with the dev reference counts when available
    ref_path = os.path.join(app_dir(), 'data', 'references', '%s_outputs.json' % ref_id)
    ref = json.load(open(ref_path, encoding='utf-8')) if (ref_id and os.path.exists(ref_path) and strict) else None
    if ref and ref.get('exporters_digest') and ref['exporters_digest'] != exporters_digest:
        log.detail('reference outputs were recorded with another set of exporter scripts (%s, now %s): not compared' % (ref['exporters_digest'], exporters_digest))
        ref = None
    if ref:
        for rel, n in sorted(ref.get('line_counts', {}).items()):
            p = os.path.join(stage, rel)
            if os.path.isfile(p) and line_count(p) != n:
                errors.append('%s 行数 %d 与参考 %d 不同' % (rel, line_count(p), n))
        for rel in ref.get('maps', []):
            if rel not in maps:
                errors.append('缺少地图 ' + rel)
        for k in ('models_referenced', 'missing_models', 'missing_textures'):
            if k in ref.get('counts', {}) and counts.get(k) != ref['counts'][k]:
                warnings.append('%s = %s，参考 %s' % (k, counts.get(k), ref['counts'][k]))
    elif counts.get('missing_textures'):
        warnings.append('数据引用了 %d 个资源包里不存在的贴图（原版数据本身也有少量缺失），例如 %s' % (counts['missing_textures'], ', '.join(missing_tex_examples)))
    return {'errors': errors, 'warnings': warnings, 'counts': counts}
