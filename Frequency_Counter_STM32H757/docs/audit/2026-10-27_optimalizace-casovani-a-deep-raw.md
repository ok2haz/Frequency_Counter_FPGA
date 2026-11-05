# Časování FPGA v3 po FW 0x0508, hluboký RAW a firmware v0.40.9 (2026-10-27)

Zdroj dat: `Frequency_Counter_FPGA_Module/abl/` (`tools/pnr_seeds.py`, 8 sestavení na variantu, BLD_TIME jako seed), čistá kopie z HEAD `048f4ab` pro simulace.

## 1. Výchozí stav (FW 0x0508, `base+rep`)

8 sestavení: CLS 3 395–3 443 (79 %), setup min −1,141 / **medián +0,034** / max +0,182 ns, **6 z 8 kladných**. Jediná opakovaná systematická cesta: čítač předděličky `u_pcb/c` (a `u_pca/c`), 4bitový binární čítač taktovaný přímo vstupním pinem (2 z 8, nejhůř −1,141 ns). Volby Gowin `rep`, `mf16`, `mf32` a jejich kombinace dávají **identické** výsledky jako základ (nemají v této sestavě účinek), proto se dál nezkoušejí.

## 2. Oprava: cíl předděličky 300 MHz místo 400 MHz (`516dcf7`)

`timing_v3.sdc` měl pro `pin_a`/`pin_b` periodu 2,5 ns (400 MHz) jako „strop předděličky"; strop vstupního bufferu LVCMOS33 je ale 300 MHz (komentář v SDC) a vstup čítače je DC–200 MHz. Cíl 3,33 ns (300 MHz):

| varianta | CLS | setup min / medián / max [ns] | kladných |
|---|---|---|---|
| základ (2,5 ns) | 3 395–3 443 | −1,141 / +0,034 / +0,182 | 6 z 8 |
| **pin33 (3,33 ns)** | 3 398–3 443 | **−0,010 / +0,225 / +0,769** | **7 z 8** |

Zůstala jediná cesta pod nulou (−0,010 ns, `tvr_a → u_raw/run`, 1 z 8), systematická cesta `u_pcb/c` zmizela. **Vydáno** přes bránu: `ab_test/FW_0x0508_v3_20261027-1514_796c009.fs` (md5 `5482e338768c5cce814ba5d77dcb772b`), CLS 79 %, registry 59 %, **setup +0,763 ns** (dříve +0,048), hold +0,418, Fmax 108,3 MHz. Funkce návrhu je stejná jako `bd20e77` (mění se jen omezení časování, ne RTL); FW číslo zůstává 0x0508, identita (git 796c009) se liší. ⬜ Neověřeno na desce: po flashi `status` → `FPGA BUILD … git 796c009`, `fpga v3 test` PASS, `spi err` bez chyb.

Důsledek omezení: P&R už nepožaduje, aby předdělička stíhala 400 MHz. Reálný požadavek (vstup DC–200 MHz, přes ÷10 do 1,1 GHz → ≤ 110 MHz na pinu) je s velkou rezervou splněn; kdyby se někdy měřilo nad 300 MHz přímo na pinu, je to mimo specifikaci vstupního bufferu.

## 3. Hluboký RAW (`-DDEEP_RAW`, PSRAM) nevychází

8 sestavení (varianta `deep`): CLS 3 731–3 796 (88–89 %), **setup −8,1 až −11,5 ns, 0 z 8 kladných**, 209–350 porušených koncových bodů. Selhávají všechny cesty uvnitř `u_raw` (`fcnt → dwa/p_addr/p_wait/p_req`, `dn → dfin`, `nw_v → dwa`) v doméně 100 MHz. Hluboký záznam v současné podobě **nelze vydat**; vyžaduje přepracování `raw_cap3` (registrovat/rozdělit aritmetiku `fcnt`, `nw_v`, `sbi` a dekódování adres), nejde o volbu P&R. Výchozí `raw_cap3l` (512 značek) beze změny.

## 4. Ověření simulacemi (čistá kopie z HEAD)

Viz `2026-10-25_optimalizace-fpga-v3-kolo2.md` kap. 8: regresní sada v3 PASS, mutace 1PPS 23/23, mutace celého návrhu 59 zachyceno + 2 ekvivalentní, nezachycená 0. Poznámka: simulace běžely nad zdrojem před změnou SDC; změna SDC RTL nemění.

## 5. Firmware v0.40.9 (`924006e`, tag `v0.40.9`, ⬜ neověřeno na HW)

- **Režim počtu platných číslic „podle rozlišení čítače":** UART `digits [sig|counter]` (výchozí `sig` = dosavadní chování), persist v bitu 1 bajtu `gen_valid` v syscfg (velikost blobu 208 B a magic beze změny, starý blob = výchozí). Hlavní číslo pak bere jen podlahu čítače (`mp_res_floor_rel`) bez σy zdroje a nejistoty reference; mění jen zobrazení, ne statistiku. Řádek `DIGITS:` ve `status`. Ovládání je zatím jen přes UART (tlačítko v okně DISPLEJ nepřidáno, protože tam nelze ověřit geometrii bez displeje).
- **`status` → `DMA2D:`** rozlišuje maximum čekání nad mezí 500 ms bez timeoutu (zahrnuje preempci UiTasku, ne dobu přenosu), L-0243.
- **Nástroje:** `tools/spi_night.py` při každém cyklu kontroluje web (`/api/state`) a při výpadku zapíše `WEB_DOWN` s řádky `CM4:`/`NET:`/`TX(CM4)` ze `status` (příčina minulých výpadků webu zatím neznámá); `abl_cls.py` varianty `deep`, `pin33`.
- Build Release CM7: 0 varování, `.text` 748 296 → 748 888 B, `audit.py` 97 OK / 0 selhání / 2 s varováním, host test `fpga_v3` PASS.

## 6. Nálezy bez změny kódu

- Zásobníky s malou rezervou (`stats`): IDLE 428 B, DacTask 800 B, I2C4 924 B, FpgaTask 1 416 B; stabilní, ale tenké.
- V `errlog` jeden `HardFault 240000` těsně před záznamem power-on (nejspíš při vypnutí napájení), neopakuje se.
