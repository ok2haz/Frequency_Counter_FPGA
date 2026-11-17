"""rel_repro.py -- priprava reprodukce VYDANEHO bitstreamu v3 do abl/rel_<commit>/ (2026-10-31, STATUS #347).

Zdroje src/ a v3/Counter_FPGA_v3.gprj z gitu (commit = identita bitstreamu, "FPGA BUILD" v UART `status`), src/build_id.vh
s BLD_TIME a BLD_GIT jako ve v3/build_v3.tcl (BLD_TIME je i seed rozmisteni, L-0222), build.tcl se stejnymi volbami jako
build_v3.tcl a vydany .fs z ab_test/ jako v3/impl/pnr/Counter_FPGA_v3.fs. Nasledne tools/dsp_sta.py rel_<commit> sestaveni
zopakuje s reporty a overi shodu datovych radku bitstreamu (2026-10-31: 796c009 i b3fbc32 shodne).
Neulozene zmeny (bit 31 BLD_GIT) se reprodukovat nedaji -- jen ciste sestaveni z commitu.

Pouziti (z adresare Frequency_Counter_FPGA_Module):
    python tools/rel_repro.py <commit, 7 znaku> "<RRRR-MM-DD HH:MM:SS>" <soubor v ab_test>
    (cas sestaveni UTC: MANIFEST.md nebo UART "FPGA BUILD")
"""
import calendar, io, os, shutil, subprocess, sys, tarfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import abl_cls

ROOT = abl_cls.ROOT
MONO = os.path.dirname(ROOT)
MOD = os.path.basename(ROOT)
if len(sys.argv) != 4:
    sys.exit(__doc__)
commit, when, fsname = sys.argv[1], sys.argv[2], sys.argv[3]
bt = calendar.timegm(time.strptime(when, '%Y-%m-%d %H:%M:%S'))
bg = int(commit[:7], 16) & 0x0FFFFFFF
d = os.path.join(abl_cls.ABL, 'rel_' + commit[:7])
if os.path.isdir(d):
    shutil.rmtree(d, onerror=lambda f, p, e: (os.chmod(p, 0o700), f(p)))
os.makedirs(d)
tar = subprocess.run(['git', '-C', MONO, 'archive', '--format=tar', commit, MOD + '/src', MOD + '/v3/Counter_FPGA_v3.gprj'],
                     capture_output=True, check=True).stdout
with tarfile.open(fileobj=io.BytesIO(tar)) as t:
    for m in t.getmembers():
        if m.isfile():
            out = os.path.join(d, *m.name.split('/')[1:])
            os.makedirs(os.path.dirname(out), exist_ok=True)
            open(out, 'wb').write(t.extractfile(m).read())
open(os.path.join(d, 'src', 'build_id.vh'), 'w').write(
    "// rel_repro.py: identita vydaneho bitstreamu\nlocalparam [31:0] BLD_TIME = 32'h%08X;\nlocalparam [31:0] BLD_GIT  = 32'h%08X;\n"
    % (bt, bg))
fwd = d.replace('\\', '/')
open(os.path.join(d, 'build.tcl'), 'w').write(f'''open_project "{fwd}/v3/Counter_FPGA_v3.gprj"
set_option -top_module top_v3
set_option -use_sspi_as_gpio 1
set_option -use_mspi_as_gpio 1
set_option -use_cpu_as_gpio 0
set_option -use_mode_as_gpio 0
set_option -use_ready_as_gpio 0
set_option -use_done_as_gpio 0
set_option -use_reconfign_as_gpio 0
set_option -use_i2c_as_gpio 0
set_option -place_option 2
set_option -route_option 1
set_option -gen_text_timing_rpt 1

run all
''')
pnr = os.path.join(d, 'v3', 'impl', 'pnr')
os.makedirs(pnr)
shutil.copy(os.path.join(ROOT, 'ab_test', fsname), os.path.join(pnr, 'Counter_FPGA_v3.fs'))
print('pripraveno %s: BLD_TIME 0x%08X BLD_GIT 0x%08X, vzor %s' % (d, bt, bg, fsname))
