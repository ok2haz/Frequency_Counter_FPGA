"""raw_fetch.py - stahne SUROVA mereni z datove cache pristroje (GET /api/raw, CM7/CM4 >= v0.20.0, IPC 26).

Kazde mereni prave jednou (kanal A, kmitocet v uHz), chronologicky, od kurzoru (absolutni index v epose).
Deska drzi ~18 h mereni (262 144 zaznamu); stahnout jde i zpetne, bez pollovani /api/state.
Jen GET, do pristroje nic nezapisuje.

Pouziti:
  python tools/raw_fetch.py --out docs/audit/data/raw.csv               # vse, co deska drzi, a konec
  python tools/raw_fetch.py --out raw.csv --follow --every 10           # pak dal dotahovat po 10 s (Ctrl+C = konec)
Opakovane spusteni se stejnym --out NAVAZE (kurzor z posledniho radku).

CSV: abs,epoch,seq,t_ms,f_hz,flags   (f_hz presne z uHz; flags = SDRAM_LOG_F_*: 1 A platny, 4 STALE, 8 SPIKE)
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request


def get(ip, frm, epoch, n, to=4.0):
    url = "http://%s/api/raw?from=%d&epoch=%d&n=%d" % (ip, frm, epoch, n)
    for pokus in range(20):
        try:
            with urllib.request.urlopen(url, timeout=to) as r:
                return json.loads(r.read().decode("ascii"))
        except urllib.error.HTTPError as e:
            if e.code in (503, 504):          # kanal zaneprazdnen / CM7 neodpovedel -> zkusit znovu
                time.sleep(0.2 + 0.1 * pokus)
                continue
            raise
        except (urllib.error.URLError, TimeoutError, ConnectionError):
            time.sleep(1.0)
    raise RuntimeError("deska neodpovida (%s)" % url)


def resume(path):
    """Kurzor z posledniho radku existujiciho CSV: (epoch, dalsi abs)."""
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        return 0, 0
    with open(path, "rb") as fh:
        fh.seek(0, 2)
        size = fh.tell()
        fh.seek(max(0, size - 4096))
        lines = fh.read().decode("ascii", "replace").strip().splitlines()
    for ln in reversed(lines):
        p = ln.split(",")
        if len(p) >= 2 and p[0].isdigit() and p[1].isdigit():
            return int(p[1]), int(p[0]) + 1
    return 0, 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ip", default="10.0.0.106")
    ap.add_argument("--out", required=True)
    ap.add_argument("--n", type=int, default=100, help="zaznamu na dotaz (max 100)")
    ap.add_argument("--follow", action="store_true", help="po dohnani dal dotahovat")
    ap.add_argument("--every", type=float, default=10.0, help="perioda dotahovani s --follow [s]")
    ap.add_argument("--max-min", type=float, default=0.0, help="s --follow: skoncit po tolika minutach (0 = Ctrl+C)")
    a = ap.parse_args()

    epoch, cur = resume(a.out)
    new = not os.path.exists(a.out) or os.path.getsize(a.out) == 0
    fh = open(a.out, "a", encoding="ascii", newline="")
    if new:
        fh.write("abs,epoch,seq,t_ms,f_hz,flags\n")
    t0 = time.time()
    tot = 0
    seq_gap = 0
    last_seq = None
    gate = None
    try:
        while True:
            try:
                j = get(a.ip, cur, epoch, a.n)
            except RuntimeError as e:
                if not a.follow:
                    raise
                # 2026-10-26: noc 25./26. 10. web (CM4) neodpovidal 3 min 20 s a logger skoncil -> v rezimu --follow se zkousi dal
                print("\n%s: %s, zkousim znovu za %.0f s" % (time.strftime("%H:%M:%S"), e, a.every), file=sys.stderr)
                time.sleep(a.every)
                continue
            if not j.get("ready"):
                print("deska: datova cache nebezi (ready=0)", file=sys.stderr)
            # F-0238: novou radu hlasi deska (newep: jine cislo epochy NEBO kurzor za hlavou = jiny beh se shodnym cislem)
            if (j["epoch"] != epoch and epoch != 0) or j.get("newep"):
                print("NOVA RADA: epocha %d -> %d (zmena brany / SIM / reset logu / restart desky); rada zacina znovu"
                      % (epoch, j["epoch"]))
                last_seq = None
            if j.get("trunc"):
                print("POZOR: zaznamy %d..%d uz byly prepsany (deska drzi jen ~18 h)" % (cur, j["first"] - 1))
            if gate != j["gate_ns"]:
                gate = j["gate_ns"]
                print("epocha %d: hradlo %d ns%s, dostupne abs %d..%d" % (j["epoch"], gate, " SIM" if j.get("sim") else "",
                                                                        j["oldest"], j["head"] - 1))
            epoch = j["epoch"]
            first = j["first"]
            for k, (seq, t_ms, fu, fl) in enumerate(j["r"]):
                if last_seq is not None and seq != last_seq + 1:
                    seq_gap += 1
                last_seq = seq
                fh.write("%d,%d,%d,%d,%d.%06d,%d\n" % (first + k, epoch, seq, t_ms, fu // 1000000, fu % 1000000, fl))
            n = j["n"]
            tot += n
            cur = first + n
            if n > 0 and cur < j["head"]:
                continue                          # jeste neco zbyva -> hned dalsi davka
            fh.flush()
            sys.stdout.write("\r%d zaznamu, kurzor %d / hlava %d, mezer v seq %d   " % (tot, cur, j["head"], seq_gap))
            sys.stdout.flush()
            if not a.follow:
                break
            if a.max_min and time.time() - t0 > a.max_min * 60:
                break
            time.sleep(a.every)
    except KeyboardInterrupt:
        pass
    finally:
        fh.close()
    print("\nKONEC: %d zaznamu do %s, mezer v seq %d (zmeskana mereni FPGA)" % (tot, a.out, seq_gap))


if __name__ == "__main__":
    main()
