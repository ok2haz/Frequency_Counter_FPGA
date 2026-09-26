/* Plati predpoklad t-testu (nezavisla rezidua) pro data, ktera ANALYZA prokladava?
 * Merime podil "prukazne" (falesne poplachy) pro data BEZ deterministickeho
 * driftu a skutecnou detekci pro data S driftem. Tri pravidla:
 *   OLD  |r| >= 0,5 (do F-0169)
 *   T    t-test 5 % s df = n-2 (dnes, mp_fit_significant / fitSig)
 *   TEFF t-test s efektivnim n: n_eff = n (1-rho1)/(1+rho1), rho1 = lag-1 autokorelace
 *        rezidui (Bretherton 1999) — kandidat na opravu
 */
let seed = 777;
function rnd() { seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }
function gauss() { let u = 0, v = 0; while (!u) u = rnd(); while (!v) v = rnd();
  return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v); }
const T95 = [12.706,4.303,3.182,2.776,2.571,2.447,2.365,2.306,2.262,2.228,2.201,2.179,2.160,2.145,2.131,2.120,2.110,2.101,2.093,2.086,2.080,2.074,2.069,2.064,2.060,2.056,2.052,2.048,2.045,2.042];
function sig(r, n) { if (!(n >= 3) || r !== r) return false; const df = n - 2, tc = df <= 30 ? T95[df - 1] : 2.042, r2 = r * r;
  if (!(r2 < 1)) return true; return Math.abs(r) * Math.sqrt(df / (1 - r2)) >= tc; }
function fit(y) { const n = y.length; let sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
  for (let i = 0; i < n; i++) { sx += i; sy += y[i]; sxx += i * i; sxy += i * y[i]; syy += y[i] * y[i]; }
  const dx = n * sxx - sx * sx, dy = n * syy - sy * sy, b = (n * sxy - sx * sy) / dx, a = (sy - b * sx) / n;
  const r = (n * sxy - sx * sy) / Math.sqrt(dx * dy);
  let c0 = 0, c1 = 0; const e = y.map((v, i) => v - a - b * i);
  for (let i = 0; i < n; i++) { c0 += e[i] * e[i]; if (i) c1 += e[i] * e[i - 1]; }
  return { r, rho: c1 / c0 }; }
function neff(n, rho) { if (!(rho > 0)) return n; if (rho >= 1) return 2; return Math.max(2, n * (1 - rho) / (1 + rho)); }

function gen(kind, n, drift) {
  const y = []; let w = 0, a = 0;
  for (let i = 0; i < n; i++) {
    const g = gauss();
    if (kind === 'white') w = g;
    else if (kind === 'ar09') { a = 0.9 * a + g; w = a; }
    else if (kind === 'rw') { a += g; w = a; }
    y.push(w + drift * i);
  }
  return y;
}
const TR = 4000;
for (const n of [20, 200]) {
  console.log('=== n = ' + n + ' ===');
  for (const [kind, drift] of [['white', 0], ['ar09', 0], ['rw', 0], ['white', 0.02 * 200 / n], ['rw', 0.5 * 200 / n]]) {
    let o = 0, t = 0, te = 0;
    for (let k = 0; k < TR; k++) {
      const f = fit(gen(kind, n, drift));
      if (Math.abs(f.r) >= 0.5) o++;
      if (sig(f.r, n)) t++;
      if (sig(f.r, Math.round(neff(n, f.rho)))) te++;
    }
    const lbl = (kind + (drift ? ' + drift' : ' bez driftu')).padEnd(18);
    console.log('  ' + lbl + ' "prukazne":  OLD ' + (100 * o / TR).toFixed(0).padStart(3) + ' %   T '
      + (100 * t / TR).toFixed(0).padStart(3) + ' %   TEFF ' + (100 * te / TR).toFixed(0).padStart(3) + ' %');
  }
}
