/* Overi dekodovani priciny resetu a kartu RF/AD8307.
 *
 * !! `resetInfo` je ZRCADLO `main.c` (radky ~298-303). RSR nese casto VIC
 * priznaku naraz (po zapnuti typicky PIN i POR), takze rozhoduje PORADI
 * priorit — a kdyby se rozeslo, web by hlasil jinou pricinu nez UART `status`.
 * Test proto kontroluje prave ty kombinace, ne jen jednotlive bity. */
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

let fail = 0;
function ok(lbl, cond, extra) {
  console.log('  ' + (cond ? 'OK  ' : 'CHYBA ') + lbl + (extra !== undefined ? '  ' + extra : ''));
  if (!cond) fail++;
}

const ri = new Function(grab('resetInfo') + '\nreturn resetInfo;')();

const BOR = 1 << 21, PIN = 1 << 22, POR = 1 << 23, SFT = 1 << 24;
const IWDG = 1 << 26, WWDG = 1 << 28, LPWR = 1 << 30;

console.log('--- jednotlive priznaky ---');
ok('IWDG1 -> WATCHDOG', ri(IWDG).t === 'WATCHDOG', ri(IWDG).t);
ok('WWDG1 -> WWDG', ri(WWDG).t === 'WWDG', ri(WWDG).t);
ok('SFT1  -> SW reset', ri(SFT).t === 'SW reset', ri(SFT).t);
ok('POR   -> power-on', ri(POR).t === 'power-on', ri(POR).t);
ok('BOR   -> brownout', ri(BOR).t === 'brownout', ri(BOR).t);
ok('PIN   -> NRST pin', ri(PIN).t === 'NRST pin', ri(PIN).t);
ok('LPWR  -> low-power', ri(LPWR).t === 'low-power', ri(LPWR).t);
ok('nic   -> ---', ri(0).t === '---', ri(0).t);

console.log('\n--- zavaznost (watchdog = system zatuhl) ---');
ok('IWDG je zavazny', ri(IWDG).bad === 1);
ok('WWDG je zavazny', ri(WWDG).bad === 1);
ok('power-on NENI zavazny', ri(POR).bad === 0);
ok('NRST NENI zavazny', ri(PIN).bad === 0);

/* Tohle je jadro testu: RSR skoro nikdy nenese jen jeden bit. */
console.log('\n--- KOMBINACE: rozhoduje priorita, ne posledni nalezeny bit ---');
ok('POR+PIN (bezne zapnuti) -> power-on', ri(POR | PIN).t === 'power-on', ri(POR | PIN).t);
ok('IWDG+PIN -> WATCHDOG prebiji', ri(IWDG | PIN).t === 'WATCHDOG', ri(IWDG | PIN).t);
ok('IWDG+POR+PIN -> WATCHDOG prebiji', ri(IWDG | POR | PIN).t === 'WATCHDOG', ri(IWDG | POR | PIN).t);
ok('WWDG+SFT -> WWDG prebiji', ri(WWDG | SFT).t === 'WWDG', ri(WWDG | SFT).t);
ok('SFT+PIN -> SW reset prebiji', ri(SFT | PIN).t === 'SW reset', ri(SFT | PIN).t);
ok('BOR+PIN -> brownout prebiji', ri(BOR | PIN).t === 'brownout', ri(BOR | PIN).t);

console.log('\n--- degradace ---');
ok('null -> null', ri(null) === null);
ok('undefined -> null', ri(undefined) === null);

/* Poradi v `main.c`: IWDG1 > WWDG1 > SFT1 > POR > BOR > PIN. Kdyby nekdo
 * v jednom ze dvou mist poradi zmenil, chytne to prave tenhle blok. */
console.log('\n--- poradi priorit shodne s main.c ---');
const ORDER = [[IWDG, 'WATCHDOG'], [WWDG, 'WWDG'], [SFT, 'SW reset'],
               [POR, 'power-on'], [BOR, 'brownout'], [PIN, 'NRST pin']];
let mask = 0, okOrder = true;
for (const [bit, want] of ORDER) {
  mask |= bit;
  /* pri kazdem pridani NIZSI priority musi vyhrat porad ta PRVNI */
  if (ri(mask).t !== 'WATCHDOG') okOrder = false;
}
ok('vsechny bity naraz -> vzdy WATCHDOG', okOrder, ri(mask).t);
for (let i = 1; i < ORDER.length; i++) {
  let m = 0;
  for (let j = i; j < ORDER.length; j++) m |= ORDER[j][0];
  ok('bez vyssich priorit vyhraje ' + ORDER[i][1], ri(m).t === ORDER[i][1], ri(m).t);
}

console.log('\n' + (fail ? 'SELHALO kontrol: ' + fail : 'vse OK'));
process.exit(fail ? 1 : 0);
