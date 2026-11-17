# Pokryl by model nejistoty HOLD (meas_present.h mp_ref_ppb, gp_ref_core ve freertos_task_fpga.c) skutecnou zmenu kodu DAC,
# kdyby holdover zacal v libovolnem okamziku noci?  Skutecna chyba holdoveru po h hodinach = |kod(t0+h) - kod(t0)| * K
# (smycka v LOCK drzi kmitocet, takze zmena kodu = zmena kmitoctu volneho OCXO; metodika F-0292).
# Model: u = sqrt(u0^2 + (1,4*rw)^2*h + (drift*h)^2), drift = |LS sklon kodu za <= 2 h| * K, rw = rms zmeny kodu za 1800 s * K * sqrt(2).
# Pomer skutecne/model je na K nezavisly (krome u0, ktere je po 1 h zanedbatelne).
# Pouziti: python -I noc_hold_model.py <csv> [<csv> ...]
import sys
import numpy as np
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
K_PPB = 0.0385          # ppb / LSB (fyzicka strmost F-0293)
RW_K = 1.4              # MP_REF_HOLD_RW_K
HIST_S = 7200.0         # historie smycky 2 h
RW_LAG_S = 1800.0


def load(path):
    t, c, ref = [], [], []
    with open(path, encoding='utf-8') as f:
        hdr = f.readline().strip().split(';')
        for ln in f:
            v = dict(zip(hdr, ln.rstrip('\n').split(';')))
            try:
                t.append(float(v['unix'])); c.append(float(v['code']))
            except (KeyError, ValueError):
                continue
            try:
                ref.append(float(v.get('ref_ppb', '')))
            except ValueError:
                ref.append(np.nan)
    return np.array(t), np.array(c), np.array(ref)


for path in sys.argv[1:]:
    t, c, ref = load(path)
    print('=' * 100)
    print(path)
    dt = np.median(np.diff(t))
    lag = int(round(RW_LAG_S / dt))
    out = {h: [] for h in (0.5, 1.0, 2.0, 3.0, 4.0)}
    det = []
    for i0 in range(len(t)):
        if t[i0] - t[0] < HIST_S:
            continue
        w = (t > t[i0] - HIST_S) & (t <= t[i0])
        tw, cw = t[w], c[w]
        slope = np.polyfit((tw - tw[0]) / 3600.0, cw, 1)[0]             # LSB/h
        drift = abs(slope) * K_PPB                                       # ppb/h
        d = cw[lag:] - cw[:-lag]
        rw = np.sqrt(np.mean(d * d)) * K_PPB * np.sqrt(3600.0 / RW_LAG_S) * RW_K   # ppb/sqrt(h)
        u0 = ref[i0] if np.isfinite(ref[i0]) else 0.011
        for h in out:
            j = np.searchsorted(t, t[i0] + h * 3600.0)
            if j >= len(t) or abs(t[j] - (t[i0] + h * 3600.0)) > 90:
                continue
            act = abs(c[j] - c[i0]) * K_PPB
            pred = np.sqrt(u0 * u0 + rw * rw * h + (drift * h) ** 2)
            out[h].append((act, pred, drift, rw, t[i0]))
            if h == 2.0:
                det.append((t[i0], act, pred, drift, rw))
    print('  h [h] |   n | skutecna chyba ppb: median / p95 / max | model ppb: median | pomer skut/model: median / p95 / max | '
          'nad 1x | nad 2x')
    for h, v in out.items():
        if not v:
            continue
        a = np.array(v)
        r = a[:, 0] / a[:, 1]
        print('  %4.1f  | %3d | %.3f / %.3f / %.3f | %.3f | %.2f / %.2f / %.2f | %3.0f %% | %3.0f %%' % (
            h, len(a), np.median(a[:, 0]), np.percentile(a[:, 0], 95), a[:, 0].max(), np.median(a[:, 1]),
            np.median(r), np.percentile(r, 95), r.max(), 100.0 * np.mean(r > 1.0), 100.0 * np.mean(r > 2.0)))
        from datetime import datetime as _dt
        bad = [_dt.fromtimestamp(a[k, 4]).strftime('%H:%M') for k in np.where(r > 2.0)[0]]
        if bad:
            print('         starty nad 2x: %s' % ', '.join(bad))
    if det:
        a = np.array([(x[1], x[2], x[3], x[4]) for x in det])
        print('  vstup do HOLD -> po 2 h: drift modelu min %.3f max %.3f ppb/h, rw modelu min %.3f max %.3f ppb/sqrt(h)' % (
            a[:, 2].min(), a[:, 2].max(), a[:, 3].min(), a[:, 3].max()))
        k = int(np.argmax(a[:, 0] / a[:, 1]))
        from datetime import datetime
        print('  nejhorsi start (2 h): %s skutecne %.3f ppb, model %.3f ppb (drift %.3f ppb/h, rw %.3f)' % (
            datetime.fromtimestamp(det[k][0]).strftime('%m-%d %H:%M'), det[k][1], det[k][2], det[k][3], det[k][4]))
