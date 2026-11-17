"""Hostovy port A2 (adev_feed_into + adev_acc_update + adev_ring_kind) -- overeni rovnosti akumulatoru a okennich estimatoru
vcetne MDEV s fazi E na stage >= 1, zlomu (since) a pretoceni ringu. Port je doslovny (stejne indexy)."""
import math
import random

RING, MMAX, STAGES = 60, 9, 4
LAMBDA = 1.0 - 1.0 / 600.0


class Stage:
    def __init__(self):
        self.ring = [0.0] * RING
        self.eph = [0.0] * RING
        self.head = 0
        self.count = 0
        self.e_n = 0
        self.acc = 0.0
        self.acc_e = 0.0
        self.acc_n = 0
        self.acc_e_ok = 0
        self.qs = [[0.0] * MMAX for _ in range(3)]
        self.qn = [[0] * MMAX for _ in range(3)]
        self.fs = [[0.0] * MMAX for _ in range(3)]
        self.fn = [[0.0] * MMAX for _ in range(3)]
        self.tot = 0
        self.since = 0


def base_n(sg, n):
    return (sg.head - n + RING) % RING


def at(sg, b, i):
    return sg.ring[(b + i) % RING]


def eat(sg, b, i):
    return sg.eph[(b + i) % RING]


def add(sg, k, m, c2):
    sg.qs[k][m - 1] += c2
    sg.qn[k][m - 1] += 1
    sg.fs[k][m - 1] = sg.fs[k][m - 1] * LAMBDA + c2
    sg.fn[k][m - 1] = sg.fn[k][m - 1] * LAMBDA + 1.0


def acc_update(sg, pb):
    M = min(sg.since, sg.count)
    for m in range(1, MMAX + 1):
        if M >= 2 * m:
            b = base_n(sg, 2 * m)
            inner = sum(at(sg, b, i + m) - at(sg, b, i) for i in range(m))
            add(sg, 0, m, inner * inner)
        if M >= 3 * m:
            b = base_n(sg, 3 * m)
            inner = sum(at(sg, b, i + 2 * m) - 2.0 * at(sg, b, i + m) + at(sg, b, i) for i in range(m))
            add(sg, 2, m, inner * inner)
        need = 3 * m if pb else 3 * m - 1
        Mm = min(sg.e_n, M) if pb else M
        if Mm >= need:
            b = base_n(sg, need)
            inner = 0.0
            for i in range(m):
                for k in range(i, i + m):
                    inner += at(sg, b, k + m) - at(sg, b, k)
            if pb:
                for l in range(m):
                    inner += eat(sg, b, l + 2 * m) - 2.0 * eat(sg, b, l + m) + eat(sg, b, l)
            add(sg, 1, m, inner * inner)


def feed(pyr, s0, v, e, e_ok):
    for s in range(s0, len(pyr)):
        sg = pyr[s]
        sg.ring[sg.head] = v
        sg.eph[sg.head] = e
        sg.head = (sg.head + 1) % RING
        if sg.count < RING:
            sg.count += 1
        sg.e_n = (min(sg.e_n + 1, RING) if e_ok else 0)
        sg.tot += 1
        sg.since += 1
        acc_update(sg, s > 0)
        if sg.acc_n == 0:
            sg.acc_e_ok = 1
        sg.acc_e += sg.acc + e
        sg.acc += v
        if not e_ok:
            sg.acc_e_ok = 0
        sg.acc_n += 1
        if sg.acc_n < 10:
            return
        v = sg.acc / 10.0
        e = sg.acc_e / 100.0
        e_ok = sg.acc_e_ok
        sg.acc = 0.0
        sg.acc_e = 0.0
        sg.acc_n = 0


def brk(pyr):
    for sg in pyr:
        sg.since = 0
        sg.acc_n = 0
        sg.acc = 0.0
        sg.acc_e = 0.0


def ring_kind(sg, m, kind, pb):
    M = sg.count
    b = base_n(sg, M)
    acc = 0.0
    n = 0
    if kind == 2:
        if M < 3 * m + 1:
            return 0.0
        for j in range(0, M - 3 * m + 1):
            inner = sum(at(sg, b, i + 2 * m) - 2.0 * at(sg, b, i + m) + at(sg, b, i) for i in range(j, j + m))
            acc += inner * inner
            n += 1
        return math.sqrt(acc / (6.0 * m * m * n)) if n else 0.0
    if kind == 1:
        Mm = sg.e_n if pb else M
        if Mm < 3 * m + 1:
            return 0.0
        bm = base_n(sg, Mm) if pb else b
        last = Mm - 3 * m if pb else Mm - 3 * m + 1
        for j in range(0, last + 1):
            inner = 0.0
            for i in range(j, j + m):
                for k in range(i, i + m):
                    inner += at(sg, bm, k + m) - at(sg, bm, k)
            if pb:
                for l in range(j, j + m):
                    inner += eat(sg, bm, l + 2 * m) - 2.0 * eat(sg, bm, l + m) + eat(sg, bm, l)
            acc += inner * inner
            n += 1
        return math.sqrt(acc / (2.0 * m ** 4 * n)) if n else 0.0
    if M < 2 * m + 1:
        return 0.0
    for j in range(0, M - 2 * m + 1):
        inner = sum(at(sg, b, i + m) - at(sg, b, i) for i in range(j, j + m))
        acc += inner * inner
        n += 1
    return math.sqrt(acc / (2.0 * m * m * n)) if n else 0.0


def acc_kind(sg, m, kind, pb, win=False):
    n = sg.qn[kind][m - 1]
    nmin = 3 if (kind == 1 and not pb) else 2
    if n < nmin:
        return 0.0
    c = 2.0 * m * m if kind == 0 else (6.0 * m * m if kind == 2 else 2.0 * m ** 4)
    if win:
        fn = sg.fn[kind][m - 1]
        return math.sqrt(sg.fs[kind][m - 1] / (c * fn)) if fn > 0 else 0.0
    return math.sqrt(sg.qs[kind][m - 1] / (c * n))


def compare(pyr, label, tol=1e-9):
    bad = 0
    for s, sg in enumerate(pyr):
        pb = s > 0
        for kind in range(3):
            for m in range(1, MMAX + 1):
                w = ring_kind(sg, m, kind, pb)
                a = acc_kind(sg, m, kind, pb)
                if w > 0.0 and abs(a / w - 1.0) > tol:
                    bad += 1
                    print("  ROZDIL %s stage %d kind %d m %d: okno %.6e acc %.6e" % (label, s, kind, m, w, a))
                if w == 0.0 and a != 0.0:
                    bad += 1
                    print("  ROZDIL(prah) %s stage %d kind %d m %d: okno 0 acc %.6e (qn %d, count %d, e_n %d)" % (
                        label, s, kind, m, a, sg.qn[kind][m - 1], sg.count, sg.e_n))
    return bad


random.seed(7)
tot_bad = 0
# 1) bez zlomu, ring neprelit na zadne stage nad nulou: stage0 tot<=60 a stage1 <= 60 => 590 vzorku
pyr = [Stage() for _ in range(STAGES)]
for i in range(590):
    feed(pyr, 0, 3e-5 + 1e-9 * random.uniform(-0.5, 0.5), 0.0, 1)
tot_bad += compare(pyr, "bez zlomu, stage1 60 polozek (stage0 pretekla -> vynechat)") if False else 0
# porovnavat jen stage, ktere se neprelily
for s in (1, 2):
    sg = pyr[s]
    print("stage %d: tot %d count %d" % (s, sg.tot, sg.count))
for s in (1,):
    for kind in range(3):
        for m in range(1, MMAX + 1):
            w = ring_kind(pyr[s], m, kind, True)
            a = acc_kind(pyr[s], m, kind, True)
            if w > 0 and abs(a / w - 1) > 1e-9:
                tot_bad += 1
                print("ROZDIL stage1", kind, m, w, a)
            if w == 0 and a != 0:
                tot_bad += 1
                print("ROZDIL(prah) stage1", kind, m, a)
print("1) bez zlomu: rozdilu %d" % tot_bad)

# 2) zlom uprostred: po zlomu se clenu preskocenych pres mezeru ma rovnat 0 a ostatni souhlasi s okennim odhadem JEN
#    kdyz ring po zlomu obsahuje jen polozky za zlomem. Overime pocitadla: n_ADEV(m) = max(0, since-2m+1) po zlomu.
pyr = [Stage() for _ in range(STAGES)]
for i in range(300):
    feed(pyr, 0, 3e-5 + 1e-9 * random.uniform(-0.5, 0.5), 0.0, 1)
q0 = [pyr[0].qn[0][m - 1] for m in range(1, MMAX + 1)]
brk(pyr)
for i in range(40):
    feed(pyr, 0, 3e-5 + 1e-9 * random.uniform(-0.5, 0.5), 0.0, 1)
ok2 = True
for m in range(1, MMAX + 1):
    exp_new = max(0, 40 - 2 * m + 1)
    got_new = pyr[0].qn[0][m - 1] - q0[m - 1]
    if got_new != exp_new:
        ok2 = False
        print("  zlom: m %d ocekavano nove cleny %d, ziskano %d" % (m, exp_new, got_new))
print("2) zlom (ADEV pocty po zlomu, stage 0): %s" % ("OK" if ok2 else "CHYBA"))

# 3) stage 1 pri rekonstrukci z logu (e_ok = 0): ADEV roste, MDEV ne
pyr = [Stage() for _ in range(STAGES)]
for i in range(50):
    feed(pyr, 1, 3e-5 + 1e-9 * random.uniform(-0.5, 0.5), 0.0, 0)
print("3) rekonstrukce: ADEV qn(m=1) = %d (ocekavano 49), MDEV qn(m=1) = %d (ocekavano 0), e_n = %d" % (
    pyr[1].qn[0][0], pyr[1].qn[1][0], pyr[1].e_n))
ok3 = (pyr[1].qn[0][0] == 49 and pyr[1].qn[1][0] == 0)
# a po ni zive vzorky (s fazi): MDEV zacne az po 3m polozek s E
for i in range(200):
    feed(pyr, 0, 3e-5 + 1e-9 * random.uniform(-0.5, 0.5), 0.0, 1)
print("   po 200 zivych vzorcich: stage1 e_n = %d, MDEV qn(m=1) = %d" % (pyr[1].e_n, pyr[1].qn[1][0]))
print("   => %s" % ("OK" if ok3 else "CHYBA"))

# 4) okno: rychle sleduje zmenu rozptylu
pyr = [Stage() for _ in range(STAGES)]
for i in range(6000):
    feed(pyr, 0, (1e-8 if i < 3000 else 1e-9) * random.gauss(0, 1), 0.0, 1)
cum, win = acc_kind(pyr[0], 1, 0, False), acc_kind(pyr[0], 1, 0, False, True)
print("4) od resetu %.3e (cekano ~7.1e-9), okno %.3e (cekano ~1e-9)" % (cum, win))
print("VYSLEDEK:", "OK" if (tot_bad == 0 and ok2 and ok3 and 5e-9 < cum < 9e-9 and 0.8e-9 < win < 1.4e-9) else "CHYBA")
