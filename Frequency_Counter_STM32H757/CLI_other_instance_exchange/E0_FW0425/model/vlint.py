import re, sys
src = {f: open(f, encoding='utf-8').read() for f in ['top.v', 'tdc.v', 'spi_app.v', 'spi_slave_phy.v']}
def strip(s):
    s = re.sub(r'//[^\n]*', '', s); s = re.sub(r'/\*.*?\*/', '', s, flags=re.S); return s
allsrc = strip('\n'.join(src.values()))
mods = {}
for m in re.finditer(r'\bmodule\s+(\w+)\s*(#\s*\((.*?)\))?\s*\((.*?)\);', allsrc, re.S):
    name, plist = m.group(1), m.group(4)
    ports = {}
    for d in re.finditer(r'\b(input|output|inout)\b\s*(wire|reg)?\s*(\[[^\]]+\])?\s*([\w\s,]+?)(?=,\s*(?:input|output|inout)\b|$)', plist.replace('\n',' ')+' ', re.S):
        for n in re.split(r'\s*,\s*', d.group(4).strip()):
            n = n.strip()
            if n: ports[n] = d.group(1)
    mods[name] = ports
errs = 0
top = strip(src['top.v'])
for inst in re.finditer(r'\b(tdc_chan|win_recip|spi_app|coh_mon|ref_guard|pps_cap|ring_osc|spi_slave_phy)\s*(#\s*\((?:[^()]|\([^()]*\))*\))?\s*(\w+)\s*\((.*?)\);', top, re.S):
    mod, iname, body = inst.group(1), inst.group(3), inst.group(4)
    conns = re.findall(r'\.(\w+)\s*\(', body)
    ports = mods.get(mod, {})
    for c in conns:
        if c not in ports: print(f"CHYBA {iname}: port .{c} neexistuje v {mod}"); errs += 1
    for p, d in ports.items():
        if d == 'input' and p not in conns: print(f"CHYBA {iname}: vstup {p} nezapojen"); errs += 1
    dup = {c for c in conns if conns.count(c) > 1}
    if dup: print(f"CHYBA {iname}: duplicitni {dup}"); errs += 1
    print(f"{iname:8s} ({mod}): {len(conns)} spojeni, {len(ports)} portu")
# nove signaly v top.v: deklarace pred pouzitim
for sig in ['bub_a','bub_b','mask_cs_en','led_hb_en','coh_st','coh_cnt','csa_s','mk_s','alias_lim','r_bub_a','r_ovl_a','bub_p','ovl_ps','tgl10','coh_gray','ok_s','boot_done','base_win']:
    d = re.search(r'\b(wire|reg)\b[^;]*\b%s\b' % sig, top)
    u = re.search(r'\b%s\b' % sig, top)
    if not d: print("CHYBA: nedeklarovano", sig); errs += 1
    elif u.start() < d.start(): print("POZOR: pouzito pred deklaraci", sig); errs += 1
print("lint chyb:", errs)
sys.exit(1 if errs else 0)
