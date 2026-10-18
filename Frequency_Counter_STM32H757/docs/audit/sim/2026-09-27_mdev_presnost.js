/* F-0182: jak zrychlit webovou mdev() BEZ ztraty presnosti.
 * Navrh v nalezu = prefixove soucty FAZE C[k] = sum x[i]. Jenze faze x je
 * kumulativni soucet y a pri driftu roste kvadraticky, C pak kubicky — rozdil
 * dvou velkych C je spatne podmineny (rusi se cislice). Porovnani tri variant
 * proti naivni referenci (SP1065, soucet m druhych diferenci):
 *   (a) prefix faze C[k]            — navrh z nalezu
 *   (b) klouzave okno nad d_k = x[k+2m]-2x[k+m]+x[k]
 *       (d_k jsou presne cleny ADEV pro tentyz m; linearni faze v nich zmizi)
 * Scenare: bily sum, silny drift + slaby sum, velky offset (y neni centrovane).
 * Beh: node 2026-09-27_mdev_presnost.js */
function phase(y, t0) { const x = [0]; for (let i = 0; i < y.length; i++) x.push(x[i] + y[i] * t0); return x; }
function ref(y, t0) {
  const x = phase(y, t0), N = y.length, o = []; let m = 1;
  while (3 * m <= N) {
    let s = 0, c = 0;
    for (let j = 0; j + 3 * m <= N + 1; j++) {
      let a = 0; for (let k = j; k < j + m; k++) a += x[k + 2 * m] - 2 * x[k + m] + x[k];
      a /= m; s += a * a; c++;
    }
    const t = m * t0; o.push({ tau: t, sig: Math.sqrt(s / (2 * t * t * c)), n: c }); m *= 2;
  }
  return o;
}
function prefX(y, t0) {
  const x = phase(y, t0), N = y.length, C = [0], o = []; let m = 1;
  for (let i = 0; i < x.length; i++) C.push(C[i] + x[i]);
  while (3 * m <= N) {
    let s = 0, c = 0;
    for (let j = 0; j + 3 * m <= N + 1; j++) {
      const a = ((C[j + 3 * m] - C[j + 2 * m]) - 2 * (C[j + 2 * m] - C[j + m]) + (C[j + m] - C[j])) / m;
      s += a * a; c++;
    }
    const t = m * t0; o.push({ tau: t, sig: Math.sqrt(s / (2 * t * t * c)), n: c }); m *= 2;
  }
  return o;
}
function winD(y, t0) {
  const x = phase(y, t0), N = y.length, d = [], o = []; let m = 1;
  while (3 * m <= N) {
    const K = N - 2 * m; let s = 0, c = 0, w = 0;
    for (let i = 0; i <= K; i++) d[i] = x[i + 2 * m] - 2 * x[i + m] + x[i];
    for (let i = 0; i < m; i++) w += d[i];
    for (let j = 0; j + 3 * m <= N + 1; j++) {
      const a = w / m; s += a * a; c++;
      if (j + m <= K) w += d[j + m] - d[j];
    }
    const t = m * t0; o.push({ tau: t, sig: Math.sqrt(s / (2 * t * t * c)), n: c }); m *= 2;
  }
  return o;
}
let seed = 0x2468ace;
function rnd() {
  seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296 - 0.5;
}
const N = 2000;
const CASES = [
  ['bily FM 1e-9',                  i => 1e-9 * rnd()],
  ['drift 1e-9 za okno + sum 1e-13', i => 1e-9 * (i / N - 0.5) + 1e-13 * rnd()],
  ['drift 1e-8 + sum 1e-14',         i => 1e-8 * (i / N - 0.5) + 1e-14 * rnd()],
  ['offset 1e-6 + sum 1e-13',        i => 1e-6 + 1e-13 * rnd()],
];
function worst(a, b) {
  let w = 0;
  for (let i = 0; i < a.length; i++) {
    if (a[i].n !== b[i].n) return Infinity;
    w = Math.max(w, Math.abs(a[i].sig / b[i].sig - 1));
  }
  return w;
}
let bad = 0;
for (const [lbl, gen] of CASES) {
  const y = []; for (let i = 0; i < N; i++) y.push(gen(i));
  const r = ref(y, 0.25), ea = worst(prefX(y, 0.25), r), eb = worst(winD(y, 0.25), r);
  if (!(eb < 1e-9)) bad++;
  console.log('  ' + lbl.padEnd(34) + ' prefix faze: ' + ea.toExponential(1)
    + '   okno nad d: ' + eb.toExponential(1));
}
{
  const y = []; for (let i = 0; i < N; i++) y.push(1e-9 * rnd());
  const T = (f, n) => { const t0 = process.hrtime.bigint(); for (let k = 0; k < n; k++) f(y, 0.25);
    return Number(process.hrtime.bigint() - t0) / 1e6 / n; };
  const tr = T(ref, 20), tw = T(winD, 200);
  console.log('  rychlost N=2000: naivni ' + tr.toFixed(2) + ' ms, okno nad d ' + tw.toFixed(3)
    + ' ms (' + (tr / tw).toFixed(0) + 'x)');
}
console.log(bad ? 'CHYBA: okno nad d neni presne' : 'okno nad d: rel. chyba < 1e-9 ve vsech scenarich');
process.exitCode = bad ? 1 : 0;
