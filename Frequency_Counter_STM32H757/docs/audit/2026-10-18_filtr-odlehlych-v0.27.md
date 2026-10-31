# Audit: filtr izolovaných odlehlých vzorků + surová pyramida + IPC v28  (2026-10-18)

- **Commit:** `49b66fe` (v0.27.0), dokumentace `eba8a5f`
- **Jádro / doména:** CM7 (UiTask: statistika; UartTask: `meas outlier`, `status full`), IPC v28, CM4 (`/api/stab`, SPA text)
- **Soubory:** `CM7/app/screens/screen_main.c` (blok HAMP, `adev_feed_filtered`, `s_adev_raw`, `adev_view`, tabulka, export, selftest), `screen_main.h`, `CM7/app/app_gpsdo.c` (tlačítko VSE/OKNO/RAW, `ipc_stab_publish`), `CM7/Core/Inc/ipc_shared.h`, `CM7/Core/Src/ipc.c`, `CM7/Core/Src/freertos_task_uart.c`, `CM4/LWIP/App/httpd_min.c`
- **Projité sekce checklistu:** paměť a linker (DTCM), vlákna a sdílený stav, IPC layout, numerika, hranice polí, nulování stavu, selftest, UI
- **Neprojité:** chování na desce (nic neběželo), skutečná doba selftestu

## Souhrn

Filtr je navržený metrologicky správně (izolovaný vzorek, díra místo náhrady, počty viditelné, surová pyramida). Jeden prokázaný defekt **v samotném selftestu** (F-0254, S1): test volá `adev_feed_into` s `ADEV_STAGES` nad jedním objektem, takže zapisuje za jeho konec. Hostový model v Pythonu to nechytil (seznamy tam byly delší). Do opravy doporučuji **v0.27.0 neflashovat**. Na skutečných datech filtr falešně označuje 0,04–0,17 % vzorků (F-0255), vliv na ADEV je ≤ 0,7 %.

**Verdikt:** podmíněně funkční – produkční cesta (`s_adev`, `s_adev_raw`, 7 stage) je v pořádku, selftest nikoli.

---

### F-0254 [S1] Selftest filtru zapisuje mimo `adev_stage_t` (cascade přes `ADEV_STAGES` nad jedním objektem)

- **Místo:** `screen_main.c:2256` (`static adev_stage_t tf, tw2;`), `:2264` (`adev_feed_into(&tw2, ADEV_STAGES, 0, ...)`), `:2263`/`:2274` (`hamp_push(&th, &tf, y)` → `:1599` `adev_feed_into(pyr, ADEV_STAGES, 0, P, ...)`).
- **Popis:** `adev_feed_into(pyr, nst, ...)` po každých 10 vložených položkách pokračuje do `pyr[1]`, `pyr[2]` … až `pyr[nst-1]`. Test předává ukazatel na JEDEN objekt (`&tw2`, `&tf`) a `nst = 7`, takže od 10. vzorku zapisuje do paměti za objektem.
- **Důkaz:** `nm` (v0.27.0): `tw2` `0x2404e858` (0x6e0 B), `tf` `0x2404ef38` (přesně za ním), `th` `0x2404f618` (za `tf`), pak `tw`, `ta.32`, `tp.35`, `tst.38`. Tedy `tf[1]` leží přímo na `th` (stav filtru: `w[]`, `n`, `h`, `out`) a `tw2[1]` na `tf`; další stage zasahují do `tw`, `ta`, `tp`, `tst`. `sizeof(adev_stage_t)` = 1 760 B (0x3020 / 7). Ostatní volání `adev_feed_into` mají `nst` shodné s velikostí pole (`:2189,2203,2213,2222` pole `[2]` s `nst = 2`; `:2236,2238` `nst = 1`; `:2248` `&tw`, `nst = 1`; produkční `s_adev`/`s_adev_raw` pole `[ADEV_STAGES]`).
- **Dopad:** `th` (počítadla `seen`, `out` a okno) se za běhu testu přepisuje stavem „stage 1" jiného objektu → test s vysokou pravděpodobností selže (`SELFTEST x/16`, SYS pilulka červená „selftest FAIL") nebo dá nahodilý výsledek; ostatní zasažené statiky jsou jen proměnné selftestu (každý blok je před použitím nuluje), takže mimo test pravděpodobně nic jiného nepoškodí, ale závisí to na pořadí linkování. Deterministické při každém běhu `selftest`, tedy i při bootu.
- **Reprodukce:** UART `selftest` na cíli po flashi v0.27.0 (očekávám FAIL v `ui: ... selftest`). Hostový Python model chybu neodhalí (seznamy `[Stage() for _ in range(2)]`).
- **Návrh opravy:** `hamp_push(h, pyr, nst, y)` s `nst` jako parametrem (produkce předá `ADEV_STAGES`, test `1`) a v testu `adev_feed_into(&tw2, 1, ...)`. Nebo pole `[ADEV_STAGES]` (+24 kB .bss). První varianta.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** nová lekce po opravě (parametr délky pole se předává spolu s polem; testovací kopie struktur musí mít stejnou délku jako produkční).
- **Stav:** opraveno 2026-10-18 (v0.27.1). Opraveno podle návrhu (`hamp_push(h, pyr, nst, y)`), navíc **kanárek 0xA5 za testovacím polem** (zapis za konec objektu se příště projeví jako FAIL testu). ⬜ neověřeno na HW. **v0.27.0 neflashovat.**

### F-0255 [S3] Filtr na skutečných datech označuje 0,04–0,17 % vzorků (ne 10⁻⁶ jako u Gaussova šumu)

- **Místo:** `screen_main.c` `hamp_is_outlier` (5 MAD, okno 31); offline port `docs/audit/sim/2026-10-18_hampel_realna_data.py`.
- **Důkaz:** běh `120min` (1,14 h, 4 102 posouzených vzorků): vyřazeno 7 (0,17 %), běh `gate025b` (2,64 h, 9 488): vyřazeno 4 (0,04 %). Vyřazené vzorky se od lokálního mediánu liší jen o 0,2–0,5 ppb. ADEV po filtraci se mění o −0,7 % @ 1 s a ≤ ±0,2 % na delších τ (120min: 7,82 → 7,77·10⁻¹¹ @ 1 s; gate025b: 1,254 → 1,251·10⁻¹⁰), tedy filtr hrbol ADEV na 20–50 s nijak neovlivňuje.
- **Popis:** Šum externího GPSDO (na kterém se měří) má těžší chvosty než normální rozdělení a lokální MAD z 31 vzorků kolísá, takže 5 MAD je 100–1000× častější než pro Gaussův šum. Označení „odlehlé" je tedy příliš silné.
- **Dopad:** malý (ADEV −0,7 % @ 1 s), ale statistika se tiše „vylepšuje" proti skutečnému šumu zdroje; počet je viditelný, takže to není skryté.
- **Návrh:** buď zvýšit práh (HAMP_K 7–8 → zhruba desetkrát méně označených), nebo přejmenovat výstup na „kandidáti na odlehlé" a ponechat. Rozhodnutí o politice (jak agresivně) patří uživateli.
- **Riziko opravy:** nízké; mění jen konstantu.
- **Stav:** opraveno 2026-10-18 (v0.27.1), rozhodnutí: **HAMP_K = 7**. Podklad (`docs/audit/sim/`, offline na 120min a gate025b): označených vzorků 5 MAD 0,171/0,042 %, 6 MAD 0,073/0,032 %, **7 MAD 0,049/0,021 %**, 8 MAD stejně jako 7, 10 MAD 0,024/0,011 %; citlivost na vložené izolované špičky 12σ se u 7 MAD nezhoršila (21 vs 19 z 28). 7 je nejnižší hodnota, od které označení dál neklesá výrazně.

### F-0256 [S4] Čtení pyramid z UartTasku bez synchronizace (`screen_main_adev_compare`, `screen_main_outlier_info`)

- **Místo:** `screen_main.c` (funkce volané z `status full`).
- **Popis:** UartTask čte `double` součty a počítadla, která UiTask aktualizuje; 64bitové čtení není atomické. Může se vypsat jedno roztržené číslo. Stejná tolerance jako u dosavadních výpisů `STATISTIKA:` (L-0023).
- **Stav:** opraveno 2026-10-18 (v0.27.1): `screen_main_outlier_info` a `screen_main_adev_compare` čtou pod PRIMASK (UiTask se mezitím nepřepne, čtení je konzistentní). Opraveno jen u těchto dvou nových funkcí; dosavadní `STATISTIKA:` výpisy zůstávají s tolerancí roztrženého čtení.

### F-0257 [S4] Netestovaná větev `meas outlier off`

- **Místo:** `adev_feed_filtered` (uvolnění cekajícího vzorku při vypnutí filtru).
- **Popis:** Selftest pokrývá `hamp_push` a rozhodnutí, ne přepnutí za běhu (uvolnění `s_hamp.have`, pořadí vzorků v pyramidě).
- **Návrh:** přidat do selftestu kontrolu pořadí (předat dva vzorky, přepnout, předat třetí; `s_adev.tot` o 3). Po opravě F-0254.
- **Stav:** opraveno 2026-10-18 (v0.27.1): `hamp_flush` + test (`tot` 2 → flush → 3, další vzorek zase čeká).

---

## Co bylo zkontrolováno a je v pořádku

- **Produkční cesta:** `hamp_push(&s_hamp, s_adev, y)` předává pole `[ADEV_STAGES]`; `adev_feed_into(s_adev_raw, ADEV_STAGES, ...)` taktéž; zlom (`adev_break_all`) nuluje obě pyramidy i `hamp_reset`; `screen_main_stats_reset` nuluje obě pyramidy a `s_hamp`.
- **Rozhodovací logika:** `hamp_is_outlier` vrací 0 pro prázdné okno (`n < HAMP_MIN`, bez dereference `NULL`), pro `MAD = 0` a pro shluk dvou vzorků (oba sousedé nejsou v normě → obě se ponechají); `prev` se bere z posledního zapsaného prvku kruhového okna správně i po zacyklení. Hostový port se stejným PRNG: 3 izolované outliery z 600 vyřazeny (`seen` = 599), shluk 2 ponechán.
- **IPC v28:** `mode` nahradilo `_pad` (offset 11), `outl`/`outn` na 32/36, `pt[]` od 40; `ipc_stab_t` ≈ 1 552 B; zapisuje se uvnitř seqlocku (`ipc_stab_publish`); CM4 kopíruje celou strukturu (`ipc_cm4_read_stab`) bez ručních offsetů; JSON přibyl o ~40 B (`bodybuf` 6 144 B, nejhůř ~4,2 kB).
- **DTCM:** `.dtcm_bss` 35 032 B (0x88D8) z 64 kB (`_edtcm_bss` 0x200188d8), `membench` testuje 0x20000000–0x2000FFFF; do DTCM jdou jen CPU data (L-0001 splněno).
- **Souběh vláken:** `g_adev_hampel_en` je `volatile uint8_t` zapisovaný z UartTasku; zápisy do pyramid a do `s_hamp` jen UiTask (`stats_push`, `stats_reset`, `adev_break_all`).
- **Počitadla (L-0017):** vyřazení je viditelné v tabulce ALLAN, `status full` a `/api/stab`.
- **Build:** Release CM7 + CM4 bez varování, `audit.py` 92 OK / 0 / 2, kontrola SPA včetně `SPA_HTML` v ELF.

## Nezkontrolováno / omezení

- Nic neběželo na desce; F-0254 je prokázaný čtením kódu a `nm`, projev (FAIL/neFAIL selftestu) se potvrdí `selftest` na cíli.
- Offline test filtru používá 1 s průměry ze 4 měření 0,25 s z CSV loggeru (ne přímo vzorky FpgaTasku); rozdíly proti firmwaru jsou v zaokrouhlení.
