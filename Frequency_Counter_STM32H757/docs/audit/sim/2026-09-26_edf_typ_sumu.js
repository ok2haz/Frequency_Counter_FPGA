/* Overeni vzorcu EDF overlapping ADEV podle typu sumu (NIST SP1065, Howe-Allan-
 * Barnes, jednoduche aproximace) proti EMPIRICKE EDF z Monte Carla:
 *   edf_emp = 2 * E[s2]^2 / Var[s2]      (s2 = odhad Allanova rozptylu)
 * Typy: alpha = 2 bily PM, 1 blikavy PM, 0 bily FM, -1 blikavy FM, -2 nahodna
 * prochazka FM. Blikave sumy generovany Kasdinovym filtrem (1/f^a) nad dlouhou
 * radou a pak nasekane na nezavisle useky po N fazovych bodech.
 * N = M + 1 (M kmitoctovych vzorku v ringu stage), m = 1, 2, 5.
 */
let seed = 20260926;
function rnd() { seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }
function gauss() { let u = 0, v = 0; while (!u) u = rnd(); while (!v) v = rnd();
  return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v); }

/* Kasdin: rada s PSD ~ 1/f^a (a = 1 blikavy), FIR s delkou K */
function kasdin(n, a, K) {
  const h = [1]; for (let k = 1; k < K; k++) h.push(h[k - 1] * (k - 1 + a / 2) / k);
  const w = []; for (let i = 0; i < n + K; i++) w.push(gauss());
  const out = new Float64Array(n);
  for (let i = 0; i < n; i++) { let s = 0; for (let k = 0; k < K; k++) s += h[k] * w[i + K - k]; out[i] = s; }
  return out;
}
/* fazova rada x (N bodu) pro dany typ */
function phaseSeg(alpha, N) {
  const x = new Float64Array(N);
  if (alpha === 2) { for (let i = 0; i < N; i++) x[i] = gauss(); return x; }
  if (alpha === 1) return kasdin(N, 1, 512);                 /* flicker PM */
  let y;
  if (alpha === 0) { y = new Float64Array(N - 1); for (let i = 0; i < N - 1; i++) y[i] = gauss(); }
  else if (alpha === -1) y = kasdin(N - 1, 1, 512);         /* flicker FM */
  else { y = new Float64Array(N - 1); let s = 0; for (let i = 0; i < N - 1; i++) { s += gauss(); y[i] = s; } }
  for (let i = 1; i < N; i++) x[i] = x[i - 1] + y[i - 1];
  return x;
}
function oavar(x, m) { const N = x.length; let s = 0, n = 0;
  for (let j = 0; j + 2 * m < N; j++) { const d = x[j + 2 * m] - 2 * x[j + m] + x[j]; s += d * d; n++; }
  return s / (2 * m * m * n); }

function edfFormula(alpha, M, m) {
  const N = M + 1;
  switch (alpha) {
  case 2:  return (N + 1) * (N - 2 * m) / (2 * (N - m));
  case 1:  return Math.exp(Math.sqrt(Math.log((N - 1) / (2 * m)) * Math.log((2 * m + 1) * (N - 1) / 4)));
  case -1: return m === 1 ? 2 * (N - 2) * (N - 2) / (2.3 * N - 4.9) : 5 * N * N / (4 * m * (N + 3 * m));
  case -2: return (N - 2) / m * ((N - 1) ** 2 - 3 * m * (N - 1) + 4 * m * m) / ((N - 3) ** 2);
  default: return (3 * (N - 1) / (2 * m) - 2 * (N - 2) / N) * 4 * m * m / (4 * m * m + 5);
  }
}
const NAMES = { 2: 'bily PM', 1: 'blikavy PM', 0: 'bily FM', '-1': 'blikavy FM', '-2': 'RW FM' };
const TR = 3000;
for (const M of [24, 120]) {
  console.log('=== M = ' + M + ' (N = ' + (M + 1) + ') ===');
  for (const alpha of [2, 1, 0, -1, -2]) {
    let line = '  ' + NAMES[alpha].padEnd(11);
    for (const m of [1, 2, 5]) {
      if (M < 2 * m + 1) continue;
      const v = [];
      for (let t = 0; t < TR; t++) v.push(oavar(phaseSeg(alpha, M + 1), m));
      const mean = v.reduce((a, b) => a + b, 0) / TR;
      const vr = v.reduce((a, b) => a + (b - mean) ** 2, 0) / (TR - 1);
      const emp = 2 * mean * mean / vr, fo = edfFormula(alpha, M, m);
      line += '  m=' + m + ': emp ' + emp.toFixed(1).padStart(5) + ' vzorec ' + fo.toFixed(1).padStart(5)
            + ' (' + (fo / emp).toFixed(2) + ')';
    }
    console.log(line);
  }
}
