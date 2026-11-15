"""Editions of the game data this project reads (the exporters ask this module instead of hard-coding the Big5 edition).

  tc  the original Traditional-Chinese install (Big5 / cp950 text), Config\\*.bin obfuscation key b'tiep8jdg'
  sc  the Steam release (Simplified-Chinese, GBK text), Config\\*.bin obfuscation key b'seowk48h'

The Config\\*.bin files are text files whose bytes are shifted by an 8-byte key (plain = (byte - key[i % 8]) & 255, docs/config_files.md).
detect(game) finds the edition by trying each key on Config\\stage.bin (a plain text file starting with '[').  Results for the 'tc' edition
are exactly what the exporters produced before this module existed."""
import os

EDITIONS = [
    {'id': 'tc', 'name': '繁体中文版 (Big5)', 'key': b'tiep8jdg', 'enc': 'cp950'},
    {'id': 'sc', 'name': '简体中文版 (GBK, Steam)', 'key': b'seowk48h', 'enc': 'gbk'},
]
_cache = {}


def _shift(d, key):
    return bytes((b - key[i % len(key)]) & 255 for i, b in enumerate(d))


def config_path(game, *parts):
    """Case-insensitive path below game/Config (the Steam install has lower-case folder names; Windows does not care, other systems do)."""
    p = game
    for part in ('Config',) + parts:
        names = os.listdir(p)
        p = os.path.join(p, part if part in names else next((n for n in names if n.lower() == part.lower()), part))
    return p


def detect(game):
    key = os.path.abspath(game)
    if key in _cache:
        return _cache[key]
    d = open(config_path(game, 'stage.bin'), 'rb').read()
    found = None
    for ed in EDITIONS:
        p = _shift(d, ed['key'])
        if p[:1] == b'[' and b'\nid=1\n' in p[:64]:
            try:
                p.decode(ed['enc'])
            except UnicodeDecodeError:
                continue
            found = ed
            break
    _cache[key] = found or EDITIONS[0]
    return _cache[key]


def decrypt(game, data):
    """Plain bytes of a Config\\*.bin file."""
    return _shift(data, detect(game)['key'])


def decode(game, data):
    return data.decode(detect(game)['enc'])
