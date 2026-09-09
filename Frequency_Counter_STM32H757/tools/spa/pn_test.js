/* Overi pnCompute/pnFft ve SPA proti TEMTEZ vektorum, jake ma pn_selftest()
 * v CM7/Core/Src/phase_noise.c — aby web a pristroj nerekly kazdy neco jineho.
 * Navic overuje ABSOLUTNI normalizaci PSD (2/(fs*wpow*segs)), coz C selftest
 * netestuje: ten kontroluje jen tvar (argmax, osa f, prevod (f0/f)^2). */
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
/* PN_N musi prijit ZE ZDROJE, ne z testu — jinak by test prosel i kdyby se
 * konstanta ve SPA rozesla s PN_NFFT v C. */
const mPN = src.match(/var PN_N=(\d+);/);
if (!mPN) throw new Error('PN_N nenalezeno ve zdroji');
const PN_N = parseInt(mPN[1], 10);

const api = new Function('var PN_N=' + PN_N + ';\n' + grab('pnFft') + grab('pnCompute')
  + '\nreturn {fft:pnFft, comp:pnCompute, N:PN_N};')();

let fail = 0;
function ok(lbl, cond, extra) {
  console.log('  ' + (cond ? 'OK  ' : 'CHYBA ') + lbl + (extra !== undefined ? '  ' + extra : ''));
  if (!cond) fail++;
}

console.log('PN_N (ze zdroje SPA) = ' + api.N + '   [C: PN_NFFT]');
ok('shoda s PN_NFFT=64 v phase_noise.h', api.N === 64, 'jinak web a displej kresli jine biny');

/* --- (1) malo dat -> 0 bodu (jako C) ------------------------------------- */
const few = new Array(api.N - 1).fill(0);
ok('n < PN_N -> 0 bodu', api.comp(few, 1e7, 1).pts.length === 0);
ok('f0 <= 0 -> 0 bodu', api.comp(new Array(api.N).fill(0), 0, 1).pts.length === 0);
ok('fs <= 0 -> 0 bodu', api.comp(new Array(api.N).fill(0), 1e7, 0).pts.length === 0);

/* --- (2)+(3) cisty ton na binu k0 ---------------------------------------- */
const k0 = 8, A = 2e-9, y = [];
for (let i = 0; i < api.N; i++) y.push(A * Math.cos(2 * Math.PI * k0 * i / api.N));
const r = api.comp(y, 1e7, 1);
ok('pocet bodu = N/2-1', r.pts.length === api.N / 2 - 1, r.pts.length);
ok('jeden segment', r.segs === 1);

let okAxis = true;
for (let i = 0; i < r.pts.length; i++) {
  if (Math.abs(r.pts[i].f - (i + 1) * 1 / api.N) > 1e-12) okAxis = false;
  if (i > 0 && r.pts[i].f <= r.pts[i - 1].f) okAxis = false;
}
ok('osa f_k = k*fs/N a rostouci', okAxis);

let imax = 0;
for (let i = 1; i < r.pts.length; i++) if (r.pts[i].l > r.pts[imax].l) imax = i;
ok('argmax na binu k0 (+-1 kvuli Hannovu oknu)',
   Math.abs(imax - (k0 - 1)) <= 1, 'imax=' + imax + ' ocek=' + (k0 - 1));

/* --- (4) f0 x2 -> +6,02 dB na temze binu (overeni clenu (f0/f)^2) -------- */
const r2 = api.comp(y, 2e7, 1);
const d = r2.pts[imax].l - r.pts[imax].l;
ok('f0 x2 -> +20*log10(2) dB', Math.abs(d - 20 * Math.log10(2)) < 0.01, d.toFixed(4) + ' dB');

/* --- (5) ABSOLUTNI normalizace (C selftest tohle NETESTUJE) --------------
 * Pro bily y s rozptylem sigma^2 je jednostranne PSD Sy(f) = 2*sigma^2/fs,
 * konstantni pres vsechny biny. Zpetne ze L(f): Sy = 2*10^(L/10)*(f/f0)^2. */
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
const SIG = 3e-10, FS = 1.0, F0 = 1e7;
const w = [];
for (let i = 0; i < api.N * 32; i++) w.push(SIG * gauss());
const rw = api.comp(w, F0, FS);
let sySum = 0;
for (const p of rw.pts) sySum += 2 * Math.pow(10, p.l / 10) * Math.pow(p.f / F0, 2);
const syAvg = sySum / rw.pts.length;
const syWant = 2 * SIG * SIG / FS;
const relN = Math.abs(syAvg - syWant) / syWant;
console.log('\n--- absolutni normalizace PSD ---');
console.log('  Sy zmerene = ' + syAvg.toExponential(3)
            + '  teorie 2*sigma^2/fs = ' + syWant.toExponential(3));
ok('normalizace do 10 %', relN < 0.10, 'odchylka ' + (relN * 100).toFixed(1) + ' %');

/* --- (6) Bartlett: vic segmentu = TYZ grid, mensi rozptyl ---------------- */
console.log('\n--- Bartlett (prumerovani segmentu) ---');
const r1 = api.comp(w.slice(w.length - api.N), F0, FS);
const rK = api.comp(w, F0, FS);
ok('segmentu = floor(n/N)', rK.segs === 32, rK.segs);
let sameGrid = r1.pts.length === rK.pts.length;
for (let i = 0; i < r1.pts.length && sameGrid; i++)
  if (r1.pts[i].f !== rK.pts[i].f) sameGrid = false;
ok('mrizka f se prumerovanim NEMENI', sameGrid);
/* !! Rozptyl se MUSI merit na Sy(f), ne primo na L(f): L nese jeste
 * deterministicky sklon (f0/f)^2, tedy -20 dB/dekadu = ~30 dB systematickeho
 * rozsahu pres biny 1..31. Kdyby se merilo na L, test by meril SKLON, ne
 * rozptyl odhadu — prvni verze tohohle testu presne tak selhala. */
function sdSy(P, f0) {
  const db = P.map(p => p.l + 20 * Math.log10(p.f / f0));   /* = 10log10(Sy/2) */
  let m = 0; for (const v of db) m += v; m /= db.length;
  let v2 = 0; for (const v of db) v2 += (v - m) * (v - m);
  return Math.sqrt(v2 / db.length);
}
const sd1 = sdSy(r1.pts, F0), sdK = sdSy(rK.pts, F0);
console.log('  rozptyl Sy(f) pres biny: 1 segment ' + sd1.toFixed(2)
            + ' dB, ' + rK.segs + ' segmentu ' + sdK.toFixed(2) + ' dB');
/* teorie: 1 periodogram = chi2(2) -> sigma 5,57 dB; K prumeru -> /sqrt(K) */
ok('vic segmentu -> hladsi odhad', sdK < sd1 * 0.5, 'pomer ' + (sdK / sd1).toFixed(2));
ok('1 segment ma rozptyl blizko teorie 5,57 dB', sd1 > 3.5 && sd1 < 8, sd1.toFixed(2) + ' dB');
ok('32 segmentu blizko 5,57/sqrt(32) = 0,98 dB', sdK > 0.4 && sdK < 2.2, sdK.toFixed(2) + ' dB');

/* --- (7) strop poctu segmentu (ochrana proti O(n) praci na kazdy tik) ---- */
const huge = new Array(api.N * 100).fill(0).map(() => SIG * gauss());
ok('segmentu shora omezeno na 32', api.comp(huge, F0, FS).segs === 32);

/* --- (8) nulovy vstup nesmi dat NaN ------------------------------------- */
const z = api.comp(new Array(api.N).fill(0), F0, FS);
ok('nulovy vstup -> -400 dB, zadne NaN',
   z.pts.length > 0 && z.pts.every(p => p.l === -400));

console.log('\n' + (fail ? 'SELHALO kontrol: ' + fail : 'vse OK'));
process.exit(fail ? 1 : 0);
