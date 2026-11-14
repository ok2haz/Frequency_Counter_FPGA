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

const parts = ['mGrid', 'adev', 'mdev', 'fit', 'effN', 'fitSig', 'floorOf'].map(n => [n, grab(n)]);
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

/* Hustota bodu (BODY/DEK, 2026-09-27): mrizka = mantisy x 10^k jako displej
 * (screen_main.c DENS_M), kazdy bod dal presne podle SP1065 a s polem `m`. */
if (api.mGrid) {
  console.log('--- mrizka tau podle hustoty (3 / 5 / 9 na dekadu) ---');
  const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
  check(eq(api.mGrid(24), [1, 2, 5]), 'vychozi (bez den) = 1-2-5: ' + api.mGrid(24).join(','));
  check(eq(api.mGrid(90, 5), [1, 2, 3, 5, 7, 10, 20, 30]), '5/dek do 3m<=90: ' + api.mGrid(90, 5).join(','));
  check(eq(api.mGrid(40, 9), [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]), '9/dek do 3m<=40: ' + api.mGrid(40, 9).join(','));
  check(api.mGrid(3).length === 1 && api.mGrid(2).length === 0, 'mez 3m <= N');
  const y = []; for (let i = 0; i < 400; i++) y.push(rnd() * 1e-9);
  for (const den of [5, 9])
    for (const [name, fn, ref] of [['adev', api.adev, refAdev], ['mdev', api.mdev, refMdev]]) {
      const pts = fn(y, 1.0, den); let bad = 0;
      for (const p of pts) {
        const r = ref(y, 1.0, p.m);
        if (!(p.m === Math.round(p.tau) && p.n === r.n && Math.abs(p.sig / r.sig - 1) < 1e-9)) bad++;
      }
      check(bad === 0 && pts.length === api.mGrid(400, den).length,
            name + ' ' + den + '/dek N=400: ' + pts.length + ' bodu, vse podle SP1065');
    }
}

/* F-0182: mdev() pocita vnitrni soucet klouzavym oknem. Musi zustat presna i tam,
 * kde by prefixove soucty FAZE ztratily cislice (drift, necentrovane y), a musi
 * byt skutecne rychlejsi nez naivni O(N*m) - jinak oprava nic neudelala.
 * Pozitivni kontrola: prefixova varianta z nalezu dava u offsetu 1e-6 chybu 4e-5
 * (docs/audit/sim/2026-09-27_mdev_presnost.js), takze prah 1e-9 ji zachyti. */
if (api.mdev) {
  console.log('--- mdev: presnost pri driftu/offsetu a rychlost (F-0182) ---');
  const N = 2000;
  const gens = [['drift 1e-9 + sum 1e-13', i => 1e-9 * (i / N - 0.5) + 1e-13 * rnd()],
                ['offset 1e-6 + sum 1e-13', i => 1e-6 + 1e-13 * rnd()]];
  for (const [lbl, g] of gens) {
    const y = []; for (let i = 0; i < N; i++) y.push(g(i));
    let worst = 0, nok = true;
    for (const p of api.mdev(y, 0.25)) {
      const r = refMdev(y, 0.25, Math.round(p.tau / 0.25));
      if (p.n !== r.n) nok = false;
      worst = Math.max(worst, Math.abs(p.sig / r.sig - 1));
    }
    check(nok && worst < 1e-9, 'mdev ' + lbl + ': rel ' + worst.toExponential(1));
  }
  const y = []; for (let i = 0; i < N; i++) y.push(1e-9 * rnd());
  const T = (f, n) => { const t0 = process.hrtime.bigint(); for (let k = 0; k < n; k++) f();
    return Number(process.hrtime.bigint() - t0) / n; };
  const tRef = T(() => { for (let m = 1; 3 * m <= N; m *= 2) refMdev(y, 0.25, m); }, 10);
  const tSpa = T(() => api.mdev(y, 0.25), 100);
  check(tRef / tSpa > 4, 'mdev N=2000 rychlejsi nez naivni O(N*m): ' + (tRef / tSpa).toFixed(0) + 'x (chci > 4x)');
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

/* Podlaha citace: web `floorOf` = firmware `adev_floor_base` (+ transformace
 * metriky). Hodnoty vzorcu overuje simulace docs/audit/sim/2026-09-26_podlaha_tdc.js. */
console.log('--- podlaha citace (floorOf = adev_floor_base) ---');
if (api.floorOf) {
  const T = 2.5e-9, F = api.floorOf;
  const near = (a, b) => Math.abs(a / b - 1) < 1e-9;
  check(near(F('adev', 1, 1, T), T / 2),                 'ADEV tau=1 s: tdc/2');
  check(near(F('adev', 100, 1, T), T / 200),             'ADEV tau=100 s: tdc/200');
  check(near(F('tdev', 4, 1, T), T / (2 * Math.sqrt(12))), 'TDEV m=4: tdc/(2 sqrt(3m))');
  check(near(F('mtie', 10, 1, T), Math.sqrt(3) * T / 2), 'MTIE: sqrt3*tau*tdc/(2 tau)');
  check(near(F('tdev', 2, 0.25, T), T / (2 * Math.sqrt(24))), 'TDEV s tau0 0,25 s: m = tau/tau0');
}
/* F-0190: rada mereni `M` se plni podle `seq_meas` a musi byt SOUVISLA.
 * Pozitivni kontrola: nad SPA pred opravou (bez ingestM/mTau0) test SELZE,
 * a hlavne scenar POLL (kazde 4. mereni) tam rada rostla - tady ne. */
console.log('--- souvislost rady mereni (F-0190) ---');
{
  const names = ['mClear', 'ingestM', 'mTau0'];
  const bodies = names.map(grab);
  names.forEach((n, i) => check(bodies[i] !== null, 'funkce ' + n + '() je v SPA'));
  const decl = grabVar('MAXM') + grabVar('lastSeq') + grabVar('MMISS');
  if (bodies.every(b => b) && decl.indexOf('mMiss') >= 0) {
    const E = new Function(decl + '\nvar streaming=1;\n' + bodies.join('\n')
      + '\nreturn {M:function(){return M;},'
      + 'st:function(){return {q:lastSeq,miss:mMiss,cut:mCut,why:mWhy};},'
      + 'stream:function(v){streaming=v;},ingestM:ingestM,mTau0:mTau0,mClear:mClear};')();
    const T0 = 0.25, add = q => E.ingestM(q, 10e6, q * T0);
    const len = () => E.M().f.length;

    for (let q = 1; q <= 400; q++) add(q);
    check(len() === 400 && E.st().miss === 0, 'SSE souvisle: 400 mereni, 0 chybi');
    check(add(400) === 0 && len() === 400, 'totez mereni podruhe se neprida');
    add(402);                                     /* chybi 401 - ojedinela dira */
    check(len() === 401 && E.st().miss === 1, 'ojedinela dira (1 z 402) se toleruje');
    check(Math.abs(E.mTau0(len()) / T0 - 1) < 1e-12,
          'tau0 pres diru = skutecny rozestup (' + E.mTau0(len()) + ' s)');

    let c0 = E.st().cut;
    for (let q = 1000; q < 1010; q++) add(q);     /* velky skok -> rez */
    add(1011);                                    /* 1 z 11 = 9 % -> rez */
    check(len() === 1 && E.st().cut === c0 + 2, 'dira nad 1 % rady -> rada zacne znovu');

    E.stream(0); c0 = E.st().cut;
    for (let q = 2000; q < 2400; q += 4) add(q);  /* 1 Hz poll pri 4 merenich/s */
    check(len() === 1 && E.st().cut === c0 + 100,
          'poll (kazde 4. mereni): rada neroste, ' + (E.st().cut - c0) + ' rezu');
    check(E.st().why.indexOf('SSE') >= 0, 'duvod v aWarn: ' + E.st().why);
    E.stream(1);

    add(4294967295); add(0);                      /* preteceni uint32 */
    check(len() === 2 && E.st().miss === 0, 'seq 0xFFFFFFFF -> 0 je souvisle');
    for (let q = 1; q < 300; q++) add(q);
    c0 = E.st().cut; add(5);                      /* seq se vratil (reset FPGA) */
    check(len() === 1 && E.st().cut === c0 + 1, 'seq zpet -> rada zacne znovu');

    E.mClear();
    for (let q = 10000; q < 10150; q++) add(q);
    for (let q = 10151; q < 12152; q++) add(q);   /* dira po 150 vzorcich, pak 2001 */
    check(len() === 2000 && E.st().miss === 0,
          'dira vypadla z okna MAXM -> mMiss zpet na 0 (' + E.st().miss + ')');
  }
}

console.log(bad ? ('\nCHYBA: ' + bad + ' kontrol selhalo') : '\nvse OK');
process.exitCode = bad ? 1 : 0;
