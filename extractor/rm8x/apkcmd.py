"""`rm8_extract --apk` / `--make-apk <output folder>`: turn an extraction into a personal APK for the phone (see apk.py)."""
import datetime
import json
import os
import shutil
import sys

from . import apk, saves as savesmod
from .util import app_dir, human
from .version import DATA_FORMAT, MANIFEST_NAME

EDITION_NAMES = {'sc': 'steam', 'tc': 'classic'}
BASE_NAME = 'rm8_base.apk'

INSTALL_HELP = """
================ 手机安装方法 ================
 1. 把这个 APK 复制到手机（USB 文件传输、数据线、网盘都行）。
 2. 在手机上点开它安装。系统提示“不允许安装未知来源的应用”时，按提示到设置里，允许你正在用的文件管理器 / 浏览器安装应用。
 3. 第一次打开“大富翁8”时，会先把 APK 里的游戏数据解压到应用自己的目录（约 1.1 GB，要等一会儿；手机至少留出 3 GB 空闲）。
    解压完以后这个 APK 文件就可以删掉了（应用自己保留数据）。
 4. 以后更新：重新运行提取器生成新的 APK，直接覆盖安装（用的是同一把签名密钥）。

重要：
 * 这个 APK 里装的是**你自己游戏的数据**%(saves_note)s，只供你本人在自己的手机上使用；不要上传、分享、转发给别人。
 * 它是用你本机生成的密钥签名的：密钥在 %(key_dir)s（请备份这个文件夹；密钥丢了或换了电脑，新生成的 APK 就不能覆盖安装旧的，需要先卸载旧的，应用里的数据会随之删除）。
 * 与我们发布的基础版 APK、开发版 APK（签名不同）不能互相覆盖安装：要换的话先卸载另一个。
"""


def find_base_apk(explicit):
    cands = []
    if explicit:
        cands.append(explicit)
    else:
        exe_dir = os.path.dirname(os.path.abspath(sys.executable)) if getattr(sys, 'frozen', False) else None
        for d in (exe_dir, app_dir(), os.path.join(app_dir(), 'android'), os.path.dirname(os.path.abspath(sys.argv[0])) if sys.argv and sys.argv[0] else None):
            if d:
                cands.append(os.path.join(d, BASE_NAME))
        # development tree: the data-free viewer APK built by port/android/build_apk.py
        cands.append(os.path.join(os.path.dirname(app_dir()), 'port', 'rm8_viewer.apk'))
    for c in cands:
        if os.path.isfile(c):
            return c
    return None


def make_personal_apk(out_dir, opts, log):
    """Returns 0 on success. Raises apk.ApkError with a Chinese message otherwise."""
    out_dir = os.path.abspath(out_dir)
    mp = os.path.join(out_dir, MANIFEST_NAME)
    if not os.path.isfile(mp):
        raise apk.UserApkError('%s 里没有 %s：请先用 rm8_extract 提取数据（不加 --no-copy-game）。' % (out_dir, MANIFEST_NAME))
    manifest = json.load(open(mp, encoding='utf-8'))
    if manifest.get('data_format') != DATA_FORMAT:
        raise apk.UserApkError('输出文件夹的数据格式是 %s，当前提取器需要 %d：请先重新提取。' % (manifest.get('data_format'), DATA_FORMAT))
    if manifest.get('options', {}).get('copy_game') == 'none':
        raise apk.UserApkError('这次提取用了 --no-copy-game，输出里没有 game\\ 文件夹，不能做手机版。请重新提取（不加 --no-copy-game）。')
    base = find_base_apk(getattr(opts, 'base_apk', None))
    if not base:
        raise apk.UserApkError('找不到基础 APK（%s）。请从移植版的发布页下载它，放在提取器旁边，或用 --base-apk 指定路径。' % BASE_NAME)
    key_dir = getattr(opts, 'key_dir', None) or apk.default_key_dir()
    out_apk = getattr(opts, 'apk_out', None) or os.path.join(out_dir, 'rm8_personal_DO_NOT_SHARE.apk')
    need = sum(os.path.getsize(os.path.join(dp, f)) for sub in ('game', 'scenes') for dp, _, fs in os.walk(os.path.join(out_dir, sub)) for f in fs)
    free = shutil.disk_usage(os.path.dirname(out_apk) or '.').free
    if free < need + (300 << 20):
        raise apk.UserApkError('磁盘空间不够：生成 APK 约需 %s，现在只剩 %s。请清理空间，或用 --apk-out 指定另一个磁盘' % (human(need), human(free)))
    log.step('生成手机用的个人 APK')
    log.info('  基础 APK：%s' % base)
    key, cert, created = apk.load_or_create_key(key_dir, log)
    log.info('  签名密钥：%s（证书指纹 SHA-256 %s…）' % (key_dir, apk.rsa.fingerprint(cert)[:23]))
    if created:
        log.warn('这是新生成的密钥，请备份文件夹 %s；以后生成的 APK 都用它签名，才能覆盖安装。' % key_dir)
    state = {'last': -1}

    def progress(done, total, name):
        if done // (100 << 20) != state['last']:
            state['last'] = done // (100 << 20)
            log.info('  写入 %s / %s  %s' % (human(done), human(total), name))
    staged = getattr(opts, 'saves_staged', None)
    sv = savesmod.staged_files(staged) if staged else []
    if sv:
        log.info('  同时装入你的原版存档：%d 个存档文件%s（首次启动时复制到应用的数据目录，已有同名文件不覆盖）' % (sum(1 for _, r in sv if r.startswith('save/')), ' + 进度 gamedate' if any(r == 'rm8_gamedate.bin' for _, r in sv) else ''))
    info = apk.build_personal_apk(base, out_dir, out_apk, key, cert, manifest, log, progress, saves=sv)
    log.step('完成：%s（%s，数据 %d 个文件）' % (out_apk, human(info['bytes']), info['files']))
    log.info(INSTALL_HELP % {'key_dir': key_dir, 'saves_note': '和你的存档 / 进度（个人数据）' if sv else ''})
    return 0
