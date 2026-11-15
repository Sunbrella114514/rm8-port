"""The double-click flow of rm8_extract.exe (no command-line arguments): a few plain-Chinese questions, then the same pipeline the command line runs.

  1) game folder (found automatically in Steam's library folders / the usual places of an old copy, or typed / pasted / picked in a window)
  2) output folder   3) what to build (Windows / Android / both / data only)   4) migrate the original saves
Nothing is created before the last "start?" question; every question can be answered with just Enter (the default is shown in [ ]).
"""
import ctypes
import os
import shutil
import sys

from . import detect, ffmpeg_tool, friendly, netdl, pipeline, saves as savesmod, toolchain
from .util import Log, human
from .version import DATA_FORMAT, EXTRACTOR_VERSION, MANIFEST_NAME

LINE = '=' * 64
DATA_NEED = 1500 << 20                  # the extraction itself (game\ 1.0 GB + scenes\ 0.2 GB + a little)
TARGET_TEXT = {'none': '只提取数据（不构建）', 'win': '提取数据 + 构建 Windows 版', 'android': '提取数据 + 构建安卓手机版（个人 APK）', 'both': '提取数据 + 构建 Windows 版和安卓手机版'}


class Cancelled(Exception):
    pass


# ------------------------------------------------------------------------------------------------------ console helpers
def disable_quick_edit():
    """A click inside a console window pauses a running program ("QuickEdit").  Switch that off for the double-click flow so a stray click does not freeze the build."""
    if os.name != 'nt' or not sys.stdin or not sys.stdin.isatty():
        return
    try:
        k = ctypes.windll.kernel32
        h = k.GetStdHandle(-10)
        mode = ctypes.c_uint32()
        if k.GetConsoleMode(h, ctypes.byref(mode)):
            k.SetConsoleMode(h, (mode.value | 0x80) & ~0x40)      # ENABLE_EXTENDED_FLAGS on, ENABLE_QUICK_EDIT_MODE off
    except Exception:
        pass


def ask(prompt):
    try:
        return input(prompt).strip()
    except EOFError:
        raise Cancelled()


def yesno(prompt, default=True):
    while True:
        s = ask('%s [%s]：' % (prompt, 'Y/n' if default else 'y/N')).lower()
        if not s:
            return default
        if s in ('y', 'yes', '是', '好', '1'):
            return True
        if s in ('n', 'no', '否', '不', '0'):
            return False
        print('  请输入 y（是）或 n（否）。')


def clean_path(s):
    s = s.strip().strip('"').strip("'").strip()
    return os.path.expandvars(os.path.expanduser(s)) if s else s


def pick_folder(title):
    """A folder-picker window; None when cancelled or when no window can be shown."""
    try:
        import tkinter
        from tkinter import filedialog
        r = tkinter.Tk()
        r.withdraw()
        r.attributes('-topmost', True)
        p = filedialog.askdirectory(title=title)
        r.destroy()
        return os.path.normpath(p) if p else None
    except Exception:
        print('  （这台电脑上打不开选择窗口，请直接把文件夹的路径粘贴到这里。）')
        return None


def existing_ancestor(path):
    p = os.path.abspath(path)
    while p and not os.path.exists(p):
        q = os.path.dirname(p)
        if q == p:
            break
        p = q
    return p


def free_space(path):
    try:
        return shutil.disk_usage(existing_ancestor(path)).free
    except OSError:
        return None


# ------------------------------------------------------------------------------------------------------ the questions
def choose_game():
    print('\n【第 1 步 / 4】选择《大富翁8》的游戏文件夹（装有 RichMan8.exe 的那个文件夹，只读取，不会被改动）')
    found = detect.find_all()
    while True:
        if found:
            print('已在这台电脑上找到：')
            for i, (p, label) in enumerate(found, 1):
                print('  %d) %s    （%s）' % (i, p, label))
            print('直接按回车 = 用第 1 个；也可以输入别的编号、直接粘贴文件夹路径，或输入 b 打开选择窗口、q 退出。')
        else:
            print('没有在常见位置自动找到游戏。请把游戏文件夹的路径粘贴到这里（Steam 版在 steamapps\\common\\Richman 8），或输入 b 打开选择窗口、q 退出。')
        s = ask('游戏文件夹：' if not found else '请选择 [1]：')
        if s.lower() == 'q':
            raise Cancelled()
        if not s and found:
            return found[0][0]
        if s.isdigit() and found and 1 <= int(s) <= len(found):
            return found[int(s) - 1][0]
        if s.lower() == 'b':
            p = pick_folder('选择大富翁8 的游戏文件夹')
            if not p:
                continue
        else:
            p = clean_path(s)
        if not p:
            continue
        if os.path.isfile(p):
            p = os.path.dirname(p)
        if not os.path.isdir(p):
            print('\n  找不到这个文件夹：%s\n  请检查有没有输错（可以从资源管理器的地址栏复制）。\n' % p)
            continue
        if detect.looks_like_game(p):
            return p
        fix = detect.suggest_fix(p)
        if fix and yesno('\n  这个文件夹里没有直接找到游戏。你是不是想选这个：%s\n  用它吗？' % fix, True):
            return fix
        print('\n  这个文件夹不是《大富翁8》的安装文件夹：%s\n  游戏文件夹里应该有 RichMan8.exe，以及 Clump、Config、Texture 等子文件夹。\n' % p)


def choose_out(game):
    print('\n【第 2 步 / 4】选择输出文件夹（提取出的数据和构建好的移植版都放在这里，数据约 1.2 GB，生成手机版 APK 再加约 0.6 GB）')
    default = os.path.join(os.path.expanduser('~'), 'RM8')
    while True:
        print('直接按回车 = %s；也可以粘贴别的路径（建议放在空间大的磁盘，路径里不要有太多层），输入 b 打开选择窗口。' % default)
        s = ask('输出文件夹 [%s]：' % default)
        if s.lower() == 'q':
            raise Cancelled()
        if s.lower() == 'b':
            p = pick_folder('选择输出文件夹（建议新建一个空文件夹）')
            if not p:
                continue
        else:
            p = clean_path(s) or default
        p = os.path.abspath(p)
        try:
            pipeline.check_output_dir(game, p, False)
        except pipeline.ExtractError as e:
            print('\n  %s\n' % e)
            continue
        free = free_space(p)
        if free is not None and free < DATA_NEED:
            print('\n  这个文件夹所在的磁盘只剩 %s，不够放数据（至少要 %s）。请换一个磁盘。\n' % (human(free), human(DATA_NEED)))
            continue
        if os.path.exists(os.path.join(p, MANIFEST_NAME)):
            print('  这是以前提取过的文件夹：会检查并更新它（里面的存档、设置不会动）。')
        try:                                  # can we write there?  (tested in the nearest existing folder: nothing is created before the final "start?")
            probe = os.path.join(existing_ancestor(p), '.rm8_write_test')
            open(probe, 'w').close()
            os.remove(probe)
        except OSError as e:
            print('\n  不能在这个位置写文件：%s\n' % friendly.os_error_message(e))
            continue
        return p


def tools_needed(targets, tcdir):
    missing = [t for t in toolchain.tools_for(targets) if not toolchain.is_installed(tcdir, t)]
    return missing


def choose_target(a):
    tcdir = a.toolchain_dir or toolchain.default_dir()
    print('\n【第 3 步 / 4】要得到什么？')
    rows = [('1', 'win', 'Windows 电脑版（推荐）：得到一个双击 rm8.bat 就能玩的文件夹'),
            ('2', 'android', '安卓手机版：得到一个个人 APK，复制到手机安装（需要同意 Google 的 Android SDK 许可协议）'),
            ('3', 'both', '电脑版和手机版都要'),
            ('4', 'none', '只提取数据（不构建，给熟悉命令行的人用）')]
    for k, t, text in rows:
        need = tools_needed({'win': {'win'}, 'android': {'android'}, 'both': {'win', 'android'}, 'none': set()}[t], tcdir)
        dl = sum(toolchain.TOOLS[x]['size'] for x in need)
        extra = ('；需要下载构建工具约 %s' % human(dl)) if dl else ('；构建工具已在这台电脑上，不用再下载' if t != 'none' else '')
        print('  %s) %s%s' % (k, text, extra))
    print('（构建工具只下载到 %s，不装进系统，不需要管理员权限。第一次构建要等十几分钟到半小时，取决于网速。）' % tcdir)
    while True:
        s = ask('请选择 [1]：') or '1'
        for k, t, _ in rows:
            if s == k:
                return t
        print('  请输入 1、2、3 或 4。')


def choose_saves(game, target):
    print('\n【第 4 步 / 4】迁移原版存档')
    log = Log(None, quiet=True)
    try:
        found = savesmod.find(game, log)
    except Exception:
        found = {'saves': {}, 'gamedate': None}
    n = len(found['saves'])
    if not n and not found['gamedate']:
        print('  没有在游戏文件夹里找到可以迁移的存档和进度（可能还没存过档），跳过。')
        return False
    print('  找到 %d 个存档%s。把它们迁移过去，就能接着原来的进度玩（只读取，不会改动原来的存档）：' % (n, ' 和游戏进度（解锁的角色、拼图）' if found['gamedate'] else ''))
    if target in ('none', 'win', 'both'):
        print('   - 电脑版：复制到 %s（那里已有不同的文件时，会先问你，不会悄悄覆盖）' % savesmod.desktop_data_dir(None))
    if target in ('android', 'both'):
        print('   - 手机版：装进个人 APK，第一次打开时复制到手机（手机上已有的同名存档不会被覆盖）')
    return yesno('  迁移存档吗？', True)


def check_downloads(a):
    """What has to be downloaded, and can it be?  Returns False when the user should be sent back (the caller re-asks the target)."""
    targets = {'win': {'win'}, 'android': {'android'}, 'both': {'win', 'android'}, 'none': set()}[a.target]
    tcdir = a.toolchain_dir or toolchain.default_dir()
    urls, what = [], []
    missing = tools_needed(targets, tcdir)
    if missing:
        urls += [toolchain.TOOLS[t]['url'] for t in missing]
        what.append('构建工具（约 %s）' % human(sum(toolchain.TOOLS[t]['size'] for t in missing)))
    need_ff = a.video and not ffmpeg_tool.have_ffmpeg(a) and a.download_ffmpeg
    if need_ff:
        urls.append(ffmpeg_tool.PINNED['url'])
        what.append('ffmpeg（转换开场 / 制作人员影片用，约 %s）' % human(ffmpeg_tool.PINNED['size']))
    if not urls:
        return True
    print('\n需要联网下载：%s。正在检查网络……' % '、'.join(what))
    bad = netdl.unreachable_hosts(urls, timeout=8)
    if not bad:
        print('  网络正常。')
        return True
    hosts = [h for h, _ in bad]
    print('\n' + netdl.offline_message('、'.join(what), hosts) + '\n')
    if missing:
        print('没有网络，构建不了移植版。你可以：先只提取数据（选 4），等有网络后再运行一次；或者按上面的办法手动放好文件。')
        return False
    if need_ff:
        if yesno('只有影片转换用的 ffmpeg 下载不了。要跳过开场 / 制作人员影片继续吗？（游戏照样能玩，以后联网后重新运行就会补上）', True):
            a.video = False
            return True
        raise Cancelled()
    return True


def confirm(a, game_label):
    print('\n' + LINE + '\n请确认：')
    print('  游戏文件夹：%s（%s）' % (a.game, game_label))
    print('  输出文件夹：%s' % a.out)
    print('  要做的事  ：%s' % TARGET_TEXT[a.target])
    print('  存档      ：%s' % ('迁移原版存档' if a.saves else '不迁移'))
    print('  影片      ：%s' % ('转换开场 / 制作人员影片' if a.video else '跳过（没有这两段影片，游戏照样能玩）'))
    print(LINE)
    return yesno('开始吗？', True)


# ------------------------------------------------------------------------------------------------------ entry
def run(a):
    """Ask the questions and fill `a` (game, out, target, saves, video ...).  Returns False when the user declines at the end."""
    disable_quick_edit()
    print(LINE)
    print('  大富翁8 移植版 · 数据提取与构建工具   v%s（数据格式 %d）' % (EXTRACTOR_VERSION, DATA_FORMAT))
    print(LINE)
    print('本工具只读取你自己合法购买并安装的《大富翁8》，把移植版需要的数据整理到你选择的文件夹，供你本人在自己的设备上使用。')
    print('整理出来的内容仍属原权利人所有：请不要上传、分享或散布输出文件夹，以及含有这些数据的安装包。移植版与原权利人无关。')
    print('随时可以按 Ctrl+C 退出；已经完成的步骤下次会自动跳过。')
    a.game = choose_game()
    label = detect.edition_guess(a.game)
    print('  已选择：%s（%s）' % (a.game, label))
    a.out = choose_out(a.game)
    print('  已选择：%s' % a.out)
    a.ask_download = False
    while True:
        a.target = choose_target(a)
        if check_downloads(a):
            break
    if a.saves is None:
        a.saves = choose_saves(a.game, a.target)
    a.saves = bool(a.saves)
    return confirm(a, label)


def confirm_unknown(err):
    """The folder is a RichMan 8 install but not a supported edition: show why and ask whether to try anyway."""
    print('\n' + str(err).split('\n如果你确定')[0])
    return yesno('\n仍然要试试吗？（可能得到不正确的数据，或移植版跑不起来）', False)


# ------------------------------------------------------------------------------------------------------ the end
def summary(out, a, manifest=None):
    """Plain-Chinese "what you got and where" block; returns the text."""
    out = os.path.abspath(out)
    lines = [LINE, '  全部完成！下面是你得到的东西', LINE, '输出文件夹：%s' % out, '']
    bat, exe = os.path.join(out, 'rm8.bat'), os.path.join(out, 'rm8_app.exe')
    apk = getattr(a, 'apk_out', None) or os.path.join(out, 'rm8_personal_DO_NOT_SHARE.apk')
    zips = sorted(f for f in os.listdir(out) if f.startswith('rm8_windows_') and f.endswith('.zip')) if os.path.isdir(out) else []
    if os.path.exists(bat) and os.path.exists(exe):
        lines += ['【电脑版】双击这个文件开始玩：', '    %s' % bat,
                  '    （第一次打开时 Windows 可能弹出蓝色的“Windows 已保护你的电脑”提示：点“更多信息”→“仍要运行”。因为这个程序没有付费买数字签名，不是病毒。）',
                  '    存档和设置保存在 %s，不在输出文件夹里。' % savesmod.desktop_data_dir(getattr(a, 'saves_dir', None))]
        for z in zips:
            lines.append('    压缩包：%s（含你的游戏数据，只限本人使用）' % os.path.join(out, z))
        lines.append('')
    if os.path.exists(apk):
        lines += ['【手机版】个人 APK：', '    %s（%s）' % (apk, human(os.path.getsize(apk))),
                  '    把它复制到安卓手机，点开安装（需要允许“安装未知来源的应用”）；第一次打开会先解压数据，手机请留出 3 GB 以上空间。',
                  '    这个 APK 里装着你自己游戏的数据，只限你本人使用，不要分享、不要上传。', '']
    if os.path.isdir(os.path.join(out, 'game')) and os.path.isdir(os.path.join(out, 'scenes')):
        lines += ['【提取出的数据】%s 里的 game 和 scenes 文件夹（移植版读取它们，不要删除或改名）。' % out, '']
    if getattr(a, 'saves', False) and os.path.isdir(os.path.join(out, 'saves')):
        lines += ['【存档】已迁移原版存档和进度；备份在 %s。存档是个人数据，同样不要分享。' % os.path.join(out, 'saves'), '']
    lines += ['【记录】过程记录在 %s（遇到问题时把它发给开发者）。' % os.path.join(out, 'extract.log'),
              '再运行一次同样的操作 = 更新：没变化的步骤会自动跳过。']
    text = '\n'.join(lines)
    return text


def write_summary_file(out, text):
    try:
        with open(os.path.join(out, '结果说明.txt'), 'w', encoding='utf-8-sig', newline='\r\n') as f:
            f.write(text + '\n')
    except OSError:
        pass
