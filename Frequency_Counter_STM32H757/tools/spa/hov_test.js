/* Smoke test odectu pod kurzorem (hovMove / hovOut) mimo prohlizec. */
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

/* fake .cw element */
function mkWrap(kind) {
  const mk = () => ({ className: '', style: {}, innerHTML: '', offsetWidth: 120 });
  return {
    hkind: kind, hcx: mk(), htip: mk(),
    getBoundingClientRect: () => ({ left: 100, width: 200 }),
  };
}

const code = 'var HOV={}, HOVK={freq:1,temp:1,vc:1,pwr:1}; var DL=null;\n'
  + grab('hovOut') + '\n' + grab('hovMove')
  + '\nreturn {mv:hovMove, out:hovOut, setHOV:function(k,v){HOV[k]=v;}, setDL:function(v){DL=v;}};';
const api = new Function(code)();

const fmt = v => (v >= 0 ? '+' : '') + v.toFixed(5) + ' Hz';
const A = []; for (let i = 0; i < 11; i++) A.push(i * 0.001);
api.setHOV('freq', { series: [{ n: 'odchylka', c: 0, a: A }], fmt });

const w = mkWrap('freq');
function at(px) { api.mv({ currentTarget: w, clientX: 100 + px }); return w; }

console.log('--- pozice kurzoru -> index/hodnota (11 vzorku, sirka 200) ---');
for (const px of [0, 100, 200]) {
  at(px);
  const txt = w.htip.innerHTML.replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ').trim();
  console.log('  x=' + String(px).padStart(3) + ' -> ' + txt
    + '   | krizek left=' + w.hcx.style.left);
}

console.log('--- mimo rozsah (clamp) ---');
at(-50); console.log('  x=-50 -> left=' + w.hcx.style.left + ' (ocekavano 0px)');
at(999); console.log('  x=999 -> left=' + w.hcx.style.left + ' (ocekavano 200px)');

console.log('--- prevesení bubliny u praveho okraje ---');
at(10);  console.log('  x=10  -> tip left=' + w.htip.style.left);
at(195); console.log('  x=195 -> tip left=' + w.htip.style.left + ' (musi byt vlevo od kurzoru)');

console.log('--- rezim datalogu (poradi bodu misto casu) ---');
api.setDL({});
at(100);
console.log('  ' + w.htip.innerHTML.replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ').trim());
api.setDL(null);

console.log('--- degradace ---');
api.setHOV('freq', { series: [{ n: 'x', c: 0, a: [1] }], fmt });
at(50); console.log('  1 vzorek        -> tip display=' + w.htip.style.display);
api.setHOV('freq', { series: [{ n: 'x', c: 0, a: [null, null, null] }], fmt });
at(50); console.log('  same null       -> tip display=' + w.htip.style.display);
api.setHOV('freq', null);
at(50); console.log('  chybi spec      -> tip display=' + w.htip.style.display);
api.out({ currentTarget: w });
console.log('  po mouseleave   -> tip display=' + w.htip.style.display
  + ', krizek=' + w.hcx.style.display);
