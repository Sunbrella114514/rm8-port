"""Wonder sites of the maps: port/scenes/<map>/wonders.txt.
usage: export_wonders.py <game_dir> <out_scenes_dir>      (all maps with a Map\\<id>.map)

[V] Role_TriggerLandLevelEvent @0x4863f0 / Land_GetLevel @0x4f0600: a road tile whose plot word (map +0x44) has a low byte 4..9 starts the event 0x5A + (type - 4) / 2
    (0x5A money, 0x5B land, 0x5C card wonder) when nothing else is on the tile (Role_OnArriveNode @0x4861a0).
[V] BuildObj_CreateWonder @0x4f1820: each such plot becomes a CTerraObj (InitWonder @0x4ee5d0): wonder (type - 4) / 2, (type odd) + 3 = the footprint, position / yaw of the plot;
    the staged models are building.bin ids 5 / 6 / 7 level 1..4 (TerraObj_AdvanceStageModel @0x4ee700, Terrain_FindById(idx + 5)), the finished one is the stage's
    own wonder art (stage.bin "wonder=a,b,c", Container +0x204 + idx * 4).
Lines:
  wsite <wonder> <tile x> <tile y>                       a road tile that triggers it
  wplot <wonder> <type> <n> <x> <y> <z> <yaw> <tx> <ty>  the plot object (pos / angle of the placement, anchor tile)
  wfinal <wonder> <art id> <dff> <texdir> <anm|->        the finished building of this map
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_scene as es
import plots

game, outdir = sys.argv[1], sys.argv[2]
at = es.art_table(game)
stages = {}
for name, s in es.read_cfg(game, 'stage.bin'):
    mf = s.get('mapfile', '')
    stages[mf.lower()] = [int(v) for v in s.get('wonder', '-1,-1,-1').split(',')]
for mid in ['%02d' % i for i in range(1, 12)] + ['download201', 'download202', 'download203', 'download204']:
    p = os.path.join(game, 'Map', mid + '.map')
    if not os.path.exists(p): continue
    m = es.load_map(p)
    W, H = m['W'], m['H']; hgt = m['heights']
    road = [int(v) for v in m['road'].flatten()]
    roadmesh = [False] * (W * H)
    for y in range(H):
        for x in range(W):
            rc = es.route_code(m['road'], x, y)
            if rc and 0 < rc[1] < 100: roadmesh[y * W + x] = True
    grid = plots.Grid(W, H, road, m['levels'], hgt, roadmesh)
    words = [(int(m['plot'].flat[i]), int(m['anchor'].flat[i])) for i in range(W * H)]
    placed = grid.place_all(words)
    lines = []
    for i in range(W * H):
        t = int(m['plot'].flat[i]) & 0xFF
        if road[i] and 4 <= t <= 9: lines.append('wsite %d %d %d' % ((t - 4) >> 1, i % W, i // W))
    for q in placed:
        t = q['type']
        r = q['result']
        if not (4 <= t <= 9) or not r: continue
        ax, ay = q['anchor'] % W, q['anchor'] // W
        lines.append('wplot %d %d %d %g %g %g %g %d %d' % ((t - 4) >> 1, t, r['n'], r['pos'][0], r['pos'][1], r['pos'][2], r['angle'], ax, ay))
    fin = stages.get(mid + '.map')
    if fin:
        for idx, art in enumerate(fin):
            rec = at.get(art) if art >= 0 else None
            if rec: lines.append('wfinal %d %d %s %s %s' % (idx, art, rec[0].lower(), rec[1].lower(), rec[2].lower() if rec[2] != '-' else '-'))
    d = os.path.join(outdir, mid)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, 'wonders.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + ('\n' if lines else ''))
    print(mid, len([l for l in lines if l.startswith('wsite')]), 'sites', len([l for l in lines if l.startswith('wplot')]), 'plots', len([l for l in lines if l.startswith('wfinal')]), 'finals')
