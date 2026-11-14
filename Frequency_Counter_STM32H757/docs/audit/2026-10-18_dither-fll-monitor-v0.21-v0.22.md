# Audit: DacTask (dither), oprava FLL v `core_step`, pasivní fázový monitor v OFF  (2026-10-18)

- **Commity:** `a3aa147` (v0.21.0 DacTask), `90f5388` (výchozí 100 Hz), `19afcb9` (v0.21.1, #318), `469a9d0` (v0.22.0, #319)
- **Jádro / doména:** CM7; `DacTask` (priorita AboveNormal) + SensorsTask (vlastník I2C1) + defaultTask (`gpsdo_tick`)
- **Soubory:** `CM7/Core/Src/freertos_task_sensors.c` (`ad5693_servo`, `DacTask_run`, `dither_next`), `freertos.c` (vytvoření tasku), `freertos_task_fpga.c` (`core_step`, `gpsdo_tick`, `st_run`), `freertos_task_uart.c` (`dac dither`), `ad5693.h`
- **Projité sekce checklistu:** vlákna a priority, sdílená sběrnice I2C1 (mutex, timeouty), zápis do HW, numerika regulátoru, stavový automat smyčky, stack/halda
- **Neprojité:** chování na desce po delším běhu (ověřeno krátce 2026-10-17: `dac dither test` PASS, 0 chyb I2C, `gpsdo test` PASS, LOCK po 288 s)

## Souhrn

Dither v samostatném tasku je navržen opatrně (znovu kontrolované podmínky uvnitř mutexu, krátké timeouty, back‑off, záloha 2 Hz v SensorsTasku, kanárek v žádném zápisu mimo mutex) a na desce nevykazuje chyby (808 B volného stacku z 1 024, 0 vynechaných kroků, 0 chyb I2C). Oprava FLL (#318) a fázový monitor (#319) fungují v rámci modelu a krátkého testu. Hlavní zbývající slabina je **závislost prahů FLL na pevné hodnotě šumu 1PPS** (F-0258): pokud je skutečný šum 1PPS vyšší než předpoklad, FLL může falešně vyhlásit divergenci a smyčka skončí ve stavu OFF.

**Verdikt:** funkční; jedna S3 slabina (F-0258), ostatní S4.

---

### F-0258 [S3] Falešná divergence FLL při šumu 1PPS vyšším než předpokládaných 30 ns

- **Místo:** `freertos_task_fpga.c` `core_step`, druhá kontrola divergence (`!kupd && fabs(ya) > 1e-8 && fabs(ya) > 1.5 * fabs(prev)`); konstanta `GPSDO_PM_NS` (30 ns) je jen **HYPOTÉZA** (datasheet NEO‑7M v repu není).
- **Popis:** Práh 1·10⁻⁸ odpovídá 3,7 σ okna 16 s jen při šumu 30 ns. Po opravě #318 se K z malých kroků neměří, takže při větším šumu rozhoduje tato druhá kontrola a ta není vůči šumu normovaná.
- **Důkaz (host model `st_fix_sim.py`, 150–300 semen, model skutečné smyčky):** šum 1PPS 30 ns: **0/900** falešných divergencí; **60 ns: 5/200 (2,5 %)**; **100 ns: 17/150 (11 %)**; 150 ns: 44/150 (29 %). Se šumovou rezervou v prahu (`1e-8 + 8·σ_okna` a `1,5·|prev| + 8·σ_okna`, σ z `GPSDO_PM_NS` = 60) vychází 0/150 do 100 ns a 9/150 při 150 ns; skutečné poruchy (špatné znaménko K, nedostatečný rozsah EFC) se zastaví **40/40** jak dnes, tak s rezervou.
- **Naměřený šum na desce:** rozptyl druhých diferencí fáze `bez opravy 463 ns²` → 8,8 ns RMS na hranu, s opravou `−q` 17 ns² → ~1,7 ns (`status` → `GPSDO: ... sawtooth ... rozptyl ns2`). Pro dnešní anténu a NEO‑7M je tedy rezerva 3–15×; slabina se projeví jen při horších podmínkách (špatná anténa, chybějící TIM‑TP, rušení).
- **Dopad:** smyčka skončí v OFF „FLL divergovala" a nezamkne; pomůže `gpsdo on`, ale po restartu to může nastat znovu.
- **Návrh opravy:** do druhé kontroly přidat šumovou rezervu (σ okna z `GPSDO_PM_NS`, který zvednout na 60 ns) – malý diff, mění jen mezní případy. Lepší varianta: odhadovat šum za běhu z `s_saw_var[]` (už se počítá) místo konstanty.
- **Riziko opravy:** nízké; selftest `gpsdo test` (3 strmosti + 2 zastavení) po opravě ověřit i na hostu s vyšším šumem.
- **Vztah k lekcím:** L‑0169 (rozhodování o divergenci z veličiny, jejíž šum se nezohlednil).
- **Stav:** otevřeno (skupina B: rozhodnout, jestli konstantu 60 ns a rezervu 8 σ, nebo odhad za běhu).

### F-0259 [S4] Dither: akumulátor sigma‑delta se posune i při neúspěšném zápisu

- **Místo:** `freertos_task_sensors.c` `ad5693_servo` (`dither_next(&s_dith_acc, q)` se volá před `ad5693_write3t`).
- **Popis:** Selže‑li zápis, `s_dith_acc` už je posunutý, ale kód v DAC zůstane starý; střední hodnota se o ten krok odchýlí (max. 1 LSB v jednom kroku). Při back‑offu (500 ms) se to akumuluje jen do `fail` kroků, ne do trvalé chyby.
- **Dopad:** zanedbatelný (1 LSB = 3,4·10⁻¹¹ na dobu poruchy sběrnice).
- **Návrh:** posunout akumulátor až po úspěšném zápisu, nebo ho při selhání vrátit.
- **Stav:** ✅ opraveno ve v0.33.1 (2026-10-22, L-0190): akumulátor se potvrdí až po úspěšném zápisu. ⬜ Nepřeloženo, neověřeno na desce.

### F-0260 [S4] Volná halda FreeRTOS jen 5 744 B (min‑ever totéž)

- **Místo:** `status` → `Heap: free 5744 B`; `configTOTAL_HEAP_SIZE` 32 768 B.
- **Popis:** Po `DacTasku` (1 kB stack + TCB) zbývá ~5,7 kB. Další task nebo fronta musí počítat s tím, že `osThreadNew` může selhat (`DacTask` má zálohu, ostatní ne).
- **Návrh:** buď zvětšit haldu (nutné `.ioc`/`FreeRTOSConfig.h` + ověření RAM_D1, volných ≈ 184 kB), nebo přesunout haldu do DTCM (STATUS #320). Nic neměnit bez potřeby.
- **Stav:** ✅ vyřešeno ve v0.30.3 (`4ef92fb`, 2026-10-18): `configTOTAL_HEAP_SIZE` 32 → 48 kB, `.ioc` srovnáno, `.bss` +16 384 B ověřeno (zápis 2026-10-22).

### F-0261 [S4] Zastaralé popisky „/4", „/16" na displeji

- **Místo:** `app_gpsdo.c:6560,6566-6569,3079,3832` (okno Čítač „f (/16)", „pin28 = /4 … pin27 = /16", Dvojkanál „CH A = odbočka /4, CH B = /16" v komentářích a texty).
- **Popis:** Od FW 0x0400 (carry‑chain TDC) deska nemá děliče /4 a /16; `freq16_x100000` je kanál B (`fpga_freq.c:734`). Web byl opraven ve v0.27.2, CM7 popisky zůstaly.
- **Návrh:** přejmenovat popisky v oknech Čítač a Dvojkanál (kanál A / kanál B) a odstranit „rozsah /16".
- **Stav:** ✅ opraveno ve v0.33.1 (2026-10-22, L-0191): okno Čítač `E-A`/`E-B`, selftest #1 „FPGA ramec + regrese“; popisky oken Čítač/Dvojkanál byly opravené už dřív. ⬜ Nepřeloženo.

---

## Co bylo zkontrolováno a je v pořádku

- **Mutex a časování `DacTask`:** zápis jen pod `i2c1MutexHandle` s timeoutem 2 ms, podmínky `ad5693_servo_ready()` se po získání mutexu kontrolují znovu (ruční `dac <kód>` a obnova po startu mají přednost), po 3 selháních zkouška jen 1× za 500 ms; obnovu sběrnice dál dělá SensorsTask. Dědění priority mutexu zvedne SensorsTask nad ostatní, takže DacTask nečeká na task s nízkou prioritou.
- **Stack a CPU na desce:** `DacTask` stackfree 808 B z 1 024 B, CPU 0 % (zaokrouhleno), `kroky bez mutexu 0`, `vynechano po chybe 0`, `chyb I2C 0`, zápisů ≈ 50/s (zápis jen při změně kódu).
- **`dither_next`:** na 65 536 krocích součet přesně `q`, kód kolísá nejvýš o 1 LSB, ochrana přetečení kódu (`ci < 0xFFFF`); `dac dither test` PASS i na desce.
- **Persistence kódu:** `g_dac_persist` jen při `g_gpsdo_lockflag` a nejvýš 1× / 10 min (wear W25Q).
- **#318:** `fll_prev_n` se nastavuje před nulováním `fll_n`; `s_dif` nikdy nedělí nulou (`fll_n ≥ fll_win ≥ 16`, `fll_prev_n ? : 1`); gate `0,25·K0·|Δkód| > 5 σ_rozdílu` nemění zastavení při špatném znaménku (rail) ani nedostatečný rozsah; `gpsdo test` PASS na desce po power‑cyklu.
- **#319:** `s_mon_x` se nuluje při vstupu do OFF a při každém přerušení (bez fixu, externí reference, odmítnutý interval), `gpsdo on` z OFF startuje FLL prvním intervalem, `pps_rej`/`pps_ok` jen počítadla; `x_raw_ns` v OFF = monitor, jinak 0; krátce ověřeno na desce (`xr_ns` 4 / −2 / 0 ns, `gpsdo on` → FLL).
- **Build:** 0 varování, `audit.py` 92 OK / 0 / 2.

## Nezkontrolováno / omezení

- Dlouhodobý běh (hodiny až dny) s ditherem 100 Hz a opakované přepínání OFF/ON; kolize `dac dither N` s ručním `dac` při zátěži sběrnice.
- Skutečný šum 1PPS při jiné anténě než dnešní (F-0258 je spočítaný modelem, ne změřený).
