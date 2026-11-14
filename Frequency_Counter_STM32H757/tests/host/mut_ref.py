"""Pouziti: CC="<zig cc>" python -I tests/host/mut_ref.py <vystupni adresar>
Pozitivni kontrola host testu t_gpsdo_ref.py: kazda mutace zdroje musi test shodit. Zdroj se po kazde mutaci vraci."""
import io, os, subprocess, sys

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SP = sys.argv[1]            # vystupni adresar host testu (jako u t_gpsdo_ref.py)
F = os.path.join(R, 'CM7', 'Core', 'Src', 'freertos_task_fpga.c')
M = os.path.join(R, 'CM7', 'Core', 'Inc', 'meas_present.h')
env = dict(os.environ)       # CC musi byt nastavene (zig cc, viz CLAUDE.md)
MUT = [
    (F, 'nsat < GPSDO_GNSS_SAT_BAD ||', 'nsat <= GPSDO_GNSS_SAT_BAD ||'),
    (F, 'hdop > GPSDO_GNSS_HDOP_BAD) { q->ok', 'hdop >= GPSDO_GNSS_HDOP_BAD) { q->ok'),
    (F, 'if (++q->run >= GPSDO_GNSS_GOOD_S)', 'if (++q->run > GPSDO_GNSS_GOOD_S)'),
    (F, 'hdop <= GPSDO_GNSS_HDOP_GOOD) {', 'hdop <= GPSDO_GNSS_HDOP_BAD) {'),
    (F, 'nsat >= GPSDO_GNSS_SAT_GOOD &&', 'nsat >= GPSDO_GNSS_SAT_BAD &&'),
    (F, 'if (!fix3d || nsat', 'if (nsat'),
    (F, '!(hdop > 0.0f) || ', ''),
    (F, 'const int32_t *xs = x_ps + (n - wn);', 'const int32_t *xs = x_ps;'),
    (F, '* 1e-3 / (double)GPSDO_HIST_PERIOD_S;          /* ps / bod', '* 1e-3;          /* ps / bod'),
    (F, 'if (n >= GP_REF_YOFF_SHORT) {', 'if (n >= 10) {'),
    (F, '#define GP_REF_YOFF_SHORT 12 ', '#define GP_REF_YOFF_SHORT 30 '),            # F-0294: sklon od 12 bodu
    (M, 'r->lock_yoff_ppb > v) v = r->lock_yoff_ppb;', 'r->lock_yoff_ppb > 1e9) v = r->lock_yoff_ppb;'),   # F-0294: vetsi z udaje a sklonu
    (M, 'r->lock_yoff_ok && r->lock_yoff_ppb > v)', 'r->lock_yoff_ppb > v)'),          # F-0294: nezmereny sklon se nepouzije
    (F, 'if (wn < GP_REF_YOFF_MIN) wn = GP_REF_YOFF_MIN;', ''),
    (M, ' - kf * kf);', ');'),
    (M, 'r->lock_yoff_ppb > kf)', 'r->lock_yoff_ppb > 0.0)'),
    (M, '#define MP_REF_YOFF_K       2.0', '#define MP_REF_YOFF_K       30.0'),
    (M, 'v = (r->entry_ppb > 0.0) ? r->entry_ppb : MP_REF_PPB_LOCK_ENTRY;', 'v = 0.01;'),
    (M, 'r->entry_meas && r->hold_drift_ppb_h > 0.0 && r->hold_rw_ppb_sqrth > 0.0) ? 1 : 0;', 'r->entry_meas && r->hold_drift_ppb_h > 0.0) ? 1 : 0;'),
    (M, 'r->entry_ppb > 0.0 && r->entry_meas && r->hold_drift_ppb_h', 'r->entry_ppb > 0.0 && r->hold_drift_ppb_h'),
    (M, 'u0 * u0 + rw * rw * h + dr * h * dr * h', 'u0 * u0 + rw * rw + dr * h * dr * h'),
    (M, 'u0 * u0 + rw * rw * h + dr * h * dr * h', 'u0 * u0 + dr * h * dr * h'),
    (M, '? MP_REF_HOLD_RW_K * r->hold_rw_ppb_sqrth :', '? r->hold_rw_ppb_sqrth :'),
    (M, '#define MP_REF_PPB_HOLD_RW  0.07', '#define MP_REF_PPB_HOLD_RW  0.03'),
    (F, 'if (n >= GP_REF_RW_MIN && kdac > 0.0) {', 'if (n >= 200 && kdac > 0.0) {'),
    (F, '* sqrt(3600.0 / ((double)GP_REF_RW_LAG', '* sqrt(1800.0 / ((double)GP_REF_RW_LAG'),
    (F, '#define GP_REF_RW_LAG    180 ', '#define GP_REF_RW_LAG    90 '),
    (F, 'code_q16[i + GP_REF_RW_LAG] - (double)code_q16[i]) / 65536.0;', 'code_q16[i + GP_REF_RW_LAG] - (double)code_q16[i]) / 65536.0 / 2;'),
]
bad = 0
for i, (p, old, new) in enumerate(MUT):
    orig = io.open(p, encoding='utf-8', newline='').read()
    if orig.count(old) != 1:
        print('M%02d KOTVA NENALEZENA (%d): %s' % (i, orig.count(old), old[:50])); bad += 1; continue
    try:
        io.open(p, 'w', encoding='utf-8', newline='').write(orig.replace(old, new))
        r = subprocess.run([sys.executable, '-I', os.path.join(R, 'tests', 'host', 't_gpsdo_ref.py'), os.path.join(SP, 'href')],
                           capture_output=True, text=True, env=env, timeout=300)
    finally:
        io.open(p, 'w', encoding='utf-8', newline='').write(orig)
    caught = r.returncode != 0
    print('M%02d %s  %s' % (i, 'zachycena' if caught else 'PREZILA  ', old[:60]))
    bad += 0 if caught else 1
r = subprocess.run([sys.executable, '-I', os.path.join(R, 'tests', 'host', 't_gpsdo_ref.py'), os.path.join(SP, 'href')],
                   capture_output=True, text=True, env=env, timeout=300)
print('bez mutace:', r.stdout.strip().splitlines()[-1], 'rc', r.returncode)
print('VYSLEDEK: %s' % ('vsechny mutace zachyceny' if bad == 0 and r.returncode == 0 else 'NEZACHYCENO %d' % bad))
