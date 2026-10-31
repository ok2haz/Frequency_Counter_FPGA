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
#include <math.h>        /* sqrt — sigma y pri hradle (win_adev_feed) */

#include "fpga_freq.h"
#include "freertos_shared.h"
#include "watchdog.h"    /* watchdog_kick_fpga — heartbeat */
#include "sdram_log.h"   /* datova cache mereni v SDRAM (dlouha presna historie) */
#include <stdio.h>       /* printf — hlaseni vadneho SDRAM regionu pri initu */
#include "errlog.h"   /* udalosti: ztrata linku / signalu FPGA */
#include "ipc_shared.h"  /* IPC_UICFG_GATE — vybrany kanal a hradlo z g_ui_cfg (UI/SCPI/web) */
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

/* v0.40.4: σy pri tau = HRADLO z navazujicich oken (Allanova odchylka sousednich mereni, exponencialni zapominani ~64 oken).
 * Hlavni cislo dosud bralo nejistotu ze σy@1s i pri hradle 0,05..0,5 s (a pod 1 s pyramida Allanovy odchylky nema data).
 * Mezera v SEQ, jine hradlo (> 10 %), jiny kanal nebo skok kmitoctu > 1e-5 = nova rada. Plati od WIN_ADEV_MIN oken.
 * Zapisuje VYHRADNE FpgaTask, cte screen_main (meas_u_rel) a `status`. */
#define WIN_ADEV_MIN  16u
volatile float    g_win_adev   = 0.0f;     /* relativni σy(τ = hradlo); 0 = zatim nezname */
volatile uint32_t g_win_adev_n = 0u;       /* pocet rozdilu v aktualni rade */
static void win_adev_feed(uint64_t uhz, uint32_t seq, uint64_t gate_ps, uint8_t ch)
{
  static uint64_t p_uhz, p_gate;
  static uint32_t p_seq;
  static uint8_t  p_ch, have;
  static double   avar;
  if (uhz == 0u || gate_ps == 0u) { have = 0u; g_win_adev_n = 0u; g_win_adev = 0.0f; return; }
  int cont = have && seq == p_seq + 1u && ch == p_ch &&
             (gate_ps > p_gate ? gate_ps - p_gate : p_gate - gate_ps) * 10u < p_gate;
  double d = 0.0;
  if (cont) {
    d = ((double)uhz - (double)p_uhz) / (double)p_uhz;
    if (d > 1e-5 || d < -1e-5) cont = 0;                    /* zmena signalu */
  }
  if (!cont) {
    avar = 0.0; g_win_adev_n = 0u; g_win_adev = 0.0f;
  } else {
    const uint32_t n = g_win_adev_n;
    const double a = (n < 64u) ? 1.0 / (double)(n + 1u) : 1.0 / 64.0;   /* nejdriv prosty prumer, pak zapominani */
    avar += a * (0.5 * d * d - avar);
    g_win_adev_n = n + 1u;
    g_win_adev = (n + 1u >= WIN_ADEV_MIN) ? (float)sqrt(avar) : 0.0f;
  }
  have = 1u; p_uhz = uhz; p_seq = seq; p_gate = gate_ps; p_ch = ch;
}

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
  uint8_t  chan_prev  = 0xFFu;      /* kanal predchoziho zpracovaneho okna (FW >= 0x041E) */
  for (;;) {
    watchdog_kick_fpga();   /* heartbeat pro IWDG (zatuhnuti FpgaTasku -> reset) */
    /* Vybrany kanal a hradlo = `g_ui_cfg` (tlacitka GATE/CHAN, SCPI, web; vsechny jdou pres UiTask). */
    const uint8_t ui_cfg    = ipc_uicfg_norm(g_ui_cfg);
    const uint8_t want_chan = (uint8_t)((ui_cfg >> 1) & 1u);
    uint8_t want_gate       = IPC_UICFG_GATE(ui_cfg);
    if (want_gate >= IPC_GATE_N) want_gate = IPC_GATE_DEFAULT;
    /* Ramec jeste z JINEHO kanalu, nez uzivatel zvolil (FPGA prepina az s dalsim merenim) se nepouzije. */
    const int cal_ran = fpga_freq_tdccal_poll() ? 1 : 0;   /* FW >= 0x0427: FPGA ceka na kalibracni tabulku od STM (blokuje ~0,6 s) */
    if (cal_ran) { fpga_stat_break(); }                    /* behem kalibrace se nemerilo -> rozpracovany vzorek statistiky zahodit */
    if (fpga_freq_poll(&m) && !fpga_freq_chan_pending(&m, want_chan)) {
      fails = 0;
      if (fpga_freq_poll_miscount() != 0 || fpga_freq_poll_check() != FPGA_CHK_OK) {
      /* Chybne napocitane okno (hrana navic/chybi, `fpga_freq_miscount`) nebo okno, ktere
       * neproslo samokontrolou (`fpga_freq_poll_check`: kontrolni pocitani, hlidac 100 MHz,
       * delka okna proti hradlu): neni to mereni. Displej ho nezobrazi (drzi predchozi okno), do statistiky ani
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
      g_freq_chan    = (m.caps & FPGA_CAP_CHAN) ? (uint8_t)(m.channel_id & 1u) : 0u;
      g_freq_regr    = m.regr_used;   /* 2026-10-21: okno merila regrese -> podlaha presnosti hlavniho cisla (mp_res_floor_rel) */
      g_freq_dirty = 1;
      taskEXIT_CRITICAL();
      /* F-0171/F-0172: KAZDE platne mereni do akumulatoru statistiky i datalogu —
       * ti si pak vezmou prumer za sve okno misto posledniho vzorku (mrtva doba).
       * Kriterium platnosti je tytez jako u `g_freq_valid` vyse. */
      int spike = 0;   /* okno vyrazene spike-rejectem (obri bin) — oznaci se i v sdram_logu jako mezera */
      if ((m.measurement_status & 0x01u) && !(m.error_flags & FPGA_ERR_SIGNAL_LOST)) {
        /* Prepnuti kanalu A <-> B: rozpracovany vzorek je z jineho signalu a reference spike-rejectu
         * (kmitocet minuleho okna) s novym kanalem nesouvisi. */
        if ((m.caps & FPGA_CAP_CHAN) && m.channel_id != chan_prev) {
          if (chan_prev != 0xFFu) fpga_stat_break();
          chan_prev = m.channel_id; uhz_prev = 0u; reject_run = 0u;
        }
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
          win_adev_feed(uhz, m.sequence, m.gate_ps, m.channel_id);
        } else {
          win_adev_feed(0u, 0u, 0u, 0u);   /* vyrazene okno = mezera v navaznosti */
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
        win_adev_feed(0u, 0u, 0u, 0u);
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

    /* 2026-10-11: kalibrace TDC SELHALA (FPGA_TDC_CAL_FAIL = vyprsel casovac kruhu). FPGA ji zkousi opakovat sama, ale kdyz
     * selhani prezije (nebo je to starsi bitstream), mereni bezi s nominalni tabulkou a chyba jde do desitek ns BEZ priznaku.
     * Proto STM pozada o novou kalibraci, nejvyse 1x za 15 s. */
    {
      fpga_meas_t cm;
      static uint32_t s_calfail_next_ms;
      if (fpga_freq_get_last(&cm) && (cm.tdc_status & FPGA_TDC_CAL_FAIL)) {
        uint32_t n_ms = HAL_GetTick();
        if ((int32_t)(n_ms - s_calfail_next_ms) >= 0) {
          s_calfail_next_ms = n_ms + 15000u;
          if (fpga_freq_tdc_cal_start()) { printf("TDC: kalibrace selhala, spoustim znovu\n"); g_tdc_recal_count++; }
        }
      }
    }

    /* 2026-10-14: vcasne varovani rezervy TDC retezu (audit 2026-10-10_volba-kmitoctu-hodin.md kap. 10). CAL report (last/ovf)
     * se meni JEN kalibraci, takze se cte jednou po kazde dokoncene (boot, teplotni rekalibrace, opakovani po FAIL, UART `tdc cal`),
     * ne periodicky: kazde cteni je SPI transakce navic. Nemeni mereni ani FPGA; vysledek je v `status` (TDC REZERVA) a pri zmene
     * urovne v errlogu. Start je tichy: prvni vyhodnoceni se hlasi jen kdyz neni OK. */
    {
      static uint8_t  s_rez_need = 1u;        /* 1 = dalsi dokoncenou kalibraci precist (po startu hned prvni) */
      static uint32_t s_rez_try_ms;
      static uint32_t s_rez_seen_n;
      static int8_t   s_rez_lvl_prev = -1;
      fpga_meas_t rm;
      if (fpga_freq_get_last(&rm) && (rm.caps & FPGA_CAP_DT)) {
        const uint8_t both = (uint8_t)(FPGA_TDC_CAL_A | FPGA_TDC_CAL_B);
        if (rm.tdc_status & FPGA_TDC_CAL_BUSY) s_rez_need = 1u;      /* kalibrace bezi -> po jejim konci precist */
        if (s_rez_need && !(rm.tdc_status & (FPGA_TDC_CAL_BUSY | FPGA_TDC_CAL_FAIL)) && (rm.tdc_status & both) == both) {
          uint32_t t_ms = HAL_GetTick();
          if ((int32_t)(t_ms - s_rez_try_ms) >= 0) {
            fpga_tdc_cal_t rc;
            memset(&rc, 0, sizeof rc);
            s_rez_try_ms = t_ms + 5000u;                                /* neuspech (napr. zrovna simulace) nezahlti sbernici */
            if (fpga_freq_tdc_report(&rc)) s_rez_need = 0u;
          }
        }
        fpga_tdc_rez_t rz;
        if (fpga_tdc_rez_get(&rz) && rz.n != s_rez_seen_n) {            /* nove vyhodnoceni (odtud nebo z UART `tdc`) */
          s_rez_seen_n = rz.n;
          uint8_t lvl = (rz.lvl[0] > rz.lvl[1]) ? rz.lvl[0] : rz.lvl[1];
          if (s_rez_lvl_prev < 0 ? (lvl != FPGA_TDC_REZ_OK) : ((int8_t)lvl != s_rez_lvl_prev)) {
            uint32_t ppm = (rz.ovf_ppm[0] > rz.ovf_ppm[1]) ? rz.ovf_ppm[0] : rz.ovf_ppm[1];
            printf("TDC: rezerva retezu A %d, B %d tapu z %u, za koncem %lu ppm -> %s\n",
                   (int)rz.rez_tap[0], (int)rz.rez_tap[1], (unsigned)rz.taps, (unsigned long)ppm,
                   (lvl == FPGA_TDC_REZ_OK) ? "OK" : ((lvl == FPGA_TDC_REZ_POZOR) ? "POZOR" : "KRITICKE"));
            (void)errlog_put(ERRLOG_K_TDC, 0u,
                             (uint32_t)(uint16_t)rz.rez_tap[0] | ((uint32_t)(uint16_t)rz.rez_tap[1] << 16),
                             (uint32_t)rz.lvl[0] | ((uint32_t)rz.lvl[1] << 4) | (ppm << 8), "tdc");
          }
          s_rez_lvl_prev = (int8_t)lvl;
        }
      }
    }

    /* Doruci vyber kanalu a hradla do FPGA (idempotentne podle echa v ramci). */
    (void)fpga_freq_cfg_sync(want_chan, want_gate);
    /* Perioda cteni: pri kratkem hradle (50/100 ms) musi byt kratsi nez hradlo, jinak FPGA mereni prepise
     * drive, nez ho precteme (dira v SEQUENCE). Jinak ~20 Hz jako dosud. */
    { uint32_t gms = fpga_gate_idx_ms(want_gate);
      uint32_t dly = (gms <= 50u) ? 10u : ((gms <= 100u) ? 25u : 50u);
      const uint32_t pms = fpga_freq_poll_ms(0);                 /* v0.40.7: zkouska `spi poll <ms>`; vychozi 40 = beze zmeny */
      if (pms != 40u && pms < dly) dly = pms;
      osDelay(dly); }
  }
}

/* ═══════════════ Smycka GPSDO (STATUS #305) — popis v gpsdo.h ═════════════════════════════════════
 * Implementace je ZDE, ne v novem gpsdo.c: novy .c se do buildu nedostane bez Close/Open Project
 * (CLAUDE.md, mechanicke pravidlo 2). Bezi v defaultTasku (gpsdo_tick), NE ve FpgaTasku -- ten ma malo
 * zasobniku (status 2026-10-16: ~750 B volnych). Udalosti 1PPS sem predava fpga_pps_get (FpgaTask je
 * vyhlasi az po shode dvou ramcu), DAC zapisuje SensorsTask podle `g_gpsdo_code_q16`. */
#include "gpsdo.h"
#include "ad5693.h"      /* g_dac, ad5693_mv_to_code, AD5693_RESTORE_MAX_MV */
#include "gps.h"         /* gps_get (fix), gps_timtp_get (sawtooth) */
#include "meas_present.h"   /* mp_ref_ppb -- nejistota reference podle stavu smycky */
#include <math.h>

volatile uint8_t  g_gpsdo_drive;
volatile uint32_t g_gpsdo_code_q16;
volatile uint8_t  g_gpsdo_lockflag;

#define GPSDO_KDAC_DEFAULT  3.4e-11   /* relativne / LSB, zmereno 2026-10-16 (4,3-4,6 Hz/V x 76,3 uV/LSB) */
#define GPSDO_TAU_DEFAULT   1000u     /* [s] casova konstanta PLL: sum GPS 30 ns -> ~4e-11 na povelu */
#define GPSDO_ZETA          0.7
#define GPSDO_FLL_WIN0      16u       /* [s] prvni okno FLL */
#define GPSDO_FLL_WINMAX    256u
#define GPSDO_FLL_LOCK_Y    5e-10     /* FLL -> LOCK: odchylka okna pod touto mezi ... */
#define GPSDO_FLL_LOCK_WIN  128u      /* ... a okno aspon tak dlouhe (sum okna 16 s je ~3e-9) */
#define GPSDO_Y_MAX         2.5e-6    /* interval s vetsi odchylkou se nebere (EFC +-1,1 ppm + rezerva) */
#define GPSDO_OUTLIER_Y     3e-7      /* LOCK: skok > 300 ns/s proti filtru = vadna hrana (~7 sigma) */
#define GPSDO_HOLD_MS       5000u     /* bez prijateho 1PPS tak dlouho -> HOLD */
#define GPSDO_X_SLIP_NS     50000.0   /* LOCK: faze > 50 us = neco se pokazilo -> znovu FLL */
#define GPSDO_SAW_W         64u       /* okno rozhodnuti o oprave sawtooth [vzorku] */
#define GPSDO_TP_GUARD_MS   60u       /* TIM-TP musi prijit aspon tolik PRED prvnim videnim hrany */
/* Audit 2026-10-20 (slabina 3): sum odectu faze 1PPS se ODHADUJE ZA BEHU (gp_pm_update: robustni sigma druhych diferenci
 * faze / sqrt(6)); pevnych 30 ns (HYPOTEZA) bylo ~10x nad namerenymi 2,9 ns (= MP_PPS_PM_NS) a prah mereni K v FLL tim
 * ~10x prisnejsi, nez data opravnuji. Pocatecni hodnota = MP_PPS_PM_NS, meze proti nesmyslum. */
#define GPSDO_PM_NS_INIT    2.9       /* = MP_PPS_PM_NS (meas_present.h), 10h beh 2026-10-17/18 */
#define GPSDO_PM_NS_MIN     0.3       /* dolni mez (1PPS po IDES8 ~0,4 ns + GPS) */
/* 2026-10-21 (namereno na desce: odhad spadl na 0,30 ns): 1PPS dnes snima HRUBY citac 100 MHz (krok 10 ns). Kdyz je faze 1PPS
 * vuci hodinam stabilni, rozdily periody vychazeji presne 0 a druhe diference nevidi nic -- skutecna nejistota odectu je ale
 * kvantizace krok/sqrt(12) = 2,89 ns. Dolni mez je proto kvantizace citace (pocita se z clk_mhz ramce); GPSDO_PM_NS_MIN plati,
 * az bude snimani jemnejsi nez ~1 ns. */
#define GPSDO_PM_QUANT_DIV  3.4641016151377544   /* sqrt(12) */
#define GPSDO_PM_NS_MAX     30.0      /* horni mez (puvodni hypoteza) */
/* Kriticka analyza 2: prah mereni K ve FLL ZUSTAVA na 30 ns. Model sumu okna FLL (sqrt(2)*PM/n) predpoklada BILY PM, ale GPS ma
 * na 16-256 s korelovane putovani (multipath, ionosfera) -> zmerene 2,9 ns (bily sum za 1 s) by prah snizilo ~10x a mohlo vratit
 * falesne divergence ze STATUS #318. Zmereny sum jde do prezentace (mp_ref_ppb) a diagnostiky; do FLL az po overeni na desce. */
#define GPSDO_PM_NS_FLL     30.0
#ifndef GPSDO_FLL_USE_MEASURED_PM
#define GPSDO_FLL_USE_MEASURED_PM 0
#endif
#define GPSDO_FLL_K_SIGMAS  5.0       /* K se z kroku meri jen kdyz i nejmensi verohodna zmena presahuje sum o tolik sigma */

/* ── Audit 2026-10-20 (slabina 3): odhad sumu odectu faze 1PPS za behu ─────────────────────────────
 * e = chyba periody [ns] po oprave sawtooth za interval 1 s. Pro bily PM faze x: e_k = x_k - x_(k-1), d = e_k - e_(k-1) je druha
 * diference faze s rozptylem 6*sigma_x^2. Robustne: EWMA |d| (oriznuta na 3 sigma, skoky FLL/GPS nerozhodi) * 1,2533 = sigma_d,
 * sigma_x = sigma_d / sqrt(6). Plati az po 64 diferencich; souvislost se prerusi s kazdym prerusenim intervalu. */
static double   s_pm_ns = GPSDO_PM_NS_INIT, s_pm_ewma = 0.0, s_pm_eprev = 0.0;
static uint8_t  s_pm_have = 0u;
static uint32_t s_pm_n = 0u;
static double gp_clamp(double v, double lo, double hi);
static void gp_pm_update(double e, uint32_t clk_mhz)
{
    double floor_ns = (1000.0 / (double)(clk_mhz ? clk_mhz : 100u)) / GPSDO_PM_QUANT_DIV;   /* kvantizace hrubeho citace */
    if (floor_ns < GPSDO_PM_NS_MIN) floor_ns = GPSDO_PM_NS_MIN;
    if (s_pm_have) {
        double d  = fabs(e - s_pm_eprev);
        double sd = s_pm_ewma * 1.2533141373155;
        if (s_pm_n > 16u && sd > 0.0 && d > 3.0 * sd) d = 3.0 * sd;   /* optimalizace 3: orez 3 sigma (test T5: 5 sigma nadhodnocovalo o 10-16 %) */
        s_pm_ewma = (s_pm_n == 0u) ? d : s_pm_ewma + (d - s_pm_ewma) * (1.0 / 256.0);
        if (s_pm_n < 0xFFFFFFFFu) s_pm_n++;
        if (s_pm_n >= 64u)
            s_pm_ns = gp_clamp(s_pm_ewma * 1.2533141373155 / 2.449489742783178, floor_ns, GPSDO_PM_NS_MAX);
    }
    s_pm_eprev = e; s_pm_have = 1u;
}
double gpsdo_pm_ns(void) { return s_pm_ns; }

/* ── Jadro regulatoru (cista logika, testuje gpsdo_selftest) ───────────────────────────────────── */
typedef struct {
    uint8_t  state;              /* GPSDO_FLL / GPSDO_LOCK (jine stavy resi obal) */
    double   code, code_max;     /* povel [LSB] */
    double   kdac, tau;
    double   k0;                 /* K zadane uzivatelem (meze odhadu: 0,25x .. 4x) */
    double   fll_e, fll_prev_y;  /* FLL: soucet chyb periody [ns], odchylka predchoziho okna */
    double   last_dc;            /* FLL: posledni krok kodu [LSB] (odhad K) */
    uint32_t fll_n, fll_win, fll_prev_n;   /* fll_prev_n = delka predchoziho okna [s] (sum rozdilu odchylek) */
    uint8_t  settle, fll_steps, rail;
    double   x, xf, integ, code_lock, yf;   /* LOCK: faze [ns], filtr faze, integrator [rel], kod pri zamceni */
    uint32_t good, bad_run;
    uint8_t  locked, diverged;
    double   pm_ns;              /* audit 2026-10-20: sum odectu faze 1PPS [ns] (0 = GPSDO_PM_NS_INIT); plni obal z gp_pm_update */
} gp_core_t;

static double gp_clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void core_start_fll(gp_core_t *c)
{
    c->state = GPSDO_FLL;
    c->last_dc = 0.0; c->rail = 0u;
    c->fll_e = 0.0; c->fll_n = 0u; c->fll_win = GPSDO_FLL_WIN0; c->fll_prev_n = 0u;
    c->settle = 1u; c->fll_prev_y = 0.0; c->fll_steps = 0u;
    c->locked = 0u;
}

/* Jeden interval 1PPS. e_ns = chyba periody za n sekund [ns] (kladna = OCXO rychly).
 * @return 1 = interval pouzit, 0 = odmitnut. */
static int core_step(gp_core_t *c, double e_ns, uint32_t n)
{
    double y = e_ns * 1e-9 / (double)n;
    if (fabs(y) > GPSDO_Y_MAX) return 0;
    if (c->state == GPSDO_FLL) {
        if (c->settle) { c->settle--; return 1; }        /* prvni interval po kroku DAC nese prechod */
        c->fll_e += e_ns; c->fll_n += n;
        if (c->fll_n < c->fll_win) return 1;
        double ya = c->fll_e * 1e-9 / (double)c->fll_n;
        c->yf = ya;
        /* K se MERI z velkych kroku (dy / dkod): opravuje odhad (2026-10-16 simulace: pevne K 2x mimo FLL
         * rozkmitalo) a hlavne pozna SPATNE ZNAMENKO (zaporne K = kazdy krok by OCXO vzdaloval).
         * Jen kdyz je krok dost velky proti SUMU oken (STATUS #318): odchylka okna n s ma sum ~sqrt(2)*PM/n (16 s: ~2,7e-9),
         * rozdil dvou oken sqrt(2)x vic. Maly krok (napr. -73 LSB = 1,2e-9 pri K 1,7e-11) by dal nahodne K, i zaporne,
         * a smycka by vyhlasila falesnou divergenci. Zmena, kterou by dala i nejmensi verohodna strmost (0,25x K0),
         * musi sum prevysit GPSDO_FLL_K_SIGMAS-krat; jinak se K nemeri a plati dosavadni odhad. */
        int kupd = 0;
#if GPSDO_FLL_USE_MEASURED_PM
        double pm = (c->pm_ns > 0.0) ? c->pm_ns : GPSDO_PM_NS_INIT;
#else
        double pm = GPSDO_PM_NS_FLL;                   /* beze zmeny proti 0x0424 (viz GPSDO_PM_NS_FLL) */
#endif
        double s_cur = pm * 1.4142135623730951e-9 / (double)c->fll_n;
        double s_prv = pm * 1.4142135623730951e-9 / (double)(c->fll_prev_n ? c->fll_prev_n : 1u);
        double s_dif = sqrt(s_cur * s_cur + s_prv * s_prv);
        if (c->fll_steps > 0u && fabs(c->last_dc) > 50.0 && fabs(c->last_dc) * 0.25 * c->k0 > GPSDO_FLL_K_SIGMAS * s_dif) {
            double ko = (ya - c->fll_prev_y) / c->last_dc;
            if (ko <= 0.0) { c->diverged = 1u; return 1; }
            if (ko > 0.25 * c->k0 && ko < 4.0 * c->k0) { c->kdac = ko; kupd = 1; }
        }
        /* Odchylka po kroku vetsi nez 1,5x pred nim (a K se tim nevysvetlilo) = divergence. */
        if (!kupd && c->fll_steps > 0u && fabs(ya) > 1e-8 && fabs(ya) > 1.5 * fabs(c->fll_prev_y)) { c->diverged = 1u; return 1; }
        double old = c->code;
        c->code = gp_clamp(c->code - ya / c->kdac, 0.0, c->code_max);
        c->last_dc = c->code - old;
        /* Kod na dorazu a kmitocet porad mimo: rozsah EFC nestaci (nebo znamenko) -> 3 okna a konec. */
        if (fabs(ya) > 1e-8 && fabs(c->last_dc) < 1.0) c->rail++; else c->rail = 0u;
        if (c->rail >= 3u) { c->diverged = 1u; return 1; }
        c->fll_prev_y = ya; c->fll_prev_n = c->fll_n; c->fll_steps++;
        c->fll_e = 0.0; c->fll_n = 0u; c->settle = 1u;
        if (fabs(ya) < GPSDO_FLL_LOCK_Y && c->fll_win >= GPSDO_FLL_LOCK_WIN) {
            c->state = GPSDO_LOCK;
            c->x = 0.0; c->xf = 0.0; c->integ = 0.0; c->code_lock = c->code;
            c->good = 0u; c->bad_run = 0u; c->yf = 0.0;
        } else if (fabs(ya) < 3e-8 && c->fll_win < GPSDO_FLL_WINMAX) {
            c->fll_win *= 2u;
        }
        return 1;
    }
    /* LOCK: PLL 2. radu. Rovnice: x' = y_zbytek - Kp*x - I, I' = Ki*x -> wn = 1/tau, Kp = 2*zeta/tau, Ki = 1/tau^2.
     * Faze se predfiltruje (tau/30), aby sum GPS (~30 ns/s) nesel primo do povelu. */
    if (c->good > 10u && fabs(y - c->yf) > GPSDO_OUTLIER_Y) {
        if (++c->bad_run > 30u) core_start_fll(c);       /* trvale "odlehle" = skok kmitoctu, ne vadna hrana */
        return 0;
    }
    c->bad_run = 0u;
    c->x += e_ns;
    double tf = c->tau / 30.0; if (tf < 10.0) tf = 10.0;
    double a = (double)n / tf; if (a > 1.0) a = 1.0;
    c->xf += (c->x - c->xf) * a;
    double ay = (double)n / 60.0; if (ay > 1.0) ay = 1.0;
    c->yf += (y - c->yf) * ay;
    double kp = 2.0 * GPSDO_ZETA / c->tau, ki = 1.0 / (c->tau * c->tau);
    double integ_new = c->integ + ki * c->xf * 1e-9 * (double)n;
    double code = c->code_lock - (kp * c->xf * 1e-9 + integ_new) / c->kdac;
    if (code < 0.0 || code > c->code_max) code = gp_clamp(code, 0.0, c->code_max);   /* anti-windup: integrator stoji */
    else c->integ = integ_new;
    c->code = code;
    c->good += n;
    c->locked = (fabs(c->xf) < 200.0 && fabs(c->yf) < 1e-9 && c->good >= 60u) ? 1u : 0u;
    if (fabs(c->xf) > GPSDO_X_SLIP_NS) core_start_fll(c);
    return 1;
}

/* ── Obal: vstupy, sawtooth, stavy WAIT/HOLD/OFF (VYHRADNE defaultTask) ────────────────────────── */
static gp_core_t s_gc;
static uint8_t   s_g_en = 1u;                  /* vychozi ZAPNUTO (zadani 2026-10-16); nepersistuje se */
static uint8_t   s_g_state = GPSDO_WAIT, s_g_why = GPSDO_WHY_NOPPS, s_g_hold_from;
static double    s_g_tau = (double)GPSDO_TAU_DEFAULT, s_g_kdac = GPSDO_KDAC_DEFAULT;
static volatile uint32_t s_g_tau_ms;           /* HAL_GetTick posledni zmeny tau (0 = nemenilo se); gpsdo_tau_advise, F-0241 */
static uint32_t  s_g_pps_n, s_g_ts_prev, s_g_last_ok_ms, s_g_state_ms;
/* Pasivni fazovy monitor ve stavu OFF (STATUS #319): smycka DAC neridi, ale fazi OCXO proti GPS 1PPS dal soucita (surova TIE,
 * stejne intervaly a sawtooth oprava jako v LOCK). Vznikne zaznam volne bezici reference vuci GPS bez vlivu smycky (STATUS #312).
 * Nuluje se pri vstupu do OFF a pri kazdem preruseni (bez fixu, externi reference, odmitnuty interval). */
static double    s_mon_x;
static uint8_t   s_mon_on;
static uint8_t   s_g_have_prev;
static uint16_t  s_g_frac_prev;     /* v0.37.0: zlomek taktu predchozi hrany [1/16384] (jemny cas FPGA v3) */
static uint8_t   s_g_fine_prev, s_g_ep_prev;   /* osa predchozi hrany: jemny/hruby a epocha */
static volatile uint8_t s_g_kset;              /* uzivatel zadal nove K (prevezme se v dalsim intervalu) */
static volatile uint32_t s_g_cmd_q16;          /* povel kodu (i virtualni pri emulaci) */
static volatile uint8_t  s_g_virt;             /* 1 = smycka ridi virtualni DAC emulatoru (`fpgasim`) */
static uint32_t  s_g_ok, s_g_rej, s_g_gap;
/* v0.40.1 (STATUS #337b): interval PSEQ 1 -> 2 po startu FPGA (falesna prvni hrana) */
static uint16_t  s_st_prev_seq = 0x100u;                       /* 0x100 = zadna predchozi udalost */
static uint8_t   s_st_have1, s_st_have12;
static uint32_t  s_st_ts1, s_st_ts12, s_st_n, s_st_bad;
static int32_t   s_st_dev;
static uint16_t  s_st_first_seq = 0x100u;                      /* 0x100 = zatim zadna hrana */
static uint32_t  s_st_first_us, s_st_next_us;
/* sawtooth */
static uint32_t  s_q_rec_n;                    /* `n` TIM-TP pouzite pro predchozi hranu */
static double    s_q_prev;                     /* oprava predchozi hrany [ns] */
static uint8_t   s_q_have;
static double    s_e_prev[3];                  /* predchozi e: bez / -dq / +dq */
static uint8_t   s_e_prev_ok;
static double    s_sv[3];
static uint32_t  s_sv_n;
static uint8_t   s_saw = GPSDO_SAW_NODATA;
static float     s_saw_var[3];
static uint32_t  s_saw_dec;
static gpsdo_status_t s_pub;                   /* publikovany stav (kopie pod kritickou sekci) */

/* Historie smycky (viz gpsdo.h): kruhovy buffer, 1 bod / 10 s. Zapisuje jen gpsdo_tick (defaultTask). */
static int32_t  s_h_x[GPSDO_HIST_N];        /* audit 2026-10-20: faze [ps] (driv int16 ns -- zisk 1PPS pod 1 ns by zanikl) */
static int32_t  s_h_y[GPSDO_HIST_N];
static uint32_t s_h_code[GPSDO_HIST_N];
static uint8_t  s_h_st[GPSDO_HIST_N];
static volatile uint16_t s_h_head, s_h_n;      /* head = index PRISTIHO zapisu, n = platnych bodu */
static uint32_t s_h_ms;

/* ── ZMERENE vstupy nejistoty reference (STATUS #324; viz mp_ref_ppb v meas_present.h) ─────────────────────────────────
 * Z historie smycky (1 bod / 10 s, posledni SOUVISLY usek LOCK) se pocita:
 *  - u    = ADEV faze OCXO–GPS pri tau = m·10 s, m = 100 (1000 s), jinak mensi (50/20/10 podle delky useku) [ppb];
 *           mensi tau dava horsi (konzervativni) cislo, protoze v nem vic vazi sum 1PPS;
 *  - drift = |sklon kodu DAC| (LS pres cely usek, nejmin 30 min) × K [ppb/h] = zmereny dopredny drift OCXO (starnuti + teplota).
 * Cista logika nad poli (kryje gpsdo_selftest). Vraci 1, kdyz je `u` zmereno. */
static int gp_ref_core(const int32_t *x_ps, const uint32_t *code_q16, int n, double kdac, double *u_ppb, double *drift_ppb_h)
{
    static const int MS[4] = { 100, 50, 20, 10 };
    *u_ppb = 0.0; *drift_ppb_h = 0.0;
    for (int k = 0; k < 4; k++) {
        int m = MS[k];
        if (n - 2 * m < 10) continue;
        double s = 0.0; int c = 0;
        for (int i = 0; i + 2 * m < n; i++) {
            double d = ((double)x_ps[i + 2 * m] - 2.0 * (double)x_ps[i + m] + (double)x_ps[i]) * 1e-3;   /* [ns] */
            s += d * d; c++;
        }
        *u_ppb = sqrt(0.5 * s / (double)c) / ((double)m * (double)GPSDO_HIST_PERIOD_S);   /* ns/s = ppb */
        break;
    }
    if (n >= 180 && kdac > 0.0) {
        double cm = 0.0;
        for (int i = 0; i < n; i++) cm += (double)code_q16[i] / 65536.0;
        cm /= (double)n;
        const double tc = 0.5 * (double)(n - 1);
        double sxy = 0.0, sxx = 0.0;
        for (int i = 0; i < n; i++) {
            double t = (double)i - tc;
            sxy += t * ((double)code_q16[i] / 65536.0 - cm);
            sxx += t * t;
        }
        double slope = (sxx > 0.0) ? sxy / sxx / (double)GPSDO_HIST_PERIOD_S : 0.0;   /* LSB / s */
        *drift_ppb_h = fabs(slope) * kdac * 3600.0 * 1e9;
    }
    return *u_ppb > 0.0;
}

static volatile double s_ref_lock_u, s_ref_drift;    /* cache zmerenych hodnot; zapis jen gpsdo_tick (pod kritickou sekci), cte kdokoli */

static void gp_ref_update(void)
{
    static int32_t  bx[GPSDO_HIST_N];                /* 2,9 kB + 2,9 kB, ne na stack; vola jen gpsdo_tick (defaultTask) */
    static uint32_t bc[GPSDO_HIST_N];
    const uint16_t cnt = s_h_n, head = s_h_head;
    int n = 0;
    for (int k = 0; k < (int)cnt; k++) {             /* od nejnovejsiho dozadu: jen souvisly usek LOCK */
        uint16_t idx = (uint16_t)((head + GPSDO_HIST_N - 1u - (uint16_t)k) % GPSDO_HIST_N);
        if (s_h_st[idx] != GPSDO_LOCK) break;
        n++;
    }
    for (int i = 0; i < n; i++) {
        uint16_t idx = (uint16_t)((head + GPSDO_HIST_N - (uint16_t)n + (uint16_t)i) % GPSDO_HIST_N);
        bx[i] = s_h_x[idx]; bc[i] = s_h_code[idx];
    }
    double u, d;
    (void)gp_ref_core(bx, bc, n, (s_gc.kdac > 0.0) ? s_gc.kdac : s_g_kdac, &u, &d);
    taskENTER_CRITICAL(); s_ref_lock_u = u; s_ref_drift = d; taskEXIT_CRITICAL();
}

static void gp_hist_push(uint32_t now)
{
    if ((uint32_t)(now - s_h_ms) < GPSDO_HIST_PERIOD_S * 1000u) return;
    s_h_ms = now ? now : 1u;
    uint16_t h = s_h_head;
    double x = (s_g_state == GPSDO_LOCK) ? s_gc.x : 0.0;     /* TIE ma smysl jen v LOCK */
    s_h_x[h]    = (int32_t)gp_clamp(x * 1000.0, -2.1e9, 2.1e9);             /* ns -> ps, rozsah +-2,1 ms */
    s_h_y[h]    = (int32_t)gp_clamp(s_gc.yf * 1e12, -2e9, 2e9);
    s_h_code[h] = (uint32_t)(gp_clamp(s_gc.code, 0.0, 65535.0) * 65536.0 + 0.5);
    s_h_st[h]   = s_g_state;
    __DMB();
    s_h_head = (uint16_t)((h + 1u) % GPSDO_HIST_N);
    if (s_h_n < GPSDO_HIST_N) s_h_n++;
    if (s_g_state == GPSDO_LOCK) gp_ref_update();   /* nova zmereni nejistoty reference (1x / 10 s) */
}

uint16_t gpsdo_hist_count(void) { return s_h_n; }
int gpsdo_hist_at(uint16_t i, gpsdo_hist_pt_t *p)
{
    uint16_t n = s_h_n, head = s_h_head;
    if (i >= n || !p) return 0;
    uint16_t idx = (uint16_t)((head + GPSDO_HIST_N - n + i) % GPSDO_HIST_N);
    p->x_ps = s_h_x[idx];
    p->x_ns = (int16_t)gp_clamp((double)s_h_x[idx] * 1e-3, -32768.0, 32767.0);   /* kompatibilita (displej, web) */
    p->y_ppt = s_h_y[idx]; p->code_q16 = s_h_code[idx]; p->state = s_h_st[idx];
    return 1;
}

/* ── Doporuceni tau smycky (gpsdo.h, STATUS #308 C). Ciste jadro nad poli (kryje gpsdo_selftest). ── */
static const uint16_t GP_ADV_M[GPSDO_ADV_NT] = {1, 2, 5, 10, 20, 50, 100, 200};   /* nasobky 10 s */

/* Overlapping HDEV (Hadamard) z faze x [s] pri tau = m*tau0; 0 = malo clenu (< 10).
 * HDEV, ne ADEV: linearni drift OCXO (starnuti, pomala teplota) ADEV zvedne umerne tau, ale PLL 2. radu ho
 * sleduje bez chyby kmitoctu — pro volbu tau smycky neni sumem. Nocni beh 2026-10-17: ADEV kodu DAC rostla
 * ~tau (drift ~5 ppb/den) a "prusecik" by vychazel jen z driftu. HDEV je vuci linearnimu driftu imunni. */
static double gp_hdev_phase(const float *x, int n, int m, double tau0)
{
    if (n - 3 * m < 10) return 0.0;
    double s = 0.0;
    for (int i = 0; i + 3 * m < n; i++) {
        double d = (double)x[i + 3 * m] - 3.0 * (double)x[i + 2 * m] + 3.0 * (double)x[i + m] - (double)x[i];
        s += d * d;
    }
    double t = (double)m * tau0;
    return sqrt(s / (6.0 * (double)(n - 3 * m) * t * t));
}

/* x = faze OCXO vs GPS [s] po tau0, tau_loop = aktualni tau smycky. Vysledek viz gpsdo_tau_adv_t (gpsdo.h). */
static void gp_tau_advise_core(const float *x, int n, double tau0, double tau_loop, gpsdo_tau_adv_t *o)
{
    memset(o, 0, sizeof *o);
    o->n = (uint16_t)n;
    if (n < 180) { o->bound = 2; return; }                     /* < 30 min LOCK: malo dat */
    for (unsigned k = 0; k < GPSDO_ADV_NT; k++) o->sx[k] = (float)gp_hdev_phase(x, n, GP_ADV_M[k], tau0);
    if (o->sx[0] <= 0.0f) { o->bound = 2; return; }
    o->ok = 1;
    double a = (double)o->sx[0] * (GP_ADV_M[0] * tau0);         /* bily PM: HDEV = a / tau */
    o->pps10 = (float)(a / 10.0);
    /* Pomer r = HDEV faze / (a/tau). Prebytek nad bilym PM 1PPS (OCXO NEBO putovani casu GPS) se rovna
     * bilemu PM prave pri r = sqrt(2). Hodnoti se jen tau <= tau_loop/3 (vys smycka fazi drzi). Prusecik
     * se interpoluje v log-log mezi sousednimi body mrizky (pevny bod skakal mezi okny 100..2000 s). */
    double rec = 0.0, last = 0.0, pt = 0.0, pr = 0.0;
    const double thr = 1.41421356;
    uint8_t bnd = 1u;                                           /* zadny prebytek: jen dolni mez */
    for (unsigned k = 1; k < GPSDO_ADV_NT && rec == 0.0; k++) {
        double t = GP_ADV_M[k] * tau0;
        if (t > tau_loop / 3.0 || o->sx[k] <= 0.0f) break;
        double r = (double)o->sx[k] * t / a;
        if (r >= thr) {
            bnd = 0u;
            if (pr > 0.0 && pr < thr && r > pr)
                rec = exp(log(pt) + (log(thr) - log(pr)) / (log(r) - log(pr)) * (log(t) - log(pt)));
            else
                rec = t;
        }
        pt = t; pr = r; last = t;
    }
    o->bound = bnd;
    if (rec == 0.0) rec = last;
    if (rec > 0.0 && rec < 100.0) rec = 100.0;                  /* meze gpsdo_set_tau */
    if (rec > 20000.0) rec = 20000.0;
    o->tau_rec = (float)rec;
}

void gpsdo_tau_advise(gpsdo_tau_adv_t *out)
{
    static float s_x[GPSDO_HIST_N];                             /* 2,9 kB, ne na stack */
    static gpsdo_tau_adv_t s_last;
    static uint32_t s_last_ms;
    static volatile uint8_t s_busy;
    uint32_t now = HAL_GetTick();
    if (out == NULL) return;
    taskENTER_CRITICAL();
    int mine = !s_busy && (s_last_ms == 0u || (uint32_t)(now - s_last_ms) >= GPSDO_HIST_PERIOD_S * 1000u);
    if (mine) s_busy = 1u;
    taskEXIT_CRITICAL();
    if (mine) {                                                 /* volaji UiTask i UartTask: pocita jen jeden */
        uint16_t cnt = gpsdo_hist_count();
        int n = 0;
        gpsdo_hist_pt_t p;
        for (int i = (int)cnt - 1; i >= 0; i--) {               /* posledni souvisly usek LOCK (od nejnovejsiho) */
            if (!gpsdo_hist_at((uint16_t)i, &p) || p.state != GPSDO_LOCK) break;
            n++;
        }
        /* F-0241: po zmene tau jen body NOVE smycky (fazi starsich tvarovala jina smycka). 0 = tau se nemenilo. */
        uint32_t tch = s_g_tau_ms;
        if (tch != 0u) {
            uint32_t since = (uint32_t)(now - tch) / (GPSDO_HIST_PERIOD_S * 1000u);
            if ((uint32_t)n > since) n = (int)since;
        }
        gpsdo_status_t st;
        gpsdo_get(&st);
        for (int k = 0; k < n; k++) {
            gpsdo_hist_at((uint16_t)(cnt - n + k), &p);
            s_x[k] = (float)p.x_ps * 1e-12f;
        }
        gpsdo_tau_adv_t r;
        gp_tau_advise_core(s_x, n, (double)GPSDO_HIST_PERIOD_S, st.tau_s, &r);
        taskENTER_CRITICAL();
        s_last = r; s_last_ms = now ? now : 1u; s_busy = 0u;
        taskEXIT_CRITICAL();
    }
    taskENTER_CRITICAL();
    *out = s_last;                                              /* kopie pod zamkem: druhy volajici muze prave zapisovat */
    taskEXIT_CRITICAL();
}

static void g_set_state(uint8_t st, uint8_t why)
{
    if (st != s_g_state) s_g_state_ms = HAL_GetTick();
    s_g_state = st; s_g_why = why;
}

/* Oprava sawtooth pro hranu, ktera byla poprve videt v `first_ms`: posledni TIM-TP, ktere prislo aspon
 * GPSDO_TP_GUARD_MS PRED tim a ne drive nez 1,5 s. Kazde TIM-TP smi patrit jen jedne hrane. */
static int saw_lookup(uint32_t first_ms, double *q_ns, uint32_t *rec_n)
{
    gps_timtp_t a, b;
    if (!gps_timtp_get(&a, &b)) return 0;
    const gps_timtp_t *r[2] = { &a, &b };
    for (int i = 0; i < 2; i++) {
        int32_t lead = (int32_t)(first_ms - r[i]->rx_ms);
        if (r[i]->n != 0u && lead >= (int32_t)GPSDO_TP_GUARD_MS && lead < 1500) {
            *q_ns = (double)r[i]->q_ps * 1e-3; *rec_n = r[i]->n; return 1;
        }
    }
    return 0;
}

/* Rozhodnuti o oprave: rozptyl druhych diferenci faze (= rozdil po sobe jdoucich chyb periody) bez opravy
 * a s obema znamenky. Oprava se zapne jen kdyz rozptyl snizi aspon o 25 % -- jinak (spatne prirazeni, modul
 * TIM-TP neposila, sum jinde) by mohla pridat sum misto ubrat. */
static void saw_account(double e_ns, int dq_ok, double dq)
{
    if (!dq_ok) { s_e_prev_ok = 0u; return; }
    double e[3] = { e_ns, e_ns - dq, e_ns + dq };
    if (s_e_prev_ok) {
        for (int i = 0; i < 3; i++) { double d = e[i] - s_e_prev[i]; s_sv[i] += d * d; }
        if (++s_sv_n >= GPSDO_SAW_W) {
            for (int i = 0; i < 3; i++) s_saw_var[i] = (float)(s_sv[i] / (double)s_sv_n);
            int best = (s_sv[1] < s_sv[2]) ? 1 : 2;
            s_saw = (s_sv[best] < 0.75 * s_sv[0]) ? (uint8_t)((best == 1) ? GPSDO_SAW_PLUS : GPSDO_SAW_MINUS)
                                                   : (uint8_t)GPSDO_SAW_OFF;
            s_saw_dec++;
            s_sv[0] = s_sv[1] = s_sv[2] = 0.0; s_sv_n = 0u;
        }
    }
    for (int i = 0; i < 3; i++) s_e_prev[i] = e[i];
    s_e_prev_ok = 1u;
}

static double g_start_code(void)
{
    if (g_dac.present && g_dac.code_valid) return (double)g_dac.code;
    return (double)g_dac_persist;
}

void gpsdo_tick(void)
{
    uint32_t now = HAL_GetTick();
    int sim = fpga_sim_active();
    int dac_ok = sim || (g_dac.present && g_dac.ctrl_ok);
    /* Zmena zdroje 1PPS (emulator <-> FPGA): virtualni povel nesmi pokracovat na skutecnem DAC ani naopak. */
    static uint8_t s_sim_prev;
    if ((uint8_t)sim != s_sim_prev) {
        s_sim_prev = (uint8_t)sim;
        s_g_have_prev = 0u; s_g_virt = 0u;
        if (s_g_state != GPSDO_OFF) g_set_state(GPSDO_WAIT, GPSDO_WHY_NOPPS);
    }

    if (!s_g_en) {
        if (s_g_state != GPSDO_OFF || s_g_why == GPSDO_WHY_NONE) g_set_state(GPSDO_OFF, (s_g_why == GPSDO_WHY_DIVERGE) ? GPSDO_WHY_DIVERGE : GPSDO_WHY_USER);
        if (!s_mon_on) { s_mon_on = 1u; s_mon_x = 0.0; s_g_have_prev = 0u; }   /* monitor navazuje na intervaly od teto chvile */
    } else {
        s_mon_on = 0u;
        if (s_g_state == GPSDO_OFF) g_set_state(GPSDO_WAIT, GPSDO_WHY_NOPPS);
    }

    fpga_pps_t p;
    int have = fpga_pps_get(&p);
    if (have && p.n != s_g_pps_n) {                            /* i v OFF: fazovy monitor (s_mon_x) */
        s_g_pps_n = p.n;
        /* v0.40.2 (STATUS #337b): PRVNI hrana, kterou STM po svem startu uvidi -- PSEQ, cas od startu FPGA (tick bezi od
         * konfigurace, ts = takty mod 2^32 = do 42,9 s) a odstup k dalsi hrane. Falesna hrana pri konfiguraci FPGA ma PSEQ 1
         * a cas ~0 ms; STM ji ale vidi jen tehdy, kdyz zacne cist drive nez prijde dalsi hrana (jinak nema vliv). */
        if (p.clk_mhz != 0u) {
            if (s_st_first_seq == 0x100u) {
                s_st_first_seq = p.seq; s_st_first_us = p.ts / p.clk_mhz; s_st_ts1 = p.ts; s_st_have1 = 1u;
            } else if (s_st_have1) {
                s_st_next_us = (p.ts - s_st_ts1) / p.clk_mhz;   /* takty mod 2^32 */
                s_st_have1 = 0u;
            }
        }
        /* interval PSEQ 1 -> 2 (PSEQ 1 po 0 je pretoceni mod 256, ne start), odchylka od mrizky 100 ms (10 Hz bez fixu
         * i 1PPS): > 1 ms = prvni hrana nebyla hrana GPS */
        if (p.seq == 1u && s_st_prev_seq != 0u) {
            s_st_ts12 = p.ts; s_st_have12 = 1u;
        } else if (p.seq == 2u && s_st_have12 && s_st_prev_seq == 1u && p.clk_mhz != 0u) {
            uint32_t us = (p.ts - s_st_ts12) / p.clk_mhz;
            int32_t dev = (int32_t)(us % 100000u);
            if (dev > 50000) dev -= 100000;
            s_st_n++; s_st_dev = dev;
            if (dev > 1000 || dev < -1000) s_st_bad++;
            s_st_have12 = 0u;
        } else {
            s_st_have12 = 0u;
        }
        s_st_prev_seq = p.seq;
        gps_data_t g;
        gps_get(&g);
        int fix = sim || (g.valid && g.fix_mode >= 2);
        uint8_t why = (!dac_ok && s_g_en) ? GPSDO_WHY_NODAC : p.ref_ext ? GPSDO_WHY_REFEXT : p.clk_bad ? GPSDO_WHY_CLK
                    : !fix ? GPSDO_WHY_NOFIX : GPSDO_WHY_NONE;
        if (why != GPSDO_WHY_NONE) {
            s_g_have_prev = 0u; s_q_have = 0u; s_e_prev_ok = 0u; s_mon_x = 0.0;
            s_pm_have = 0u;                                    /* audit 2026-10-20: souvislost odhadu sumu */
            if (s_g_state == GPSDO_WAIT) s_g_why = why;
        } else if (!s_g_have_prev || p.fine != s_g_fine_prev || p.epoch != s_g_ep_prev) {
            /* v0.37.0: novy zacatek i pri zmene osy casu 1PPS (jemny <-> hruby, nova epocha PLL ve FPGA): interval pres
             * zmenu osy by nesl posun az ~3 takty (latence + faze PLL) */
            s_g_ts_prev = p.ts; s_g_have_prev = 1u;
            s_g_frac_prev = p.frac; s_g_fine_prev = p.fine; s_g_ep_prev = p.epoch;
            double q; uint32_t rn;
            if (saw_lookup(p.first_ms, &q, &rn)) { s_q_prev = q; s_q_rec_n = rn; s_q_have = 1u; } else s_q_have = 0u;
            s_e_prev_ok = 0u;
        } else {
            uint32_t dticks = p.ts - s_g_ts_prev;              /* mod 2^32 */
            const double dfrac = ((double)p.frac - (double)s_g_frac_prev) / 16384.0;   /* v0.37.0: jemny cas */
            s_g_ts_prev = p.ts; s_g_frac_prev = p.frac;
            double F = (double)p.clk_mhz * 1e6;
            uint32_t ns = (uint32_t)((double)dticks / F + 0.5);
            double e_t = (double)dticks + dfrac - (double)ns * F;
            /* oprava sawtooth teto hrany (nejdriv precist, at se drzi navaznost i pri odmitnuti) */
            double q = 0.0; uint32_t rn = 0u;
            int q_ok = saw_lookup(p.first_ms, &q, &rn) && (rn != s_q_rec_n || !s_q_have);
            int dq_ok = q_ok && s_q_have && ns == 1u;
            double dq = dq_ok ? (q - s_q_prev) : 0.0;
            if (q_ok) { s_q_prev = q; s_q_rec_n = rn; s_q_have = 1u; } else s_q_have = 0u;

            if (ns < 1u || ns > 40u || fabs(e_t) > GPSDO_Y_MAX * (double)ns * F) {
                s_g_rej++; s_e_prev_ok = 0u; s_mon_x = 0.0;    /* 10 Hz bez fixu dava ns = 0 */
                s_pm_have = 0u;
            } else {
                if (ns > 1u) s_g_gap++;
                double e_ns = e_t * 1000.0 / (double)p.clk_mhz;
                saw_account(e_ns, dq_ok, dq);
                double e_use = e_ns;
                if (dq_ok && s_saw == GPSDO_SAW_PLUS)  e_use = e_ns - dq;
                if (dq_ok && s_saw == GPSDO_SAW_MINUS) e_use = e_ns + dq;
                if (ns == 1u) gp_pm_update(e_use, p.clk_mhz); else s_pm_have = 0u;   /* audit 2026-10-20: odhad sumu 1PPS */

                if (!s_g_en) {                                 /* OFF: jen soucet faze, nic se neridi */
                    s_mon_x += e_use; s_g_ok++;
                } else if (s_g_state == GPSDO_WAIT) {          /* prvni pouzitelny interval -> rozbeh */
                    memset(&s_gc, 0, sizeof s_gc);
                    s_gc.kdac = s_g_kdac; s_gc.k0 = s_g_kdac; s_g_kset = 0u;
                    s_gc.code_max = (double)ad5693_mv_to_code(AD5693_RESTORE_MAX_MV);
                    s_gc.code = gp_clamp(g_start_code(), 0.0, s_gc.code_max);
                    core_start_fll(&s_gc);
                    g_set_state(GPSDO_FLL, GPSDO_WHY_NONE);
                } else if (s_g_state == GPSDO_HOLD) {          /* navrat 1PPS po holdoveru */
                    if (s_g_hold_from == GPSDO_LOCK) {
                        /* nova fazova kotva; integrator (= zbytkovy kmitocet) zustava, kod se nepohne */
                        s_gc.code_lock = s_gc.code + s_gc.integ / s_gc.kdac;
                        s_gc.x = 0.0; s_gc.xf = 0.0; s_gc.good = 0u; s_gc.bad_run = 0u;
                        s_gc.state = GPSDO_LOCK;
                    } else {
                        core_start_fll(&s_gc);
                    }
                    g_set_state(s_gc.state, GPSDO_WHY_NONE);
                }
                if (s_g_en) {
                    /* K se prepisuje jen novou hodnotou od uzivatele -- jinak by se zahodil odhad z FLL */
                    if (s_g_kset) { s_gc.kdac = s_g_kdac; s_gc.k0 = s_g_kdac; s_g_kset = 0u; }
                    s_gc.tau = s_g_tau;
                    s_gc.pm_ns = s_pm_ns;                      /* audit 2026-10-20: zmereny sum 1PPS do FLL */
                    if (core_step(&s_gc, e_use, ns)) { s_g_ok++; s_g_last_ok_ms = now; } else s_g_rej++;
                    if (s_gc.diverged) {
                        s_g_en = 0u; g_set_state(GPSDO_OFF, GPSDO_WHY_DIVERGE);
                    } else if (s_gc.state != s_g_state && (s_g_state == GPSDO_FLL || s_g_state == GPSDO_LOCK)) {
                        g_set_state(s_gc.state, GPSDO_WHY_NONE);
                    }
                }
            }
        }
    }

    /* Bez prijateho 1PPS -> holdover (drzi posledni kod). */
    if ((s_g_state == GPSDO_FLL || s_g_state == GPSDO_LOCK) && (uint32_t)(now - s_g_last_ok_ms) > GPSDO_HOLD_MS) {
        s_g_hold_from = s_g_state;
        g_set_state(GPSDO_HOLD, GPSDO_WHY_NONE);
    }
    if (s_g_en && s_g_state == GPSDO_WAIT && !have) s_g_why = GPSDO_WHY_NOPPS;

    /* Vystup pro SensorsTask. DAC se ridi jen v aktivnich stavech; WAIT/OFF ho nechava byt.
     * 🔴 S emulatorem (`fpgasim`) se ridi jen VIRTUALNI DAC (`s_g_cmd_q16` -> gpsdo_sim_dac_code): emulovana
     * 1PPS by jinak skutecnym DAC rozladila fyzicky OCXO podle modelu, ktery s nim nema nic spolecneho. */
    int drive = (s_g_state == GPSDO_FLL || s_g_state == GPSDO_LOCK || s_g_state == GPSDO_HOLD) && dac_ok;
    if (drive) s_g_cmd_q16 = (uint32_t)(gp_clamp(s_gc.code, 0.0, 65535.0) * 65536.0 + 0.5);
    s_g_virt = (drive && sim) ? 1u : 0u;
    if (drive && !sim) g_gpsdo_code_q16 = s_g_cmd_q16;
    g_gpsdo_drive    = (drive && !sim) ? 1u : 0u;
    g_gpsdo_lockflag = (s_g_state == GPSDO_LOCK && s_gc.locked) ? 1u : 0u;

    gpsdo_status_t st;
    st.state = s_g_state; st.why = s_g_why; st.locked = g_gpsdo_lockflag; st.saw = s_saw;
    st.y = s_gc.yf; st.x_ns = s_gc.xf; st.code = s_gc.code;
    st.x_raw_ns = (s_g_state == GPSDO_LOCK) ? s_gc.x : ((s_g_state == GPSDO_OFF) ? s_mon_x : 0.0);   /* OFF: fazovy monitor */
    gp_hist_push(now);
    st.kdac = (s_gc.kdac > 0.0) ? s_gc.kdac : s_g_kdac;   /* odhad z FLL, jinak zadane */
    st.tau_s = s_g_tau;
    st.fll_win_s = s_gc.fll_win; st.state_s = (now - s_g_state_ms) / 1000u;
    st.pps_ok = s_g_ok; st.pps_rej = s_g_rej; st.pps_gap = s_g_gap;
    st.saw_var[0] = s_saw_var[0]; st.saw_var[1] = s_saw_var[1]; st.saw_var[2] = s_saw_var[2]; st.saw_n = s_saw_dec;
    st.pm_ns = s_pm_ns;
    st.st_n = s_st_n; st.st_bad = s_st_bad; st.st_dev_us = s_st_dev;
    st.st_first_seq = s_st_first_seq; st.st_first_us = s_st_first_us; st.st_next_us = s_st_next_us;
    taskENTER_CRITICAL(); s_pub = st; taskEXIT_CRITICAL();
}

void gpsdo_get(gpsdo_status_t *out)
{
    taskENTER_CRITICAL(); *out = s_pub; taskEXIT_CRITICAL();
}

/* Nejistota reference [ppb] podle stavu smycky z ZMERENYCH hodnot (FLL: |y| okna, LOCK: ADEV faze, HOLD: ADEV + drift kodu); kdyz mereni
 * chybi, nahradni tabulka. Jedno misto pravdy pro hlavni cislo, okno ANALYZA i web (IPC `ref_ppb_x100`). Smi volat kdokoli. */
double gpsdo_ref_ppb(uint8_t *measured)
{
    gpsdo_status_t st;
    gpsdo_get(&st);
    mp_ref_in_t r;
    memset(&r, 0, sizeof r);
    r.state = st.state; r.why = st.why; r.locked = st.locked; r.state_s = st.state_s;
    r.y_abs_ppb = fabs(st.y) * 1e9; r.fll_win_s = st.fll_win_s;
    r.pm_ns = st.pm_ns;                                        /* audit 2026-10-20 */
    taskENTER_CRITICAL(); r.lock_u_ppb = s_ref_lock_u; r.hold_u0_ppb = s_ref_lock_u; r.hold_drift_ppb_h = s_ref_drift; taskEXIT_CRITICAL();
    return mp_ref_ppb(&r, measured);
}

void gpsdo_enable(int on)
{
    if (on) { if (!s_g_en) { s_gc.diverged = 0u; s_g_why = GPSDO_WHY_NONE; } s_g_en = 1u; }
    else    { s_g_en = 0u; s_g_why = GPSDO_WHY_USER; }
}
int gpsdo_enabled(void) { return s_g_en ? 1 : 0; }

void gpsdo_set_tau(uint32_t tau_s)
{
    if (tau_s < 100u) tau_s = 100u;
    if (tau_s > 20000u) tau_s = 20000u;
    if ((double)tau_s != s_g_tau) s_g_tau_ms = HAL_GetTick() | 1u;   /* F-0241: od ted plati nova smycka */
    s_g_tau = (double)tau_s;
}
void gpsdo_set_kdac(double k)
{
    if (k >= 1e-12 && k <= 1e-9) { s_g_kdac = k; s_g_kset = 1u; }
}

const char *gpsdo_state_name(uint8_t st)
{
    static const char *const n[] = { "OFF", "WAIT", "FLL", "LOCK", "HOLD" };
    return (st < 5u) ? n[st] : "?";
}
const char *gpsdo_why_text(uint8_t why)
{
    static const char *const t[] = { "", "DAC nenalezen", "FPGA nehlasi 1PPS (FW < 0x0424?)", "GPS bez fixu",
                                     "FPGA na externi referenci", "hodiny FPGA vadne",
                                     "FLL divergovala (znamenko/velikost K?)", "vypnuto uzivatelem" };
    return (why < 8u) ? t[why] : "?";
}

uint16_t gpsdo_sim_dac_code(void)
{
    if (s_g_virt) return (uint16_t)((s_g_cmd_q16 + 0x8000u) >> 16);   /* smycka ridi virtualni DAC emulatoru */
    if (g_dac.present && g_dac.code_valid) return g_dac.code;
    return g_dac_persist;
}

/* ── Selftest: regulator nad modelem OCXO (bez HW) ──────────────────────────────────────────────
 * Model: y = Y0 + Ktrue * kod, faze roste o y*1e9 ns/s, kazda hrana ma sum ~N(0, 30 ns) a kvantizaci
 * 10 ns. Ktrue je ZAMERNE o ~9 % jinak nez odhad regulatoru. Pozadavky: (1) do 4000 s LOCK, (2) za
 * dalsich 8000 s |y| < 2e-10 a |faze| < 300 ns, (3) se SPATNYM znamenkem K se FLL zastavi (divergence). */
static uint32_t s_st_rnd;
static double st_gauss(void)
{
    double s = 0.0;
    for (int i = 0; i < 3; i++) { s_st_rnd ^= s_st_rnd << 13; s_st_rnd ^= s_st_rnd >> 17; s_st_rnd ^= s_st_rnd << 5;
                                  s += (double)(s_st_rnd >> 8) / 16777216.0; }
    return (s - 1.5) * 2.0;
}

static int st_run(double kdac_est, double KT, uint32_t secs, uint32_t *lock_at, double *y_end, double *x_end, int *div)
{
    static gp_core_t c;                         /* static: ~170 B mimo zasobnik volajiciho */
    memset(&c, 0, sizeof c);
    c.code_max = 52429.0; c.kdac = kdac_est; c.k0 = fabs(kdac_est); c.tau = (double)GPSDO_TAU_DEFAULT; c.code = 0.0;
    core_start_fll(&c);
    const double Y0 = -8.4e-7;
    double ph = 0.0, t_prev = 0.0;
    *lock_at = 0u; *div = 0;
    double ysum = 0.0; uint32_t yn = 0u;
    for (uint32_t s = 1; s <= secs; s++) {
        double y = Y0 + KT * c.code;
        ph += y * 1e9;
        double t = floor((ph + st_gauss() * 30.0) / 10.0) * 10.0;   /* sum GPS + kvantizace 10 ns */
        if (s > 1u) (void)core_step(&c, t - t_prev, 1u);
        t_prev = t;
        if (c.diverged) { *div = 1; return 0; }
        if (c.state == GPSDO_LOCK && *lock_at == 0u) *lock_at = s;
        if (s > secs - 2000u) { ysum += y; yn++; }
    }
    *y_end = yn ? ysum / (double)yn : 1.0;
    *x_end = c.xf;
    return c.state == GPSDO_LOCK;
}

/* Doporuceni tau nad syntetickou fazi (720 bodu po 10 s = 2 h, smycka tau 1000 s):
 *  (1) jen bily PM 1PPS (sigma 5 ns) -> prusecik neni, jen dolni mez;
 *  (2) + bily FM OCXO 3e-10 na 10 s -> sum OCXO vystoupi nad 1PPS mezi 100 a 200 s;
 *  (3) 100 bodu (< 30 min) -> malo dat. */
static int st_tau_advise(void)
{
    static float x[GPSDO_HIST_N];
    gpsdo_tau_adv_t r;
    int ok = 1;
    for (int pass = 0; pass < 2; pass++) {
        s_st_rnd = 0xC0FFEEu + (uint32_t)pass;
        double rw = 0.0;
        for (unsigned i = 0; i < GPSDO_HIST_N; i++) {
            if (pass) rw += 3.0e-10 * 10.0 * st_gauss();     /* bily FM: krok faze = y * tau0 */
            x[i] = (float)(5e-9 * st_gauss() + rw);
        }
        /* ocekavani: (1) HDEV bileho PM pri 10 s ~ 9e-10, prebytek zadny -> dolni mez = posledni hodnocene
         * tau 200 s (<= 1000/3); (2) prusecik bileho FM 3e-10 a bileho PM vychazi ~95 s (namereno 114 s). */
        gp_tau_advise_core(x, (int)GPSDO_HIST_N, 10.0, 1000.0, &r);
        if (pass == 0) ok = ok && r.ok && r.bound == 1u && r.tau_rec == 200.0f && r.pps10 > 6e-10f && r.pps10 < 1.2e-9f;
        else           ok = ok && r.ok && r.bound == 0u && r.tau_rec >= 100.0f && r.tau_rec <= 200.0f;
    }
    gp_tau_advise_core(x, 100, 10.0, 1000.0, &r);
    ok = ok && !r.ok && r.bound == 2u;
    return ok;
}

int gpsdo_selftest(void)
{
    /* Skutecne K modelu ruzne od odhadu 3,4e-11 (0,5x .. 3,5x): FLL si ho zmeri a zamkne. */
    static const double KT[3] = { 3.1e-11, 1.7e-11, 1.2e-10 };
    uint32_t la; double ye, xe; int dv;
    int ok = 1;
    for (int i = 0; i < 3; i++) {
        s_st_rnd = 0x9E3779B9u + (uint32_t)i;
        int lk = st_run(GPSDO_KDAC_DEFAULT, KT[i], 12000u, &la, &ye, &xe, &dv);
        ok = ok && lk && la > 0u && la < 4000u && fabs(ye) < 2e-10 && fabs(xe) < 300.0 && !dv;
    }
    /* Spatne znamenko K a rozsah EFC, ktery nestaci (K 3,4x mensi): smycka se musi ZASTAVIT. */
    s_st_rnd = 0x12345u;
    (void)st_run(-GPSDO_KDAC_DEFAULT, 3.1e-11, 3000u, &la, &ye, &xe, &dv);
    ok = ok && dv;
    s_st_rnd = 0x54321u;
    (void)st_run(GPSDO_KDAC_DEFAULT, 1.0e-11, 3000u, &la, &ye, &xe, &dv);
    ok = ok && dv;
    ok = ok && st_tau_advise();
    /* Zmerena nejistota reference: syntetika. Drift kodu 0,76 LSB/h (K 3,4e-11) = 0,0258 ppb/h; bily PM ±5 ns (σ 2,9 ns) -> ADEV @ 1000 s ~ 0,005 ppb. */
    {   static int32_t  tx[360];          /* audit 2026-10-20: faze v ps (driv int16 ns) */
        static uint32_t tc[360];
        uint32_t rnd = 0x1234ABCDu;
        for (int i = 0; i < 360; i++) {
            rnd = rnd * 1664525u + 1013904223u;
            tx[i] = (int32_t)(((int)((rnd >> 16) % 11u) - 5) * 1000);   /* +-5 ns jako drive */
            tc[i] = (uint32_t)((24920.0 + 0.76 / 3600.0 * 10.0 * (double)i) * 65536.0 + 0.5);
        }
        double u, d;
        int m = gp_ref_core(tx, tc, 360, 3.4e-11, &u, &d);
        ok = ok && m && u > 0.003 && u < 0.009 && d > 0.0255 && d < 0.0262;
        m = gp_ref_core(tx, tc, 100, 3.4e-11, &u, &d);                       /* kratky usek: ADEV na mensim tau, drift NEzmeren (< 30 min) */
        ok = ok && m && d == 0.0;
        m = gp_ref_core(tx, tc, 15, 3.4e-11, &u, &d);                        /* skoro nic: nezmereno */
        ok = ok && !m && u == 0.0 && d == 0.0;
    }
    return ok;
}
