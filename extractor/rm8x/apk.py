"""Build a personal APK: take the data-free base APK, add the extracted data under assets/rm8data/, align it like zipalign and sign it with
APK Signature Scheme v2 -- all in pure Python, no Android SDK / JDK needed.

Layout the app expects (port/app/rm8_app.cpp unpackBundledData, same as port/android/build_apk.py --with-data):
    assets/rm8data/manifest.txt      "version <id>" then one "<size>\\t<relative path>" line per file
    assets/rm8data/<relative path>   game/... and scenes/... (+ manifest.json), STORED for .mp3 .mpg .png, DEFLATED otherwise
The app unpacks the files into its data folder on the first start (and again when <id> changes).

Zip handling follows zipalign: STORED entries start on a 4-byte boundary, STORED .so libraries on a 16 KiB boundary (-P 16); the padding goes into
the local header's extra field.  Entries of the base APK are copied without recompressing; its old signature (META-INF/*) and signing block are dropped.
The result is signed with the user's own key (rsa_x509.py): v2 only, which Android 7.0+ (the app's minSdk is 24) requires and accepts.
"""
import datetime
import hashlib
import os
import json
import struct
import tempfile
import zipfile
import zlib

from . import rsa_x509 as rsa

V2_ID = 0x7109871a
ALG_RSA_PKCS1_SHA256 = 0x0103
CHUNK = 1 << 20
DOS_TIME, DOS_DATE = 0, ((2026 - 1980) << 9) | (1 << 5) | 1        # fixed timestamp (reproducible)
STORED_EXT = ('.mp3', '.mpg', '.png')
PAGE = 16384


class ApkError(Exception):
    pass


class UserApkError(ApkError):
    no_hint = True


# ------------------------------------------------------------------------------------------------------ signing key
def default_key_dir():
    base = os.environ.get('APPDATA') or os.path.join(os.path.expanduser('~'), '.config')
    return os.path.join(base, 'RM8', 'apk_key')


def load_or_create_key(key_dir, log=None):
    """-> (RsaKey, certificate DER, created: bool).  The same key is reused so that later APKs install over earlier ones."""
    kp, cp = os.path.join(key_dir, 'rm8_apk_key.pem'), os.path.join(key_dir, 'rm8_apk_cert.pem')
    if os.path.exists(kp) and os.path.exists(cp):
        key = rsa.RsaKey.from_pem(open(kp, encoding='ascii').read())
        cert = rsa.pem_decode(open(cp, encoding='ascii').read(), 'CERTIFICATE')
        if rsa.cert_spki(cert) != key.spki():
            raise ApkError('签名密钥和证书对不上：%s' % key_dir)
        return key, cert, False
    if log:
        log.info('  第一次生成你自己的 APK 签名密钥（只在本机，约几秒）...')
    key = rsa.RsaKey.generate(2048)
    cert = rsa.self_signed_cert(key)
    os.makedirs(key_dir, exist_ok=True)
    with open(kp, 'w', encoding='ascii', newline='\n') as f:
        f.write(key.to_pem())
    with open(cp, 'w', encoding='ascii', newline='\n') as f:
        f.write(rsa.pem('CERTIFICATE', cert))
    return key, cert, True


# ------------------------------------------------------------------------------------------------------ zip writing
def _pad_extra(data_offset_without_extra, align):
    """Extra-field bytes that make the entry data start on `align`."""
    need = (-data_offset_without_extra) % align
    if need == 0:
        return b''
    if need < 4:
        need += align
    return struct.pack('<HH', 0xD935, need - 4) + b'\x00' * (need - 4)


class ZipOut:
    def __init__(self, path):
        self.f = open(path, 'wb')
        self.entries = []          # (name, method, crc, csize, usize, header_offset)

    def _align_for(self, name, method):
        if method != 0:
            return 1
        return PAGE if name.endswith('.so') else 4

    def add_raw(self, name, method, crc, csize, usize, raw_iter):
        """Write one entry whose (already compressed / stored) data comes from raw_iter."""
        nb = name.encode('utf-8')
        off = self.f.tell()
        align = self._align_for(name, method)
        extra = _pad_extra(off + 30 + len(nb), align) if align > 1 else b''
        self.f.write(struct.pack('<IHHHHHIIIHH', 0x04034b50, 20, 0x0800, method, DOS_TIME, DOS_DATE, crc, csize, usize, len(nb), len(extra)))
        self.f.write(nb + extra)
        n = 0
        for chunk in raw_iter:
            self.f.write(chunk)
            n += len(chunk)
        if n != csize:
            raise ApkError('internal error: %s wrote %d bytes, expected %d' % (name, n, csize))
        self.entries.append((name, method, crc, csize, usize, off))

    def add_file(self, name, path, stored):
        usize = os.path.getsize(path)
        crc = 0
        if stored:
            with open(path, 'rb') as f:
                for b in iter(lambda: f.read(1 << 22), b''):
                    crc = zlib.crc32(b, crc)

            def gen():
                with open(path, 'rb') as f:
                    for b in iter(lambda: f.read(1 << 22), b''):
                        yield b
            self.add_raw(name, 0, crc & 0xffffffff, usize, usize, gen())
            return
        # deflate: compress to a temporary file first (the compressed size goes into the header)
        with tempfile.TemporaryFile() as t:
            co = zlib.compressobj(6, zlib.DEFLATED, -15)
            with open(path, 'rb') as f:
                for b in iter(lambda: f.read(1 << 22), b''):
                    crc = zlib.crc32(b, crc)
                    t.write(co.compress(b))
            t.write(co.flush())
            csize = t.tell()
            t.seek(0)
            self.add_raw(name, 8, crc & 0xffffffff, csize, usize, iter(lambda: t.read(1 << 22), b''))

    def add_bytes(self, name, data, stored=False):
        crc = zlib.crc32(data) & 0xffffffff
        if stored:
            self.add_raw(name, 0, crc, len(data), len(data), iter([data]))
        else:
            co = zlib.compressobj(6, zlib.DEFLATED, -15)
            z = co.compress(data) + co.flush()
            self.add_raw(name, 8, crc, len(z), len(data), iter([z]))

    def central_directory(self):
        out = []
        for name, method, crc, csize, usize, off in self.entries:
            nb = name.encode('utf-8')
            out.append(struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 0x0314, 20, 0x0800, method, DOS_TIME, DOS_DATE, crc, csize, usize,
                                   len(nb), 0, 0, 0, 0, 0, off) + nb)
        return b''.join(out)


def eocd(count, cd_size, cd_offset):
    return struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, count, count, cd_size, cd_offset, 0)


# ------------------------------------------------------------------------------------------------------ v2 signing
def _chunks_digest(f, start, end):
    """Digests of the 1 MiB chunks of f[start:end] -> list of 32-byte values."""
    out = []
    f.seek(start)
    left = end - start
    while left > 0:
        b = f.read(min(CHUNK, left))
        if not b:
            raise ApkError('short read')
        left -= len(b)
        out.append(hashlib.sha256(b'\xa5' + struct.pack('<I', len(b)) + b).digest())
    return out


def _top_digest(parts):
    """parts: list of (bytes-like source) chunk digest lists, in order -> final 32-byte digest."""
    allc = [d for p in parts for d in p]
    return hashlib.sha256(b'\x5a' + struct.pack('<I', len(allc)) + b''.join(allc)).digest()


def _lp(b):
    return struct.pack('<I', len(b)) + b


def build_signing_block(digest, cert_der, key):
    digests = _lp(struct.pack('<I', ALG_RSA_PKCS1_SHA256) + _lp(digest))
    signed_data = _lp(digests) + _lp(_lp(cert_der)) + _lp(b'')
    sig = key.sign_sha256(signed_data)
    sigs = _lp(struct.pack('<I', ALG_RSA_PKCS1_SHA256) + _lp(sig))
    signer = _lp(signed_data) + _lp(sigs) + _lp(key.spki())
    value = _lp(_lp(signer))
    pair = struct.pack('<QI', 4 + len(value), V2_ID) + value
    size = len(pair) + 8 + 16
    return struct.pack('<Q', size) + pair + struct.pack('<Q', size) + b'APK Sig Block 42'


def sign_v2(path, cert_der, key):
    """Insert an APK Signing Block (v2) before the central directory of the zip at `path` (in place)."""
    with open(path, 'r+b') as f:
        f.seek(0, 2)
        total = f.tell()
        f.seek(total - 22)
        e = f.read(22)
        sig, _, _, n1, n2, cd_size, cd_off, clen = struct.unpack('<IHHHHIIH', e)
        if sig != 0x06054b50 or clen:
            raise ApkError('unexpected end-of-central-directory record')
        f.seek(cd_off)
        cd = f.read(cd_size)
        eo = eocd(n1, cd_size, cd_off)                    # for the digest the offset is that of the (future) signing block == cd_off
        parts = [_chunks_digest(f, 0, cd_off)]
        parts.append([hashlib.sha256(b'\xa5' + struct.pack('<I', len(cd[i:i + CHUNK])) + cd[i:i + CHUNK]).digest() for i in range(0, len(cd), CHUNK)])
        parts.append([hashlib.sha256(b'\xa5' + struct.pack('<I', len(eo)) + eo).digest()])
        block = build_signing_block(_top_digest(parts), cert_der, key)
        f.seek(cd_off)
        f.write(block)
        f.write(cd)
        f.write(eocd(n1, cd_size, cd_off + len(block)))
        f.truncate()


def verify_v2(path):
    """Independent check of the v2 signature (digest + RSA signature). Returns the certificate DER; raises ApkError."""
    with open(path, 'rb') as f:
        f.seek(0, 2)
        total = f.tell()
        f.seek(total - 22)
        sig, _, _, n1, n2, cd_size, cd_off, clen = struct.unpack('<IHHHHIIH', f.read(22))
        if sig != 0x06054b50:
            raise ApkError('no EOCD')
        f.seek(cd_off - 24)
        size2, magic = struct.unpack('<Q16s', f.read(24))
        if magic != b'APK Sig Block 42':
            raise ApkError('no signing block')
        block_start = cd_off - size2 - 8
        f.seek(block_start)
        size1 = struct.unpack('<Q', f.read(8))[0]
        if size1 != size2:
            raise ApkError('signing block sizes differ')
        pairs = f.read(size2 - 24)
        p, v2 = 0, None
        while p < len(pairs):
            ln, pid = struct.unpack_from('<QI', pairs, p)
            if pid == V2_ID:
                v2 = pairs[p + 12:p + 8 + ln]
            p += 8 + ln
        if v2 is None:
            raise ApkError('no v2 block')
        q = 4                                              # signers sequence: length, then signer
        signer_len = struct.unpack_from('<I', v2, q)[0]; q += 4
        signer = v2[q:q + signer_len]
        sd_len = struct.unpack_from('<I', signer, 0)[0]
        signed_data = signer[4:4 + sd_len]
        r = 4 + sd_len
        sigs_len = struct.unpack_from('<I', signer, r)[0]
        sigs = signer[r + 4:r + 4 + sigs_len]
        r += 4 + sigs_len
        pk_len = struct.unpack_from('<I', signer, r)[0]
        spki = signer[r + 4:r + 4 + pk_len]
        # signature
        one_len = struct.unpack_from('<I', sigs, 0)[0]
        algo, sg_len = struct.unpack_from('<II', sigs, 4)
        signature = sigs[12:12 + sg_len]
        if algo != ALG_RSA_PKCS1_SHA256 or not rsa.verify_sha256(spki, signed_data, signature):
            raise ApkError('v2 signature does not verify')
        # digest
        dl = struct.unpack_from('<I', signed_data, 0)[0]
        digs = signed_data[4:4 + dl]
        dlen = struct.unpack_from('<I', digs, 8)[0]
        want = digs[12:12 + dlen]
        cl = struct.unpack_from('<I', signed_data, 4 + dl)[0]
        certs = signed_data[8 + dl:8 + dl + cl]
        cert = certs[4:4 + struct.unpack_from('<I', certs, 0)[0]]
        f.seek(cd_off)
        cd = f.read(cd_size)
        parts = [_chunks_digest(f, 0, block_start)]
        parts.append([hashlib.sha256(b'\xa5' + struct.pack('<I', len(cd[i:i + CHUNK])) + cd[i:i + CHUNK]).digest() for i in range(0, len(cd), CHUNK)])
        eo = eocd(n1, cd_size, block_start)
        parts.append([hashlib.sha256(b'\xa5' + struct.pack('<I', len(eo)) + eo).digest()])
        if _top_digest(parts) != want:
            raise ApkError('v2 digest does not match the APK contents')
        if rsa.cert_spki(cert) != spki:
            raise ApkError('certificate does not match the public key')
        return cert


# ------------------------------------------------------------------------------------------------------ the build
def data_list(out_dir):
    """[(abs path, rel path)] of everything the app unpacks: game\\ and scenes\\ (+ a trimmed manifest.json handled by the caller)."""
    files = []
    for sub in ('game', 'scenes'):
        base = os.path.join(out_dir, sub)
        for dp, _, fs in os.walk(base):
            for f in sorted(fs):
                p = os.path.join(dp, f)
                files.append((p, os.path.relpath(p, out_dir).replace('\\', '/')))
    files.sort(key=lambda t: t[1])
    return files


def check_base(base_apk):
    try:
        z = zipfile.ZipFile(base_apk)
    except (OSError, zipfile.BadZipFile) as e:
        raise ApkError('基础 APK 打不开：%s（%s）' % (base_apk, e))
    names = set(z.namelist())
    for need in ('AndroidManifest.xml', 'classes.dex', 'lib/arm64-v8a/libmain.so', 'lib/arm64-v8a/libSDL2.so'):
        if need not in names:
            raise ApkError('这不是移植版的基础 APK（缺少 %s）：%s' % (need, base_apk))
    z.close()


IMPORT_PREFIX = 'import_saves/'      # manifest.txt / asset path prefix of the migrated saves; the app copies them to <data dir>/<rest> only when that file does not exist


def build_personal_apk(base_apk, out_dir, out_apk, key, cert, manifest, log=None, progress=None, saves=None):
    """base_apk + extracted data in out_dir -> out_apk (aligned, v2-signed).  `manifest` is the extraction's manifest dict."""
    check_base(base_apk)
    files = data_list(out_dir)
    if not any(r.startswith('game/Clump/') for _, r in files):
        raise ApkError('输出文件夹里没有 game\\（提取时用了 --no-copy-game？）：手机版需要完整的 game\\ 和 scenes\\')
    # a trimmed copy of manifest.json (the user's own game path stays on the PC)
    m = dict(manifest)
    m['source'] = dict(manifest.get('source', {}), game_dir='')
    mjson = json.dumps(m, ensure_ascii=False, indent=1, sort_keys=True).encode('utf-8')
    h = hashlib.sha1()
    for p, r in files:
        h.update(('%s %d\n' % (r, os.path.getsize(p))).encode())
    for k in sorted(manifest.get('outputs', {})):
        h.update((k + manifest['outputs'][k]['sha256']).encode())
    for k in sorted(manifest.get('game_files', {})):
        h.update((k + manifest['game_files'][k]['sha256']).encode())
    saves = saves or []                                     # [(abs path, rel path)] from saves.staged_files
    for p, r in saves:
        h.update(('%s %d %s\n' % (r, os.path.getsize(p), hashlib.sha1(open(p, 'rb').read()).hexdigest())).encode())
    h.update(str(manifest.get('data_format')).encode() + str(manifest.get('extractor_version')).encode())
    version = h.hexdigest()[:16]
    lines = ['version %s' % version] + ['%d\t%s' % (os.path.getsize(p), r) for p, r in files] + ['%d\tmanifest.json' % len(mjson)] + ['%d\t%s%s' % (os.path.getsize(p), IMPORT_PREFIX, r) for p, r in saves]
    tmp_out = out_apk + '.part'
    zo = ZipOut(tmp_out)
    try:
        with zipfile.ZipFile(base_apk) as z, open(base_apk, 'rb') as raw:
            for zi in z.infolist():
                n = zi.filename
                if n.endswith('/') or n.startswith('META-INF/') or n.startswith('assets/rm8data/'):
                    continue
                raw.seek(zi.header_offset)
                lh = raw.read(30)
                nlen, elen = struct.unpack_from('<HH', lh, 26)
                raw.seek(zi.header_offset + 30 + nlen + elen)
                data = raw.read(zi.compress_size)
                zo.add_raw(n, zi.compress_type, zi.CRC, zi.compress_size, zi.file_size, iter([data]))
        zo.add_bytes('assets/rm8data/manifest.txt', ('\n'.join(lines) + '\n').encode('utf-8'))
        total = sum(os.path.getsize(p) for p, _ in files)
        done = 0
        for i, (p, r) in enumerate(files):
            zo.add_file('assets/rm8data/' + r, p, stored=r.endswith(STORED_EXT))
            done += os.path.getsize(p)
            if progress and (i % 100 == 0 or os.path.getsize(p) > (20 << 20)):
                progress(done, total, r)
        zo.add_bytes('assets/rm8data/manifest.json', mjson)
        for p, r in saves:
            zo.add_file('assets/rm8data/' + IMPORT_PREFIX + r, p, stored=False)
        cd_off = zo.f.tell()
        cd = zo.central_directory()
        zo.f.write(cd)
        zo.f.write(eocd(len(zo.entries), len(cd), cd_off))
        zo.f.close()
        if os.path.getsize(tmp_out) >= (1 << 32) - (1 << 20):
            raise ApkError('APK 超过 4 GB，装不了')
        if log:
            log.info('  签名（APK Signature Scheme v2）...')
        sign_v2(tmp_out, cert, key)
        verify_v2(tmp_out)
        if os.path.exists(out_apk):
            os.remove(out_apk)
        os.rename(tmp_out, out_apk)
    except BaseException:
        try:
            zo.f.close()
        except Exception:
            pass
        if os.path.exists(tmp_out):
            os.remove(tmp_out)
        raise
    return {'version': version, 'files': len(files) + 1, 'bytes': os.path.getsize(out_apk), 'data_bytes': total}
