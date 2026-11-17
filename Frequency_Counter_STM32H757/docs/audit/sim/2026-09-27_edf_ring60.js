/* Kriticky audit 2026-09-27: plati vzorce EDF (adev_edf_alpha, screen_main.c)
 * i v NOVEM rozsahu po zhusteni Allanu — ring stage M = 60 a m = 1..9?
 * Puvodni overeni (2026-09-26_edf_typ_sumu.js) pokrylo jen M = 24/120, m = 1, 2, 5.
 * Empiricka EDF = 2 E[s2]^2 / Var[s2] z Monte Carla, tytez generatory sumu
 * (Kasdin pro blikave). Hlasi pomer vzorec / empirie; > 1 = pas UZSI, nez data dovoli.
 * Beh: node 2026-09-27_edf_ring60.js */
let seed = 20260927;
function rnd() { seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }
function gauss() { let u = 0, v = 0; while (!u) u = rnd(); while (!v) v = rnd();
  return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v); }
function kasdin(n, a, K) {
  const h = [1]; for (let k = 1; k < K; k++) h.push(h[k - 1] * (k - 1 + a / 2) / k);
  const w = []; for (let i = 0; i < n + K; i++) w.push(gauss());
  const out = new Float64Array(n);
  for (let i = 0; i < n; i++) { let s = 0; for (let k = 0; k < K; k++) s += h[k] * w[i + K - k]; out[i] = s; }
  return out;
}
function phaseSeg(alpha, N) {
  const x = new Float64Array(N);
  if (alpha === 2) { for (let i = 0; i < N; i++) x[i] = gauss(); return x; }
  if (alpha === 1) return kasdin(N, 1, 512);
  let y;
  if (alpha === 0) { y = new Float64Array(N - 1); for (let i = 0; i < N - 1; i++) y[i] = gauss(); }
  else if (alpha === -1) y = kasdin(N - 1, 1, 512);
  else { y = new Float64Array(N - 1); let s = 0; for (let i = 0; i < N - 1; i++) { s += gauss(); y[i] = s; } }
  for (let i = 1; i < N; i++) x[i] = x[i - 1] + y[i - 1];
  return x;
}
function oavar(x, m) { const N = x.length; let s = 0, n = 0;
  for (let j = 0; j + 2 * m < N; j++) { const d = x[j + 2 * m] - 2 * x[j + m] + x[j]; s += d * d; n++; }
  return s / (2 * m * m * n); }
function edfFormula(alpha, M, m) {            /* 1:1 adev_edf_alpha (vc. spodni meze 1) */
  const N = M + 1; let e;
  switch (alpha) {
  case 2:  e = (N + 1) * (N - 2 * m) / (2 * (N - m)); break;
  case 1:  e = Math.exp(Math.sqrt(Math.log((N - 1) / (2 * m)) * Math.log((2 * m + 1) * (N - 1) / 4))); break;
  case -1: e = m === 1 ? 2 * (N - 2) * (N - 2) / (2.3 * N - 4.9) : 5 * N * N / (4 * m * (N + 3 * m)); break;
  case -2: e = (N - 2) / m * ((N - 1) ** 2 - 3 * m * (N - 1) + 4 * m * m) / ((N - 3) ** 2); break;
  default: e = (3 * (N - 1) / (2 * m) - 2 * (N - 2) / N) * 4 * m * m / (4 * m * m + 5);
  }
  return (e >= 1) ? e : 1;
}
const NAMES = { 2: 'bily PM', 1: 'blikavy PM', 0: 'bily FM', '-1': 'blikavy FM', '-2': 'RW FM' };
const TR = 4000, M = 60, MS = [1, 2, 3, 4, 5, 6, 7, 8, 9];
console.log('M = ' + M + ', pomer EDF vzorec/empirie (1,00 = shoda; >1 = pas uzsi, nez data dovoli)');
console.log('  typ          ' + MS.map(m => ('m=' + m).padStart(6)).join(''));
let worst = { r: 1, txt: '' };
for (const alpha of [2, 1, 0, -1, -2]) {
  const v = MS.map(() => []);
  for (let t = 0; t < TR; t++) {
    const x = phaseSeg(alpha, M + 1);
    MS.forEach((m, k) => v[k].push(oavar(x, m)));
  }
  let line = '  ' + NAMES[alpha].padEnd(11);
  MS.forEach((m, k) => {
    const mu = v[k].reduce((a, b) => a + b, 0) / TR;
    const va = v[k].reduce((a, b) => a + (b - mu) ** 2, 0) / (TR - 1);
    const emp = 2 * mu * mu / va, f = edfFormula(alpha, M, m), r = f / emp;
    line += r.toFixed(2).padStart(6);
    const dev = Math.max(r, 1 / r);
    if (dev > Math.max(worst.r, 1 / worst.r)) worst = { r, txt: NAMES[alpha] + ' m=' + m + ' (vzorec ' + f.toFixed(1) + ', empirie ' + emp.toFixed(1) + ')' };
  });
  console.log(line);
}
console.log('nejvetsi odchylka: pomer ' + worst.r.toFixed(2) + ' u ' + worst.txt);
