/* F-0191: kolikrat se kazdy sloupec pasu nejistoty blenduje (allan_band_fill).
 * Body = tau mrizky 1..9 na dekadu pres 5 dekad (hustota 9 -> 45 bodu, + konec),
 * X pozice jako v allan_plot_curve (log osa pres sirku karty 330 px).
 * Stara logika: kazdy usek c = 0..cols vcetne kraju, cols < 1 -> 1.
 * Nova: stejne useky, ale sloupec <= posledni vyplneny se preskoci.
 * Beh: node 2026-09-27_f0191_pas.js */
const W = 330, X0 = 20;
const taus = [];
for (let d = 0; d < 5; d++) for (let m = 1; m <= 9; m++) taus.push(m * 10 ** d);
taus.push(1e5);
const lmin = Math.log10(taus[0]), lmax = Math.log10(taus[taus.length - 1]);
const xs = taus.map(t => Math.trunc(X0 + (Math.log10(t) - lmin) / (lmax - lmin) * W));

function oldFill() {
  const n = new Map();
  for (let i = 1; i < xs.length; i++) {
    let cols = xs[i] - xs[i - 1]; if (cols < 1) cols = 1;
    for (let c = 0; c <= cols; c++) { const cx = xs[i - 1] + c; n.set(cx, (n.get(cx) || 0) + 1); }
  }
  return n;
}
function newFill() {
  const n = new Map(); let xd = -32768;
  for (let i = 1; i < xs.length; i++) {
    const cols = xs[i] - xs[i - 1];
    for (let c = 0; c <= cols; c++) {
      const cx = xs[i - 1] + c; if (cx <= xd) continue;
      n.set(cx, (n.get(cx) || 0) + 1); xd = cx;
    }
  }
  return n;
}
function report(name, n) {
  const hist = {}; for (const v of n.values()) hist[v] = (hist[v] || 0) + 1;
  const span = Math.max(...n.keys()) - Math.min(...n.keys()) + 1;
  console.log(name + ': ' + n.size + ' sloupcu (rozpeti ' + span + '), pocet blendu na sloupec: '
    + Object.keys(hist).map(k => k + 'x ' + hist[k]).join(', '));
  return { hist, size: n.size, span };
}
const o = report('stara', oldFill()), nw = report('nova ', newFill());
const ok = Object.keys(nw.hist).length === 1 && nw.hist[1] === nw.size && nw.size === nw.span
  && (o.hist[2] || 0) > 0;
console.log('pozitivni kontrola: stara logika ma ' + (o.hist[2] || 0) + ' dvakrat a '
  + (o.hist[3] || 0) + ' trikrat blendovanych sloupcu');
console.log(ok ? 'vse OK (nova: kazdy sloupec presne jednou, bez der)' : 'CHYBA');
process.exitCode = ok ? 0 : 1;
