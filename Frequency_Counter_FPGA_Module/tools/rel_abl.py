"""rel_abl.py -- vyda ZKUSEBNI bitstream v3 z abl/<var>@k do ab_test/ (STATUS #347).

Metriky z .tr / .rpt.txt sestaveni (fpga_release.parse_*), FW z src/app_v3.v, nejistota z timing_v3.sdc, synteticky
BLD_TIME z build_id.vh (seed rozmisteni, L-0222). Jmeno TEST_FW_0x<FW>_v3_<nazev>_<YYYYMMDD-HHMM UTC>.fs, v MANIFEST.md
radek "TEST (<popis>), ne vydani". Cas sestaveni = "Created Time" z hlavicky .fs (lokalni -> UTC). Nic jineho nemeni;
na rozdil od fpga_release.py neprojde branou casovani -- zkusebni bitstream vybira clovek po `pnr_seeds report`.

Pouziti (z adresare Frequency_Counter_FPGA_Module):
    python tools/rel_abl.py <var@k, napr. appOsc@3> <nazev> "<popis do MANIFEST>"
"""
import datetime, hashlib, os, re, shutil, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fpga_release as fr

if len(sys.argv) != 4:
    sys.exit(__doc__)
abl_name, shortname, popis = sys.argv[1], sys.argv[2], sys.argv[3]
d = os.path.join(fr.ROOT, 'abl', abl_name)
pnr = os.path.join(d, 'v3', 'impl', 'pnr')
base = 'Counter_FPGA_v3'
fs = os.path.join(pnr, base + '.fs')
if not os.path.exists(fs):
    sys.exit('bitstream chybi: %s (sestaveni neprobehlo?)' % fs)
tr = fr.parse_tr(fr.read(os.path.join(pnr, base + '.tr')))
rpt = fr.parse_rpt(fr.read(os.path.join(pnr, base + '.rpt.txt')))
unc = fr.parse_unc(fr.read(os.path.join(d, 'src', 'timing_v3.sdc')))
bt, bg = fr.parse_build_id(fr.read(os.path.join(d, 'src', 'build_id.vh')))
fw = fr.parse_fw(fr.read(os.path.join(d, 'src', 'app_v3.v')))
md5 = hashlib.md5(open(fs, 'rb').read()).hexdigest()
# cas sestaveni z hlavicky .fs ("//Created Time: Fri Oct 30 20:23:54 2026"), lokalni -> UTC
m = re.search(r'Created Time:\s*(.+)', open(fs, encoding='ascii', errors='replace').read(600))
utc = datetime.datetime.strptime(m.group(1).strip(), '%a %b %d %H:%M:%S %Y').astimezone().astimezone(datetime.timezone.utc)
if tr['setup_viol'] or tr['hold_viol']:
    print('POZOR: sestaveni ma poruseni casovani (setup %s / hold %s) -- nevydavam'
          % (tr['setup_viol'], tr['hold_viol']))
    sys.exit(1)
name = 'TEST_FW_0x%04X_v3_%s_%s.fs' % (fw, shortname, utc.strftime('%Y%m%d-%H%M'))
shutil.copyfile(fs, os.path.join(fr.ROOT, 'ab_test', name))
row = ('| %s | %s | 0x%04X (v3) TEST (%s), ne vydani | %s | build_id synteticky 0x%08X (pnr_seeds %s) '
       '| %d %% | %d %% | %s | %s | %s | %s |\n') % (
    name, md5, fw, popis, utc.strftime('%Y-%m-%d %H:%M:%S'), bt, abl_name,
    rpt['cls'], rpt['reg'], tr['setup_ws'], tr['hold_ws'], tr['fmax'], unc)
open(os.path.join(fr.ROOT, 'ab_test', 'MANIFEST.md'), 'a', encoding='utf-8').write(row)
print('%s  md5 %s' % (name, md5))
print('CLS %d%% reg %d%% setup %s hold %s Fmax %s unc %s' % (rpt['cls'], rpt['reg'], tr['setup_ws'], tr['hold_ws'], tr['fmax'], unc))
