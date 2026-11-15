"""Extract the data the RM8 port needs from your own installed copy of 大富翁8 (RichMan 8).

    rm8_extract.py <游戏文件夹> <输出文件夹> [选项]
    rm8_extract.py --verify <输出文件夹> [--full]

Nothing in the game folder is modified.  The output folder gets
    game\\          the original packs the port reads as they are (Clump, Texture, Sound, Music, Pysrc\\*.pyc, Config\\init.txt)
    scenes\\        data derived from your files (maps, UI, tables, converted movies ...)
    manifest.json  versions + sha256 of everything (the game app checks it)
Run the port with   rm8_app <输出>\\game <输出>\\scenes     (Android: copy game\\ and scenes\\ into Android/data/com.rm8.port/files/)
"""
import argparse
import json
import multiprocessing
import os
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))     # python -I does not add the script folder itself
from rm8x import apk as apkmod, apkcmd, audit, builder, friendly, pipeline, saves as savesmod, toolchain, validate, wizard  # noqa: E402,F401
from rm8x.fingerprint import InstallError, UnsupportedVersion  # noqa: E402
from rm8x.util import Log  # noqa: E402
from rm8x.version import DATA_FORMAT, EXTRACTOR_VERSION, MANIFEST_NAME  # noqa: E402

DISCLAIMER = (
    '本工具只读取你自己合法购买并安装的《大富翁8》，把移植版需要的数据提取/转换到你选择的文件夹，供你本人在自己的设备上使用。\n'
    '提取出的内容仍属原权利人所有，请不要上传、分享或散布输出文件夹、含有这些数据的安装包。移植版与原权利人无关。')


class ChineseParser(argparse.ArgumentParser):
    def error(self, message):
        self.print_usage(sys.stderr)
        sys.stderr.write('命令行参数有问题：%s\n（用 --help 查看所有选项；不带任何参数运行会一步步问你。）\n' % message)
        sys.exit(2)


def parse(argv):
    ap = ChineseParser(prog='rm8_extract', description='从你自己的大富翁8安装中提取移植版需要的数据（提取器 %s，数据格式 %d）。不带任何参数运行（双击 rm8_extract.exe）会一步步问你。' % (EXTRACTOR_VERSION, DATA_FORMAT),
                       epilog='常用：\n  rm8_extract.exe <游戏文件夹> <输出文件夹> --target win         提取并构建 Windows 版\n  rm8_extract.exe <游戏文件夹> <输出文件夹> --target both --saves   两个版本都要，并迁移存档\n  rm8_extract.exe --verify <输出文件夹>                           检查输出是否完好\n详细说明见 README。',
                       formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('game', nargs='?', help='大富翁8 安装文件夹（里面有 RichMan8.exe、Clump、Config 等）')
    ap.add_argument('out', nargs='?', help='输出文件夹（新建或上次提取的文件夹；同一文件夹再次运行 = 更新）')
    ap.add_argument('--version', action='version', version='rm8_extract %s（数据格式 %d）' % (EXTRACTOR_VERSION, DATA_FORMAT), help='显示版本')
    ap.add_argument('--verify', metavar='OUT', help='只检查一个已有的输出文件夹是否完好、版本是否匹配')
    ap.add_argument('--full', action='store_true', help='配合 --verify：重新计算所有文件的 SHA-256（默认只检查存在与大小）')
    ap.add_argument('--no-video', dest='video', action='store_false', help='不转换开场/制作人员影片（不需要 ffmpeg）')
    ap.add_argument('--ffmpeg', help='ffmpeg.exe 的路径（旁边需要有 ffprobe.exe）')
    ap.add_argument('--no-download-ffmpeg', dest='download_ffmpeg', action='store_false', help='找不到 ffmpeg 时不要自动下载（默认会从固定地址下载并校验 SHA-256，需要网络；不想下载可改用 --no-video）')
    ap.add_argument('--download-ffmpeg', dest='download_ffmpeg', action='store_true', help=argparse.SUPPRESS)      # kept for old command lines: now the default
    ap.set_defaults(download_ffmpeg=True)
    cg = ap.add_mutually_exclusive_group()
    cg.add_argument('--no-copy-game', dest='copy_game', action='store_const', const='none', help='不复制原版大文件（约 1.1 GB），运行时直接指向原游戏文件夹（仅电脑版）')
    cg.add_argument('--link-game', dest='copy_game', action='store_const', const='link', help='用硬链接代替复制（同一磁盘分区才有效，否则自动复制）')
    ap.set_defaults(copy_game='copy')
    ap.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 1), help='并行导出地图的进程数（默认 CPU 数，最多 8）')
    ap.add_argument('--allow-unknown', action='store_true', help='允许版本不在受支持列表内的安装继续（结果可能不正确）')
    ap.add_argument('--quick', action='store_true', help='只检查文件大小，不计算源文件的 SHA-256（更快，更不严格）')
    ap.add_argument('--build-tables', action='store_true', help='（开发用）另外生成 build_tables/*.inc：由游戏数据生成的 C++ 头文件（存档用的 Big5 字表、地块顺序），仅繁体版')
    ap.add_argument('--no-reference', action='store_true', help=argparse.SUPPRESS)      # developer: skip the reference check (used to record a new reference)
    ap.add_argument('--rebuild', action='store_true', help='即使输出已是最新也重新提取')
    ap.add_argument('--force', action='store_true', help='输出文件夹非空且没有 manifest.json 时仍继续（只替换 game、scenes、manifest.json）')
    ap.add_argument('--scripts', help='（开发用）导出脚本文件夹，默认使用随附的 exporters\\')
    ap.add_argument('--apk', action='store_true', help='提取完成后，把数据装进移植版的基础 APK，生成手机用的个人 APK（用你本机生成的密钥签名；不需要 Android SDK / JDK）')
    ap.add_argument('--make-apk', metavar='OUT', help='只用一个已有的输出文件夹生成手机用的个人 APK（不重新提取）')
    ap.add_argument('--base-apk', help='基础 APK（rm8_base.apk）的路径；默认找提取器旁边的 rm8_base.apk')
    ap.add_argument('--key-dir', help='APK 签名密钥所在文件夹（默认 %%APPDATA%%\\RM8\\apk_key，第一次使用时自动生成；请备份）')
    ap.add_argument('--apk-out', help='个人 APK 的保存路径（默认在输出文件夹里：rm8_personal_DO_NOT_SHARE.apk，里面有你自己的游戏数据，不要分享）')
    ap.add_argument('--target', choices=['none', 'win', 'android', 'both'], default='none',
                    help='提取后顺便构建移植版：win = 可直接运行的 Windows 文件夹，android = 手机用的个人 APK，both = 两个都要（缺的构建工具会自动下载到私有缓存文件夹，需要网络）')
    ap.add_argument('--build', metavar='OUT', help='不重新提取，只用已有的输出文件夹构建移植版（配合 --target）')
    ap.add_argument('--source', help='移植版源码：源码文件夹（里面有 port\\）或发布页的源码包 .zip；默认找 rm8_extract.exe 旁边的 port\\')
    ap.add_argument('--toolchain-dir', help='构建工具的缓存文件夹（默认 %%LOCALAPPDATA%%\\RM8\\toolchain；不会装进系统，不需要管理员权限）')
    ap.add_argument('--work-dir', help='构建用的工作文件夹（默认 %%LOCALAPPDATA%%\\RM8\\build；重新运行时已完成的步骤会跳过）')
    ap.add_argument('--accept-android-licenses', action='store_true', help='（非交互时）你已阅读并接受 Android SDK 许可协议；不加的话交互运行会显示全文并询问，非交互运行会停下')
    ap.add_argument('--no-install-tools', action='store_true', help='缺构建工具时不要自动下载安装，直接报错')
    ap.add_argument('--zip', action='store_true', help='Windows 版另外压缩成 zip（含你的游戏数据，只限本人使用）')
    sg = ap.add_mutually_exclusive_group()
    sg.add_argument('--saves', dest='saves', action='store_true', default=None, help='打包存档：只读地读出游戏文件夹里的原版存档（Save/record_*.sav）和进度（Config/gamedate.bin；Steam 版还会看 Steam 云存档文件夹），迁移到移植版：电脑版放进数据文件夹（%%APPDATA%%/RM8，已有不同的文件时询问，非交互运行不覆盖），安卓个人 APK 装进 APK（首次启动时复制，已有同名文件不覆盖）')
    sg.add_argument('--no-saves', dest='saves', action='store_false', help='不迁移存档（非交互运行的默认值；交互运行会询问）')
    ap.add_argument('--saves-from', help='存档来源的游戏文件夹（默认就是提取用的游戏文件夹；--build / --make-apk 时默认取输出里 manifest 记录的路径）')
    ap.add_argument('--saves-dir', help='电脑版数据文件夹（默认 %%RM8_DATA_DIR%% 或 %%APPDATA%%/RM8，和移植版读的位置一致）')
    ap.add_argument('--quiet', action='store_true')
    return ap, ap.parse_args(argv)


def verify_mode(out, full):
    log = Log()
    mp = os.path.join(out, MANIFEST_NAME)
    if not os.path.exists(mp):
        log.error('%s 里没有 %s：还没有提取过，或者这不是输出文件夹。' % (out, MANIFEST_NAME)); return 3
    import json
    m = json.load(open(mp, encoding='utf-8'))
    log.info('提取器 %s，数据格式 %s，创建于 %s' % (m.get('extractor_version'), m.get('data_format'), m.get('created')))
    log.info('游戏版本：%s（指纹 %s，%s）' % (m['source'].get('identity'), m['source'].get('fingerprint'), m['source'].get('variant')))
    code = 0
    if m.get('data_format') != DATA_FORMAT:
        log.error('数据格式是 %s，而当前程序需要 %d：请用最新的提取器重新提取。' % (m.get('data_format'), DATA_FORMAT)); code = 4
    bad = validate.check_manifest_files(out, m, log, quick=not full)
    if bad:
        log.error('%d 个文件缺失或损坏，例如：%s' % (len(bad), ', '.join(bad[:8]))); code = 4
    if not code:
        log.info('检查通过（%s，%d 个派生文件，%d 个原版文件）' % ('含 SHA-256' if full else '存在与大小', len(m.get('outputs', {})), len(m.get('game_files', {}))))
    return code


def targets_of(a):
    return {'none': set(), 'win': {'win'}, 'android': {'android'}, 'both': {'win', 'android'}}[a.target]


def prepare_saves(out, a, log, interactive, install_desktop):
    """--saves: read the original saves + profile (read only), stage them in <out>/saves and, for the desktop, put them where the app reads them."""
    if not a.saves:
        return
    out = os.path.abspath(out)
    game = a.saves_from or a.game
    if not game:
        try:
            game = json.load(open(os.path.join(out, MANIFEST_NAME), encoding='utf-8')).get('source', {}).get('game_dir') or None
        except (OSError, ValueError):
            game = None
    log.step('迁移存档（只读取游戏文件夹，不会修改它）')
    found = None
    if game and os.path.isdir(game):
        found = savesmod.find(game, log)
        if not savesmod.describe(found, log):
            found = None
    elif game:
        log.warn('存档来源文件夹不存在：%s' % game)
    staged = os.path.join(out, 'saves')
    if found:
        try:
            savesmod.stage(out, found, log)
        except OSError as e:
            raise pipeline.ExtractError('复制存档失败：%s' % e)
    elif os.path.isdir(staged):
        log.info('  没有读到新的存档，沿用输出文件夹里上次整理好的 saves 文件夹')
    if not savesmod.staged_files(staged):
        log.warn('没有可迁移的存档或进度。')
        return
    a.saves_staged = staged
    if install_desktop:
        c, k = savesmod.install_desktop(staged, savesmod.desktop_data_dir(a.saves_dir), interactive, log)
        log.info('  电脑版：复制 %d 个文件，保留已有的 %d 个。' % (c, k))
    else:
        log.info('  已整理到 %s，会装进个人 APK。' % staged)


def after_extract(out, a, log, interactive):
    """Build the port / make the personal APK for the extraction in `out`."""
    targets = targets_of(a)
    prepare_saves(out, a, log, interactive, install_desktop=(a.target != 'android' and not a.apk) or a.target in ('win', 'both'))
    if targets:
        mp = os.path.join(os.path.abspath(out), MANIFEST_NAME)
        manifest = json.load(open(mp, encoding='utf-8'))
        if manifest.get('options', {}).get('copy_game') == 'none':
            raise builder.BuildError('这次提取用了 --no-copy-game，输出里没有 game\\ 文件夹，不能打包。请重新提取（不加 --no-copy-game）。')
        return builder.build(out, manifest, targets, a, log, interactive)
    if a.apk:
        return apkcmd.make_personal_apk(out, a, log)
    return 0


def fail(log, e, code, hint=True):
    """Print a failure the user can act on and return the exit code (details are in extract.log).  The "report it" hint only goes with
    failures the user cannot fix (internal errors, a failed build, a failed check): not with a wrong folder, no network, no space ..."""
    log.error(str(e))
    if hint and code not in (2, 3, 130):
        log.info('\n如果按上面的办法仍然不行，请到本项目仓库的 Issues 页面反馈，并附上输出文件夹里的 extract.log（里面只有文件路径和步骤记录，没有你的个人信息）。')
    return code


def handle(log, e):
    """Map an exception to a (message, exit code); None when it is not one we know."""
    if isinstance(e, (builder.BuildError, toolchain.ToolError)):
        return str(e), 6
    if isinstance(e, apkmod.ApkError):
        return str(e), 4
    if isinstance(e, InstallError):
        return str(e), 3
    if isinstance(e, pipeline.ExtractError):
        return str(e), 4
    if isinstance(e, OSError):
        return friendly.os_error_message(e), 4
    return None


def finish(out, a, log):
    """The "what you got and where" block (console + <out>\\结果说明.txt)."""
    if a.quiet or not os.path.isdir(out):
        return
    text = wizard.summary(out, a)
    log.info('\n' + text)
    wizard.write_summary_file(out, text)


def run_wrapped(fn, log, interactive=False):
    """Run fn() and turn every failure into a plain-Chinese message + exit code."""
    try:
        return fn()
    except KeyboardInterrupt:
        log.error('已取消。已经完成的步骤下次会自动跳过；输出文件夹里原有的 game / scenes 没有被改动。')
        return 130
    except wizard.Cancelled:
        log.error('已取消。')
        return 2
    except Exception as e:
        m = handle(log, e)
        if m:
            return fail(log, m[0], m[1], hint=not getattr(e, 'no_hint', False) and not (isinstance(e, OSError) and (friendly.is_disk_full(e) or isinstance(e, PermissionError))))
        return fail(log, '程序出了意料之外的错误（不是你的操作问题）：\n%s' % traceback.format_exc(), 5)


def main(argv=None):
    ap, a = parse(argv)
    interactive = not (a.game or a.verify or a.make_apk or a.build)
    if a.verify:
        return verify_mode(os.path.abspath(a.verify), a.full)
    if a.make_apk:
        log = Log(None, quiet=a.quiet)

        def go_apk():
            if os.path.isdir(a.make_apk):
                log.attach(os.path.join(os.path.abspath(a.make_apk), 'extract.log'))
            prepare_saves(a.make_apk, a, log, sys.stdin.isatty(), install_desktop=False)
            rc = apkcmd.make_personal_apk(a.make_apk, a, log)
            if rc == 0:
                finish(a.make_apk, a, log)
            return rc
        try:
            return run_wrapped(go_apk, log)
        finally:
            log.close()
    if a.build:
        log = Log(None, quiet=a.quiet)

        def go_build():
            if a.target == 'none' and not a.saves:
                a.target = 'both'
            if os.path.isdir(a.build):
                log.attach(os.path.join(os.path.abspath(a.build), 'extract.log'))
            rc = after_extract(a.build, a, log, sys.stdin.isatty())
            if rc == 0:
                finish(a.build, a, log)
            return rc
        try:
            return run_wrapped(go_build, log)
        finally:
            log.close()
    if interactive:
        try:
            if not wizard.run(a):
                print('\n已取消，什么都没有做。')
                return 2
        except wizard.Cancelled:
            print('\n已取消，什么都没有做。')
            return 2
        except KeyboardInterrupt:
            print('\n已取消，什么都没有做。')
            return 130
    if not a.game or not a.out:
        ap.print_usage(sys.stderr)
        print('需要两个参数：游戏文件夹 和 输出文件夹。（不带任何参数运行 rm8_extract.exe，会一步步问你。）', file=sys.stderr)
        return 2
    log = Log(None, quiet=a.quiet)
    log.info('rm8_extract %s（数据格式 %d）' % (EXTRACTOR_VERSION, DATA_FORMAT))
    if not interactive:
        log.info(DISCLAIMER)
    a.saves = bool(a.saves)       # not asked (non-interactive run) and no flag: off
    if (a.apk or a.target != 'none') and a.copy_game == 'none':
        log.error('--apk / --target 需要完整的 game\\ 文件夹，不能和 --no-copy-game 一起用。'); return 2

    def go():
        try:
            rc = pipeline.extract(a.game, a.out, a, log)
        except UnsupportedVersion as e:
            if not (interactive and wizard.confirm_unknown(e)):
                raise
            a.allow_unknown = True
            log.info('按你的选择继续（未识别的版本，结果可能不正确）。')
            rc = pipeline.extract(a.game, a.out, a, log)
        if rc == 0:
            rc = after_extract(a.out, a, log, interactive or sys.stdin.isatty())
        if rc == 0:
            finish(a.out, a, log)
        return rc
    try:
        return run_wrapped(go, log, interactive)
    finally:
        log.close()


def _console_setup():
    """Chinese text must survive a redirected / piped run, and a piped stdin (tests, scripts) is read as UTF-8."""
    for _s in (sys.stdout, sys.stderr):
        try:
            _s.reconfigure(encoding='utf-8', errors='replace')
        except Exception:
            pass
    try:
        if sys.stdin and not sys.stdin.isatty():
            sys.stdin.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass


if __name__ == '__main__':
    multiprocessing.freeze_support()
    _console_setup()
    rc = main()
    if len(sys.argv) == 1 and getattr(sys, 'frozen', False):
        try:
            input('\n按回车键关闭窗口...')
        except (EOFError, KeyboardInterrupt):
            pass
    sys.exit(rc)
