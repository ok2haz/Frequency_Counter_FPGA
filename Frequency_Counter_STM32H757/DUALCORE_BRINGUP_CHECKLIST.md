# Dvoujádro CM7 + CM4 — bring-up checklist (aktivace na HW)

> Co zkontrolovat a přenastavit, aby CM4 nabootoval a rozjel se IPC (snapshot CM7→CM4
> + heartbeat CM4→CM7). **Kód obou stran je hotový a zkompilovaný** (viz `STATUS.md`
> #19/#20/#22); tato aktivace je **HW-závislá** a nedá se ověřit bez flashe bank2.
>
> Podklady: audit 2026-08-09, `NAVRH_ARCHITEKTURA_CM7_CM4.md` §11, `CUBEMX_CHECKLIST.md`,
> `Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c`, oba linkery, `*.ioc`.
>
> ⚠️ **Než začneš:** zálohuj funkční `.ioc` a option bytes (přečti a ulož je z CubeProgrammeru).
> Špatný option byte (BCM4 + boot adresa) může vést k tomu, že se **nespustí ani jedno jádro**.

---

## Stav před aktivací (co dnes vidíš)

| Signál | Dnes (CM4 vypnutý) | Po správné aktivaci |
|---|---|---|
| UART boot log | `[BOOT] CM4 (D2) nenabehl -> degradovany rezim...` | (řádek se **neobjeví**) |
| Hlavní obrazovka, CPU blok (vpravo v headeru) | `4:off` (červeně) | `4:--` → pak `4:OK` (zeleně) |
| CM4 LED_2 | nesvítí | bliká ~1,25 Hz; **svítí trvale při GPS fixu** (čte snapshot) |
| `g_cm4_absent` (Health / SYS pill) | `1` → SYS amber | `0` |

Trojstav CPU bloku (`screen_main_redraw_cpu`): `4:off` = nenabootoval (`g_cm4_absent`),
`4:--` = D2 ready ale IPC ticho (`g_cm4_alive=0`), `4:OK` = heartbeat roste (CM4 běží + mluví).

---

## 1) 🔴 Option bytes (KRITICKÉ — přes STM32CubeProgrammer, NE v `.ioc`)

CM4 se spouští **výhradně přes option bytes**. `USER_VECT_TAB_ADDRESS` je v boot souboru
**zakomentovaný** (řádek 93 `system_stm32h7xx_dualcore_boot_cm4_cm7.c`) → VTOR se ručně
nenastavuje, oba cory jedou z **auto-remapu boot adresy** danou option byty.

Připoj desku, otevři **STM32CubeProgrammer → Option Bytes** a nastav:

- [ ] **`BCM4` = zaškrtnuto (1)** — povolí boot Cortex-M4. **Tohle je nejčastější příčina, že CM4 „off".**
- [ ] **`BCM7` = zaškrtnuto (1)** — boot Cortex-M7 (obvykle už 1; ověř).
- [ ] **`BOOT_CM7_ADD0` = `0x0800`** → CM7 boot z **bank1 `0x08000000`** (hodnota = adresa >> 16).
- [ ] **`BOOT_CM4_ADD0` = `0x0810`** → CM4 boot z **bank2 `0x08100000`**.
- [ ] Ověř, že FLASH je v **dual-bank** režimu a **není nastaven bank swap** (`SWAP_BANK`=0). H757 je dual-bank nativně.
- [ ] (volitelně) `nSWBOOT`/`BOOT` piny — necháváme boot z flash dle ADD0; hlídej, ať BOOT0 pin netáhne do system bootloaderu.

⚠️ Po zápisu option bytes proveď **power-cycle desky** (ne jen NRST) — některé změny se aplikují až studeným startem.

---

## 2) 🔴 Naflashovat OBĚ banky

CM7 běží z bank1, CM4 z bank2. **Když je naflashnutá jen bank1**, CM4 nemá co spustit → `4:off`.

- [ ] Build **CM7** projektu (`H757_LED_CM7`) → `.elf` do **bank1 `0x08000000`**.
- [ ] Build **CM4** projektu (`H757_LED_CM4`) → `.elf` do **bank2 `0x08100000`**.
- [ ] Flash obou:
  - **CubeIDE:** Run konfigurace zvlášť pro CM7 a pro CM4 (každá flashuje svou banku). Nebo
  - **CubeProgrammer:** načti oba `.elf` a naprogramuj (CM4 `.elf` má LMA v bank2 → sedne samo).
- [ ] ⚠️ **Neexistuje** `tools/make_release_image.ps1` (jen `gen_fonts.ps1`, `uart.ps1`) — slučování bank dělej ručně přes CubeProgrammer, nebo si ten skript dopiš (spoj bank1+bank2 do jednoho image).

> Pozn.: dokud není hotové 1), samotné naflashování bank2 nestačí — bez `BCM4=1` CM4 zůstane v resetu.

---

## 3) 🟡 SRAM4 / D3 clock (paměť IPC `0x38000000`) — ověřit na OBOU jádrech

IPC snapshot + ringy leží v **SRAM4 (D3, `0x38000000`, 64 KB)**. Ani CM7, ani CM4 dnes
**nepovolují SRAM4 clock explicitně** (spoléhají na default; CM7 má jen MPU region 2 non-cacheable).

- [ ] Pokud po aktivaci **IPC mlčí** (CM4 nikdy nevidí `magic`, nebo CM7 zápis faultne):
  povolit D3/SRAM4 clock na **obou** jádrech (per-core RCC — `RCC_C1_AHB4ENR` pro CM7,
  `RCC_C2_AHB4ENR` pro CM4). V HAL typicky `__HAL_RCC_D3SRAM1_CLK_ENABLE()` z kontextu daného jádra.
- [ ] Rychlý test: v CM4 po bootu přečti `g_ipc.snap.magic` — má být `0x31435049` („IPC1"). Pokud čteš `0` nebo garbage → clock/přístup.

⚠️ Je to **symetrický** problém: kdyby SRAM4 clock chyběl, selže i publikace z CM7 (taky netestováno na HW).

---

## 4) 🟡 D2 SRAM split (HOTOVO v linkerech) — jen ověřit, nerozbít

Rozdělení paměti je hotové (`STATUS.md` #22), **linkery NEjsou řízené `.ioc`** → regen je nepřepíše.

- [ ] CM7 `RAM_D2 : ORIGIN = 0x30000000, LENGTH = 128K` (SRAM1) — oba CM7 linkery.
- [ ] CM4 `RAM : ORIGIN = 0x10020000, LENGTH = 160K` (SRAM2+3) — `CM4/STM32H757BITX_FLASH.ld` i `_RAM.ld`.
- [ ] CM4 `_estack` vyjde `0x10048000` (vrchol D2). CM4 firmware zabírá ~2 KB → 160K je obří rezerva.
- [ ] ⚠️ **Per-core clock pro D2 SRAM2/3:** až CM4 dostane ETH/lwIP (heap v D2), musí mít
  SRAM2/3 clock povolený **z CM4** (`RCC_C2_AHB2ENR`). Dnes CM4 do D2 skoro nic nedává, ale hlídej u ETH.

---

## 5) 🟡 `.ioc` — pozor při regeneraci (CM4 má aspirační IP)

`.ioc` má pro CM4 zapnuto víc IP, než se reálně staví. **Skutečný CM4 build** (`hal_conf`)
má jen: `TIM, GPIO, DMA, MDMA, RCC, FLASH, EXTI, PWR, I2C, CORTEX, HSEM`; `main.c` volá jen
`MX_GPIO_Init` + `MX_TIM12_Init`.

`.ioc` ale u `CortexM4.IPs` uvádí navíc: **`FREERTOS_M4, OPENAMP_M4, USB_DEVICE_M4, USB_HOST_M4,
FATFS_M4, IWDG2, WWDG2, VREFBUF, PDM2PCM_M4, BDMA`** — tyhle **NEjsou** v hal_conf ani v main.c.

- [ ] ⚠️ **Při „Generate Code" z `.ioc` CubeMX může pro CM4 vygenerovat init těch IP** (FreeRTOS/OpenAMP/USB…)
  → nabobtná CM4 image + možné konflikty. Před regenerací buď:
  - v `.ioc` ty nepoužité CM4 IP **vypni** (Pinout&Config → přepni kontext na CortexM4 → odškrtni), **nebo**
  - regeneruj a hlídej, že se USER CODE (beep, LED, `ipc_cm4_init`/loop) zachoval a nové `MX_*_Init` nezalomí build.
- [ ] Sdílení `ipc_shared.h` je přes **relativní include** (`CM4/Core/Inc/ipc_cm4.h` →
  `"../../../CM7/Core/Inc/ipc_shared.h"`) — **regen-safe** (USER CODE + nové soubory). Až budeš chtít,
  přidej `CM7/Core/Inc` do include path CM4 projektu a přepni na `<ipc_shared.h>`.
- [ ] `CORTEX_M4.MPU_Control = __NULL` (MPU na CM4 vypnuté) — **správně**, CM4 nemá D-cache, SRAM4 nepotřebuje MPU.

---

## 6) 🟡 Boot handshake (HOTOVO v kódu) — jen pochopit, co se děje

Sekvence je odolná: když CM4 nenabootuje, CM7 **nespadne** do `Error_Handler` (černá obrazovka),
ale jede degradovaně a nastaví `g_cm4_absent`.

- CM7 `main.c` **Boot_Mode_Sequence_1**: čeká na `RCC_FLAG_D2CKRDY` (CM4 došel do STOP), timeout → `g_cm4_absent=1`.
- CM7 **Boot_Mode_Sequence_2**: `HSEM` take+release (probudí CM4), znovu čeká `D2CKRDY`, timeout → `g_cm4_absent=1`.
- CM4 `main.c` **Boot_Mode_Sequence_1**: `HSEM` clock + `HAL_HSEM_ActivateNotification` + `HAL_PWREx_EnterSTOPMode(...D2...)` → čeká, až ho CM7 pustí.
- [ ] Nic neměň, pokud handshake funguje. Kdyby CM7 tuhl na bootu (černá) → zkontroluj, že timeouty v Sequence_1/2 zůstaly (nesmí být `Error_Handler`).

---

## 7) ✅ Ověřovací postup (v tomto pořadí)

1. [ ] **Před option byty** (výchozí stav): CM7 jede, displej OK, UART hlásí „CM4 nenabehl", CPU blok `4:off`.
2. [ ] Nastav **option bytes** (sekce 1) + **power-cycle**.
3. [ ] Naflashuj **obě banky** (sekce 2).
4. [ ] Po resetu sleduj:
   - [ ] UART boot log **neobsahuje** „CM4 nenabehl".
   - [ ] CM4 **LED_2 bliká** (CM4 běží bare loop).
   - [ ] CPU blok přejde `4:--` → **`4:OK` (zeleně)** do ~3 s (CM7 vidí rostoucí heartbeat).
   - [ ] Při **GPS fixu** CM4 **LED_2 svítí trvale** (CM4 čte `snap.flags & IPC_F_GPS_VALID` = round-trip funguje).
5. [ ] Když `4:--` zůstává (CM4 běží, ale IPC mlčí) → sekce 3 (SRAM4 clock) + ověř `g_ipc.snap.magic` na CM4.

---

## 8) 🟡 Co ZŮSTÁVÁ jako TODO (mimo tento bring-up)

- [ ] **Producent cmd ringu na CM4** (příkazy CM4→CM7): dozraje až se SCPI/web na CM4 (`#25`/`#26`) — CM4 dnes nemá odkud brát příkazy.
- [ ] **IWDG2 na CM4** + `stall:CM4` do crash black-boxu (`NAVRH §11.4`) — hlídání zaseknutého CM4 za běhu.
- [ ] **Reálný CM4 CPU %** ve snapshotu (`cm4_cpu_pct`): dnes CM4 posílá `0` (bare loop, bez idle měření) → CPU blok ukazuje jen `4:OK`, ne procenta.
- [ ] **ETH/lwIP na CM4** (`#24`): až pak se D2 SRAM2/3 opravdu využije (deskriptory + heap) — hlídat per-core clock.

---

## Rychlá reference adres

| Co | Adresa / hodnota |
|---|---|
| CM7 flash (bank1) | `0x08000000` (BOOT_CM7_ADD0 = `0x0800`) |
| CM4 flash (bank2) | `0x08100000` (BOOT_CM4_ADD0 = `0x0810`) |
| IPC sdílená paměť (SRAM4/D3) | `0x38000000`, 64 KB, magic `0x31435049` |
| D2 SRAM — CM7 (SRAM1) | `0x30000000`, 128 KB |
| D2 SRAM — CM4 (SRAM2+3) | CM4 alias `0x10020000`, 160 KB (= CM7 `0x30020000`) |
| Boot enable CM4 | option byte `BCM4 = 1` |
