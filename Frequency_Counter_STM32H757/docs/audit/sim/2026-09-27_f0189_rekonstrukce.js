/* F-0189: prepis nove rekonstrukce z datalogu (app_gpsdo.c stats_seed_tick)
 * do JS. Syntetizuje log se tremi sezenimi a vadami a overuje, ze do pyramidy
 * jde jen POSLEDNI souvisly usek tehoz signalu:
 *   sezeni A: 5000 zaznamu 10 MHz, pak vypnuto 2 h (mezera)
 *   sezeni B: 3000 zaznamu 12 MHz (jiny signal!), pak restart 20 s
 *   sezeni C: 1000 zaznamu 10 MHz, uprostred 50 zaznamu bez prumeru (vypadek)
 *   sezeni D: 800 zaznamu 10 MHz po kratkem restartu 30 s  (navazuje na C)
 * Reference signalu = 10 MHz. Ocekavano: vlozi se C za vypadkem + D, a nic z A/B.
 * Beh: node 2026-09-27_f0189_rekonstrukce.js */
const P = 10, GAP = P + 120;
const log = []; let t = 1.8e9, seq = 1;
function sess(n, f, avgOk) { for (let i = 0; i < n; i++) { log.push({ seq: seq++, t, f, avg: avgOk(i) }); t += P; } }
sess(5000, 10e6, () => 1); t += 7200;
sess(3000, 12e6, () => 1); t += 20;
sess(1000, 10e6, i => (i >= 400 && i < 450) ? 0 : 1); t += 30;
sess(800, 10e6, () => 1);
const ref = 10e6;
const usable = r => r.avg && r.t && Math.abs(r.f / ref - 1) <= 1e-4;
let pyr = [], cuts = 0, lastT = 0, lastSeq = 0, brk = 0, done = 0;
for (const r of log) {                         /* chronologicky, jako smycka v C */
  if (lastSeq && r.seq <= lastSeq) continue;
  lastSeq = r.seq;
  if (!usable(r)) { brk = 1; continue; }
  if (lastT && r.t - lastT > GAP) brk = 1;
  if (brk) { if (done) { pyr = []; cuts++; } done = 0; brk = 0; }
  lastT = r.t; pyr.push(r); done++;
}
const first = pyr[0], last = pyr[pyr.length - 1];
const want = 550 + 800;                        /* C za vypadkem (1000-450) + D */
const allRef = pyr.every(r => r.f === 10e6);
const contiguous = pyr.every((r, i) => i === 0 || r.t - pyr[i - 1].t <= GAP);
const ok = pyr.length === want && allRef && contiguous && last.seq === log[log.length - 1].seq;
console.log('vlozeno ' + pyr.length + ' zaznamu (chci ' + want + '), rezu ' + cuts
  + ', vse 10 MHz: ' + allRef + ', souvisle: ' + contiguous
  + ', konci nejnovejsim: ' + (last.seq === log[log.length - 1].seq)
  + ', zacina seq ' + first.seq);
/* stara logika pro srovnani: vsechno s prumerem, bez casu a signalu */
const old = log.filter(r => r.avg).length;
console.log('stara rekonstrukce by vlozila ' + old + ' zaznamu vcetne 12 MHz sezeni a mezery 2 h');
console.log(ok ? 'vse OK' : 'CHYBA'); process.exitCode = ok ? 0 : 1;
