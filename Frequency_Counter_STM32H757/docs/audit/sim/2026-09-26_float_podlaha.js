/* Numericka podlaha float32 ve statistice stability (modul 24, bod 2).
 * Prepis 1:1 dnesni cesty v screen_main.c:
 *   y = (float)((f - f0) / f0)      f0 = CELE Hz prvniho mereni -> |y| < 1/f
 *   s_y[] a ring stage 0 (float), dekadovy prumer: acc (float) += v; v = acc/10 (float)
 * proti tymz vypoctum v double. Sum: bily FM s ADEV(1 s) = sigma, tedy stabilni
 * zdroj; offset y0 = 0,99/f (nejhorsi pripad: f tesne pod celym Hz + 1).
 * Vystup: pomer ADEV(float) / ADEV(double) pro tau = 1, 10, 100, 1000 s.
 */
const f32 = Math.fround;
let seed = 4242;
function rnd() { seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }
function gauss() { let u = 0, v = 0; while (!u) u = rnd(); while (!v) v = rnd();
  return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v); }

function oadev(y, m) { const M = y.length; let s = 0, n = 0;
  for (let j = 0; j + 2 * m <= M; j++) { let a = 0; for (let i = j; i < j + m; i++) a += y[i + m] - y[i]; s += a * a; n++; }
  return Math.sqrt(s / (2 * m * m * n)); }

/* pyramida: stage s drzi prumery 10^s vzorku; fl = float32 (jako firmware) */
function pyramid(ys, fl) {
  const st = [[], [], [], []]; const acc = [0, 0, 0, 0], an = [0, 0, 0, 0];
  const R = fl ? f32 : (x => x);
  for (let v of ys) {
    v = R(v);
    for (let s = 0; s < 4; s++) {
      st[s].push(v);
      acc[s] = R(acc[s] + v);
      if (++an[s] < 10) break;
      v = R(acc[s] / 10); acc[s] = 0; an[s] = 0;
    }
  }
  return st;
}

const N = 200000, SIG = 1e-12;
let worst = [];
for (const f of [10e6, 1e6, 1e5, 1e4, 1e3]) {
  const y0 = 0.99 / f;
  const yd = []; for (let i = 0; i < N; i++) yd.push(y0 + SIG * gauss());
  const pd = pyramid(yd, false), pf = pyramid(yd, true);
  let line = 'f=' + String(f).padEnd(9) + ' y0=' + y0.toExponential(1) + ' |';
  for (let s = 0; s < 4; s++) {
    const a = oadev(pd[s], 1), b = oadev(pf[s], 1);
    line += '  tau ' + String(10 ** s).padStart(4) + ': ' + (b / a).toFixed(2) + 'x';
  }
  console.log(line);
}
console.log('(ocekavano pro double: 1.00x; ADEV(double) pri tau=1 s = 1e-12, klesa jako tau^-1/2)');
