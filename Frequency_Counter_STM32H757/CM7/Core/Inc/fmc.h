/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : FMC.h
  * Description        : This file provides code for the configuration
  *                      of the FMC peripheral.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __FMC_H
#define __FMC_H
#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern SDRAM_HandleTypeDef hsdram1;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_FMC_Init(void);
void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef* hsdram);
void HAL_SDRAM_MspDeInit(SDRAM_HandleTypeDef* hsdram);

/* USER CODE BEGIN Prototypes */

/* Inicializacni sekvence SDRAM dle JEDEC. Vraci 0 pri uspechu, jinak CISLO
 * KROKU, ktery selhal (1 CLK_ENABLE, 2 PALL, 3 AUTOREFRESH, 4 LOAD_MODE,
 * 5 ProgramRefreshRate, 6 SDRTR se nepotvrdil).
 * ⚠️ Do 2026-09-07 se navratove hodnoty vsech kroku ZAHAZOVALY, takze se
 * sekvence mohla tise nedokoncit — a presne to vypadalo jako "po teplem resetu
 * OK, po studenem ne".
 * ⚠️ Lze spustit ZNOVU za behu (UART `sdraminit`) — to je pokus, ktery odlisi
 * chybu v CASOVANI prvni inicializace od jine priciny. Bezi z UartTasku;
 * behem nej muze displej kratce probliknout (LTDC cte tutez SDRAM). */
/* Ocekavana hodnota `SDRTR`. Vystavena, aby ji `status` nemusel opisovat —
 * druha kopie te konstanty by se drive nebo pozdeji rozesla a diagnostika by
 * tise porovnavala proti spatnemu cislu (SKILL §5).
 * ⚠️ 2026-09-09: presne to uz nastalo — `fmc.c` mel VLASTNI `#define
 * REFRESH_COUNT 371` a tohle byla jeho rucni kopie. Dve mista, jedna pravda.
 * Od teto zmeny je zdrojem pravdy TENHLE radek a `fmc.c` z nej odvozuje.
 *
 * 🔴 ZPET NA 371 (2026-09-09, druhe mereni). Docasne zvyseni na 175 (tREF 32 ms,
 * dvojnasobna rezerva) melo overit hypotezu "chybi rezerva obnovy". MERENI JI
 * VYVRATILO: pri SDRTR=175 hlasil `membench` porad 496 068 chybnych bitu retence
 * a `bgcheck` 120/120 rozpadlych bloku. Kdyby slo o rezervu obnovy, dvojnasobek
 * by to vyrazne zlepsil. Nezlepsil -> obnova to NENI a hleda se ve CTECI CESTE
 * (viz `ReadPipeDelay` ve fmc.c). Vraceno na spec hodnotu, at castejsi obnova
 * zbytecne nebere pasmo LTDC, ktere podteka. */
#define REFRESH_COUNT_EXPECTED 371

/* 🔑 F-0151 (2026-09-25): hodnota vyse NENI volne cislo, ale vysledek vzorce —
 * a ted to hlida prekladac. Do teto zmeny stal ve `fmc.c` `_Static_assert`,
 * ktery po sjednoceni zdroje porovnaval `REFRESH_COUNT_EXPECTED` SAM SE SEBOU:
 * TAUTOLOGIE, ktera nemohla selhat nikdy, ale tvarila se jako pojistka.
 *
 * Skutecna vada, ktera hrozi, je jina: rozchod hodnoty s TAKTEM SDCLK. Presne
 * ta tu byla od prvniho commitu (1835 prevzatych z ST prikladu pro jinou desku
 * = obnova matice za 304 ms misto 64 ms, 3 338 207 chybnych bitu).
 *
 * Mez je JEDNOSTRANNA zamerne: obnovovat se smi kdykoli CASTEJI, nikdy RIDEJI.
 * Prilis nizka hodnota jen ubira pasmo (meritelne na `LTDC: podteceni FIFO`),
 * prilis vysoka data ZTRACI. Assert proto hlida jen horni stranu.
 * ⚠️ `+1` je zaokrouhleni: presny vzorec da 370,625 a 371 je jeho zaokrouhleni
 * nahoru, tedy tREF prekrocen o 0,1 % — v praxi neskodne, ale bez te tolerance
 * by kontrola padala na spravne hodnote.
 * ⚠️ Kdyz zmenis delicku FMC nebo `SDClockPeriod`, uprav `FMC_SDCLK_MHZ`.
 * Pri ZVYSENI taktu assert projde (stara hodnota = castejsi obnova = bezpecne),
 * pri SNIZENI build spadne — a to je prave ten smer, ktery dela skodu. */
/* ⚠️ F-0157: obe hodnoty nize jsou DRUHYM vyjadrenim toho, co uz stoji
 * v konfiguraci radice — `fmc.c:207` (`RowBitsNumber = ..._ROW_BITS_NUM_13`)
 * a `fmc.c:212` (`SDClockPeriod = ..._CLOCK_PERIOD_2`). Vynutit tu vazbu NELZE:
 * jsou to runtime prirazeni v GENEROVANEM kodu mimo `USER CODE`, kam
 * `_Static_assert` nedosahne a kde by regen upravu smazal. Kdyz tam nekdo sahne,
 * MUSI to prenest sem — jinak assert nize projde, ale bude pocitat s jinou
 * geometrii, nez jakou ma radic (L-0018). */
#define FMC_SDCLK_MHZ           50u     /* PLL2R 100 MHz / SDClockPeriod_2, viz fmc.c:212 */
#define SDRAM_ROWS            8192u     /* RowBitsNumber = 13 -> 2^13 radku, viz fmc.c:207 */
#define SDRAM_TREF_US        64000u     /* tREF cele matice, MT48LC16M16A2 do 85 C */
#define REFRESH_COUNT_SPEC_MAX \
    ((SDRAM_TREF_US * FMC_SDCLK_MHZ / SDRAM_ROWS) - 20u)
_Static_assert(REFRESH_COUNT_EXPECTED <= REFRESH_COUNT_SPEC_MAX + 1u,
               "REFRESH_COUNT_EXPECTED obnovuje SDRAM RIDEJI nez tREF pri danem "
               "FMC_SDCLK_MHZ — data se rozpadnou (viz historie 1835)");

uint8_t fmc_sdram_init_sequence(void);
extern volatile uint8_t  g_fmc_init_fail;   /* 0 = sekvence prosla cela */
extern volatile uint32_t g_fmc_init_runs;

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif
#endif /*__FMC_H */

/**
  * @}
  */

/**
  * @}
  */
