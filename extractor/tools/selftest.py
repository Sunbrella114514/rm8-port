"""End-to-end self test of rm8_extract on a real install (runs the CLI as a user would, checks exit codes, messages and side effects).

  python -I tools\\selftest.py --game <install> --work <empty scratch folder> [--exe path\\to\\rm8_extract.exe]
It copies the install once (about 1.2 GB) into <work>\\fake to test the 'modified install' path; the real install is only read."""
import argparse, os, shutil, subprocess, sys, json, hashlib

HERE = os.path.dirname(os.path.abspath(__file__))
EXT = os.path.dirname(HERE)
fails = []


def run(cmd, expect, must_have=(), must_not=(), stdin_text=None, env=None):
    kw = {'input': stdin_text} if stdin_text is not None else {'stdin': subprocess.DEVNULL}
    r = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace', env=env, **kw)
    out = r.stdout + r.stderr
    ok = r.returncode == expect and all(m in out for m in must_have) and not any(m in out for m in must_not)
    print('%s  rc=%d (want %d)  %s' % ('PASS' if ok else 'FAIL', r.returncode, expect, ' '.join(cmd[-4:])[:110]))
    if not ok:
        fails.append(cmd); print(out[-800:])
    return out


def tree_snapshot(d):
    out = {}
    for dp, _, fs in os.walk(d):
        for f in fs:
            p = os.path.join(dp, f); out[p] = (os.path.getsize(p), os.path.getmtime(p))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--exe')
    a = ap.parse_args()
    if os.path.exists(a.work) and os.listdir(a.work):
        sys.exit('--work must be empty')
    os.makedirs(a.work, exist_ok=True)
    base = [a.exe] if a.exe else [sys.executable, '-I', os.path.join(EXT, 'rm8_extract.py')]
    flags = ['--no-video', '--no-copy-game', '--quick']
    out1 = os.path.join(a.work, 'out1')

    fake = os.path.join(a.work, 'fake')       # scratch copy: every test that could write near an install uses it, never the real one
    print('copying the install ...'); shutil.copytree(a.game, fake)
    # 3: output inside the game folder / equal to it
    run(base + [fake, os.path.join(fake, 'extract_out')] + flags, 4, ['不能与游戏文件夹相同'])
    assert not os.path.exists(os.path.join(fake, 'extract_out')), 'must not write into the game folder'
    # 1-2: wrong folders
    run(base + [os.path.join(EXT, 'data'), os.path.join(a.work, 'o_wrong')] + flags, 3, ['这个文件夹里没有找到《大富翁8》'])
    run(base + [os.path.dirname(a.game), os.path.join(a.work, 'o_parent')] + flags, 3, ['这个文件夹里没有找到《大富翁8》'])
    run(base + [os.path.join(a.work, 'nonexistent'), os.path.join(a.work, 'o_none')] + flags, 3, ['找不到游戏文件夹'])
    assert not os.path.exists(os.path.join(a.work, 'o_wrong')), 'a failed run must not create the output folder'
    # 4: output folder that is not empty and not ours: untouched
    stray = os.path.join(a.work, 'stray'); os.makedirs(stray); open(os.path.join(stray, 'mine.txt'), 'w').write('x')
    before = tree_snapshot(stray)
    run(base + [a.game, stray] + flags, 4, ['输出文件夹不是空的'])
    assert tree_snapshot(stray) == before, 'a refused run must not touch the folder'
    # 5: normal run, then update = nothing to do, then verify
    run(base + [a.game, out1] + flags, 0, ['完成'])
    m1 = os.path.getmtime(os.path.join(out1, 'manifest.json'))
    run(base + [a.game, out1] + flags, 0, ['无需重新提取'])
    assert os.path.getmtime(os.path.join(out1, 'manifest.json')) == m1, 'manifest rewritten on a no-op update'
    run(base + ['--verify', out1], 0, ['检查通过'])
    run(base + ['--verify', out1, '--full'], 0, ['检查通过'])
    # user files next to the data survive an update
    open(os.path.join(out1, 'rm8_gamedate.bin'), 'w').write('profile')
    victim = os.path.join(out1, 'scenes', 'roles.txt')
    os.remove(victim)
    run(base + ['--verify', out1], 4, ['缺失或损坏'])
    run(base + [a.game, out1] + flags, 0, ['已有输出不完整', '完成'])
    assert os.path.exists(victim) and open(os.path.join(out1, 'rm8_gamedate.bin')).read() == 'profile', 'update lost a file'
    run(base + [a.game, out1] + flags + ['--rebuild'], 0, ['完成'])
    run(base + ['--verify', out1, '--full'], 0, ['检查通过'])
    # 6: a modified install: refused, accepted with --allow-unknown
    card = next(os.path.join(dp, f) for dp, _, fs in os.walk(fake) for f in fs if f.lower() == 'card.bin')
    orig = open(card, 'rb').read()
    d = bytearray(orig); d[-3] ^= 1; open(card, 'wb').write(bytes(d))
    run(base + [fake, os.path.join(a.work, 'o_fake')] + flags, 3, ['与受支持的版本不一致', 'card.bin'])
    assert not os.path.exists(os.path.join(a.work, 'o_fake', 'scenes')), 'refused install must not produce data'
    # a damaged table that the exporter cannot parse: a clean error and nothing half-written is left in place
    run(base + [fake, os.path.join(a.work, 'o_fake_bad')] + flags + ['--allow-unknown'], 4, ['导出步骤“卡片”失败'])
    assert not os.path.exists(os.path.join(a.work, 'o_fake_bad', 'scenes')), 'failed run must not leave scenes behind'
    open(card, 'wb').write(orig)
    fest = next(os.path.join(dp, f) for dp, _, fs in os.walk(fake) for f in fs if f.lower() == 'festival.bin')
    d = bytearray(open(fest, 'rb').read()); d[-3] ^= 1; open(fest, 'wb').write(bytes(d))        # a date digit: still parses
    run(base + [fake, os.path.join(a.work, 'o_fake2')] + flags + ['--allow-unknown'], 0, ['未识别的游戏版本'])
    # 6b: saves migration (--saves): copies the record + profile into the data folder, never touches the game folder, never overwrites a different file without asking
    fake_before = tree_snapshot(fake)
    ud = os.path.join(a.work, 'ud')
    out_s = run(base + [fake, os.path.join(a.work, 'o_saves')] + flags + ['--allow-unknown', '--saves', '--saves-dir', ud], 0, ['迁移存档', '电脑版：复制'])
    assert os.path.exists(os.path.join(ud, 'rm8_gamedate.bin')), 'profile not migrated'
    assert any(f.startswith('record_') for f in os.listdir(os.path.join(ud, 'save'))), 'no record migrated'
    assert not any(f.lower() == 'record_temp.sav' for f in os.listdir(os.path.join(ud, 'save'))), 'the autosave must not be migrated'
    rec = os.path.join(ud, 'save', next(f for f in os.listdir(os.path.join(ud, 'save')) if f.startswith('record_')))
    open(rec, 'wb').write(b'newer progress made in the port')
    run(base + ['--build', os.path.join(a.work, 'o_saves'), '--target', 'none', '--saves', '--saves-from', fake, '--saves-dir', ud, '--no-install-tools'], 0, ['保留不动'])
    assert open(rec, 'rb').read() == b'newer progress made in the port', 'a different port save was overwritten'
    run(base + [fake, os.path.join(a.work, 'o_nosaves')] + flags + ['--allow-unknown'], 0, [], ['迁移存档'])       # off by default when not interactive
    for k, v in fake_before.items():
        assert tree_snapshot(fake).get(k) == v, 'game folder file changed: %s' % k
    # 6c: the double-click flow (no arguments): typed answers.  A wrong folder is explained / corrected, a folder that is not ours is refused,
    #     and "no" at the final question creates nothing.
    def answers(*lines):
        return '\n'.join(lines) + '\n'
    quick = ['--no-video', '--no-copy-game', '--quick']
    wiz_out = os.path.join(a.work, 'wiz_out')
    run(base + quick, 2, ['这个文件夹里没有直接找到游戏', '这个文件夹不是《大富翁8》的安装文件夹', '找不到这个文件夹', '输出文件夹不是空的', '已取消，什么都没有做'],
        stdin_text=answers(os.path.dirname(a.game), 'n', os.path.join(a.work, 'nonexistent'), a.game, stray, wiz_out, '4', 'n', 'n'))
    assert not os.path.exists(wiz_out), 'a declined wizard must not create the output folder'
    out_w = run(base + quick + ['--no-saves'], 0, ['第 1 步 / 4', '请确认', '全部完成', '输出文件夹：'], stdin_text=answers(a.game, wiz_out, '4', 'y'))
    assert os.path.exists(os.path.join(wiz_out, 'manifest.json')) and os.path.exists(os.path.join(wiz_out, '结果说明.txt')), 'wizard run: manifest / summary file missing'
    # an unsupported (modified) install: the wizard explains and asks; "n" stops with exit code 3, "y" goes on
    run(base + quick + ['--no-saves'], 3, ['与受支持的版本不一致', '仍然要试试吗'], stdin_text=answers(fake, os.path.join(a.work, 'o_wiz_fake_n'), '4', 'y', 'n'))
    assert not os.path.exists(os.path.join(a.work, 'o_wiz_fake_n', 'scenes')), 'declined unsupported install must not produce data'
    run(base + quick + ['--no-saves'], 0, ['仍然要试试吗', '按你的选择继续', '全部完成'], stdin_text=answers(fake, os.path.join(a.work, 'o_wiz_fake_y'), '4', 'y', 'y'))
    # a closed stdin in the middle of the questions is a clean cancel, not a traceback
    run(base + quick, 2, ['已取消'], ['Traceback'], stdin_text=answers(a.game))
    # 6d: no network (a proxy that nobody listens on): a plain message and exit code 6, nothing downloaded
    src_root = os.path.dirname(EXT)
    if os.path.isfile(os.path.join(src_root, 'port', 'CMakeLists.txt')):
        o_net = os.path.join(a.work, 'o_net')
        run(base + [a.game, o_net, '--link-game', '--no-video', '--quick'], 0, ['完成'])
        env = dict(os.environ, HTTPS_PROXY='http://127.0.0.1:9', HTTP_PROXY='http://127.0.0.1:9', https_proxy='http://127.0.0.1:9', http_proxy='http://127.0.0.1:9')
        env.pop('NO_PROXY', None); env.pop('no_proxy', None)
        tcd = os.path.join(a.work, 'toolchain_empty')
        run(base + ['--build', o_net, '--target', 'win', '--source', src_root, '--toolchain-dir', tcd, '--work-dir', os.path.join(a.work, 'work_net')], 6,
            ['无法连接网络', '构建工具'], ['Traceback'], env=env)
        assert not os.path.exists(os.path.join(tcd, 'cmake')), 'nothing may be installed when the network is down'
    # 6e: no disk space (simulated in this process) -> a plain message and exit code 4, nothing written
    sys.path.insert(0, EXT)
    import collections, contextlib, errno, io
    import rm8_extract
    from rm8x import friendly
    real_usage = shutil.disk_usage
    shutil.disk_usage = lambda p: collections.namedtuple('usage', 'total used free')(100 << 20, 99 << 20, 1 << 20)
    buf = io.StringIO()
    try:
        with contextlib.redirect_stdout(buf):
            rc = rm8_extract.main([a.game, os.path.join(a.work, 'o_full'), '--no-video', '--no-copy-game', '--quick'])
    finally:
        shutil.disk_usage = real_usage
    ok = rc == 4 and '磁盘空间不够' in buf.getvalue() and not os.path.exists(os.path.join(a.work, 'o_full', 'scenes'))
    print('%s  disk full (simulated): rc=%d' % ('PASS' if ok else 'FAIL', rc))
    if not ok:
        fails.append('disk-full'); print(buf.getvalue()[-600:])
    msg = friendly.os_error_message(OSError(errno.ENOSPC, 'No space left on device'))
    ok = '磁盘空间不够' in msg and 'No space' not in msg
    print('%s  ENOSPC message: %s' % ('PASS' if ok else 'FAIL', msg[:40]))
    if not ok:
        fails.append('enospc')
    # 7: a missing file
    pk = next(os.path.join(dp, f) for dp, _, fs in os.walk(fake) for f in fs if f.lower() == 'tex0001.dat')
    os.remove(pk)
    run(base + [fake, os.path.join(a.work, 'o_fake3')] + flags, 3, ['游戏文件不完整', 'tex0001.dat'])
    print('\n%s' % ('ALL PASS' if not fails else '%d FAILED' % len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
