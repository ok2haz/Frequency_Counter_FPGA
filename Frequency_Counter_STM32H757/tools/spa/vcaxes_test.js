/* Test os grafu Vc: vlevo kod DAC, vpravo napeti [V], KAZDA osa SVE meritko (2026-10-17, pozadavek uzivatele).
 * `vcAxes(s)` cte `s.spans = [rozsah Vc [mV], rozsah povelu DAC [mV]]` (null = rada skryta). Kazdy popisek musi lezet
 * presne tam, kde na SVE krivce lezi jeho hodnota: leve popisky (kod) se prepocitaji na mV a porovnaji s rozsahem
 * povelu, prave (V) s rozsahem Vc. Rozsahy obou rad jsou zamerne ruzne (ofset ADC ~17 mV, jiny rozkmit).
 * Dal: skryty povel = leva osa prazdna a mrizku kresli prava; skryte Vc = prava osa prazdna. */
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

const els = {};
const code =
  'var els=arguments[0]; function $(id){ return els[id]||(els[id]={innerHTML:""}); }\n'
  + 'var DAC_K=65536/5000;\n'
  + grab('niceStep') + '\n' + grab('niceAxis') + '\n' + grab('axNum') + '\n' + grab('axDec') + '\n'
  + grab('axY') + '\n' + grab('vcAxes') + '\n' + grab('clean') + '\n' + grab('span') + '\n'
  + 'return {vcAxes:vcAxes, span:span};';
let api;
try { api = new Function(code)(els); } catch (e) { console.log('CHYBA harness: ' + e.message); process.exit(1); }
const vcAxes = api.vcAxes;
const K = 65536 / 5000;
/* Meze stejne jako `spec('vc')`: Vc pres span() v mV, povel pres span() v KODECH a zpet na mV. */
function spansOf(V, D) {
  const sv = V ? api.span([[V[0], V[1]]]) : null;
  const sc = D ? api.span([[D[0] * K, D[1] * K]]) : null;
  return [sv, sc ? [sc[0] / K, sc[1] / K] : null];
}

let bad = 0;
function ok(c, m) { if (!c) { bad++; console.log('  CHYBA ' + m); } }

function labels(id) {
  const out = [], h = els[id].innerHTML, re = /<b class=(\w+) style=top:([\d.]+)%>([^<]*)<\/b>/g;
  let m; while ((m = re.exec(h))) out.push({ top: parseFloat(m[2]), txt: m[3] });
  return out;
}
function reset() { els.ylVc = { innerHTML: 'x' }; els.yrVc = { innerHTML: 'x' }; els.gyVc = { innerHTML: 'x' }; }

/* [rozsah Vc], [rozsah povelu] v mV */
const cases = [[[1910, 1922], [1896, 1903]], [[0, 100], [50, 70]], [[900, 1100], [1880, 1892]],
               [[0, 4000], [1000.5, 1000.9]], [[56, 2100], [1899.62, 1899.70]]];
for (const [V0, D0] of cases) {
  const sp = spansOf(V0, D0), V = sp[0], D = sp[1];
  const tag = '[Vc ' + V0 + ' | DAC ' + D0 + ']';
  reset();
  vcAxes({ span: [Math.min(V[0], D[0]), Math.max(V[1], D[1])], spans: sp });
  const L = labels('ylVc'), R = labels('yrVc');
  ok(L.length >= 2, tag + ' vlevo aspon 2 dilky (je ' + L.length + ')');
  ok(R.length >= 2, tag + ' vpravo aspon 2 dilky (je ' + R.length + ')');
  for (const a of L) {
    const mv = parseFloat(a.txt) / (65536 / 5000);
    const want = 100 - (mv - D[0]) * 100 / (D[1] - D[0]);
    const tol = 0.5 / (65536 / 5000) * 100 / (D[1] - D[0]) + 0.1;   // kod zaokrouhlen na cele cislo
    ok(Math.abs(want - a.top) <= tol, tag + ' DAC ' + a.txt + ': cara ' + a.top + ' %, ma byt ' + want.toFixed(2) + ' %');
  }
  for (const a of R) {
    const mv = parseFloat(a.txt) * 1000;
    const want = 100 - (mv - V[0]) * 100 / (V[1] - V[0]);
    const dec = (a.txt.split('.')[1] || '').length;
    const tol = 0.5 * Math.pow(10, -dec) * 1000 * 100 / (V[1] - V[0]) + 0.1;   // popisek zaokrouhlen na `dec` desetin V
    ok(Math.abs(want - a.top) <= tol, tag + ' ' + a.txt + ' V: cara ' + a.top + ' %, ma byt ' + want.toFixed(2) + ' %');
  }
  ok(/yu>DAC kod</.test(els.ylVc.innerHTML), tag + ' vlevo je jednotka "DAC kod"');
  ok(/yu>V</.test(els.yrVc.innerHTML), tag + ' vpravo je jednotka "V"');
  ok(els.gyVc.innerHTML.indexOf('<line') >= 0, tag + ' mrizka existuje (leva osa)');
  ok(els.yrVc.innerHTML.indexOf('<line') < 0, tag + ' popisky prave osy nejsou cary');
}
/* skryty povel DAC: leva osa prazdna, mrizku prevezme prava */
reset();
vcAxes({ span: [1910, 1922], spans: spansOf([1910, 1922], null) });
ok(els.ylVc.innerHTML === '', 'skryty DAC: leva osa prazdna');
ok(labels('yrVc').length >= 2 && els.gyVc.innerHTML.indexOf('<line') >= 0, 'skryty DAC: prava osa i mrizka zustanou');
/* skryte Vc: prava osa prazdna */
reset();
vcAxes({ span: [1896, 1903], spans: spansOf(null, [1896, 1903]) });
ok(els.yrVc.innerHTML === '' && labels('ylVc').length >= 2, 'skryte Vc: prava osa prazdna, leva zustane');

console.log(bad ? 'vcaxes_test: CHYBA (' + bad + ')' : 'vcaxes_test: vse OK (' + cases.length + ' dvojic rozsahu + skryti)');
process.exit(bad ? 1 : 0);
