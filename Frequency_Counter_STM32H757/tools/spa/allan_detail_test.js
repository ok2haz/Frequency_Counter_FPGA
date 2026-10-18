/* Detail Allanu (klik na kartu ALLAN): kresli se krivka? lze v detailu prepnout metriku ADEV/TDEV/MTIE a hustotu bodu 3/5/9 na dekadu?
 * Spusti CELOU SPA nad atrapou DOM (spa_harness.js) s daty jako z /api/stab, otevre detail a klika na prepinace.
 * Zavedeno 2026-10-18 po hlaseni „detail Allanu je prazdny, nejde prepinat ADEV/TDEV/MTIE a body na dekadu“. */
const harness = require('./spa_harness.js');
const path = process.argv[2];
let bad = 0;
function check(c, m) { if (c) console.log('  ok     ' + m); else { console.log('  CHYBA  ' + m); bad++; } }

const body = `
  /* data jako /api/stab: bod = [tau, adev, mdev, hdev, n, mantisa] pro mantisy 1..9 */
  var P=[], dd, k, tau, sg;
  for(dd=0; dd<=4; dd++) for(k=1;k<=9;k++){ tau=k*Math.pow(10,dd); if(tau>10000) continue;
    sg=2e-10/Math.sqrt(tau)+3e-12; P.push([tau, sg, sg*0.8, sg*0.9, Math.max(3,Math.floor(36000/tau)), k]); }
  FWS={real:1, p:P, tau0:1, n:36000, ro:3, rn:35000, rm:4};
  LAST={tdc_ps:105, gate_ns:250000000, freq_hz:1e7, sy1e15:1.4e5};
  M.f=[]; for(var q=0;q<10;q++) M.f.push(1e7); M.t=M.f.map(function(x,i){return i;}); M.k=M.f.map(function(x,i){return i;});
  var out={};
  drawStab();
  out.stabA = stabA ? stabA.length : -1;
  out.nzAd = byId['nzAd'] ? byId['nzAd'].textContent : '';
  openZoom('adev');
  var svg=$('dSvg');
  out.zoomChildren = svg.children.length;
  out.hasCurve = svg.children.some(function(c){ return c.attrs && c.attrs['class']==='ln s3' && (c.attrs.points||'').length>20; });
  out.ttl = $('dTtl').textContent;
  /* prepnuti metriky tlacitkem v ovladacich prvcich: simulace kliknuti na <button data-m=tdev> */
  function click(id, attr, val){ var l=$(id).listeners.click||[]; var e={target:{tagName:'BUTTON', getAttribute:function(a){ return a===attr?val:null; }}}; l.forEach(function(f){ f(e); }); return l.length; }
  out.nMetListeners = click('segMet','data-m','tdev');
  out.metricAfter = metric; out.stabMetricAfter = stabMetric;
  out.ttl2 = $('dTtl').textContent;
  out.nDenListeners = click('segDen','data-d','9');
  out.denAfter = aDen;
  out.zoomPts9 = stabA ? stabA.length : -1;
  out.curve9 = $('dSvg').children.some(function(c){ return c.attrs && c.attrs['class']==='ln s3'; });
  return JSON.stringify(out);
`;
let r;
try { r = JSON.parse(harness(path, body)); } catch (e) { console.log('  CHYBA  vyjimka: ' + e.stack.split('\n').slice(0, 4).join(' | ')); process.exit(1); }
console.log('  ' + JSON.stringify(r));
check(r.stabA > 5, 'nahled: body Allanu spocitany (' + r.stabA + ')');
check(r.zoomChildren > 10 && r.hasCurve, 'detail: nakreslena krivka (deti SVG ' + r.zoomChildren + ')');
check(r.nMetListeners > 0 && r.metricAfter === 'tdev' && r.stabMetricAfter === 'tdev', 'prepinac metriky (segMet) existuje a prepne na TDEV');
check(/TDEV/i.test(r.ttl2), 'detail po prepnuti ukazuje TDEV (' + r.ttl2 + ')');
check(r.nDenListeners > 0 && r.denAfter === 9, 'prepinac hustoty (segDen) prepne na 9 bodu/dekadu');
check(r.zoomPts9 > r.stabA && r.curve9, 'detail po prepnuti hustoty ma vic bodu (' + r.stabA + ' -> ' + r.zoomPts9 + ')');
/* --- druhy scenar: pristroj ma po zapnuti JEN 1 bod Allanu (pyramida ceka na ustaleni reference), web ma ale 300 mereni ----------- */
const body2 = `
  var out={};
  FWS={real:1, p:[[1,3.9e-10,0,0,0,1]], tau0:1, n:197, ro:0, rn:0, rm:4};
  LAST={tdc_ps:105, gate_ns:250000000, freq_hz:1e7, sy1e15:3.9e5, uptime_s:795};
  M.f=[]; M.t=[]; M.k=[]; var sd=0x2468ace;
  for(var q=0;q<300;q++){ sd=(sd*1103515245+12345)&0x7fffffff; M.f.push(1e7+0.02*((sd/0x7fffffff)-0.5)); M.t.push(q); M.k.push(q); }
  drawStab();
  out.stabA2 = stabA ? stabA.length : -1;
  out.warn = $('aWarn').textContent;
  openZoom('adev');
  out.ctl = $('dAdCtl').style.display;
  out.curve = $('dSvg').children.some(function(c){ return c.attrs && c.attrs['class']==='ln s3' && (c.attrs.points||'').length>10; });
  function click(id, attr, val){ var l=$(id).listeners.click||[]; var e={target:{tagName:'BUTTON', getAttribute:function(a){ return a===attr?val:null; }}}; l.forEach(function(f){ f(e); }); return l.length; }
  out.n2 = click('segMet2','data-m','mtie') + click('segDen2','data-d','5');
  out.metric2 = metric; out.den2 = aDen;
  out.ttl = $('dTtl').textContent;
  /* nic k kresleni: zadna data ani web -> detail musi VYSVETLIT, proc je prazdny */
  FWS={real:1, p:[[1,3.9e-10,0,0,0,1]], tau0:1, n:2}; M.f=[1e7,1e7]; M.t=[0,1]; M.k=[0,1];
  drawStab(); drawZoom();
  out.emptyNote = $('dNote').textContent; out.emptySt = $('dSt').innerHTML;
  /* legenda: klik na <span data-g=temp data-s=1> prepne HID.temp[1] */
  var lgSp={getAttribute:function(a){ return a==='data-g'?'temp':(a==='data-s'?'1':null); }};
  lgSp.closest=function(){ return lgSp; };
  var hb = hidOn('temp',1);
  (document.listeners.click||[]).forEach(function(f){ f({target:lgSp}); });
  out.legToggled = (hidOn('temp',1)!==hb);
  return JSON.stringify(out);
`;
let r2;
try { r2 = JSON.parse(harness(path, body2)); } catch (e) { console.log('  CHYBA  vyjimka (scenar 2): ' + e.stack.split('\n').slice(0, 4).join(' | ')); process.exit(1); }
console.log('  ' + JSON.stringify(r2));
check(r2.stabA2 >= 5, 'pristroj ma 1 bod -> web pocita vlastni Allan (' + r2.stabA2 + ' bodu)');
check(/zatim ma jen 1/.test(r2.warn), 'varovani rika, proc se pouziva vypocet webu');
check(r2.ctl === 'flex', 'detail Allanu zobrazi prepinace metriky a hustoty (v overlayi)');
check(r2.curve, 'detail kresli krivku i kdyz pristroj ma jen 1 bod');
check(r2.n2 === 2 && r2.metric2 === 'mtie' && r2.den2 === 5, 'prepinace V DETAILU (segMet2/segDen2) meni metriku na MTIE a hustotu na 5');
check(/ZATIM NENI CO KRESLIT/.test(r2.emptyNote) && r2.emptySt.indexOf('zatim malo bodu') >= 0, 'prazdny detail vysvetli proc je prazdny');
check(r2.legToggled, 'klik na legendu krivky (temp) ji skryje/zobrazi');
process.exit(bad ? 1 : 0);
