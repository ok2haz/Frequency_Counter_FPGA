/* Kriticky audit 2026-09-27: `gate_ns` z FPGA je FLOOR(dt · 2,5 ns)
 * (Frequency_Counter_FPGA_Module/src/spi_app.v:507  gate_ns <= (dt_q*5) >> 1),
 * zatimco `freq_x100000` se v FPGA pocita z PRESNEHO dt (spi_app.v:515).
 * STM hi-res (`fpga_freq_hires_hz`, `freq_frame_to_lsb`, akumulator `fpga_acc_add`)
 * deli `edges·mul·1e9 / gate_ns` -> jmenovatel je o 0 nebo 0,5 ns mensi.
 * Model citace podle spi_app.v: /4 predelic, N period, casove znacky prvni
 * a posledni hrany kvantovane krokem 2,5 ns (4 faze), dt = rozdil v tickach.
 * Srovnava: (A) x1e5 z FPGA, (B) STM hi-res z gate_ns (dnes), (C) hi-res z ticku
 * zpetne rekonstruovanych `round(gate_ns·2/5)` (pravidlo (1) z FPGA_PROTOCOL_V2_NAVRH.md).
 * Beh: node 2026-09-27_gate_floor.js */
let seed = 7;
function rnd() { seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }
const TICK = 2.5e-9, PRESC = 4, GATE = 0.25;
function run(f, K, coherent) {
  let sA = 0, sB = 0, sC = 0, sumE = 0n, sumG = 0, sumT = 0, odd = 0;
  const period = PRESC / f;
  for (let k = 0; k < K; k++) {
    const N = Math.floor(GATE / period);
    const t0 = coherent ? 0.3e-9 : rnd() * 1e-6;              /* faze prvni hrany */
    const dt = Math.floor((t0 + N * period) / TICK) - Math.floor(t0 / TICK);
    if (dt & 1) odd++;
    const x1e5 = Math.round(N * PRESC * 1e5 / (dt * TICK));     /* FPGA: z presneho dt */
    const gate_ns = Math.floor(dt * 5 / 2);                     /* FPGA: floor */
    const fA = x1e5 / 1e5;
    const fB = N * PRESC * 1e9 / gate_ns;                       /* STM dnes */
    const ticks = Math.round(gate_ns * 2 / 5);                  /* rekonstrukce */
    const fC = N * PRESC / (ticks * TICK);
    sA += fA / f - 1; sB += fB / f - 1; sC += fC / f - 1;
    sumE += BigInt(N * PRESC); sumG += gate_ns; sumT += ticks;
  }
  const accB = Number(sumE) * 1e9 / sumG / f - 1;               /* fpga_acc: Σcyklu/Σgate_ns */
  const accC = Number(sumE) / (sumT * TICK) / f - 1;
  return { A: sA / K, B: sB / K, C: sC / K, accB, accC, odd: odd / K };
}
const e = v => (v >= 0 ? '+' : '') + v.toExponential(2);
for (const [lbl, f, coh] of [['10 MHz asynchronni', 10e6 + 0.0123, false],
                             ['10 MHz koherentni s referenci', 10e6, true],
                             ['1 MHz asynchronni', 1e6 + 0.00123, false],
                             ['100 MHz asynchronni', 100e6 + 0.123, false]]) {
  const r = run(f, 20000, coh);
  console.log(lbl + '  (lichy dt v ' + (r.odd * 100).toFixed(0) + ' % mereni)');
  console.log('   stredni rel. chyba jednoho mereni:  x1e5 ' + e(r.A) + '   hi-res dnes ' + e(r.B)
    + '   hi-res z ticku ' + e(r.C));
  console.log('   prumer Σcyklu/Σhradel (fpga_acc):     dnes ' + e(r.accB) + '   z ticku ' + e(r.accC));
}
/* Podil lichych dt zavisi na zlomku N·perioda/tick, tedy na PRESNEM kmitoctu —
 * sken 10 MHz ± 5 Hz ukaze rozsah systematicke chyby, ne jeden bod. */
{ let mn = 1, mx = -1, mnC = 1, mxC = -1;
  for (let i = 0; i <= 200; i++) {
    const r = run(10e6 - 5 + i * 0.05 + 0.0017, 1500, false);
    mn = Math.min(mn, r.accB); mx = Math.max(mx, r.accB);
    mnC = Math.min(mnC, r.accC); mxC = Math.max(mxC, r.accC);
  }
  console.log('sken 10 MHz ± 5 Hz (201 kmitoctu): hi-res dnes ' + e(mn) + ' .. ' + e(mx)
    + '   z ticku ' + e(mnC) + ' .. ' + e(mxC));
}
