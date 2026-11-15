"""Small helpers shared by the extractor modules."""
import hashlib
import os
import sys
import time

CHUNK = 4 << 20


def sha256_file(path, progress=None):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            b = f.read(CHUNK)
            if not b:
                break
            h.update(b)
            if progress:
                progress(len(b))
    return h.hexdigest()


def human(n):
    for u in ('B', 'KB', 'MB', 'GB'):
        if n < 1024 or u == 'GB':
            return ('%d %s' % (n, u)) if u == 'B' else ('%.1f %s' % (n, u))
        n /= 1024.0


class Log:
    """Console + file log. Messages for the user are Chinese; technical details stay as they are."""

    def __init__(self, path=None, quiet=False):
        self.f = open(path, 'a', encoding='utf-8', newline='\n') if path else None
        self.quiet = quiet
        self.t0 = time.time()

    def attach(self, path):
        """Start writing to a log file (called once the output folder is known to be ours)."""
        self.f = open(path, 'a', encoding='utf-8', newline='\n')
        self.f.write('--- %s ---\n' % time.strftime('%Y-%m-%d %H:%M:%S'))

    def _w(self, s, console=True):
        if self.f:
            self.f.write('[%7.1fs] %s\n' % (time.time() - self.t0, s)); self.f.flush()
        if console and not self.quiet:
            try:
                print(s, flush=True)
            except UnicodeEncodeError:
                print(s.encode('ascii', 'replace').decode('ascii'), flush=True)

    def info(self, s): self._w(s)
    def step(self, s): self._w('==> ' + s)
    def warn(self, s): self._w('警告: ' + s)
    def error(self, s): self._w('错误: ' + s)
    def detail(self, s): self._w(s, console=False)

    def close(self):
        if self.f:
            self.f.close(); self.f = None


def app_dir():
    """Folder holding the extractor's own files (data/, exporters/): the PyInstaller bundle or the source folder."""
    if getattr(sys, 'frozen', False):
        return getattr(sys, '_MEIPASS', os.path.dirname(sys.executable))
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def digest_of(mapping):
    """Short digest of {name: sha256}: identifies one exact set of exporter scripts."""
    return hashlib.sha256('\n'.join('%s:%s' % kv for kv in sorted(mapping.items())).encode()).hexdigest()[:16]
