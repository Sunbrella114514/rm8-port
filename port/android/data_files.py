"""The data files the Android app needs (the same list android/deploy.py pushes), as (local path, path relative to the app data dir).
Used by build_apk.py --with-data to bundle them into the APK (assets/rm8data/...), unpacked by the app on its first start."""
import os, glob

SHARED = ('roles.txt', 'particles.txt', 'zh_t2s.txt', 'cards.txt', 'stages.txt', 'stocks.txt', 'npcs.txt', 'event_arts.txt',
          'questions.txt', 'story.txt', 'movie_arts.txt', 'news.txt', 'introduce.txt', 'save_tables.txt')


def data_files(game, scenes):
    def g(*p):	# the game folder may use other letter case (the Steam version: lower case folders)
        path = game
        for part in p:
            hit = next((e for e in os.listdir(path) if e.lower() == part.lower()), part) if os.path.isdir(path) else part
            path = os.path.join(path, hit)
        return path
    out = [(g('Clump', 'clump0000.dat'), 'game/Clump/clump0000.dat')]
    for f in sorted(os.listdir(g('Pysrc'))):
        if f.lower().endswith('.pyc'): out.append((os.path.join(g('Pysrc'), f), 'game/Pysrc/' + f))
    for t in ('tex0000.dat', 'tex0001.dat'): out.append((g('Texture', t), 'game/Texture/' + t))
    out.append((g('Sound', 'snd0000.dat'), 'game/Sound/snd0000.dat'))
    out.append((g('Music', 'mus0000.dat'), 'game/Music/mus0000.dat'))
    out.append((g('Config', 'init.txt'), 'game/Config/init.txt'))
    for sd in sorted(glob.glob(os.path.join(scenes, '*', 'scene.txt'))):
        d = os.path.dirname(sd); sid = os.path.basename(d)
        for f in sorted(os.listdir(d)):
            if f in ('scene.txt', 'wonders.txt') or (f.startswith(('lm_', 'alm_')) and f.endswith('.png')): out.append((os.path.join(d, f), 'scenes/%s/%s' % (sid, f)))
    for sub in ('ui', 'video', 'minigame'):
        sdir = os.path.join(scenes, sub)
        if os.path.isdir(sdir):
            for f in sorted(os.listdir(sdir)): out.append((os.path.join(sdir, f), 'scenes/%s/%s' % (sub, f)))
    for f in SHARED:
        p = os.path.join(scenes, f)
        if os.path.exists(p): out.append((p, 'scenes/' + f))
    missing = [l for l, _ in out if not os.path.isfile(l)]
    if missing: raise SystemExit('missing data files: ' + ', '.join(missing[:5]))
    return out
