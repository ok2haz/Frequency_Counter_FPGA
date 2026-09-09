/* Overi saveUi/loadUi: ulozene volby prezijí reload, ale POSKOZENY nebo cizi
 * obsah localStorage nesmi rozbit stranku ani propasovat nesmyslnou hodnotu. */
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

/* Stub localStorage + promenne, ktere saveUi/loadUi ctou a pisou.
 * Vraci se PRISTUPOVE funkce, ne kopie — jinak by test kontroloval snapshot. */
function mk(store) {
  const LS = {
    d: Object.assign({}, store),
    getItem(k) { return (k in this.d) ? this.d[k] : null; },
    setItem(k, v) { this.d[k] = String(v); }
  };
  /* !! Harness musi mit VSECHNY promenne, na ktere saveUi/loadUi sahaji.
   * saveUi je cely v try/catch, takze chybejici promenna se neprojevi jako
   * vyjimka, ale jako TICHE NEULOZENI - a test pak hlasi zahadnou chybu
   * v kodu, ktera je ve skutecnosti v harnessu. */
  const f = new Function('localStorage',
    'var win=300, dlWin=0, metric="adev", statPer=0, devMul=1;\n'
    + 'var THEMES=["amber","blue","light"], theme="amber";\n'
    + 'var FRZ=null, FRZPAIR=[["lFreq","rFreq"],["lAdev","rAdev"],["lPn","rPn"]];\n'
    + grab('saveUi') + grab('loadUi')
    + '\nreturn {save:saveUi, load:loadUi,'
    + ' get:function(){return {win:win,dlWin:dlWin,metric:metric,statPer:statPer,'
    + '   devMul:devMul,theme:theme,frz:FRZ};},'
    + ' set:function(o){ if(o.win!==undefined)win=o.win; if(o.dlWin!==undefined)dlWin=o.dlWin;'
    + '  if(o.metric!==undefined)metric=o.metric; if(o.statPer!==undefined)statPer=o.statPer;'
    + '  if(o.devMul!==undefined)devMul=o.devMul; if(o.theme!==undefined)theme=o.theme;'
    + '  if(o.frz!==undefined)FRZ=o.frz; }};')(LS);
  f.LS = LS;
  return f;
}

console.log('--- ulozeni a nacteni (round-trip) ---');
let a = mk({});
a.set({ win: 3600, dlWin: 0, metric: 'tdev', statPer: 1, devMul: 1000 });
a.save();
console.log('  ulozeno: ' + a.LS.d.gpref);
let b = mk(a.LS.d);
b.load();
let g = b.get();
ok('metric', g.metric === 'tdev', g.metric);
ok('statPer', g.statPer === 1, g.statPer);
ok('devMul', g.devMul === 1000, g.devMul);
ok('win', g.win === 3600, g.win);

console.log('\n--- dlouhe okno (datalog) ma prednost pred kratkym ---');
a = mk({}); a.set({ win: 300, dlWin: 86400 }); a.save();
b = mk(a.LS.d); b.load(); g = b.get();
ok('dlWin obnoveno', g.dlWin === 86400, g.dlWin);
ok('win zustal vychozi (kresli se dlWin)', g.win === 300, g.win);

console.log('\n--- prazdne / chybejici uloziste ---');
b = mk({}); b.load(); g = b.get();
ok('bez klice -> vychozi', g.win === 300 && g.metric === 'adev' && g.devMul === 1);

console.log('\n--- POSKOZENY obsah nesmi shodit ani propasovat nesmysl ---');
const junk = ['', 'null', '{', 'neco', '[]', '"retezec"', '{"m":1}',
              '{"m":"zzz","sp":9,"dm":-5,"dw":-1,"w":-1}',
              '{"m":{"a":1},"sp":null,"dm":"x"}'];
for (const j of junk) {
  b = mk({ gpref: j });
  let threw = false;
  try { b.load(); } catch (e) { threw = true; }
  g = b.get();
  const clean = (g.metric === 'adev' && g.statPer === 0 && g.devMul === 1
                 && g.win === 300 && g.dlWin === 0);
  ok('gpref=' + JSON.stringify(j).slice(0, 34), !threw && clean,
     threw ? 'VYJIMKA' : JSON.stringify(g));
}

console.log('\n--- platne hodnoty se PRESTO prijmou ---');
b = mk({ gpref: '{"m":"mtie","sp":1,"dm":1e6,"w":600}' });
b.load(); g = b.get();
ok('mtie + statPer 1 + devMul 1e6 + win 600',
   g.metric === 'mtie' && g.statPer === 1 && g.devMul === 1e6 && g.win === 600,
   JSON.stringify(g));

console.log('\n--- localStorage nedostupny (privatni okno) ---');
const blocked = new Function('localStorage',
  'var win=300, dlWin=0, metric="adev", statPer=0, devMul=1;\n'
  + grab('saveUi') + grab('loadUi')
  + '\nreturn {save:saveUi, load:loadUi};')(
  { getItem() { throw new Error('SecurityError'); },
    setItem() { throw new Error('SecurityError'); } });
let threw = false;
try { blocked.load(); blocked.save(); } catch (e) { threw = true; }
ok('load i save prezijí vyhozenou vyjimku', !threw);

console.log('\n--- paleta (nova volba) ---');
a = mk({}); a.set({ theme: 'light' }); a.save();
b = mk(a.LS.d); b.load();
ok('svetla paleta prezije reload', b.get().theme === 'light', b.get().theme);
b = mk({ gpref: '{"th":"neexistuje"}' }); b.load();
ok('neznama paleta -> vychozi jantar', b.get().theme === 'amber', b.get().theme);

console.log('\n--- zmrazene porovnani ---');
const P3 = ['0,0 1,1', '', '2,2 3,3'];
a = mk({}); a.set({ frz: { p: P3, t: '12:00:00', w: 300, g: 1 } }); a.save();
b = mk(a.LS.d); b.load();
const fz = b.get().frz;
ok('reference prezije reload', !!fz && fz.p.length === 3 && fz.p[0] === P3[0],
   JSON.stringify(fz));
ok('cas zachovan', !!fz && fz.t === '12:00:00', fz && fz.t);
/* Reference se sype do setAttribute -> z ciziho gpref tam nesmi jit cokoli. */
for (const j of ['{"fz":{"p":[1,2,3]}}', '{"fz":{"p":["a"]}}',
                 '{"fz":{"p":"neco"}}', '{"fz":true}', '{"fz":{}}']) {
  b = mk({ gpref: j }); b.load();
  ok('poskozena reference odmitnuta: ' + j.slice(0, 26), b.get().frz === null,
     JSON.stringify(b.get().frz));
}

console.log('\n' + (fail ? 'SELHALO kontrol: ' + fail : 'vse OK'));
process.exit(fail ? 1 : 0);
