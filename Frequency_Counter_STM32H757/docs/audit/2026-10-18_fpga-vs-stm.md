# Rozbor: co z FPGA lze přesunout na STM (2026-10-18, v0.30.5 na desce, FPGA FW 0x0424)

Zdroje: `status`, `stats`, `regr`, `tdc` z běžící desky po power-cyklu (uptime ~500 s); `sim/res_breakdown.py` nad syntetizovaným netlistem; `docs/TDC_MATEMATIKA.md`; `src/spi_app.v` (`regr_acc`, `win_recip`). Nic se tímto dokumentem neměnilo. Odhady úspor jsou ⬜ ÚVAHA, ne měření; rozpis registrů je podle jmen v netlistu.

## 1. Stav
- Měřicí cesta čistá: CRC err 0, díry 0, resync 0, `CITANI HRAN` 0/0, hodiny 100M výpadků 0. Samokontrola drží na 1 zamítnutém okně z náběhu.
- GPSDO po power-cyklu LOCK za ~2 min.
- `regr`: zamítnuto 8,5 % oken (kumulativně), v posledních ~1 800 oknech 4,7 %; dopoledne dlouhý běh 2,5 %; 2026-10-08 0 %. σ(f_regr − f_2pt) 0,104 ppb (dříve 0,46 ppb).

## 2. Úzká místa
| # | hrdlo | stav |
|---|---|---|
| 1 | FPGA CLS | 85 % (strop brány), setup rezerva 0,06 ns, registry 4 336 z 6 693 |
| 2 | řetěz TDC | 268 tapů, rezerva A 16, B 14 tapů (POZOR); ohrožuje ji chlad |
| 3 | šum značky | ~105 ps (dvoubodový odhad), zdroj neznámý (O4) |
| 4 | zamítání regrese | 4,7-8,5 % oken, příčina neznámá |
| 5 | stacky | IDLE 428 B, DacTask 808 B, FpgaTask 1 232 B, I2C4 1 068 B |
| - | SPI | 128 B / 50 ms = ~0,2 % kapacity 12,5 MHz; není hrdlo |
| - | CPU, heap | CPU 33 %, heap 21,6 kB volných; UiTask 29-31 % (v rozboru optimalizace 5 %, neprověřeno) |

## 3. Co je v FPGA
Registry 4 336: `q_r` + `qd_r` 1 075 (25 %). LUT: dekodér (`tb`, `ld_c`, `pg_c`, `code`) ≈ 1 300 z 3 254 (40 %).

| blok | FF (odhad) | přesun na STM? |
|---|---|---|
| dělič v `regr_acc` (`nq`, `rem`, `dsub`, `xq`, `fin_q`, `dtr`, řízení) | ~300 | ano: FPGA pošle Σx, Σy, n, STM podělí v `double` |
| výstupní registry regrese `xm`/`ym`/`n` | ~224 | ano, nahradí je držení surových součtů |
| kalibrace (`cnt`, `cum`, `xv`, `hcur`, `lutv`, `lut_q3`, ...) | ~300 | ano: histogram se už čte (`tdc hist`), LUT by počítal STM; chybí kanál STM -> FPGA pro zápis LUT |
| duplicitní zachytávací registry rámce (`h_*`, `dt_a_lat`, `dt_b_hold`, `periods_*`) | ~400 | částečně (zbytečné stupně, ne matematika) |
| diagnostika `d_*` | ~150 | ne: napájí `tdc` (rezerva řetězu) |
| `q_r`/`qd_r`, dekodér, ring osc, počítání hran, SPI PHY | - | ne: fyzika a časování |

## 4. Největší páka: histogram kódů místo sčítání časů
Regrese dnes sčítá `t - t0` pro každou z ~312 500 hran segmentu a pak dělí. Alternativa:
- FPGA sčítá jen hrubou část (celé takty, levný celočíselný sčítač) a ukládá do BRAM histogram kódů TDC pro každý segment (512 x ~19 b).
- STM spočte `Σ lut[kód] = Σ_k h[k] * lut[k]` a podělí.
- Matematicky shodné s dneškem (bez zaokrouhlení `floor(Σy*16/n)`). BRAM je využitá na 20 % (5 z 26).
- Odhad úspory ~400-500 FF = 6-9 p.b. CLS (regrese dnes stojí 11-13 p.b.). Pokračování: i uzavírací hrany jako (hrubý tick, kód) a LUT na STM (odpadne `ts_ps` 96, `ref_ts` 96, `h_ts` 64 FF a odčítačky), případně kalibrace na STM. Souhrn odhadem 1 000+ FF, CLS ~65-70 %.
- Cena: protokol. Dva histogramy ~5 kB na okno, rámec má 128 B a je skoro plný (volné bajty 65-67, 97-99, 107) -> bulk režim protokolu v2, ~40 rámců na okno (~3 ms SPI z 250 ms). Riziko: časování BRAM čtení a zápisu při 100 MHz (L-0133).
- Vedlejší výhoda: STM může přepočítat uložená data novou LUT (sloučení lichých binů, korekce teploty, INL).

## 5. Co přesun nepřinese
- Vyšší přesnost sama o sobě. Teoretický limit regrese 1,4e-12; naměřený 5,4-6,5e-11 (2026-10-08) i dnešních 1,85e-10 jsou dány šumem zdroje a oscilátoru a vzácnými odlehlými okny, ne tím, kde se dělí.
- Zkrácení řetězu TDC (O5) nedoporučeno: rezerva B je 14 tapů a při chladu se řetěz zrychlí.
- Uvolněné místo je smysluplné pro regresi CH_B (O6), třetí TDC kanál pro 1PPS (#262), dolaďování časování.

## 6. Pořadí kroků
1. Změřit zamítání regrese: do `regr` přidat rozdělení |f_regr/f_2pt - 1| zamítnutých oken (jen STM). Mez 5e-8 = 12,5 ns; 1 takt = 40 ppb, 2 takty = 80 ppb.
2. Rozhodující pokus o příčině (viz `docs/MERENI_A_KALIBRACE.md`, záznam 2026-10-18 odpoledne): smyčka OFF, pevný kód, δ ≈ +0,8 ppm; ověří hypotézu zamčené (stojící) fáze.
3. Prověřit UiTask 29-31 %.
4. Teprve potom protokol v2 / bulk režim a krok A (histogram kódů).

## 7. Neověřeno / nezahrnuto
Nečetl jsem celé `top.v`, `tdc.v` ani `spi_app.v`; rozpis registrů vychází z jmen v netlistu a z `regr_acc` a `win_recip`. Jednotky `max cekani 246 ms` u DMA2D v `status` jsem neověřoval.

## 8. Doplněk po měření s kanálem A na vlastní referenci (120 min, 2026-10-18)
- Podlaha přístroje s regresí: ADEV 2,9·10⁻¹¹ @ 0,25 s, 5,9·10⁻¹² @ 10 s, 6·10⁻¹³ @ 400 s (stojící fáze, tedy optimisticky). Proto **přesun matematiky z FPGA na STM přesnost nezlepší**: omezuje ji zdroj (1,85·10⁻¹⁰ s externím GPSDO), ne dělení v FPGA.
- Zamítání regrese 0,17 % za 120 min (4,6-8,5 % v prvních minutách po zapnutí), skoky v A 0,5 % oken, medián 66 ps; shluky, ne trend.
- Zdůvodnění přesunu zůstává jen v uvolnění CLS (CH_B regrese, 1PPS TDC), ne v přesnosti. Zápis a čísla: `docs/MERENI_A_KALIBRACE.md`.
