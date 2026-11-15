"""Read-access audit for the extraction: records every file opened for reading and flags reads outside the allowed folders.

Why: several exporters were written against the dev machine (e.g. scripts/prteff.py has a hard-coded game path, others read out/ intermediates).
The extractor patches / feeds all of that explicitly; this hook proves nothing else was read (and lists the game files that really are inputs).
"""
import os
import sys

_state = {'prefixes': [], 'game': '', 'reads': set(), 'foreign': set(), 'installed': False}


def _norm(p):
    return os.path.normcase(os.path.abspath(p)).rstrip(os.sep + '/')    # drive root 'R:' + sep -> 'r:'


def install(game_dir, allowed_dirs):
    _state['game'] = _norm(game_dir)
    _state['prefixes'] = [_state['game']] + [_norm(d) for d in allowed_dirs if d]
    _state['prefixes'] += [_norm(sys.prefix), _norm(sys.base_prefix), _norm(sys.exec_prefix)]
    _state['prefixes'].append(_norm(sys.executable))        # a frozen exe reads its own archive
    if hasattr(sys, '_MEIPASS'):
        _state['prefixes'].append(_norm(sys._MEIPASS))
    if not _state['installed']:
        sys.addaudithook(_hook)
        _state['installed'] = True


def _hook(event, args):
    if event != 'open' or not _state['prefixes']:
        return
    path, mode = args[0], args[1]
    if not isinstance(path, (str, bytes, os.PathLike)) or isinstance(path, int):
        return
    if mode is not None and isinstance(mode, str) and any(c in mode for c in 'wax+') and 'r' not in mode:
        return
    try:
        p = _norm(os.fsdecode(path))
    except Exception:
        return
    g = _state['game']
    if p == g or p.startswith(g + os.sep):
        _state['reads'].add(os.path.relpath(p, g).replace('\\', '/'))
        return
    for pre in _state['prefixes']:
        if p == pre or p.startswith(pre + os.sep):
            return
    _state['foreign'].add(p)


def snapshot():
    return sorted(_state['reads']), sorted(_state['foreign'])


def merge(reads, foreign):
    _state['reads'].update(reads)
    _state['foreign'].update(foreign)
