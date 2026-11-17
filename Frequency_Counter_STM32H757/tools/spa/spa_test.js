/* Smoke test novych funkci SPA (drawStats / drawHist / fmtSec) mimo prohlizec.
 * Vytahne je ze skutecneho spa.js, doplni DOM stub a pusti nad synteticky daty. */
const fs = require('fs');
const src = fs.readFileSync(process.argv[2], 'utf8');

function grab(name) {
  const i = src.indexOf('function ' + name + '(');
  if (i < 0) throw new Error('nenalezeno: ' + name);
  let d = 0, j = src.indexOf('{', i);
  for (let k = j; k < src.length; k++) {
    if (src[k] === '{') d++;
    else if (src[k] === '}') { d--; if (d === 0) return src.slice(i, k + 1); }
  }
  throw new Error('neuzavreno: ' + name);
}

const els = {};
function el(id) {
  if (!els[id]) els[id] = { id, textContent: '', innerHTML: '', style: {},
                            attrs: {}, setAttribute(k, v) { this.attrs[k] = v; } };
  return els[id];
}

const ctx = {
  $: el,
  M: { t: [], f: [] },
  statPer: 0,
  clean: a => a.filter(v => v !== null && v !== undefined),
  stats: null, fmtSec: null, drawStats: null, drawHist: null,
  Math, console,
};

/* drawHist nove kresli osy pres sdilene helpery -> harness je musi mit taky,
 * jinak test spadne na ReferenceError a hlasi chybu, ktera v kodu neni. */
/* Osove helpery drz POHROMADE: kdykoli `axY`/`axX` dostanou novou zavislost,
 * harness na ni spadne ReferenceError a vypada to jako chyba v kodu.
 * (Stalo se 2x — nejdriv `niceAxis`, pak `axNum`.) */
const AX = ['niceStep', 'niceAxis', 'axDec', 'axNum', 'axY', 'axX'];
const code = [grab('stats'), grab('fmtSec')].concat(AX.map(grab))
             .concat([grab('drawStats'), grab('drawHist')]).join('\n');
const run = new Function('$', 'M', 'ctx', 'clean',
  code + '\n; return {stats:stats,fmtSec:fmtSec,drawStats:drawStats,drawHist:drawHist,' +
  'setPer:function(v){statPer=v;}};\nvar statPer=0;');

/* statPer musi byt uvnitr scope -> vlozime deklaraci pred kod */
const run2 = new Function('$', 'M', 'clean',
  'var statPer=0;\n' + code +
  '\n; return {stats:stats,fmtSec:fmtSec,drawStats:drawStats,drawHist:drawHist,' +
  'setPer:function(v){statPer=v;}};');

const api = run2(el, ctx.M, ctx.clean);

/* --- data: 10 MHz se sumem ~0.02 Hz + jeden vystrelek ------------------- */
let seed = 42;
const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff - 0.5;
for (let i = 0; i < 200; i++) ctx.M.f.push(10e6 + rnd() * 0.04);
ctx.M.f.push(10e6 + 0.5);            /* vystrelek -> musi se objevit v MAX i v ocasu */
for (let i = 0; i < ctx.M.f.length; i++) ctx.M.t.push(Date.now() / 1000 + i);

console.log('--- fmtSec ---');
for (const v of [2.5, 1.5e-3, 4e-6, 1e-7, 3e-13])
  console.log('  ', v, '->', api.fmtSec(v, 4));

console.log('--- drawStats (FREKV) ---');
api.setPer(0); api.drawStats();
console.log('  stStat =', el('stStat').textContent);
console.log('  tStat  =', el('tStat').innerHTML.replace(/<\/?[a-z]+>/g, '|').slice(0, 160));

console.log('--- drawStats (PERIODA) ---');
api.setPer(1); api.drawStats();
console.log('  tStat  =', el('tStat').innerHTML.replace(/<\/?[a-z]+>/g, '|').slice(0, 160));
console.log('  sWarn  =', el('sWarn').textContent.slice(0, 60));

console.log('--- drawHist ---');
api.drawHist();
const bars = (el('hBars').innerHTML.match(/<rect/g) || []).length;
console.log('  sloupcu   =', bars);
console.log('  stHist    =', el('stHist').textContent);
console.log('  yHT (max) =', el('yHT').textContent);
console.log('  hMean x   =', el('hMean').attrs.x1, ' hMed x =', el('hMed').attrs.x1);
console.log('  hWarn     =', el('hWarn').textContent.slice(0, 90));

console.log('--- osa X histogramu + ocasy (>2 sigma) ---');
console.log('  xHL =', el('xHL').textContent, ' xHM =', el('xHM').textContent,
            ' xHR =', el('xHR').textContent);
const nb2 = (el('hBars').innerHTML.match(/class=hb2/g) || []).length;
const nb1 = (el('hBars').innerHTML.match(/class=hb /g) || []).length;
console.log('  sloupcu bezne =', nb1, ' ocasove (hb2) =', nb2,
            ' -> vystrelek odlisen:', nb2 > 0);
console.log('  zakladna y=88?',
  !/y=(9[0-9]|100)\./.test(el('hBars').innerHTML));

/* --- hranicni pripady --------------------------------------------------- */
console.log('--- malo dat ---');
ctx.M.f.length = 0; ctx.M.f.push(10e6, 10e6, 10e6);
api.drawHist(); console.log('  hWarn(3 vz) =', el('hWarn').textContent);
api.setPer(0); api.drawStats(); console.log('  stStat(3 vz) =', el('stStat').textContent);

console.log('--- vsechny vzorky stejne (nulovy rozsah) ---');
ctx.M.f.length = 0; for (let i = 0; i < 20; i++) ctx.M.f.push(10e6);
api.drawHist();
console.log('  sloupcu =', (el('hBars').innerHTML.match(/<rect/g) || []).length,
            ' stHist =', el('stHist').textContent);
console.log('  NaN v markerech?', /NaN/.test(String(el('hMean').attrs.x1) + el('hMed').attrs.x1));
console.log('  NaN v barech?   ', /NaN/.test(el('hBars').innerHTML));
