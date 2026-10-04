# Mereni chyby delky okna na desce: N po sobe jdoucich DATA ramcu (fpgaraw),
# z kazdeho edges (abs 20) a dt_a (abs 118, jednotky T/16384). Kmitocet generatoru
# se odhadne jako median f_i, chyba okna = dt_i - edges_i / f_ref (v ps).
# POZOR: obsahuje i nestabilitu generatoru a OCXO za ~0,25 s a drift za dobu mereni.
import serial, time, re, statistics as st, sys
N = int(sys.argv[1]) if len(sys.argv) > 1 else 80
s = serial.Serial('COM10', 115200, timeout=0.3)
def run(c, w=0.6):
    s.reset_input_buffer(); s.write(c + b'\r\n'); t = time.time(); d = b''
    while time.time() - t < w: d += s.read(2000)
    return d.decode('utf-8', 'replace')
vals = []; last = None; t0 = time.time()
while len(vals) < N and time.time() - t0 < 240:
    r = run(b'fpgaraw')
    by = []
    for l in r.splitlines():
        if l.startswith('[') and ']' in l:
            by += [int(x, 16) for x in l.split(']', 1)[1].split()]
    if len(by) < 128: continue
    seq = int.from_bytes(bytes(by[4:8]), 'little')
    if seq == last: continue
    last = seq
    e = int.from_bytes(bytes(by[20:28]), 'little'); d = int.from_bytes(bytes(by[118:124]), 'little')
    if e and d: vals.append((seq, e, d))
f = [e * 1.6384e12 / d for _, e, d in vals]
fref = st.median(f)
err = [(d - e / fref * 1.6384e12) * 625 / 1024 for _, e, d in vals]
print(f'ramcu {len(vals)}, f_ref {fref:.5f} Hz, sigma f {st.stdev(f):.5f} Hz ({st.stdev(f)/fref:.2e})')
print(f'chyba okna: sigma {st.stdev(err):.0f} ps, min {min(err):.0f}, max {max(err):.0f} ps')
srt = sorted(abs(x) for x in err)
print(f'|chyba| median {srt[len(srt)//2]:.0f} ps, 90. percentil {srt[int(len(srt)*0.9)]:.0f} ps')
print('prvnich 20 chyb [ps]:', ' '.join(f'{x:.0f}' for x in err[:20]))
