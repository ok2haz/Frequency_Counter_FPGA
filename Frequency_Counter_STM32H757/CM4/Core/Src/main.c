/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "eth.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ipc_cm4.h"   /* IPC konzument: cte snapshot CM7->CM4 + publikuje heartbeat */
#include "iwdg2.h"     /* nezavisly watchdog CM4 (~4 s); zaseknuta smycka -> reset CM4 */
#include "lwip_app.h"  /* lwIP NO_SYS=1: DHCP klient + ping (F5) */
#include "scpi.h"      /* scpi_selftest — dukaz, ze SCPI jadro na CM4 skutecne BEZI (W2) */
#include "httpd_min.h" /* httpd_min_selftest — dukaz pro HTTP parser (W4) */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#ifndef HSEM_ID_0
#define HSEM_ID_0 (0U) /* HW semaphore 0*/
#endif

/* Adresa PHY na MDIO. Overeno na HW 2026-08-22 skenem z CM7 (`eth` -> "PHYAD 0"). */
#define ETH_PHY_ADDR   0u

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile uint32_t beepTicks = 0;

/* ── Degradovany bring-up periferii CM4 ──────────────────────────────────────
 * ⚠️ PRINCIP: na CM4 nesmi selhani periferie zabit jadro. CubeMX generuje do
 * kazdeho MX_*_Init() `Error_Handler()`, coz je tady `while(1)` — a mrtva CM4
 * znamena mrtve IPC, tedy "4:off" na displeji. Kvuli nefunkcnimu ethernetu
 * (nebo pipaku!) bychom prisli o funkcni dvoujadro. CM7 si `Error_Handler`
 * dovolit muze (kdyz nejede on, nejede pristroj a `bootled_fail` to odpipa),
 * CM4 je ale KONEKTIVITA — ta smi chybet.
 *
 * Reseni je ZAMERNE mimo generovany kod, aby ho regenerace nesmazala: po dobu
 * initu periferii se zvedne `g_init_nonfatal` a `Error_Handler()` (jehoz telo
 * lezi v USER CODE bloku) se v tom okne jen zapocita a VRATI. `Error_Handler`
 * neni `noreturn`, takze je to legalni; volajici MX_*_Init pak dobehne do konce.
 * Diky tomu je `eth.c`/`eth.h` uplne BEZE ZMENY proti vystupu CubeMX. */
volatile uint8_t  g_init_nonfatal = 0;   /* 1 = bezi bring-up, selhani se toleruje */
volatile uint8_t  g_init_faults   = 0;   /* kolik MX_*_Init v tom okne selhalo */
volatile uint8_t  g_eth_init_ok   = 0;   /* 1 = HAL_ETH_Init proslo (bezi RMII REF_CLK) */
volatile uint32_t g_eth_phy_id    = 0;   /* PHYID1<<16|PHYID2; LAN8742A = 0x0007C131, 0 = neprecteno */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

/* USER CODE BEGIN Boot_Mode_Sequence_1 */
  /*HW semaphore Clock enable*/
  __HAL_RCC_HSEM_CLK_ENABLE();
  /* Activate HSEM notification for Cortex-M4*/
  HAL_HSEM_ActivateNotification(__HAL_HSEM_SEMID_TO_MASK(HSEM_ID_0));
  /*
  Domain D2 goes to STOP mode (Cortex-M4 in deep-sleep) waiting for Cortex-M7 to
  perform system initialization (system clock config, external memory configuration.. )
  */
  HAL_PWREx_ClearPendingEvent();
  HAL_PWREx_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFE, PWR_D2_DOMAIN);
  /* Clear HSEM flag */
  __HAL_HSEM_CLEAR_FLAG(__HAL_HSEM_SEMID_TO_MASK(HSEM_ID_0));

/* USER CODE END Boot_Mode_Sequence_1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* ⚠️ PER-CORE RCC (audit #23, 2026-08-11) — CM4 si musi povolit hodiny ve SVE
   * domene (RCC_C2_*ENR), ne v domene CM7.
   *
   * Past: `__HAL_RCC_XXX_CLK_ENABLE()` zapisuje VZDY do `RCC->xxxENR` = registr
   * CPU1 (CM7) — makra NEJSOU prepinana podle `CORE_CM4`, C2 varianty jsou
   * samostatna rodina `__HAL_RCC_C2_XXX_CLK_ENABLE()`. Generovany kod CubeMX
   * (MX_GPIO_Init, HAL_TIM_Base_MspInit, HAL_MspInit) pouziva plain varianty,
   * takze CM4 dosud povoloval hodiny CM7. Dnes to "funguje", protoze periferie
   * je clockovana, kdyz je bit v KTEREMKOLI z obou registru — jenze prirazeni
   * domene rozhoduje pri low-power a autonomnim behu D2. S ETH by to kouslo.
   *
   * Reseni je regen-safe: generovany kod NEmenime (prepsal by ho regen), jen
   * tady navic povolime tytez hodiny i v C2 domene. Redundantni zapis nevadi.
   * Musi byt PRED MX_GPIO_Init/MX_TIM12_Init. */
  __HAL_RCC_C2_GPIOG_CLK_ENABLE();    /* LED_2 (PG7) */
  __HAL_RCC_C2_TIM12_CLK_ENABLE();    /* beeper PWM */
  __HAL_RCC_C2_HSEM_CLK_ENABLE();     /* boot gate + budouci notifikace */
  __HAL_RCC_C2_SYSCFG_CLK_ENABLE();
  /* D2 SRAM2+3 = domaci RAM CM4 (linker `RAM @0x10020000/160K`). Dnes bezi na
   * reset-default, ale ETH deskriptory a lwIP heap tu poblezi -> vlastnit je
   * explicitne (DUALCORE_BRINGUP_CHECKLIST.md §4). */
  __HAL_RCC_C2_D2SRAM2_CLK_ENABLE();
  __HAL_RCC_C2_D2SRAM3_CLK_ENABLE();
  /* USER CODE END Init */

  /* USER CODE BEGIN SysInit */
  /* Otevri okno degradovaneho bring-upu — plati pro VSECHNA MX_*_Init nize
   * (uzavira se v USER CODE 2). Zamerne pro vsechny, ne jen pro ETH: zadna
   * periferie CM4 (pipak, LED, ETH) nestoji za to, aby kvuli ni umrelo cele
   * jadro a s nim IPC. Viz `g_init_nonfatal` v PV. */
  g_init_nonfatal = 1;
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM12_Init();
  MX_ETH_Init();
  /* USER CODE BEGIN 2 */
  /* Bring-up dobehl -> `Error_Handler` je od ted zase skutecne fatalni. */
  g_init_nonfatal = 0;

  /* Stav ETH se zjistuje ZVENCI z handle (`gState` je READY jen po uspesnem
   * HAL_ETH_Init) — proto nemusime nic pridavat do generovaneho `eth.c`.
   * PHY ID (reg 2/3 = PHYID1/PHYID2, IEEE 802.3 cl.22) = kriterium F3: dukaz, ze
   * s LAN8742A mluvi CM4 pres MDIO/HAL, ne jen CM7 bit-bangem (UART `eth`).
   * ⚠️ Cte se az po uspesnem initu — driv neni nastaveny MDIO CSR clock range,
   * takze `g_eth_phy_id == 0` znamena "nedoslo se sem", NE "PHY mlci". */
  g_eth_init_ok = (heth.gState == HAL_ETH_STATE_READY) ? 1u : 0u;
  if (g_eth_init_ok)
  {
    uint32_t id1 = 0u, id2 = 0u;
    if (HAL_ETH_ReadPHYRegister(&heth, ETH_PHY_ADDR, 2u, &id1) == HAL_OK &&
        HAL_ETH_ReadPHYRegister(&heth, ETH_PHY_ADDR, 3u, &id2) == HAL_OK)
    {
      g_eth_phy_id = ((id1 & 0xFFFFu) << 16) | (id2 & 0xFFFFu);
    }
  }

  Beep(420, 200);
  HAL_Delay(400);
  Beep(840, 200);
  HAL_Delay(400);
  Beep(1680, 200);
  HAL_Delay(400);
  ipc_cm4_init();   /* IPC: reset lokalniho stavu pred ctenim snapshotu */

  /* W2 (2026-08-23): dukaz, ze `scpi.c`+`ipc_scpi.c` na CM4 SKUTECNE BEZI, ne jen se
   * prelozi (-DCORE_CM4 uz drive dokazal jen preklad, viz WEB_UI_PLAN.md). Test je
   * pure-logic (fabrikuje si vlastni `scpi_src_t`, viz scpi.c), takze je bezpecny
   * spustit i pred rozjezdem ETH/lwIP. Vysledek jde do IPC (v7) — CM4 nema konzoli,
   * takze IPC je jediny kanal, kterym se to da zvenku overit. */
  ipc_cm4_set_scpi_selftest((uint8_t)scpi_selftest());
  /* W4 (2026-08-23): stejny dukaz pro parser HTTP pozadavku (httpd_min.c) — take
   * pure-logic, bezpecny pred ETH/lwIP. Vysledek do IPC (v9). */
  ipc_cm4_set_httpd_selftest((uint8_t)httpd_min_selftest());
  /* ETH bring-up vysledek -> CM7 (v6, F3). MX_ETH_Init je zamerne NEfatalni
   * (viz eth.c), takze sem dojdeme i kdyz ETH nenabehlo — a prave to chceme
   * ohlasit. `g_eth_phy_id` != 0 = CM4 skutecne precetla LAN8742A pres MDIO,
   * cimz je F3 splnene (dosud PHY cetl jen CM7 bit-bangem).
   * Prvni publikace hned pri bootu; smycka ji pak opakuje (viz komentar tam). */
  ipc_cm4_set_eth(g_eth_init_ok, g_eth_phy_id);

  /* lwIP (F5) — az PO `ipc_cm4_init()`, protoze `lwip_app_process()` publikuje stav
   * linky pres IPC. Bezpecne i kdyz ETH nenabehlo: rozhrani zustane "down" a modul
   * jen tise tika (viz degradace v lwip_app.c). */
  lwip_app_init();
  /* ⚠️⚠️ IWDG2 ZAMERNE VYPNUTY (zmereno na HW 2026-08-13) ─────────────────────
   * Reset scope IWDG2 je **SYSTEM-WIDE, ne per-core**. Overeno primo: docasna
   * CM4, ktera po 20 s prestala krmit watchdog, shodila CELY pristroj —
   * uptime CM7 pak cykloval 3 -> 13 -> 22 -> 8 -> 17 s, tedy reset kazdych ~24 s.
   * Predpoklad "CPU2/CM4-only" z DUALCORE_BRINGUP_CHECKLIST §8 tedy NEPLATI.
   *
   * Zapnuty IWDG2 by znamenal, ze zaseknuta CM4 (ktera dnes dela temer nic)
   * shodi i displej a mereni. To je vyrazne horsi nez zaseknuta CM4 samotna,
   * proto se nepouziva — presne jak pro tenhle pripad predepisuje CLAUDE.md.
   *
   * Zaseknuti CM4 se NEZTRATI: CM7 ho vidi pres heartbeat (`ipc_cm4_alive`),
   * loguje `stall:CM4` a pocita `g_cm4_stall_count` (UART `status`).
   * Az bude na CM4 bezet ETH/SCPI, da se pridat cileny restart CM4 z CM7
   * (drzet ho v resetu pres RCC) — to uz je ale jina vec nez nezavisly watchdog.
   * iwdg2_init(); */
  (void)iwdg2_init;   /* ponechano prelozene, at je to jednorazove vratitelne */

  /* DWT cyklovy citac pro mereni VLASTNI zateze CM4 (idle-based) -> publikuje se
   * pres IPC heartbeat jako "CM4:xx%" v CM7 headeru. Clock-agnosticky: pocita se
   * pomer busy_cyc / total_cyc, netreba znat SystemCoreClock (na CM4 nemusi byt
   * spravne, protoze CM4 nevola SystemClock_Config). */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
  /* Lokalni v main() (ta nikdy neskonci) — proto BEZ prefixu `s_`, ktery je
   * v projektu vyhrazeny pro file-scope/static promenne. */
  uint32_t cm4_busy_cyc = 0, cm4_win_cyc0 = 0, cm4_win_ms = 0, cm4_pct = 0;
  uint32_t cm4_slow_ms = 0, cm4_led_ms = 0;   /* throttle pomale casti a LED */
  uint8_t  cm4_have = 0, cm4_gps = 0;         /* posledni vysledek cteni snapshotu */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  /* ⚠️ SMYCKA JE ROZDELENA NA RYCHLOU A POMALOU CAST (prestavba pri F5, 2026-08-22).
   * Puvodne koncila `HAL_Delay(800)`, coz bylo pro ethernet nepouzitelne: lwIP by se
   * obslouzil 1x za 800 ms, RX ring (4 deskriptory) by pri jakemkoli provozu pretekl
   * a ping by mel RTT skoro sekundu. Ted se kazdou iteraci (~1 ms) obsluhuje jen lwIP,
   * kdezto IPC a LED bezi na ~5 Hz — cist cely snapshot (~0,3 kB) tisickrat za sekundu
   * by bylo plytvani a heartbeat CM7 stejne cte 2x/s.
   * LED_2 uz NEBLIKA blokujicimi HAL_Delay, ale stavovym automatem nad HAL_GetTick. */
  while (1)
  {
	  uint32_t cyc0 = DWT->CYCCNT;   /* start mereni work-cyklu teto iterace */
	  uint32_t now  = HAL_GetTick();

	  /* ── RYCHLA CAST: kazdou iteraci ──────────────────────────────────────── */
	  iwdg2_kick();       /* obnov watchdog CM4 (iterace ~1 ms << 4 s timeout) */
	  lwip_app_process(); /* prijate ramce + lwIP timery (DHCP/ARP/TCP) + stav linky */
	  httpd_min_poll();   /* v12: dokonci odlozene /api/log + push SSE (throttle ~20 Hz uvnitr) */

	  /* ── POMALA CAST: ~5 Hz ───────────────────────────────────────────────── */
	  if ((int32_t)(now - cm4_slow_ms) >= 200) {
		  cm4_slow_ms = now;

		  /* IPC: dokud CM7 neorazitkuje snapshot, zkousej overit hlavicku. */
		  if (!ipc_cm4_ready()) ipc_cm4_check();
		  /* Precti aktualni snapshot (seqlock) + publikuj heartbeat pro CM7.
		   * ⚠️ Duveryhodne JEN kdyz CM7 zije (snapshot seq roste) — jinak stara data. */
		  ipc_snapshot_t snap;
		  cm4_have = (ipc_cm4_ready() && ipc_cm4_cm7_alive(now) && ipc_cm4_read(&snap)) ? 1u : 0u;
		  cm4_gps  = (cm4_have && (snap.flags & IPC_F_GPS_VALID)) ? 1u : 0u;
		  ipc_cm4_heartbeat(cm4_pct, now / 1000u);   /* posledni zmerena vlastni zatez [%] */
		  /* ⚠️ PHY ID se pri STUDENEM startu nemusi precist: LAN8742A jeste bezi
		   * vlastni power-on reset, MDIO mlci a cteni vrati same jednicky
		   * (0xFFFF FFFF). Pri HW pruchodu 2026-08-30 to tak dopadlo — link i DHCP
		   * pak byly v poradku, jen `status` hlasil nesmyslne "PHY ID 0xFFFFFFFF".
		   * Dokud hodnota nedava smysl, zkousi se docist (5x/s, zastavi se hned
		   * po uspechu -> zadna trvala zatez MDIO). */
		  if (g_eth_init_ok && (g_eth_phy_id == 0u || g_eth_phy_id == 0xFFFFFFFFu))
		  {
			  uint32_t id1 = 0u, id2 = 0u;
			  if (HAL_ETH_ReadPHYRegister(&heth, ETH_PHY_ADDR, 2u, &id1) == HAL_OK &&
			      HAL_ETH_ReadPHYRegister(&heth, ETH_PHY_ADDR, 3u, &id2) == HAL_OK)
			  {
				  uint32_t id = ((id1 & 0xFFFFu) << 16) | (id2 & 0xFFFFu);
				  if (id != 0u && id != 0xFFFFFFFFu) g_eth_phy_id = id;
			  }
		  }
		  /* ETH bring-up (v6, F3) se publikuje OPAKOVANE, ne jen jednou po initu:
		   * samostatny reset CM7 dela v `ipc_init` memset cele sdilene struktury,
		   * takze jednorazovy zapis by se ztratil a Health by hlasil "ETH:--". */
		  ipc_cm4_set_eth(g_eth_init_ok, g_eth_phy_id);
	  }

	  /* LED_2 = VIDITELNY dukaz mezijaderneho ctení: sviti trvale pri GPS fixu ze
	   * snapshotu CM7; jinak (bez IPC / bez fixu) pomalu blika = holy heartbeat. */
	  if (cm4_gps) {
		  HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, GPIO_PIN_SET);
	  } else if ((int32_t)(now - cm4_led_ms) >= 400) {
		  cm4_led_ms = now;
		  HAL_GPIO_TogglePin(LED_2_GPIO_Port, LED_2_Pin);
	  }

	  /* Zmer VLASTNI zatez: work-cykly teto iterace (vse KROME nasledneho HAL_Delay,
	   * ktere je "idle"). Kazdou ~1 s spocitej pomer busy/total -> cm4_pct.
	   * S lwIP uz to nebude 0 % — naskoci to podle provozu na siti. */
	  cm4_busy_cyc += DWT->CYCCNT - cyc0;
	  if (now - cm4_win_ms >= 1000u) {
		  uint32_t total = DWT->CYCCNT - cm4_win_cyc0;   /* celkove cykly v okne (unsigned wrap OK) */
		  cm4_pct = total ? (uint32_t)((uint64_t)cm4_busy_cyc * 100u / total) : 0u;
		  if (cm4_pct > 100u) cm4_pct = 100u;
		  cm4_busy_cyc = 0u; cm4_win_cyc0 = DWT->CYCCNT; cm4_win_ms = now;
	  }

	  HAL_Delay(1);   /* jediny "idle" bod smycky -> lwIP se obsluhuje ~1000x/s */

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_FMC|RCC_PERIPHCLK_ADC
                              |RCC_PERIPHCLK_SPI2|RCC_PERIPHCLK_LTDC;
  PeriphClkInitStruct.PLL2.PLL2M = 5;
  PeriphClkInitStruct.PLL2.PLL2N = 40;
  PeriphClkInitStruct.PLL2.PLL2P = 1;
  PeriphClkInitStruct.PLL2.PLL2Q = 1;
  PeriphClkInitStruct.PLL2.PLL2R = 2;
  PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_2;
  PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
  PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
  PeriphClkInitStruct.PLL3.PLL3M = 5;
  PeriphClkInitStruct.PLL3.PLL3N = 35;
  PeriphClkInitStruct.PLL3.PLL3P = 2;
  PeriphClkInitStruct.PLL3.PLL3Q = 2;
  PeriphClkInitStruct.PLL3.PLL3R = 7;
  PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_2;
  PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOMEDIUM;
  PeriphClkInitStruct.PLL3.PLL3FRACN = 0;
  PeriphClkInitStruct.FmcClockSelection = RCC_FMCCLKSOURCE_PLL2;
  PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL2;
  PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL3;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void Beep(uint32_t frequency, uint32_t duration_ms)
{
    uint32_t period = 1000000 / frequency;
    uint32_t pulse = period / 2;

    __HAL_TIM_SET_AUTORELOAD(&htim12, period - 1);
    __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, pulse);

    HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_2);

    beepTicks = duration_ms;
}


/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* ⚠️ Behem bring-upu periferii NENI selhani duvod k zastaveni CM4 — viz komentar
   * u `g_init_nonfatal` v PV. Zapocitej a vrat se; volajici MX_*_Init dobehne.
   * Tim padem NEMUSIME sahat do generovaneho `eth.c` (regen ho prepisuje).
   * Po dokoncení initu (`g_init_nonfatal = 0` v USER CODE 2) uz je chovani
   * puvodni, tj. skutecne fatalni. */
  if (g_init_nonfatal)
  {
    if (g_init_faults < 255u) g_init_faults++;
    return;
  }
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
