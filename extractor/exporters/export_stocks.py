"""Export the stock list for the port: port/scenes/stocks.txt
One line per stock section of Config/stock.bin, in file order (Stock_LoadFromINI @0x4b8e20 walks the sections in order):
  stock <TAB> c <TAB> id <TAB> initprice <TAB> totalnum <TAB> vivacity <TAB> name
c = map id (1..11) or 100 (every map); -1 power company and 0 ticket centre are not on the exchange (never picked by the loader).
id = the object's +0x374 flag: 1 = never delisted (Stock_ResetStock @0x4bc330 returns early).
Numbers stay as the file's text (the game reads initprice / vivacity with sscanf "%f", totalnum / c / id with Ini_GetInteger).
usage: export_stocks.py <game_dir> <out_file>"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_scene as es

game, out = sys.argv[1], sys.argv[2]
lines = []
for name, sec in es.read_cfg(game, 'stock.bin'):
    f = [sec.get('c', '0').strip(), sec.get('id', '0').strip(), sec.get('initprice', '100').strip(), sec.get('totalnum', '10000').strip(),
         sec.get('vivacity', '1').strip(), name]
    assert all('\t' not in x and '\n' not in x for x in f), f
    lines.append('stock\t' + '\t'.join(f))
open(out, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
print('%d stocks -> %s' % (len(lines), out))
