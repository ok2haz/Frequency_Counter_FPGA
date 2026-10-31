"""Opakovane nahrani FPGA a overeni mereni po kazdem nahrani (STATUS #282/#283, L-0141).

Proc: L-0141 -- tentyz bitstream meril v 6 ze 7 nahrani spatne a jednou spravne. Jedno spravne mereni po
nahrani proto nic nedokazuje; pravidlo je "aspon 4 nahrani". Tohle to dela bez rucni prace a vysledek
zapise do tabulky, aby slo porovnat bitstreamy mezi sebou.

Kazde kolo: nahraje bitstream (Gowin programmer_cli), pocka na rozbeh (kalibrace TDC, auto START STM,
SET_CONFIG), pak z webu pristroje (SSE /api/stream, CM4) vezme N mereni a porovna s ocekavanym kmitoctem.
Volitelne (--uart) precte z konzole CM7 `status` a vypise radky FPGA BUILD / HODINY / SAMOKONTROLA
(od FW 0x041F) -- odmitnuta okna se na webu NEOBJEVI (freq_hz = null), takze "zadne mereni" po nahrani
s FW 0x041F typicky znamena, ze samokontrola okna zamita; duvod ukaze prave `status`.

Pouziti (z adresare Frequency_Counter_FPGA_Module), vstup CH_A = znamy signal:
    python tools/fpga_load_test.py --fs ab_test/FW_0x041F_....fs --expect 10000008 --n 5
    python tools/fpga_load_test.py --fs X.fs --expect 1e7 --mode flash --n 5     # 1x flash, pak 5x rekonfigurace z flash
    python tools/fpga_load_test.py --fs X.fs --expect 1e7 --uart COM10          # + `status` z konzole
    python tools/fpga_load_test.py --selftest

--mode sram  (vychozi): kazde kolo "SRAM Program" (op 2, BEZ verify: nas navrh za behu prepisuje BSRAM -
             histogram a tabulky TDC -- takze verify (op 4) vzdy hlasi "Verify Failed") -- flash FPGA se nemeni, po power-cyklu
             nabehne puvodni obraz z flash.
--mode flash: prvni kolo "embFlash Erase,Program,Verify" (op 6), dalsi kola "Reprogram" (op 1 = nova
             konfigurace z flash, flash se uz neprepisuje).
Navratovy kod: 0 = vsechna kola spravne, 1 = aspon jedno spatne / bez mereni, 2 = chyba nastroje.
"""
import argparse, json, os, statistics, subprocess, sys, time, urllib.request

PROG = r'C:\Gowin\Gowin_V1.9.12_x64\Programmer\bin\programmer_cli.exe'
DEVICE = 'GW1NR-9C'
PROG_TIMEOUT_S = 90        # SRAM program trva jednotky sekund; vic = zaseknuty programator


def program(fs, op, extra):
    cmd = [PROG, '--device', DEVICE, '--run', str(op)] + (['--fsFile', os.path.abspath(fs)] if op != 1 else []) + extra
    t = time.time()
    try:
        # 2026-10-10: bez limitu se programmer_cli zasekl na 20 min (100 % CPU, kabel nešel otevřít) a test visel s ním
        r = subprocess.run(cmd, capture_output=True, text=True, errors='replace', timeout=PROG_TIMEOUT_S)
    except subprocess.TimeoutExpired as e:
        return False, time.time() - t, 'TIMEOUT programmer_cli po %d s (kabel? jiny program drzi JTAG?)\n%s' % (
            PROG_TIMEOUT_S, (e.stdout or b'').decode('utf-8', 'replace') if isinstance(e.stdout, bytes) else (e.stdout or ''))
    out = (r.stdout + r.stderr)
    ok = r.returncode == 0 and ('error' not in out.lower() or 'error: 0' in out.lower())
    return ok, time.time() - t, out


def sse_samples(host, n, timeout):
    """Vrati seznam (freq_hz, seq, gate_ns) -- jen udalosti s platnym kmitoctem; konci po n nebo timeoutu."""
    res, nulls = [], 0
    t_end = time.time() + timeout
    try:
        req = urllib.request.urlopen('http://%s/api/stream' % host, timeout=5)
    except Exception as e:  # sit nebo CM4 nedostupne
        return None, str(e)
    try:
        while time.time() < t_end and len(res) < n:
            line = req.readline().decode('utf-8', 'replace')
            if not line.startswith('data:'):
                continue
            try:
                d = json.loads(line[5:])
            except ValueError:
                continue
            if d.get('freq_hz') is None or d.get('signal_lost'):
                nulls += 1
                continue
            res.append((float(d['freq_hz']), d.get('seq_meas'), d.get('gate_ns')))
    except Exception as e:
        return res, 'prerusen stream: %s' % e
    finally:
        try: req.close()
        except Exception: pass
    return res, ('%d udalosti bez mereni' % nulls) if nulls else ''


def uart_status(port):
    try:
        import serial
    except ImportError:
        return ['(pyserial neni nainstalovan)']
    try:
        s = serial.Serial(port, 115200, timeout=0.3)
    except Exception as e:
        return ['(%s: %s)' % (port, e)]
    try:
        s.reset_input_buffer(); s.write(b'status\r\n')
        buf, quiet = b'', time.time()
        while time.time() - quiet < 1.2:
            b = s.read(4096)
            if b: buf += b; quiet = time.time()
        lines = buf.decode('utf-8', 'replace').splitlines()
        keys = ('FPGA BUILD', 'FPGA HODINY', 'FPGA SAMOKONTROLA', 'FPGA FW', 'TDC:', 'CITANI HRAN')
        return [l.strip() for l in lines if any(k in l for k in keys)] or ['(v `status` zadne radky FPGA)']
    finally:
        s.close()


def verdict(samples, expect, tol):
    """('OK'|'SPATNE'|'BEZ MERENI', median, max_rel) -- ciste-logicke (selftest)."""
    if not samples:
        return 'BEZ MERENI', None, None
    f = [x[0] for x in samples]
    rel = [abs(v / expect - 1.0) for v in f]
    return ('OK' if max(rel) <= tol else 'SPATNE'), statistics.median(f), max(rel)


def selftest():
    ok = verdict([], 1e7, 1e-6)[0] == 'BEZ MERENI'
    ok &= verdict([(10000008.0, 1, 0), (10000008.2, 2, 0)], 10000008.0, 1e-6)[0] == 'OK'
    v = verdict([(10000008.0, 1, 0), (1578628.0, 2, 0)], 10000008.0, 1e-6)        # L-0141
    ok &= v[0] == 'SPATNE' and v[2] > 0.8
    print('selftest fpga_load_test: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--fs', help='bitstream .fs')
    ap.add_argument('--expect', type=float, help='ocekavany kmitocet CH_A [Hz]')
    ap.add_argument('--tol', type=float, default=1e-6, help='mez relativni odchylky (vychozi 1e-6)')
    ap.add_argument('--n', type=int, default=4, help='pocet nahrani (L-0141: aspon 4)')
    ap.add_argument('--mode', choices=('sram', 'flash'), default='sram')
    ap.add_argument('--samples', type=int, default=8, help='mereni na kolo')
    ap.add_argument('--settle', type=float, default=8.0, help='cekani po nahrani [s]')
    ap.add_argument('--host', default='10.0.0.106')
    ap.add_argument('--uart', help='COM port konzole CM7 (volitelne, cte `status`)')
    ap.add_argument('--cable-index', help='predano programmer_cli (vic kabelu)')
    ap.add_argument('--log', default='impl/load_test.log')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.fs or a.expect is None:
        ap.error('--fs a --expect jsou povinne')
    if not os.path.exists(a.fs):
        print('bitstream %s neexistuje' % a.fs); return 2
    extra = ['--cable-index', a.cable_index] if a.cable_index else []
    os.makedirs(os.path.dirname(os.path.abspath(a.log)), exist_ok=True)
    log = open(a.log, 'a', encoding='utf-8')
    log.write('\n=== %s  %s  mode=%s n=%d expect=%.3f\n' % (time.strftime('%Y-%m-%d %H:%M:%S'), a.fs, a.mode, a.n, a.expect))
    rows, bad = [], 0
    for k in range(a.n):
        op = 2 if a.mode == 'sram' else (6 if k == 0 else 1)
        ok, dt, out = program(a.fs, op, extra)
        log.write('--- kolo %d, programmer op %d (%.1f s), rc ok=%s\n%s\n' % (k + 1, op, dt, ok, out[-2000:]))
        if not ok:
            rows.append((k + 1, op, 'NAHRANI SELHALO', None, None, ''))
            bad += 1
            continue
        time.sleep(a.settle)
        s, note = sse_samples(a.host, a.samples, timeout=a.settle + 20.0)
        if s is None:
            print('web pristroje %s nedostupny: %s' % (a.host, note)); return 2
        v, med, mx = verdict(s, a.expect, a.tol)
        if v != 'OK':
            bad += 1
        st = uart_status(a.uart) if a.uart else []
        rows.append((k + 1, op, v, med, mx, note))
        log.write('    %s median=%s max_rel=%s %s\n' % (v, med, mx, note))
        for l in st:
            log.write('    | %s\n' % l)
        print('kolo %d/%d (op %d): %-10s %s  %s' % (k + 1, a.n, op, v,
              ('median %.3f Hz, max odchylka %.2e' % (med, mx)) if med is not None else '', note))
        for l in st:
            print('    | %s' % l)
    print('\nSOUHRN: spravne %d z %d nahrani (%s, %s)' % (a.n - bad, a.n, os.path.basename(a.fs), a.mode))
    log.write('SOUHRN: spravne %d z %d\n' % (a.n - bad, a.n))
    log.close()
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
