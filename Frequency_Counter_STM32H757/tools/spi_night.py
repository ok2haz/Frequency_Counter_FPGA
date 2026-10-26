#!/usr/bin/env python3
"""spi_night.py -- automaticky nocni test chyb SPI v3 (STATUS #343, firmware >= v0.40.7).

Jeden spoustec: (1) pocka na GPSDO LOCK a konec WARMUP, (2) striida vetve (zakladni konfigurace, `spi poll 10`, `spi dma on`,
SCK 6,25 MHz), (3) kazdych --period s precte pres UART `spi err`, `spi err dump`, `status` a uklada do CSV, (4) spusti webove
zaznamniky (tools/state_csv.py, tools/raw_fetch.py --follow), takze jedna noc dava SPI statistiku i mereni.

Vystup (adresar --outdir, vychozi noc_logs/<datum_cas>/):
  spi_chyby.csv    kazda nova SPI chyba (cas, ramec, SEQ, druh, bloky OK, odstup od PSEQ, serie)
  spi_bloky.csv    surove vadne bloky (hex 256 B) k nove chybe
  spi_vetve.csv    souhrn za kazdou vetev (pocet transakci, chyb podle druhu a bloku, serie, chyb/h, chyb/Mramcu)
  spi_cykly.csv    kazdy cyklus: uptime, GPSDO, WARMUP, CRC err, zamitnuta okna, SEQ diry, souhrn spi err
  spi_udalosti.csv START, RESTART, NO_UART, CFG_MISMATCH, WAIT_LOCK, LOCK_OK, KONEC ...
  state.csv, raw.csv, state.log, raw.log   webove zaznamniky (jen s webem; --no-web je vypne)

Pouziti (z adresare Frequency_Counter_STM32H757, zadny jiny terminal nesmi drzet port):
    python tools/spi_night.py --port COM10 --ip 10.0.0.106                 # plan na 16 h
    python tools/spi_night.py --test                                        # kratka zkouska (~15 min) s vstrikovanymi chybami
    python tools/spi_night.py --selftest                                    # jen test parsovani (bez desky)
Pri restartu desky (uptime klesne) zapise RESTART a znovu nastavi aktualni vetev (spi poll/dma/takt se po resetu vraci na vychozi).
Ctrl+C = konec se souhrnem aktualni vetve.
"""
import argparse, csv, datetime, os, re, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UARTQ = os.path.join(ROOT, 'tools', 'uartq.ps1')

# vetev -> (prikazy nastaveni, ocekavany takt Hz, ocekavane DMA 0/1, ocekavany odstup pollu ms, ocekavana rychlost hran SCK/MOSI/CS).
# Vzdy cela konfigurace (po resetu se vsechno vraci na vychozi). Rychlost hran: `spi pins` je od STM v0.40.8; vychozi SCK MEDIUM, MOSI MEDIUM, CS HIGH.
PINS_DEF = ['spi pins sck medium', 'spi pins mosi medium', 'spi pins cs high']
ARMS = {
    'A': (['spi 25', 'spi poll 40', 'spi dma off'] + PINS_DEF, 25000000, 0, 40, ('MEDIUM', 'MEDIUM', 'HIGH')),
    'B': (['spi 25', 'spi poll 10', 'spi dma off'] + PINS_DEF, 25000000, 0, 10, ('MEDIUM', 'MEDIUM', 'HIGH')),
    'C': (['spi 25', 'spi poll 40', 'spi dma on'] + PINS_DEF,  25000000, 1, 40, ('MEDIUM', 'MEDIUM', 'HIGH')),
    'E': (['spi 7',  'spi poll 40', 'spi dma off'] + PINS_DEF, 6250000,  0, 40, ('MEDIUM', 'MEDIUM', 'HIGH')),
    'L': (['spi 25', 'spi poll 40', 'spi dma off', 'spi pins low'],  25000000, 0, 40, ('LOW', 'LOW', 'LOW')),            # nejpomalejsi hrany
    'V': (['spi 25', 'spi poll 40', 'spi dma off', 'spi pins very'], 25000000, 0, 40, ('VERY_HIGH', 'VERY_HIGH', 'VERY_HIGH')),   # nejrychlejsi hrany
    'M': (['spi 25', 'spi poll 10', 'spi dma off', 'spi pins low'],  25000000, 0, 10, ('LOW', 'LOW', 'LOW')),
    'W': (['spi 25', 'spi poll 10', 'spi dma off', 'spi pins very'], 25000000, 0, 10, ('VERY_HIGH', 'VERY_HIGH', 'VERY_HIGH')),
    'K': (['spi 25', 'spi poll 10', 'spi dma off', 'spi pins sck medium', 'spi pins mosi medium', 'spi pins cs low'], 25000000, 0, 10, ('MEDIUM', 'MEDIUM', 'LOW')),
    'S': (['spi 25', 'spi poll 10', 'spi dma off', 'spi pins sck low', 'spi pins mosi low', 'spi pins cs high'],     25000000, 0, 10, ('LOW', 'LOW', 'HIGH')),
}
# plan 1 = noc 2026-10-25 (chyby na rameci, FPGA 0x0507); plan 2 = po filtru CS (FPGA 0x0508): meri se citace zahozenych pulzu na CS
PLAN = [('A', 2.0), ('B', 1.0), ('A', 2.0), ('C', 2.0), ('A', 2.0), ('E', 2.0), ('B', 1.0), ('C', 2.0), ('A', 1.5)]   # 15,5 h + cekani
PLAN2 = [('A', 1.5), ('L', 1.5), ('V', 1.5), ('A', 1.5), ('B', 1.0), ('E', 1.5), ('L', 1.0), ('V', 1.0), ('A', 1.0)]   # 11,5 h + cekani
PLAN3 = [('B', 0.75), ('M', 0.75), ('W', 0.75), ('K', 0.75), ('S', 0.75), ('B', 0.75), ('M', 0.75), ('W', 0.75), ('A', 0.75)]   # 6,75 h, polling 10 ms = vic udalosti
PLAN_TEST = [('A', 0.05), ('B', 0.05), ('L', 0.05), ('V', 0.05)]                                                    # 3 min kazda

RE_UP = re.compile(r'RUNNING gpsdo-ui (v[\d.]+)\s+uptime\s+(\d+)s')
RE_GPSDO = re.compile(r'^GPSDO: (\w+)', re.M)
RE_CRC = re.compile(r'FPGA: link (\w+), CRC err (\d+)')
RE_CIT = re.compile(r'zamitnuto oken: citani (\d+), hodiny (\d+), delka okna (\d+)')
RE_SEQD = re.compile(r'SEQ FPGA: diry (\d+)')
RE_S1 = re.compile(r'SPI ERR: transakci (\d+), chyb (\d+) = CRC bloku 0: (\d+), CRC bloku 1-3: (\d+), MAGIC/VER: (\d+), LEN: (\d+)'
                   r' \| CRC podle bloku 0:(\d+) 1:(\d+) 2:(\d+)\s+3:(\d+)')
RE_S2 = re.compile(r'SPI ERR: serii (\d+) \(izolovanych (\d+), nejdelsi (\d+)\) \| transakce max (\d+) us, odstup max (\d+) ms'
                   r' \| nejkratsi odstup (\d+) ms')
RE_E = re.compile(r'^\s*#(\d+) t=(\d+)\.(\d{3}) s ramec (\d+) kind (\d) bloky OK ([0-9A-F]+) SEQ (\d+) od PSEQ (\d+) ms'
                  r' odstup (\d+) ms serie (\d+) slot (-?\d+)', re.M)
RE_SLOT = re.compile(r'SPI ERR: slot (\d+) = blok (\d+) ramce')
RE_HEXL = re.compile(r'^\s+([0-9A-F]{3}):((?: [0-9A-F]{2}){32})\s*$', re.M)
RE_FRX = re.compile(r'RX chyb CRC (\d+) delka (\d+)')      # pocitadla na strane FPGA (`fpga v3`): vstup MOSI, delka transakce
RE_SCK = re.compile(r'SCK = (\d+) Hz')
RE_DMA = re.compile(r'SPI2 DMA: (POUZIVA SE|nepouziva se)')
RE_POLL = re.compile(r'nejkratsi odstup transakci v3 = (\d+) ms')
RE_PINS = re.compile(r'rychlost vystupu SCK (\w+), MOSI (\w+), CS (\w+)')
RE_CSGL = re.compile(r'pulzy na CS zahozene filtrem FPGA \((\d+) taktu 100 MHz = \d+ ns\): aktivni 1-2 taktu (\d+), 3-4: (\d+), 5-8: (\d+), 9\+: (\d+) \| v klidu (\d+)')
CS_KEYS = ['cs_filt', 'cs_a12', 'cs_a34', 'cs_a58', 'cs_a9p', 'cs_idle']

S_KEYS = ['frames', 'err', 'k1', 'k2', 'k3', 'k4', 'b0', 'b1', 'b2', 'b3', 'bursts', 'isolated', 'max_run', 'xfer_us_max', 'gap_ms_max', 'poll_ms']


def iso():
    return datetime.datetime.now().isoformat(timespec='seconds')


def uart(port, cmds, idle=3000, mx=60000, timeout=150):
    cmd = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', UARTQ, '-Port', port,
           '-Idle', str(idle), '-Max', str(mx), '-Cmds', ';'.join(cmds)]
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, encoding='utf-8', errors='replace').stdout
    except subprocess.TimeoutExpired:
        return ''


def parse_stats(txt):
    """Souhrn `spi err` -> dict (S_KEYS) nebo None."""
    m1, m2 = RE_S1.search(txt), RE_S2.search(txt)
    if not m1 or not m2:
        return None
    v = [int(x) for x in m1.groups()] + [int(x) for x in m2.groups()]
    # m1: frames err k1 k2 k3 k4 b0 b1 b2 b3 ; m2: bursts isolated max_run xfer_us_max gap_ms_max poll_ms
    return dict(zip(S_KEYS, v))


def parse_cs(txt):
    """Citace zahozenych pulzu na CS z FPGA (`spi err`, FW >= 0x0508) -> dict CS_KEYS nebo None."""
    m = RE_CSGL.search(txt)
    return dict(zip(CS_KEYS, [int(x) for x in m.groups()])) if m else None


def parse_errors(txt):
    out = []
    for m in RE_E.finditer(txt):
        i, ts, tms, fr, kind, mask, seq, pps, gap, run, slot = m.groups()
        out.append(dict(idx=int(i), t_ms=int(ts) * 1000 + int(tms), frame=int(fr), kind=int(kind), mask=mask, seq=int(seq),
                        since_pps=int(pps), gap=int(gap), run=int(run), slot=int(slot)))
    return out


def parse_dump(txt):
    """slot -> (index bloku v ramci, hex 512 znaku)."""
    out = {}
    ms = list(RE_SLOT.finditer(txt))
    for i, m in enumerate(ms):
        end = ms[i + 1].start() if i + 1 < len(ms) else len(txt)
        rows = RE_HEXL.findall(txt[m.end():end])
        if len(rows) >= 8:
            out[int(m.group(1))] = (int(m.group(2)), ''.join(h.replace(' ', '') for _, h in rows[:8]))
    return out


def parse_status(txt):
    d = {}
    m = RE_UP.search(txt)
    d['fw'], d['uptime'] = (m.group(1), int(m.group(2))) if m else ('', -1)
    m = RE_GPSDO.search(txt)
    d['gpsdo'] = m.group(1) if m else ''
    d['warm'] = 1 if 'TEPLOTA NEUSTALENA' in txt else 0
    m = RE_CRC.search(txt)
    d['crc_err'] = int(m.group(2)) if m else -1
    m = RE_CIT.search(txt)
    d['citani'], d['hodiny'], d['delka'] = (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else (-1, -1, -1)
    m = RE_SEQD.search(txt)
    d['seq_diry'] = int(m.group(1)) if m else -1
    m = RE_FRX.search(txt)
    d['fpga_rx_crc'], d['fpga_rx_len'] = (int(m.group(1)), int(m.group(2))) if m else (-1, -1)
    return d


class Csv:
    def __init__(self, path, cols):
        self.cols = cols
        new = not os.path.exists(path) or os.path.getsize(path) == 0
        self.f = open(path, 'a', newline='', encoding='utf-8')
        self.w = csv.DictWriter(self.f, fieldnames=cols)
        if new:
            self.w.writeheader()

    def put(self, **kw):
        self.w.writerow({c: kw.get(c, '') for c in self.cols})
        self.f.flush()


def selftest():
    sample = (
        'SPI ERR: transakci 1517, chyb 6 = CRC bloku 0: 2, CRC bloku 1-3: 4, MAGIC/VER: 0, LEN: 0 | CRC podle bloku 0:2 1:1 2:3 \n3:0\n'
        'SPI ERR: serii 3 (izolovanych 1, nejdelsi 3) | transakce max 577 us, odstup max 79 ms | nejkratsi odstup 40 ms\n'
        '  #0 t=73.775 s ramec 1446 kind 2 bloky OK D SEQ 4417 od PSEQ 700 ms odstup 50 ms serie 1 slot 1\n'
        '  #1 t=70.175 s ramec 1374 kind 1 bloky OK E SEQ 4201 od PSEQ 100 ms odstup 50 ms serie 2 slot 0\n'
        '  #2 t=59.425 s ramec 1159 kind 2 bloky OK B SEQ 3556 od PSEQ 350 ms odstup 50 ms serie 3 slot -1\n'
        'SPI ERR: slot 0 = blok 0 ramce\n' +
        ''.join('  %03X:%s\n' % (o, ' 00' * 32) for o in range(0, 256, 32)) +
        'SPI ERR: slot 1 = blok 1 ramce\n' +
        ''.join('  %03X: 01%s\n' % (o, ' 02' * 31) for o in range(0, 256, 32)) +
        '===== > status\nRUNNING gpsdo-ui v0.40.7  uptime 104s\nVAROVANI: prio 7  TEPLOTA NEUSTALENA  - OCXO se zahriva\n'
        'GPSDO: FLL | 68 s | y -12.305 ppb\nSEQ FPGA: diry 1 (zmeskano 1 mereni), resync 0  <== x\nFPGA: link OK, CRC err 6, last 30s ago\n'
        '  FPGA SAMOKONTROLA: zamitnuto oken: citani 3, hodiny 0, delka okna 0 | posledni rozdil 0\n'
        '  FW 0x0507 CAPS 0x005F7C32, REC_HEAD 117377 REC_FIRST 117370, v kruhu 8, FLAGS 0x21, RX chyb CRC 1 delka 17\n'
        'SPI ERR: pulzy na CS zahozene filtrem FPGA (16 taktu 100 MHz = 160 ns): aktivni 1-2 taktu 4, 3-4: 3, 5-8: 2, 9+: 1 | v klidu 7 (saturuji na 255)\n'
        'SPI2: rychlost vystupu SCK MEDIUM, MOSI MEDIUM, CS HIGH (pouziti: spi pins [sck|mosi|cs] <low|medium|high|very>; nepersistuje se)\n')
    s = parse_stats(sample)
    assert s and s['frames'] == 1517 and s['err'] == 6 and s['k1'] == 2 and s['k2'] == 4 and s['b2'] == 3 and s['b3'] == 0, s
    assert s['bursts'] == 3 and s['isolated'] == 1 and s['max_run'] == 3 and s['xfer_us_max'] == 577 and s['gap_ms_max'] == 79 and s['poll_ms'] == 40, s
    e = parse_errors(sample)
    assert len(e) == 3 and e[0]['t_ms'] == 73775 and e[0]['kind'] == 2 and e[0]['mask'] == 'D' and e[0]['slot'] == 1 and e[2]['slot'] == -1, e
    d = parse_dump(sample)
    assert sorted(d) == [0, 1] and d[0][0] == 0 and d[1][0] == 1 and len(d[0][1]) == 512 and d[1][1].startswith('0102'), d.keys()
    st = parse_status(sample)
    assert st['uptime'] == 104 and st['gpsdo'] == 'FLL' and st['warm'] == 1 and st['crc_err'] == 6 and st['citani'] == 3 and st['seq_diry'] == 1, st
    assert st['fpga_rx_crc'] == 1 and st['fpga_rx_len'] == 17, st
    cs = parse_cs(sample)
    assert cs == dict(cs_filt=16, cs_a12=4, cs_a34=3, cs_a58=2, cs_a9p=1, cs_idle=7), cs
    assert RE_PINS.search(sample).groups() == ('MEDIUM', 'MEDIUM', 'HIGH')
    assert parse_cs('SPI ERR: FPGA nema citace pulzu na CS') is None
    assert RE_SCK.search('SPI2: SCK = 6250000 Hz (ramec 128 B ~ 163 us), CRC err 0').group(1) == '6250000'
    print('selftest parsovani: PASS')


class Run:
    def __init__(self, a):
        self.a = a
        os.makedirs(a.outdir, exist_ok=True)
        p = lambda n: os.path.join(a.outdir, n)
        self.errs = Csv(p('spi_chyby.csv'), ['t_iso', 'boot', 'arm', 'rep', 'uptime_s', 't_ms', 'frame', 'kind', 'blk_ok', 'seq', 'since_pps_ms', 'gap_ms', 'run', 'slot'])
        self.blks = Csv(p('spi_bloky.csv'), ['t_iso', 'boot', 'arm', 'rep', 't_ms', 'kind', 'blok', 'hex'])
        self.arms = Csv(p('spi_vetve.csv'), ['arm', 'rep', 'start', 'end', 'dur_h', 'cfg_ok', 'restarts'] + S_KEYS + ['err_per_h', 'err_per_Mframes'] +
                        ['d_cs_a12', 'd_cs_a34', 'd_cs_a58', 'd_cs_a9p', 'd_cs_idle', 'cs_aktivni_per_Mframes'])
        self.cyc = Csv(p('spi_cykly.csv'), ['t_iso', 'boot', 'arm', 'rep', 'uptime_s', 'gpsdo', 'warm', 'crc_err_total', 'citani', 'hodiny', 'delka', 'seq_diry', 'fpga_rx_crc', 'fpga_rx_len'] + CS_KEYS + S_KEYS)
        self.evs = Csv(p('spi_udalosti.csv'), ['t_iso', 'event', 'detail'])
        self.boot = 0
        self.seen = set()
        self.last_up = -1
        self.procs = []

    def ev(self, event, detail=''):
        print('%s %s %s' % (iso(), event, detail), flush=True)
        self.evs.put(t_iso=iso(), event=event, detail=detail)

    def web(self):
        a = self.a
        if a.no_web:
            return
        py = sys.executable
        for name, cmd in (('state', [py, os.path.join(ROOT, 'tools', 'state_csv.py'), '--ip', a.ip, '--out', os.path.join(a.outdir, 'state.csv'), '--every', '10']),
                          ('raw', [py, os.path.join(ROOT, 'tools', 'raw_fetch.py'), '--ip', a.ip, '--out', os.path.join(a.outdir, 'raw.csv'), '--follow', '--every', '10'])):
            lf = open(os.path.join(a.outdir, name + '.log'), 'w')
            self.procs.append(subprocess.Popen(cmd, stdout=lf, stderr=subprocess.STDOUT, cwd=ROOT))
            self.ev('WEB_START', name)

    def stop(self):
        for p in self.procs:
            p.terminate()

    def wait_lock(self):
        a = self.a
        if a.no_wait:
            self.ev('WAIT_LOCK_PRESKOCENO')
            return
        t0 = time.time()
        last = 0.0
        while time.time() - t0 < a.wait_lock_min * 60:
            st = parse_status(uart(a.port, ['status'], mx=40000))
            if st['gpsdo'] == 'LOCK' and not st['warm'] and st['uptime'] >= 0:
                self.ev('LOCK_OK', 'uptime %d s' % st['uptime'])
                return
            if time.time() - last >= 300:
                self.ev('WAIT_LOCK', 'GPSDO %s warm=%d uptime %d' % (st['gpsdo'], st['warm'], st['uptime']))
                last = time.time()
            time.sleep(30)
        self.ev('LOCK_TIMEOUT', 'pokracuji bez LOCK')

    def apply_cfg(self, arm, inject):
        cmds, sck, dma, poll, pins = ARMS[arm]
        for attempt in range(3):
            txt = uart(self.a.port, list(cmds))
            m_s, m_d, m_p, m_pins = RE_SCK.findall(txt), RE_DMA.findall(txt), RE_POLL.findall(txt), RE_PINS.findall(txt)
            ok = bool(m_s) and int(m_s[-1]) == sck and bool(m_p) and int(m_p[-1]) == poll and bool(m_d) and ((m_d[-1] == 'POUZIVA SE') == bool(dma))
            if m_pins:                                   # `spi pins` existuje od STM v0.40.8; se starsim firmwarem se rychlost hran nekontroluje
                ok = ok and tuple(m_pins[-1]) == pins
            if ok:
                # prepnuti taktu / DMA za behu pokazi jednu transakci (zmereno 2026-10-25: MAGIC/VER + vsechny CRC) -> reset az po ustaleni
                time.sleep(4)
                c = ['spi err reset'] + (['spi err inject 2 2'] if inject else [])
                uart(self.a.port, c, idle=2000, mx=20000)
                return True
            self.ev('CFG_MISMATCH', 'vetev %s pokus %d: SCK %s DMA %s poll %s hrany %s' % (arm, attempt + 1, m_s[-1:], m_d[-1:], m_p[-1:], m_pins[-1:]))
            time.sleep(5)
        return False

    def cycle(self, arm, rep):
        txt = uart(self.a.port, ['spi err', 'spi err dump', 'fpga v3', 'status'], mx=100000)
        stat, st = parse_stats(txt), parse_status(txt)
        if stat is None or st['uptime'] < 0:
            self.ev('NO_UART', 'prazdna nebo neuplna odpoved (%d znaku)' % len(txt))
            return None, None
        errs, dump = parse_errors(txt), parse_dump(txt)
        cs = parse_cs(txt)
        st['cs'] = cs
        for e in reversed(errs):                        # nejstarsi prvni
            key = (self.boot, e['t_ms'], e['frame'])
            if key in self.seen:
                continue
            self.seen.add(key)
            self.errs.put(t_iso=iso(), boot=self.boot, arm=arm, rep=rep, uptime_s=st['uptime'], t_ms=e['t_ms'], frame=e['frame'], kind=e['kind'],
                          blk_ok=e['mask'], seq=e['seq'], since_pps_ms=e['since_pps'], gap_ms=e['gap'], run=e['run'], slot=e['slot'])
            if e['slot'] >= 0 and e['slot'] in dump:
                bk, hx = dump[e['slot']]
                self.blks.put(t_iso=iso(), boot=self.boot, arm=arm, rep=rep, t_ms=e['t_ms'], kind=e['kind'], blok=bk, hex=hx)
        self.cyc.put(t_iso=iso(), boot=self.boot, arm=arm, rep=rep, uptime_s=st['uptime'], gpsdo=st['gpsdo'], warm=st['warm'],
                     crc_err_total=st['crc_err'], citani=st['citani'], hodiny=st['hodiny'], delka=st['delka'], seq_diry=st['seq_diry'],
                     fpga_rx_crc=st['fpga_rx_crc'], fpga_rx_len=st['fpga_rx_len'], **(cs or {}), **stat)
        print('%s %s%d up %ds GPSDO %s | ramcu %d chyb %d (serii %d)%s' % (iso(), arm, rep, st['uptime'], st['gpsdo'], stat['frames'], stat['err'], stat['bursts'],
              (' | pulzy CS aktivni %d klid %d' % (cs['cs_a12'] + cs['cs_a34'] + cs['cs_a58'] + cs['cs_a9p'], cs['cs_idle'])) if cs else ''), flush=True)
        return stat, st

    def run_arm(self, arm, rep, hours):
        a = self.a
        cfg_ok = self.apply_cfg(arm, a.inject_test)
        t_start, start_iso = time.time(), iso()
        self.ev('VETEV_START', '%s%d %.2f h cfg_ok=%d' % (arm, rep, hours, cfg_ok))
        acc = {k: 0 for k in S_KEYS}
        last, restarts = None, 0
        cs0 = cs_last = None                                               # citace pulzu na CS jsou kumulativni (nulovat je nelze) -> pocita se rozdil
        def add(dst, s):
            for k in S_KEYS:
                if k in ('max_run', 'xfer_us_max', 'gap_ms_max', 'poll_ms'):
                    dst[k] = max(dst[k], s[k])
                else:
                    dst[k] += s[k]
        t_next = time.time()
        while time.time() - t_start < hours * 3600.0:
            time.sleep(max(0.0, t_next - time.time()))
            t_next = time.time() + a.period
            stat, st = self.cycle(arm, rep)
            if stat is None:
                continue
            if self.last_up >= 0 and st['uptime'] + 5 < self.last_up:      # restart desky
                self.boot += 1
                restarts += 1
                self.ev('RESTART', 'uptime %d -> %d, boot %d' % (self.last_up, st['uptime'], self.boot))
                if last:
                    add(acc, last)
                last = None
                cfg_ok = self.apply_cfg(arm, a.inject_test) and cfg_ok
                self.last_up = st['uptime']
                continue
            self.last_up = st['uptime']
            last = stat
            if st.get('cs'):
                cs_last = st['cs']
                cs0 = cs0 or st['cs']
        stat, st = self.cycle(arm, rep)                                  # posledni cteni vetve
        if stat is not None:
            last = stat
            if st.get('cs'):
                cs_last = st['cs']
                cs0 = cs0 or st['cs']
        tot = dict(acc)
        if last:
            add(tot, last)
        dur_h = (time.time() - t_start) / 3600.0
        dcs = {}
        if cs0 and cs_last:
            for k in ('cs_a12', 'cs_a34', 'cs_a58', 'cs_a9p', 'cs_idle'):
                dcs['d_' + k] = cs_last[k] - cs0[k]
            act = sum(dcs['d_' + k] for k in ('cs_a12', 'cs_a34', 'cs_a58', 'cs_a9p'))
            dcs['cs_aktivni_per_Mframes'] = '%.1f' % (act * 1e6 / tot['frames'] if tot['frames'] else 0)
        self.arms.put(arm=arm, rep=rep, start=start_iso, end=iso(), dur_h='%.3f' % dur_h, cfg_ok=int(cfg_ok), restarts=restarts, **tot,
                      err_per_h='%.2f' % (tot['err'] / dur_h if dur_h else 0), err_per_Mframes='%.1f' % (tot['err'] * 1e6 / tot['frames'] if tot['frames'] else 0), **dcs)
        self.ev('VETEV_KONEC', '%s%d: ramcu %d chyb %d (%.2f/h), serii %d' % (arm, rep, tot['frames'], tot['err'], tot['err'] / dur_h if dur_h else 0, tot['bursts']))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM10')
    ap.add_argument('--ip', default='10.0.0.106')
    ap.add_argument('--outdir', default=None)
    ap.add_argument('--period', type=float, default=120.0, help='perioda cteni spi err [s]')
    ap.add_argument('--wait-lock-min', type=float, default=60.0)
    ap.add_argument('--no-wait', action='store_true')
    ap.add_argument('--no-web', action='store_true')
    ap.add_argument('--test', action='store_true', help='kratka zkouska: 4 vetve po 3 min, bez cekani na LOCK, s vstrikovanymi chybami')
    ap.add_argument('--inject-test', action='store_true', help='na zacatku kazde vetve vstrikne 2 chyby (jen zkouska potrubi)')
    ap.add_argument('--plan', type=int, choices=(1, 2, 3), default=3, help='3 = rychly polling, hrany M/W/K/S (STM >= v0.40.8); 1 = noc 2026-10-25 (A,B,C,E; 15,5 h), 2 = po filtru CS (A,L,V,B,E; 11,5 h; STM >= v0.40.8)')
    ap.add_argument('--until', default=None, help='HH:MM - nejpozdeji dokdy skoncit (vetve se zkrati)')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    if a.selftest:
        selftest()
        return
    plan = {1: PLAN, 2: PLAN2, 3: PLAN3}[a.plan]
    a.deadline = None
    if a.until:
        hh, mm = [int(x) for x in a.until.split(':')]
        d = datetime.datetime.now().replace(hour=hh, minute=mm, second=0, microsecond=0)
        if d < datetime.datetime.now():
            d += datetime.timedelta(days=1)
        a.deadline = d.timestamp()
    if a.test:
        plan, a.no_wait, a.inject_test = PLAN_TEST, True, True
        a.period = min(a.period, 40.0)
    a.outdir = a.outdir or os.path.join('noc_logs', datetime.datetime.now().strftime('%Y%m%d_%H%M') + ('_test' if a.test else ''))
    r = Run(a)
    r.ev('START', 'plan %s, vystup %s' % (' '.join('%s%.2f' % (x, h) for x, h in plan), a.outdir))
    try:
        r.web()
        r.wait_lock()
        reps = {}
        for arm, h in plan:
            if a.deadline:
                left = (a.deadline - time.time()) / 3600.0
                if left < 0.1:
                    r.ev('KONEC', 'termin --until, dalsi vetve vynechany')
                    break
                h = min(h, left)
            reps[arm] = reps.get(arm, 0) + 1
            r.run_arm(arm, reps[arm], h)
        r.apply_cfg('A', False)                                          # na konci zpet na zakladni konfiguraci
        r.ev('KONEC', 'plan dokoncen')
    except KeyboardInterrupt:
        r.ev('KONEC', 'preruseno Ctrl+C')
    finally:
        r.stop()


if __name__ == '__main__':
    main()
