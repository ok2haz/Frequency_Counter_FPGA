/* Test selektivniho zapinani/vypinani krivek (jen WEB, 2026-10-16): hidOn/togSer/spec nad VYEXTRAHOVANYM JS.
 * Vlastnosti: (1) skryta rada nejde do autoskaly osy (rozsah se zuzi), (2) kdyz jsou skryte VSECHNY, osa zustane
 * podle vsech (jinak by graf ztratil osy), (3) stav je po klucich grafu ('temp' neovlivni 'pwr'), (4) prepnuti
 * dvakrat vrati puvodni stav, (5) hovMove skrytou radu nehlasi. */
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

/* Minimalni prostredi: H (historie), src(), tail, DL, win, LAST, NOMV, localStorage, drawAll/drawZoom/zoom/legSync stuby. */
const pre = `
var HID={}; var H={f:[],t0:[],t1:[],t2:[],t3:[],vc:[],vd:[],p0:[],p1:[],p2:[],p3:[],ns:[],hd:[]};
var win=300, DL=null, zoom=null, LAST=null, NOMV={p0:3300,p1:5000,p2:2500,p3:3300};
var calls={draw:0,zoom:0}; var store={};
var localStorage={getItem:function(k){return store[k]===undefined?null:store[k];},setItem:function(k,v){store[k]=v;}};
function drawAll(){calls.draw++;} function drawZoom(){calls.zoom++;}
function tail(k){ var a=H[k]; return a.slice(Math.max(0,a.length-win)); }
function src(k){ return tail(k); }
`;
const code = pre
  + grab('clean') + '\n' + grab('niceStep') + '\n' + grab('niceAxis') + '\n' + grab('span') + '\n'
  + 'function stats(a){return null;}\n'
  + grab('hidOn') + '\n' + grab('togSer') + '\n' + grab('spec') + '\n'
  + 'function legSync(){}\n'
  + 'return {spec:spec,tog:togSer,hidOn:hidOn,H:H,calls:calls,store:store,reset:function(){HID={};}};';
const api = new Function(code)();

let bad = 0;
function ok(c, m) { console.log((c ? '  OK   ' : '  CHYBA ') + m); if (!c) bad++; }

/* Data: 4 teploty; deska (t1) vyrazne vyssi -> musi menit rozsah osy. */
for (let i = 0; i < 20; i++) {
  api.H.t0.push(45 + i * 0.01); api.H.t1.push(25 + i * 0.01);
  api.H.t2.push(33); api.H.t3.push(38);
}
const sAll = api.spec('temp');
ok(sAll.series.length === 4 && sAll.series.every(x => !x.hid), 'vychozi stav: zadna rada neni skryta');
ok(sAll.span && sAll.span[0] <= 25 && sAll.span[1] >= 45, 'osa pokryva vsechny rady (25..45 C)');

api.tog('temp', 1);   /* skryj DESKU (nejnizsi) */
ok(api.hidOn('temp', 1) && !api.hidOn('temp', 0), 'skryta je jen vybrana rada');
ok(api.calls.draw === 1, 'prepnuti vyvola prekresleni');
const s1 = api.spec('temp');
ok(s1.series[1].hid === true && s1.series[0].hid === false, 'spec oznaci skrytou radu');
ok(s1.span[0] > sAll.span[0], 'skryta rada nejde do autoskaly (spodni mez osy se zvedla)');
ok(JSON.parse(api.store.ghid).temp[1] === 1, 'stav se ulozi do localStorage');

const sp = api.spec('pwr');
ok(sp.series.every(x => !x.hid), 'stav je po klicich grafu: skryti v "temp" nezmeni "pwr"');

api.tog('temp', 1);   /* znovu zobraz */
ok(!api.hidOn('temp', 1), 'dvojite prepnuti vrati puvodni stav');
ok(api.spec('temp').span[0] === sAll.span[0], 'osa se vrati na puvodni rozsah');

for (let j = 0; j < 4; j++) api.tog('temp', j);   /* skryj VSECHNY */
const sNone = api.spec('temp');
ok(sNone.series.every(x => x.hid), 'vsechny rady skryte');
ok(sNone.span && sNone.span[0] === sAll.span[0] && sNone.span[1] === sAll.span[1],
   'kdyz jsou skryte vsechny, osa zustane podle vsech (graf neztrati osy)');

api.reset();
ok(api.spec('freq').series.every(x => !x.hid) && api.spec('vc').series.every(x => !x.hid),
   'graf kmitoctu (jedna rada) a Vc (dve rady) po resetu nic neskryva');

/* #310: graf Vc ma DVE rady (Vc z ADC + povel DAC v mV); skryti jedne prizpusobi osu druhe. */
for (let i = 0; i < 40; i++) { api.H.vc.push(1917 + 0.5 * Math.sin(i / 5)); api.H.vd.push(1899.6 + 2 * Math.sin(i / 7)); }
const v2 = api.spec('vc');
ok(v2.series.length === 2 && v2.series[1].n === 'povel DAC', 'Vc: dve rady (Vc, povel DAC)');
ok(v2.span[0] < 1900 && v2.span[1] > 1917, 'Vc: osa pokryva obe rady (' + v2.span[0].toFixed(1) + '..' + v2.span[1].toFixed(1) + ')');
api.tog('vc', 1);   /* skryj povel DAC */
const v1 = api.spec('vc');
ok(v1.series[1].hid && v1.span[0] > 1915 && v1.span[1] < 1919, 'Vc: po skryti DAC se osa zuzi na samotne Vc (' + v1.span[0].toFixed(1) + '..' + v1.span[1].toFixed(1) + ')');
api.tog('vc', 1); api.tog('vc', 0);   /* DAC zpet, skryj Vc */
const v0 = api.spec('vc');
ok(v0.series[0].hid && v0.span[1] < 1905, 'Vc: po skryti Vc se osa zuzi na samotny povel DAC (' + v0.span[0].toFixed(1) + '..' + v0.span[1].toFixed(1) + ')');
api.reset();

console.log(bad ? 'ser_test: CHYBA (' + bad + ')' : 'ser_test: vse OK');
process.exit(bad ? 1 : 0);
