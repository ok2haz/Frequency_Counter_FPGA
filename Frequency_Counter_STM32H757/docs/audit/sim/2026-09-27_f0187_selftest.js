/* F-0187: vernypřepis noveho selftestu screen_main_selftest (fazova pyramida)
 * do JS — na cili bez sondy nejde pustit. Stejna LCG (uint32), stejna data,
 * adev_feed_into + adev_ring_kind(MDEV, pb=1) proti prime definici nad prumery
 * faze bloku. Musi vyjit |got/ref - 1| < 1e-5 pro m = 1, 3, 9 (float navrat).
 * Beh: node 2026-09-27_f0187_selftest.js */
const RING = 60;
function stage() { return { ring: new Float64Array(RING), eph: new Float64Array(RING), head: 0, count: 0,
  e_n: 0, acc: 0, acc_e: 0, acc_n: 0, acc_e_ok: 0 }; }
function feedInto(st, nst, s0, v, e, eok) {
  for (let s = s0; s < nst; s++) {
    const sg = st[s];
    sg.ring[sg.head] = v; sg.eph[sg.head] = e;
    sg.head = (sg.head + 1) % RING;
    if (sg.count < RING) sg.count++;
    sg.e_n = eok ? Math.min(sg.e_n + 1, RING) : 0;
    if (sg.acc_n === 0) sg.acc_e_ok = 1;
    sg.acc_e += sg.acc + e; sg.acc += v;
    if (!eok) sg.acc_e_ok = 0;
    if (++sg.acc_n < 10) return;
    v = sg.acc / 10; e = sg.acc_e / 100; eok = sg.acc_e_ok;
    sg.acc = 0; sg.acc_e = 0; sg.acc_n = 0;
  }
}
function baseN(sg, n) { return (sg.head - n + RING) % RING; }
function at(sg, b, i) { let k = b + i; if (k >= RING) k -= RING; return sg.ring[k]; }
function eat(sg, b, i) { let k = b + i; if (k >= RING) k -= RING; return sg.eph[k]; }
function mdev(sg, m, pb) {
  const M = sg.count, Mm = pb ? sg.e_n : M;
  if (Mm < 3 * m + 1) return 0;
  const bm = pb ? baseN(sg, Mm) : baseN(sg, M), last = pb ? Mm - 3 * m : Mm - 3 * m + 1;
  let acc = 0, n = 0;
  for (let j = 0; j <= last; j++) {
    let inner = 0;
    for (let i = j; i < j + m; i++) for (let k = i; k < i + m; k++) inner += at(sg, bm, k + m) - at(sg, bm, k);
    if (pb) for (let l = j; l < j + m; l++) inner += eat(sg, bm, l + 2 * m) - 2 * eat(sg, bm, l + m) + eat(sg, bm, l);
    acc += inner * inner; n++;
  }
  return Math.fround(Math.sqrt(Math.fround(acc / (2 * m ** 4 * n))));
}
const tp = [stage(), stage()], xb = [];
let r = 777 >>> 0, x = 0, sx = 0;
for (let i = 0; i < 400; i++) {
  r = (Math.imul(r, 1103515245) + 12345) >>> 0;
  const y = 3e-5 + 1e-9 * ((r >>> 8) / 16777216 - 0.5);
  if (i % 10 === 0) sx = 0;
  sx += x; x += y;
  if (i % 10 === 9) xb.push(sx / 10);
  feedInto(tp, 2, 0, y, 0, 1);
}
let bad = 0;
for (const m of [1, 3, 9]) {
  let a2 = 0, n2 = 0;
  for (let k = 0; k + 3 * m <= xb.length; k++) {
    let a = 0; for (let l = k; l < k + m; l++) a += xb[l + 2 * m] - 2 * xb[l + m] + xb[l];
    a /= m; a2 += a * a; n2++;
  }
  const ref = Math.sqrt(a2 / (2 * (10 * m) ** 2 * n2)), got = mdev(tp[1], m, 1);
  const rel = Math.abs(got / ref - 1), ok = ref > 0 && rel < 1e-5; if (!ok) bad++;
  console.log((ok ? '  ok    ' : '  CHYBA ') + 'm=' + m + '  pyramida ' + got.toExponential(6) + '  primo ' + ref.toExponential(6) + '  rel ' + rel.toExponential(1));
}
/* pozitivni kontrola: bez E (stara pyramida) musi test SELHAT */
{ let a2 = 0, n2 = 0; const m = 3;
  for (let k = 0; k + 3 * m <= xb.length; k++) { let a = 0; for (let l = k; l < k + m; l++) a += xb[l + 2 * m] - 2 * xb[l + m] + xb[l]; a /= m; a2 += a * a; n2++; }
  const ref = Math.sqrt(a2 / (2 * (10 * m) ** 2 * n2));
  const sg = tp[1]; const saveE = sg.eph.slice(); sg.eph.fill(0);
  const got = mdev(sg, m, 1); sg.eph.set(saveE);
  const rel = Math.abs(got / ref - 1);
  console.log('  pozitivni kontrola (E = 0, jako drive): rel ' + rel.toExponential(1) + (rel > 1e-5 ? '  -> test by SELHAL (spravne)' : '  -> CHYBA: test nic nekontroluje'));
  if (!(rel > 1e-5)) bad++;
}
feedInto(tp, 2, 1, 3e-5, 0, 0);
const off = mdev(tp[1], 1, 1) === 0; if (!off) bad++;
console.log((off ? '  ok    ' : '  CHYBA ') + 'vzorek z logu (bez faze) vypne MDEV nad stage 1');
console.log(bad ? 'CHYBA' : 'vse OK'); process.exitCode = bad ? 1 : 0;
