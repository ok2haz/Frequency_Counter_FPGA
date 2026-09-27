/* Kriticky audit 2026-09-27: po nulovani statistiky pri zmene signalu (F-0183,
 * `freq_advance` -> `screen_main_stats_reset`) se NEVYPRAZDNI fronta hotovych
 * vzorku (`fpga_stat`) ani rozpracovany akumulator. Model podle kodu:
 *   FpgaTask: mereni po 0,25 s, vzorek = 4 mereni (fpga_acc_add, #27), fronta;
 *   UiTask:   freq_advance kazdych 50 ms (tick_freq), nuluje pri zmene > 1e-4;
 *             app_gpsdo_tick_stats_sample 1x/s odebere VSE z fronty do pyramidy.
 * Signal skoci z f1 na f2 v nahodnem okamziku. Pocita, kolik vzorku se po nulovani
 * dostalo do NOVE pyramidy s kmitoctem jinym nez f2, a jejich y proti novemu
 * nominalu. Beh: node 2026-09-27_reset_fronta.js */
let seed = 3;
function rnd() { seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }
function trial(f1, f2) {
  const tSwitch = 10 + rnd();                 /* s */
  const uiPhase = rnd() * 1.0, advPhase = rnd() * 0.05;
  let queue = [], acc = { c: 0, g: 0, n: 0 }, lastF = f1, ref = f1, reset = false;
  let bad = [], t = 0;
  const events = [];
  for (let k = 1; k <= 120; k++) events.push({ t: k * 0.25, type: 'meas' });
  for (let k = 0; k <= 30 / 0.05; k++) events.push({ t: advPhase + k * 0.05, type: 'adv' });
  for (let k = 0; k <= 30; k++) events.push({ t: uiPhase + k, type: 'pop' });
  events.sort((a, b) => a.t - b.t);
  for (const ev of events) {
    if (ev.type === 'meas') {
      const f = ev.t <= tSwitch ? f1 : (ev.t - 0.25 >= tSwitch ? f2 : f1 + (f2 - f1) * (ev.t - tSwitch) / 0.25);
      lastF = f;                               /* g_freq_x100000 z posledniho mereni */
      acc.c += f * 0.25; acc.g += 0.25; acc.n++;
      if (acc.n === 4) { queue.push(acc.c / acc.g); acc = { c: 0, g: 0, n: 0 }; }
    } else if (ev.type === 'adv') {
      if (Math.abs(lastF / ref - 1) > 1e-4) { ref = lastF; reset = true; }   /* F-0183 */
    } else {
      for (const hz of queue) if (reset && Math.abs(hz / f2 - 1) > 1e-12) bad.push(hz / f2 - 1);
      queue = [];
    }
  }
  return bad;
}
const f1 = 10e6, f2 = 12e6;
let cnt = [0, 0, 0, 0], worst = 0;
for (let i = 0; i < 2000; i++) {
  const b = trial(f1, f2); cnt[Math.min(b.length, 3)]++;
  for (const y of b) worst = Math.max(worst, Math.abs(y));
}
console.log('10 -> 12 MHz, 2000 pokusu: po nulovani vlozeno cizich/smisenych vzorku:');
console.log('   0: ' + cnt[0] + '   1: ' + cnt[1] + '   2: ' + cnt[2] + '   3+: ' + cnt[3]);
console.log('   nejvetsi |y| takoveho vzorku proti novemu nominalu: ' + worst.toExponential(2));
console.log('   dozivani v pyramide (ring 60): stage 0 60 s, stage 1 600 s, stage 2 1,7 h,');
console.log('   stage 3 17 h, stage 4 7 dni — s vahou 1/10^s, tj. pri |y| 0,1 stale 1e-5 na stage 4');
