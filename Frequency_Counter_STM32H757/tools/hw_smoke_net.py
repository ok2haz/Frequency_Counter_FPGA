"""Sitovy smoke test pristroje (HTTP + SCPI, jen cteni): python tools/hw_smoke_net.py [IP]

Overuje /api/state, /api/stab, /api/sats, /api/errlog, /api/fpga, /api/log (souvislost davek), SCPI dotazy, 40 rychlych dotazu a SPA.
Nic na pristroji nemeni. Navratovy kod 0 = vse PASS. Vznikl 2026-10-18 pri HW testech v0.27.2 (docs/audit/2026-10-18_hw-testy.md).
"""
import json
import sys
import time
import urllib.request

IP = sys.argv[1] if len(sys.argv) > 1 else "10.0.0.106"


def get(path, timeout=15):
    t = time.time()
    with urllib.request.urlopen("http://%s%s" % (IP, path), timeout=timeout) as r:
        b = r.read()
        return r.status, b, time.time() - t


def post(path, data, timeout=10):
    req = urllib.request.Request("http://%s%s" % (IP, path), data=data.encode(), method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read().decode().strip()


res = []


def check(name, ok, info=""):
    res.append((name, ok))
    print("%-48s %s  %s" % (name, "PASS" if ok else "FAIL", info))


# 1) /api/state: pole, ktera ma server emitovat
st, b, dt = get("/api/state")
s = json.loads(b)
check("GET /api/state (JSON, %d B, %.0f ms)" % (len(b), dt * 1000), st == 200 and "freq_hz" in s)
check("  freq_hz 10 MHz +-1 ppb", abs(s["freq_hz"] - 1e7) < 10.0, "%.5f Hz" % s["freq_hz"])
check("  ref_ppb_x10 (v0.27.2) / ref_ppb_x100 (v0.29)", ("ref_ppb_x10" in s) or ("ref_ppb_x100" in s), str(s.get("ref_ppb_x10", s.get("ref_ppb_x100"))))
check("  gpsdo blok: LOCK + locked", s.get("gpsdo", {}).get("locked") == 1, json.dumps({k: s["gpsdo"][k] for k in ("state", "locked", "y_ppt", "x_ns")}) if "gpsdo" in s else "chybi")
check("  GPS fix 3D, >= 6 druzic", s["gps"]["fix_mode"] == 3 and s["gps"]["num_sat"] >= 6, "%s sat, HDOP %s" % (s["gps"]["num_sat"], s["gps"]["hdop"]))
check("  signal_lost == false, spi_ok", (not s["signal_lost"]) and s["spi_ok"])

# 2) dalsi endpointy
for path, key in (("/api/stab", "p"), ("/api/sats", None), ("/api/errlog?n=5&from=0", None), ("/api/fpga", None)):
    try:
        st, b, dt = get(path)
        ok = st == 200 and len(b) > 10
        if ok:
            j = json.loads(b)
            if key:
                ok = key in j
        check("GET %s (%d B, %.0f ms)" % (path, len(b), dt * 1000), ok)
    except Exception as e:
        check("GET %s" % path, False, repr(e))

# 3) stab: ADEV z firmwaru
st, b, dt = get("/api/stab")
j = json.loads(b)
p1 = [x for x in j["p"] if abs(x[0] - 1) < 1e-9]
check("  /api/stab: sigma(1 s) v rozsahu 5e-11..5e-10", bool(p1) and 5e-11 < p1[0][1] < 5e-10, str(p1[0][:3]) if p1 else "")
check("  /api/stab: filtr odlehlych (ro/rn/rm)", all(k in j for k in ("ro", "rn")), "ro=%s rn=%s" % (j.get("ro"), j.get("rn")))

# 4) /api/log: souvislost casu v jedne davce (decimace) + nepreklada se
st, b, dt = get("/api/log?win=21600&n=48&from=0&env=0")
j = json.loads(b)
t = [x[0] for x in j["p"]]
mono = all(t[i] > t[i + 1] for i in range(len(t) - 1))      # od nejnovejsiho
check("GET /api/log win=21600 (%d bodu, %.0f ms)" % (len(t), dt * 1000), st == 200 and len(t) >= 40 and mono, "rozpeti %.1f h" % ((t[0] - t[-1]) / 3600.0))
# v0.27.2: klient posila celé okno kazde davce -> davky se prekryvaji (oprava ve v0.29.0: win/DLCHUNKS)
st, b2, dt2 = get("/api/log?win=21600&n=48&from=528&env=0")
t2 = [x[0] for x in json.loads(b2)["p"]]
check("  server: win=cele okno pokryje okno v JEDNE davce (proto klient v0.29 posila win/4)", t2[0] > t[-1], "davka 1 zacina %.1f h pred koncem davky 0 -> v0.27.2 se davky prekryvaji" % ((t2[0] - t[-1]) / 3600.0))
st, b3, dt3 = get("/api/log?win=5400&n=48&from=0&env=0")
t3 = [x[0] for x in json.loads(b3)["p"]]
span3 = (t3[0] - t3[-1]) / 3600.0
check("  oprava v0.29: davka win/4 pokryje ~1/4 okna", 1.2 < span3 < 1.6, "%.2f h" % span3)

# 5) SCPI cteni (jen dotazy)
for q, exp in (("*IDN?", "OK2HAZ"), ("SENS:FREQ:GATE?", None), ("MEAS:FREQ?", None), ("SYST:ERR?", None), ("*OPC?", "1")):
    try:
        r = post("/api/scpi", q)
        check("SCPI %s -> %s" % (q, r[:40]), (exp is None and len(r) > 0) or (exp is not None and exp in r))
    except Exception as e:
        check("SCPI %s" % q, False, repr(e))

# 6) zatez: 40 rychlych dotazu po sobe, zadna chyba, rozumna doba
errs = 0
tmax = 0.0
t0 = time.time()
for i in range(40):
    try:
        st, b, dt = get("/api/state", timeout=5)
        tmax = max(tmax, dt)
        if st != 200:
            errs += 1
    except Exception:
        errs += 1
check("40 rychlych GET /api/state (chyb %d, max %.0f ms, celkem %.1f s)" % (errs, tmax * 1000, time.time() - t0), errs == 0 and tmax < 1.5)

# 7) SPA
st, b, dt = get("/")
check("GET / (SPA %d B, %.0f ms)" % (len(b), dt * 1000), st == 200 and len(b) > 100000 and b"<title>" in b)
print()
print("PASS %d / %d" % (sum(1 for _, o in res if o), len(res)))
sys.exit(0 if all(o for _, o in res) else 1)
