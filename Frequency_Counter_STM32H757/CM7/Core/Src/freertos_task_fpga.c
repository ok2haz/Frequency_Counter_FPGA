/*
 * freertos_task_fpga.c
 *
 * SPI2 čítač kmitočtu z FPGA (StartFpgaTask) — vyčleněno z freertos.c.
 * Inicializuje SPI, polluje ~20 Hz, nové měření naformatuje do g_freq_text
 * a stav linky do g_spi_text pro UiTask. Protokol viz CLAUDE.md / fpga_freq.c.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os2.h"

#include <string.h>
#include <stdbool.h>

#include "fpga_freq.h"
#include "freertos_shared.h"
#include "watchdog.h"    /* watchdog_kick_fpga — heartbeat */
#include "sdram_log.h"   /* datova cache mereni v SDRAM (dlouha presna historie) */
#include <stdio.h>       /* printf — hlaseni vadneho SDRAM regionu pri initu */
#include "errlog.h"   /* udalosti: ztrata linku / signalu FPGA */
#include "sensor_stat.h"  /* g_sensors[SENS_T4A] — FPGA teplota pro rekalibraci TDC */

/* Teplotni rekalibrace TDC (2026-10-04). Kalibracni tabulka hustoty kodu plati
 * pro teplotu, pri ktere probehla: zpozdeni carry retezu na kremiku driftuji
 * s teplotou (~0,1-0,3 %/°C), takze kody se posunou a presnost TDC degraduje.
 * CM7 hlida FPGA teplotu (TMP117 0x4A u FPGA) a pri driftu >= TDC_RECAL_DELTA_C
 * od posledni kalibrace posle `tdc cal` (bezi ve FPGA ~0,5 s, mereni se zastavi).
 * Neblokujici: jen posle povel, dalsi polly ukazou BUSY a pak novou tabulku. */
#define TDC_RECAL_DELTA_C   3.0f      /* prah driftu FPGA teploty pro rekalibraci [°C] */
#define TDC_RECAL_MIN_MS    30000u    /* min. odstup dvou rekalibraci (proti thrashingu na prahu) */
volatile uint32_t g_tdc_recal_count = 0;   /* kolik teplotnich rekalibraci probehlo (status) */
volatile int16_t  g_tdc_cal_temp_c10 = 0;  /* teplota posledni kalibrace, ×10 °C (status) */

/* Cesta A (2026-10-04): vyrazeni oken poskozenych obrim binem TDC (kod 134 = 19,5 %
 * hran). Kdyz hrana okna padne do saturovaneho kodu, dt_a je o ~2-4 ns mimo ->
 * hi-res kmitocet jednorazove skoci o ~1,6·10⁻⁸ (max 4330 ps / 250 ms okno). To je
 * >> sum dobrych hran (~7·10⁻¹⁰) a hluboko pod realnou zmenou signalu (>1e-4, tu resi
 * signal-match reset v screen_main). Takove okno se nepridava do statistiky/Allan.
 * Prah relativne: uhz_prev / TDC_SPIKE_REL_DIV = uhz_prev × 5·10⁻⁹ (~0,05 Hz @ 10 MHz). */
#define TDC_SPIKE_REL_DIV   50000000ull    /* 2·10⁻⁸ (~5× skok obriho binu 9,28 ns = 3,7·10⁻⁸ je presne za prahem). Do 2026-10-08 3·10⁻⁹: s cistym vstupem to vyrazovalo 8,3 % BEZNYCH oken (sigma okna 196 ps = 0,8·10⁻⁹), tedy umele zlepsovalo statistiku */
volatile uint32_t g_tdc_spike_count = 0;   /* kolik oken vyrazeno jako obri-bin artefakt (status) */

void StartFpgaTask(void *argument)
{
  (void)argument;              /* signaturu urcuje CMSIS-RTOS, parametr nepouzivame */
  /* Kontrakt bod 1/8: nebudit SPI piny, dokud FPGA nedokonci konfiguraci z flash
   * po power-on/resetu. CS uz drzime vysoko (idle); jeste pridame prodlevu, aby
   * GW1NR-9 stihl nabootovat drive, nez na nej zacneme clockovat. */
  osDelay(250);

  fpga_freq_init();

  /* Datova cache mereni v SDRAM. Init SAM OVERI pamet (vzory + adresni aliasing
   * pres celych 16 MB) — `membench` testuje aliasing jen do 2 MB a tenhle region
   * lezi az na 0xC1000000. Pri vadne pameti se log NEZAPNE, aby analyza radeji
   * nemela data nez tise prepisovana. */
  if (!sdram_log_init()) {
    sdram_log_stat_t st; sdram_log_stat(&st);
    printf("sdram_log: VYPNUT - %s\n", st.fail);
  }

  fpga_meas_t m;
  uint32_t fails = 0;
  uint8_t  lost  = 0;
  float    tdc_cal_t = -1000.0f;    /* teplota [°C] posledni kalibrace TDC; < -500 = jeste nemame referenci */
  uint32_t tdc_cal_next_ms = 0;     /* HAL_GetTick, od kdy smi dalsi rekalibrace (rate-limit) */
  uint64_t uhz_prev = 0u;           /* hi-res kmitocet posledniho PRIJATEHO okna [µHz] — spike reject (cesta A) */
  uint32_t reject_run = 0u;         /* kolik oken za sebou zamitnuto (pojistka proti zablokovani na realne zmene) */
  for (;;) {
    watchdog_kick_fpga();   /* heartbeat pro IWDG (zatuhnuti FpgaTasku -> reset) */
    if (fpga_freq_poll(&m)) {
      fails = 0;
      if (fpga_freq_poll_miscount() != 0) {
      /* Chybne napocitane okno (hrana navic/chybi, `fpga_freq_miscount`): neni to
       * mereni. Displej ho nezobrazi (drzi predchozi okno), do statistiky ani
       * datalogu nejde; v SDRAM logu je MEZERA (priznak SPIKE). `fpga_stat_break`
       * se nevola -- vzorek se doplni ze zbylych oken (stejne jako spike). */
        sdram_log_put(m.sequence, 0u, 0u, SDRAM_LOG_F_A_VALID | SDRAM_LOG_F_SPIKE,
                      HAL_GetTick(), m.gate_time_ns, fpga_sim_active() ? 1u : 0u);
      } else {
      /* Vyber zobrazovany zdroj: /4 (nejlepsi rozliseni) dokud bez chyby a pod ~380 MHz,
       * jinak /16 (vyssi rozsah). freq*_x100000 uz ma delicku zahrnutou -> jen /100000. */
      int use16 = 0;
      uint64_t v = fpga_freq_select(&m, &use16);
      char buf[48], ibuf[64];
      fpga_freq_format_val(v, buf, sizeof(buf));
      fpga_freq_format_info(&m, use16, ibuf, sizeof(ibuf));
      taskENTER_CRITICAL();
      strncpy((char *)g_freq_text, buf, sizeof(g_freq_text) - 1);
      g_freq_text[sizeof(g_freq_text) - 1] = '\0';
      strncpy((char *)g_freq_info, ibuf, sizeof(g_freq_info) - 1);
      g_freq_info[sizeof(g_freq_info) - 1] = '\0';
      /* Numericka hodnota pro headline + statistiky (#1): vybrany zdroj v (/4 nebo
       * /16, x1e5) + SEQUENCE (kadence vzorku) + priznak platnosti. screen_main to
       * cte misto simulace. Platne = poll vratil ramec, mereni VALID a bez SIGNAL_LOST. */
      g_freq_x100000 = v;
      g_freq_seq     = m.sequence;
      g_freq_valid   = (m.measurement_status & 0x01u)
                       && !(m.error_flags & FPGA_ERR_SIGNAL_LOST);
      /* Surova reciproka dvojice -> headline si z ni dopocita VIC DESETIN, nez nese
       * zaokrouhlene `x100000` (f = edges × 4 × 1e9 / gate_ns).
       * ⚠️ `edge_count` je pocet period POUZE pin28 (/4); pro /16 (pin27) ho ramec
       * nema -> pri prepnuti na /16 se hi-res vypne a zobrazi se 5 desetin z x1e5. */
      g_freq_edges   = m.edge_count;
      g_freq_gate_ns = m.gate_time_ns;
      g_freq_gate_ps = m.gate_ps;
      g_freq_hires   = (!use16 && m.edge_count > 0u && m.gate_ps > 0u) ? 1u : 0u;
      g_freq_dirty = 1;
      taskEXIT_CRITICAL();
      /* F-0171/F-0172: KAZDE platne mereni do akumulatoru statistiky i datalogu —
       * ti si pak vezmou prumer za sve okno misto posledniho vzorku (mrtva doba).
       * Kriterium platnosti je tytez jako u `g_freq_valid` vyse. */
      int spike = 0;   /* okno vyrazene spike-rejectem (obri bin) — oznaci se i v sdram_logu jako mezera */
      if ((m.measurement_status & 0x01u) && !(m.error_flags & FPGA_ERR_SIGNAL_LOST)) {
        /* 🔴 F-0193: mereni pred timhle FPGA prepsala drive, nez jsme ho precetli
         * (dira v SEQUENCE) -> rozpracovany vzorek by mel uvnitr mrtvou dobu:
         * Σhradel ~1 s, v case ale vic. Zahodit a zacit od tohoto mereni.
         * Pocita se v `fpga_freq_poll` (`status` -> radek SEQ FPGA). */
        if (fpga_freq_seq_gap() != 0u) fpga_stat_break();
        /* Cesta A: vyrad okno poskozene obrim binem TDC (viz hlavicka). Detekce =
         * jednorazovy skok hi-res kmitoctu > uhz_prev×5e-9; ref se aktualizuje VZDY
         * (realna zmena signalu se nezablokuje, nejvyse 1 okno). Vyradi se jen ze
         * statistiky/Allan; datalog (nize) i displej vedou syrovou hodnotu dal. */
        uint64_t uhz = (m.gate_ps > 0u) ? fpga_freq_hires_uhz(v, m.edge_count, m.gate_ps) : 0u;
        /* uhz_prev = posledni PRIJATE okno (ne posledni vubec) -> izolovany spike se
         * zahodi cistě (revert po nem uz neni vuci spiku). Pojistka reject_run: po 3
         * zamitnutich v rade to NENI spike, ale skutecna zmena signalu (pod prahem
         * 1e-4 signal-matche) -> prijmi a resyncni, aby se stat nezablokoval. */
        if (uhz > 0u && uhz_prev > 0u && reject_run < 3u) {
          uint64_t d = (uhz > uhz_prev) ? (uhz - uhz_prev) : (uhz_prev - uhz);
          if (d > uhz_prev / TDC_SPIKE_REL_DIV) { spike = 1; reject_run++; g_tdc_spike_count++; }
        }
        if (!spike) {
          if (uhz > 0u) uhz_prev = uhz;
          reject_run = 0u;
          fpga_acc_add(v, m.edge_count, m.gate_ps);
        }
        /* Spike (obri bin): okno se do akumulatoru NEPRIDA (dropne se), vzorek se
         * dopocte ze zbylych oken -> Allan se dal kresli (mezera je jen sub-vzorkova,
         * ~0,25 s z ~1 s vzorku). Pyramidu ZAMERNE NEresetujeme: pri ~19,5 % spiku
         * by `fpga_stat_break` Allan VYHLADOVEL (nekreslil by se vubec) a mezeru v
         * pyramide stejne neoznaci (break nuluje jen akumulator). Prava oprava je
         * odstranit obri bin ve FPGA (B1a, STATUS #267) -- uzivatel zvolil prioritu
         * B1a, ne kompromis ve statistice. Vadne okno se znaci v ULOZENE rade
         * priznakem SDRAM_LOG_F_SPIKE (nize), aby ho pozdejsi rekonstrukce poznala. */
      } else {
        fpga_stat_break();   /* #27: okna uz nenavazuji -> rozpracovany vzorek pryc */
        uhz_prev = 0u; reject_run = 0u;   /* ztrata signalu -> zahod referenci spike-rejectu */
      }
      /* Do datove cache jde kmitocet v µHz dopocteny z reciproke dvojice —
       * hi-res (~7 platnych desetin), tedy vic, nez nese zaokrouhlene `x100000`.
       * ⚠️ Nasobitel `edge_count` (1/4/16) NEODVOZUJEME sami: `fpga_freq_hires_uhz`
       * ho overuje proti autoritativni hodnote z ramce. Pevny predpoklad "×4" uz
       * jednou zpusobil, ze `fpgasim on 10000000` hlasil 40 MHz (a6c0128).
       * ⚠️ Uklada se JEN nove mereni (poll vratil ramec), takze kadence logu =
       * kadence FPGA (~4/s), ne 20 Hz pollu. */
      uint32_t lf = SDRAM_LOG_F_A_VALID;   /* kanal B az s dvoukanalovou deskou */
      if (m.error_flags & FPGA_ERR_SIGNAL_LOST) lf |= SDRAM_LOG_F_STALE;
      if (spike) lf |= SDRAM_LOG_F_SPIKE;  /* TDC-poskozene okno -> rekonstrukce Allan ho vezme jako mezeru */
      sdram_log_put(m.sequence,
                    fpga_freq_hires_uhz(v, m.edge_count, m.gate_ps),
                    0u, lf,
                    HAL_GetTick(), m.gate_time_ns,
                    fpga_sim_active() ? 1u : 0u);
      }
    } else if (!fpga_freq_link_ok()) {
      /* zadny platny ramec -> FPGA mozna bootl pozdeji / resetoval; po ~3 s znovu START
       * (20 Hz polling -> 60 iteraci = ~3 s) */
      if (++fails >= 60) {
        /* ~3 s bez jedineho platneho ramce -> link je opravdu mrtvy (ne jen
         * "zatim neni nove mereni"). Do trvale historie, at jde poznat, jestli
         * FPGA vypadava opakovane. */
        (void)errlog_put(ERRLOG_K_FPGA, 1u, fpga_freq_crc_count(), 0u, "link");
        fpga_freq_restart(); fails = 0;
      }
    } else {
      fails = 0;   /* link zije, jen zatim neni nove mereni */
    }

    /* Ztrata signalu: autoritativni z FPGA (error_flags bit1 SIGNAL_LOST, watchdog ~2.5 s)
     * nebo mrtvy link. Nahrazuje drivejsi SEQ-staleness heuristiku - ta falesne hlasila
     * stale u nizkych kmitoctu, kde se reciproke okno legitimne protahne. Pri ztrate UI
     * ztlumi kmitocet na sedou. */
    uint8_t l = (!fpga_freq_link_ok() || fpga_freq_signal_lost()) ? 1 : 0;
    if (l != lost) {
      lost = l;
      if (l) {
        (void)errlog_put(ERRLOG_K_FPGA, 2u, fpga_freq_crc_count(), 0u, "signal");
        /* F-0193: pri ztrate signalu FPGA nemeri a SEQUENCE neroste, takze
         * po navratu by na sebe navazovala — dira v case by v SEQUENCE nebyla
         * videt a rozpracovany vzorek by slepil mereni pred ztratou a po ni.
         * (U mrtveho linku FPGA meri dal, takze diru pak ukaze i SEQUENCE.) */
        fpga_stat_break();
      }
      taskENTER_CRITICAL();
      g_freq_stale = l;
      if (l) g_freq_valid = 0;   /* ztrata signalu -> hodnota uz neni platna (headline -> SIM fallback / seda) */
      g_freq_dirty = 1;     /* prekreslit kmitocet v jine barve (ztlumeny / zluty) */
      taskEXIT_CRITICAL();
    }

    /* Stav SPI/komunikace -> displej (prekreslit jen pri zmene textu/linky) */
    char sbuf[64];
    fpga_freq_format_status(sbuf, sizeof(sbuf));
    uint8_t ok = fpga_freq_link_ok() ? 1 : 0;
    taskENTER_CRITICAL();
    if (ok != g_spi_ok || strncmp((const char *)g_spi_text, sbuf, sizeof(g_spi_text)) != 0) {
      strncpy((char *)g_spi_text, sbuf, sizeof(g_spi_text) - 1);
      g_spi_text[sizeof(g_spi_text) - 1] = '\0';
      g_spi_ok = ok;
      g_spi_dirty = 1;
    }
    taskEXIT_CRITICAL();

    /* --- Teplotni rekalibrace TDC (viz hlavicka souboru) --- */
    if (fpga_freq_link_ok() && g_sensors[SENS_T4A].valid) {
      float t = g_sensors[SENS_T4A].last;
      uint32_t now_ms = HAL_GetTick();
      if (tdc_cal_t < -500.0f) {
        tdc_cal_t = t;                           /* prvni platna teplota = reference boot kalibrace */
        g_tdc_cal_temp_c10 = (int16_t)(t * 10.0f);
      } else if (now_ms >= tdc_cal_next_ms) {
        float d = t - tdc_cal_t; if (d < 0.0f) d = -d;
        if (d >= TDC_RECAL_DELTA_C && fpga_freq_tdc_cal_start()) {
          /* bez %f (nano.specs) -> teploty jako cele.desetiny */
          int o0 = (int)tdc_cal_t, o1 = (int)((tdc_cal_t - (float)o0) * 10.0f);
          int n0 = (int)t,         n1 = (int)((t - (float)n0) * 10.0f);
          printf("TDC: teplotni rekalibrace, FPGA %d.%d -> %d.%d C\n", o0, o1, n0, n1);
          tdc_cal_t          = t;
          g_tdc_cal_temp_c10 = (int16_t)(t * 10.0f);
          tdc_cal_next_ms    = now_ms + TDC_RECAL_MIN_MS;
          g_tdc_recal_count++;
        }
      }
    }

    osDelay(50);   /* ~20 Hz cteni */
  }
}
