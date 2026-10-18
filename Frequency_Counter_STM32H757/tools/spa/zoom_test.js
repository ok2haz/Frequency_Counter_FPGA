/* Dymovy test detailu histogramu a spektrogramu Df (STATUS: web, klik na kartu).
 * Spusti SKUTECNE funkce drawZoomHist / drawZoomSpec vytazene ze servirovaneho JS
 * nad atrapou DOM a kontroluje, co nakreslily: pocet sloupcu, normalni krivku,
 * osy a hodnoty v tabulce proti nezavisle spocitane statistice.
 * Pozitivni kontrola: kdyz funkce chybi nebo vyhodi vyjimku, test selze. */
const fs = require('fs');
const src = fs.readFileSync(process.argv[2], 'utf8');
let bad = 0;
function check(cond, msg) {
  if (cond) console.log('  ok     ' + msg);
  else { console.log('  CHYBA  ' + msg); bad++; }
}
function grab(name) {
  const i = src.indexOf('function ' + name + '(');
  if (i < 0) return null;
  let d = 0;
  for (let k = src.indexOf('{', i); k < src.length; k++) {
    if (src[k] === '{') d++;
    else if (src[k] === '}') { d--; if (d === 0) return src.slice(i, k + 1); }
  }
  return null;
}

/* --- atrapa DOM ------------------------------------------------------------ */
function El(tag) {
  return { tag, attrs: {}, children: [], style: {}, textContent: '', _html: '',
    setAttribute(k, v) { this.attrs[k] = String(v); },
    appendChild(c) { this.children.push(c); return c; },
    set innerHTML(v) { this._html = v; }, get innerHTML() { return this._html; } };
}
const ids = {};
function $(id) { return ids[id] || (ids[id] = El('div')); }

const names = ['mk', 'clean', 'stats', 'niceStep', 'niceAxis', 'pointsOf', 'dur', 'drawZoomHist', 'drawZoomSpec'];
const bodies = names.map(n => [n, grab(n)]);
for (const [n, b] of bodies) check(b !== null, 'funkce ' + n + '() je v SPA');
if (bad) process.exit(1);

function run(M, srcF, DL, dlWin) {
  const body = 'var SVGNS="http://www.w3.org/2000/svg";\n' + bodies.map(p => p[1]).join('\n')
    + '\nreturn {drawZoomHist:drawZoomHist, drawZoomSpec:drawZoomSpec};';
  const doc = { createElementNS: (ns, t) => El(t), createElement: t => El(t) };
  const api = new Function('$', 'document', 'M', 'src', 'DL', 'dlWin', body)($, doc, M, srcF, DL, dlWin);
  const svg = El('svg'), ax = El('div');
  return { api, svg, ax };
}

/* deterministicky sum */
let seed = 0x2468ace;
function rnd() {
  seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296 - 0.5;
}

/* --- histogram -------------------------------------------------------------- */
{
  const N = 400, f = [];
  for (let i = 0; i < N; i++) f.push(10000000 + 0.02 * (rnd() + rnd() + rnd()));   /* ~normalni */
  const { api, svg, ax } = run({ f }, k => [], null, 0);
  let ok = true;
  try { api.drawZoomHist(svg, ax); } catch (e) { ok = false; console.log('  vyjimka: ' + e.message); }
  check(ok, 'drawZoomHist nad 400 mereni nevyhodi vyjimku');
  const rects = svg.children.filter(c => c.tag === 'rect');
  check(rects.length === 48, 'histogram: 48 sloupcu (' + rects.length + ')');
  const tot = rects.reduce((s, r) => s, 0);
  check(svg.children.some(c => c.tag === 'polyline' && c.attrs.class === 'ln rf' && c.attrs.points.length > 100),
        'histogram: kresli normalni krivku');
  check(svg.children.filter(c => c.tag === 'line' && c.attrs.class.indexOf('ln') === 0).length === 2,
        'histogram: cara prumeru a medianu');
  check(ax.children.length >= 6, 'histogram: osy maji popisky (' + ax.children.length + ')');
  /* nezavisla statistika */
  let m = 0; for (const v of f) m += v; m /= N;
  let q = 0; for (const v of f) q += (v - m) * (v - m);
  const sd = Math.sqrt(q / (N - 1));
  const h = $('dSt').innerHTML;
  check(h.indexOf('SIGMA (n-1)') >= 0 && h.indexOf(sd.toFixed(5) + ' Hz') >= 0,
        'histogram: sigma v tabulce = ' + sd.toFixed(5) + ' Hz');
  check(h.indexOf('VZORKU N</div><div class=v>400<') >= 0, 'histogram: N = 400');
  /* ~normalni (soucet 3 rovnomernych): spicatost mirne zaporna, sikmost ~0 */
  const mk4 = /SPICATOST<\/div><div class=v>(-?[0-9.]+)</.exec(h);
  check(mk4 && Math.abs(parseFloat(mk4[1])) < 1.0, 'histogram: spicatost ~normalni (' + (mk4 && mk4[1]) + ')');
}
/* malo dat -> bez vyjimky a s vysvetlenim */
{
  const { api, svg, ax } = run({ f: [1, 2, 3] }, k => [], null, 0);
  let ok = true;
  try { api.drawZoomHist(svg, ax); } catch (e) { ok = false; }
  check(ok && svg.children.length === 0 && $('dNote').textContent.indexOf('aspon 8') >= 0,
        'histogram: pod 8 mereni -> prazdny graf a vysvetleni');
}

/* --- spektrogram ------------------------------------------------------------ */
{
  const N = 600, f = [];
  for (let i = 0; i < N; i++) f.push(10000000 + 0.05 * Math.sin(i * 2 * Math.PI / 100) + 0.002 * rnd());
  const { api, svg, ax } = run({ f: [] }, k => (k === 'f' ? f : []), null, 0);
  let ok = true;
  try { api.drawZoomSpec(svg, ax); } catch (e) { ok = false; console.log('  vyjimka: ' + e.message); }
  check(ok, 'drawZoomSpec nad 600 vzorky nevyhodi vyjimku');
  const rects = svg.children.filter(c => c.tag === 'rect');
  check(rects.length === N, 'spektrogram: sloupec na vzorek (' + rects.length + ')');
  const cls = {}; rects.forEach(r => { cls[r.attrs.class] = 1; });
  check(Object.keys(cls).length >= 4, 'spektrogram: pouziva vic barevnych trid (' + Object.keys(cls).join(',') + ')');
  check(svg.children.some(c => c.tag === 'polyline' && c.attrs.points.length > 100), 'spektrogram: kresli krivku odchylky');
  check(ax.children.length === 10, 'spektrogram: 5 popisku Y + 5 popisku X (' + ax.children.length + ')');
  check(ax.children.filter(c => c.tag === 'u').pop().textContent === 'ted', 'spektrogram: posledni popisek X = ted');
  const h = $('dSt').innerHTML;
  check(h.indexOf('ROZSAH</div><div class=v>+-0.05') >= 0, 'spektrogram: rozsah ~ +-0.05 Hz');
  check(h.indexOf('VZORKU</div><div class=v>600<') >= 0, 'spektrogram: N = 600');
}
{
  const { api, svg, ax } = run({ f: [] }, k => [1, 2], null, 0);
  let ok = true;
  try { api.drawZoomSpec(svg, ax); } catch (e) { ok = false; }
  check(ok && svg.children.length === 0, 'spektrogram: pod 8 vzorku -> prazdny graf');
}
process.exit(bad ? 1 : 0);
