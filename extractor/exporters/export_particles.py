"""Export the particle effects (PrtEffTable.tb + uvlisttable.tb) for the port: port/scenes/particles.txt.
usage: export_particles.py <out particles.txt>
Format (docs/particles.md): one line per effect
  eff <id> key value key value ...   (vector values comma-joined; tex = pack key of uvlist frame 0's texture)
  uv <id> <n> u0 v0 u1 v1 ms ...      (the uvlist frames of the effect)
  key <id> time time_bias r g b a rb gb bb ab   (color keys, color_key_count of them, in order)
Only kind 0 / simple_mode 0 effects exist (all 26)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prteff

effects, uvl, _ = prteff.load_tables()
out = []
j = lambda v: ','.join('%.9g' % x for x in v)
for eid in sorted(effects):
    e = effects[eid]
    assert e['simple_mode'] == 0 and e['shape'] in (0, 2, 3) and e['chain_effect'] == -1, eid
    rec = uvl[e['uvlist_id']]; fr = rec['frames']
    tex = rec['textures'][fr[0]['texture_index']]['path'] if rec['textures'] else '-'
    kv = [('shape', e['shape']), ('max', e['max_prt']), ('num', e['emit_num']), ('numb', e['emit_num_bias']),
          ('gap', e['emit_gap']), ('gapb', e['emit_gap_bias']), ('life', e['life']), ('lifeb', e['life_bias']),
          ('vel', e['vel']), ('velb', e['vel_bias']), ('dir', j(e['dir'])), ('dirb', j(e['dir_bias'])),
          ('force', j(e['force'])), ('emt', j(e['emt_size'])),
          ('ss', j(e['size_start'])), ('se', j(e['size_end'])), ('ssb', j(e['size_start_bias'])), ('seb', j(e['size_end_bias'])),
          ('hassize', int(e['has_size_prop'])), ('rs', e['rot_start']), ('rsb', e['rot_start_bias']), ('re', e['rot_end']),
          ('reb', e['rot_end_bias']), ('hasrot', int(e['has_rot_prop'])), ('src', e['src_blend']), ('dst', e['dst_blend']),
          ('valpha', e['vertex_alpha']), ('dur', e['duration']), ('sr', e['sphere_radius']), ('srg', e['sphere_radius_gap']),
          ('suse', e['sphere_use_emission'] & 0xFF), ('nkeys', e['color_key_count']), ('tex', 'prt\\' + tex)]
    out.append('eff %d %s' % (eid, ' '.join('%s %s' % (k, ('%.9g' % v) if isinstance(v, float) else v) for k, v in kv)))
    out.append('uv %d %d %s' % (eid, len(fr), ' '.join('%.9g %.9g %.9g %.9g %d' % (f['u0'], f['v0'], f['u1'], f['v1'], f['duration_ms']) for f in fr)))
    for k in e['color_keys_all'][:max(0, min(e['color_key_count'], 8))]:
        out.append('key %d %.9g %.9g %s %s' % (eid, k['time'], k['time_bias'], ' '.join(map(str, k['rgba'])), ' '.join(map(str, k['rgba_bias']))))
os.makedirs(os.path.dirname(os.path.abspath(sys.argv[1])), exist_ok=True)
open(sys.argv[1], 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
print('%d effects -> %s' % (len(effects), sys.argv[1]))
