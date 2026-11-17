/* F-0180: prepis `fmt_scpi_hz_sig` (scpi.c) 1:1 do JS (double je v obou IEEE-754,
 * uint64 aritmetika pres BigInt) — overeni vektoru selftestu, ktery bez sondy
 * na cili pustit nejde, a overeni, ze 15 platnych cislic na libovolnem kmitoctu
 * nikdy nevyjde hruber nez 1e-14 relativne (tedy hluboko pod podlahou citace).
 * Beh: node 2026-09-27_fmt_hz_sig.js */
function sig(hz) {
  if (!(hz > -4.0e9 && hz < 4.0e9)) return '9.91E37';
  let neg = hz < 0; if (neg) hz = -hz;
  let k = 1; for (let t = Math.floor(hz) >>> 0; t >= 10; t = Math.floor(t / 10)) k++;
  const dec = 15 - k;
  let p = 1n; for (let i = 0; i < dec; i++) p *= 10n;
  const x = BigInt(Math.floor(hz * Number(p) + 0.5));   /* (uint64_t)(hz*p + 0.5) */
  let frac = x % p, fd = '';
  for (let i = 0; i < dec; i++) { fd = String(frac % 10n) + fd; frac /= 10n; }
  return (neg ? '-' : '') + String(x / p) + '.' + fd;
}
const V = [
  [10000000.0123456789, '10000000.0123457'],
  [10.0000000001234,    '10.0000000001234'],
  [1400000000.123456,   '1400000000.12346'],
  [-0.5,                '-0.50000000000000'],
  [NaN,                 '9.91E37'],
];
let bad = 0;
for (const [v, want] of V) {
  const got = sig(v), ok = got === want; if (!ok) bad++;
  console.log((ok ? '  ok    ' : '  CHYBA ') + String(v).padEnd(22) + ' -> ' + got + (ok ? '' : '  (chci ' + want + ')'));
}
/* relativni chyba po zpetnem precteni pres cely rozsah 0,5 Hz .. 3,9 GHz */
let worst = 0;
for (let e = -0.3; e < 9.59; e += 0.0137) {
  const f = Math.pow(10, e) * (1 + 1e-9 * Math.sin(e * 1000));
  worst = Math.max(worst, Math.abs(parseFloat(sig(f)) / f - 1));
}
console.log('  nejhorsi relativni chyba 0,5 Hz..3,9 GHz: ' + worst.toExponential(1) + ' (chci < 1e-14)');
if (!(worst < 1e-14)) bad++;
/* datalog selftest: x1e5 z presne hodnoty */
const x5 = Math.floor(10000000.012345678 * 1e5 + 0.5);
console.log('  datalog x1e5(10000000.012345678) = ' + x5 + (x5 === 1000000001235 ? '  ok' : '  CHYBA'));
if (x5 !== 1000000001235) bad++;
console.log(bad ? 'CHYBA' : 'vse OK'); process.exitCode = bad ? 1 : 0;
