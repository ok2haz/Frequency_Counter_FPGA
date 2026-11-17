/* Overeni aritmetiky fpga_acc_add/take (F-0171) proti presne hodnote.
 * Prepis 1:1 vcetne uint64 (BigInt) a double casti. */
function mulOf(x100000, edges, gate) {           /* fpga_freq_hires_mul */
  if (gate === 0n || edges === 0n) return 0n;
  const ref = x100000 / 100000n; if (ref === 0n) return 0n;
  for (const M of [1n, 4n, 16n]) {
    if (edges > 4000000000n / M) continue;
    const v = (edges * M * 1000000000n) / gate;
    const d = v > ref ? v - ref : ref - v;
    if (d * 1000n <= ref) return M;
  }
  return 0n;
}
function cyc(x, e, g) {
  const m = mulOf(x, e, g);
  if (m !== 0n) return e * m * 100000n;
  return BigInt(Math.floor(Number(x) * Number(g) * 1e-9 + 0.5));
}
let bad = 0;
function chk(c, msg) { console.log((c ? '  ok    ' : '  CHYBA ') + msg); if (!c) bad++; }

/* 1) 10 MHz, /4 vetev (edges = periody /4), 4 navazujici okna s ruznym hradlem */
{
  const f = 10000000.123456;                     /* skutecny kmitocet */
  let C = 0n, G = 0n, cycTrue = 0, tTrue = 0;
  for (const gate of [250000000n, 250000123n, 249999877n, 250000456n]) {
    const periods = Math.round(f * Number(gate) * 1e-9 / 4);   /* cele periody /4 */
    const edges = BigInt(periods);
    const g2 = BigInt(Math.round(periods * 4 / f * 1e9));      /* hradlo = cele periody */
    const x = BigInt(Math.round(Number(edges) * 4 / Number(g2) * 1e9 * 1e5));
    C += cyc(x, edges, g2); G += g2; cycTrue += periods * 4; tTrue += Number(g2) * 1e-9;
  }
  const hz = Number(C) * 1e4 / Number(G);
  const ex = cycTrue / tTrue;
  chk(Math.abs(hz - ex) / ex < 1e-15, '/4: prumer = presny kmitocet sjednoceni oken (rel ' + (Math.abs(hz - ex) / ex).toExponential(1) + ')');
  chk(Math.abs(hz - f) < 0.05, '/4: blizko skutecnemu (tolerance = kvantizace hradla na 1 ns v modelu) (' + hz.toFixed(6) + ' Hz)');
}
/* 2) zalozni cesta (mul = 0): 1,4 GHz z x1e5, edges nepasuji */
{
  const x = 140000000012345n, g = 250000000n;
  const c = cyc(x, 7n, g);                       /* edges nesedi -> fallback */
  const hz = Number(c) * 1e4 / Number(g);
  chk(Math.abs(hz - Number(x) / 1e5) < 1e-4, 'fallback: 1,4 GHz z x1e5 (' + hz.toFixed(5) + ' Hz)');
}
/* 3) mez uint64: 3600 s pri 1,4 GHz */
{
  const per = 1400000000n * 100000n;             /* cyc_e5 za 1 s */
  chk(per * 3600n < (1n << 62n), '3600 s pri 1,4 GHz < 2^62 (' + (per * 3600n).toString() + ')');
}
console.log(bad ? 'CHYBA' : 'vse OK'); process.exitCode = bad ? 1 : 0;
