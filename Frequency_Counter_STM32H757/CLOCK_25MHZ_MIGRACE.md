# Výměna oscilátoru 10 → 25 MHz — migrace hodin

> Vzniklo 2026-08-13 po uzavření etapy **ETH F0**: PHY LAN8742A vyžaduje 25 MHz na
> `XTAL1/CLKIN`, ale dostává 10 MHz — `X1` je sdílený s HSE procesoru (výstup jde
> přes **R5** do `RCC_OSC` a přes **R6** do `OSC_25M`). Řešením je výměna `X1`
> za **25 MHz TCXO**, jenže tím se změní i **HSE procesoru**, takže se musí
> přepočítat celý strom hodin.

## ⚠️ Neaplikovat dřív, než je TCXO na desce

Dokud je na desce 10 MHz a firmware by měl konfiguraci pro 25 MHz, PLL1 by dostala
2 MHz místo 5 (mimo deklarovaný `VCIRANGE`) a SYSCLK by vyšel **192 místo 480 MHz**.
Deska by naběhla, ale všechno by jelo na ~40 %: rozsypané časování displeje,
špatná baudrate UARTu, špatné SPI hodiny na FPGA. Proto je to zatím **jen recept**.

## Řídicí myšlenka: nechat všechny VCO beze změny

Všechny tři PLL umí při 25 MHz dosáhnout **přesně stejného VCO** jako dnes, když se
změní jen `M` a `N`. Důsledek: **žádný odvozený kmitočet se nezmění**, takže se
nemusí sáhnout na nic dalšího v projektu — ani na FMC/SDRAM, ani na SPI2 (FPGA),
ani na LTDC, ADC nebo SDMMC.

Klíč je `M = 5` u všech tří PLL: `25 / 5 = 5 MHz` na vstupu PLL, což padne do
**`VCIRANGE_2` (4–8 MHz)**. `M = 1` by dalo 25 MHz, což je mimo všechny rozsahy
(strop je 16 MHz).

| PLL | dnes (HSE 10) | po výměně (HSE 25) | VCO | co z toho jde |
|---|---|---|---|---|
| **PLL1** | M=1 N=96 `RGE_3` | **M=5 N=192 `RGE_2`** | 960 MHz *(beze změny)* | P=2 → **480 MHz SYSCLK**, Q=15 → 64 MHz (SDMMC), R=2 → 480 |
| **PLL2** | M=1 N=20 `RGE_3` | **M=5 N=40 `RGE_2`** | 200 MHz *(beze změny)* | Q=1 → 200 MHz (**SPI123 = FPGA**), R=2 → 100 MHz (**FMC/SDRAM**) |
| **PLL3** | M=1 N=17 **FRACN=4096** `RGE_3` | **M=5 N=35 FRACN=0** `RGE_2` | 175 MHz *(beze změny)* | R=7 → **25 MHz** (LTDC pixel + ADC) |
| **DSI PLL** | NDIV=70 IDF=1 ODF=1 | **NDIV=28** IDF=1 ODF=1 | 1400 MHz *(beze změny)* | **700 Mbps/lane** |

**Bonus u PLL3:** `175 / 5 = 35` vychází celé, takže **`FRACN` může na 0**. Dnes je
fractional mód (`FRACN=4096`) jen proto, že `175 / 10 = 17,5`. Míň jitteru na
pixel clocku zadarmo.

**Kontrola DSI:** dnes `(10/1) × 2 × 70 = 1400 MHz`; nově `(25/1) × 2 × 28 = 1400 MHz`.
`NDIV=28` je v povoleném rozsahu (10–125).

## Postup

1. **Vyměnit `X1` za 25 MHz TCXO.** HSE je v režimu **BYPASS**, takže výstup
   oscilátoru je přesně to, co se čeká. ⚠️ Pozor na `Tri-State` pin — ve schématu
   je u něj poznámka „Opravit enable TCXO".
2. **CubeMX:** *RCC → HSE → Input frequency* = **25 MHz**. Solver přepočítá strom
   sám, ale ⚠️ **zkontroluj, že zvolil hodnoty z tabulky výše** — může sáhnout po
   jiné kombinaci `M/N` a posunout některý odvozený kmitočet. Cíl je, aby
   `SYSCLK 480`, `SPI123 200`, `FMC 100`, `LTDC 25`, `SDMMC 64` a `DSI 700`
   zůstaly **beze změny**.
   - `.ioc` klíče, které se toho týkají: `RCC.HSE_VALUE`, `RCC.DIVM1/DIVN1`,
     `RCC.DIVM2/DIVN2`, `RCC.DIVM3/DIVN3`, `RCC.PLLFRACN`, `RCC.PLL3FRACN`,
     `RCC.PLLDSINDIV`. (Ostatní `*Freq_Value` jsou dopočítané — needituj ručně.)
3. **`HSE_VALUE` v obou jádrech:** `CM7/Core/Inc/stm32h7xx_hal_conf.h` **a**
   `CM4/Core/Inc/stm32h7xx_hal_conf.h`, řádek 109: `10000000UL` → `25000000UL`.
   ⚠️ Komentář u něj („FPGA case fixed to 60MHZ") je zavádějící zbytek — přepsat.
4. **Přeložit obě jádra, naflashovat obě banky.**
5. **Ověřit na HW v tomhle pořadí** (od nejrychlejšího důkazu):
   - displej naběhne a nebliká → LTDC + DSI mají správné hodiny,
   - UART konzole čitelná → PCLK sedí,
   - `status` → uptime roste rozumnou rychlostí (ne 2,5× pomaleji),
   - `fpgaraw` / `freq` → SPI2 link žije (PLL2Q beze změny),
   - `sdram write/read` → FMC (PLL2R),
   - `sd diag` → SDMMC (PLL1Q),
   - **`eth`** → tohle je ta odměna: PHY má konečně 25 MHz a musí odpovědět.

## Co se NEMĚNÍ

- **Přesnost měření.** Časovou základnu čítače nese OCXO → Si5356 → FPGA. `X1`
  taktuje jen procesor, takže na naměřený kmitočet nemá vliv.
- USB (běží z HSI48), RTC (LSE 32,768 kHz), IWDG (LSI) — všechno nezávislé na HSE.
- Všechny periferní kmitočty (viz tabulka) — proto se nemusí přepočítávat
  prescalery SPI, baudrate UARTu ani timing displeje.
