# A/B test bitstreamu: nový `796c009` proti starému `bd20e77` (2026-10-27)

**Otázka:** zhoršila se přesnost měření kmitočtu po nasazení bitstreamu `796c009` (omezení předděličky na pinech 300 MHz místo 400 MHz)?
Stav k 2026-10-27 večer: nový běh má Allanovu odchylku σy @ 1 s ≈ 1,7–2,0·10⁻¹⁰, starý běh z noci medián 1,2·10⁻¹⁰ (rozsah útvarů 0,95–2,0·10⁻¹⁰); σy @ 16 s nový ≈ 0,9–1,2·10⁻⁹ proti 6,2·10⁻¹⁰. Rozdíl ~1,3–1,9×, ale starý běh je z noci a nový z odpoledne a večera, zdroj (externí GPSDO s okenní anténou) mívá denní kolísání. Jednotlivé srovnání proto nestačí.

## Pravidla (aby rozdíl nezpůsobilo něco jiného)

- Stejné zapojení: stejný zdroj na kanálu A, stejné kabely a anténa, nic se nepřipojuje ani nepřepíná během celého testu.
- Stejný firmware CM7 + CM4 (v0.42.0) ve všech během; mění se **jen FPGA bitstream**.
- Před každým během **úplný power-cyklus** (ne jen reset) a pak 45 minut zahřátí; měří se až od uptime 2700 s.
- Každý běh minimálně 135 minut uptime (45 min zahřátí + 90 min měření = 3 úseky po 30 min). Lepší je 225 min (5 úseků).
- Webhook vypnutý (`alarmpush off`), do přístroje se během běhu nic nepíše; UART jen `status`.
- Pořadí **A-B-A-B**: nový, starý, nový, starý. Tím se odliší denní doba zdroje od bitstreamu. Celý test ≈ 9 hodin; zkrácená varianta A-B-A (≈ 7 h) je slabší.

## Postup jednoho běhu

1. Nahrát do FPGA bitstream:
   - **A (nový):** `C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_FPGA_Module\ab_test\FW_0x0508_v3_20261027-1514_796c009.fs`
   - **B (starý):** `C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_FPGA_Module\ab_test\FW_0x0508_v3_20261026-1944_bd20e77.fs`
2. Úplný power-cyklus (odpojit napájení desky).
3. Spustit sběr (skript sám čeká na uptime a LOCK a zapíše identitu bitstreamu):

```powershell
powershell -ExecutionPolicy Bypass -File "C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_STM32H757\tools\ab_mereni.ps1" -Name novy1
```

Další běhy: `-Name stary1`, `-Name novy2`, `-Name stary2`. Výchozí cílový uptime je 8100 s (45 min + 90 min); pro 5 úseků `-TargetUptimeS 11700`.

Výstup: `raw_<Name>.csv` (surová měření) a `raw_<Name>.txt` (`status` při stažení, obsahuje řádek `FPGA BUILD`; zkontrolovat, že odpovídá zamýšlenému bitstreamu).

## Vyhodnocení

```powershell
cd C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_STM32H757
python tools\ab_report.py novy1=raw_novy1.csv stary1=raw_stary1.csv
python tools\ab_report.py novy2=raw_novy2.csv stary2=raw_stary2.csv
python tools\ab_report.py novy1=raw_novy1.csv stary2=raw_stary2.csv
```

Skript vypíše ADEV po úsecích a medián každého běhu, poměr mediánů (první ku druhému) a stručný verdikt. Čtení:

| poměr nový / starý | závěr |
|---|---|
| < 1,3 ve všech τ | v rozptylu, rozdíl neprokázán; bitstream `796c009` zůstává |
| > 1,5 ve všech τ **v obou dvojicích** (A-B i B-A) | skutečné zhoršení, vrátit omezení předděličky na 2,5 ns (viz níže) |
| jen v jedné dvojici | kolísání zdroje (denní doba), nebo malý efekt; doplnit další dvojici |

## Co udělat při potvrzeném zhoršení

1. V `Frequency_Counter_FPGA_Module/src/timing_v3.sdc` vrátit `pin_a`/`pin_b` na `-period 2.5 -waveform {0 1.25}`.
2. Znovu vydat bitstream: `python tools/fpga_release.py --v3 --build` (z `Frequency_Counter_FPGA_Module`), 8 sestavení `python tools/pnr_seeds.py run base 8 -j 4`.
3. Nahrát `FW_0x0508_v3_20261026-1944_bd20e77.fs` jako dočasný návrat (už ověřený na desce, SPI bez chyb).

## Poznámky

- `ab_report.py` bere kmitočet kanálu A z `raw_fetch.py` (ADEV relativně k 10 MHz); konstantní offset zdroje na výsledek nemá vliv.
- Čas v CSV je uptime desky; denní doba běhů se do nich nezapisuje, proto si u každého běhu poznamenej čas startu (skript ho vypisuje do konzole).

## Výsledek noci 2026-10-27/28: zhoršení nepotvrzeno

Jedna noc, `tools/ab_noc.ps1` (deska zapnuta 20:36, FPGA `796c009`, CM7/CM4 v0.42.0) proti noci 26.–27. 10. (`bd20e77`, `noc_raw.csv`, zapnuto 21:12), srovnáno jen v okně dne 22:00–05:00, úseky po 30 min po 45 min zahřátí (14 úseků nový, 9 starý).

| τ | medián nový | medián starý | poměr |
|---|---|---|---|
| 1 s | 1,26·10⁻¹⁰ | 1,19·10⁻¹⁰ | 1,06 |
| 16 s | 6,16·10⁻¹⁰ | 5,73·10⁻¹⁰ | 1,07 |
| 64 s | 3,63·10⁻¹⁰ | 3,79·10⁻¹⁰ | 0,96 |

Rozsahy úseků se překrývají, rozdíl mezi úseky jednoho běhu je větší než mezi běhy. Dřívější dojem zhoršení vznikl srovnáním různé denní doby zdroje. Verdikt: `796c009` zůstává, omezení předděličky 300 MHz se nevrací; A-B-A-B není potřeba.
