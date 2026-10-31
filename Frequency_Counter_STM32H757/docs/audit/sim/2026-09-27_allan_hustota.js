/* Hustsi Allan (2026-09-27) — overeni dvou zmen v screen_main.c, ktere se na
 * cili bez sondy pustit nedaji:
 *  (a) `adev_at(base, i)` (jedno odecteni misto modula) = puvodni
 *      `(head - count + i + 2R) % R` pro VSECHNY head/count/i pri R = 60;
 *  (b) sousede pro lokalni sklon EDF (nejblizsi bod >= 0,29 dekady):
 *      pri 1-2-5 presne puvodni sousede i-1 / i+1 (vychozi chovani beze zmeny),
 *      pri 5 a 9 bodech na dekadu rozestup >= 0,29 dekady (krome okraju).
 * Beh: node 2026-09-27_allan_hustota.js */
const R = 60;
let bad = 0;
/* (a) */
let na = 0;
for (let head = 0; head < R; head++)
  for (let count = 0; count <= R; count++) {
    const base = (head - count + R) % R;
    for (let i = 0; i < count; i++) {
      let k = base + i; if (k >= R) k -= R;
      const old = (head - count + i + 2 * R) % R;
      if (k !== old || k < 0 || k >= R) { bad++; if (bad < 5) console.log('  CHYBA index', head, count, i, k, old); }
      na++;
    }
  }
console.log('  (a) adev_at = puvodni modulo: ' + na + ' kombinaci, chyb ' + bad);
/* (b) */
const DENS = [[1, 2, 5], [1, 2, 3, 5, 7], [1, 2, 3, 4, 5, 6, 7, 8, 9]];
function grid(d, stages) { const t = []; for (let s = 0; s < stages; s++) for (const m of DENS[d]) t.push(m * Math.pow(10, s)); return t; }
function nb(t, i) {
  let i0 = i, i1 = i;
  while (i0 > 0 && Math.log10(t[i] / t[i0]) < 0.29) i0--;
  while (i1 < t.length - 1 && Math.log10(t[i1] / t[i]) < 0.29) i1++;
  return [i0, i1];
}
for (let stages = 1; stages <= 6; stages++) {
  const t = grid(0, stages).map(v => v * 1.0137);          /* tau0_scale != 1 */
  for (let i = 0; i < t.length; i++) {
    const [i0, i1] = nb(t, i);
    const o0 = i > 0 ? i - 1 : i, o1 = i < t.length - 1 ? i + 1 : i;
    if (i0 !== o0 || i1 !== o1) { bad++; console.log('  CHYBA 1-2-5 soused', stages, i, i0, i1); }
  }
}
console.log('  (b) 1-2-5: sousede totozni s puvodnim i-1/i+1 (1..6 stage)');
for (const d of [1, 2]) {
  const t = grid(d, 6); let minspan = 9;
  for (let i = 1; i < t.length - 1; i++) {
    const [i0, i1] = nb(t, i);
    if (i0 > 0) minspan = Math.min(minspan, Math.log10(t[i] / t[i0]));
    if (i1 < t.length - 1) minspan = Math.min(minspan, Math.log10(t[i1] / t[i]));
  }
  if (!(minspan >= 0.29)) bad++;
  console.log('  (b) ' + DENS[d].length + ' bodu/dek: nejmensi rozestup souseda ' + minspan.toFixed(3) + ' dekady (chci >= 0,29)');
}
console.log(bad ? 'CHYBA' : 'vse OK'); process.exitCode = bad ? 1 : 0;
