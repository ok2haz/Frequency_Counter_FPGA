/* F-0186: prepis noveho emulatoru ramce (fpga_freq.c sim_build_frame) + STM
 * cesty (fpga_freq_hires_mul, fpga_freq_dt_ticks, fpga_freq_hires_hz) do JS
 * (uint64 pres BigInt). Overuje na rozsahu 4 Hz .. 1,4 GHz, ze:
 *  - kontrola nasobitele projde (mul = 1, emulator pocita nedeleny signal),
 *  - hi-res z ticku se od x1e5 z "FPGA" lisi jen o zaokrouhleni x1e5 (<= 5e-6 Hz),
 *  - stredni chyba hi-res z ticku proti pravde je jen sum TDC, kdezto deleni
 *    `gate_ns` primo (stary kod) dava systematicky +0..2e-9.
 * Beh: node 2026-09-27_f0186_emulator.js */
let seed = 11;
function rnd() { seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }
const TPS = 400000000;                           /* FPGA_TICKS_PER_S */
function frame(hz) {                             /* = sim_build_frame */
  let edges = Math.floor(hz * 0.25); if (edges === 0) edges = 1;
  const ph = Math.floor(rnd() * 1000) / 1000;
  let ticks = Math.floor(edges / hz * TPS + ph); if (ticks === 0) ticks = 1;
  const gate_ns = Math.floor(ticks * 5 / 2);
  const fx = Math.floor(edges * 1e5 * TPS / ticks + 0.5);
  return { edges, gate_ns, fx, ticks };
}
function dtTicks(g) { return (BigInt(g) * 2000n + 2500n) / 5000n; }
function hiresMul(fx, edges, g) {                /* = fpga_freq_hires_mul */
  const ref = BigInt(fx) / 100000n; if (ref === 0n) return 0;
  for (const M of [1n, 4n, 16n]) {
    if (BigInt(edges) > 4000000000n / M) continue;
    const v = (BigInt(edges) * M * 1000000000n) / BigInt(g);
    const d = v > ref ? v - ref : ref - v;
    if (d * 1000n <= ref) return Number(M);
  }
  return 0;
}
let bad = 0;
for (const hz of [4.123, 40.9, 1000.37, 32768.0001, 1e6 + 0.37, 1e7 + 0.0123, 1e8 + 1.7, 3.9e8 + 11, 1.4e9 + 123]) {
  let sOld = 0, sNew = 0, maxd = 0, mulOk = true; const K = 400;
  for (let k = 0; k < K; k++) {
    const f = frame(hz);
    const mul = hiresMul(f.fx, f.edges, f.gate_ns); if (mul !== 1) mulOk = false;
    const t = Number(dtTicks(f.gate_ns)); if (t !== f.ticks) mulOk = false;
    const hNew = f.edges * mul * TPS / t;
    const hOld = f.edges * mul * 1e9 / f.gate_ns;
    maxd = Math.max(maxd, Math.abs(hNew - f.fx / 1e5));
    sNew += hNew / hz - 1; sOld += hOld / hz - 1;
  }
  const ok = mulOk && maxd <= 5.1e-6 * Math.max(1, hz / 1e9);
  if (!ok) bad++;
  console.log((ok ? '  ok    ' : '  CHYBA ') + String(hz).padEnd(14) + ' mul/ticky ' + (mulOk ? 'OK' : 'NE')
    + '  |hi-res - x1e5| max ' + maxd.toExponential(1) + ' Hz   stredni chyba: stary ' + (sOld / K).toExponential(1)
    + '  novy ' + (sNew / K).toExponential(1));
}
console.log(bad ? 'CHYBA' : 'vse OK'); process.exitCode = bad ? 1 : 0;
