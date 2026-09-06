/* Overi deleni os (niceStep/niceAxis/niceAxisLog) a urceni typu sumu.
 * Osy jsou nove nosne pro VSECH 9 grafu, takze chyba tady zkazi kazdy z nich. */
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
const api = new Function(
  ['niceStep', 'niceAxis', 'niceAxisLog', 'axDec', 'axNum', 'logSlope',
   'noiseName', 'noiseDesc', 'adev', 'mdev'].map(grab).join('\n')
  + '\nreturn {step:niceStep, ax:niceAxis, axl:niceAxisLog, slope:logSlope,'
  + ' nname:noiseName, ndesc:noiseDesc, adev:adev, mdev:mdev,'
  + ' dec:axDec, num:axNum};')();

let fail = 0;
function ok(lbl, cond, extra) {
  console.log('  ' + (cond ? 'OK  ' : 'CHYBA ') + lbl + (extra !== undefined ? '  ' + extra : ''));
  if (!cond) fail++;
}

console.log('--- niceStep: jen 1/2/5 x 10^n ---');
for (const [raw, want] of [[1, 1], [1.5, 2], [3, 5], [7, 10], [0.03, 0.05],
                           [230, 500], [0.0011, 0.002], [1e-9, 1e-9]]) {
  const g = api.step(raw);
  ok('step(' + raw + ') = ' + want, Math.abs(g - want) < want * 1e-9, g);
}
/* mantisa kazdeho kroku musi byt 1, 2 nebo 5 - jinak osa nebude "hezka" */
/* !! Mantisu ziskej pres FLOOR, ne ROUND: pro krok 5 je log10 = 0.699 a
 * Math.round da 1 -> mantisa vyjde 0.5 a test hlasi chybu, ktera neni v kodu.
 * (Ctvrta vada testu v teto session - viz SKILL 7e.) */
let allNice = true, worstR = null;
for (let i = 0; i < 2000; i++) {
  const r = Math.pow(10, (Math.random() * 18) - 9) * (1 + Math.random() * 9);
  const st = api.step(r);
  const m = st / Math.pow(10, Math.floor(Math.log10(st) + 1e-9));
  if (![1, 2, 5].some(x => Math.abs(m - x) < 1e-6)) { allNice = false; worstR = [r, st, m]; }
  if (!(st >= r)) { allNice = false; worstR = [r, st, m]; }
}
ok('2000 nahodnych radu: mantisa vzdy 1/2/5 a step >= raw', allNice,
   worstR ? JSON.stringify(worstR) : '');

console.log('\n--- niceAxis: rozsah VEN na cely krok, dilky na nasobcich ---');
function chk(lbl, lo, hi, want) {
  const a = api.ax(lo, hi, want || 4);
  const okLo = a.lo <= lo + 1e-12, okHi = a.hi >= hi - 1e-12;
  let onGrid = true;
  for (const t of a.t) if (Math.abs(t / a.step - Math.round(t / a.step)) > 1e-6) onGrid = false;
  const ends = Math.abs(a.t[0] - a.lo) < 1e-12
            && Math.abs(a.t[a.t.length - 1] - a.hi) < 1e-12;
  ok(lbl, okLo && okHi && onGrid && ends,
     '[' + a.lo + ', ' + a.hi + '] krok ' + a.step + ', ' + a.t.length + ' dilku');
}
chk('11.9473 .. 11.9581', 11.9473, 11.9581);
chk('0 .. 7 (pocty)', 0, 7);
chk('-48 .. +12 mV', -48, 12);
chk('velmi male cislo', 1e-9, 3e-9);
chk('prochazi nulou', -0.4, 0.9);

console.log('\n--- degradace: nesmi vyrobit tisic car ani NaN ---');
for (const [lbl, lo, hi] of [['hi == lo', 5, 5], ['hi < lo', 9, 2],
                             ['NaN', NaN, NaN], ['Infinity', -Infinity, Infinity],
                             ['obri rozsah', 0, 1e300]]) {
  const a = api.ax(lo, hi, 4);
  const good = isFinite(a.lo) && isFinite(a.hi) && a.hi > a.lo
            && a.t.length >= 2 && a.t.length <= 41 && a.t.every(isFinite);
  ok(lbl, good, a.t.length + ' dilku [' + a.lo + ', ' + a.hi + ']');
}

console.log('\n--- niceAxisLog: dilky na dekadach ---');
let a = api.axl(Math.log10(2e-11), Math.log10(7e-9));
ok('cele dekady', a.lo === -11 && a.hi === -8, '[' + a.lo + ', ' + a.hi + ']');
ok('vsechny dilky cele', a.t.every(v => v === Math.round(v)), JSON.stringify(a.t));
a = api.axl(0, 12);
ok('mnoho dekad -> popisuje kazdou n-tou (max 9 dilku)', a.t.length <= 9,
   a.t.length + ' dilku, krok ' + a.step);

console.log('\n--- desetiny podle KROKU osy (aby se popisek vesel do sloupce) ---');
for (const [step, want] of [[0.005, 3], [20, 0], [1, 0], [0.1, 1],
                            [1e-5, 5], [1e-9, 6], [500, 0]]) {
  const g = api.dec(step);
  ok('axDec(' + step + ') = ' + want, g === want, g);
}
ok('axDec(0) nespadne', api.dec(0) === 0);
ok('axDec(NaN) nespadne', api.dec(NaN) === 0);

/* 🔴 TOHLE je kontrola, ktera chybela a stala jedno kolo:
 * `.cw` ma `overflow:hidden`, takze prilis siroky popisek se TISE orizne zleva.
 * Levy sloupec je 56 px minus 7 px odsazeni = 49 px, monospace 10 px ~ 6 px/znak
 * => 8 znaku. Jednotka uz v popisku NENI (kresli se jednou nad osou). */
const YCOL_CH = 8;
console.log('\n--- sirka popisku osy Y (limit ' + YCOL_CH + ' znaku) ---');
function widest(lo, hi, f) {
  const a = api.ax(lo, hi, 4), g = f || api.num(a, 0);
  let w = 0, s = '';
  for (const t of a.t) { const x = g(t); if (x.length > w) { w = x.length; s = x; } }
  return { w, s, step: a.step };
}
const CASES = [
  ['KMITOCET uzky',   -0.0001, 0.0001, null],
  ['KMITOCET siroky', -1500, 1500, null],
  ['TEPLOTY',         20, 70, null],
  ['OCXO Vc [mV]',    1900, 1960, null],
  ['NAPAJENI [mV]',   -60, 20, null],
  ['HISTOGRAM n',     0, 250, null],
  ['FAZOVY SUM dB',   -140, -80, null],
  /* Krajni pripady: pod rozlisenim TDC by desetinny zapis nabobtnal, nad
   * 100k by narostla cela cast. Obojí musi spadnout do vedeckeho zapisu. */
  ['velmi UZKY',      -1e-7, 1e-7, null],
  ['velmi SIROKY',    -2e6, 2e6, null],
  ['obri',            0, 5e9, null],
];
for (const [n, lo, hi, f] of CASES) {
  const r = widest(lo, hi, f);
  ok(n.padEnd(17) + ' max "' + r.s + '"', r.w <= YCOL_CH,
     r.w + ' znaku (krok ' + r.step + ')');
}
/* ALLAN pouziva `sci` (vlastni formatovac) - overit zvlast */
const sciW = '1.23e-11'.length;
ok('ALLAN sci "1.23e-11"', sciW <= YCOL_CH, sciW + ' znaku');

console.log('\n--- logSlope + typ sumu (proti ZNAMYM sklonum) ---');
function synth(mu, n) {      /* sigma_y(tau) = tau^mu */
  const P = [];
  for (let i = 0; i < (n || 6); i++) { const t = Math.pow(2, i); P.push({ tau: t, sig: Math.pow(t, mu) }); }
  return P;
}
for (const mu of [-1, -0.5, 0, 0.5, 1]) {
  const g = api.slope(synth(mu));
  ok('sklon tau^' + mu, Math.abs(g - mu) < 1e-9, g.toFixed(4));
}
ok('bily FM',        api.nname(-0.5, null).indexOf('bily FM') >= 0, api.nname(-0.5, null));
ok('flicker floor',  api.nname(0, null).indexOf('flicker') >= 0, api.nname(0, null));
ok('RW FM',          api.nname(0.5, null).indexOf('prochazka') >= 0, api.nname(0.5, null));
ok('drift',          api.nname(1, null).indexOf('drift') >= 0, api.nname(1, null));
/* Klicovy rozdil, kvuli kteremu MDEV existuje: pri mu=-1 rozhodne az MDEV. */
ok('mu=-1, MDEV -3/2 -> BILY PM', api.nname(-1, -1.5).indexOf('bily fazovy') >= 0,
   api.nname(-1, -1.5));
ok('mu=-1, MDEV -1   -> BLIKAVY PM', api.nname(-1, -1.0).indexOf('blikavy fazovy') >= 0,
   api.nname(-1, -1.0));

console.log('\n--- noiseDesc nad SKUTECNYM sumem (ne nad syntetickou krivkou) ---');
let s2 = 0x9E3779B9;
function rnd() {
  s2 |= 0; s2 = (s2 + 0x6D2B79F5) | 0;
  let t = Math.imul(s2 ^ (s2 >>> 15), 1 | s2);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}
function gauss() { let u = 0, v = 0; while (!u) u = rnd(); while (!v) v = rnd(); return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v); }
const yFM = []; for (let i = 0; i < 2048; i++) yFM.push(gauss());
let d = api.ndesc(api.adev(yFM, 1), api.mdev(yFM, 1));
console.log('   bily FM  -> ' + d);
ok('rozpozna bily FM', d.indexOf('bily FM') >= 0);
const w = []; for (let i = 0; i <= 2048; i++) w.push(gauss());
const yPM = []; for (let i = 0; i < 2048; i++) yPM.push(w[i + 1] - w[i]);
d = api.ndesc(api.adev(yPM, 1), api.mdev(yPM, 1));
console.log('   bily PM  -> ' + d);
ok('rozpozna BILY PM (tedy sahl po MDEV)', d.indexOf('bily fazovy') >= 0);

console.log('\n--- noiseDesc degradace ---');
ok('prazdne', api.ndesc([], null) === '');
ok('dva body (min. 3)', api.ndesc(synth(0, 2), null) === '');
d = api.ndesc(synth(-0.5, 4), null);
ok('4 body -> priznat orientacnost', d.indexOf('orientacni') >= 0, d);

console.log('\n' + (fail ? 'SELHALO kontrol: ' + fail : 'vse OK'));
process.exit(fail ? 1 : 0);
