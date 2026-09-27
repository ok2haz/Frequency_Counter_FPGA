/* Kriticky audit 2026-09-27: MDEV (a z nej TDEV) z decimacni pyramidy.
 * Stage s drzi PRUMERY kmitoctu po 10^s s = faze PODVZORKOVANA po 10^s s.
 * `adev_stage_kind(MDEV)` pak prumeruje m bodu faze po 10^s s, zatimco
 * standardni MDEV (τ0 = 1 s) prumeruje n = m·10^s bodu po 1 s. ADEV a HDEV
 * potrebuji jen prumery kmitoctu pres τ, takze jim podvzorkovani nevadi — MDEV
 * ano, a nejvic u FAZOVEHO sumu, tedy prave tam, kde ma MDEV rozlisovat.
 * Srovnani na tychz datech (cela rada, ne jen ring 60 — jde o STREDNI hodnotu
 * estimatoru, ne o jeho rozptyl):
 *   pyramida: decimace prumerem x10 na stage s, MDEV s m = τ/10^s
 *   presne:   MDEV z plne rady po 1 s, n = τ
 * Beh: node 2026-09-27_mdev_pyramida.js */
let seed = 99;
function rnd() { seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }
function gauss() { let u = 0, v = 0; while (!u) u = rnd(); while (!v) v = rnd();
  return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v); }
function kasdin(n, a, K) {
  const h = [1]; for (let k = 1; k < K; k++) h.push(h[k - 1] * (k - 1 + a / 2) / k);
  const w = new Float64Array(n + K); for (let i = 0; i < n + K; i++) w[i] = gauss();
  const out = new Float64Array(n);
  for (let i = 0; i < n; i++) { let s = 0; for (let k = 0; k < K; k++) s += h[k] * w[i + K - k]; out[i] = s; }
  return out;
}
/* MDEV nad kmitoctovou radou y (τ0 = 1 jednotka), m = nasobek — tentyz vzorec
 * jako adev_stage_kind (inner = ΣΣ(y[k+m]-y[k]), /(2 m^4 n)), prefixove soucty
 * pro rychlost (y centrovana, kratka rada -> presnost staci). */
function mdevY(y, m) {
  const M = y.length; if (M < 3 * m + 1) return NaN;
  const d = new Float64Array(M - m); for (let k = 0; k + m < M; k++) d[k] = y[k + m] - y[k];
  const P = new Float64Array(d.length + 1); for (let k = 0; k < d.length; k++) P[k + 1] = P[k] + d[k];
  const S = new Float64Array(d.length - m + 1); for (let i = 0; i + m <= d.length; i++) S[i] = P[i + m] - P[i];
  const Q = new Float64Array(S.length + 1); for (let i = 0; i < S.length; i++) Q[i + 1] = Q[i] + S[i];
  let acc = 0, n = 0;
  for (let j = 0; j <= M - 3 * m + 1; j++) { const inner = Q[j + m] - Q[j]; acc += inner * inner; n++; }
  return Math.sqrt(acc / (2 * m ** 4 * n));
}
/* ADEV a HDEV tymz vzorcem jako adev_stage_kind — kontrola, ze jim podvzorkovani
 * NEVADI (potrebuji jen prumery kmitoctu pres τ). */
function adevY(y, m) { const M = y.length; let acc = 0, n = 0;
  for (let j = 0; j <= M - 2 * m; j++) { let in_ = 0; for (let i = j; i < j + m; i++) in_ += y[i + m] - y[i]; acc += in_ * in_; n++; }
  return Math.sqrt(acc / (2 * m * m * n)); }
function hdevY(y, m) { const M = y.length; let acc = 0, n = 0;
  for (let j = 0; j <= M - 3 * m; j++) { let in_ = 0; for (let i = j; i < j + m; i++) in_ += y[i + 2 * m] - 2 * y[i + m] + y[i]; acc += in_ * in_; n++; }
  return Math.sqrt(acc / (6 * m * m * n)); }
/* OPRAVA F-0187: fazova pyramida. Kazda stage nese vedle prumeru kmitoctu Ybar
 * i E = (prumer faze bloku - faze na jeho zacatku) / delka bloku (τ0 = 1).
 * Decimace x10: Ybar = Σ Ybar_j / 10, E = Σ (C_j + E_j) / 100, C_j = Σ_{q<j} Ybar_q.
 * MDEV: inner = ΣΣ(Ybar[k+m]-Ybar[k]) + Σ_l (E[l+2m] - 2E[l+m] + E[l]),
 * j <= M-3m (nad stage 0 chybi faze za poslednim blokem), /(2 m^4 n). */
function decimE(Y, E) {
  const Yo = [], Eo = [];
  for (let i = 0; i + 10 <= Y.length; i += 10) {
    let a = 0, ae = 0;
    for (let j = 0; j < 10; j++) { ae += a + E[i + j]; a += Y[i + j]; }
    Yo.push(a / 10); Eo.push(ae / 100);
  }
  return [Yo, Eo];
}
function mdevYE(Y, E, m, pb) {
  const M = Y.length; if (M < 3 * m + 1) return NaN;
  const last = pb ? M - 3 * m : M - 3 * m + 1;
  let acc = 0, n = 0;
  for (let j = 0; j <= last; j++) {
    let in_ = 0;
    for (let i = j; i < j + m; i++) for (let k = i; k < i + m; k++) in_ += Y[k + m] - Y[k];
    if (pb) for (let l = j; l < j + m; l++) in_ += E[l + 2 * m] - 2 * E[l + m] + E[l];
    acc += in_ * in_; n++;
  }
  return Math.sqrt(acc / (2 * m ** 4 * n));
}
function decim(y, f) { const o = []; for (let i = 0; i + f <= y.length; i += f) { let s = 0; for (let k = 0; k < f; k++) s += y[i + k]; o.push(s / f); } return o; }
function series(type, N) {
  if (type === 'bily PM') { const x = new Float64Array(N + 1); for (let i = 0; i <= N; i++) x[i] = gauss();
    const y = new Float64Array(N); for (let i = 0; i < N; i++) y[i] = x[i + 1] - x[i]; return y; }
  if (type === 'blikavy PM') { const x = kasdin(N + 1, 1, 2048);
    const y = new Float64Array(N); for (let i = 0; i < N; i++) y[i] = x[i + 1] - x[i]; return y; }
  if (type === 'bily FM') { const y = new Float64Array(N); for (let i = 0; i < N; i++) y[i] = gauss(); return y; }
  return null;
}
function slope(t, v) { let sx = 0, sy = 0, sxx = 0, sxy = 0, k = 0;
  for (let i = 0; i < t.length; i++) { if (!(v[i] > 0)) continue; const X = Math.log10(t[i]), Y = Math.log10(v[i]);
    sx += X; sy += Y; sxx += X * X; sxy += X * Y; k++; }
  return (k * sxy - sx * sy) / (k * sxx - sx * sx); }
const N = 200000;
for (const type of ['bily PM', 'blikavy PM', 'bily FM']) {
  const y = Array.from(series(type, N));
  console.log('=== ' + type + ' ===   pomer MDEV pyramida / presne (1,00 = shoda)');
  const tT = [], vP = [], vE = [];
  for (let s = 0; s <= 2; s++) {
    const ys = s ? decim(y, 10 ** s) : y;
    let line = '  stage ' + s + ' (τ = m·' + 10 ** s + ' s): ';
    for (const m of [1, 2, 3, 5, 7, 9]) {
      const tau = m * 10 ** s;
      const p = mdevY(ys, m), e = mdevY(y, tau);
      line += ' m=' + m + ' ' + (p / e).toFixed(2);
      if ([1, 2, 5].includes(m)) { tT.push(tau); vP.push(p); vE.push(e); }
    }
    console.log(line);
  }
  { const y1 = decim(y, 10), y2 = decim(y, 100); let la = '  kontrola ADEV/HDEV (pyramida/presne):';
    for (const [s, ys] of [[1, y1], [2, y2]]) for (const m of [1, 5]) {
      const tau = m * 10 ** s;
      la += ' τ=' + tau + ' A ' + (adevY(ys, m) / adevY(y, tau)).toFixed(2) + ' H ' + (hdevY(ys, m) / hdevY(y, tau)).toFixed(2);
    }
    console.log(la); }
  /* OPRAVA: fazova pyramida (jen s <= 2, M = cela rada — stredni hodnota) */
  { const vF = [], tF = [];
    let Y = y.slice(), E = new Array(y.length).fill(0), line = '  OPRAVA fazova pyramida (pomer):';
    for (let s = 0; s <= 2; s++) {
      if (s) { [Y, E] = decimE(Y, E); }
      for (const m of [1, 2, 3, 5, 7, 9]) {
        const tau = m * 10 ** s, p = mdevYE(Y, E, m, s > 0), e = mdevY(y, tau);
        if (m === 1 || m === 9) line += ' τ=' + tau + ' ' + (p / e).toFixed(2);
        if ([1, 2, 5].includes(m)) { tF.push(tau); vF.push(p); }
      }
    }
    const muF = slope(tF, vF);
    console.log(line + '   sklon ' + muF.toFixed(2) + ' -> ' + (muF < -1.25 ? 'bily PM' : 'blikavy PM'));
  }
  const muP = slope(tT, vP), muE = slope(tT, vE);
  const cls = mu => (mu < -1.25 ? 'bily PM' : 'blikavy PM');
  console.log('  sklon MDEV (body 1-2-5, τ 1..500 s): pyramida ' + muP.toFixed(2) + ' -> ' + cls(muP)
    + '   presne ' + muE.toFixed(2) + ' -> ' + cls(muE) + '   (prah noise_desc/web: < -1,25 = bily PM)');
}
