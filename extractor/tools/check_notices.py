r"""Check that THIRD_PARTY_NOTICES.md lists every download the extractor / builder can make (and nothing else in its download list).

  python -I tools\check_notices.py [--root <tree with THIRD_PARTY_NOTICES.md, extractor\ and port\>]

The code side is read from the pins themselves: rm8x\toolchain.TOOLS (+ the license text URL), rm8x\ffmpeg_tool.PINNED, port\extern\fetch_deps.DEPS.
The document side is the section "所有会联网下载的东西" (every https:// address in it).  Exit status 1 on any difference."""
import argparse, importlib.util, os, re, sys

sys.dont_write_bytecode = True      # this script imports modules from the tree it checks: never leave __pycache__ there

HERE = os.path.dirname(os.path.abspath(__file__))
EXT = os.path.dirname(HERE)
ROOT = os.path.dirname(EXT)


def code_urls(root):
    sys.path.insert(0, os.path.join(root, 'extractor'))
    try:
        from rm8x import toolchain, ffmpeg_tool
    finally:
        sys.path.pop(0)
    urls = {t['url'] for t in toolchain.TOOLS.values()}
    urls.add(toolchain.REPO_XML)
    urls.add(ffmpeg_tool.PINNED['url'])
    spec = importlib.util.spec_from_file_location('fetch_deps_chk', os.path.join(root, 'port', 'extern', 'fetch_deps.py'))
    fd = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fd)
    for d in fd.DEPS:
        if d.get('url'):
            urls.add(d['url'])
        for f in d.get('files', []):
            urls.add(f[1])
    return urls


def doc_urls(root):
    text = open(os.path.join(root, 'THIRD_PARTY_NOTICES.md'), encoding='utf-8').read()
    a = text.index('## 所有会联网下载的东西')
    b = text.find('\n## ', a + 5)
    sec = text[a:b if b > 0 else len(text)]
    return {u.rstrip('.,;）') for u in re.findall(r'https?://[A-Za-z0-9._/%+~?=&#:-]+', sec)} - {'https://github.com/'}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=ROOT)
    a = ap.parse_args()
    code, doc = code_urls(a.root), doc_urls(a.root)
    miss = sorted(code - doc)
    extra = sorted(doc - code)
    for u in miss:
        print('NOT IN THIRD_PARTY_NOTICES.md:', u)
    for u in extra:
        print('IN THE NOTICES BUT NOT IN THE CODE:', u)
    print('downloads in code: %d, listed: %d' % (len(code), len(doc)))
    print('RESULT:', 'OK' if not (miss or extra) else 'MISMATCH')
    return 1 if (miss or extra) else 0


if __name__ == '__main__':
    sys.exit(main())
