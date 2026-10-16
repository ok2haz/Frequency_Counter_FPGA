import csv, sys
sys.path.insert(0, r"C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_STM32H757\docs\audit\data")
import importlib.util
spec = importlib.util.spec_from_file_location("wa", r"C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_STM32H757\docs\audit\data\2026-10-15_warmup_an.py")
# modul pri importu spusti run(); proto jen vlastni nacteni a sklon
def load(p):
    out = []
    for r in csv.DictReader(open(p, newline="")):
        try:
            if r["ok"] != "1": continue
            out.append((int(r["uptime_s"]), float(r["temp_ocxo_c"]), float(r["temp_board_c"])))
        except (ValueError, KeyError):
            pass
    return out

def slope(rows, i, win=120):
    t1 = rows[i][0]; j = i
    while j > 0 and t1 - rows[j - 1][0] <= win: j -= 1
    if j == i: return 0.0
    return (rows[i][1] - rows[j][1]) / (rows[i][0] - rows[j][0]) * 60.0

def ready(rows, i, por, slope_max, min_s_por=900, min_s_rst=300, dt_min=10.0, dt_check=True):
    t, o, b = rows[i]
    if dt_check and (o - b) < dt_min: return False
    return t >= (min_s_por if por else min_s_rst) and abs(slope(rows, i)) < slope_max

base = "C:/GitHub/Frequency_Counter_FPGA/Frequency_Counter_STM32H757/docs/audit/data/"
for name, por in (("M3b_studeny", True), ("M3c_teply", False)):
    rows = load(base + "2026-10-15_%s.csv" % name)
    def first_stable(fn):
        for i in range(len(rows)):
            if all(fn(k) for k in range(i, len(rows))): return rows[i][0]
        return None
    old = first_stable(lambda k: ready(rows, k, por, 0.08, 300, 300, dt_check=False))
    new = first_stable(lambda k: ready(rows, k, por, 0.3))
    flap = sum(1 for k in range(len(rows)) if rows[k][0] >= 900 and not ready(rows, k, por, 0.3))
    print("%-12s por=%s  stare pravidlo (300 s, 0,08): hotovo od %s s | nove (%s s, 0,3, dT>=10): hotovo od %s s | radku po 900 s bez 'hotovo': %d"
          % (name, por, old, 900 if por else 300, new, flap))
# mrtva pec: teplota plasti = deska + 1 C, sklon 0
rows = [(t, 29.0 + 1.0, 29.0) for t in range(0, 1200, 5)]
print("mrtva pec (dT=1 C, sklon 0): stare pravidlo hotovo od", next(r[0] for i, r in enumerate(rows) if ready(rows, i, True, 0.08, 300, 300, dt_check=False)),
      "s | nove:", next((r[0] for i, r in enumerate(rows) if ready(rows, i, True, 0.3)), "nikdy"))
