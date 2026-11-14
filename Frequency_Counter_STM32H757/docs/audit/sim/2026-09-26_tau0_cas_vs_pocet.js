/* #27: delka oken vzorku statistiky — skladani PODLE CASU (1s tik UiTasku,
 * dnesni stav po F-0171) proti skladani PODLE POCTU mereni (vzorek je hotovy,
 * jakmile Σhradel >= 1 s - hradlo/2).
 * Model FPGA: okno mereni = 0,25 s + cekani na hranu signalu (0..1/f),
 * okna navazuji. Tik: perioda 1000 ms + latence smycky (0..10 ms, jako
 * `if (now - last >= 1000) last = now`). Vystup: prumer a relativni rozptyl
 * delky vzorku (to, co by mela osa τ pocitat jako τ0).
 */
let seed = 3;
function rnd() { seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }

function run(f, T) {
  /* casy konce mereni */
  const ends = [], gates = []; let t = 0;
  while (t < T) { const g = 0.25 + (f > 0 ? rnd() / f : 0); t += g; ends.push(t); gates.push(g); }
  /* (a) podle casu: tik kazdych 1,000 s + latence */
  const byTime = []; let tick = 1.0 + rnd() * 0.01, acc = 0, i = 0;
  while (tick < T) {
    acc = 0; while (i < ends.length && ends[i] <= tick) { acc += gates[i]; i++; }
    if (acc > 0) byTime.push(acc);
    tick += 1.0 + rnd() * 0.01;
  }
  /* (b) podle poctu: hotovo, kdyz Σ >= 1 s - g/2 */
  const byCount = []; acc = 0;
  for (const g of gates) { acc += g; if (2 * acc + g >= 2.0) { byCount.push(acc); acc = 0; } }
  const st = a => { const m = a.reduce((x, y) => x + y, 0) / a.length;
    const sd = Math.sqrt(a.reduce((x, y) => x + (y - m) ** 2, 0) / a.length); return [m, sd / m]; };
  const [ma, sa] = st(byTime), [mb, sb] = st(byCount);
  console.log(('f = ' + f + ' Hz').padEnd(16) + ' podle casu: τ0 ' + ma.toFixed(3) + ' s, rozptyl '
    + (100 * sa).toFixed(1).padStart(5) + ' %   |  podle poctu: τ0 ' + mb.toFixed(3) + ' s, rozptyl '
    + (100 * sb).toFixed(2).padStart(6) + ' %');
}
for (const f of [10e6, 1000, 100, 40, 2.5]) run(f, 20000);
