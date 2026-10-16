/* Test os grafu Vc (2026-10-16): vlevo kod DAC, vpravo napeti [V]. Dilky jsou hezka cisla v KAZDE jednotce, ale poloha cary
 * se pocita z rozsahu v mV, takze KAZDY popisek musi lezet presne tam, kde na krivce lezi jeho hodnota. Test projde nekolik
 * rozsahu a pro kazdy popisek (vlevo i vpravo) prepocte hodnotu zpet na mV a porovna procentni polohu s `top:`. */
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
  + grab('axY') + '\n' + grab('vcAxes') + '\nreturn vcAxes;';
let vcAxes;
try { vcAxes = new Function(code)(els); } catch (e) { console.log('CHYBA harness: ' + e.message); process.exit(1); }

let bad = 0;
function ok(c, m) { if (!c) { bad++; console.log('  CHYBA ' + m); } }

function labels(id) {
  const out = [], h = els[id].innerHTML, re = /<b class=(\w+) style=top:([\d.]+)%>([^<]*)<\/b>/g;
  let m; while ((m = re.exec(h))) out.push({ top: parseFloat(m[2]), txt: m[3] });
  return out;
}

const ranges = [[0, 100], [50, 70], [900, 1100], [1880, 1892], [0, 4000], [1000.5, 1000.9], [56, 2100]];
for (const [lo, hi] of ranges) {
  els.ylVc = { innerHTML: '' }; els.yrVc = { innerHTML: '' }; els.gyVc = { innerHTML: '' };
  vcAxes({ span: [lo, hi] });
  const L = labels('ylVc'), R = labels('yrVc');
  ok(L.length >= 2, '[' + lo + ',' + hi + '] vlevo aspon 2 dilky (je ' + L.length + ')');
  ok(R.length >= 2, '[' + lo + ',' + hi + '] vpravo aspon 2 dilky (je ' + R.length + ')');
  for (const a of L) {
    const mv = parseFloat(a.txt) / (65536 / 5000);
    const want = 100 - (mv - lo) * 100 / (hi - lo);
    // kod je zaokrouhlen na cele cislo -> chyba polohy max 0,5 LSB = 0,038 mV
    const tol = 0.5 / (65536 / 5000) * 100 / (hi - lo) + 0.1;
    ok(Math.abs(want - a.top) <= tol, '[' + lo + ',' + hi + '] DAC ' + a.txt + ': cara ' + a.top + ' %, ma byt ' + want.toFixed(2) + ' %');
  }
  for (const a of R) {
    const mv = parseFloat(a.txt) * 1000;
    const want = 100 - (mv - lo) * 100 / (hi - lo);
    const tol = 0.0005 * 1000 * 100 / (hi - lo) + 0.1;   // popisek zaokrouhlen na 2-3 desetiny V
    ok(Math.abs(want - a.top) <= tol, '[' + lo + ',' + hi + '] ' + a.txt + ' V: cara ' + a.top + ' %, ma byt ' + want.toFixed(2) + ' %');
  }
  ok(/yu>DAC kod</.test(els.ylVc.innerHTML), '[' + lo + ',' + hi + '] vlevo je jednotka "DAC kod"');
  ok(/yu>V</.test(els.yrVc.innerHTML), '[' + lo + ',' + hi + '] vpravo je jednotka "V"');
  ok(els.yrVc.innerHTML.indexOf('<line') < 0, '[' + lo + ',' + hi + '] pravou osu nekresli mrizku (ta patri levé)');
}
console.log(bad ? 'vcaxes_test: CHYBA (' + bad + ')' : 'vcaxes_test: vse OK (' + ranges.length + ' rozsahu)');
process.exit(bad ? 1 : 0);
