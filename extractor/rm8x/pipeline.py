"""The extraction pipeline: validate the install, run the exporters into a staging folder, copy the raw files, validate, write the manifest,
and swap the result into the output folder.  Everything the original exporters (scripts\\export_*.py) do stays untouched; they are run
in-process (runpy) from a private copy so that the packaged and the dev flow behave the same."""
import concurrent.futures as cf
import contextlib
import datetime
import glob
import io
import json
import multiprocessing
import os
import runpy
import shutil
import sys
import tempfile
import time
import traceback

from . import audit, fingerprint, pyc24, savetables, validate
from .fingerprint import find_case
from .ffmpeg_tool import find_ffmpeg
from .util import Log, app_dir, digest_of, human, sha256_file
from .version import DATA_FORMAT, EXTRACTOR_VERSION, MANIFEST_NAME

OWNED = ('game', 'scenes', 'build_tables', MANIFEST_NAME, 'extract.log')


class ExtractError(Exception):
    pass


class UserExtractError(ExtractError):
    no_hint = True


# ---------------------------------------------------------------------------------------------- running an exporter script
def run_exporter(scripts_dir, name, args):
    """Run scripts_dir/name as __main__ with argv=args. Returns (ok, captured stdout/stderr)."""
    path = os.path.join(scripts_dir, name)
    buf = io.StringIO()
    old_argv, old_path = sys.argv, list(sys.path)
    sys.argv = [path] + list(args)
    sys.path.insert(0, scripts_dir)
    ok = True
    try:
        with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
            runpy.run_path(path, run_name='__main__')
    except SystemExit as e:
        if e.code not in (None, 0):
            ok = False
            buf.write('\n%s exited with %r\n' % (name, e.code))
    except BaseException:
        ok = False
        buf.write('\n' + traceback.format_exc())
    finally:
        sys.argv, sys.path[:] = old_argv, old_path
    return ok, buf.getvalue()


def patch_prteff(scripts_dir, game):
    """scripts/prteff.py hard-codes the dev game folder (also in a default argument): point it at the real one."""
    sys.path.insert(0, scripts_dir)
    try:
        import prteff
    finally:
        sys.path.remove(scripts_dir)
    g = os.path.abspath(game)
    prt, uvl = os.path.join(g, 'Config', 'prt', 'PrtEffTable.tb'), os.path.join(g, 'Config', 'prt', 'uvlisttable.tb')
    defaults = prteff.load_tables.__defaults__
    if not defaults or len(defaults) != 2:
        raise ExtractError('内部错误：prteff.load_tables 的默认参数形式变了，请更新提取器')
    prteff.load_tables.__defaults__ = (prt, uvl)
    prteff.GAME, prteff.PRT_TABLE, prteff.UVL_TABLE = g, prt, uvl
    prteff.TEX_DAT = os.path.join(g, 'Texture', 'tex0000.dat')


# worker process of the map export pool
def _worker_init(game, allowed, scripts_dir):
    sys.dont_write_bytecode = True
    audit.install(game, allowed)
    patch_prteff(scripts_dir, game)


def _map_job(args):
    scripts_dir, game, mid, outdir = args
    t0 = time.time()
    os.makedirs(outdir, exist_ok=True)
    ok, text = run_exporter(scripts_dir, 'export_scene.py', [game, mid, outdir])
    reads, foreign = audit.snapshot()
    return mid, ok, text, time.time() - t0, reads, foreign


# ---------------------------------------------------------------------------------------------- steps
def discover_maps(game):
    md = find_case(game, 'Map')
    names = sorted(os.path.splitext(f)[0] for f in os.listdir(md) if f.lower().endswith('.map')
                   and os.path.exists(os.path.join(md, os.path.splitext(f)[0] + '.ltm')))
    return names


def load_edition(scripts_dir):
    """scripts/rm8_edition.py (detects the Big5 'tc' install or the GBK Steam 'sc' release, picks the Config key + text encoding)."""
    sys.path.insert(0, scripts_dir)
    try:
        import rm8_edition
    finally:
        sys.path.remove(scripts_dir)
    return rm8_edition


def stage_bin_text(game, scripts_dir):
    """Config/stage.bin decrypted + decoded (what scripts/export_stages.py reads as out/config_utf8/stage.bin.txt)."""
    ed = load_edition(scripts_dir)
    d = ed.decrypt(game, open(ed.config_path(game, 'stage.bin'), 'rb').read())
    encs = ('big5hkscs', 'cp950', 'gbk') if ed.detect(game)['id'] == 'tc' else ('gbk', 'big5hkscs')    # tc: the order scripts/to_utf8.py used
    for enc in encs:
        try:
            return d.decode(enc)
        except UnicodeDecodeError:
            pass
    return d.decode(ed.detect(game)['enc'], errors='replace')


def story_inputs(game, work):
    """export_story.py reads ../out/story/missions.json and movie_stats.json next to the scripts.
    missions.json: our own table (data/missions.json); movie_stats.json: art ids of Movie.ArtCreate, read from the game's cutscene .pyc files."""
    d = os.path.join(work, 'out', 'story')
    os.makedirs(d, exist_ok=True)
    shutil.copyfile(os.path.join(app_dir(), 'data', 'missions.json'), os.path.join(d, 'missions.json'))
    ids = set()
    n = 0
    for f in sorted(glob.glob(os.path.join(find_case(game, 'Pysrc') or game, 'da*.pyc'))):
        try:
            ids |= set(pyc24.art_create_ids(open(f, 'rb').read())); n += 1
        except ValueError as e:
            raise ExtractError('读不懂游戏的过场脚本 %s：%s\n这通常说明游戏文件被改动过或已损坏；Steam 版请先“验证游戏文件的完整性”。' % (os.path.basename(f), e))
    if not n:
        raise ExtractError('游戏的 Pysrc 文件夹里没有过场脚本（da*.pyc）：游戏文件可能不完整')
    json.dump({'artCreate_ids': {str(i): 1 for i in sorted(ids)}, 'note': 'generated by rm8x.pyc24 from %d da*.pyc files' % n},
              open(os.path.join(d, 'movie_stats.json'), 'w', encoding='utf-8'), indent=1)
    return len(ids)


def run_steps(game, stage, work, scripts_dir, opts, log):
    """Run every exporter into stage/scenes. Returns the list of (step, seconds)."""
    scenes = os.path.join(stage, 'scenes')
    os.makedirs(scenes, exist_ok=True)
    allowed = [work, stage, tempfile.gettempdir(), app_dir()]
    patch_prteff(scripts_dir, game)
    timings = []

    def one(label, script, args):
        t0 = time.time()
        log.info('  %s ...' % label)
        ok, text = run_exporter(scripts_dir, script, args)
        log.detail(text.strip())
        if not ok:
            raise ExtractError('导出步骤“%s”失败（详见 extract.log）：\n%s' % (label, text.strip()[-600:]))
        timings.append((label, time.time() - t0))

    # 1) the maps (heavy; in parallel)
    maps = discover_maps(game)
    if not maps:
        raise ExtractError('游戏的 Map 文件夹里没有地图文件（.map / .ltm）：游戏文件可能不完整')
    log.info('  地图 %d 张 (%s) ...' % (len(maps), ' '.join(maps)))
    t0 = time.time()
    jobs = [(scripts_dir, game, m, os.path.join(scenes, m)) for m in maps]
    nproc = max(1, min(opts.jobs, len(jobs)))
    if nproc == 1:
        results = [_map_job(j) for j in jobs]
    else:
        ctx = multiprocessing.get_context('spawn')
        with cf.ProcessPoolExecutor(max_workers=nproc, mp_context=ctx, initializer=_worker_init, initargs=(game, allowed, scripts_dir)) as ex:
            results = list(ex.map(_map_job, jobs))
    for mid, ok, text, secs, reads, foreign in results:
        log.detail('map %s: %.1fs\n%s' % (mid, secs, text.strip()))
        audit.merge(reads, foreign)
        if not ok:
            raise ExtractError('地图 %s 导出失败（详见 extract.log）：\n%s' % (mid, text.strip()[-600:]))
    timings.append(('maps', time.time() - t0))

    # 2) shared tables
    one('奇迹地块 (wonders.txt)', 'export_wonders.py', [game, scenes])       # writes scenes/<map>/wonders.txt for every map
    one('界面 (UI)', 'export_ui.py', [game, os.path.join(scenes, 'ui')])
    one('角色', 'export_roles.py', [game, os.path.join(scenes, 'roles.txt')])
    one('NPC', 'export_npcs.py', [game, os.path.join(scenes, 'npcs.txt')])
    one('粒子特效', 'export_particles.py', [os.path.join(scenes, 'particles.txt')])
    one('卡片', 'export_cards.py', [game, os.path.join(scenes, 'cards.txt')])
    one('股票', 'export_stocks.py', [game, os.path.join(scenes, 'stocks.txt')])
    sb = os.path.join(work, 'stage.bin.txt')
    open(sb, 'w', encoding='utf-8', newline='').write(stage_bin_text(game, scripts_dir))
    one('关卡列表', 'export_stages.py', [sb, os.path.join(scenes, 'stages.txt')])
    one('新闻', 'export_news.py', [game, os.path.join(scenes, 'news.txt')])
    one('地图摆件介绍', 'export_introduce.py', [game, os.path.join(scenes, 'introduce.txt')])
    one('问答题', 'export_questions.py', [game, os.path.join(scenes, 'questions.txt')])
    one('事件展示模型', 'export_event_arts.py', [game, os.path.join(scenes, 'event_arts.txt')])
    one('小游戏精灵', 'export_minigame.py', [game, os.path.join(scenes, 'minigame')])
    n_arts = story_inputs(game, work)
    log.detail('story: %d movie art ids from the cutscene scripts' % n_arts)
    one('闯关模式', 'export_story.py', [game, os.path.join(scenes, 'story.txt'), os.path.join(scenes, 'movie_arts.txt')])
    one('繁转简字表 (OpenCC)', 'make_t2s.py', [os.path.join(app_dir(), 'data', 'opencc'), os.path.join(scenes, 'zh_t2s.txt')])

    # 2a) the save-format tables that used to be compiled into the app (scenes/save_tables.txt)
    t0 = time.time()
    ed = load_edition(scripts_dir).detect(game)
    inc = os.path.join(work, 'save_terra_order.inc')
    ok, text = run_exporter(scripts_dir, 'export_save_terra_order.py', [game, inc])
    log.detail(text.strip())
    if not ok:
        raise ExtractError('导出步骤“存档用的地块顺序”失败（详见 extract.log）：\n' + text.strip()[-600:])
    try:
        savetables.write_save_tables(os.path.join(scenes, 'save_tables.txt'), ed['enc'], savetables.native_codes(os.path.join(scenes, 'stocks.txt'), ed['enc']),
                                     savetables.parse_terra_inc(open(inc, encoding='ascii').read()))
    except ValueError as e:
        raise ExtractError('导出步骤“存档表”失败：%s' % e)
    timings.append(('save tables', time.time() - t0))

    # 2b) optional build-time tables (C++ include files generated from the game data; only valid for the Big5 edition: they encode stock names as Big5)
    if getattr(opts, 'build_tables', False):
        bt = os.path.join(stage, 'build_tables')
        os.makedirs(bt, exist_ok=True)
        if load_edition(scripts_dir).detect(game)['id'] != 'tc':
            log.warn('--build-tables 只对繁体(Big5)版有效，已跳过')
        else:
            one('存档表 save_big5.inc', 'export_save_big5.py', [os.path.join(scenes, 'stocks.txt'), os.path.join(bt, 'save_big5.inc')])
            one('存档表 save_terra_order.inc', 'export_save_terra_order.py', [game, os.path.join(bt, 'save_terra_order.inc')])

    # 3) videos (Bink -> MPEG-1), needs ffmpeg
    video = {'status': 'skipped'}
    if opts.video:
        ff = find_ffmpeg(opts, log)
        if ff:
            t0 = time.time()
            log.info('  影片 (Bink -> MPEG-1) 使用 %s ...' % ff['path'])
            ok, text = run_exporter(scripts_dir, 'convert_videos.py', ['--game', game, '--ffmpeg', ff['path'], '--out', os.path.join(scenes, 'video'), '--tolerance', '0.5'])
            log.detail(text.strip())
            if not ok:
                raise ExtractError('影片转换失败（详见 extract.log）：\n' + text.strip()[-600:])
            timings.append(('video', time.time() - t0))
            video = {'status': 'converted', 'ffmpeg': ff}
        else:
            log.warn('没有可用的 ffmpeg（找不到，且下载被关闭或失败），跳过开场/制作人员影片（游戏仍可玩，只是没有这两段影片）。可用 --ffmpeg 指定你自己的 ffmpeg 后重新运行。')
            video = {'status': 'no-ffmpeg'}
    else:
        log.info('  已按要求跳过影片')

    # the overview image is a developer check picture of the map (a render of copyrighted art that the game never reads)
    for m in maps:
        p = os.path.join(scenes, m, 'overview.png')
        if os.path.exists(p):
            os.remove(p)
    return timings, video, maps


# ---------------------------------------------------------------------------------------------- raw files
INIT_KEYS = ('sound', 'music', 'speed', 'difficuty', 'character', 'binkplay')    # the only init.txt keys the app reads


def write_init(game, stage):
    """game/Config/init.txt for the app: only the keys it reads, taken from the user's own file (the original also holds window
    settings and an unrelated token that need not travel with the data)."""
    src = find_case(game, 'Config/init.txt') or ''
    kept = []
    if os.path.exists(src):
        for line in open(src, 'rb').read().decode('cp950', errors='replace').splitlines():
            k, _, v = line.partition('=')
            if k.strip() in INIT_KEYS and v.strip().lstrip('-').isdigit():
                kept.append('%s=%d' % (k.strip(), int(v.strip())))
    dst = os.path.join(stage, 'game', 'Config', 'init.txt')
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, 'w', encoding='ascii', newline='\n') as f:
        f.write('[param]\n' + '\n'.join(kept) + ('\n' if kept else ''))
    return {'path': 'Config/init.txt', 'keys': [k.split('=')[0] for k in kept]}


def copy_raw(game, stage, files, mode, log):
    """Copy (or hard-link) the needed original files into stage/game. Returns {relpath: {size, sha256}}."""
    out = {}
    total = sum(f['size'] for f in files)
    done = 0
    t0 = time.time()
    for f in files:
        src = find_case(game, f['path'])
        dst = os.path.join(stage, 'game', f['path'])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        linked = False
        if mode == 'link':
            try:
                os.link(src, dst); linked = True
            except OSError:
                linked = False
        if not linked:
            shutil.copyfile(src, dst)
        if os.path.getsize(dst) != f['size']:
            raise ExtractError('复制 %s 后大小对不上：磁盘可能已满，或磁盘有问题。请腾出空间后重新运行' % f['path'])
        out[f['path']] = {'size': f['size'], 'sha256': f['sha256'], 'mode': 'link' if linked else 'copy'}
        done += f['size']
        if f['size'] > 20 << 20:
            log.info('  %s  (%s, 累计 %s / %s, %.0fs)' % (f['path'], human(f['size']), human(done), human(total), time.time() - t0))
    return out


# ---------------------------------------------------------------------------------------------- output folder handling
def check_output_dir(game, out, force):
    g, o = os.path.normcase(os.path.abspath(game)), os.path.normcase(os.path.abspath(out))
    if o == g or o.startswith(g + os.sep) or g.startswith(o + os.sep):
        raise UserExtractError('输出文件夹不能与游戏文件夹相同、也不能互相包含：\n  游戏 %s\n  输出 %s' % (game, out))
    if os.path.isdir(out) and os.listdir(out):
        names = set(os.listdir(out))
        if MANIFEST_NAME in names or force:
            return
        stray = sorted(names - {'rm8_settings.txt', 'rm8_gamedate.bin', 'rm8_box.bin', 'extract.log', 'saves'})
        if stray:
            raise UserExtractError('输出文件夹不是空的，而且不是上次提取的结果：%s\n请换一个空文件夹（或确认无误后加 --force，只会替换 game、scenes、manifest.json）。' % out)


def swap_in(stage, out, log):
    """Replace game/, scenes/, manifest.json in out by the staged ones (user files next to them, e.g. saves / settings, stay)."""
    os.makedirs(out, exist_ok=True)
    tag = '.old-%d' % os.getpid()
    moved = []
    try:
        for name in ('game', 'scenes', 'build_tables', MANIFEST_NAME):
            new, cur = os.path.join(stage, name), os.path.join(out, name)
            if os.path.exists(cur):
                os.rename(cur, cur + tag); moved.append(cur)
            if os.path.exists(new):
                os.rename(new, cur)
    except OSError:
        for cur in moved:                                  # roll back
            if os.path.exists(cur): shutil.rmtree(cur, ignore_errors=True) if os.path.isdir(cur) else os.remove(cur)
            if os.path.exists(cur + tag): os.rename(cur + tag, cur)
        raise
    for cur in moved:
        p = cur + tag
        shutil.rmtree(p, ignore_errors=True) if os.path.isdir(p) else os.remove(p)


# ---------------------------------------------------------------------------------------------- main entry
def extract(game, out, opts, log):
    t_start = time.time()
    game, out = os.path.abspath(game), os.path.abspath(out)
    sys.dont_write_bytecode = True

    # 0. is the output folder usable (nothing is written before this passes)
    check_output_dir(game, out, opts.force)
    # 1. is this the game?
    log.step('检查游戏文件夹 %s' % game)
    report = fingerprint.verify_install(game, log, need_video=opts.video, allow_unknown=opts.allow_unknown, quick=opts.quick, use_reference=not opts.no_reference)
    os.makedirs(out, exist_ok=True)
    log.attach(os.path.join(out, 'extract.log'))

    # 2. is there an up-to-date result already?
    mpath = os.path.join(out, MANIFEST_NAME)
    if os.path.exists(mpath) and not opts.rebuild:
        try:
            m = json.load(open(mpath, encoding='utf-8'))
            if (m.get('extractor_version') == EXTRACTOR_VERSION and m.get('data_format') == DATA_FORMAT
                    and m.get('source', {}).get('fingerprint') == report['fingerprint']
                    and m.get('options', {}).get('video') == bool(opts.video)
                    and m.get('options', {}).get('copy_game') == opts.copy_game
                    and m.get('options', {}).get('build_tables', False) == bool(getattr(opts, 'build_tables', False))):
                bad = validate.check_manifest_files(out, m, log, quick=True)
                if not bad:
                    log.info('输出已是最新（同一游戏文件、同一提取器版本），无需重新提取。要强制重做请加 --rebuild。')
                    return 0
                log.info('已有输出不完整（%d 个文件异常），将重新提取。' % len(bad))
        except (ValueError, OSError) as e:
            log.detail('old manifest unreadable: %s' % e)

    # 3. disk space
    need = sum(f['size'] for f in report['raw_files']) if opts.copy_game == 'copy' else 0
    need += 400 << 20
    os.makedirs(out, exist_ok=True)
    free = shutil.disk_usage(out).free
    if free < need:
        raise UserExtractError('磁盘空间不够：输出文件夹所在的磁盘需要约 %s，现在只剩 %s。请清理空间，或换一个空间大的输出文件夹' % (human(need), human(free)))

    stage = os.path.join(out, '.staging-%d' % os.getpid())
    work = tempfile.mkdtemp(prefix='rm8x_')
    scripts_dir = os.path.join(work, 'scripts')
    try:
        os.makedirs(stage)
        src_scripts = opts.scripts or (os.path.join(app_dir(), 'exporters') if os.path.isdir(os.path.join(app_dir(), 'exporters')) else None)
        if not src_scripts or not os.path.isdir(src_scripts):
            raise ExtractError('程序自带的导出脚本找不到（程序文件不完整？请重新下载 rm8_extract.exe）exporters\\（运行 tools\\sync_exporters.py，或用 --scripts 指定）')
        shutil.copytree(src_scripts, scripts_dir, ignore=shutil.ignore_patterns('__pycache__', 'MANIFEST.json'))
        exporter_hashes = {f: sha256_file(os.path.join(scripts_dir, f)) for f in sorted(os.listdir(scripts_dir)) if f.endswith('.py')}
        audit.install(game, [work, stage, tempfile.gettempdir(), app_dir()])

        log.step('导出派生数据 → scenes\\ (jobs=%d)' % opts.jobs)
        timings, video, maps = run_steps(game, stage, work, scripts_dir, opts, log)
        reads, foreign = audit.snapshot()
        if foreign:
            raise ExtractError('导出脚本读取了游戏文件夹以外的文件（内部错误，请报告）：\n  ' + '\n  '.join(foreign[:10]))
        log.detail('game files read by the exporters (%d): %s' % (len(reads), ', '.join(reads)))

        raw = {}
        generated = {}
        if opts.copy_game != 'none':
            log.step('复制需要的原版文件 → game\\ (%s)' % ('硬链接' if opts.copy_game == 'link' else '复制'))
            raw = copy_raw(game, stage, report['raw_files'], opts.copy_game, log)
            generated['game/Config/init.txt'] = write_init(game, stage)
        else:
            log.info('不复制原版文件（--no-copy-game）：运行时需直接指向原游戏文件夹')

        log.step('校验结果')
        vres = validate.validate_output(stage, game, raw, maps, video, log, copied=(opts.copy_game != 'none'), strict=not opts.allow_unknown, ref_id=report['reference_id'], exporters_digest=digest_of(exporter_hashes))
        if vres['errors']:
            raise ExtractError('提取完成后的自检没通过，数据可能不完整（请重新运行一次；仍然如此就把 extract.log 发给开发者）：\n  ' + '\n  '.join(vres['errors'][:15]))

        outputs = validate.hash_tree(stage, ('scenes', 'build_tables'))
        manifest = {
            'format': 'rm8-extract-manifest',
            'extractor_version': EXTRACTOR_VERSION,
            'data_format': DATA_FORMAT,
            'created': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
            'python': sys.version.split()[0],
            'options': {'video': bool(opts.video), 'copy_game': opts.copy_game, 'build_tables': bool(getattr(opts, 'build_tables', False))},
            'source': {'game_dir': game, 'fingerprint': report['fingerprint'], 'identity': report['identity'], 'edition': report['edition'],
                       'reference_id': report['reference_id'], 'variant': report['variant'], 'files': report['source_files']},
            'game_files': raw,
            'generated_game_files': generated,
            'game_dir_for_runtime': ('game' if opts.copy_game != 'none' else game),
            'scenes_dir_for_runtime': 'scenes',
            'maps': maps,
            'video': video,
            'exporters': exporter_hashes,
            'exporters_digest': digest_of(exporter_hashes),
            'data_files': {'missions.json': sha256_file(os.path.join(app_dir(), 'data', 'missions.json'))},
            'game_files_read_by_exporters': reads,
            'outputs': outputs,
            'warnings': vres['warnings'] + report['warnings'],
            'timings': {k: round(v, 1) for k, v in timings},
        }
        json.dump(manifest, open(os.path.join(stage, MANIFEST_NAME), 'w', encoding='utf-8', newline='\n'), ensure_ascii=False, indent=1, sort_keys=True)
        swap_in(stage, out, log)
        log.step('完成：%s（用时 %.0f 秒）' % (out, time.time() - t_start))
        for w in manifest['warnings']:
            log.warn(w)
        return 0
    finally:
        shutil.rmtree(stage, ignore_errors=True)
        shutil.rmtree(work, ignore_errors=True)
