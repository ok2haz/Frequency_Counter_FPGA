"""#369 krok 1d: tb_top_v3 s koherentnimi hodinami 10/100 MHz pri ruzne pevne fazi (TB_PH_PS), domena aplikace = clk_ref_10m
(bez APP_OSC, jako zkusebni bitstreamy t386/t387, kde se interval A->B rozbijel). Hleda okno faze, kde test selze nebo
kde se vysledky (sirka pulzu, zpozdeni A/B, chyby) lisi od ostatnich fazi.
RTL simulace nema metastabilitu ani zpozdeni vedeni: najde jen FUNKCNI zavislost na fazi (nesynchronizovany prechod,
zachyceni ve stejnem taktu). Casovou rezervu prechodu resi STA (krok 1c).
Pouziti (z adresare Frequency_Counter_FPGA_Module):  python sim/sweep_phase_v3.py [test] [krok_ps] [paralelne]
  test = topv3pw (vychozi; zpozdeni a sirka A/B) nebo topv3 (zakladni mereni)."""
import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = os.path.join(ROOT, 'sim')
SRC = os.path.join(ROOT, 'src')
BASE = ['-DSIM_TOP', '-DSIM_LUT_PS=800', '-DSIM_IDX', '-DSIM_TAP_MEAN=39', '-DSIM_TAP_SPREAD=15', '-DSIM_BREAK_IDX=268',
        '-DSIM_AUTOCAL']
TESTS = {   # shodne s sim/run.ps1
    'topv3pw': ['-DTB_MODE=3', '-DTB_FA=5.0e6', '-DTB_WA=37300.0'],
    'topv3':   ['-DTB_NFR=14', '-DTB_FB=13.300161e6'],
}
RTL = ['top_v3.v', 'v3_core.v', 'app_v3.v', 'spi_slave_phy_v3.v', 'spi_app.v', 'tdc.v']
FILES = ([os.path.join(SIM, f) for f in ('gowin_models.v', 'gowin_models_ext.v')] + [os.path.join(SRC, f) for f in RTL]
         + [os.path.join(SIM, 'tb_top_v3.sv')])


def run(test, ph, tmp):
    vvp = os.path.join(tmp, '%s_%05d.vvp' % (test, ph))
    c = subprocess.run(['iverilog', '-g2012', '-I', SIM, '-s', 'tb_top_v3'] + BASE + TESTS[test] + ['-DTB_PH_PS=%d' % ph,
                       '-o', vvp] + FILES, capture_output=True, text=True)
    if c.returncode != 0:
        return ph, 'COMPILE FAIL', c.stderr[-400:]
    r = subprocess.run(['vvp', '-n', vvp], capture_output=True, text=True, cwd=tmp)
    out = r.stdout
    os.remove(vvp)
    verdict = 'PASS' if re.search(r'^PASS', out, re.M) else 'FAIL'
    keep = [ln for ln in out.splitlines() if re.search(r'PASS|FAIL|CHYBA|chyb|max \||sirka pulzu|zpozdeni A/B|A->B|skok', ln)]
    return ph, verdict, '\n'.join(keep[-12:])


def main():
    test = sys.argv[1] if len(sys.argv) > 1 else 'topv3pw'
    step = int(sys.argv[2]) if len(sys.argv) > 2 else 500
    par = int(sys.argv[3]) if len(sys.argv) > 3 else 10
    phases = list(range(0, 10000, step))
    with tempfile.TemporaryDirectory() as tmp, ThreadPoolExecutor(par) as ex:
        res = sorted(ex.map(lambda p: run(test, p, tmp), phases))
    nfail = 0
    for ph, v, txt in res:
        rel = (ph + 5000) % 10000
        print('=== TB_PH_PS %5d (clk100 %4.1f ns po clk10): %s' % (ph, rel / 1000.0, v))
        for ln in txt.splitlines():
            print('    ' + ln)
        nfail += v != 'PASS'
    print('SOUHRN %s: %d fazi, selhalo %d' % (test, len(res), nfail))


if __name__ == '__main__':
    main()
