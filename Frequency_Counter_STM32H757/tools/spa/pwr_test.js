/* Overi, ze graf NAPAJENI pise urovne ve V a rozptyly v mV (a ze ostatni
 * grafy, ktere `fmtLvl`/`fmtSpr` nemaji, se chovaji jako driv). */
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
const api = new Function(grab('fLvl') + '\n' + grab('fSpr') + '\n' + grab('stats')
  + '\nfunction clean(a){return a.filter(v=>v!==null&&v!==undefined);}'
  + '\nreturn {fLvl:fLvl, fSpr:fSpr, stats:stats};')();

/* spec NAPAJENI vytazeny ze zdroje (musi sedet s tim, co je v httpd_min.c) */
const mLvl = src.match(/s\.fmtLvl=(function\(v,se\)\{[^}]*\});/);
const mSpr = src.match(/s\.fmtSpr=(function\(v\)\{[^}]*\});/);
const mFmt = src.match(/s\.fmt=(function\(v\)\{ return \(v>=0\?'\+':''\)\+v\.toFixed\(1\)\+' mV'; \})/);
console.log('nalezeno ve zdroji: fmtLvl=%s fmtSpr=%s fmt(mV)=%s',
  !!mLvl, !!mSpr, !!mFmt);
const s = { fmt: eval('(' + mFmt[1] + ')'),
            fmtLvl: eval('(' + mLvl[1] + ')'),
            fmtSpr: eval('(' + mSpr[1] + ')') };

/* 12 V vetev: nominal 12000 mV, mereni kolem 11952 mV se zvlnenim ~6 mV */
const nom = 12000, se = { n: '12 V', c: 0, nom };
const raw = [11952, 11949, 11955, 11951, 11958, 11947, 11953];
const dev = raw.map(v => v - nom);           /* to, co je v s.series[].a */
const st = api.stats(dev);

console.log('\n--- radek tabulky pro 12 V vetev ---');
console.log('  AKTUALNI  :', api.fLvl(s, st.last, se));
console.log('  MIN       :', api.fLvl(s, st.min, se));
console.log('  MAX       :', api.fLvl(s, st.max, se));
console.log('  ROZKMIT   :', api.fSpr(s, st.max - st.min));
console.log('  SMER.ODCH.:', api.fSpr(s, st.sd));
console.log('  (osa Y / odecet pod kurzorem:', s.fmt(st.last) + ')');

console.log('\n--- kontroly ---');
const lvl = [api.fLvl(s, st.last, se), api.fLvl(s, st.min, se), api.fLvl(s, st.max, se)];
const spr = [api.fSpr(s, st.max - st.min), api.fSpr(s, st.sd)];
console.log('  urovne konci na V :', lvl.every(x => x.endsWith(' V')));
console.log('  rozptyly konci mV :', spr.every(x => x.endsWith(' mV')));
console.log('  rozptyl bez +     :', spr.every(x => !x.includes('+')));
console.log('  aktualni == 11.953 V ?', api.fLvl(s, st.last, se) === '11.953 V');
console.log('  rozkmit  == 11.0 mV ?', api.fSpr(s, st.max - st.min) === '11.0 mV');

/* 2,5 V VREF: mala vetev musi dat rozumna cisla ve stejnych sloupcich */
const se2 = { n: 'VREF', c: 1, nom: 2500 };
const d2 = [2, -1, 0, 3, -2];
const st2 = api.stats(d2);
console.log('\n--- VREF (nominal 2500 mV) ---');
console.log('  AKTUALNI:', api.fLvl(s, st2.last, se2),
            ' ROZKMIT:', api.fSpr(s, st2.max - st2.min),
            ' SMER.ODCH.:', api.fSpr(s, st2.sd));

/* Graf bez fmtLvl/fmtSpr (napr. TEPLOTY) musi spadnout zpet na fmt */
const sT = { fmt: v => v.toFixed(2) + ' C' };
console.log('\n--- fallback u grafu bez hooku (TEPLOTY) ---');
console.log('  fLvl:', api.fLvl(sT, 45.678, {}), ' fSpr:', api.fSpr(sT, 0.25),
            ' -> stejne jako driv:', api.fLvl(sT, 45.678, {}) === '45.68 C');
