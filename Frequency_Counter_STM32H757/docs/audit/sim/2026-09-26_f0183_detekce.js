/* F-0183 + F-0184: prepis rozhodnuti ve `freq_advance` (screen_main.c) — kdy se
 * statistika vynuluje a kdy se meni nominal (reference y).
 *   STARE: kazda zmena poctu celych cislic = prestavba + reset + novy nominal.
 *   NOVE:  zmena radu = jen prestavba formatu (nominal zustava); reset + novy
 *          nominal jen pri prvnim realnem mereni nebo zmene signalu > 1e-4.
 * Scenare: stabilni signal se sumem NESMI resetovat (i pres hranici dekady),
 * zmena signalu MUSI resetovat prave jednou. Pocitaji se i skoky nominalu. */
let seed = 9;
function rnd() { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648 - 0.5; }
function digits(f) { let n = 1; for (let t = Math.floor(f); t >= 10; t = Math.floor(t / 10)) n++; return n; }
function run(seq, neu) {
  let ref = 0, intDig = 0, nominal = 0, resets = 0, nomJumps = 0;
  for (const f of seq) {
    const x = Math.round(f * 1e5), hz = x / 1e5, idg = digits(hz);
    const fmt = (idg !== intDig);
    let sig;
    if (!neu) sig = fmt;                                   /* stare chovani */
    else sig = (ref <= 0) || Math.abs(hz / ref - 1) > 1e-4;
    if (fmt || sig) {
      intDig = idg;
      const nn = Math.floor(hz);
      if (sig) { resets++; ref = hz; if (nominal && nn !== nominal) nomJumps++; nominal = nn; }
    }
  }
  return { resets, nomJumps };
}
const S = (f, n, rel) => Array.from({ length: n }, () => f * (1 + rel * rnd()));
const CASES = [
  ['stabilni 10 MHz, sum 1,4e-8 (kmita pres hranici dekady)', S(1e7, 4000, 2.8e-8), 1],
  ['40,9 Hz (|y| 2 % proti nominalu)',                       S(40.9, 4000, 1e-6), 1],
  ['posun o 5e-5 (drift, bez zmeny signalu)', [...S(1e7 * 1.0001, 100, 1e-8), ...S(1e7 * 1.00015, 100, 1e-8)], 1],
  ['10 -> 12 MHz',                                           [...S(1.0001e7, 100, 1e-8), ...S(1.2e7, 100, 1e-8)], 2],
  ['40 -> 60 Hz',                                            [...S(40, 100, 1e-6), ...S(60, 100, 1e-6)], 2],
];
let bad = 0;
for (const [lbl, seq, want] of CASES) {
  seed = 9;
  const o = run(seq, false), n = run(seq, true);
  const ok = (n.resets === want && n.nomJumps === Math.max(0, want - 1));
  if (!ok) bad++;
  console.log((ok ? '  ok    ' : '  CHYBA ') + lbl.padEnd(55) + ' nove: reset ' + n.resets
    + ' (chci ' + want + '), skoky nominalu ' + n.nomJumps + '  |  stare: reset ' + o.resets);
}
console.log(bad ? 'CHYBA' : 'vse OK'); process.exitCode = bad ? 1 : 0;
