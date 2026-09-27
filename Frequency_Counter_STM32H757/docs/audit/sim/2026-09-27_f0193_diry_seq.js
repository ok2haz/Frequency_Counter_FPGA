/* F-0193: diry v SEQUENCE a vzorky statistiky s mrtvou dobou.
 * Model (zjednoduseny, ale se vsemi podstatnymi mechanismy):
 *  - FPGA meri NEPRETRZITE: mereni k konci v case 0,25·k s, seq = k,
 *    nepotvrzene prepise dalsim (spi_app.v:190) -> STM vidi jen posledni.
 *  - FpgaTask polluje po 52 ms (osDelay 50 + zpracovani); obcas je ZABLOKOVANY
 *    (UART `fpgaloop` drzi SPI mutex ~3 s).
 *  - akumulator: vzorek se uzavre pri 2·Σgate + g >= 2 s (fpga_acc_add).
 * Vada = vzorek, jehoz mereni v case nepokryvaji souvisly usek (Σgate < rozpeti).
 * Dale: (a) stary emulator sim_produce (next = now + 250) diru nevyrobi ani pri
 * zablokovanem pollu, novy (pevna mrizka) ano; (b) ztrata signalu: SEQUENCE
 * po navratu navazuje, takze bez breaku pri ztrate by vzorek slepil obe strany.
 * Beh: node 2026-09-27_f0193_diry_seq.js */
const GATE = 0.25, POLL = 0.052, T_END = 3600;

function gapOf(prev, cur) {                /* verny prepis fpga_seq_gap */
  if (prev === 0xFFFFFFFF) return 0;
  const d = (cur - prev) >>> 0;
  if (d === 1) return 0;
  if (d >= 2 && d <= 64) return d - 1;
  return 0xFFFFFFFF;
}

/* Beh FpgaTasku nad realnou FPGA.
 *  o.useGap      nova logika: break pri dire v SEQ (freertos_task_fpga.c)
 *  o.blocks      [[od, do], ...] zablokovani FpgaTasku
 *  o.loss        [od, delka] ztrata signalu (FPGA nemeri, seq stoji)
 *  o.breakOnLoss nova logika: break pri prechodu na ztratu (watchdog FPGA 2,5 s) */
function run(o) {
  const blocks = o.blocks || [], loss = o.loss;
  const blocked = t => blocks.some(b => t >= b[0] && t < b[1]);
  /* Casy konce mereni: pred ztratou po 0,25 s, behem ztraty nic, pak zase. */
  const tEndOf = k => (loss && k * GATE > loss[0]) ? k * GATE + loss[1] : k * GATE;
  const kAt = t => {                       /* posledni dokoncene mereni v case t */
    if (!loss || t < loss[0]) return Math.floor(t / GATE);
    if (t < loss[0] + loss[1]) return Math.floor(loss[0] / GATE);
    return Math.floor((t - loss[1]) / GATE);
  };
  let last = 0xFFFFFFFF, acc = [], bad = [], samples = 0, gaps = 0, missed = 0, lost = 0;
  for (let i = 0; ; i++) {
    const t = i * POLL; if (t >= T_END) break;
    if (blocked(t)) continue;
    const inLoss = loss && t >= loss[0] && t < loss[0] + loss[1];
    const l = (inLoss && t >= loss[0] + 2.5) ? 1 : 0;
    if (l !== lost) { lost = l; if (l && o.breakOnLoss) acc = []; }
    if (inLoss) continue;                              /* VALID=0 -> poll false */
    const k = kAt(t);
    if (k === 0 || k === last) continue;               /* nic noveho */
    const g = gapOf(last, k);
    if (g !== 0 && g !== 0xFFFFFFFF) { gaps++; missed += g; }
    if (o.useGap && g !== 0) acc = [];
    last = k;
    acc.push(tEndOf(k));
    const sum = acc.length * GATE;
    if (2 * sum + GATE >= 2 - 1e-12) {                 /* 2·Σ + g >= 2 s */
      const span = acc[acc.length - 1] - (acc[0] - GATE);
      if (span > sum * 1.001) bad.push(acc[0].toFixed(2) + '..' + acc[acc.length - 1].toFixed(2));
      samples++; acc = [];
    }
  }
  return { samples, bad: bad.length, badAt: bad.slice(0, 3), gaps, missed };
}
const B = []; for (let t = 100; t < T_END; t += 300) B.push([t, t + 3.0]);
const o = run({ blocks: B }), n = run({ blocks: B, useGap: true });
console.log('realna FPGA, 12 zablokovani FpgaTasku po 3 s za hodinu:');
console.log('  stara logika: vzorku ' + o.samples + ', s mrtvou dobou ' + o.bad
  + '  (diry v SEQ ' + o.gaps + ', zmeskano ' + o.missed + ' mereni — nikde nepocitano)');
console.log('  nova logika:  vzorku ' + n.samples + ', s mrtvou dobou ' + n.bad
  + '  (diry ' + n.gaps + ', zmeskano ' + n.missed + ' -> status SEQ FPGA)');

/* (a) emulator: stary vs novy sim_produce, FpgaTask stoji 1 s */
function emu(newGrid) {
  let next = 0, seq = 0, seen = [], last = -1;
  for (let i = 0; i * POLL < 10; i++) {
    const t = i * POLL;
    if (t >= 4 && t < 5) continue;
    if (t >= next) {
      if (newGrid) { const k = Math.floor((t - next) / GATE) + 1; next += k * GATE; seq += k; }
      else { next = t + GATE; seq++; }
    }
    if (seq !== last) { seen.push(seq); last = seq; }
  }
  let g = 0; for (let i = 1; i < seen.length; i++) if (seen[i] - seen[i - 1] > 1) g++;
  return { g, rate: seq / 10 };
}
const eo = emu(false), en = emu(true);
console.log('emulator, FpgaTask stoji 1 s: stary diru ' + eo.g + 'x (vyrobeno ' + eo.rate.toFixed(2)
  + ' mereni/s pri hradle 0,25 s), novy ' + en.g + 'x (' + en.rate.toFixed(2) + ' mereni/s)');

/* (b) ztrata signalu 7 s, bez zablokovani: SEQUENCE navazuje -> dira jen v case */
const lo = run({ useGap: true, loss: [1000.6, 7] });
const ln = run({ useGap: true, loss: [1000.6, 7], breakOnLoss: true });
console.log('ztrata signalu 7 s: jen s breakem pri dire ' + lo.bad + ' vzorek s mrtvou dobou '
  + JSON.stringify(lo.badAt) + ' (dir v SEQ ' + lo.gaps + ' — neviditelne), s breakem pri ztrate ' + ln.bad);

const ok = o.bad > 0 && n.bad === 0 && n.gaps === o.gaps && n.gaps > 0
  && eo.g === 0 && en.g > 0 && en.rate > eo.rate && lo.bad > 0 && lo.gaps === 0 && ln.bad === 0;
console.log(ok ? 'vse OK' : 'CHYBA'); process.exitCode = ok ? 0 : 1;
