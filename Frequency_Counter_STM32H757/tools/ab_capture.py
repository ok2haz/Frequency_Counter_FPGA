"""Zachyti N sekund soucasnych mereni kanalu A a B z /api/state (jen cteni): python tools/ab_capture.py <sekund> <vystup.npz> [IP]. Vyhodnoceni: tools/ab_analyze.py.
Soubor se prubezne ulozi kazdych 300 s (pri preruseni dlouheho zaznamu se neztrati vse)."""
import json
import sys
import time
import urllib.request

import numpy as np

IP = sys.argv[3] if len(sys.argv) > 3 else "10.0.0.106"
DUR = float(sys.argv[1]) if len(sys.argv) > 1 else 600.0
OUT = sys.argv[2] if len(sys.argv) > 2 else "ab.npz"
SAVE_EVERY = 300.0
rec = {}
t0 = time.time()
t_save = t0
n_err = 0


def save():
    seq = np.array(sorted(rec))
    A = np.array([rec[q][0] for q in seq])
    B = np.array([rec[q][1] for q in seq])
    np.savez(OUT, seq=seq, A=A, B=B)
    return len(seq)


while time.time() - t0 < DUR:
    try:
        with urllib.request.urlopen("http://%s/api/state" % IP, timeout=3) as r:
            s = json.loads(r.read())
        q = s["seq_meas"]
        if q not in rec and s.get("freq_hz") is not None and s.get("freq16_hz") is not None and s.get("channel") == 0:
            rec[q] = (s["freq_hz"], s["freq16_hz"])
    except Exception:
        n_err += 1
    if time.time() - t_save >= SAVE_EVERY:
        t_save = time.time()
        print("%.0f s: %d oken, chyb dotazu %d" % (t_save - t0, save(), n_err), flush=True)
    time.sleep(0.05)
print("zachyceno %d oken z %d s (chyb dotazu %d)" % (save(), DUR, n_err))
