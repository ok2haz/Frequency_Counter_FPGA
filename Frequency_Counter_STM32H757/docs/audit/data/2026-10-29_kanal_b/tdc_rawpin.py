"""(2026-10-31) Kalibrace hustotou kodu PRES VSTUPNI PIN bez zmeny FPGA: RAW_CAPTURE (512 po sobe jdoucich znacek) se
signalem nesoudelnym se 100 MHz. Pri rovnomerne skutecne fazi je hustota namerenych jemnych casu rozdil mezi tabulkou TDC
(kalibrovanou z ring oscilatoru) a skutecnou cestou signalu: E(phi) = tau - T * F(tau), phi = T * F(tau), F = empiricka
distribucni funkce namerenych jemnych casu tau. Porovnani s tvarem z testu s ujizdejici fazi (tdcref_A/B.npy z
tdc_gencyc.py, faze s neznamym pocatkem -> kruhovy posun) rozhodne, zda krok 1 (kalibrace z pinu) slozku 10 ns opravi.

Zapojeni jako u testu s ujizdejici fazi (Si5356 CLK_2 -> A, CLK_4 -> B). Mereni (deska, UART):
  python -I tdc_rawpin.py sber --port COM10 --out <adresar> [--n 400] [--f 11603104]
     nastavi `gen 2 <f>` a `gen 4 <f>` (pomer 100 MHz / f ~ 8,618: krok faze mezi hranami ~ zlaty rez), pak --n krat
     `fpga raw a dump` a `fpga raw b dump`, surovy vystup do <adresar>/raw_<kanal>.txt (pripisuje, lze navazat)
Rozbor (kdekoli, nad kopii):
  python -I tdc_rawpin.py rozbor <adresar> [--ref <adresar s tdcref_A.npy, tdcref_B.npy>]"""
import argparse, os, re, subprocess, sys, time
import numpy as np

T = 10000.0                                       # perioda taktu [ps]
UARTQ = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'tools', 'uartq.ps1')
RE_MARK = re.compile(r'^\s*(\d+)\s+(-?)(\d+)\.(\d{6})(\d{6}) s\s+d\s')


def uart(port, cmds, idle=700, mx=20000, timeout=600):
    cmd = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', os.path.normpath(UARTQ), '-Port', port,
           '-Idle', str(idle), '-Max', str(mx), '-Cmds', ';'.join(cmds)]
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, encoding='utf-8', errors='replace').stdout
    except subprocess.TimeoutExpired:
        return ''


def sber(a):
    os.makedirs(a.out, exist_ok=True)
    print(uart(a.port, ['gen 2 %d' % a.f, 'gen 4 %d' % a.f, 'gen']))
    done, t0 = 0, time.time()
    while done < a.n:
        k = min(10, a.n - done)
        for ch in ('a', 'b'):
            txt = uart(a.port, ['fpga raw %s dump' % ch] * k)
            with open(os.path.join(a.out, 'raw_%s.txt' % ch), 'a', encoding='utf-8') as f:
                f.write(txt)
        done += k
        print('%d/%d zachytu, %.0f s' % (done, a.n, time.time() - t0), flush=True)


def load(path):
    """jemne casy [ps] po zachytech: kazdy blok `FPGA RAW_CAPTURE` zvlast (mezera mezi zachyty neni souvisla)."""
    caps, cur = [], []
    for line in open(path, encoding='utf-8', errors='replace'):
        if 'RAW_CAPTURE' in line:
            if cur: caps.append(cur)
            cur = []
            continue
        m = RE_MARK.match(line)
        if m and not m.group(2):
            ps = int(m.group(3)) * 10 ** 12 + int(m.group(4)) * 10 ** 6 + int(m.group(5))
            cur.append(ps)
    if cur: caps.append(cur)
    return caps


def inl(tau, nb=400):
    """E(phi) na mrizce nb bodu periody: phi = T*F(tau), E = tau - phi (stredni hodnota odectena)."""
    s = np.sort(tau)
    n = len(s)
    q = (np.arange(n) + 0.5) / n                   # F v bodech vzorku
    phi = q * T
    e = s - phi
    g = (np.arange(nb) + 0.5) * T / nb
    eg = np.interp(g, phi, e)
    return g, eg - eg.mean()


def rozbor(a):
    for ch in ('a', 'b'):
        p = os.path.join(a.adr, 'raw_%s.txt' % ch)
        if not os.path.exists(p):
            continue
        caps = load(p)
        allm = [m for c in caps for m in c]
        tau = np.array([m % 10000 for m in allm], dtype=float)
        # kontrola rovnomernosti zdroje: kroky faze mezi po sobe jdoucimi znackami
        d = np.concatenate([np.diff(np.array(c, dtype=np.int64)) for c in caps if len(c) > 1])
        per = np.median(d)
        print('kanal %s: %d zachytu, %d znacek, median rozdilu %.1f ps (%.4f MHz)' % (
            ch.upper(), len(caps), len(tau), per, 1e6 / per if per else 0))
        if len(tau) < 2000:
            print('  malo znacek'); continue
        h, _ = np.histogram(tau, bins=100, range=(0, T))
        print('  hustota po 100 ps: min %.2f / max %.2f stredni (rovnomerna faze + spravna tabulka = 1 +- %.2f)' % (
            h.min() / h.mean(), h.max() / h.mean(), 3.0 / np.sqrt(h.mean())))
        g, e = inl(tau)
        print('  E(phi) z hustoty: spicka-spicka %.1f ps, rms %.1f ps (statisticka chyba ~%.1f ps)' % (
            e.max() - e.min(), e.std(), 0.5 * T / np.sqrt(len(tau))))
        print('  E po 500 ps: ' + ' '.join('%+.0f' % e[i] for i in range(0, len(e), 20)))
        np.save(os.path.join(a.adr, 'pin_E_%s.npy' % ch.upper()), np.vstack([g, e]))
        if a.ref:
            rp = os.path.join(a.ref, 'tdcref_%s.npy' % ch.upper())
            if os.path.exists(rp):
                r = np.load(rp)
                rg = (np.arange(len(r)) + 0.5) * T / len(r)
                ri = np.interp(g, rg, r, period=T)
                best = max(((np.corrcoef(e, np.roll(ri, s))[0, 1], s) for s in range(len(e))))
                c, s = best
                rs = np.roll(ri, s)
                print('  proti testu s ujizdejici fazi (%s): korelace po posunu %+.2f (posun %.0f ps), rms %.1f / %.1f ps, '
                      'rms rozdilu %.1f ps' % (rp, c, s * T / len(e), e.std(), rs.std(), (e - rs).std()))


def syntest(a):
    """Overeni rozboru na syntetice (L-0249): znacky s fazi krokem ~zlaty rez, znamy tvar E, sum 15 ps; zapise raw_a.txt
    ve formatu UART `fpga raw a dump` a tdcref_A.npy s posunutym pocatkem faze; rozbor musi tvar vratit (korelace ~1)."""
    os.makedirs(a.out, exist_ok=True)
    rng = np.random.default_rng(1)
    E = lambda p: 120.0 * np.sin(2 * np.pi * p / T) + 60.0 * np.sin(6 * np.pi * p / T + 1.0) - 150.0 * np.exp(-((p - 6000.0) / 300.0) ** 2)
    per = 1e12 / 11603104.0
    with open(os.path.join(a.out, 'raw_a.txt'), 'w', encoding='utf-8') as f:
        for c in range(a.n):
            f.write('FPGA RAW_CAPTURE kanal A: 512 znacek, ceka nejvys 5 s\n')
            t0 = rng.uniform(0, 1e9)
            for i in range(512):
                tt = t0 + i * per
                ph = tt % T
                ps = int(round(tt - ph + ((ph + E(ph) + rng.normal(0, 15.0)) % T)))
                f.write('  %3d  %d.%06d%06d s  d 0.000000086183 s\n' % (i, ps // 10 ** 12, (ps % 10 ** 12) // 10 ** 6, ps % 10 ** 6))
    g = (np.arange(800) + 0.5) * T / 800
    np.save(os.path.join(a.out, 'tdcref_A.npy'), np.roll(E(g) - E(g).mean(), 123))
    print('syntetika zapsana do %s, rozbor: tdc_rawpin.py rozbor %s --ref %s' % (a.out, a.out, a.out))


ap = argparse.ArgumentParser()
sp = ap.add_subparsers(dest='cmd', required=True)
s1 = sp.add_parser('sber'); s1.add_argument('--port', default='COM10'); s1.add_argument('--out', required=True)
s1.add_argument('--n', type=int, default=400); s1.add_argument('--f', type=int, default=11603104)
s2 = sp.add_parser('rozbor'); s2.add_argument('adr'); s2.add_argument('--ref')
s3 = sp.add_parser('syntest'); s3.add_argument('--out', required=True); s3.add_argument('--n', type=int, default=200)
a = ap.parse_args()
{'sber': sber, 'rozbor': rozbor, 'syntest': syntest}[a.cmd](a)
