"""Compare two game installs file by file (case-insensitive names): identical / different (size or content) / only on one side.
usage: python -I compare_installs.py <install A> <install B>      (read-only; hashes every file once)"""
import hashlib, os, sys


def walk(root):
    out = {}
    for dp, _, fs in os.walk(root):
        for f in fs:
            p = os.path.join(dp, f)
            rel = os.path.relpath(p, root).replace('\\', '/')
            out[rel.lower()] = (rel, p, os.path.getsize(p))
    return out


def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 22), b''):
            h.update(b)
    return h.hexdigest()


def main():
    A, B = sys.argv[1], sys.argv[2]
    a, b = walk(A), walk(B)
    only_a = sorted(k for k in a if k not in b)
    only_b = sorted(k for k in b if k not in a)
    same, diff = [], []
    for k in sorted(set(a) & set(b)):
        if a[k][2] != b[k][2]:
            diff.append((k, a[k][2], b[k][2], 'size'))
        elif sha(a[k][1]) == sha(b[k][1]):
            same.append(k)
        else:
            diff.append((k, a[k][2], b[k][2], 'content'))
    print('files A %d B %d; identical %d; different %d; only A %d; only B %d' % (len(a), len(b), len(same), len(diff), len(only_a), len(only_b)))
    print('--- different')
    for k, sa, sb, why in diff:
        print('%-45s %10d %10d %s' % (k, sa, sb, why))
    print('--- only in A')
    for k in only_a: print(' ', a[k][0], a[k][2])
    print('--- only in B')
    for k in only_b: print(' ', b[k][0], b[k][2])


if __name__ == '__main__':
    main()
