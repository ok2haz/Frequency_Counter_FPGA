# STYLE_CZ.md — styl kódu a českých komentářů

Platí pro každou úpravu kódu i komentářů. Cíl: **čitelnost a reprodukovatelnost** —
po roce musí být z komentáře jasné, *proč* je kód takový, ne *co* dělá.

---

## 1. Zlaté pravidlo

> Kód říká **co**. Komentář říká **proč**, **za jakých předpokladů** a **jaké má jednotky**.

Komentář, který jen převypráví řádek pod sebou, se **maže**.

```c
/* ŠPATNĚ — duplikuje kód */
// nastavíme prescaler na 8
htim2.Init.Prescaler = 7;

/* SPRÁVNĚ — doplňuje to, co v kódu není */
// 240 MHz APB1 timer clock / (7+1) = 30 MHz -> krok 33,3 ns, přetečení za 2,18 ms
htim2.Init.Prescaler = 7;
```

## 2. Jazyk a kódování

- Komentáře **česky**, diakritika ano, soubory v **UTF-8 bez BOM**.
  Do překladu doplň `-finput-charset=UTF-8 -fexec-charset=UTF-8`.
  *Výjimka:* pokud nějaký nástroj v řetězci (starý IDE, generátor) UTF-8 nezvládá,
  přejde se na češtinu bez diakritiky — rozhodnutí se zapíše do `LESSONS.md`.
- Identifikátory, názvy funkcí a typů **anglicky** (kvůli HAL/CMSIS konzistenci).
  Nemíchat `nastav_teplotu()` a `HAL_ADC_Start()` v jednom modulu.
- Doxygen značky anglicky (`@brief`, `@param`), obsah česky.
- Bez emoji, bez ASCII artu, bez podpisů typu „upravil Claude“.

## 3. Hlavička souboru (povinná)

```c
/**
 * @file    uart_rx.c
 * @brief   Příjem rámců z UART3 přes DMA s detekcí IDLE linky.
 *
 * @details Buffer leží v AXI SRAM (D1), protože DMA1 nemá přístup do DTCM.
 *          Délka rámce se počítá z DMA NDTR v IDLE callbacku.
 *          Cache se invaliduje před předáním dat aplikaci (32B zarovnání!).
 *
 * @note    Jádro: CM7. Periferie: USART3 (D2), DMA1_Stream1.
 * @note    Vyžaduje hodiny D2 SRAM1 (RCC AHB2ENR).
 */
```

Pravidla: `@brief` = jedna věta do 100 znaků. `@details` jen pokud přináší
informaci, kterou nelze vyčíst ze kódu. `@note` pro HW závislosti (jádro,
periferie, domény, cache, errata).

## 4. Hlavička funkce

Doxygen blok **jen u funkcí v hlavičkovém souboru** (veřejné API) a u složitých
statických funkcí. Triviální statické funkce (3 řádky, jasný název) blok nemají.

```c
/**
 * @brief  Odešle rámec a čeká na potvrzení.
 * @param  data  Ukazatel na data, min. 32B zarovnaný (kvůli cache).
 * @param  len   Délka v bajtech, 1..MAX_FRAME (256).
 * @param  timeout_ms  0 = neblokující.
 * @retval HAL_OK       potvrzeno
 * @retval HAL_TIMEOUT  bez odpovědi v limitu, linka zůstává inicializovaná
 * @retval HAL_ERROR    chyba periferie, nutná reinicializace přes drv_reset()
 * @note   Nesmí být voláno z ISR (blokuje na semaforu).
 */
```

**Vždy uveď u parametrů:** jednotky, povolený rozsah, kdo vlastní paměť.
**Vždy uveď u návratu:** všechny hodnoty a co znamenají pro další chování.

## 5. Komentáře v těle funkce

- Blokový komentář `/* … */` nad logickým krokem, max. 2 řádky.
- Řádkový `//` na konec řádku jen krátce (do ~40 znaků), zarovnaně.
- Komentuj povinně:
  - **magické konstanty** (odkud pochází — datasheet, kapitola, výpočet),
  - **workaroundy erraty** (číslo erraty + revize silikonu),
  - **záměrně prázdné bloky** (`/* záměrně prázdné: příznak maže HW při čtení DR */`),
  - **cache / DMA / bariéry** (proč je tam ta operace a co se stane bez ní),
  - **synchronizaci mezi jádry** (kdo drží HSEM, jaké je pořadí),
  - **kritické pořadí operací** (`/* pořadí je závazné: nejdřív hodiny, pak GPIO */`).

```c
/* Errata 2.2.9 (rev V): po zápisu CR1 je nutná prodleva 2 hodinové cykly,
   jinak se ztratí následující zápis do CR2. */
__DSB();
```

## 6. Co se nekomentuje

Getery/setery, zjevné cykly, `return`, uzávěry bloků (`} // end if`),
změny historie („// 2024-03-12 upravil JN“) — na to je git.
Zakomentovaný kód se **maže**, ne archivuje.

## 7. TODO / FIXME

Jen ve formátu s vlastníkem a datem, jinak se maže:

```c
// TODO(jmeno, 2026-09-09): doplnit kontrolu CRC po odsouhlasení protokolu v2
// FIXME(jmeno, 2026-09-09): S2 — chybí timeout, viz docs/audit/2026-09-09_spi.md#F-0007
```

## 8. Formátování kódu

Vynucuje `.clang-format` (viz `.clang-format` v korenu), zde jen to, co nástroj
neumí:

- Šířka řádku 100 znaků, odsazení 4 mezery, žádné taby.
- Jeden příkaz na řádek, vždy složené závorky i u jednořádkového `if`.
- `if (HAL_x(...) != HAL_OK)` — porovnávej explicitně, ne `if (!HAL_x(...))`.
- Registry a bitové masky přes CMSIS makra (`USART_CR1_UE`), ne číselné literály.
- `static` u všeho, co nemá být viditelné mimo modul.
- Typy z `<stdint.h>` (`uint32_t`), nikoli `unsigned int`; `bool` z `<stdbool.h>`.
- Konstanty přes `static const` nebo `enum`, ne `#define` (kde to jde).
- Pořadí v modulu: includy → makra → typy → prototypy statických → data → funkce.

## 9. Pravidla pro průchod komentáři (fáze F6)

1. **Nikdy neměň logiku.** Commit obsahuje jen komentáře a whitespace.
2. Anglické komentáře v ručně psaném kódu se přepisují do češtiny podle pravidel
   výše. Komentáře **v generovaném CubeMX kódu mimo USER CODE bloky se nechávají**.
3. Nesouhlasí-li komentář s kódem, je to **nález S1/S2** (ne oprava komentáře!) —
   nejdřív zjisti, co je špatně: kód, nebo komentář. Zapiš do `docs/audit/`.
4. Kontrola po dokončení: build musí projít a **binárka by se měla lišit minimálně**.
   Pokud se změní, hledej `__LINE__`/`assert`/`__FILE__` (posun řádků) — cokoli
   jiného znamená, že jsi omylem změnil kód.
   ```bash
   arm-none-eabi-size build/app.elf   # před a po, porovnej .text/.data/.bss
   ```
