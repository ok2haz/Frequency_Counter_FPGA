#!/usr/bin/env python3
"""spi_night_report.py -- vyhodnoceni nocniho testu SPI (vystup tools/spi_night.py).

    python -I tools/spi_night_report.py noc_logs/20261025_2106

Vypise: souhrn vetvi a test rozdilu (chyby na ramec), druhy a bloky, fazi vuci PPS, intervaly a shlukovani, analyzu vadnych bloku
(posun proti sablone hlavicky, CRC prazdneho bloku s jinou znackou), pocitadla FPGA, dopad na mereni (zamitnuta okna, sigma_y).
Jen cteni CSV.
"""
import csv, math, os, sys
from collections import Counter, defaultdict


def rd(path):
    with open(path, newline='', encoding='utf-8') as f:
        return list(csv.DictReader(f))


def binom_p(k, n, p):
    """dvoustranny presny binomicky test"""
    pm = [math.comb(n, i) * p ** i * (1 - p) ** (n - i) for i in range(n + 1)]
    return min(1.0, sum(x for x in pm if x <= pm[k] * (1 + 1e-9)))


def gammainc_lower(a, x):
    if x <= 0:
        return 0.0
    s, term, n = 1.0 / a, 1.0 / a, 0
    while abs(term) > 1e-12 * abs(s) and n < 1000:
        n += 1
        term *= x / (a + n)
        s += term
    return s * math.exp(-x + a * math.log(x) - math.lgamma(a))


def chi2_sf(x, k):
    return max(0.0, 1.0 - gammainc_lower(k / 2.0, x / 2.0))


def crc16(data):
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


_ZMAP = {}


def zero_marker_map():
    """CRC bloku {k2, s2} + 252 nul -> (k2, s2). CRC nulove casti je linearni (posuvny registr bez vstupu), spocita se z 16 baznich vektoru."""
    if _ZMAP:
        return _ZMAP

    def run_zeros(c, n):
        for _ in range(n * 8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
        return c
    basis = [run_zeros(1 << i, 252) for i in range(16)]

    def f(c):
        r = 0
        for i in range(16):
            if (c >> i) & 1:
                r ^= basis[i]
        return r
    for k2 in range(256):
        for s2 in range(256):
            c = crc16(bytes([k2, s2]))
            _ZMAP[f(c)] = (k2, s2)
    return _ZMAP


def bits(bs):
    return [(b >> (7 - i)) & 1 for b in bs for i in range(8)]


# konstantni bajty bloku 0 (PROTOKOL_V3 kap. 3.1, FW 0x0507, git 28c71c9, CAPS 0x005F7C32)
TEMPLATE = {0: 0xA5, 1: 0x03, 2: 0x80, 8: 0x00, 9: 0x04, 10: 0x04, 11: 0x00, 16: 0xC9, 17: 0x71, 18: 0x8C, 19: 0x02, 20: 0x07, 21: 0x05,
            22: 0x64, 23: 0x19, 24: 0x32, 25: 0x7C, 26: 0x5F, 27: 0x00}


def best_shift(raw):
    O = bits(raw)
    tpl_bits = {}
    for off, v in TEMPLATE.items():
        for i in range(8):
            tpl_bits[off * 8 + i] = (v >> (7 - i)) & 1
    res = []
    for d in range(-40, 41):
        ok = tot = 0
        for j, v in tpl_bits.items():
            i = j + d
            if 0 <= i < len(O):
                tot += 1
                ok += (O[i] == v)
        res.append((ok / tot if tot else 0, d))
    res.sort(reverse=True)
    return res[0], dict((d, s) for s, d in res)[0]


def main():
    d = sys.argv[1]
    P = lambda n: os.path.join(d, n)
    vet, err, blk, cyc, evs = rd(P('spi_vetve.csv')), rd(P('spi_chyby.csv')), rd(P('spi_bloky.csv')), rd(P('spi_cykly.csv')), rd(P('spi_udalosti.csv'))
    st = rd(P('state.csv'))

    print('=' * 100)
    print('1. VETVE (chyby na ramec)')
    print('=' * 100)
    grp = defaultdict(lambda: dict(h=0.0, fr=0, er=0, k=[0, 0, 0, 0], b=[0, 0, 0, 0], n=0))
    for r in vet:
        g = grp[r['arm']]
        g['h'] += float(r['dur_h']); g['fr'] += int(r['frames']); g['er'] += int(r['err']); g['n'] += 1
        for i in range(4):
            g['k'][i] += int(r['k%d' % (i + 1)]); g['b'][i] += int(r['b%d' % i])
        print('  %s%s  %.2f h  ramcu %7d  chyb %3d  %6.2f/h  %6.1f /Mramcu' % (r['arm'], r['rep'], float(r['dur_h']), int(r['frames']), int(r['err']),
              float(r['err_per_h']), float(r['err_per_Mframes'])))
    print()
    A = grp['A']
    print('  %-3s %5s %9s %5s %8s %10s %9s   %s' % ('vetev', 'h', 'ramcu', 'chyb', 'chyb/h', '/Mramcu', 'p vs A', 'druhy CRC0/CRC1-3/MAGIC/LEN'))
    for a in sorted(grp):
        g = grp[a]
        p = ''
        if a != 'A':
            n = g['er'] + A['er']
            p = '%.3f' % binom_p(g['er'], n, g['fr'] / (g['fr'] + A['fr']))
        print('  %-5s %5.2f %9d %5d %8.2f %10.1f %9s   %s' % (a, g['h'], g['fr'], g['er'], g['er'] / g['h'], g['er'] * 1e6 / g['fr'], p, g['k']))
    fr_all, er_all = sum(g['fr'] for g in grp.values()), sum(g['er'] for g in grp.values())
    print('  celkem: ramcu %d, chyb %d, %.1f /Mramcu' % (fr_all, er_all, er_all * 1e6 / fr_all))
    print('  pomer ramcu/h B : A = %.2f, pomer chyb/h B : A = %.2f  (stejny pomer = chyba je na ramec, ne v case)' % (
        (grp['B']['fr'] / grp['B']['h']) / (A['fr'] / A['h']), (grp['B']['er'] / grp['B']['h']) / (A['er'] / A['h'])))

    print()
    print('=' * 100)
    print('2. DRUHY A BLOKY')
    print('=' * 100)
    print('  druh:', dict(Counter(int(e['kind']) for e in err)), ' (1 CRC bloku 0, 2 CRC bloku 1-3, 3 MAGIC/VER, 4 LEN)')
    masks = Counter(e['blk_ok'] for e in err)
    print('  maska bloku OK (hex, bit k = blok k sedi):', dict(masks.most_common()))
    firstbad = Counter()
    for e in err:
        m = int(e['blk_ok'], 16)
        for k in range(4):
            if not (m >> k) & 1:
                firstbad[k] += 1
                break
    print('  PRVNI vadny blok:', dict(sorted(firstbad.items())), ' (rovnomerne by bylo ~25 % v kazdem)')
    badcnt = Counter()
    for e in err:
        m = int(e['blk_ok'], 16)
        for k in range(4):
            if not (m >> k) & 1:
                badcnt[k] += 1
    print('  vadne bloky celkem:', dict(sorted(badcnt.items())), '(z %d chyb)' % len(err))
    nbad = Counter(bin(15 & ~int(e['blk_ok'], 16)).count('1') for e in err)
    print('  pocet vadnych bloku v ramci:', dict(sorted(nbad.items())))
    print('  serie (run):', dict(Counter(int(e['run']) for e in err)))

    print()
    print('=' * 100)
    print('3. FAZE VUCI PPS (ms od posledni zmeny PSEQ; presnost +- perioda pollu)')
    print('=' * 100)
    ph = [int(e['since_pps_ms']) for e in err if int(e['since_pps_ms']) < 65535]
    bins = Counter(min(p // 100, 10) for p in ph)
    print('  histogram po 100 ms:', [(b * 100, bins.get(b, 0)) for b in range(11)])
    obs = [bins.get(b, 0) for b in range(10)]
    n = sum(obs)
    chi = sum((o - n / 10) ** 2 / (n / 10) for o in obs)
    print('  chi2 vs rovnomerne (10 binu, 0-999 ms): %.1f, p = %.3f, n = %d' % (chi, chi2_sf(chi, 9), n))
    for lo, hi in ((150, 250), (0, 100)):
        c = sum(1 for p in ph if lo <= p < hi)
        print('  okno %d-%d ms: %d chyb (%.1f %%; cekano %.1f %% pri rovnomernem)' % (lo, hi, c, 100.0 * c / max(1, len(ph)), (hi - lo) / 10.0))

    print()
    print('=' * 100)
    print('4. CAS: intervaly, shlukovani, vyvoj')
    print('=' * 100)
    t = sorted(int(e['uptime_s']) for e in err)
    gaps = [b - a for a, b in zip(t, t[1:])]
    if gaps:
        mean = sum(gaps) / len(gaps)
        print('  intervaly mezi chybami [s]: min %d, median %d, prumer %.0f, max %d (cteni po 120 s, takze 0 = ve stejnem cyklu)' % (
            min(gaps), sorted(gaps)[len(gaps) // 2], mean, max(gaps)))
        same = sum(1 for g in gaps if g == 0)
        print('  chyby ve stejnem 120s cyklu jako predchozi: %d z %d' % (same, len(gaps)))
    # rozptylovy index po 20 min v kazde vetvi A
    t0 = int(cyc[0]['uptime_s'])
    per = Counter((int(e['uptime_s']) - t0) // 1200 for e in err if e['arm'] == 'A')
    vals = [per.get(i, 0) for i in range(max(per) + 1)] if per else []
    if vals:
        m = sum(vals) / len(vals)
        v = sum((x - m) ** 2 for x in vals) / (len(vals) - 1)
        print('  vetve A po 20 min: prumer %.2f, rozptyl %.2f (Poisson: rozptyl ~ prumer; pomer %.2f)' % (m, v, v / m if m else 0))
    byh = Counter((e['t_iso'][:13]) for e in err if e['arm'] != 'B')
    print('  chyby (bez vetve B) po hodinach:', dict(sorted(byh.items())))

    print()
    print('=' * 100)
    print('5. SEQ / cislo ramce (jen chyby s platnou hlavickou: druh 2)')
    print('=' * 100)
    k2 = [e for e in err if e['kind'] == '2']
    for kbits in (1, 2, 3, 4, 5, 6):
        c = Counter(int(e['seq']) & ((1 << kbits) - 1) for e in k2)
        nn = len(k2)
        exp = nn / (1 << kbits)
        chi = sum((c.get(i, 0) - exp) ** 2 / exp for i in range(1 << kbits)) if exp else 0
        print('  SEQ mod %2d: chi2 %.1f, p = %.3f' % (1 << kbits, chi, chi2_sf(chi, (1 << kbits) - 1)))
    for kbits in (1, 2, 3, 4):
        c = Counter(int(e['frame']) & ((1 << kbits) - 1) for e in err)
        nn = len(err)
        exp = nn / (1 << kbits)
        chi = sum((c.get(i, 0) - exp) ** 2 / exp for i in range(1 << kbits))
        print('  cislo ramce STM mod %2d: chi2 %.1f, p = %.3f' % (1 << kbits, chi, chi2_sf(chi, (1 << kbits) - 1)))

    print()
    print('=' * 100)
    print('6. VADNE BLOKY (surova data)')
    print('=' * 100)
    zero_cases = shifts = 0
    blk0_shift = []
    marker_hits = []
    for r in blk:
        raw = bytes.fromhex(r['hex'])
        k = int(r['blok'])
        nz = sum(1 for b in raw[:254] if b)
        if k == 0:
            (sc, dd), sc0 = best_shift(raw)
            blk0_shift.append((r['t_ms'], r['kind'], dd, sc, sc0))
        else:
            # blok z nul se znackou {k, SEQ_lo}: ktera znacka by dala ulozene CRC?
            stored = raw[254] | (raw[255] << 8)
            payload_zero = all(b == 0 for b in raw[2:254])
            ok_now = crc16(raw[:254]) == stored
            hit = None
            if payload_zero:
                zero_cases += 1
                hit = zero_marker_map().get(stored)
                marker_hits.append((r['t_ms'], k, raw[0], raw[1], hit, ok_now))
    print('  vadnych bloku 0 (hlavicka): %d; bloku 1-3: %d (z toho s nulovym obsahem za znackou: %d)' % (
        len(blk0_shift), len(blk) - len(blk0_shift), zero_cases))
    if blk0_shift:
        print('  blok 0: nejlepsi posun proti sablone hlavicky [bity] (skore = shoda konstantnich bitu, 0.5 = nahoda; skore pri posunu 0):')
        for t_ms, kind, dd, sc, sc0 in blk0_shift[:40]:
            print('    t_ms %-9s druh %s  posun %+3d bit  skore %.2f  (bez posunu %.2f)' % (t_ms, kind, dd, sc, sc0))
        sh = Counter(dd for _, _, dd, sc, _ in blk0_shift if sc >= 0.75)
        print('  rozlozeni posunu u zretelnych shod (skore >= 0.75):', dict(sorted(sh.items())), ' z', len(blk0_shift))
    if marker_hits:
        print('  bloky 1-3 s nulovym obsahem: znacka prijata -> znacka, ktera by dala ulozene CRC (None = zadna):')
        for t_ms, k, m0, m1, hit, okn in marker_hits[:40]:
            print('    t_ms %-9s blok %d  prijato {%02X,%02X}  CRC sedi k prijatym datum: %s  odpovidajici znacka: %s' % (
                t_ms, k, m0, m1, okn, ('{%02X,%02X}' % hit) if hit else None))

    print()
    print('=' * 100)
    print('7. POCITADLA FPGA A DOPAD NA MERENI')
    print('=' * 100)
    first, last = cyc[0], cyc[-1]
    for key in ('fpga_rx_crc', 'fpga_rx_len', 'seq_diry', 'citani', 'hodiny', 'delka', 'crc_err_total'):
        print('  %-14s %5s -> %5s' % (key, first[key], last[key]))
    inc = 0
    both = 0
    prev = None
    for r in cyc:
        if prev is not None and r['boot'] == prev['boot']:
            de = int(r['crc_err_total']) - int(prev['crc_err_total'])
            dl = int(r['fpga_rx_len']) - int(prev['fpga_rx_len'])
            dc = int(r['fpga_rx_crc']) - int(prev['fpga_rx_crc'])
            if dl or dc:
                inc += 1
                both += (de > 0)
        prev = r
    print('  cykly, ve kterych FPGA zvysila RX_LEN nebo RX_CRC: %d (z nich s novou SPI chybou na STM: %d)' % (inc, both))
    gps = Counter(r['gpsdo'] for r in cyc)
    print('  GPSDO stav v cyklech:', dict(gps), '; warm:', dict(Counter(r['warm'] for r in cyc)))
    print('  udalosti:', dict(Counter(e['event'] for e in evs)))
    if st:
        print('  state.csv: %d radku, udalosti %s, stav GPSDO %s, spi_ok False: %d, signal_lost True: %d' % (
            len(st), dict(Counter(r['event'] for r in st)), dict(Counter(r['gpsdo_state'] for r in st)),
            sum(1 for r in st if r['spi_ok'] == 'False'), sum(1 for r in st if r['signal_lost'] == 'True')))
        for key in ('temp_ocxo_c', 'temp_board_c', 'vc_mv', 'v12_mv', 'v5_mv'):
            vv = [float(r[key]) for r in st if r[key] not in ('', None)]
            if vv:
                print('    %-13s min %.2f  max %.2f' % (key, min(vv), max(vv)))
        # sigma_y@1s po vetvich (cas z vetve)
        print('  sigma_y@1s (sy1e15 = 1e-15) median po vetvich:')
        for r in vet:
            sel = [int(x['sy1e15']) for x in st if x['sy1e15'] not in ('', '0') and r['start'] <= x['t_iso'] <= r['end']]
            sel.sort()
            if sel:
                print('    %s%s  n %4d  median %8d  min %8d  q90 %8d' % (r['arm'], r['rep'], len(sel), sel[len(sel) // 2], sel[0], sel[int(len(sel) * 0.9)]))


if __name__ == '__main__':
    main()
