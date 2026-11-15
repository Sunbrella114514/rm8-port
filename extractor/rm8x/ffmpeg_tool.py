"""Finding (or, only on request, downloading) ffmpeg for the Bink -> MPEG-1 conversion of the two movies.

The extractor never ships ffmpeg.  Search order: --ffmpeg PATH, <extractor>\\tools\\ffmpeg\\(bin\\)ffmpeg.exe, a previous download (cache), PATH.
When none is found the pinned download below is the DEFAULT (opt out: --no-download-ffmpeg or --no-video; interactive runs ask first).  It downloads
the pinned official-site build (a Windows build linked from ffmpeg.org's download page), checks size and SHA-256
against the values pinned here, and only unpacks ffmpeg.exe / ffprobe.exe (no other zip member is written).  The program runs as a separate process.
License note: FFmpeg's native Bink decoder (libavcodec) is LGPL; this particular build is GPL-3.0 as a whole.  We neither ship nor link it.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

from . import netdl
from .util import app_dir, human

# Pinned download (gyan.dev "essentials" build 9.0.2, the build listed on https://ffmpeg.org/download.html#build-windows).
# The numbers were recorded from the build the dev machine uses (checked against the publisher's .sha256 file).
PINNED = {
    'version': '9.0.2',
    'url': 'https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-9.0.2-essentials_build.zip',
    'size': 114768076,
    'sha256': '60f467265b1e312373dbcd92200c2618a74850f98d3d078e94296bb3fa2047ba',
    'license': 'GPL-3.0 build (essentials); only the LGPL-capable native mpeg1video/mp2 encoders and the Bink decoder are used',
}
WANT = ('ffmpeg.exe', 'ffprobe.exe')


def cache_dir():
    base = os.environ.get('LOCALAPPDATA') or os.path.join(os.path.expanduser('~'), '.cache')
    return os.path.join(base, 'rm8_extract', 'ffmpeg-' + PINNED['version'])


def _probe(path):
    """Return the 'ffmpeg version ...' line, or None when this does not run."""
    try:
        r = subprocess.run([path, '-version'], capture_output=True, text=True, timeout=30)
        if r.returncode == 0 and r.stdout.startswith('ffmpeg version'):
            return r.stdout.splitlines()[0]
    except (OSError, subprocess.SubprocessError):
        pass
    return None


def _with_ffprobe(path):
    exe = '.exe' if os.name == 'nt' else ''
    return os.path.exists(os.path.join(os.path.dirname(path), 'ffprobe' + exe))


def _candidates(opts):
    exe = 'ffmpeg.exe' if os.name == 'nt' else 'ffmpeg'
    if opts.ffmpeg:
        yield opts.ffmpeg, 'user'
        return
    root = app_dir()
    yield os.path.join(root, 'tools', 'ffmpeg', exe), 'bundled-folder'
    yield os.path.join(root, 'tools', 'ffmpeg', 'bin', exe), 'bundled-folder'
    yield os.path.join(cache_dir(), exe), 'download-cache'
    w = shutil.which('ffmpeg')
    if w:
        yield w, 'PATH'


def have_ffmpeg(opts):
    """An ffmpeg that is already on this machine (no download): {'path', 'version', 'source'} or None."""
    for path, where in _candidates(opts):
        if os.path.exists(path):
            v = _probe(path)
            if v and _with_ffprobe(path):
                return {'path': path, 'version': v, 'source': where}
    return None


def find_ffmpeg(opts, log):
    for path, where in _candidates(opts):
        if os.path.exists(path):
            v = _probe(path)
            if v and _with_ffprobe(path):
                return {'path': path, 'version': v, 'source': where}
            if where == 'user':
                log.warn('你用 --ffmpeg 指定的 %s 不能运行，或者它旁边没有 ffprobe.exe。' % path)
    if getattr(opts, 'download_ffmpeg', True):
        return download_ffmpeg(log, ask=getattr(opts, 'ask_download', False))
    return None


NOTICE = '\n'.join([
    '  没有找到 ffmpeg。把原版影片（Bink 格式）转成移植版能播放的 MPEG-1 需要它，现在自动下载：',
    '    来源：%(url)s',
    '          （ffmpeg 官网 ffmpeg.org/download.html 列出的 Windows 构建，版本 %(version)s）',
    '    大小：%(mb).0f MB；会核对大小和 SHA-256（%(sha)s…），不一致就丢弃',
    '    只会解出 ffmpeg.exe 和 ffprobe.exe，保存到 %(dst)s；作为独立程序运行，不会安装到系统',
    '    许可证：FFmpeg 自带的 Bink 解码器是 LGPL，这个构建整体是 GPL-3.0；本程序和移植版都不包含也不再分发它',
    '    不想下载：按 Ctrl+C 后加 --no-video 跳过影片（游戏照样能玩），或用 --ffmpeg 指定你自己的 ffmpeg',
    '  下载较大，网速慢时可能要等十几分钟……'])


def download_ffmpeg(log, ask=False):
    dst = cache_dir()
    log.info(NOTICE % {'url': PINNED['url'], 'version': PINNED['version'], 'mb': PINNED['size'] / 1e6, 'sha': PINNED['sha256'][:12], 'dst': dst})
    if ask:
        try:
            ans = input('  现在下载吗？[Y/n] ').strip().lower()
        except EOFError:
            ans = ''
        if ans in ('n', 'no'):
            log.warn('已取消下载，将跳过影片')
            return None
    os.makedirs(dst, exist_ok=True)
    tmpdir = tempfile.mkdtemp(prefix='rm8x_dl_')
    zpath = os.path.join(tmpdir, 'ffmpeg.zip')
    try:
        netdl.download_file(PINNED['url'], zpath, PINNED['size'], log, 'ffmpeg')
        n = os.path.getsize(zpath)
        h = hashlib.sha256(open(zpath, 'rb').read())
        if n != PINNED['size'] or h.hexdigest() != PINNED['sha256']:
            raise RuntimeError('下载文件的大小/SHA-256 与固定值不符（%d 字节，%s）。已丢弃。' % (n, h.hexdigest()))
        got = 0
        with zipfile.ZipFile(zpath) as z:
            for info in z.infolist():
                base = os.path.basename(info.filename)
                if base in WANT and info.filename.replace('\\', '/').split('/')[-2:-1] == ['bin']:
                    with z.open(info) as s, open(os.path.join(dst, base), 'wb') as o:
                        shutil.copyfileobj(s, o)
                    got += 1
        if got != len(WANT):
            raise RuntimeError('压缩包里没有找到 bin/ffmpeg.exe 和 ffprobe.exe')
        path = os.path.join(dst, 'ffmpeg.exe')
        v = _probe(path)
        if not v:
            raise RuntimeError('下载的 ffmpeg 无法运行')
        log.info('  已校验并解压到 %s' % dst)
        return {'path': path, 'version': v, 'source': 'downloaded', 'url': PINNED['url'], 'sha256': PINNED['sha256']}
    except Exception as e:
        from . import friendly
        text = str(e)
        if friendly.is_disk_full(e):
            log.warn('磁盘空间不够，没能下载 ffmpeg（约需 %s）。已跳过开场 / 制作人员影片，游戏照样能玩；腾出空间后重新运行就会补上。' % human(PINNED['size'] * 3))
        elif isinstance(e, netdl.DownloadError) or netdl.looks_like_network_error(text):
            log.warn('没能下载 ffmpeg。' + friendly.network_failure('用来转换影片的 ffmpeg', [PINNED['url']], text) +
                     '\n  已跳过开场 / 制作人员影片，游戏照样能玩；网络好了以后重新运行（加 --rebuild）就会补上。也可以用 --ffmpeg 指定你自己的 ffmpeg。')
        else:
            log.warn('下载 ffmpeg 失败：%s\n  已跳过开场 / 制作人员影片，游戏照样能玩。' % text)
        return None
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)
