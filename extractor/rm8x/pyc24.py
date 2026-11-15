"""Minimal reader for CPython 2.4 .pyc files (marshal format) -- just enough to list Movie.ArtCreate(<id>, ...) calls
of the cutscene scripts (Pysrc\\daNN_N.pyc) without a Python 2 interpreter or a decompiler.

The cutscene scripts are straight-line Movie.* / CTime.* call sequences, so the first argument of Movie.ArtCreate is always a
LOAD_CONST right after LOAD_ATTR ArtCreate.  Anything that does not fit this pattern raises an error (never guessed).
"""
import struct

MAGIC_24 = b'\x6d\xf2\x0d\x0a'      # CPython 2.4 (62061)
LOAD_CONST, LOAD_NAME, LOAD_GLOBAL, LOAD_ATTR = 100, 101, 116, 105
HAVE_ARGUMENT = 90
EXTENDED_ARG = 143


class Code:
    def __init__(self, code, consts, names, name):
        self.code, self.consts, self.names, self.name = code, consts, names, name


class _Reader:
    def __init__(self, data):
        self.d, self.p, self.interned = data, 0, []

    def take(self, n):
        v = self.d[self.p:self.p + n]
        if len(v) != n:
            raise ValueError('truncated marshal data')
        self.p += n
        return v

    def u32(self):
        return struct.unpack('<I', self.take(4))[0]

    def obj(self):
        t = self.take(1)
        if t == b'0': return None
        if t == b'N': return None
        if t == b'T': return True
        if t == b'F': return False
        if t == b'i': return struct.unpack('<i', self.take(4))[0]
        if t == b'l':
            n = struct.unpack('<i', self.take(4))[0]
            digits = [struct.unpack('<H', self.take(2))[0] for _ in range(abs(n))]
            v = sum(dg << (15 * i) for i, dg in enumerate(digits))
            return -v if n < 0 else v
        if t == b'f': return float(self.take(self.take(1)[0]))
        if t == b'g': return struct.unpack('<d', self.take(8))[0]
        if t == b'x': return complex(float(self.take(self.take(1)[0])), float(self.take(self.take(1)[0])))
        if t == b'y': return complex(*struct.unpack('<dd', self.take(16)))
        if t == b's': return self.take(self.u32())
        if t == b't':
            v = self.take(self.u32()); self.interned.append(v); return v
        if t == b'R': return self.interned[self.u32()]
        if t == b'u': return self.take(self.u32()).decode('utf-8', 'replace')
        if t in (b'(', b'['): return tuple(self.obj() for _ in range(self.u32()))
        if t == b'{':
            out = {}
            while True:
                k = self.obj_or_null()
                if k is _NULL: return out
                out[k] = self.obj()
        if t == b'c':
            self.u32(); self.u32(); self.u32(); self.u32()   # argcount nlocals stacksize flags
            code = self.obj(); consts = self.obj(); names = self.obj()
            self.obj(); self.obj(); self.obj()                # varnames freevars cellvars
            self.obj(); self.take(0)                          # filename
            name = self.obj(); self.u32(); self.obj()         # name, firstlineno, lnotab
            return Code(code, consts, names, name)
        raise ValueError('unsupported marshal type %r at %d' % (t, self.p - 1))

    def obj_or_null(self):
        if self.d[self.p:self.p + 1] == b'0':
            self.p += 1
            return _NULL
        return self.obj()


_NULL = object()


def load_pyc(data):
    if data[:4] != MAGIC_24:
        raise ValueError('not a CPython 2.4 pyc (magic %r)' % data[:4])
    r = _Reader(data); r.p = 8
    c = r.obj()
    if not isinstance(c, Code):
        raise ValueError('no code object')
    return c


def _walk(c):
    yield c
    for k in c.consts:
        if isinstance(k, Code):
            yield from _walk(k)


def art_create_ids(data):
    """Art ids passed as first argument to Movie.ArtCreate in one pyc."""
    ids = []
    for c in _walk(load_pyc(data)):
        code, i, n = c.code, 0, len(c.code)
        ops = []                                              # (op, arg) with EXTENDED_ARG folded
        ext = 0
        while i < n:
            op = code[i]; i += 1
            if op >= HAVE_ARGUMENT:
                arg = code[i] | (code[i + 1] << 8) | ext; i += 2
                if op == EXTENDED_ARG: ext = arg << 16; continue
                ext = 0
            else:
                arg = None
            ops.append((op, arg))
        for k, (op, arg) in enumerate(ops):
            if op == LOAD_ATTR and c.names[arg] == b'ArtCreate':
                if k == 0 or ops[k - 1][0] not in (LOAD_NAME, LOAD_GLOBAL) or c.names[ops[k - 1][1]] != b'Movie':
                    raise ValueError('ArtCreate is not Movie.ArtCreate in %r' % c.name)
                nop, narg = ops[k + 1]
                if nop != LOAD_CONST or not isinstance(c.consts[narg], int):
                    raise ValueError('Movie.ArtCreate with a non-constant art id in %r' % c.name)
                ids.append(c.consts[narg])
    return ids
