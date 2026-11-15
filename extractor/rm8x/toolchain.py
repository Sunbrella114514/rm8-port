"""Build tools the port needs, installed on demand into a private cache folder (default %LOCALAPPDATA%\\RM8\\toolchain; never system-wide, no admin).

Pinned versions = the ones in port/README.md.  Every archive is checked against its pinned size and SHA-256 (Android packages also against the SHA-1 that
Google publishes in repository2-3.xml) before it is unpacked; unpacking goes to "<dest>.part" and is renamed when complete, so an interrupted run
leaves nothing half-installed.  A file placed by hand into <cache>\\downloads\\ under the expected name is used when its hash matches (offline use).

    llvm-mingw 20261104 (ucrt, x86_64)   Windows compiler (clang) + libc++ etc.        Apache-2.0 with LLVM exception / MIT / ...
    CMake 4.5.0, Ninja 1.13.2            build system                                    BSD-3 / Apache-2.0
    Temurin JDK 21.0.13+9                javac / d8 / keytool for the APK                GPL-2.0 with Classpath exception
    Android: NDK r29 (29.0.14206865), build-tools 36.1.0, platform android-35, from dl.google.com -- the same packages sdkmanager installs, fetched
             directly (fewer downloads, checksums pinned) after the user has seen and accepted the Android SDK license text.
"""
import hashlib
import json
import os
import shutil
import sys
import time
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
from concurrent.futures import ThreadPoolExecutor

from . import netdl
from .util import human

GOOGLE = 'https://dl.google.com/android/repository/'
REPO_XML = GOOGLE + 'repository2-3.xml'
LICENSE_TEXT_SHA256 = 'de5fa465e908e7f098e718082f013135b9a36ee706d032cf510f4f1153424b2a'     # android-sdk-license text (stripped) of repository2-3.xml
MARK = '.rm8_tool.json'


class ToolError(Exception):
    pass


class UserToolError(ToolError):
    """Something the user can fix (no network, no space, licence not accepted): no "please report this" hint."""
    no_hint = True


TOOLS = {
    'llvm-mingw': dict(label='llvm-mingw 20261104（Windows 版 clang 编译器）', targets=('win',), dest='llvm-mingw', strip=1, check='bin/x86_64-w64-mingw32-clang.exe',
                       url='https://github.com/mstorsjo/llvm-mingw/releases/download/20261104/llvm-mingw-20261104-ucrt-x86_64.zip', size=191037046,
                       sha256='552191b5754c49a5a20723276a6fc56541ea1922c4c50b7b669022333fc1b40f'),
    'cmake': dict(label='CMake 4.5.0', targets=('win', 'android'), dest='cmake', strip=1, check='bin/cmake.exe',
                  url='https://github.com/Kitware/CMake/releases/download/v4.5.0/cmake-4.5.0-windows-x86_64.zip', size=56341654,
                  sha256='301b1728317ccd84e984b02e366a8293735bc5ac618757d3d8f58b578f062411'),
    'ninja': dict(label='Ninja 1.13.2', targets=('win', 'android'), dest='ninja', strip=0, check='ninja.exe',
                  url='https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip', size=291570,
                  sha256='07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'),
    'jdk': dict(label='Temurin JDK 21.0.13+9', targets=('android',), dest='jdk', strip=1, check='bin/javac.exe',
                url='https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.13%2B9/OpenJDK21U-jdk_x64_windows_hotspot_21.0.13_9.zip',
                size=205421281, sha256='a4dbaa05fca4bd89d84584e58c459bd732efebb1722af80ab13c6516832004f9'),
    'android-ndk': dict(label='Android NDK r29 (29.0.14206865)', targets=('android',), dest='android-sdk/ndk/r29', strip=1, check='build/cmake/android.toolchain.cmake',
                        url=GOOGLE + 'android-ndk-r29-windows.zip', size=833850862, sha1='ab3bb30fbb9e6903666d60c55d11e78b04e07472',
                        sha256='4f83a1a87ea0d33ae2b43812ce27b768be949bc78acf90b955134d19e3068f1c', sdk=True),
    'android-build-tools': dict(label='Android build-tools 36.1.0', targets=('android',), dest='android-sdk/build-tools/36.1.0', strip=1, check='zipalign.exe',
                                url=GOOGLE + 'build-tools_r36.1_windows.zip', size=59366211, sha1='95f048ae758dad90617975fe3f787f038b03338c',
                                sha256='23189d2d52b40a070a05e9cf7e497c9563f67fee76902e8fd3135ef29ef4dbeb', sdk=True),
    'android-platform': dict(label='Android platform android-35', targets=('android',), dest='android-sdk/platforms/android-35', strip=1, check='android.jar',
                             url=GOOGLE + 'platform-35_r02.zip', size=64273788, sha1='0bb560a90a7a2cbd0dd8348224d518b638fe7949',
                             sha256='0988cacad01b38a18a47bac14a0695f246bc76c1b06c0eeb8eb0dc825ab0c8e0', sdk=True),
}
ORDER = ['cmake', 'ninja', 'llvm-mingw', 'jdk', 'android-build-tools', 'android-platform', 'android-ndk']


def default_dir():
    base = os.environ.get('LOCALAPPDATA') or os.path.join(os.path.expanduser('~'), '.cache')
    return os.path.join(base, 'RM8', 'toolchain')


def lp(path):
    """Long-path safe form of a Windows path."""
    path = os.path.abspath(path)
    if os.name == 'nt' and not path.startswith('\\\\?\\') and len(path) > 200:
        return '\\\\?\\' + path
    return path


def dest_path(root, tool):
    return os.path.join(root, *tool['dest'].split('/'))


def is_installed(root, tid):
    t = TOOLS[tid]
    d = dest_path(root, t)
    try:
        m = json.load(open(os.path.join(d, MARK), encoding='utf-8'))
    except (OSError, ValueError):
        return False
    return m.get('sha256') == t['sha256'] and os.path.exists(os.path.join(d, *t['check'].split('/')))


# ------------------------------------------------------------------------------------------------------ download
def _hash_file(path, algo):
    h = hashlib.new(algo)
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 22), b''):
            h.update(b)
    return h.hexdigest()


def _verify(path, t):
    if os.path.getsize(path) != t['size']:
        return False
    if _hash_file(path, 'sha256') != t['sha256']:
        return False
    if t.get('sha1') and _hash_file(path, 'sha1') != t['sha1']:
        return False
    return True


def download(tid, dl_dir, log):
    t = TOOLS[tid]
    os.makedirs(dl_dir, exist_ok=True)
    name = t['url'].rsplit('/', 1)[1].replace('%2B', '+')
    path = os.path.join(dl_dir, name)
    if os.path.exists(path) and _verify(path, t):
        log.info('  %s：使用已下载的文件 %s' % (t['label'], path))
        return path
    part = path + '.part'
    log.info('  下载 %s（%s，%s）' % (t['label'], human(t['size']), t['url']))
    try:
        netdl.download_file(t['url'], part, t['size'], log, t['label'])
    except netdl.DownloadError as e:
        from . import friendly
        raise UserToolError('下载 %s 失败。\n%s\n  下载地址：%s\n  手动下载的话，文件放到 %s，文件名必须是 %s。'
                        % (t['label'], friendly.network_failure('构建工具', [t['url']], str(e)), t['url'], dl_dir, name))
    except OSError as e:
        from . import friendly
        raise ToolError('下载 %s 时写文件出错：%s' % (t['label'], friendly.os_error_message(e)))
    if os.path.getsize(part) != t['size'] or _hash_file(part, 'sha256') != t['sha256'] or (t.get('sha1') and _hash_file(part, 'sha1') != t['sha1']):
        os.remove(part)
        raise ToolError('%s 下载下来的文件大小 / 校验值与程序里记录的不一致，已丢弃（可能是下载中途断了，或者网络环境改动了下载内容）。请重新运行；如果反复出现，换一个网络再试。' % t['label'])
    os.replace(part, path)
    return path


# ------------------------------------------------------------------------------------------------------ install
def install(tid, root, dl_dir, log, path=None):
    t = TOOLS[tid]
    dest = dest_path(root, t)
    if is_installed(root, tid):
        return dest
    path = path or download(tid, dl_dir, log)
    z = zipfile.ZipFile(path)
    infos = [i for i in z.infolist() if not i.filename.endswith('/')]
    need = sum(i.file_size for i in infos)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    free = shutil.disk_usage(os.path.dirname(dest)).free
    if free < need + (300 << 20):
        raise UserToolError('磁盘空间不够：解压 %s 约需 %s，%s 所在的磁盘只剩 %s。请清理空间，或用 --toolchain-dir 换一个空间大的磁盘。' % (t['label'], human(need), root, human(free)))
    log.info('  安装 %s（解压 %d 个文件，%s）→ %s' % (t['label'], len(infos), human(need), dest))
    part = dest + '.part'
    shutil.rmtree(lp(part), ignore_errors=True)
    strip = t['strip']
    for i, info in enumerate(infos):
        parts = [p for p in info.filename.replace('\\', '/').split('/') if p not in ('', '.')]
        if len(parts) <= strip:
            continue
        parts = parts[strip:]
        if any(p == '..' or ':' in p for p in parts):
            raise ToolError('压缩包里有不安全的路径：%s' % info.filename)
        out = os.path.join(part, *parts)
        os.makedirs(lp(os.path.dirname(out)), exist_ok=True)
        with z.open(info) as s, open(lp(out), 'wb') as f:
            shutil.copyfileobj(s, f, 1 << 20)
    z.close()
    if not os.path.exists(os.path.join(part, *t['check'].split('/'))):
        shutil.rmtree(lp(part), ignore_errors=True)
        raise ToolError('%s 解压后找不到 %s：压缩包结构和预期不同。' % (t['label'], t['check']))
    json.dump({'id': tid, 'sha256': t['sha256'], 'url': t['url'], 'installed': time.strftime('%Y-%m-%d %H:%M:%S')}, open(os.path.join(part, MARK), 'w', encoding='utf-8'))
    shutil.rmtree(lp(dest), ignore_errors=True)
    os.rename(lp(part), lp(dest))
    return dest


# ------------------------------------------------------------------------------------------------------ Android licenses
def android_license_text(dl_dir):
    os.makedirs(dl_dir, exist_ok=True)
    p = os.path.join(dl_dir, 'repository2-3.xml')
    try:
        with urllib.request.urlopen(urllib.request.Request(REPO_XML, headers={'User-Agent': 'rm8-extract'}), timeout=60) as r:
            data = r.read()
        open(p, 'wb').write(data)
    except Exception as e:
        if not os.path.exists(p):
            from . import friendly
            raise UserToolError('取不到 Android SDK 的许可协议文本（%s）。\n%s' %(REPO_XML, friendly.network_failure('Android SDK 许可协议', [REPO_XML], str(e))))
    for l in ET.parse(p).getroot().iter('license'):
        if l.get('id') == 'android-sdk-license':
            return l.text.strip()
    raise ToolError('Google 的许可协议文件里找不到 android-sdk-license（文件格式可能变了），请到 Issues 页面反馈')


def ensure_android_licenses(root, dl_dir, accept, interactive, log):
    rec = os.path.join(root, 'android-sdk', 'licenses', 'rm8_accepted_android-sdk-license.json')
    try:
        if json.load(open(rec, encoding='utf-8')).get('sha256') == LICENSE_TEXT_SHA256:
            return
    except (OSError, ValueError):
        pass
    text = android_license_text(dl_dir)
    h = hashlib.sha256(text.encode()).hexdigest()
    log.info('')
    log.info('构建安卓版需要 Android SDK 的组件（NDK、build-tools、platform，来自 Google 的 dl.google.com）。使用它们需要先接受 Android SDK 许可协议：')
    log.info('')
    log.info(text)
    log.info('')
    if h != LICENSE_TEXT_SHA256:
        log.warn('许可协议的文字与本程序固定的版本不同（Google 更新过），请重新阅读。')
    if accept:
        log.info('已按 --accept-android-licenses 接受上面的 Android SDK 许可协议。')
    elif interactive:
        try:
            ans = input('你是否阅读并接受上面的 Android SDK 许可协议？输入 y 接受，其它任意键放弃：').strip().lower()
        except EOFError:
            ans = ''
        if ans not in ('y', 'yes'):
            raise UserToolError('没有接受 Android SDK 许可协议，无法构建安卓版。（可以改用已有的基础 APK：--base-apk，或只构建 Windows 版：--target win；非交互运行请加 --accept-android-licenses）')
    else:
        raise UserToolError('构建安卓版需要接受 Android SDK 许可协议（全文见上方，或 https://developer.android.com/studio/terms ）。'
                        '确认接受后，加上 --accept-android-licenses 重新运行。')
    os.makedirs(os.path.dirname(rec), exist_ok=True)
    json.dump({'license': 'android-sdk-license', 'sha256': h, 'accepted': time.strftime('%Y-%m-%d %H:%M:%S'), 'by': 'rm8_extract (explicit user consent)'},
              open(rec, 'w', encoding='utf-8'))


# ------------------------------------------------------------------------------------------------------ high level
def tools_for(targets):
    return [tid for tid in ORDER if set(TOOLS[tid]['targets']) & set(targets)]


def ensure(targets, root, opts, log, interactive=False):
    """Install whatever is missing for the targets ('win', 'android'); returns {tool id: install dir}."""
    root = os.path.abspath(root)
    dl = os.path.join(root, 'downloads')
    need = tools_for(targets)
    missing = [t for t in need if not is_installed(root, t)]
    if missing and getattr(opts, 'no_install_tools', False):
        raise UserToolError('缺少构建工具：%s（你指定了 --no-install-tools，所以不自动安装）。工具缓存文件夹：%s' % ('、'.join(TOOLS[t]['label'] for t in missing), root))
    if missing:
        total = sum(TOOLS[t]['size'] for t in missing)
        log.info('需要安装 %d 个构建工具（下载约 %s），装到私有文件夹 %s（不需要管理员权限，不改系统设置）：' % (len(missing), human(total), root))
        for t in missing:
            log.info('    %s' % TOOLS[t]['label'])
        os.makedirs(root, exist_ok=True)
        bad = netdl.unreachable_hosts([TOOLS[t]['url'] for t in missing], timeout=8)
        if bad:
            raise UserToolError(netdl.offline_message('构建工具（%s）' % '、'.join(TOOLS[t]['label'].split('（')[0] for t in missing), [h for h, _ in bad]))
        free = shutil.disk_usage(root).free
        est = sum(TOOLS[t]['size'] for t in missing) * 3
        if free < est:
            raise UserToolError('磁盘空间不够：下载并安装构建工具大约需要 %s，%s 所在的磁盘只剩 %s。请清理空间，或用 --toolchain-dir 换一个空间大的磁盘。' % (human(est), root, human(free)))
        if any(TOOLS[t].get('sdk') for t in missing):
            ensure_android_licenses(root, dl, getattr(opts, 'accept_android_licenses', False), interactive, log)
    out = {}
    paths = {}
    if missing:                                           # download everything that is missing at the same time (a single connection is often slow)
        with ThreadPoolExecutor(max_workers=min(4, len(missing))) as ex:
            futs = {t: ex.submit(download, t, dl, log) for t in missing}
            for t, f in futs.items():
                paths[t] = f.result()
    for tid in need:
        out[tid] = install(tid, root, dl, log, paths.get(tid)) if tid in missing else dest_path(root, TOOLS[tid])
    return out


def build_env(dirs, targets):
    """PATH / JAVA_HOME for the build commands."""
    env = dict(os.environ)
    paths = []
    if 'win' in targets:
        paths.append(os.path.join(dirs['llvm-mingw'], 'bin'))
    paths += [os.path.join(dirs['cmake'], 'bin'), dirs['ninja']]
    if 'android' in targets:
        paths.append(os.path.join(dirs['jdk'], 'bin'))
        env['JAVA_HOME'] = dirs['jdk']
    env['PATH'] = os.pathsep.join(paths) + os.pathsep + env.get('PATH', '')
    return env
