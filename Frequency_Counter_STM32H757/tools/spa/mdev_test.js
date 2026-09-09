/* Overi MDEV / TDEV / MTIE proti ZNAMYM vlastnostem sumu, ne proti sobe samym.
 * Klicova vlastnost, kvuli ktere MDEV existuje: bily a blikavy fazovy sum maji
 * v ADEV STEJNY sklon (tau^-1), v MDEV RUZNY (tau^-3/2 vs tau^-1). */
const fs = require('fs');
const src = fs.readFileSync(process.argv[2], 'utf8');

function grab(name) {
  const i = src.indexOf('function ' + name + '(');
  if (i < 0) throw new Error('nenalezeno: ' + name);
  let d = 0;
  for (let k = src.indexOf('{', i); k < src.length; k++) {
    if (src[k] === '{') d++;
    else if (src[k] === '}') { d--; if (d === 0) return src.slice(i, k + 1); }
  }
}
const api = new Function(grab('adev') + grab('mdev') + grab('stabPoints')
  + '\nreturn {adev:adev, mdev:mdev, stab:stabPoints};')();

/* Deterministicky, ale POCTIVY generator. ⚠️ Prvni verze pouzivala LCG
 * (s*1103515245+12345) + soucet 12 vzorku: ta ma silnou mrizkovou korelaci mezi
 * po sobe jdoucimi hodnotami, takze „bily FM" bily nebyl a test hlasil poměr
 * MDEV/ADEV 0,191 misto 0,707 — vada TESTU, ne MDEV. mulberry32 + Box-Muller. */
let s = 0x9E3779B9;
function rnd() {
  s |= 0; s = (s + 0x6D2B79F5) | 0;
  let t = Math.imul(s ^ (s >>> 15), 1 | s);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}
function gauss() {
  let u = 0, v = 0;
  while (u === 0) u = rnd();
  while (v === 0) v = rnd();
  return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v);
}

function slope(pts) {                     /* sklon v log-log (nejmensi ctverce) */
  const P = pts.filter(p => p.sig > 0);
  let sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (const p of P) {
    const X = Math.log10(p.tau), Y = Math.log10(p.sig);
    sx += X; sy += Y; sxx += X * X; sxy += X * Y;
  }
  const n = P.length;
  return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

const N = 4096, tau0 = 1;

/* --- 1) BILY FM: y je nekorelovane -> ADEV i MDEV maji sklon -1/2 --------- */
const yFM = []; for (let i = 0; i < N; i++) yFM.push(gauss());
const aFM = api.adev(yFM, tau0), mFM = api.mdev(yFM, tau0);
console.log('--- bily FM (y nekorelovane) ---');
console.log('  sklon ADEV = %s  (teorie -0.5)', slope(aFM).toFixed(3));
console.log('  sklon MDEV = %s  (teorie -0.5)', slope(mFM).toFixed(3));
const rat = mFM[mFM.length - 1].sig / aFM[aFM.length - 1].sig;
console.log('  MDEV/ADEV pri velkem tau = %s  (teorie ~0.707 = 1/sqrt2)', rat.toFixed(3));

/* --- 2) BILY PM: y = diference bileho sumu -> ADEV -1, MDEV -3/2 --------- */
const w = []; for (let i = 0; i <= N; i++) w.push(gauss());
const yPM = []; for (let i = 0; i < N; i++) yPM.push(w[i + 1] - w[i]);
const aPM = api.adev(yPM, tau0), mPM = api.mdev(yPM, tau0);
console.log('\n--- bily PM (y = diference bileho sumu) ---');
console.log('  sklon ADEV = %s  (teorie -1.0)', slope(aPM).toFixed(3));
console.log('  sklon MDEV = %s  (teorie -1.5)', slope(mPM).toFixed(3));
console.log('  >>> MDEV je STRMEJSI nez ADEV:', slope(mPM) < slope(aPM) - 0.3,
            ' <- prave tim MDEV odlisi bily PM od blikaveho');

/* --- 3) TDEV musi byt tau*MDEV/sqrt(3), NE z ADEV ------------------------ */
console.log('\n--- TDEV = tau*MDEV/sqrt(3) (a NE z ADEV) ---');
const T = api.stab(yPM, tau0, 'tdev');
let okT = true, worst = 0;
for (let i = 0; i < T.length; i++) {
  const want = mPM[i].tau * mPM[i].sig / Math.sqrt(3);
  const rel = Math.abs(T[i].sig - want) / want;
  if (rel > worst) worst = rel;
  if (rel > 1e-12) okT = false;
}
console.log('  shoda s MDEV:', okT, ' (nejhorsi rel. odchylka %s)', worst.toExponential(1));
const fromAdev = aPM[3].tau * aPM[3].sig / Math.sqrt(3);
console.log('  kdyby se pocital z ADEV: %s vs spravne %s  -> lisi se %sx',
  fromAdev.toExponential(2), T[3].sig.toExponential(2), (fromAdev / T[3].sig).toFixed(2));

/* --- 4) MTIE odhad + degradace ------------------------------------------ */
const MT = api.stab(yPM, tau0, 'mtie');
console.log('\n--- MTIE (odhad sqrt3*tau*ADEV) ---');
console.log('  prvni bod: %s s  (ADEV %s)', MT[0].sig.toExponential(2), aPM[0].sig.toExponential(2));
console.log('  = sqrt3*tau*ADEV:', Math.abs(MT[0].sig - Math.sqrt(3) * aPM[0].tau * aPM[0].sig) < 1e-15);
console.log('\n--- degradace ---');
for (const [lbl, arr] of [['prazdne', []], ['3 vzorky', [1, 2, 3]]])
  console.log('  ' + lbl.padEnd(9) + ' adev=' + api.adev(arr, 1).length
    + ' mdev=' + api.mdev(arr, 1).length
    + ' tdev=' + api.stab(arr, 1, 'tdev').length);
console.log('  tau0=0    mdev=' + api.mdev([1, 2, 3, 4, 5], 0).length);
