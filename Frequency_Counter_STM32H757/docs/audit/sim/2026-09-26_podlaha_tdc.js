/* Overeni vzorcu podlahy citace (screen_main.c `adev_floor_base`).
 * Model: casove znacky kvantovane TDC krokem, chyba kazde znacky rovnomerna
 * v <-tdc/2, +tdc/2> a nezavisla; navazujici okna sdileji hranicni znacku.
 * y_k = (x_{k+1} - x_k)/tau0. Estimatory = prepis vzorcu z adev_stage_kind
 * (ADEV/HDEV/MDEV nad frekvencnimi daty). Srovnani s:
 *   ADEV tdc/(2 tau), HDEV 0,527 tdc/tau, MDEV tdc/(2 tau sqrt m).
 */
let seed = 777;
function rnd() { seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }
const TDC = 2.5e-9, TAU0 = 1, N = 60000;
const x = []; for (let k = 0; k <= N; k++) x.push((rnd() - 0.5) * TDC);
const y = []; for (let k = 0; k < N; k++) y.push((x[k + 1] - x[k]) / TAU0);
function adev(m) { let a = 0, n = 0; for (let j = 0; j + 2 * m <= N; j++) {
  let s = 0; for (let i = j; i < j + m; i++) s += y[i + m] - y[i]; a += s * s; n++; }
  return Math.sqrt(a / (2 * m * m * n)); }
function hdev(m) { let a = 0, n = 0; for (let j = 0; j + 3 * m <= N; j++) {
  let s = 0; for (let i = j; i < j + m; i++) s += y[i + 2 * m] - 2 * y[i + m] + y[i]; a += s * s; n++; }
  return Math.sqrt(a / (6 * m * m * n)); }
function mdev(m) { let a = 0, n = 0; for (let j = 0; j + 3 * m - 1 <= N - 1 + 1 && j + 3 * m - 1 < N + 0; j++) {
  let s = 0; for (let i = j; i < j + m; i++) for (let k = i; k < i + m; k++) s += y[k + m] - y[k];
  if (j + 3 * m - 2 >= N) break; a += s * s; n++; }
  return Math.sqrt(a / (2 * m * m * m * m * n)); }
let bad = 0;
for (const m of [1, 2, 5]) {
  const tau = m * TAU0;
  const r = [
    ['ADEV', adev(m), TDC / (2 * tau)],
    ['HDEV', hdev(m), 0.52704628 * TDC / tau],
    ['MDEV', mdev(m), TDC / (2 * tau * Math.sqrt(m))],
  ];
  for (const [n, sim, f] of r) {
    const rel = sim / f;
    const ok = Math.abs(rel - 1) < 0.03;
    if (!ok) bad++;
    console.log((ok ? '  ok    ' : '  CHYBA ') + n + ' m=' + m + ': simulace ' + sim.toExponential(3)
      + '  vzorec ' + f.toExponential(3) + '  pomer ' + rel.toFixed(3));
  }
}
console.log(bad ? 'CHYBA' : 'vse OK'); process.exitCode = bad ? 1 : 0;
