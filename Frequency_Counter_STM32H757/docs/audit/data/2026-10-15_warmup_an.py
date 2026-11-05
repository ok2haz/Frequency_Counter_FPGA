import csv, sys, statistics as st

def load(p):
    rows = []
    with open(p, newline="") as f:
        for r in csv.DictReader(f):
            try:
                if r["ok"] != "1":
                    continue
                rows.append((int(r["uptime_s"]), float(r["freq_hz"]), float(r["temp_ocxo_c"]),
                             float(r["temp_board_c"]), float(r["vc_mv"]), int(r["seq_meas"])))
            except (ValueError, KeyError):
                pass
    return rows

def slope(rows, i, win=120):
    """sklon teploty OCXO [C/min] z bodu v okne `win` s koncici v i (jako sensor_hist: posledni - prvni / span)"""
    t1 = rows[i][0]
    j = i
    while j > 0 and t1 - rows[j - 1][0] <= win:
        j -= 1
    if j == i:
        return None
    return (rows[i][2] - rows[j][2]) / (rows[i][0] - rows[j][0]) * 60.0

def run(p, label, fref=None):
    rows = [r for r in load(p) if not (r[1] != r[1])]
    # jen radky s merenim (freq_hz platne)
    print("==", label, p.split("/")[-1], "radku", len(rows))
    if not rows:
        return
    # reference = medián poslednich 60 vzorku
    tail = [r[1] for r in rows[-60:]]
    f_end = st.median(tail)
    print("  konec: uptime %d s, freq medián %.4f Hz, T_ocxo %.2f C" % (rows[-1][0], f_end, rows[-1][2]))
    print("  uptime  T_ocxo  slope[C/min]  freq[Hz]   dev_od_konce[ppb]   Vc[mV]")
    last = -999
    for i, r in enumerate(rows):
        if r[0] - last < 30 and i != len(rows) - 1:
            continue
        last = r[0]
        s = slope(rows, i)
        # vyhlazeni frekvence: median 5 okolnich vzorku
        a = max(0, i - 2); b = min(len(rows), i + 3)
        fm = st.median([x[1] for x in rows[a:b]])
        dev = (fm - f_end) / 10e6 * 1e9
        print("  %5d  %6.2f  %s  %12.4f  %10.1f   %5.0f" % (r[0], r[2], ("%+7.3f" % s) if s is not None else "   --  ", fm, dev, r[4]))
    # kdy slope < 0.08 poprve natrvalo
    first_ok = None
    for i in range(len(rows)):
        s = slope(rows, i)
        if s is not None and abs(s) < 0.08 and rows[i][0] >= 300:
            if all((slope(rows, k) is None) or abs(slope(rows, k)) < 0.08 for k in range(i, len(rows))):
                first_ok = i
                break
    if first_ok is not None:
        r = rows[first_ok]
        a = max(0, first_ok - 2); b = min(len(rows), first_ok + 3)
        fm = st.median([x[1] for x in rows[a:b]])
        print("  warmup_ready() natrvalo pravda od uptime %d s (T %.2f C); freq dev od konce %.1f ppb" % (r[0], r[2], (fm - f_end) / 10e6 * 1e9))
    else:
        print("  warmup_ready() natrvalo pravda: nenastalo v zaznamu")

base = "C:/GitHub/Frequency_Counter_FPGA/Frequency_Counter_STM32H757/docs/audit/data/"
run(base + "2026-10-15_M3b_studeny.csv", "M3b studeny")
run(base + "2026-10-15_M3c_teply.csv", "M3c teply (po flashi STM, FPGA bezelo)")
