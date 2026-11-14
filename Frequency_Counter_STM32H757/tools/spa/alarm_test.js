/* Overi pruh alarmu: co ho rozsviti, co ne, a ze POCITADLA (historie) samy
 * o sobe pruh nevyvolaji — jinak by po jedinem davnem vypadku svitil navzdy. */
const fs = require('fs');
const src = fs.readFileSync(process.argv[2], 'utf8');

function grab(name) {
  const i = src.indexOf('function ' + name + '(');
  if (i < 0) throw new Error('nenalezeno: ' + name);
  let d = 0;
  for (let k = src.indexOf('{', i); k < src.length; k++) {
    if (src[k] === '{') d++;
    else if (src[k] === '}') { d--; if (d === 0) return src.slice(i, k + 1); }
  }
}

/* minimalni DOM stub — pruh je jediny prvek, ktery drawAlarm potrebuje */
const el = { attrs: {}, innerHTML: '' };
el.setAttribute = (k, v) => { el.attrs[k] = v; };
el.removeAttribute = (k) => { delete el.attrs[k]; };
const api = new Function('$',
  grab('mathY') + grab('limitVerdict') + grab('drawAlarm')
  + '\nreturn drawAlarm;')(() => el);

/* Zrcadlo `meas_math.c` — testuje se zvlast, protoze na nem visi i pruh alarmu */
const mm = new Function(grab('mathY') + grab('limitVerdict')
  + '\nreturn {y:mathY, v:limitVerdict};')();

let fail = 0;
function ok(lbl, cond, extra) {
  console.log('  ' + (cond ? 'OK  ' : 'CHYBA ') + lbl + (extra !== undefined ? '  ' + extra : ''));
  if (!cond) fail++;
}
function run(lbl, s, wantOn, wantSev, mustHave) {
  el.attrs = {}; el.innerHTML = '';
  api(s);
  const on = el.attrs['data-on'] === '1';
  const sev = el.attrs['data-sev'];
  let bad = (on !== wantOn);
  if (wantOn && wantSev && sev !== wantSev) bad = true;
  if (wantOn && mustHave && el.innerHTML.indexOf(mustHave) < 0) bad = true;
  console.log('  ' + (bad ? 'CHYBA ' : 'OK  ') + lbl
    + '  -> ' + (on ? '[' + sev + '] ' + el.innerHTML.replace(/<[^>]*>/g, ' ').trim() : 'skryty'));
  if (bad) fail++;
}

const OK = { freq_hz: 1e7, sys_level: 0, selftest: 1,
             mon: { vbat: 0, ocxo: 0, adev: 0 },
             alarms: { fpga: 0, gps: 0, lim: 0, vbat: 0, ocxo: 0, adev: 0 } };
const cp = (o) => JSON.parse(JSON.stringify(o));

console.log('--- kdy ma pruh MLCET ---');
run('vse v poradku', cp(OK), false);
run('null snapshot (vypadek spojeni)', null, false);

console.log('\n--- historie sama pruh NEVYVOLA ---');
let h = cp(OK); h.alarms.gps = 7; h.alarms.fpga = 3;
run('pocitadla > 0, ale nic neni spatne TED', h, false);

console.log('\n--- varovani (zluta) ---');
let a = cp(OK); a.mon.vbat = 1;
run('VBAT pod prahem', a, true, 'warn', 'VBAT');
a = cp(OK); a.mon.ocxo = 1;
run('OCXO mimo pasmo', a, true, 'warn', 'OCXO');
a = cp(OK); a.sys_level = 1;
run('SYS degradace', a, true, 'warn', 'degradovan');

console.log('\n--- chyba (cervena) ---');
a = cp(OK); a.freq_hz = null;
run('bez mereni', a, true, 'bad', 'BEZ MERENI');
a = cp(OK); a.sys_level = 2;
run('SYS ERR', a, true, 'bad', 'SYS ERR');
a = cp(OK); a.selftest = 2;
run('selftest FAIL', a, true, 'bad', 'SELFTEST');

console.log('\n--- chyba PREBIJI varovani (severita = nejhorsi) ---');
a = cp(OK); a.sys_level = 2; a.mon.vbat = 1; a.mon.ocxo = 1;
run('SYS ERR + 2 varovani', a, true, 'bad', 'VBAT');

console.log('\n--- pocitadla se pripoji, kdyz uz pruh sviti ---');
a = cp(OK); a.mon.vbat = 1; a.alarms.vbat = 4; a.alarms.gps = 2;
run('VBAT bad + historie', a, true, 'warn', 'za beh');
console.log('    obsahuje "VBAT 4x": ' + (el.innerHTML.indexOf('VBAT 4x') >= 0));
console.log('    obsahuje "GPS 2x" : ' + (el.innerHTML.indexOf('GPS 2x') >= 0));
if (el.innerHTML.indexOf('VBAT 4x') < 0 || el.innerHTML.indexOf('GPS 2x') < 0) fail++;

console.log('\n--- degradace: chybejici pole nesmi shodit ---');
/* prazdny snapshot = opravdu neni mereni -> pruh MA svitit (puvodni ocekavani
 * v testu bylo spatne, ne kod). */
run('prazdny objekt', {}, true, 'bad', 'BEZ MERENI');
run('jen freq', { freq_hz: 1e7 }, false);
a = { freq_hz: 1e7, sys_level: 2 };
run('bez mon/alarms, ale sys_level=2', a, true, 'bad', 'SYS ERR');

console.log('\n--- pruh se po naprave SAM zhasne ---');
a = cp(OK); a.mon.ocxo = 1; api(a);
const wasOn = el.attrs['data-on'] === '1';
api(cp(OK));
const nowOff = el.attrs['data-on'] === undefined;
console.log('  ' + (wasOn && nowOff ? 'OK  ' : 'CHYBA ') + 'sviti -> zhasne');
if (!(wasOn && nowOff)) fail++;

/* ── Zrcadlo meas_math.c ──────────────────────────────────────────────────────
 * Poradi operaci MUSI sedet s pristrojem: y = en?(m*x+b):x, pak -= null_ref.
 * Meze jsou do PASS INKLUZIVNI. Kdyby se to rozeslo, web by nad TYMIZ daty
 * ukazoval jiny verdikt nez displej — tedy dve pravdy o jednom pristroji. */
console.log('\n--- mathY: poradi operaci jako meas_math_apply ---');
function my(cfg, x) { return mm.y({ math: cfg }, x); }
ok('math VYP -> Y = X', my({ en: false }, 7) === 7);
ok('Y = m*x + b', my({ en: true, m: 2, b: 3 }, 5) === 13);
ok('NULL odecte az PO m*x+b',
   my({ en: true, m: 2, b: 3, null_en: true, null_ref: 10 }, 5) === 3);
ok('NULL bez math jde primo z X',
   my({ en: false, null_en: true, null_ref: 4 }, 10) === 6);
ok('chybejici m/b -> neutralni (1, 0)', my({ en: true }, 9) === 9);
ok('bez snapshotu -> null', mm.y(null, 5) === null);
ok('X null -> null', my({ en: true, m: 2, b: 0 }, null) === null);

console.log('\n--- limitVerdict: meze INKLUZIVNI do PASS ---');
const LC = { limit_en: true, lo: -1, hi: 1 };
ok('uvnitr -> PASS',      mm.v({ math: LC }, 0) === 1);
ok('presne na dolni mezi -> PASS', mm.v({ math: LC }, -1) === 1);
ok('presne na horni mezi -> PASS', mm.v({ math: LC }, 1) === 1);
ok('pod mezi -> LO',      mm.v({ math: LC }, -1.001) === 2);
ok('nad mezi -> HI',      mm.v({ math: LC }, 1.001) === 3);
ok('limity vyp -> 0',     mm.v({ math: { limit_en: false, lo: -1, hi: 1 } }, 99) === 0);
ok('y null -> 0',         mm.v({ math: LC }, null) === 0);

console.log('\n--- LIMIT FAIL v pruhu alarmu ---');
const LIMBAD = { freq_hz: 5, sys_level: 0, selftest: 1,
                 mon: { vbat: 0, ocxo: 0, adev: 0 },
                 alarms: { fpga: 0, gps: 0, lim: 0, vbat: 0, ocxo: 0, adev: 0 },
                 math: { en: false, limit_en: true, lo: 0, hi: 1 } };
run('Y nad horni mezi', LIMBAD, true, 'bad', 'nad horni');
const LO = JSON.parse(JSON.stringify(LIMBAD)); LO.freq_hz = -5;
run('Y pod dolni mezi', LO, true, 'bad', 'pod dolni');
const OKL = JSON.parse(JSON.stringify(LIMBAD)); OKL.freq_hz = 0.5;
run('Y uvnitr mezi -> ticho', OKL, false);
const OFFL = JSON.parse(JSON.stringify(LIMBAD)); OFFL.math.limit_en = false;
run('limity vypnute -> ticho', OFFL, false);

console.log('\n' + (fail ? 'SELHALO kontrol: ' + fail : 'vse OK'));
process.exit(fail ? 1 : 0);
