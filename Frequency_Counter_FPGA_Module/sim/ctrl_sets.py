#!/usr/bin/env python3
"""Rozbor syntetizovaneho netlistu Gowin (.vg): ridici sady registru (CLK, CE, SR) a ALU podle jmena.
Dva FF v jedne CLS musi sdilet CLK/CE/SR -> registry s unikatni ridici sadou nejdou parovat a zabiraji
CLS s jednim FF. Pouziti: ctrlsets.py Counter_FPGA.vg"""
import re, sys, collections

t = open(sys.argv[1], encoding='utf-8', errors='ignore').read()


def base(name):
    name = name.lstrip('\\')
    name = re.sub(r'_s\d*$', '', name)
    name = re.sub(r'\[\d+\]', '', name)
    name = re.sub(r'_\d+$', '', name)
    name = re.sub(r'_\d+$', '', name)
    return name


# instance: TYP JMENO ( .PORT(net), ... );
inst = re.finditer(r'^\s*(\w+)\s+(\\?\S+)\s*\((.*?)\);', t, re.M | re.S)
ff_sets = collections.Counter()
ff_types = collections.Counter()
ff_by_set = collections.defaultdict(collections.Counter)
alu = collections.Counter()
alu_mode = collections.Counter()
nff = 0
for m in inst:
    typ, name, body = m.group(1), m.group(2), m.group(3)
    ports = dict(re.findall(r'\.(\w+)\(([^()]*)\)', body))
    if typ.startswith('DFF'):
        nff += 1
        ff_types[typ] += 1
        clk = ports.get('CLK', '?').strip()
        ce = ports.get('CE', '1').strip()
        sr = next((ports[k].strip() for k in ('RESET', 'SET', 'CLEAR', 'PRESET') if k in ports), '0')
        key = (clk, ce, sr, typ)
        ff_sets[key] += 1
        ff_by_set[key][base(name)] += 1
    elif typ == 'ALU':
        alu[base(name)] += 1

print('FF celkem', nff, '| ridicich sad (CLK, CE, SR, typ):', len(ff_sets))
odd = sum(v % 2 for v in ff_sets.values())
singles = sum(1 for v in ff_sets.values() if v == 1)
print('sad s 1 FF:', singles, '| lichych sad (aspon 1 FF bez paru):', odd)
print('FF typy:', ', '.join('%s %d' % kv for kv in ff_types.most_common()))
print('\nNejvetsi ridici sady:')
for k, v in ff_sets.most_common(12):
    top = ', '.join('%s %d' % kv for kv in ff_by_set[k].most_common(3))
    print('  %5d  CE=%-22s SR=%-18s %-6s  [%s]' % (v, k[1][:22], k[2][:18], k[3], top))
print('\nSady do 3 FF (kandidati na sjednoceni CE/SR):', sum(1 for v in ff_sets.values() if v <= 3),
      'sad,', sum(v for v in ff_sets.values() if v <= 3), 'FF')
print('\nALU podle jmena (celkem %d):' % sum(alu.values()))
for k, v in alu.most_common(25):
    print('  %5d  %s' % (v, k))
