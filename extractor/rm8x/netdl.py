"""Resumable file download with progress lines.  Uses curl.exe when it is there (every Windows 10 / 11 has one; on some machines it is many times faster
than Python's urllib), otherwise urllib.  Checking the result (size / SHA-256) is the caller's job."""
import os
import shutil
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request

from .util import human


class DownloadError(Exception):
    pass


def download_file(url, part, total, log, label=''):
    """Download `url` to `part`, resuming a partial file. `total` = expected size (for progress and the resume decision)."""
    os.makedirs(os.path.dirname(part), exist_ok=True)
    if os.path.exists(part) and total and os.path.getsize(part) > total:
        os.remove(part)
    if os.path.exists(part) and total and os.path.getsize(part) == total:
        return
    curl = shutil.which('curl')
    t0 = time.time()
    base = os.path.getsize(part) if os.path.exists(part) else 0
    state = {'last': -1}

    def report():
        done = os.path.getsize(part) if os.path.exists(part) else 0
        pct = done * 100 // total if total else 0
        if pct // 10 != state['last'] // 10:
            state['last'] = pct
            log.info('    [%s] %3d%%  %s / %s  (%.1f MB/s)' % (label[:22], pct, human(done), human(total), max(done - base, 0) / 1e6 / max(time.time() - t0, 0.1)))
    if curl:
        cmd = [curl, '-sS', '-L', '--fail', '--retry', '5', '--retry-delay', '3', '--retry-connrefused', '-C', '-', '-A', 'rm8-extract', '-o', part, url]
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors='replace')
        while p.poll() is None:
            time.sleep(1.0)
            report()
        err = p.stderr.read().strip()
        if p.returncode != 0:
            raise DownloadError('curl 退出码 %d：%s' % (p.returncode, err[-300:]))
        report()
        return
    err = None
    for attempt in range(5):
        try:
            done = os.path.getsize(part) if os.path.exists(part) else 0
            req = urllib.request.Request(url, headers={'User-Agent': 'rm8-extract', **({'Range': 'bytes=%d-' % done} if done else {})})
            with urllib.request.urlopen(req, timeout=60) as r:
                if done and r.status != 206:
                    done = 0
                with open(part, 'ab' if done else 'wb') as f:
                    while True:
                        b = r.read(1 << 20)
                        if not b:
                            break
                        f.write(b)
                        report()
            return
        except Exception as e:
            err = e
            time.sleep(3 * (attempt + 1))
    raise DownloadError(str(err))


# ---------------------------------------------------------------------------------------------- is the network there?
def _reachable(url, timeout=10):
    """True when `url` answers at all (any HTTP status counts).  urllib honours the system / environment proxy; curl.exe is tried as a second opinion."""
    try:
        req = urllib.request.Request(url, method='HEAD', headers={'User-Agent': 'rm8-extract'})
        urllib.request.urlopen(req, timeout=timeout).close()
        return True
    except urllib.error.HTTPError:
        return True                    # the server answered: it is reachable
    except Exception:
        pass
    curl = shutil.which('curl')
    if curl:
        try:
            r = subprocess.run([curl, '-sI', '-L', '--max-time', str(timeout), '-o', os.devnull, '-w', '%{http_code}', '-A', 'rm8-extract', url],
                               capture_output=True, text=True, timeout=timeout + 5)
            return r.stdout.strip().isdigit() and int(r.stdout.strip()) > 0
        except (OSError, subprocess.SubprocessError, ValueError):
            pass
    return False


def unreachable_hosts(urls, timeout=10):
    """The hosts (one probe URL each) of `urls` that cannot be reached: [(host, url)]."""
    seen, bad = set(), []
    for u in urls:
        host = urllib.parse.urlsplit(u).netloc
        if host in seen:
            continue
        seen.add(host)
        if not _reachable('https://%s/' % host, timeout):
            bad.append((host, u))
    return bad


NET_PATTERNS = ('getaddrinfo', 'urlopen error', 'timed out', 'time out', 'Connection refused', 'Connection reset', 'Could not resolve', "Couldn't connect",
                'Failed to connect', 'Network is unreachable', 'WinError 10060', 'WinError 10061', 'WinError 10054', 'WinError 11001', 'WinError 10051',
                'SSL', 'curl 退出码 6', 'curl 退出码 7', 'curl 退出码 28', 'curl 退出码 35', 'curl 退出码 56', 'URLError', 'RemoteDisconnected', 'IncompleteRead')


def looks_like_network_error(text):
    return any(p in text for p in NET_PATTERNS)


def offline_message(what, hosts=()):
    h = ('（连不上：%s）' % '、'.join(hosts)) if hosts else ''
    return ('无法连接网络%s，所以不能下载%s。\n'
            '  - 请检查网络连接，然后重新运行（已下载的部分会接着用，不会重头开始）。\n'
            '  - 如果你上网需要代理，可以先设置环境变量 HTTPS_PROXY（例如 http://127.0.0.1:7890）再运行；\n'
            '  - 也可以用别的办法自己下载文件，放进工具缓存文件夹里的 downloads 文件夹，文件名要与下载地址里的一样，程序会校验后直接使用。' % (h, what))
