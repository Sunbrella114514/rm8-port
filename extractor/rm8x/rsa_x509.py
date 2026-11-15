"""Pure-Python RSA key generation, PKCS#1 v1.5 (SHA-256) signing and a minimal self-signed X.509 certificate -- just enough to sign an APK with
the APK Signature Scheme v2 without the Android SDK or a JDK.  Standard library only.

Written for this project (GPL-3.0-or-later).  The key material is the user's own: generated locally on first use and never leaves the machine.
RSA-2048 with e = 65537; primes by Miller-Rabin (40 rounds) from the OS random source.  This is adequate for a personal sideloading key;
it is not meant as a general crypto library (no blinding / constant-time arithmetic, which do not matter for an offline signing tool).
"""
import base64
import datetime
import hashlib
import secrets

SHA256_DIGESTINFO = bytes.fromhex('3031300d060960864801650304020105000420')
OID_SHA256_RSA = bytes.fromhex('2a864886f70d01010b')      # 1.2.840.113549.1.1.11
OID_RSA = bytes.fromhex('2a864886f70d010101')             # 1.2.840.113549.1.1.1
OID_CN = bytes.fromhex('550403')                          # 2.5.4.3
OID_O = bytes.fromhex('55040a')                           # 2.5.4.10

# ---------------------------------------------------------------------------------------------------------------- DER


def _len(n):
    if n < 0x80:
        return bytes([n])
    b = n.to_bytes((n.bit_length() + 7) // 8, 'big')
    return bytes([0x80 | len(b)]) + b


def tlv(tag, body):
    return bytes([tag]) + _len(len(body)) + body


def der_int(n):
    b = n.to_bytes(max(1, (n.bit_length() + 8) // 8), 'big')       # leading 0x00 when the top bit is set
    return tlv(0x02, b)


def der_seq(*items):
    return tlv(0x30, b''.join(items))


def der_set(*items):
    return tlv(0x31, b''.join(items))


def der_oid(raw):
    return tlv(0x06, raw)


def der_null():
    return b'\x05\x00'


def der_bitstring(b):
    return tlv(0x03, b'\x00' + b)


def der_utf8(s):
    return tlv(0x0c, s.encode('utf-8'))


def der_utctime(dt):
    return tlv(0x17, dt.strftime('%y%m%d%H%M%SZ').encode())


def parse_tlv(data, pos=0):
    """-> (tag, body_start, body_end)"""
    tag = data[pos]
    l = data[pos + 1]
    p = pos + 2
    if l & 0x80:
        k = l & 0x7f
        l = int.from_bytes(data[p:p + k], 'big')
        p += k
    return tag, p, p + l


def parse_seq_ints(data):
    tag, s, e = parse_tlv(data)
    assert tag == 0x30
    out, p = [], s
    while p < e:
        t, bs, be = parse_tlv(data, p)
        assert t == 0x02
        out.append(int.from_bytes(data[bs:be], 'big'))
        p = be
    return out

# ---------------------------------------------------------------------------------------------------------------- RSA


def _is_prime(n, rounds=40):
    if n < 2:
        return False
    for p in (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37):
        if n % p == 0:
            return n == p
    d, r = n - 1, 0
    while d % 2 == 0:
        d //= 2
        r += 1
    for _ in range(rounds):
        a = secrets.randbelow(n - 3) + 2
        x = pow(a, d, n)
        if x in (1, n - 1):
            continue
        for _ in range(r - 1):
            x = pow(x, 2, n)
            if x == n - 1:
                break
        else:
            return False
    return True


def _gen_prime(bits, e):
    while True:
        c = secrets.randbits(bits) | (1 << (bits - 1)) | (1 << (bits - 2)) | 1      # top two bits set so that p*q has full length
        if (c - 1) % e != 0 and _is_prime(c):
            return c


class RsaKey:
    def __init__(self, n, e, d, p, q):
        self.n, self.e, self.d, self.p, self.q = n, e, d, p, q

    @staticmethod
    def generate(bits=2048, e=65537):
        while True:
            p, q = _gen_prime(bits // 2, e), _gen_prime(bits // 2, e)
            if p == q:
                continue
            n = p * q
            if n.bit_length() != bits:
                continue
            lam = (p - 1) * (q - 1) // _gcd(p - 1, q - 1)
            return RsaKey(n, e, pow(e, -1, lam), p, q)

    @property
    def size(self):
        return (self.n.bit_length() + 7) // 8

    def sign_sha256(self, message):
        k = self.size
        t = SHA256_DIGESTINFO + hashlib.sha256(message).digest()
        em = b'\x00\x01' + b'\xff' * (k - len(t) - 3) + b'\x00' + t
        return pow(int.from_bytes(em, 'big'), self.d, self.n).to_bytes(k, 'big')

    def spki(self):
        return der_seq(der_seq(der_oid(OID_RSA), der_null()), der_bitstring(der_seq(der_int(self.n), der_int(self.e))))

    def to_pem(self):
        d, p, q = self.d, self.p, self.q
        der = der_seq(der_int(0), der_int(self.n), der_int(self.e), der_int(d), der_int(p), der_int(q), der_int(d % (p - 1)), der_int(d % (q - 1)), der_int(pow(q, -1, p)))
        return pem('RSA PRIVATE KEY', der)

    @staticmethod
    def from_pem(text):
        v = parse_seq_ints(pem_decode(text, 'RSA PRIVATE KEY'))
        key = RsaKey(v[1], v[2], v[3], v[4], v[5])
        if key.p * key.q != key.n or pow(pow(12345, key.e, key.n), key.d, key.n) != 12345:
            raise ValueError('damaged RSA key')
        return key


def _gcd(a, b):
    while b:
        a, b = b, a % b
    return a


def verify_sha256(spki_der, message, signature):
    """Verify an RSASSA-PKCS1-v1_5 / SHA-256 signature against a SubjectPublicKeyInfo."""
    tag, s, e = parse_tlv(spki_der)
    _, s2, e2 = parse_tlv(spki_der, s)                      # AlgorithmIdentifier
    t3, s3, e3 = parse_tlv(spki_der, e2)                    # BIT STRING
    n, ee = parse_seq_ints(spki_der[s3 + 1:e3])
    k = (n.bit_length() + 7) // 8
    if len(signature) != k:
        return False
    em = pow(int.from_bytes(signature, 'big'), ee, n).to_bytes(k, 'big')
    t = SHA256_DIGESTINFO + hashlib.sha256(message).digest()
    return em == b'\x00\x01' + b'\xff' * (k - len(t) - 3) + b'\x00' + t

# ---------------------------------------------------------------------------------------------------------------- PEM / X.509


def pem(label, der):
    b = base64.b64encode(der).decode()
    return '-----BEGIN %s-----\n%s\n-----END %s-----\n' % (label, '\n'.join(b[i:i + 64] for i in range(0, len(b), 64)), label)


def pem_decode(text, label):
    a = '-----BEGIN %s-----' % label
    b = '-----END %s-----' % label
    i, j = text.index(a) + len(a), text.index(b)
    return base64.b64decode(''.join(text[i:j].split()))


def _name(cn, org):
    return der_seq(der_set(der_seq(der_oid(OID_O), der_utf8(org))), der_set(der_seq(der_oid(OID_CN), der_utf8(cn))))


def self_signed_cert(key, cn='RM8 personal APK key', org='RM8 Port (local, personal use)', not_before=None, years=25):
    """DER of a self-signed v3 certificate (no extensions), sha256WithRSAEncryption."""
    nb = not_before or datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)
    na = datetime.datetime(min(nb.year + years, 2049), 12, 31, 23, 59, 59)          # UTCTime reaches 2049
    alg = der_seq(der_oid(OID_SHA256_RSA), der_null())
    tbs = der_seq(tlv(0xa0, der_int(2)), der_int(secrets.randbits(63) | 1), alg, _name(cn, org),
                  der_seq(der_utctime(nb), der_utctime(na)), _name(cn, org), key.spki())
    return der_seq(tbs, alg, der_bitstring(key.sign_sha256(tbs)))


def cert_spki(cert_der):
    """SubjectPublicKeyInfo DER taken out of a certificate."""
    _, s, e = parse_tlv(cert_der)
    _, ts, te = parse_tlv(cert_der, s)                      # TBSCertificate
    p = ts
    t, bs, be = parse_tlv(cert_der, p)
    if t == 0xa0:                                           # explicit version
        p = be
    for _ in range(5):                                      # serial, sigalg, issuer, validity, subject
        t, bs, be = parse_tlv(cert_der, p)
        p = be
    t, bs, be = parse_tlv(cert_der, p)
    return cert_der[p:be]


def fingerprint(cert_der):
    h = hashlib.sha256(cert_der).hexdigest()
    return ':'.join(h[i:i + 2] for i in range(0, len(h), 2)).upper()
