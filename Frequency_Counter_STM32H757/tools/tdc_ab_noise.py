"""Merení sumu TDC: STEJNY signal na CH_A i CH_B (STATUS #272, krok 1 analyzy hodin).

Zapojeni: jeden zdroj (GPSDO / generator 10 MHz) rozdeleny T-kusem nebo splitterem na CH_A a CH_B, kabely stejne dlouhe.
Obe okna se uzaviraji TOUZ hranou po tiku hradla, takze chyba kazdeho kanalu se lisi jen sumem jejich TDC:
    d = (f_A - f_B) / f * T_gate  =  dt_A - dt_B   [s]      (N je stejne -> f_A/f_B = dt_B/dt_A)
    sigma_single = sigma(d) / sqrt(2)            (kdyz jsou kanaly nezavisle a stejne zasumene)

Co vysledek rika:
    sigma_single ~ 105 ps (jako sigma jedne znacky z dt_a)  -> sum je v kazdem TDC zvlast (vstup, rozmisteni, kalibrace)
    sigma_single <<  105 ps                                  -> sum je SPOLECNY obema kanalum: hodiny (Si5356, SSC?), napajeni,
                                                                zdroj nebo OCXO -- A/B test je od nich "slepy", protoze se
                                                                ve rozdilu zrusi. Dalsi krok: osciloskop na CLK_0 / REF_Test.
Okna, kde kanaly uzavreny ruznou hranou (N_A != N_B, rozdil ~ 1 perioda), se pozname jako odlehla a vyrazuji se (MAD).

Pouziti:
    python tools/tdc_ab_noise.py collect --windows 600 [--host 10.0.0.106] [--out ab.csv]
    python tools/tdc_ab_noise.py analyze ab.csv
    python tools/tdc_ab_noise.py selftest
"""
import argparse, json, math, statistics as st, sys, time, urllib.request


def collect(host, n, out):
    rows, last = [], None
    req = urllib.request.urlopen('http://%s/api/stream' % host, timeout=30)
    t_end = time.time() + n * 0.6 + 60
    while len(rows) < n and time.time() < t_end:
        line = req.readline().decode('utf-8', 'replace')
        if not line.startswith('data:'):
            continue
        try:
            d = json.loads(line[5:])
        except ValueError:
            continue
        fa, fb, seq = d.get('freq_hz'), d.get('freq16_hz'), d.get('seq_meas')
        if fa is None or fb is None or seq == last:
            continue
        last = seq
        rows.append((seq, fa, fb, d.get('gate_ns') or 250000000, d.get('channel')))
        if len(rows) % 100 == 0:
            print('  %d oken' % len(rows), flush=True)
    req.close()
    with open(out, 'w') as f:
        f.write('seq,f_primary,f_secondary,gate_ns,channel\n')
        for r in rows:
            f.write('%d,%.9f,%.9f,%d,%s\n' % r)
    print('ulozeno %d oken do %s' % (len(rows), out))


def robust_sigma(x):
    """sigma z MAD (odolna vuci odlehlym oknum s jinou uzaviraci hranou)."""
    m = st.median(x)
    mad = st.median([abs(v - m) for v in x])
    return 1.4826 * mad, m


def analyze_rows(rows):
    """rows: (seq, f1, f2, gate_ns). Vraci slovnik s vysledky; casy v ps."""
    d_ps = []
    for seq, f1, f2, g, *_ in rows:
        if f1 <= 0 or f2 <= 0:
            continue
        f = 0.5 * (f1 + f2)
        d_ps.append((f1 - f2) / f * g * 1e3)          # (rel. rozdil) * gate [ns] * 1000 = ps
    if len(d_ps) < 10:
        return None
    sig, med = robust_sigma(d_ps)
    # odlehla okna = jina uzaviraci hrana: |d| > 5 sigma NEBO > 20 ns (perioda signalu 100 ns pri 10 MHz)
    lim = max(5 * sig, 1.0)
    ok = [v for v in d_ps if abs(v - med) <= lim]
    return {
        'n': len(d_ps), 'outliers': len(d_ps) - len(ok), 'offset_ps': med,
        'sigma_diff_ps': st.stdev(ok) if len(ok) > 2 else float('nan'),
        'sigma_single_ps': (st.stdev(ok) / math.sqrt(2)) if len(ok) > 2 else float('nan'),
        'sigma_mad_ps': sig,
    }


def analyze(path):
    rows = []
    with open(path) as f:
        next(f)
        for l in f:
            p = l.strip().split(',')
            rows.append((int(p[0]), float(p[1]), float(p[2]), int(p[3])))
    r = analyze_rows(rows)
    if r is None:
        print('malo dat'); return 1
    print('oken %d, odlehlych %d (jina uzaviraci hrana)' % (r['n'], r['outliers']))
    print('konstantni offset A-B (zpozdeni kabelu + kanalu): %.0f ps' % r['offset_ps'])
    print('sigma(dt_A - dt_B) = %.0f ps  ->  sigma jedne znacky (nezavisle kanaly) = %.0f ps' %
          (r['sigma_diff_ps'], r['sigma_single_ps']))
    print('pro srovnani: sigma jedne znacky z dt_a (STATUS #272) ~ 105 ps')
    return 0


def selftest():
    import random
    random.seed(1)
    f0, g = 10000008.4, 250000000
    rows = []
    for i in range(500):
        e1, e2 = random.gauss(0, 60.0), random.gauss(0, 60.0)          # ps, nezavisle kanaly
        rel = (e2 - e1) * 1e-12 / (g * 1e-9)                          # f_A/f_B - 1 = (dt_B - dt_A)/dt  (A: chyba e1, B: e2)
        f1, f2 = f0 * (1 + rel / 2), f0 * (1 - rel / 2)
        if i % 50 == 0:                                                # okno uzavrene jinou hranou: N lisi o 1 -> 4 Hz
            f2 += 4.0
        rows.append((i, f1, f2, g))
    r = analyze_rows(rows)
    ok = r is not None and 7 <= r['outliers'] <= 13 and 55 < r['sigma_single_ps'] < 65
    print('selftest tdc_ab_noise: %s (sigma_single %.1f ps, odlehle %d)' % ('PASS' if ok else 'FAIL', r['sigma_single_ps'], r['outliers']))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sp = ap.add_subparsers(dest='cmd', required=True)
    c = sp.add_parser('collect'); c.add_argument('--windows', type=int, default=600)
    c.add_argument('--host', default='10.0.0.106'); c.add_argument('--out', default='ab.csv')
    a_ = sp.add_parser('analyze'); a_.add_argument('file')
    sp.add_parser('selftest')
    a = ap.parse_args()
    if a.cmd == 'collect': collect(a.host, a.windows, a.out); return 0
    if a.cmd == 'analyze': return analyze(a.file)
    return selftest()


if __name__ == '__main__':
    sys.exit(main())
