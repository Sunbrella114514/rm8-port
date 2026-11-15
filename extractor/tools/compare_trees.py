"""Compare two scenes folders: byte-identical files, differing files (PNGs: decoded pixels), files only on one side.
usage: python -I compare_trees.py <A scenes dir> <B scenes dir> [--ignore name ...]"""
import hashlib, io, os, sys


def walk(root):
    out = {}
    for dp, _, fs in os.walk(root):
        for f in fs:
            p = os.path.join(dp, f)
            out[os.path.relpath(p, root).replace('\\', '/')] = p
    return out


def sha(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()


def pix(p):
    from PIL import Image
    im = Image.open(p)
    return im.mode, im.size, hashlib.sha256(im.tobytes()).hexdigest()


def main():
    a, b = sys.argv[1], sys.argv[2]
    ignore = set()
    if '--ignore' in sys.argv:
        ignore = set(sys.argv[sys.argv.index('--ignore') + 1:])
    A, B = walk(a), walk(b)
    ign = lambda k: os.path.basename(k) in ignore
    onlyA = sorted(k for k in A if k not in B and not ign(k))
    onlyB = sorted(k for k in B if k not in A and not ign(k))
    same = diff = pixsame = 0
    bad = []
    for k in sorted(set(A) & set(B)):
        if ign(k):
            continue
        if sha(A[k]) == sha(B[k]):
            same += 1
        elif k.endswith('.png') and pix(A[k]) == pix(B[k]):
            pixsame += 1
        else:
            diff += 1; bad.append(k)
    print('identical bytes: %d, PNG identical pixels (bytes differ): %d, different: %d, only in A: %d, only in B: %d' % (same, pixsame, diff, len(onlyA), len(onlyB)))
    for k in bad[:40]: print('  DIFF', k)
    for k in onlyA[:20]: print('  only A', k)
    for k in onlyB[:20]: print('  only B', k)
    return 0 if not (diff or onlyA or onlyB) else 1


if __name__ == '__main__':
    sys.exit(main())
