/* Simulace vlivu mrtve doby na ADEV (druhy pruchod modulu 24).
 * Model: faze x(t) na mrizce 0,25 s (FPGA hradlo), mereni k = (x[k+1]-x[k])/0.25,
 * FPGA dava 4 navazujici mereni/s. Displej bere 1x/s NEJNOVEJSI (3 ze 4 zahodi),
 * datalog bere 1x/10 s nejnovejsi. Srovnani:
 *   A) displej: y_1s = jedno 0,25s mereni kazdou sekundu (mrtva doba 0,75 s)
 *   B) spravne: y_1s = prumer 4 navazujicich 0,25s mereni (bez mrtve doby)
 *   C) datalog: y_10s = jedno 0,25s mereni kazdych 10 s
 *   D) spravne 10 s: prumer 40 navazujicich
 *   E) live stage 1 displeje: prumer 10 vzorku A (tj. 10 useku 0,25 s rozhozenych po 10 s)
 */
let seed = 12345;
function rnd() { seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }
function gauss() { let u = 0, v = 0; while (!u) u = rnd(); while (!v) v = rnd();
  return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v); }

function oadev(y, m) {            /* overlapping ADEV, m = pocet vzorku, vraci sigma */
  const M = y.length; let s = 0, n = 0;
  for (let j = 0; j + 2 * m <= M; j++) {
    let a = 0; for (let i = j; i < j + m; i++) a += y[i + m] - y[i];
    s += a * a; n++; }
  return Math.sqrt(s / (2 * m * m * n));
}

function run(kind) {
  const G = 0.25, NS = 4 * 20000;           /* 20000 s dat */
  const x = new Float64Array(NS + 1);
  if (kind === 'WFM') { for (let k = 0; k < NS; k++) x[k + 1] = x[k] + gauss() * Math.sqrt(G); }
  else { for (let k = 0; k <= NS; k++) x[k] = gauss(); }   /* WPM: nezavisla chyba kazde znacky */
  const g = new Float64Array(NS);
  for (let k = 0; k < NS; k++) g[k] = (x[k + 1] - x[k]) / G;
  const A = [], B = [], C = [], D = [];
  for (let s = 0; s < NS / 4; s++) {
    A.push(g[4 * s + 3]);                                  /* nejnovejsi v sekunde */
    B.push((g[4 * s] + g[4 * s + 1] + g[4 * s + 2] + g[4 * s + 3]) / 4);
  }
  for (let s = 0; s < NS / 40; s++) {
    C.push(g[40 * s + 39]);
    let a = 0; for (let i = 0; i < 40; i++) a += g[40 * s + i]; D.push(a / 40);
  }
  const E = []; for (let s = 0; s + 10 <= A.length; s += 10) { let a = 0; for (let i = 0; i < 10; i++) a += A[s + i]; E.push(a / 10); }
  console.log('--- ' + kind + ' ---');
  for (const m of [1, 2, 5, 10, 20, 50]) {
    const a = oadev(A, m), b = oadev(B, m);
    console.log('  tau=' + String(m).padStart(3) + ' s  displej/spravne = ' + (a / b).toFixed(2)
      + '   (sklon spravne ' + b.toExponential(2) + ', displej ' + a.toExponential(2) + ')');
  }
  for (const m of [1, 2, 5]) {
    const c = oadev(C, m), d = oadev(D, m), e = oadev(E, m);
    console.log('  tau=' + String(10 * m).padStart(3) + ' s  datalog/spravne = ' + (c / d).toFixed(2)
      + '   live stage1/spravne = ' + (e / d).toFixed(2) + '   datalog/live = ' + (c / e).toFixed(2));
  }
  const s1 = Math.log10(oadev(A, 50) / oadev(A, 1)) / Math.log10(50);
  const s2 = Math.log10(oadev(B, 50) / oadev(B, 1)) / Math.log10(50);
  console.log('  log-log sklon 1..50 s: displej ' + s1.toFixed(2) + ', spravne ' + s2.toFixed(2));
}
run('WFM');
run('WPM');
