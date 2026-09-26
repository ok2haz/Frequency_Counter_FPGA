/* Overi webove (JS) dvojce estimatoru proti NEZAVISLE referenci a proti
 * firmwaru (audit F-0170). Na rozdil od mdev_test.js, ktery jen vypisuje,
 * tenhle test VYNUCUJE: kazda neshoda tiskne CHYBA a nastavi nenulovy exit.
 *
 * Proc existuje: firmware mel od F-0166 opravene meze overlapping estimatoru
 * a od F-0169 t-test prukaznosti prokladu, ale SPA pocita totez vlastnim JS
 * a oprava se tam neprenesla (L-0012). Web a displej pak pro tataz data
 * davaly jiny verdikt driftu (L-0018).
 *
 * Reference = NIST SP1065 z FAZOVYCH dat, psana naivne jinym algoritmem
 * (primo podle definice, bez sdileni kodu se SPA):
 *   x[0] = 0, x[k+1] = x[k] + y[k]*tau0     (N kmitoctovych vzorku -> N+1 fazi)
 *   ADEV: clenu Nx-2m,  j = 0 .. Nx-2m-1    (Nx = N+1)
 *   MDEV: clenu Nx-3m+1, j = 0 .. Nx-3m
 * Pozitivni kontrola: nad SPA pred opravou F-0170 musi test SELHAT
 * (MDEV o clen mene, chybejici fitSig).
 */
const fs = require('fs');
const src = fs.readFileSync(process.argv[2], 'utf8');

let bad = 0;
function check(cond, msg) {
  if (cond) console.log('  ok     ' + msg);
  else { console.log('  CHYBA  ' + msg); bad++; }
}

function grab(name) {
  const i = src.indexOf('function ' + name + '(');
  if (i < 0) return null;
  let d = 0;
  for (let k = src.indexOf('{', i); k < src.length; k++) {
    if (src[k] === '{') d++;
    else if (src[k] === '}') { d--; if (d === 0) return src.slice(i, k + 1); }
  }
  return null;
}
function grabVar(name) {                       /* `var NAME=[...];` na jednom radku */
  const i = src.indexOf('var ' + name + '=');
  if (i < 0) return '';
  return src.slice(i, src.indexOf(';', i) + 1);
}

const parts = ['adev', 'mdev', 'fit', 'effN', 'fitSig'].map(n => [n, grab(n)]);
for (const [n, body] of parts) check(body !== null, 'funkce ' + n + '() je v SPA');
const body = grabVar('T95') + parts.filter(p => p[1]).map(p => p[1]).join('\n');
const api = new Function(body + '\nreturn {'
  + parts.filter(p => p[1]).map(p => p[0] + ':' + p[0]).join(',') + '};')();

/* --- reference SP1065 z fazi -------------------------------------------- */
function phase(y, tau0) { const x = [0]; for (let i = 0; i < y.length; i++) x.push(x[i] + y[i] * tau0); return x; }
function refAdev(y, tau0, m) {
  const x = phase(y, tau0), Nx = x.length, t = m * tau0;
  let s = 0, n = 0;
  for (let j = 0; j <= Nx - 2 * m - 1; j++) { const d = x[j + 2 * m] - 2 * x[j + m] + x[j]; s += d * d; n++; }
  return { sig: Math.sqrt(s / (2 * n * t * t)), n: n };
}
function refMdev(y, tau0, m) {
  const x = phase(y, tau0), Nx = x.length, t = m * tau0;
  let s = 0, n = 0;
  for (let j = 0; j <= Nx - 3 * m; j++) {
    let a = 0;
    for (let i = j; i <= j + m - 1; i++) a += x[i + 2 * m] - 2 * x[i + m] + x[i];
    s += (a / m) * (a / m); n++;
  }
  return { sig: Math.sqrt(s / (2 * n * t * t)), n: n };
}

/* deterministicky sum (mulberry32 — viz mdev_test.js, proc ne LCG) */
let seed = 0x1234567;
function rnd() {
  seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296 - 0.5;
}

console.log('--- ADEV / MDEV proti SP1065 (pocet clenu i hodnota) ---');
for (const N of [24, 64, 257]) {
  const y = []; for (let i = 0; i < N; i++) y.push(rnd() * 1e-9);
  for (const [name, fn, ref] of [['adev', api.adev, refAdev], ['mdev', api.mdev, refMdev]]) {
    if (!fn) continue;
    const pts = fn(y, 1.0);
    check(pts.length > 0, name + ' N=' + N + ': vraci body');
    for (const p of pts) {
      const m = Math.round(p.tau);
      const r = ref(y, 1.0, m);
      const rel = Math.abs(p.sig - r.sig) / r.sig;
      check(p.n === r.n && rel < 1e-9,
        name + ' N=' + N + ' m=' + m + ': clenu ' + p.n + ' (ref ' + r.n + '), rel ' + rel.toExponential(1));
    }
  }
}

console.log('--- prukaznost prokladu = mp_fit_significant (firmware) ---');
/* Tabulka kritickych t je ve DVOU kopiich (JS v SPA a `T95_2S` v meas_present.c).
 * Sloucit je nejde (dva jazyky, dve jadra), takze se hlida ROZDIL (L-0020). */
{
  const path = require('path');
  const fw = fs.readFileSync(path.join(__dirname, '..', '..', 'CM7', 'Core', 'Src',
                                       'meas_present.c'), 'utf8');
  const mf = /T95_2S\[30\]\s*=\s*\{([^}]*)\}/.exec(fw);
  const mj = /var T95=\[([^\]]*)\]/.exec(src);
  const tf = mf ? mf[1].split(',').map(v => parseFloat(v)).filter(v => !isNaN(v)) : [];
  const tj = mj ? mj[1].split(',').map(v => parseFloat(v)) : [];
  check(tf.length === 30, 'firmware T95_2S nalezena (30 hodnot, nalezeno ' + tf.length + ')');
  check(tj.length === 30 && tj.every((v, i) => v === tf[i]),
        'SPA T95 je shodna s firmwarovou T95_2S');
}
if (api.fitSig) {
  const S = api.fitSig;
  /* Tytez pripady jako mp_selftest (meas_present.c) — web a displej musi
   * pro tataz data rict totez. */
  check(S(0.4, 200) === true,  'r=0,4  n=200 -> prukazne');
  check(S(0.4, 5) === false,   'r=0,4  n=5   -> neprukazne');
  check(S(0.99, 5) === true,   'r=0,99 n=5   -> prukazne (t=12 > 3,18)');
  /* Hranice pro df=3: r_krit = t/sqrt(df+t^2) = 0,8783. Stary prah 0,5 by
   * r=0,6 pri n=5 vzal jako prukazne. */
  check(S(0.6, 5) === false,   'r=0,6  n=5   -> neprukazne (stary prah |r|>=0,5 by rekl prukazne)');
  check(S(0.87, 5) === false && S(0.88, 5) === true, 'hranice df=3 mezi r=0,87 a 0,88');
  check(S(0.3, 200) === true,  'r=0,3  n=200 -> prukazne (stary prah by rekl neprukazne)');
  check(S(-0.99, 5) === true,  'zaporna korelace se posuzuje v absolutni hodnote');
  check(S(1, 3) === true,      'dokonala primka n=3 -> prukazne');
  check(S(0.99, 2) === false,  'n<3 -> neprukazne');
  check(S(NaN, 100) === false, 'NaN -> neprukazne (L-0087: NaN do bezpecne vetve)');
  /* F-0173: tytez vektory jako mp_selftest — autokorelace snizi n_eff. */
  check(S(0.4, 200, 0.9) === false, 'r=0,4 n=200 rho=0,9 -> neprukazne (n_eff 10,5, df 8)');
  check(S(0.4, 200, -0.5) === true, 'zaporna rho se neuplatni');
  check(S(0.4, 200, 0) === true,    'rho=0 -> jako bez korekce');
}
if (api.fit) {
  /* F-0173: rho z fit() = lag-1 autokorelace reziduí (nezavisla reference). */
  let s = 0; const y = [];
  for (let i = 0; i < 200; i++) { s = 0.9 * s + rnd(); y.push(1e-12 * i + 1e-11 * s); }
  const F = api.fit(y, 1.0);
  let mx = 0, my = 0; for (let i = 0; i < 200; i++) { mx += i; my += y[i]; } mx /= 200; my /= 200;
  let sxx = 0, sxy = 0; for (let i = 0; i < 200; i++) { sxx += (i - mx) ** 2; sxy += (i - mx) * (y[i] - my); }
  const b = sxy / sxx, a = my - b * mx; let e0 = 0, e1 = 0, ep = 0;
  for (let i = 0; i < 200; i++) { const e = y[i] - a - b * i; e0 += e * e; if (i) e1 += e * ep; ep = e; }
  const ok = !!F && typeof F.rho === 'number' && Math.abs(F.rho - e1 / e0) < 1e-9;
  check(ok, 'fit().rho = autokorelace reziduí (' + (ok ? F.rho.toFixed(4) : 'chybi/nesedi')
        + ', ref ' + (e1 / e0).toFixed(4) + ')');
}
if (api.fit) {
  const y = []; for (let i = 0; i < 50; i++) y.push(1e-12 * i + rnd() * 1e-13);
  const F = api.fit(y, 1.0);
  check(F && F.n === 50, 'fit() vraci pocet bodu n (potrebuje ho t-test)');
}

console.log(bad ? ('\nCHYBA: ' + bad + ' kontrol selhalo') : '\nvse OK');
process.exitCode = bad ? 1 : 0;
