/* Test grafu GPSDO (TIE / y / kod DAC, 2026-10-16) nad VYEXTRAHOVANYM JS: spec('gp') ma bere rozsah osy z bodu ve stavu LOCK
 * (kdyz jich je >= 10), jinak ze vsech; rozbeh FLL (stovky ppb) tedy nesmi zplostit ustalenou cast; prepinac gpSel meni
 * zdroj, jednotku i formatovani; TIE se kresli jen v LOCK (jinde null). */
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
  throw new Error('neuzavreno: ' + name);
}

const pre = `
var HID={}; var H={f:[],t0:[],t1:[],t2:[],t3:[],vc:[],p0:[],p1:[],p2:[],p3:[],ns:[],hd:[],gx:[],gy:[],gc:[],gs:[]};
var win=300, DL=null, zoom=null, LAST=null, NOMV={p0:3300,p1:5000,p2:2500,p3:3300}, gpSel=0;
function tail(k){ var a=H[k]; return a.slice(Math.max(0,a.length-win)); }
function src(k){ return tail(k); }
function hidOn(g,i){ return false; }
`;
const code = pre
  + grab('clean') + '\n' + grab('niceStep') + '\n' + grab('niceAxis') + '\n' + grab('span') + '\n'
  + grab('spec') + '\n'
  + 'return {spec:spec,H:H,setSel:function(v){gpSel=v;},setWin:function(v){win=v;}};';
const api = new Function(code)();

let bad = 0;
function ok(c, m) { console.log((c ? '  OK   ' : '  CHYBA ') + m); if (!c) bad++; }

/* 40 s rozbehu FLL (y -840 ... -5 ppb, kod 0 -> 24790) a 120 s ustalene LOCK (y +-0.05 ppb, TIE +-30 ns, kod 24794 +-0.4) */
function fill(nFll, nLock) {
  api.H.gx.length = api.H.gy.length = api.H.gc.length = api.H.gs.length = 0;
  for (let i = 0; i < nFll; i++) {
    api.H.gx.push(null); api.H.gy.push(-840 + i * 20); api.H.gc.push(24000 + i * 20); api.H.gs.push(2);
  }
  for (let i = 0; i < nLock; i++) {
    api.H.gx.push(Math.round(30 * Math.sin(i / 7))); api.H.gy.push(0.05 * Math.sin(i / 5));
    api.H.gc.push(24794 + 0.4 * Math.sin(i / 9)); api.H.gs.push(3);
  }
}
api.setWin(1000);

fill(40, 120);
api.setSel(0);
let s = api.spec('gp');
ok(s.series.length === 1 && s.unit === 'ns', 'TIE: jedna rada v ns');
ok(s.series[0].a.slice(0, 40).every(v => v === null), 'TIE je ve FLL null (neni co kreslit)');
ok(s.span && s.span[0] >= -50 && s.span[1] <= 50 && s.span[1] > 25, 'TIE: osa podle LOCK (+-30 ns -> maly rozsah), je ' + JSON.stringify(s.span));

api.setSel(1);
s = api.spec('gp');
ok(s.unit === 'ppb' && s.series[0].a.length === 160, 'y: jednotka ppb, 160 bodu');
ok(s.span && s.span[1] - s.span[0] < 1, 'y: rozbeh FLL (-840 ppb) NEzplosti osu, rozsah ' + JSON.stringify(s.span) + ' je do 1 ppb');
ok(s.fmt(0.1234) === '+0.123 ppb' && s.fmt(-0.5) === '-0.500 ppb', 'y: formatovani se znamenkem a 3 desetinami');

api.setSel(2);
s = api.spec('gp');
ok(s.unit === 'LSB' && s.span[0] > 24790 && s.span[1] < 24800, 'kod: osa kolem 24794 (ne od 24000), je ' + JSON.stringify(s.span));

fill(40, 5);   /* jen 5 bodu LOCK -> osa ze vsech bodu (nema dost dat pro zuzeni) */
api.setSel(1);
s = api.spec('gp');
ok(s.span[1] - s.span[0] > 500, 'mene nez 10 bodu LOCK: osa ze vsech bodu (siroka), ' + JSON.stringify(s.span));

fill(0, 0);
ok(api.spec('gp').span === null, 'bez dat: osa neexistuje (graf ukaze "ceka na data")');

fill(0, 30);
api.setSel(0);
ok(api.spec('gp').series[0].a.every(v => v !== null), 'jen LOCK: TIE bez dir');

console.log(bad ? 'gp_test: CHYBA (' + bad + ')' : 'gp_test: vse OK');
process.exit(bad ? 1 : 0);
