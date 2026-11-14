# Audit: přezkum změn v0.45.18 (F-0294) a v0.45.19 (menu UI)  (2026-11-14)

- **Commit:** 07a2f6c (zdroje beze změny od da63ff8 / d97c488)
- **Jádro / doména:** CM7 (`freertos_task_fpga.c`, `meas_present.h/.c`, `app_gpsdo.c/.h`)
- **Projité sekce checklistu:** relevantní jen obecné (pořadí operací, velikosti bufferů, sdílený stav); periferie (DMA/MPU/NVIC) se změny netýkají.
- **Neprojité (a proč):** HW sekce checklistu (hodiny, DMA, cache) – změny jsou čistá logika a vykreslování; `.map` – žádný nový buffer.
- **Precedent:** `/audit-modul` bez jména, žádný `nezačato` → přezkum posledních kódových změn (jako F-0155/F-0199/F-0225).

## Souhrn
Dvě změny od posledního přezkumu: (1) F-0294 – dolní mez nejistoty reference v prvních 5 min LOCK (sklon fáze od 12 bodů),
(2) menu UI – zrušeno okno SENZORY (s_view=4), tlačítko RESET MIN/MAX přesunuto do Diagnostiky, přejmenování NUL/RESET,
rozšířené buffery v Holdoveru. Žádná chyba chování nenalezena. F-0294 má potvrzení na desce po power-cyklu (ref 0,119 ppb
≥ max |y| 0,100 ppb). Dva nálezy S4 (zastaralá dokumentace/komentáře, mezera v mutačním testu). **Verdikt: funkční.**

---

### F-0295 [S4] Zastaralé komentáře a dokumentace po v0.45.18 / v0.45.19

- **Místo:** `CM7/Core/Src/freertos_task_fpga.c:631` („nejmin 300 s“ – sklon se od v0.45.18 měří od 12 bodů = 110 s);
  `CLAUDE.md` ř. 2171, 2325, 2391 (okno SENZORY, s_view=4, footer „SENZORY/…/GRAFY“); `UI_SIZES.md:125` (Health footer
  `SENZORY {18,..} GRAFY {236,..}`, dnes jen `GRAFY {18,..}`).
- **Popis:** komentář/dokument tvrdí chování, které už neplatí; nový čtenář hledá okno, které neexistuje.
- **Důkaz:** `git show da63ff8` (zrušeno `app_gpsdo_render_sensors`, `nm` nad `H757_LED_CM7.elf` → 0 symbolů),
  `git show 016dfc8` (`GP_REF_YOFF_SHORT 12`).
- **Dopad:** žádný na běh; riziko chybného rozhodování při příští úpravě (L-0018, L-0281).
- **Reprodukce:** `grep -n "SENZORY" CLAUDE.md UI_SIZES.md`.
- **Návrh opravy:** `docs:` commit, žádná logika.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** L-0018 (duplicitní fakta se rozjedou).
- **Stav:** otevřeno

### F-0296 [S4] Mutační test `mut_ref.py` nekryje větev F-0294 a jedna mutace je zastaralá

- **Místo:** `scratchpad/mut_ref.py` (seznam `MUT`), `CM7/Core/Inc/meas_present.h:238`, `freertos_task_fpga.c:659`.
- **Popis:** mutace `'if (n >= GP_REF_YOFF_MIN) {'` už v kódu nenajde kotvu (kód má `GP_REF_YOFF_SHORT`). Pro řádek
  `if (r->lock_yoff_ok && r->lock_yoff_ppb > v) v = r->lock_yoff_ppb;` a konstantu `GP_REF_YOFF_SHORT` mutace neexistují,
  takže není doloženo, že host test změnu v nich zachytí.
- **Důkaz:** diff `016dfc8`; seznam `MUT` neobsahuje `lock_yoff_ppb > v` ani `GP_REF_YOFF_SHORT`.
- **Dopad:** nelze tvrdit, že selhání nové větve by test odhalil (host test i `mp_selftest` ji ale pokrývají přímo: n=11 / n=15,
  případ „bez ADEV vyhrává větší“ v `meas_present.c:564`).
- **Reprodukce:** HYPOTÉZA – ověřit: doplnit mutace `lock_yoff_ppb > v` → `>= 1e9`, `GP_REF_YOFF_SHORT` → `GP_REF_YOFF_MIN`,
  opravit zastaralou kotvu a pustit `mut_ref.py` (host, zig cc); každá musí test shodit.
- **Návrh opravy:** doplnit 2 mutace, opravit kotvu. Mutace upravují zdroj dočasně, proto až ve fázi F5.
- **Riziko opravy:** nízké (host nástroj).
- **Vztah k lekcím:** L-0281 (host test před nahráním), L-0282.
- **Stav:** otevřeno

---

## Co bylo zkontrolováno a je v pořádku
- **F-0294 logika:** `gp_ref_core` pro 12 ≤ n < 30 dává `wn = n` (clamp `wn > n`), index `xs = x_ps + (n - wn)` v mezích; jednotky ps → ppb
  stejné jako dřív; mez se v `mp_ref_ppb` jen zvyšuje (`max`), výsledek zůstává „odhad“ (`*meas` se nemění) – nikdy nesnižuje nejistotu.
- **HOLD:** `s_ref_yoff` se čistí při odchodu z HOLD (ř. 766); vstup do HOLD z krátkého LOCK dědí už konzervativnější hodnotu.
- **Selftest/hostitelský test:** n=11 nic, n=15 sklon změřen, u/d ne; PASS (`t_gpsdo_ref.py`, `mp_selftest`); deska: `selftest` 16/16 po power-cyklu.
- **Menu:** všechna místa s `case 4`/`s_view == 4` odstraněna (render_view, screensaver-switch, živý refresh, touch); `grep` bez zbytků;
  `app_gpsdo.h` deklarace odstraněna; `SENS_R0/SENS_DY` ponechány (používá PAMĚŤ); `fmt_temp_d`/`temp_deci` dál používá Diagnostika.
- **RESET MIN/MAX v Diagnostice:** rect {240,417,210,61} neleží v BACK_RECT (650) ani v NASTROJE (18..228); tlačítko jde přes
  `ui_button_render` → registr encoderu, takže zůstanou dvě úplné ovládací cesty. Handler volá `render_diag` (cache `dchg` překreslí min/max).
- **Buffery:** `t[20]`, `b[48]` jsou větší než zdroj `dchg` (invariant `sizeof(src) >= sizeof(cache)` neporušen); build 0 varování, audit 97/0/2,
  symbol `app_gpsdo_render_sensors` v `.elf` není.
- **Šířky popisků (mono_22, 13 px/znak):** `RST STAT.` 117 px ve 130, `NUL Y ZAP` 117 v 204, `RESET ALARMU` 156 v 200, `RESET MIN/MAX` 169 v 210.

## Nezkontrolováno / omezení tohoto běhu
- Vzhled menu na displeji (nové tlačítko v Diagnostice, footer Health, nové popisky) – ⬜ neověřeno na HW; v0.45.19 nenahrána.
- Skutečné chování F-0294 po rozladění kódu (jen přirozené dosedání po power-cyklu).
- Mutační sada nebyla pouštěna (měnila by zdroj; zákaz fáze auditu).
