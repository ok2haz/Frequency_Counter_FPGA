/* F-0177: porovnani stare a nove segmentace num_layout (prepis 1:1). */
const C = 'C', S = 'S', F = 'F', NONE = '~', MAX = 12;
function oldLayout(I, Fr, nu) {
  const seg = []; let first = I % 3; if (first === 0) first = 3; let rem = I;
  seg.push({ l: first, v: C, u: 0, s: '.' }); rem -= first;
  while (rem > 0 && seg.length < MAX - 1) { seg.push({ l: 3, v: C, u: 0, s: '.' }); rem -= 3; }
  seg[seg.length - 1].s = Fr > 0 ? ',' : NONE;
  if (Fr < 2) nu = Fr; else { if (nu < 1) nu = 1; if (nu > Fr - 1) nu = Fr - 1; }
  const nc = Fr - nu; let p = 1;
  const lv = q => q <= nc ? C : (q === nc + 1 ? S : F), un = q => q === nc ? 1 : 0;
  while (p <= Fr && seg.length < MAX) {
    const l0 = lv(p), u0 = un(p); let len = 0;
    while (p + len <= Fr) { const q = p + len; if (lv(q) !== l0 || un(q) !== u0) break; if (len > 0 && (q - 1) % 3 === 0) break; len++; }
    const e = p + len - 1; seg.push({ l: len, v: l0, u: u0, s: (e % 3 === 0 && e < Fr) ? ' ' : NONE }); p += len;
  }
  seg[seg.length - 1].s = NONE; return seg;
}
function newLayout(I, Fr, nu) {
  const seg = []; const T = I + Fr; let nc, noU = 0;
  if (Fr < 2) { nc = I; noU = 1; } else { if (nu < 1) nu = 1; if (nu > T - 1) nu = T - 1; nc = T - nu; }
  const lv = d => d <= nc ? C : (d === nc + 1 ? S : F), un = d => (!noU && d === nc) ? 1 : 0;
  let p = 1;
  while (p <= I && seg.length < MAX) {
    const l0 = lv(p), u0 = un(p); let len = 1;
    while (p + len <= I) { const q = p + len; if ((I - (q - 1)) % 3 === 0) break; if (lv(q) !== l0 || un(q) !== u0) break; len++; }
    const e = p + len - 1;
    seg.push({ l: len, v: l0, u: u0, s: e === I ? (Fr > 0 ? ',' : NONE) : ((I - e) % 3 === 0 ? '.' : NONE) }); p += len;
  }
  p = 1;
  while (p <= Fr && seg.length < MAX) {
    const d = I + p, l0 = lv(d), u0 = un(d); let len = 0;
    while (p + len <= Fr) { const q = p + len; if (lv(I + q) !== l0 || un(I + q) !== u0) break; if (len > 0 && (q - 1) % 3 === 0) break; len++; }
    const e = p + len - 1; seg.push({ l: len, v: l0, u: u0, s: (e % 3 === 0 && e < Fr) ? ' ' : NONE }); p += len;
  }
  seg[seg.length - 1].s = NONE; return seg;
}
const str = sg => sg.map(x => x.v.repeat(x.l).replace(/./g, (c, i) => (x.u && i === x.l - 1) ? '_' : c) + x.s).join('');
let bad = 0, same = 0;
for (let I = 1; I <= 12; I++) for (let Fr = 0; Fr <= 7; Fr++) for (let nu = 0; nu <= Math.max(0, Fr - 1); nu++) {
  const a = str(oldLayout(I, Fr, nu)), b = str(newLayout(I, Fr, nu));
  if (a !== b) { bad++; console.log('ROZDIL I=' + I + ' F=' + Fr + ' nu=' + nu + '\n  old ' + a + '\n  new ' + b); } else same++;
}
console.log('shodne v puvodnim rozsahu: ' + same + ', rozdilu: ' + bad);
/* nove chovani */
for (const [lbl, I, Fr, nu] of [['10 MHz, 7 des, res 0,14 Hz', 8, 7, 8], ['100 MHz, 6 des, res 1,4 Hz', 9, 6, 7],
                                 ['1,4 GHz, 4 des, res 20 Hz', 10, 4, 6], ['SIM 10 MHz, 6 des, nu 2', 8, 6, 2]]) {
  const sg = newLayout(I, Fr, nu);
  console.log(lbl.padEnd(30) + str(sg) + '   segmentu ' + sg.length);
}
process.exitCode = bad ? 1 : 0;
