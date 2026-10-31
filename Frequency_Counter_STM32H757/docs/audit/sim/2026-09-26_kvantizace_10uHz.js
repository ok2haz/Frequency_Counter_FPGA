/* Tretí pruchod modulu 24: vliv zaokrouhleni kmitoctu na 10 µHz (x100000),
 * ktere nese IPC snapshot -> web (/api/state freq_hz) a SCPI, a datalog.
 * Model: mereni po 0,25 s (navazujici okna, jako web), sum = PODLAHA CITACE
 * (bily PM z kvantizace TDC, sigma_x = tdc/sqrt(12)) -> nejlepsi mozny signal.
 * Porovnava ADEV z presnych hodnot a z hodnot zaokrouhlenych na 1e-5 Hz.
 */
let seed = 11;
function rnd() { seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }
function oadev(y, m) { const M = y.length; let s = 0, n = 0;
  for (let j = 0; j + 2 * m <= M; j++) { let a = 0; for (let i = j; i < j + m; i++) a += y[i + m] - y[i]; s += a * a; n++; }
  return Math.sqrt(s / (2 * m * m * n)); }

function run(f, tdc, label) {
  const G = 0.25, N = 40000;
  const x = []; for (let k = 0; k <= N; k++) x.push((rnd() - 0.5) * tdc);   /* chyba znacek [s] */
  /* f_mer = f * (1 + (x_{k+1}-x_k)/G); fazovy posun 0,37 LSB, at zaokrouhleni neni trivialni */
  const fTrue = [], fRnd = [];
  for (let k = 0; k < N; k++) {
    const fm = f * (1 + (x[k + 1] - x[k]) / G) + 0.37e-5;
    fTrue.push(fm); fRnd.push(Math.round(fm * 1e5) / 1e5);
  }
  const yT = fTrue.map(v => (v - f) / f), yR = fRnd.map(v => (v - f) / f);
  let line = label.padEnd(26);
  for (const m of [1, 10, 100]) {
    const a = oadev(yT, m), b = oadev(yR, m);
    line += '  τ=' + (m * G).toString().padEnd(4) + ' ' + (b / a).toFixed(2) + 'x';
  }
  console.log(line);
}
for (const [tdc, nm] of [[2.5e-9, 'TDC 2,5 ns'], [22e-12, 'TDC 22 ps (nova deska)']]) {
  console.log('--- ' + nm + ' — pomer ADEV(zaokrouhleno 10 µHz) / ADEV(presne) ---');
  for (const f of [10e6, 1e6, 100e3, 10e3, 1e3]) run(f, tdc, '  f = ' + f + ' Hz');
}
