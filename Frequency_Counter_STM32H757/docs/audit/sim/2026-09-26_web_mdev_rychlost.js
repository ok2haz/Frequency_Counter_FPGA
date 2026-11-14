/* Rychlost webove mdev() (SPA) proti verzi s prefixovymi soucty (O(N) na m).
 * Beh: node 2026-09-26_web_mdev_rychlost.js tools/spa/_out/spa.js
 * Meri skutecnou funkci vytazenou ze SPA a overuje, ze prefixova verze dava
 * TYTEZ hodnoty (rel. rozdil < 1e-9) i pocty clenu. */
const fs = require('fs');
const src = fs.readFileSync(process.argv[2], 'utf8');
function grab(name) {
  const i = src.indexOf('function ' + name + '(');
  let d = 0;
  for (let k = src.indexOf('{', i); k < src.length; k++) {
    if (src[k] === '{') d++; else if (src[k] === '}') { d--; if (!d) return src.slice(i, k + 1); }
  }
}
const mdevSpa = new Function(grab('mdev') + '\nreturn mdev;')();

/* Prefixova verze: S_j = Σ_{i=j}^{j+m-1} (x[i+2m] - 2x[i+m] + x[i])
 *                     = (C[j+3m]-C[j+2m]) - 2(C[j+2m]-C[j+m]) + (C[j+m]-C[j]),
 * kde C[k] = Σ_{i<k} x[i]. Tytez meze a normalizace jako SPA po F-0170. */
function mdevFast(y, tau0) {
  const N = y.length; if (N < 4 || !(tau0 > 0)) return [];
  const x = [0]; for (let i = 0; i < N; i++) x.push(x[i] + y[i] * tau0);
  const C = [0]; for (let i = 0; i < x.length; i++) C.push(C[i] + x[i]);
  const out = []; let m = 1;
  while (3 * m <= N) {
    let s = 0, cnt = 0;
    for (let j = 0; j + 3 * m <= N + 1; j++) {
      const a = (C[j + 3 * m] - C[j + 2 * m]) - 2 * (C[j + 2 * m] - C[j + m]) + (C[j + m] - C[j]);
      const v = a / m; s += v * v; cnt++;
    }
    const t = m * tau0; out.push({ tau: t, sig: Math.sqrt(s / (2 * t * t * cnt)), n: cnt });
    m *= 2;
  }
  return out;
}
let seed = 5;
function rnd() { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648 - 0.5; }
const y = []; for (let i = 0; i < 2000; i++) y.push(rnd() * 1e-9);
function time(fn, reps) { const t0 = process.hrtime.bigint(); let r; for (let k = 0; k < reps; k++) r = fn(y, 0.25);
  return [Number(process.hrtime.bigint() - t0) / 1e6 / reps, r]; }
const [ta, ra] = time(mdevSpa, 20), [tb, rb] = time(mdevFast, 200);
let same = ra.length === rb.length;
for (let i = 0; same && i < ra.length; i++)
  if (ra[i].n !== rb[i].n || Math.abs(ra[i].sig / rb[i].sig - 1) > 1e-9) same = false;
console.log('N = 2000 mereni (MAXM): SPA mdev ' + ta.toFixed(2) + ' ms, prefixova ' + tb.toFixed(3)
  + ' ms  (' + (ta / tb).toFixed(0) + 'x), vysledky shodne: ' + same);
console.log('(PC; mobil typicky 5-10x pomalejsi. drawStab bezi pri kazde zprave ~4x/s a mdev\n'
  + ' vola noiseDesc vzdy + stabPoints pri metrice TDEV.)');
process.exitCode = same ? 0 : 1;
