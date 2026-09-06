/* Smoke test karty NEJISTOTA MERENI (drawUnc / sigmaAtTau) mimo prohlizec.
 * Porovnava proti stejnemu vzorci, jaky pouziva pristroj (`mp_budget`). */
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

const els = {};
const el = id => els[id] || (els[id] = { id, textContent: '', innerHTML: '',
                                         attrs: {}, setAttribute(k, v) { this.attrs[k] = v; },
                                         removeAttribute(k) { delete this.attrs[k]; } });

const code =
  'var LAST=null, lastAdev=null;\n' +
  'function sci(v){ if(v===null||v===undefined||!isFinite(v)) return "--"; return v.toExponential(2); }\n' +
  'function setv(id,v){ var e=$(id); if(v===null||v===undefined){e.textContent="--";}else{e.textContent=v;} }\n' +
  grab('sigmaAtTau') + '\n' + grab('drawUnc') +
  '\n; return {draw:drawUnc, setState:function(s){LAST=s;}, setAdev:function(a){lastAdev=a;},' +
  ' sigmaAtTau:sigmaAtTau};';
const api = new Function('$', code)(el);

/* --- referencni vypocet podle mp_budget() v meas_present.c --------------- */
function ref(hz, gate, tdc_ps, sigma, ref_ppb) {
  const uRes = Math.SQRT2 * (tdc_ps * 1e-12) / gate;
  const uSta = sigma > 0 ? sigma : 0;
  const uRef = ref_ppb * 1e-9;
  const uTot = Math.sqrt(uRes * uRes + uSta * uSta + uRef * uRef);
  return { uRes, uSta, uRef, uTot, U: 2 * uTot, hz_U: 2 * uTot * hz };
}

const TDC = 2500, PPB = 1.0;               /* MP_TDC_PS / MP_REF_PPB */
const state = { freq_hz: 10e6, set_gate_s: 1, tdc_ps: TDC, ref_ppb_x10: PPB * 10 };

console.log('--- sigmaAtTau: vybira nejblizsi tau v LOG mire ---');
api.setAdev([{ tau: 1, sig: 1e-11 }, { tau: 10, sig: 4e-12 }, { tau: 100, sig: 2e-12 }]);
for (const t of [1, 3, 9, 10, 60, 100])
  console.log('   tau=%-5s -> sigma %s', t, api.sigmaAtTau(t).toExponential(2));
console.log('   prazdny ADEV ->', (api.setAdev(null), api.sigmaAtTau(1)));

console.log('\n--- drawUnc proti referenci (mp_budget) ---');
api.setAdev([{ tau: 1, sig: 1e-11 }]);
api.setState(state);
api.draw();
const r = ref(10e6, 1, TDC, 1e-11, PPB);
console.log('   uHead   =', el('uHead').textContent, ' ocekavano', r.hz_U.toExponential(2) + ' Hz');
console.log('   stUnc   =', el('stUnc').textContent, ' ocekavano', r.U.toExponential(2) + ' rel');
const txt = el('tUnc').innerHTML.replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ').trim();
console.log('   tabulka =', txt.slice(0, 150));
console.log('   shoda U :', el('stUnc').textContent === r.U.toExponential(2) + ' rel');

console.log('\n--- delsi hradlo musi nejistotu SNIZIT (u_res ~ 1/gate) ---');
for (const g of [0.1, 1, 10, 100]) {
  api.setState(Object.assign({}, state, { set_gate_s: g }));
  api.draw();
  console.log('   gate=%-5s U=%s  (%s)', g, el('stUnc').textContent, el('uWarn').textContent.slice(0, 46));
}

console.log('\n--- degradace ---');
api.setState(null);            api.draw(); console.log('   bez dat        -> uHead=', el('uHead').textContent);
api.setState({ freq_hz: null, tdc_ps: TDC }); api.draw(); console.log('   freq null      -> uHead=', el('uHead').textContent);
api.setState({ freq_hz: 10e6 });              api.draw(); console.log('   chybi tdc_ps   -> uHead=', el('uHead').textContent);
api.setAdev(null); api.setState(state);       api.draw();
console.log('   bez ADEV       -> U=', el('stUnc').textContent, '|', el('uWarn').textContent.slice(-42));
