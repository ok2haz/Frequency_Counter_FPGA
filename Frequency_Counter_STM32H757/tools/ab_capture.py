"""Zachyti N sekund soucasnych mereni kanalu A a B z /api/state (jen cteni): python tools/ab_capture.py <sekund> <vystup.npz> [IP]. Vyhodnoceni: tools/ab_analyze.py."""
import json
import sys
import time
import urllib.request

import numpy as np

IP = sys.argv[3] if len(sys.argv) > 3 else "10.0.0.106"
DUR = float(sys.argv[1]) if len(sys.argv) > 1 else 600.0
rec = {}
t0 = time.time()
n_err = 0
while time.time() - t0 < DUR:
    try:
        with urllib.request.urlopen("http://%s/api/state" % IP, timeout=3) as r:
            s = json.loads(r.read())
        q = s["seq_meas"]
        if q not in rec and s.get("freq_hz") is not None and s.get("freq16_hz") is not None and s.get("channel") == 0:
            rec[q] = (s["freq_hz"], s["freq16_hz"])
    except Exception:
        n_err += 1
    time.sleep(0.05)
seq = np.array(sorted(rec))
A = np.array([rec[q][0] for q in seq])
B = np.array([rec[q][1] for q in seq])
np.savez(sys.argv[2] if len(sys.argv) > 2 else "ab.npz", seq=seq, A=A, B=B)
print("zachyceno %d oken z %d s (chyb dotazu %d)" % (len(seq), DUR, n_err))
