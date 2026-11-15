"""Build the port from source for the user: fetch third-party sources, configure + build, package (Windows folder and / or personal Android APK).

Steps are resumable: every step records a signature (source + tool pins + inputs) in <work>/state.json and is skipped when nothing changed and its
output still exists.  Tools come from rm8x.toolchain (private cache folder); the port's Python scripts (fetch_deps.py, build_apk.py) run in-process.
"""
import contextlib
import datetime
import glob
import hashlib
import io
import json
import os
import re
import runpy
import shutil
import subprocess
import sys
import zipfile

from . import apk as apkmod
from . import apkcmd, toolchain
from .toolchain import ToolError
from .util import app_dir, human

SKIP_DIRS = {'.git', '__pycache__', '.cache', 'scenes', 'results'}
SKIP_EXT = ('.apk', '.idsig', '.log', '.pyc', '.sav')
SKIP_NAMES = {'rm8_box.bin', 'rm8_gamedate.bin', 'rm8_settings.txt', 'debug.keystore'}
EXTERN_KEEP = {'fetch_deps.py', 'README.md'}


class BuildError(Exception):
    pass


class UserBuildError(BuildError):
    no_hint = True


# ------------------------------------------------------------------------------------------------------ source
def _port_dir(root):
    for cand in (root, os.path.join(root, 'port')):
        if os.path.isfile(os.path.join(cand, 'CMakeLists.txt')) and os.path.isdir(os.path.join(cand, 'app')):
            return cand
    return None


def find_source(opts, work, log):
    """-> directory of the port source (the one with CMakeLists.txt + app/)."""
    cands = []
    if getattr(opts, 'source', None):
        s = os.path.abspath(opts.source)
        if os.path.isfile(s) and s.lower().endswith('.zip'):
            dst = os.path.join(work, 'source_zip')
            marker = os.path.join(dst, '.from')
            sig = '%s %d' % (s, os.path.getsize(s))
            if not (os.path.exists(marker) and open(marker).read() == sig):
                shutil.rmtree(dst, ignore_errors=True)
                os.makedirs(dst)
                log.info('  解压源码包 %s' % s)
                with zipfile.ZipFile(s) as z:
                    for i in z.infolist():
                        n = i.filename.replace('\\', '/')
                        if '..' in n.split('/') or n.startswith('/') or ':' in n.split('/')[0]:
                            raise BuildError('源码包里有不安全的路径：%s' % i.filename)
                    z.extractall(dst)
                open(marker, 'w').write(sig)
            cands.append(dst)
        else:
            cands.append(s)
        p = next((c for c in map(_port_dir, cands) if c), None)
        if not p:
            raise UserBuildError('--source 指定的位置里找不到移植版源码（需要有 port\\CMakeLists.txt 和 port\\app）：%s' % opts.source)
        return p
    exe_dir = os.path.dirname(os.path.abspath(sys.executable)) if getattr(sys, 'frozen', False) else None
    for base in (exe_dir, os.path.join(exe_dir, 'source') if exe_dir else None, os.path.dirname(app_dir())):
        if base:
            p = _port_dir(base)
            if p:
                return p
    for base in (exe_dir, os.getcwd()):                              # the release page's rm8-port-src-<tag>.zip, saved next to the exe
        if base:
            zips = sorted(glob.glob(os.path.join(base, 'rm8-port-src*.zip')), key=os.path.getmtime, reverse=True)
            if zips:
                class Z:
                    source = zips[0]
                log.info('  找到旁边的源码包 %s' % zips[0])
                return find_source(Z, work, log)
    rel = os.path.join(app_dir(), 'data', 'source_release.json')
    info = json.load(open(rel, encoding='utf-8')) if os.path.exists(rel) else {}
    if info.get('url') and info.get('sha256'):
        return _download_source(info, work, log)
    raise UserBuildError('找不到移植版的源码，没法构建。请到发布页下载“源码包”（文件名是 rm8-port-src-….zip），'
                     '和 rm8_extract.exe 放在同一个文件夹里再运行；或者用 --source 指定它（也可以指定源码文件夹）。\n'
                     '只想要提取出来的数据的话，选“只提取数据”（不加 --target）就不需要它。')


def _download_source(info, work, log):
    import urllib.request
    dl = os.path.join(work, 'source_release.zip')
    if not (os.path.exists(dl) and hashlib.sha256(open(dl, 'rb').read()).hexdigest() == info['sha256']):
        log.info('  下载移植版源码包 %s' % info['url'])
        os.makedirs(work, exist_ok=True)
        try:
            data = urllib.request.urlopen(urllib.request.Request(info['url'], headers={'User-Agent': 'rm8-extract'}), timeout=120).read()
        except Exception as e:
            from . import friendly
            raise UserBuildError('下载源码包失败。\n%s\n  地址：%s' % (friendly.network_failure('移植版的源码包', [info['url']], str(e)), info['url']))
        if hashlib.sha256(data).hexdigest() != info['sha256']:
            raise BuildError('源码包的校验值与程序里记录的不一致（下载可能不完整，或内容被替换），已丢弃，请重新运行。')
        open(dl, 'wb').write(data)

    class O:
        source = dl
    return find_source(O, work, log)


def _wanted(root, rel_parts, fname):
    if fname in SKIP_NAMES or fname.lower().endswith(SKIP_EXT):
        return False
    if rel_parts and rel_parts[0] == 'extern':                       # third-party sources come from fetch_deps.py
        return len(rel_parts) == 1 and fname in EXTERN_KEEP
    return True


def sync_source(port_src, dst, log):
    """Copy the port source (without build products / third-party trees / user data) into dst; unchanged files are left alone. Returns a signature."""
    h = hashlib.sha256()
    want = {}
    for dp, dns, fs in os.walk(port_src):
        rel = os.path.relpath(dp, port_src)
        parts = [] if rel == '.' else rel.replace('\\', '/').split('/')
        dns[:] = [d for d in dns if d not in SKIP_DIRS and not d.startswith('build-')]
        if parts and parts[0] == 'extern':
            dns[:] = []
        for f in sorted(fs):
            if _wanted(port_src, parts, f):
                want['/'.join(parts + [f])] = os.path.join(dp, f)
    for r, p in sorted(want.items()):
        d = hashlib.sha256(open(p, 'rb').read()).hexdigest()
        h.update(('%s %s\n' % (r, d)).encode())
        q = os.path.join(dst, *r.split('/'))
        if os.path.exists(q) and os.path.getsize(q) == os.path.getsize(p) and hashlib.sha256(open(q, 'rb').read()).hexdigest() == d:
            continue
        os.makedirs(os.path.dirname(q), exist_ok=True)
        shutil.copyfile(p, q)
    # remove files that are no longer in the source (keep fetched third-party trees and dot-caches)
    for dp, dns, fs in os.walk(dst):
        rel = os.path.relpath(dp, dst)
        parts = [] if rel == '.' else rel.replace('\\', '/').split('/')
        if parts and parts[0] == 'extern':
            dns[:] = []
            continue
        for f in fs:
            r = '/'.join(parts + [f])
            if r not in want and f != 'debug.keystore' and not r.startswith('extern/'):
                os.remove(os.path.join(dp, f))
    log.detail('source copy: %d files, signature %s' % (len(want), h.hexdigest()[:12]))
    return h.hexdigest()[:16]


# ------------------------------------------------------------------------------------------------------ running things
def run(cmd, env, cwd, log, label, progress=True):
    """Run a command, stream its output into the log; raise BuildError (with the tail of the output) on failure."""
    exe = shutil.which(cmd[0], path=env.get('PATH')) if not os.path.isabs(cmd[0]) else cmd[0]      # Popen would search the parent's PATH, not env's
    if not exe:
        raise BuildError('%s：找不到程序 %s（构建工具没装好？）' % (label, cmd[0]))
    cmd = [exe] + list(cmd[1:])
    log.detail('$ ' + ' '.join(cmd))
    p = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
    tail, last = [], -1
    for line in p.stdout:
        line = line.rstrip()
        log.detail(line)
        tail.append(line)
        del tail[:-60]
        m = re.match(r'\[(\d+)/(\d+)\]', line)
        if progress and m:
            pct = int(m.group(1)) * 100 // int(m.group(2))
            if pct // 10 != last // 10:
                last = pct
                log.info('    %s … %d%%（%s/%s）' % (label, pct, m.group(1), m.group(2)))
    rc = p.wait()
    if rc:
        if any('No space left' in l for l in tail):
            raise UserBuildError('%s 失败：磁盘空间不够。请清理空间，或用 --work-dir 换一个空间大的磁盘后重新运行（已完成的部分会接着用）。' % label)
        raise BuildError('%s 失败了（退出码 %d）。最后的输出：\n%s\n完整日志在输出文件夹里的 extract.log；把它发给开发者（Issues 页面）可以帮你查原因。' % (label, rc, '\n'.join(tail[-25:])))


def run_py(script, args, env, log, label):
    """Run one of the port's Python scripts in-process (a frozen rm8_extract.exe has no separate interpreter)."""
    log.detail('py ' + script + ' ' + ' '.join(args))
    buf = io.StringIO()
    old_argv, old_env, old_cwd = sys.argv, dict(os.environ), os.getcwd()
    sys.argv = [script] + list(args)
    os.environ.update(env)
    rc = 0
    try:
        with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
            runpy.run_path(script, run_name='__main__')
    except SystemExit as e:
        rc = e.code if isinstance(e.code, int) else (0 if e.code is None else 1)
        if isinstance(e.code, str):
            buf.write(e.code)
    except BaseException as e:
        rc = 1
        import traceback
        buf.write(traceback.format_exc())
    finally:
        sys.argv = old_argv
        os.environ.clear(); os.environ.update(old_env)
        os.chdir(old_cwd)
    log.detail(buf.getvalue())
    if rc:
        from . import friendly, netdl
        text = buf.getvalue()
        if netdl.looks_like_network_error(text):
            raise UserBuildError('%s 失败。\n%s\n  （技术细节见输出文件夹里的 extract.log）' % (label, friendly.network_failure('第三方源码', ['https://github.com/', 'https://www.python.org/'], text)))
        if 'No space left' in text or 'WinError 112' in text:
            raise UserBuildError('%s 失败：磁盘空间不够。请清理空间，或用 --work-dir / --toolchain-dir 换一个空间大的磁盘后重新运行（已完成的部分会接着用）。' % label)
        raise BuildError('%s 失败：\n%s' % (label, text[-2500:]))
    return buf.getvalue()


class State:
    def __init__(self, path):
        self.path = path
        try:
            self.d = json.load(open(path, encoding='utf-8'))
        except (OSError, ValueError):
            self.d = {}

    def done(self, step, sig, output=None):
        return self.d.get(step) == sig and (output is None or os.path.exists(output))

    def mark(self, step, sig):
        self.d[step] = sig
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        json.dump(self.d, open(self.path, 'w', encoding='utf-8'), indent=1)


# ------------------------------------------------------------------------------------------------------ the build
def tool_sig(targets):
    return ','.join('%s:%s' % (t, toolchain.TOOLS[t]['sha256'][:12]) for t in toolchain.tools_for(targets))


def build(out_dir, manifest, targets, opts, log, interactive=False):
    """targets: set of 'win' / 'android'.  Returns 0 or raises BuildError / ToolError / ApkError."""
    out_dir = os.path.abspath(out_dir)
    work = os.path.abspath(getattr(opts, 'work_dir', None) or os.path.join(os.path.dirname(toolchain.default_dir()), 'build'))
    tcdir = getattr(opts, 'toolchain_dir', None) or toolchain.default_dir()
    os.makedirs(work, exist_ok=True)
    log.step('构建移植版（%s）' % ' + '.join({'win': 'Windows', 'android': '安卓'}[t] for t in sorted(targets, reverse=True)))
    free = shutil.disk_usage(work).free
    if free < (4 << 30):
        raise UserBuildError('磁盘空间不够：构建大约需要 4 GB 的工作空间，%s 所在的磁盘只剩 %s。请清理空间，或用 --work-dir 换一个空间大的磁盘。' % (work, human(free)))
    log.info('  工作文件夹：%s   工具缓存：%s' % (work, tcdir))
    # 1. source
    port_src = find_source(opts, work, log)
    log.info('  移植版源码：%s' % port_src)
    src = os.path.join(work, 'src', 'port')
    os.makedirs(src, exist_ok=True)
    sig = sync_source(port_src, src, log)
    # 2. tools
    base_given = bool(getattr(opts, 'base_apk', None))
    need_targets = set(targets)
    if 'android' in targets and base_given:
        need_targets.discard('android')                     # an existing base APK: nothing to compile for Android
    dirs = toolchain.ensure(need_targets, tcdir, opts, log, interactive) if need_targets else {}
    env = toolchain.build_env(dirs, need_targets) if need_targets else dict(os.environ)
    st = State(os.path.join(work, 'state.json'))
    py = sys.executable
    # 3. third-party sources (needed to compile)
    if need_targets:
        fd = os.path.join(src, 'extern', 'fetch_deps.py')
        depsig = sig
        if not st.done('deps', depsig, os.path.join(src, 'extern', 'SDL2-2.32.10', 'CMakeLists.txt')):
            log.step('下载第三方源码（固定版本，校验 SHA-256）')
            cache = os.path.join(tcdir, 'downloads', 'deps')
            os.makedirs(cache, exist_ok=True)
            log.detail(run_py(fd, [], {'RM8_DEPS_CACHE': cache}, log, '下载第三方源码'))
            st.mark('deps', depsig)
        else:
            log.info('  第三方源码已就绪，跳过')
    summary = []
    if 'win' in targets:
        summary.append(build_windows(out_dir, manifest, src, work, dirs, env, sig, st, opts, log))
    if 'android' in targets:
        summary.append(build_android(out_dir, manifest, src, work, dirs, env, sig, st, opts, log, base_given))
    log.step('构建完成')
    for s in summary:
        log.detail(s)
    return 0


def _cmake_common():
    return ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DLIBRW_PLATFORM=GL3', '-DLIBRW_GL3_GFXLIB=SDL2']


def build_windows(out_dir, manifest, src, work, dirs, env, sig, st, opts, log):
    bdir = os.path.join(work, 'build-win')
    exe = os.path.join(bdir, 'rm8_app.exe')
    wsig = sig + '|' + tool_sig({'win'})
    if st.done('win', wsig, exe):
        log.info('  Windows 版：源码和工具都没变，跳过编译')
    else:
        log.step('编译 Windows 版（clang + Ninja，第一次约 2–5 分钟）')
        cc = ['-DCMAKE_C_COMPILER=x86_64-w64-mingw32-clang', '-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++']
        if not os.path.exists(os.path.join(bdir, 'build.ninja')):
            run(['cmake', '-S', src, '-B', bdir] + _cmake_common() + cc, env, work, log, '配置（CMake）', progress=False)
        run(['cmake', '--build', bdir, '--target', 'rm8_app'], env, work, log, '编译')
        if not os.path.exists(exe):
            raise BuildError('编译完成但没有生成 %s' % exe)
        st.mark('win', wsig)
    # package into the output folder: exe + needed DLLs + launcher; game\ and scenes\ are already there
    log.step('打包 Windows 版')
    shutil.copyfile(exe, os.path.join(out_dir, 'rm8_app.exe'))
    dlls = windows_dlls(exe, dirs, log)
    for d in dlls:
        shutil.copyfile(d, os.path.join(out_dir, os.path.basename(d)))
    with open(os.path.join(out_dir, 'rm8.bat'), 'w', encoding='ascii', newline='\r\n') as f:
        f.write('@echo off\n'
                'rem Starts the port on the data in this folder (game\\, scenes\\ and manifest.json, made by rm8_extract).\n'
                'rem rm8_app takes the single output folder: it reads manifest.json and finds game\\ and scenes\\ itself.\n'
                'cd /d "%%~dp0"\n'
                '"%%~dp0rm8_app.exe" "%%~dp0." %%*\n'.replace('%%', '%'))
    with open(os.path.join(out_dir, 'README_windows.txt'), 'w', encoding='utf-8-sig', newline='\r\n') as f:
        f.write('大富翁8 移植版（Windows）\n\n双击 rm8.bat 启动。这个文件夹里装着你自己游戏的数据（game、scenes），只供你本人使用，不要分享。\n'
                '第一次运行时，Windows 可能弹出蓝色的“Windows 已保护你的电脑”（SmartScreen）：点“更多信息”→“仍要运行”。'
                '这是因为程序没有付费的数字签名，不是病毒。\n'
                '存档和设置保存在 %APPDATA%\\RM8（rm8_gamedate.bin、rm8_box.bin、rm8_settings.txt、save 文件夹），不在这个文件夹里，重新提取或删除这个文件夹都不会丢进度。\n'
                '需要 Windows 10 或更新（或安装了 Universal C Runtime），显卡驱动要支持 OpenGL 3.3（打不开或黑屏时先更新显卡驱动）；电脑版文字使用系统自带的宋体。\n')
    res = '  Windows 版：%s\\rm8.bat（用的文件：rm8_app.exe%s）' % (out_dir, ('，' + '、'.join(os.path.basename(d) for d in dlls)) if dlls else '，没有额外的 DLL')
    if getattr(opts, 'zip', False):
        zp = os.path.join(out_dir, 'rm8_windows_%s_%s.zip' % (apkcmd.EDITION_NAMES.get(manifest.get('source', {}).get('edition'), 'x'), datetime.date.today().strftime('%Y%m%d')))
        log.info('  压缩成 %s（含你的游戏数据，只限本人使用）...' % zp)
        names = ['rm8_app.exe', 'rm8.bat', 'README_windows.txt', 'manifest.json'] + [os.path.basename(d) for d in dlls]
        with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for n in names:
                z.write(os.path.join(out_dir, n), n)
            for sub in ('game', 'scenes'):
                for dp, _, fs in os.walk(os.path.join(out_dir, sub)):
                    for f in sorted(fs):
                        p = os.path.join(dp, f)
                        z.write(p, os.path.relpath(p, out_dir).replace('\\', '/'))
        res += '\n  压缩包：%s（%s）' % (zp, human(os.path.getsize(zp)))
    return res


SYSTEM_DLL_RE = re.compile(r'^(api-ms-win-|ext-ms-win-)', re.I)


def windows_dlls(exe, dirs, log):
    """DLLs next to the exe that are not part of Windows: parse the import table with llvm-objdump and look for them in the llvm-mingw tree."""
    objdump = os.path.join(dirs['llvm-mingw'], 'bin', 'llvm-objdump.exe')
    r = subprocess.run([objdump, '-p', exe], capture_output=True, text=True, errors='replace')
    names = sorted({m.group(1) for m in re.finditer(r'DLL Name: (\S+)', r.stdout)})
    sysdir = os.path.join(os.environ.get('SystemRoot', r'C:\Windows'), 'System32')
    extra = []
    for n in names:
        if SYSTEM_DLL_RE.match(n) or os.path.exists(os.path.join(sysdir, n)):
            continue
        for d in (os.path.join(dirs['llvm-mingw'], 'x86_64-w64-mingw32', 'bin'), os.path.join(dirs['llvm-mingw'], 'bin')):
            if os.path.exists(os.path.join(d, n)):
                extra.append(os.path.join(d, n))
                break
        else:
            raise BuildError('rm8_app.exe 需要 %s，但在 Windows 和编译器里都找不到它。' % n)
    log.detail('imports of rm8_app.exe: %s; copied next to it: %s' % (names, [os.path.basename(e) for e in extra]))
    log.info('  rm8_app.exe 依赖 %d 个系统 DLL，需要随带的 DLL：%s' % (len(names), '、'.join(os.path.basename(e) for e in extra) or '无（已静态链接）'))
    return extra


def build_android(out_dir, manifest, src, work, dirs, env, sig, st, opts, log, base_given):
    if base_given:
        base = os.path.abspath(opts.base_apk)
        log.info('  使用指定的基础 APK：%s（不再编译安卓版）' % base)
    else:
        bdir = os.path.join(work, 'build-android')
        base = os.path.join(work, 'rm8_base.apk')
        asig = sig + '|' + tool_sig({'android'})
        sdk = os.path.join(dirs['android-ndk'], '..', '..')
        sdk = os.path.abspath(sdk)
        if st.done('android', asig, base):
            log.info('  安卓版：源码和工具都没变，跳过编译')
        else:
            log.step('编译安卓版（NDK r29，arm64-v8a，第一次约 3–6 分钟）')
            tcf = os.path.join(sdk, 'ndk', 'r29', 'build', 'cmake', 'android.toolchain.cmake')
            if not os.path.exists(os.path.join(bdir, 'build.ninja')):
                run(['cmake', '-S', src, '-B', bdir] + _cmake_common() + ['-DCMAKE_TOOLCHAIN_FILE=' + tcf, '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-24'],
                    env, work, log, '配置（CMake）', progress=False)
            run(['cmake', '--build', bdir], env, work, log, '编译')
            log.step('打包基础 APK（javac → d8 → aapt2 → zipalign → 签名）')
            run_py(os.path.join(src, 'android', 'build_apk.py'), ['--build', bdir, '--out', base, '--sdk', sdk, '--jdk', dirs['jdk']], env, log, '打包基础 APK')
            if not os.path.exists(base):
                raise BuildError('没有生成基础 APK：%s' % base)
            st.mark('android', asig)
    # inject the user's data
    class O:
        pass
    o = O()
    o.base_apk = base
    o.key_dir = getattr(opts, 'key_dir', None)
    o.apk_out = getattr(opts, 'apk_out', None)
    o.saves_staged = getattr(opts, 'saves_staged', None)
    log.detail('base apk %s (%d bytes)' % (base, os.path.getsize(base)))
    apkcmd.make_personal_apk(out_dir, o, log)
    return '  安卓版：%s（里面有你自己的游戏数据%s，不要分享；安装方法见上面的说明）' % (o.apk_out or os.path.join(out_dir, 'rm8_personal_DO_NOT_SHARE.apk'), '和存档' if o.saves_staged else '')
