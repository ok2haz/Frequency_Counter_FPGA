import re, math
import sys
p = sys.argv[1] if len(sys.argv) > 1 else 'docs/audit/2026-10-04_tdc-hist.txt'
A = [0]*256; B = [0]*256
for k, a, b in re.findall(r'^(\d+);(\d+);(\d+)', open(p).read(), re.M):
    A[int(k)] = int(a); B[int(k)] = int(b)
T = 10000.0  # perioda vzorkovani [ps]
for name, H in (('A', A), ('B', B)):
    N = sum(H)
    w = [h / N * T for h in H]                 # sirka binu [ps]
    nz = [i for i, h in enumerate(H) if h]
    first, last = nz[0], nz[-1]
    used = [w[i] for i in range(first, last + 1)]
    zeros = sum(1 for x in used if x == 0)
    srt = sorted(((w[i], i) for i in nz), reverse=True)
    # kvantizacni chyba casu jedne hrany: hrana je rovnomerne v binu sirky w_i,
    # tabulka vraci stred binu -> rozptyl w_i^2/12, vazeno pravdepodobnosti binu p_i = w_i/T
    var = sum((x / T) * x * x / 12.0 for x in w)
    sig1 = math.sqrt(var)
    # okno = rozdil dvou nezavislych hran
    sigw = math.sqrt(2) * sig1
    # bez nejvetsiho binu
    var_wo = sum((w[i] / T) * w[i] ** 2 / 12.0 for i in nz if i != srt[0][1])
    print(f'== kanal {name}: N={N}, kody {first}..{last}, obsazenych {len(nz)}, prazdnych uvnitr {zeros}')
    print('   nejvetsi biny [ps]: ' + ', '.join(f'k={i}:{x:.0f}' for x, i in srt[:6]))
    print(f'   median sirky obsazeneho binu {sorted(w[i] for i in nz)[len(nz)//2]:.0f} ps, prumer na kod (vc. prazdnych) {T/(last-first+1):.0f} ps')
    print(f'   kvantizacni sigma 1 hrany {sig1:.0f} ps -> okno (2 hrany) {sigw:.0f} ps')
    print(f'   z toho bez nejvetsiho binu: hrana {math.sqrt(var_wo):.0f} ps (podil nejvetsiho binu na rozptylu {100*(var-var_wo)/var:.0f} %)')
