"""run_doplnek.py -- spusti cilene testy ze sim/run_ps1_doplnek.txt stejne jako sim/run_sim.py (2026-10-25).

Radky zaznamu z doplnku se vlozi do docasne kopie sim/run.ps1 (pred radek "dalsi testy pridavej sem") a pusti se
sim/run_sim.py nad docasnou kopii korene (src/ a sim/ se kopiruji, repo se nemeni). Po zarazeni radku do run.ps1
staci run_sim.py samotny.

Pouziti (z adresare Frequency_Counter_FPGA_Module):
    python tools/run_doplnek.py . [jmeno ...]        # bez jmen = vsechny testy run.ps1 + doplnku
Vystup .vvp do adresare podle promenne SIM_OUT (vychozi docasny adresar).
"""
import os, shutil, subprocess, sys, tempfile

root = os.path.abspath(sys.argv[1])
names = sys.argv[2:]
with open(os.path.join(root, 'sim', 'run_ps1_doplnek.txt'), encoding='utf-8') as f:
    lines = [l for l in f.read().splitlines() if l.strip() and not l.strip().startswith('#')]
with tempfile.TemporaryDirectory() as td:
    shutil.copytree(os.path.join(root, 'sim'), os.path.join(td, 'sim'))
    shutil.copytree(os.path.join(root, 'src'), os.path.join(td, 'src'))
    p = os.path.join(td, 'sim', 'run.ps1')
    with open(p, encoding='utf-8') as f:
        s = f.read()
    mark = '    # další testy přidávej sem, jak přibývají moduly'
    if mark not in s:
        sys.exit('run.ps1: chybi radek "%s"' % mark.strip())
    s = s.replace(mark, '\n'.join(lines) + '\n' + mark)
    with open(p, 'w', encoding='utf-8', newline='\n') as f:
        f.write(s)
    env = dict(os.environ)
    env.setdefault('SIM_OUT', td)
    r = subprocess.run([sys.executable, os.path.join(td, 'sim', 'run_sim.py'), td] + names, env=env)
    sys.exit(r.returncode)
