/**
 * @file    fpga_freq.c
 * @brief   SPI2 master driver pro FPGA citac kmitoctu. Viz fpga_freq.h.
 */
#include "fpga_freq.h"
#include "gpio_guard.h"   /* gpio_cfg_lock — PB12 sousedi s PB13 = ETH_TXD1 */
#include "stm32h7xx_hal.h"
#include "i2c.h"          /* hi2c1.TIMINGR do diagnostiky */
#include "sensor_stat.h"  /* g_sensors[].err_total/samples do diagnostiky I2C1 */
#include "cmsis_os2.h"   /* SPI2 mutex: FpgaTask poll vs UART fpgaraw/fpgaloop */
#include "gpsdo.h"       /* gpsdo_sim_dac_code — emulator 1PPS ladi OCXO podle kodu DAC */
#include "fpga_phrec.h"  /* audit 2026-10-20 (E1): fazovy zaznam z teziste segmentu, kmitocet centroid-to-centroid */
#include "fpga_tdccal.h" /* FW 0x0427: kalibracni tabulka TDC z histogramu (vypocet v STM) */
#include "watchdog.h"    /* watchdog_kick_fpga — behem kalibrace TDC (~0,6 s) */
#include "fpga_crc.h"    /* optimalizace 6: kontrola CRC prijateho ramce (SW tabulka, volitelne HW jednotka FPGA_CRC_USE_HW) */
#include "fpga_v3.h"     /* protokol v3 (krok 4c): ramec 1024 B, prijem zaznamu, okna, emulator */
/* Optimalizace 6: datova faze SPI pres DMA. 0 = blokujici HAL_SPI_TransmitReceive jako dosud. 1 az po konfiguraci CubeMX
 * (DMA streamy SPI2, NVIC, sekce FPGA_DMA_SECTION, callbacky) -- 2026-10-20_CRC_DMA_zadani_CLI.md. */
#ifndef FPGA_SPI_USE_DMA
#define FPGA_SPI_USE_DMA 1   /* 2026-10-22 (krok 7): prelozeno; DMA se pouzije az po `spi dma on` (fpga_spi_dma_enable) */
#endif
#if FPGA_SPI_USE_DMA
#include "fpga_spi_dma.h"
#endif

/* Audit 2026-10-20 (E1): 1 = delka okna pro VSECHNY dalsi vypocty (statistika, datalog, IPC) je gate_cc_ps (kmitocet mezi
 * tezisti sousednich oken; Σcykly/Σhradla pak teleskopuje -> spravna ADEV pro bily PM pristroje). 0 = dnesni chovani
 * (N / f_regr v okne). Prepnout na 1 az po overeni E1 offline (dokument auditu, kap. E1). */
#ifndef FPGA_STATS_USE_CC
#define FPGA_STATS_USE_CC 0
#endif
#include <string.h>
#include <stdio.h>
#include <math.h>     /* fabs v selftestu */

extern SPI_HandleTypeDef hspi2;

/* === CS pin (PB12, drive manualni GPIO, active-low) === */
#define FPGA_CS_GPIO_Port   GPIOB
#define FPGA_CS_Pin         GPIO_PIN_12

/* === SPI rychlost ===
 * Kontrakt FPGA (GW1NR-9, oversampling SCK na 100 MHz): cil <= 6 MHz,
 * absolutni maximum ~10 MHz. NEPREKRACOVAT. */
#define FPGA_SCK_TARGET_HZ  12500000u  /* 2026-10-18 (zadani uzivatele „10 MHz“): kernel SPI123 = PLL2P = 200 MHz -> stupne 100/50/25/12,5/6,25/3,125 MHz;
 *   10 MHz neexistuje, prvni stupen >= 10 je 12,5 MHz (/16), pod nim 6,25. Drive 5 MHz (-> 3,125 MHz). Ramec 128 B: 328 us -> 82 us. Skutecny takt = kernel/2^n,
 *   viz "fpga: SPI2 kernel=.. SCK ~.." v boot logu. ⚠️ Vyzaduje bitstream s PHY na 100 MHz (7affd41);
 *   starsi PHY na 10 MHz zvlada jen ~2 MHz. Pri NOLINK vratit 1000000u. */
#define FPGA_SCK_MAX_HZ     40000000u  /* tvrdy strop: FPGA PHY od FW 0x040C bere SCK primo (STA ~40 MHz, #264); drive 10 MHz (oversampling) */
/* Rychlost vystupu SCK/MOSI (OSPEEDR, 0 LOW .. 3 VERY_HIGH). MEDIUM = nejnizsi stupen, ktery by mel stacit na SCK 25 MHz
 * (LOW je podle datasheetu STM32H7 urceny radove do ~12-16 MHz podle zateze -- HYPOTEZA z pameti, overit v tabulce
 * „I/O AC characteristics" a osciloskopem na J3); vyssi stupen = strmejsi hrany = vic ruseni do casovacich vstupu FPGA. */
#ifndef FPGA_SPI_PIN_SPEED
#define FPGA_SPI_PIN_SPEED  GPIO_SPEED_FREQ_MEDIUM
#endif
#if FPGA_SCK_TARGET_HZ > FPGA_SCK_MAX_HZ
#error "FPGA_SCK_TARGET_HZ prekracuje povolene maximum FPGA slave (~40 MHz)"
#endif

/* === Frame (v2, FPGA_PROTOCOL_V2_NAVRH.md) === */
#define FR_LEN        FPGA_FRAME_LEN   /* 128 B — jediny zdroj pravdy v fpga_freq.h (L-0012) */
#define FR_MAGIC      0xA5
#define FR_VERSION    0x02
#define FR_PAYLOAD    12               /* offset payloadu (beze zmeny proti v1) */
#define FR_PAYLOAD_MAX 114             /* v2: max payload (bylo 50 u v1) */
#define FR_CRC_LEN    126              /* CRC pokryva byte 0..125 (bylo 0..61 u v1) */

/* TYPE */
#define TYPE_SET_CONFIG 0x01
#define TYPE_ACK      0x06
#define TYPE_START    0x08
#define TYPE_STOP     0x09
#define TYPE_DATA     0x80
#define TYPE_CAL      0xA0             /* zadost o / odpoved s CAL reportem (TDC) */

/* STATUS/FLAGS bity */
#define ST_DATA_VALID    (1u << 0)
#define ST_DATA_FRESH    (1u << 1)
#define ST_FIFO_EMPTY    (1u << 2)
#define ST_FIFO_OVERFLOW (1u << 3)
#define ST_BUSY          (1u << 4)
#define ST_RX_CRC_ERROR  (1u << 5)
#define ST_ACK_OK        (1u << 6)
#define ST_ERROR         (1u << 7)

static uint32_t g_last_seq = 0xFFFFFFFFu;  /* posledni potvrzena seq */
/* F-0193: souvislost SEQUENCE (pise jen FpgaTask v `fpga_freq_poll`; `status`
 * cte 32bitove hodnoty bez zamku — jednotlive jsou atomicke). */
static uint32_t s_poll_gap   = 0u;         /* dira pred poslednim vracenym merenim */
/* 2026-10-06: okna s chybnym POCTEM hran (`fpga_freq_miscount`). Takove okno se
 * NEZAPISE do `s_last`, takze ho neuvidi zadny konzument (IPC/web, SCPI, okna UI),
 * a FpgaTask ho dostane s priznakem, aby ho vyradil i ze statistiky. */
static uint64_t s_mc_ref     = 0u;         /* µHz posledniho prijateho okna (reference) */
static uint32_t s_mc_run     = 0u;         /* kolik oken za sebou vyrazeno (pojistka) */
static uint8_t  s_prev_chan  = 0xFFu;      /* kanal predchoziho ramce (FW >= 0x041E) — zmena nuluje referenci miscountu */
static int      s_poll_mc    = 0;          /* miscount posledniho vraceneho mereni (k) */
static uint32_t s_mc_pos     = 0u, s_mc_neg = 0u;   /* soucty od bootu pro `status` */
static uint32_t s_seq_gaps   = 0u;         /* kolikrat byla dira */
static uint32_t s_seq_missed = 0u;         /* kolik mereni celkem chybelo */
static uint32_t s_seq_resync = 0u;         /* skoky/navraty (reset FPGA, start emulace) */
/* 2026-10-10 (STATUS #283): samokontrola okna (`fpga_freq_window_check`, FW 0x041F caps bit10). Zamitnute
 * okno se jako miscount nezapise do `s_last` a FpgaTask ho vyradi; soucty jdou do `status`. */
static int      s_poll_chk   = FPGA_CHK_OK;   /* vysledek posledniho vraceneho mereni */
static uint32_t s_chk_count  = 0u, s_chk_clock = 0u, s_chk_window = 0u;
/* posledni zamitnuti pocitanim / delkou okna (2026-10-21) a pocet zamitnuti po rozbehu (> FPGA_CHK_STARTUP_MEAS mereni) */
#define FPGA_CHK_STARTUP_MEAS 40u
static uint32_t s_chk_last_ms = 0u, s_chk_last_seq = 0u, s_chk_last_nth = 0u, s_chk_late = 0u;
static int8_t   s_chk_last_ccd = 0;
static uint16_t s_prev_loss  = 0xFFFFu;    /* clk_loss predchoziho ramce (0xFFFF = zatim zadny) */
static uint16_t s_prev_coh   = 0xFFFFu;    /* FW 0x0425: coh_cnt predchoziho ramce */
static uint32_t s_chk_fmax   = 0u, s_chk_coh = 0u, s_chk_fmax_b = 0u;
static uint8_t  s_phrec_selftest_ok = 0u;   /* audit 2026-10-20: vysledek fpga_phrec_selftest pri startu */
uint8_t fpga_freq_phrec_selftest_ok(void) { return s_phrec_selftest_ok; }   /* FW 0x0425: odmitnuta okna nad mezi primeho mereni / chyba koherence */
static uint8_t  s_prev_win   = 0xFFu;      /* echo hradla predchoziho ramce: okno pres zmenu hradla se neposuzuje */
static uint32_t s_dbg_frames = 0u, s_dbg_new = 0u, s_dbg_valid = 0u;   /* v22: platne DATA ramce / nova mereni / ramce s DATA_VALID */
static uint32_t s_rej_seq    = 0u;         /* SEQUENCE naposledy odmitnuteho mereni ... */
static uint8_t  s_rej_on     = 0u;         /* ... platna (1) do prijeti dalsiho mereni */
static uint32_t g_sck_hz   = 0;
static uint8_t  s_link_ok  = 0;            /* 1 = posledni poll dostal platny ramec (MAGIC+CRC) */
static uint32_t g_rx_crc   = 0;            /* pocet ramcu se spatnym CRC */
static uint32_t g_rx_crc_last_ms = 0;      /* HAL_GetTick() pri posledni CRC chybe (0=zadna) -> "uptime od posledni" */
static uint8_t  g_init_ok  = 0;            /* 1 = init+selftest OK, smime komunikovat */
static uint8_t  g_xfer_ok  = 0;            /* 1 = posledni HAL_SPI prenos vratil HAL_OK */
static uint8_t  g_last_rx0 = 0;            /* prvni prijaty bajt z MISO (bring-up diag) */
static fpga_meas_t s_last;                 /* posledni naparsovany DATA ramec (i nefresh/invalid) */
static uint8_t  s_last_seen = 0;           /* 1 = aspon jeden DATA ramec dorazil */
static fpga_meas_t s_frame;                /* posledni platny DATA ramec VCETNE odmitnutych mereni (jen diagnostika) */
static uint8_t  s_frame_seen = 0;
static uint8_t  s_proto  = 0u;             /* protokol v3 (krok 4c): 0 = verze FPGA nezjistena, 2 = v2 (128 B), 3 = v3 (1024 B) */
static uint8_t  s_sim_v3 = 0u;             /* fpgasim v3: zdroj ramcu = emulator FPGA v3 (fpga_v3_emu) */

/* === Little-endian cteni === */
static uint64_t rd_le64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}
static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd_le48(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 6; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}
/* dt v jednotkach T_clk/16384 -> ps, zaokrouhleno. Jednotka = 1e6/(16384*clk_mhz) ps = 15625/(256*clk_mhz) ps:
 * 100 MHz 0,6103515625 ps (625/1024), 125 MHz 0,48828125 ps (125/256). u < 2^48, nasobek < 2^62: vejde se do uint64.
 * clk_mhz = 0 (poskozeny ramec) se bere jako 100 MHz (stary FW), at se nedeli nulou. */
static uint64_t dt_units_to_ps(uint64_t u, uint32_t clk_mhz)
{
    const uint64_t den = 256ull * (uint64_t)(clk_mhz ? clk_mhz : 100u);
    return (u * 15625ull + den / 2u) / den;
}
/* f [x1e5 Hz] = edges * 1e17 / gate_ps (double: vysledek <= 1,4e14 < 2^53) */
static uint64_t freq_x1e5_from(uint64_t edges, uint64_t gate_ps)
{
    if (edges == 0u || gate_ps == 0u) return 0u;
    return (uint64_t)((double)edges * 1e17 / (double)gate_ps + 0.5);
}

/* === CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, xorout 0) === */
/* Optimalizace 5 (2026-10-20): CRC-16/CCITT-FALSE pres pulbajtovou tabulku (16 polozek, 32 B flash). Bez vetveni ve smycce;
 * na PC 1,6x rychlejsi nez bitova smycka, na CM7 odhadem 2-3x (pro ramec v3 1024 B desitky us -> jednotky az desitky us).
 * Nejlepsi pro v3: HW jednotka CRC STM32H7 (polynom 0x1021, init 0xFFFF, bez reflexe = CCITT-FALSE) -- 0 % CPU.
 * Vysledek je bit po bitu stejny (crc16_ccitt_ref; overeno v selftestu i na hostu na nahodnych datech 1..1024 B). */
static const uint16_t k_crc_nib[16] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF };
static uint16_t crc16_ccitt(const uint8_t *d, int n)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < n; i++) {
        crc = (uint16_t)((crc << 4) ^ k_crc_nib[(crc >> 12) ^ (d[i] >> 4)]);
        crc = (uint16_t)((crc << 4) ^ k_crc_nib[(crc >> 12) ^ (d[i] & 0x0Fu)]);
    }
    return crc;
}
static uint16_t crc16_ccitt_ref(const uint8_t *d, int n)   /* bitova reference (selftest) */
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < n; i++) {
        crc ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* === Sestaveni TX ramce === */
static void build_frame(uint8_t type, uint32_t seq, const uint8_t *payload, uint16_t plen, uint8_t *f)
{
    if (plen > FR_PAYLOAD_MAX) plen = FR_PAYLOAD_MAX;  /* klamp PRED zapisem PAYLOAD_LEN */
    memset(f, 0, FR_LEN);
    f[0] = FR_MAGIC;
    f[1] = FR_VERSION;
    f[2] = type;
    f[3] = 0;                      /* FLAGS (STM32 -> FPGA): 0 */
    f[4] = (uint8_t)(seq);
    f[5] = (uint8_t)(seq >> 8);
    f[6] = (uint8_t)(seq >> 16);
    f[7] = (uint8_t)(seq >> 24);
    f[8] = (uint8_t)(plen);
    f[9] = (uint8_t)(plen >> 8);
    /* f[10..11] RESERVED = 0 */
    if (payload && plen) memcpy(&f[FR_PAYLOAD], payload, plen);
    uint16_t crc = crc16_ccitt(f, FR_CRC_LEN);
    f[126] = (uint8_t)(crc);        /* low byte */
    f[127] = (uint8_t)(crc >> 8);   /* high byte */
}

/* === Presne us prodlevy pres DWT cyklovy citac (M7 @ SystemCoreClock).
 * NOP-busy-wait byl nespolehlivy napric -O0/-O2; DWT je deterministicky. === */
static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
}
static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000u);
    while ((uint32_t)(DWT->CYCCNT - start) < ticks) { __NOP(); }
}

/* Prodlevy dle kontraktu: CS setup/hold >=1 us (davame 2), mezi ramci >=20 us
 * (FPGA potrebuje ~124 cyklu @10 MHz na slozeni noveho ramce; davame 25). */
#define FPGA_CS_GAP_US     2u
#define FPGA_FRAME_GAP_US  25u

static void cs_low(void)  { HAL_GPIO_WritePin(FPGA_CS_GPIO_Port, FPGA_CS_Pin, GPIO_PIN_RESET); }
static void cs_high(void) { HAL_GPIO_WritePin(FPGA_CS_GPIO_Port, FPGA_CS_Pin, GPIO_PIN_SET); }

/* SPI2 mutex: xfer() vola FpgaTask (poll 20 Hz) i UartTask (fpgaraw/fpgaloop).
 * Bez zamku by se dve transakce prokladaly (CS/SCK kolize -> rozbity ramec,
 * u FPGA slave rozhozene pocitani bitu). Vytvari fpga_freq_init (bezi uz pod
 * schedulerem z FpgaTasku). */
static osMutexId_t s_spi_mtx;

/* Jedna transakce BEZ zamku (volajici drzi `s_spi_mtx`, nebo bezi pred schedulerem). */
static bool xfer_raw(const uint8_t *tx, uint8_t *rx)
{
    cs_low();
    delay_us(FPGA_CS_GAP_US);                         /* CS low -> 1. SCK (>=1 us) */
#if FPGA_SPI_USE_DMA
    /* DMA jen pod schedulerem a mimo preruseni; pri chybe se transakce UKONCI (CS high) a vrati false -- zadna druha datova
     * faze pod stejnym CS (FPGA by videla delsi ramec). Po 3 chybach za sebou modul DMA vypne a jede se blokujici cestou. */
    HAL_StatusTypeDef st;
    if (fpga_spi_dma_ready()) st = fpga_spi_dma_txrx(tx, rx, FR_LEN, 100) ? HAL_OK : HAL_ERROR;
    else                      st = HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)tx, rx, FR_LEN, 100);
#else
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)tx, rx, FR_LEN, 100);
#endif
    delay_us(FPGA_CS_GAP_US);                         /* posledni SCK -> CS high (>=1 us) */
    cs_high();
    delay_us(FPGA_FRAME_GAP_US);                      /* pauza mezi ramci (>=20 us) */
    return (st == HAL_OK);
}

static bool xfer(const uint8_t *tx, uint8_t *rx)
{
    bool locked = false;
    if (s_spi_mtx != NULL && osKernelGetState() == osKernelRunning) {
        if (osMutexAcquire(s_spi_mtx, 100) != osOK) return false;
        locked = true;
    }
    bool ok = xfer_raw(tx, rx);
    if (locked) osMutexRelease(s_spi_mtx);
    return ok;
}

/* Co jsme FPGA naposledy POSLALI (0xFF = nic / zapomenuto) — vyznam u `fpga_freq_cfg_sync`. */
static uint8_t s_cfg_sent_chan = 0xFFu, s_cfg_sent_win = 0xFFu;
void fpga_freq_restart(void)
{
    if (!g_init_ok) return;
    s_cfg_sent_chan = 0xFFu; s_cfg_sent_win = 0xFFu;   /* ~3 s bez linku: FPGA mohla restartovat -> konfiguraci poslat znovu */
    if (!s_sim_v3) s_proto = 0u;                         /* protokol v3: verzi FPGA zjistit znovu (mohla byt nahrana jina) */
    uint8_t tx[FR_LEN], rx[FR_LEN];
    build_frame(TYPE_START, 0, NULL, 0, tx);
    xfer(tx, rx);
}

bool fpga_freq_link_ok(void)
{
    return s_link_ok != 0;
}

/* Pocet ramcu se spatnym CRC od bootu (diagnostika linky). */
uint32_t fpga_freq_crc_count(void)
{
    return g_rx_crc;
}

/* "uptime od posledni CRC chyby" v sekundach (0 = zadna chyba). */
uint32_t fpga_freq_crc_last_age_s(void)
{
    if (g_rx_crc == 0) return 0;
    return (HAL_GetTick() - g_rx_crc_last_ms) / 1000u;
}

/* Akceptacni krok 1: CRC self-test. crc16("123456789") MUSI byt 0x29B1,
 * jinak nase CRC neodpovida FPGA a nesmime nic posilat. */
bool fpga_freq_crc_selftest(void)
{
    /* optimalizace 5: tabulkova CRC = bitova reference (pseudonahodne ramce 1..FR_LEN B).
     * Buffer jen FR_LEN (128 B): vola se ze startu FpgaTasku (zasobnik 2560 B) -- buf[1024] tam po flashi v0.31.0
     * nechal volnych 36 B (L-0186). static nejde: soubezne ji vola run_selftests z jineho tasku. */
    {   uint8_t buf[FR_LEN]; uint32_t r = 0xC0FFEEu; int same = 1;
        for (int t = 0; t < 64 && same; t++) {
            int n = 1 + (int)(r % (uint32_t)FR_LEN);
            for (int i = 0; i < n; i++) { r = r * 1664525u + 1013904223u; buf[i] = (uint8_t)(r >> 24); }
            same = (crc16_ccitt(buf, n) == crc16_ccitt_ref(buf, n));
        }
        if (!same) { printf("fpga: CRC tabulka != reference\n"); return false; }
    }
    static const uint8_t v[9] = { '1','2','3','4','5','6','7','8','9' };
    uint16_t c = crc16_ccitt(v, 9);
    bool ok = (c == 0x29B1);
    printf("fpga: CRC selftest crc16(\"123456789\")=0x%04X %s\n",
           (unsigned)c, ok ? "OK" : "FAIL");
    return ok;
}

void fpga_freq_init(void)
{
    dwt_init();                  /* cyklovy citac pro presne us prodlevy */

    if (s_spi_mtx == NULL) s_spi_mtx = osMutexNew(NULL);   /* serializace xfer() */
    fpga_crc_init();                                      /* optimalizace 6: SW tabulka; HW jednotka jen s FPGA_CRC_USE_HW */
#if FPGA_SPI_USE_DMA
    /* krok 7: HW (streamy, NVIC) az pri prvnim `spi dma on` -- vychozi start je beze zmeny */
#endif

    /* audit 2026-10-20 (E1): fazovy zaznam -- selftest neblokuje SPI (jen hlaseni), stav se vynuluje */
    s_phrec_selftest_ok = (uint8_t)fpga_phrec_selftest();
    if (!fpga_freq_crc_selftest()) {
        /* CRC build neodpovida FPGA -> nezahajovat komunikaci (kontrakt bod 7.1) */
        printf("fpga: CRC selftest FAIL - SPI komunikace NEZAHAJENA\n");
        return;
    }

    /* CS pin (PB12) jako push-pull vystup - self-contained, nezavisle na gpio.c/IOC.
     * V IOC je PB12 jeste pod starym nazvem "SPI2_RCK" (74HC595 latch, uz nepouzity);
     * kdyby ho regen odebral, tahle konfigurace zajisti, ze CS porad funguje. */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gi = {0};
    gi.Pin   = FPGA_CS_Pin;
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_cfg_lock();
    HAL_GPIO_Init(FPGA_CS_GPIO_Port, &gi);
    gpio_cfg_unlock();

    cs_high();   /* CS idle (deasserted) */

    /* 2026-10-22 (protokol v3, docs/PROTOKOL_V3.md kap. 2): vystupy SPI2 SCK (PI1) a MOSI (PB15) na rychlost
     * FPGA_SPI_PIN_SPEED pro SCK 25 MHz. CubeMX je generuje jako GPIO_SPEED_FREQ_LOW (spi.c, HAL_SPI_MspInit), ten bezi
     * jen z MX_SPI2_Init -- HAL_SPI_Init pri zmene prescaleru (stav READY) piny nemeni, staci tedy jednou zde
     * (regen-safe, rozhodnuti uzivatele: ne v .ioc). Meni se JEN OSPEEDR (MODER/AFR beze zmeny), pod gpio_cfg_lock
     * kvuli zavodu jader o GPIOB (PB13 = ETH CM4). MISO (PI2) je na STM vstup, rychlost vystupu se ho netyka. */
    gpio_cfg_lock();
    GPIOI->OSPEEDR = (GPIOI->OSPEEDR & ~(3u << (1u * 2u)))  | (FPGA_SPI_PIN_SPEED << (1u * 2u));
    GPIOB->OSPEEDR = (GPIOB->OSPEEDR & ~(3u << (15u * 2u))) | (FPGA_SPI_PIN_SPEED << (15u * 2u));
    gpio_cfg_unlock();
    printf("fpga: SPI2 rychlost vystupu SCK=%lu MOSI=%lu (0 LOW, 1 MEDIUM, 2 HIGH, 3 VERY_HIGH)\n",
           (unsigned long)((GPIOI->OSPEEDR >> (1u * 2u)) & 3u), (unsigned long)((GPIOB->OSPEEDR >> (15u * 2u)) & 3u));

    /* Zvolit prescaler tak, aby SCK <= cil (bring-up). */
    static const struct { uint32_t e, d; } P[] = {
        { SPI_BAUDRATEPRESCALER_2,   2   }, { SPI_BAUDRATEPRESCALER_4,   4   },
        { SPI_BAUDRATEPRESCALER_8,   8   }, { SPI_BAUDRATEPRESCALER_16,  16  },
        { SPI_BAUDRATEPRESCALER_32,  32  }, { SPI_BAUDRATEPRESCALER_64,  64  },
        { SPI_BAUDRATEPRESCALER_128, 128 }, { SPI_BAUDRATEPRESCALER_256, 256 },
    };
    uint32_t k = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI123);
    uint32_t chosen = SPI_BAUDRATEPRESCALER_256, div = 256;
    for (int i = 0; i < 8; i++) {
        if (k / P[i].d <= FPGA_SCK_TARGET_HZ) { chosen = P[i].e; div = P[i].d; break; }
    }
    hspi2.Init.BaudRatePrescaler = chosen;
    /* SCK musi v klidu zustat LOW (Mode 0). S AFCNTR=DISABLE STM mezi prenosy uvolni
     * piny SCK/MOSI -> SCK plave a pull-up na FPGA ho tahne HIGH -> FPGA vidi pri CS-low
     * falesnou hranu a rozhodi pocitani bitu (RX0:FF). ENABLE = SPI drzi piny na idle
     * urovni i kdyz je vypnute (CFG2.AFCNTR=1). */
    hspi2.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
    if (HAL_SPI_Init(&hspi2) != HAL_OK) {
        printf("fpga: HAL_SPI_Init FAILED\n");
        return;
    }
    g_sck_hz = (k && div) ? k / div : 0;
    printf("fpga: SPI2 kernel=%lu Hz, SCK ~%lu Hz, CS=PB12\n",
           (unsigned long)k, (unsigned long)g_sck_hz);

    g_init_ok = 1;               /* od ted smime komunikovat */

    /* Spustit kontinualni mereni */
    fpga_freq_restart();
    printf("fpga: START odeslan\n");
}

/* Naparsuje DATA payload (absolutni offsety, payload zacina na FR_PAYLOAD=12). */
/* ══════════════ Emulator FPGA ramcu (viz fpga_freq.h) ══════════════════════
 * Sklada syntetický DATA ramec VCETNE spravneho CRC, takze projde uplne stejnou
 * cestou jako ramec z draru. Zadny zvlastni rezim v `parse_data` ani nize — to
 * je smysl celeho cviceni. */
#define SIM_FAULT_NONE   0
#define SIM_FAULT_LOST   1
#define SIM_FAULT_CRC    2
#define SIM_FAULT_DIV16  3
#define SIM_FAULT_PHASE  4

static uint8_t  s_sim_on     = 0;
static double   s_sim_hz     = 0.0;
static float    s_sim_noise  = 0.0f;     /* bila slozka +-ppb */
static float    s_sim_drift  = 0.0f;     /* ppb/hodinu */
static uint8_t  s_sim_fault  = SIM_FAULT_NONE;
static uint32_t s_sim_seq    = 0;
static uint32_t s_sim_next_ms = 0;       /* kdy vyrobit DALSI mereni */
static uint32_t s_sim_skip   = 0;        /* F-0193: jednorazove preskocit N mereni */
static uint64_t s_sim_ts     = 0;
static uint32_t s_sim_t0     = 0;        /* start emulace (pro drift) */
static uint32_t s_sim_rnd    = 0x12345678u;

/* Pravdepodobne nejlevnejsi rozumny generator: 32bit xorshift. Nepotrebujeme
 * kvalitni nahodnost, jen necosinusovy sum bez zavislosti na libc. */
static uint32_t sim_rand(void)
{
    uint32_t x = s_sim_rnd;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s_sim_rnd = x;
    return x;
}

/* Vrati kmitocet aktualniho mereni [Hz] = nominal * (1 + (drift + sum) * 1e-9). */
static double sim_freq_hz(void)
{
    float hours = (float)(HAL_GetTick() - s_sim_t0) / 3600000.0f;
    float dev_ppb = s_sim_drift * hours;
    if (s_sim_noise > 0.0f) {
        /* uniformne v <-noise, +noise> */
        float u = (float)(sim_rand() >> 8) / (float)(1u << 24);   /* 0..1 */
        dev_ppb += (u * 2.0f - 1.0f) * s_sim_noise;
    }
    return s_sim_hz * (1.0 + (double)dev_ppb * 1e-9);
}

static void sim_put_le64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* Naplni `f` (FR_LEN B, v2 ramec) kompletnim DATA ramcem. `fresh` = jde o NOVE
 * mereni.
 * 🔴 2026-10-03: emulator dela TOTEZ co FPGA FW 0x0400 (`top.v`/`spi_app.v`, L-0099):
 *  - posila PRESNE hodnoty (edge_count, dt v jednotkach T_clk/16384, caps bit5,
 *    FW_VERSION 0x0400, tdc_status = zkalibrovano), `frequency_x100000` = 0
 *    (kmitocet pocita parser z edges/dt);
 *  - cas hran ma KVANTIZACNI SUM TDC: kazda ze dvou hran okna dostane rovnomerne
 *    +-28,5 ps (bin ~57 ps), tedy sigma dt ~ 23 ps — jinak by hi-res cifry ukazovaly
 *    dokonalost, kterou prave carry-chain TDC nema;
 *  - `gate_time_ns` = floor(dt * 5 / 8192) (jako `gate_div`);
 *  - CH_B nese tentyz signal s VLASTNIM sumem (edges_b, dt_b).
 * `fpgasim fault phase` = tdc_status "retez A kratky" (puvodni 4fazova vada tu uz
 * nema smysl, injektor zustava pozorovatelny). */
static double sim_urand(void) { return (double)(sim_rand() % 100001u) / 100000.0; }   /* 0..1 */
#define SIM_CLK_MHZ 100u     /* emulovane hodiny FPGA = skutecne (top.v CLK_MHZ) */

/* ── Emulace GPS 1PPS (FW 0x0424, L-0099: tak, jak ji sklada RTL `pps_cap`) ─────────────────────────
 * Hrany na mrizce 1000 ms (HAL_GetTick = emulovany cas GPS). FPGA posila takt citace 100 MHz pri hrane;
 * hodiny 100 MHz jsou z OCXO, takze idealni faze roste o 1e8 * (1 + y_ocxo) taktu za sekundu a kazda hrana
 * ma vlastni (NEakumulovany) sum GPS ~30 ns RMS = 3 takty, pak floor na cely takt (kvantizace 10 ns).
 * y_ocxo zavisi na kodu DAC (`gpsdo_sim_dac_code`) -> `fpgasim on` + smycka GPSDO = uplna smycka bez desky.
 * ⚠️ Kanal A (mereny signal) na OCXO NEZAVISI -- zjednoduseni, emulator neladi casovou zakladnu mereni. */
#define SIM_OCXO_Y0    (-8.4e-7)    /* zmereno 2026-10-16 pri Vc = 0 V */
#define SIM_OCXO_KDAC  (3.1e-11)    /* relativne / LSB -- ZAMERNE jinak nez vychozi odhad serva (3,4e-11) */
static uint32_t s_sim_pps_next_ms;
static double   s_sim_pps_phase;    /* idealni faze [takty] od startu emulace */
static uint32_t s_sim_pps_ts;
static uint64_t s_sim_pps_pts;      /* v0.37.0: jemny cas pro emulator v3 [T/16384], krok 2048 (IDES8 1,25 ns) */
static uint8_t  s_sim_pps_seq;
static uint8_t  s_sim_nopps;        /* `fpgasim fault nopps`: GPS bez 1PPS (test holdoveru) */

static double sim_gauss(void)       /* ~N(0,1): soucet 3 rovnomernych (rozptyl 1/4) * 2 */
{
    return (sim_urand() + sim_urand() + sim_urand() - 1.5) * 2.0;
}

static void sim_pps_step(uint32_t now)
{
    if (s_sim_nopps) { s_sim_pps_next_ms = now + 1000u; return; }
    while ((int32_t)(now - s_sim_pps_next_ms) >= 0) {
        double y = SIM_OCXO_Y0 + SIM_OCXO_KDAC * (double)gpsdo_sim_dac_code();
        s_sim_pps_phase += 1e8 * (1.0 + y);
        const double tn = s_sim_pps_phase + sim_gauss() * 3.0;
        double t = floor(tn);
        s_sim_pps_ts = (uint32_t)(uint64_t)t;            /* mod 2^32 jako 32bitovy citac ve FPGA */
        s_sim_pps_pts = ((uint64_t)(tn * 8.0)) << 11;    /* v3 FW 0x0502: IDES8 = 8 vzorku na takt */
        s_sim_pps_seq++;
        s_sim_pps_next_ms += 1000u;
    }
}

static void sim_build_frame(uint8_t *f, int fresh)
{
    uint8_t pl[FR_PAYLOAD_MAX];
    memset(pl, 0, sizeof pl);
    memset(f, 0, FR_LEN);

    double hz = sim_freq_hz();
    uint64_t edges = (uint64_t)(hz * 0.25);
    if (edges == 0u) edges = 1u;                     /* nizke f: okno se protahne */
    /* skutecne okno [ps] + dva kvantizacni chyby TDC (kazda rovnomerne +-28,5 ps) */
    double dt_true_ps = (double)edges / hz * 1e12;
    double qa = (sim_urand() - 0.5) * 57.0 - (sim_urand() - 0.5) * 57.0;
    double qb = (sim_urand() - 0.5) * 57.0 - (sim_urand() - 0.5) * 57.0;
    /* hodiny 100 MHz, jednotka T/16384 = 0,6103515625 ps (1,6384 jednotky na ps) */
    uint64_t dt_a_u = (uint64_t)((dt_true_ps + qa) * (SIM_CLK_MHZ * 16384.0 / 1e6) + 0.5);
    uint64_t dt_b_u = (uint64_t)((dt_true_ps + qb) * (SIM_CLK_MHZ * 16384.0 / 1e6) + 0.5);
    if (dt_a_u == 0u) dt_a_u = 1u;
    if (dt_b_u == 0u) dt_b_u = 1u;
    uint64_t gate_ns = (dt_a_u * 5u) >> 13;          /* jako gate_div ve FPGA (100 MHz: dt * 5 / 8192) */

    sim_put_le64(pl + 0,  0u);                       /* frequency_x100000: FW 0x0400 nepocita */
    sim_put_le64(pl + 8,  edges);                    /* edge_count              */
    sim_put_le64(pl + 16, gate_ns);                  /* gate_time_ns            */
    sim_put_le64(pl + 24, s_sim_ts);                 /* timestamp_10MHz_ticks   */
    pl[32] = 0;                                      /* channel_id              */
    pl[33] = fresh ? 0x03u : 0x01u;                  /* measurement_status V+F  */
    if (s_sim_fault == SIM_FAULT_LOST) {
        pl[34] = 0x02;                               /* bit1 SIGNAL_LOST        */
        pl[33] = 0x00;                               /* uz ne VALID             */
    }
    pl[38] = 0;                                      /* phase_status: mrtve pole */
    pl[39] = (s_sim_fault == SIM_FAULT_DIV16) ? 0x01u : 0x00u;   /* status2 bit0 (CH_B chyba) */
    for (int i = 0; i < 4; i++) pl[40 + i] = (uint8_t)(s_sim_pps_ts >> (8 * i));   /* abs 52..55: 1PPS (FW 0x0424) */
    pl[44] = s_sim_pps_seq;                          /* abs 56 */
    pl[48] = 0x00; pl[49] = 0x04;                    /* fw_version 0x0400 (abs 60-61) */
    pl[50] = 0x23; pl[51] = 0x70;                    /* caps 0x7023 (abs 62-63): bit12 = abs 99 hodiny, bit13 = 1PPS, bit14 = diagnostika E0 */
    pl[45] = 0u; pl[46] = 0u; pl[47] = 0u;           /* abs 57..59 (caps bit14): bubliny P/S, znacky pri CS -- emulator bez ruseni */
    pl[87] = (uint8_t)SIM_CLK_MHZ;                   /* abs 99: hodiny FPGA [MHz] */
    pl[52] = 0x01;                                   /* clk_status */
    pl[88] = (uint8_t)(FPGA_TDC_CAL_A | FPGA_TDC_CAL_B |
                       ((s_sim_fault == SIM_FAULT_PHASE) ? FPGA_TDC_SHORT_A : 0u));   /* abs 100 */
    for (int i = 0; i < 6; i++) pl[89 + i] = (uint8_t)(dt_b_u >> (8 * i));            /* abs 101..106 */
    for (int i = 0; i < 4; i++) pl[96 + i] = (uint8_t)(edges >> (8 * i));             /* abs 108..111 */
    sim_put_le64(pl + 106, dt_a_u);                  /* abs 118..125 dt_a */

    f[0] = FR_MAGIC;
    f[1] = FR_VERSION;
    f[2] = TYPE_DATA;
    f[3] = (s_sim_fault == SIM_FAULT_LOST)
             ? (uint8_t)ST_ACK_OK
             : (uint8_t)(ST_DATA_VALID | (fresh ? ST_DATA_FRESH : 0u) | ST_ACK_OK);
    f[4] = (uint8_t)(s_sim_seq);
    f[5] = (uint8_t)(s_sim_seq >> 8);
    f[6] = (uint8_t)(s_sim_seq >> 16);
    f[7] = (uint8_t)(s_sim_seq >> 24);
    f[8] = FR_PAYLOAD_MAX;                           /* PAYLOAD_LEN */
    f[10] = 0x03u;                                   /* abs 10 (caps bit14): hlidac koherence platny, posledni epocha OK */
    f[11] = 0u;                                      /* abs 11: chybnych epoch 0 */
    memcpy(&f[FR_PAYLOAD], pl, sizeof pl);

    uint16_t crc = crc16_ccitt(f, FR_CRC_LEN);
    if (s_sim_fault == SIM_FAULT_CRC) crc ^= 0xFFFFu;   /* schvalne spatne */
    f[126] = (uint8_t)(crc);
    f[127] = (uint8_t)(crc >> 8);
}

/* Vyrobi ramec do rx[]. Nova SEQ jen ~4x/s (realna FPGA ma gate 0,25 s), i kdyz
 * FpgaTask polluje 20x/s — jinak by emulace vyrabela 20 mereni/s a zkreslila by
 * vse, co se opira o tempo mereni.
 * 🔴 F-0193: mereni bezi na PEVNE mrizce 250 ms a SEQUENCE roste o POCET mereni,
 * ktera od minula probehla — jako FPGA, ktera meri nepretrzite a nepotvrzene
 * mereni prepise. Drive se dalsi mereni planovalo az od okamziku pollu
 * (`now + 250`), takze zpozdeny FpgaTask diru v SEQUENCE nikdy nevidel a rozestup
 * mereni se zaokrouhloval NAHORU na nasobek periody pollu (50 ms + zpracovani),
 * tedy mene nez 4 mereni/s pri hradle 0,25 s = skryta mrtva doba emulatoru. */
static void sim_produce(uint8_t *rx)
{
    uint32_t now = HAL_GetTick();
    int fresh = 0;
    if ((int32_t)(now - s_sim_next_ms) >= 0) {
        uint32_t k = (now - s_sim_next_ms) / 250u + 1u;   /* kolik mereni probehlo */
        s_sim_next_ms += k * 250u;
        k += s_sim_skip;             /* injektor `fpgasim fault gap` */
        s_sim_skip = 0u;
        s_sim_seq += k;
        s_sim_ts  += (uint64_t)k * 2500000ull;   /* 0,25 s v tikach 10 MHz */
        fresh = 1;
    }
    sim_pps_step(now);
    sim_build_frame(rx, fresh);
}

void fpga_sim_set(int on, double hz, float noise_ppb, float drift_ppb_h)
{
    s_sim_on    = on ? 1u : 0u;
    s_sim_v3    = 0u;                               /* emulator v2 (128 B); v3 = fpga_sim_v3_set */
    if (!on) { s_sim_hz = 0.0; s_proto = 0u; return; }
    if (hz < 1.0) hz = 10000000.0;                  /* default 10 MHz */
    s_sim_hz    = hz;
    s_sim_noise = (noise_ppb < 0.0f) ? 0.0f : noise_ppb;
    s_sim_drift = drift_ppb_h;
    s_sim_t0    = HAL_GetTick();
    s_sim_next_ms = s_sim_t0;
    s_sim_pps_next_ms = s_sim_t0 + 1000u;
    s_sim_nopps = 0u;
    /* SEQ zamerne NEnulujeme: `g_last_seq` uz muze byt cokoli a shodne cislo by
     * vypadalo jako "neni nove mereni". Skok kupredu je bezpecny (kontrakt
     * porovnava jen nerovnost). */
    s_sim_seq  += 1000u;
}

int    fpga_sim_active(void) { return s_sim_on ? 1 : 0; }
static double s3_emu_hz;                   /* emulator v3 (definice nize) */
double fpga_sim_hz(void)     { return s_sim_on ? (s_sim_v3 ? s3_emu_hz : s_sim_hz) : 0.0; }

int fpga_sim_fault(const char *what)
{
    if (what == NULL)                   return 0;
    if (strcmp(what, "none")  == 0) { s_sim_fault = SIM_FAULT_NONE; s_sim_nopps = 0u; return 1; }
    if (strcmp(what, "nopps") == 0) { s_sim_nopps = 1u;              return 1; }   /* GPS bez 1PPS (holdover) */
    if (strcmp(what, "lost")  == 0) { s_sim_fault = SIM_FAULT_LOST;  return 1; }
    if (strcmp(what, "crc")   == 0) { s_sim_fault = SIM_FAULT_CRC;   return 1; }
    if (strcmp(what, "div16") == 0) { s_sim_fault = SIM_FAULT_DIV16; return 1; }
    if (strcmp(what, "phase") == 0) { s_sim_fault = SIM_FAULT_PHASE; return 1; }
    if (strcmp(what, "gap")   == 0) { s_sim_skip = 3u;               return 1; }   /* F-0193 */
    return 0;
}

/* ── regrese (FW >= 0x0411) ────────────────────────────────────────────────── */
static uint32_t s_rg_win = 0u, s_rg_used = 0u, s_rg_rej = 0u, s_rg_nl[2] = { 0u, 0u };
static double   s_rg_fr = 0.0, s_rg_f2 = 0.0, s_rg_s1 = 0.0, s_rg_s2 = 0.0;   /* Σd, Σd² [relativne] */
static double   s_rg_ewma = 0.0;          /* audit 2026-10-20: EWMA |d| prijatych oken (robustni odhad sigma_d) */
/* Kriticka analyza 2: EWMA se uci jen z PRIJATYCH oken -> kdyby sum zdroje vzrostl, mez by odmitala vse a uz se nezvetsila
 * (uzamceni). Po FPGA_REGR_REJ_RESET odmitnutich za sebou se adaptace vrati na zahrivani (pevna mez 5e-8). */
static uint32_t s_rg_adapt_n = 0u, s_rg_rej_run = 0u;
#define FPGA_REGR_REJ_RESET 16u

void fpga_freq_regr_reset(void)
{
    s_rg_win = s_rg_used = s_rg_rej = 0u; s_rg_nl[0] = s_rg_nl[1] = 0u;
    s_rg_fr = s_rg_f2 = s_rg_s1 = s_rg_s2 = 0.0;
    s_rg_ewma = 0.0; s_rg_adapt_n = 0u; s_rg_rej_run = 0u;
}

/* Audit 2026-10-20: adaptivni mez prijeti regrese (viz FPGA_REGR_K_SIGMA v .h). */
double fpga_freq_regr_limit(void)
{
    if (s_rg_adapt_n < FPGA_REGR_WARMUP || s_rg_ewma <= 0.0) return FPGA_REGR_MAX_REL;
    double lim = FPGA_REGR_K_SIGMA * s_rg_ewma * 1.2533141373155;
    if (lim < FPGA_REGR_MIN_REL) lim = FPGA_REGR_MIN_REL;
    if (lim > FPGA_REGR_MAX_REL) lim = FPGA_REGR_MAX_REL;
    return lim;
}

void fpga_freq_regr_stat(fpga_regr_stat_t *o)
{
    if (o == NULL) return;
    const uint32_t pm = __get_PRIMASK();      /* FpgaTask zapisuje pocitadla; cteni bez zamku by se roztrhlo */
    __disable_irq();
    o->windows = s_rg_win; o->used = s_rg_used; o->rejected = s_rg_rej;
    o->n_last[0] = s_rg_nl[0]; o->n_last[1] = s_rg_nl[1];
    o->f_regr_last = s_rg_fr; o->f_2pt_last = s_rg_f2;
    double n = (double)(s_rg_used ? s_rg_used : 1u);
    double mu = s_rg_s1 / n;
    double var = s_rg_s2 / n - mu * mu;
    o->d_mean_ppb  = mu * 1e9;
    o->d_sigma_ppb = (var > 0.0) ? sqrt(var) * 1e9 : 0.0;
    __set_PRIMASK(pm);
}

/* ── zaznamnik oken pro INL ── */
static fpga_inl_rec_t s_inl[FPGA_INL_N];
static uint32_t s_inl_w = 0u;      /* celkem zapsanych (index = s_inl_w % N) */
uint32_t fpga_freq_inl_count(void) { return (s_inl_w < FPGA_INL_N) ? s_inl_w : FPGA_INL_N; }
int fpga_freq_inl_get(uint32_t i, fpga_inl_rec_t *r)
{
    uint32_t n = fpga_freq_inl_count();
    if (r == NULL || i >= n) return 0;
    uint32_t base = (s_inl_w < FPGA_INL_N) ? 0u : (s_inl_w % FPGA_INL_N);
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    *r = s_inl[(base + i) % FPGA_INL_N];
    __set_PRIMASK(pm);
    return 1;
}
void fpga_freq_inl_reset(void)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    s_inl_w = 0u;
    __set_PRIMASK(pm);
}

static uint32_t rd_le24(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16); }
static uint64_t rd_le40(const uint8_t *p) { return (uint64_t)rd_le32(p) | ((uint64_t)p[4] << 32); }
static uint64_t rd_leN(const uint8_t *p, int n) { uint64_t v = 0u; for (int i = n - 1; i >= 0; i--) v = (v << 8) | p[i]; return v; }

/** FW >= 0x0426 (caps bit3): surove soucty segmentu -> teziste ve STEJNYCH jednotkach, jake do 0x0425 pocitala FPGA:
 *  xm = x0*4096 + floor(sx*4096/n) [1/4096 hrany], ym = y0*16 + floor((sy3<<3)*16/n) [1/16 jednotky T/16384].
 *  Celociselne v uint64: sx < 2^42 -> sx*4096 < 2^54; sy3 < 2^56 -> sy3*128 < 2^63. n < 2 -> 0 (blok se zneplatni). */
static void rg_raw_to_mean(uint32_t n, uint32_t x0, uint64_t sx, uint64_t y0, uint64_t sy3, uint64_t *xm, uint64_t *ym)
{
    if (n < 2u) { *xm = 0u; *ym = 0u; return; }
    *xm = ((uint64_t)x0 << 12) + (sx << 12) / n;
    *ym = (y0 << 4) + (sy3 << 7) / n;
}

/** Kmitocet [Hz] ze sklonu primky mezi stredy segmentu A a B (cisty vypocet, selftest #1):
 *  f = (xB - xA) / (yB - yA); x = index hrany (v 1/256), y = cas (v 1/16 jednotky T/16384 = 15625/(256*clk_mhz) ps;
 *  100 MHz 0,6103515625 ps).
 *  @return 0.0 = nelze (B <= A, nulovy jmenovatel). */
double fpga_freq_regr_hz2(uint64_t xm_a, uint64_t xm_b, uint64_t ym_a, uint64_t ym_b, uint32_t clk_mhz, uint16_t xdiv)
{
    if (xm_b <= xm_a || ym_b <= ym_a) return 0.0;
    double dx = (double)(xm_b - xm_a) / (double)(xdiv ? xdiv : 256u);   /* audit 4: 1/256 nebo 1/4096 hrany */                              /* hrany */
    double dy = (double)(ym_b - ym_a) / 16.0 * 15625.0 / (256.0 * (double)(clk_mhz ? clk_mhz : 100u));   /* ps */
    return dx / dy * 1e12;
}

/* puvodni rozhrani (xm v 1/256 hrany) -- selftesty, emulator */
double fpga_freq_regr_hz(uint64_t xm_a, uint64_t xm_b, uint64_t ym_a, uint64_t ym_b, uint32_t clk_mhz)
{
    return fpga_freq_regr_hz2(xm_a, xm_b, ym_a, ym_b, clk_mhz, 256u);
}

/* ── GPS 1PPS (FW >= 0x0424, STATUS #305) ───────────────────────────────────────────────────────
 * FPGA cte bajty 52..56 zive z domeny 100 MHz (bez snimku). Hrana prave behem skladani ramce muze dat
 * roztrzenou hodnotu; ta se ve dvou po sobe jdoucich ramcich nezopakuje (hodnota se meni nejvys 10x/s,
 * ramce chodi 20x/s), takze udalost se vyhlasi az pri SHODE dvou ramcu. Zapisuje FpgaTask (z pollu),
 * cte kdokoli pres fpga_pps_get (kratky IRQ-off, jako fpga_freq_get_last). */
static fpga_pps_t s_pps;
static uint8_t    s_pps_have;
static uint32_t   s_pps_torn;
static uint32_t   s_pc_ts, s_pc_first;
static uint8_t    s_pc_seq, s_pc_have;
static uint16_t   s_pc_frac;          /* v0.37.0: zlomek, priznak a epocha kandidata (shoda dvou ramcu) */
static uint8_t    s_pc_fine, s_pc_ep;

static void pps_feed(const fpga_meas_t *m)
{
    if (!m->pps_ok) { s_pc_have = 0u; return; }
    const int same_t = m->pps_ts == s_pc_ts && m->pps_frac == s_pc_frac && m->pps_fine == s_pc_fine && m->pps_epoch == s_pc_ep;
    if (s_pc_have && m->pps_seq == s_pc_seq && same_t) {
        if (s_pps_have && s_pps.seq == m->pps_seq && s_pps.ts == m->pps_ts && s_pps.frac == m->pps_frac) return;   /* uz vyhlaseno */
        fpga_pps_t e;
        e.ts       = m->pps_ts;
        e.seq      = m->pps_seq;
        e.clk_mhz  = m->clk_mhz ? m->clk_mhz : 100u;
        e.ref_ext  = ((m->caps & FPGA_CAP_REFCTL) && (m->clk_status & FPGA_CLK_REF_EXT)) ? 1u : 0u;
        e.clk_bad  = ((m->caps & FPGA_CAP_SELFCHK) &&
                      (!(m->clk_status & FPGA_CLK_OK) || (m->clk_status & FPGA_CLK_RECOVER))) ? 1u : 0u;
        e.first_ms = s_pc_first;
        e.n        = s_pps.n + 1u;
        e.frac     = m->pps_frac; e.fine = m->pps_fine; e.epoch = m->pps_epoch;
        uint32_t pm = __get_PRIMASK();
        __disable_irq();
        s_pps = e; s_pps_have = 1u;
        __set_PRIMASK(pm);
        return;
    }
    /* nova hodnota: kandidat. Zmena jen jedne slozky = jiste roztrzene cteni (nova hrana meni obe). */
    if (s_pc_have && ((m->pps_seq == s_pc_seq) != same_t)) s_pps_torn++;
    s_pc_seq = m->pps_seq; s_pc_ts = m->pps_ts; s_pc_have = 1u;
    s_pc_frac = m->pps_frac; s_pc_fine = m->pps_fine; s_pc_ep = m->pps_epoch;
    s_pc_first = HAL_GetTick();
}

bool fpga_pps_get(fpga_pps_t *out)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    uint8_t have = s_pps_have;
    if (have && out) *out = s_pps;
    __set_PRIMASK(pm);
    return have != 0u;
}

uint32_t fpga_pps_torn(void) { return s_pps_torn; }

static void parse_data(const uint8_t *rx, fpga_meas_t *m)
{
    const uint8_t *p = &rx[FR_PAYLOAD];
    m->frequency_x100000  = rd_le64(p + 0);    /* abs 12: pin28 /4   */
    m->edge_count         = rd_le64(p + 8);    /* abs 20 */
    m->gate_time_ns       = rd_le64(p + 16);   /* abs 28 */
    m->timestamp_ticks    = rd_le64(p + 24);   /* abs 36 */
    m->channel_id         = p[32];             /* abs 44 */
    m->measurement_status = p[33];             /* abs 45 */
    m->error_flags        = rd_le32(p + 34);   /* abs 46 */
    m->phase_status       = p[38];             /* abs 50 */
    m->status2            = p[39];             /* abs 51 */
    m->freq16_x100000     = rd_le64(p + 40);   /* abs 52: pin27 /16  */
    /* v2 rozsireni — beze zmeny proti tomu, co uz existovalo v v1 offsetech
     * 0..47 (payload). Stary v1 rámec (a emulator, ktery je NEplni) tady
     * necha nuly, coz se cte jako "neznama verze" — bezpecne. */
    m->fw_version         = (uint16_t)p[48] | ((uint16_t)p[49] << 8);   /* abs 60-61 */
    m->caps               = (uint16_t)p[50] | ((uint16_t)p[51] << 8);   /* abs 62-63 */
    m->clk_status         = p[52];             /* abs 64 */
    m->clk_loss           = p[53];             /* abs 65 */
    m->clk_mhz            = (m->caps & FPGA_CAP_CLK) ? p[87] : 100u;   /* abs 99: hodiny FPGA [MHz]; bez bitu 100 (FW <= 0x0420) */
    if (m->caps & FPGA_CAP_RAWREG) {   /* FW >= 0x0426: abs 24..43 nesou regresi segmentu B -> edge_count jen u32, gate_ns/ts neplati */
        m->edge_count &= 0xFFFFFFFFull; m->gate_time_ns = 0u; m->timestamp_ticks = 0u;
    }
    m->sequence           = rd_le32(&rx[4]);
    m->status_flags       = rx[3];
    /* FW >= 0x0424 (caps bit13): abs 52..56 = GPS 1PPS (do 0x0423 freq16_x100000, ktery FPGA posilala jako 0
     * a ktery se s caps bit5 stejne prepocita nize z edges_b/dt_b) */
    m->pps_ok  = (m->caps & FPGA_CAP_PPS) ? 1u : 0u;
    m->pps_ts  = m->pps_ok ? rd_le32(p + 40) : 0u;   /* abs 52..55 */
    m->pps_frac = 0u; m->pps_fine = 0u; m->pps_epoch = 0u;   /* v0.37.0: v2 = jen hruby cas */
    m->presc_log2 = 0u;                                       /* audit F-0275: v2 bez predelicky (struktura se nenuluje) */
    m->pps_seq = m->pps_ok ? p[44] : 0u;              /* abs 56 */
    /* FW >= 0x0425 (caps bit14): diagnostika O4 + hlidac koherence (abs 10/11 lezi v hlavicce, rx[10]/rx[11]) */
    m->diag0_ok = (m->caps & FPGA_CAP_DIAG0) ? 1u : 0u;
    m->coh_st   = m->diag0_ok ? rx[10] : 0u;
    m->coh_cnt  = m->diag0_ok ? rx[11] : 0u;
    m->bub_p    = m->diag0_ok ? p[45] : 0u;               /* abs 57 */
    m->bub_s    = m->diag0_ok ? p[46] : 0u;               /* abs 58 */
    m->ovl_p    = m->diag0_ok ? (uint8_t)(p[47] & 0x0Fu) : 0u;   /* abs 59 */
    m->ovl_s    = m->diag0_ok ? (uint8_t)(p[47] >> 4) : 0u;
    if (m->caps & FPGA_CAP_STMCAL) {   /* FW >= 0x0427: abs 112..115 (p + 100..103) */
        m->lut_wait = (uint8_t)(p[100] & 3u);
        m->lut_cnt  = (uint16_t)((uint16_t)p[101] | ((uint16_t)(p[100] >> 2) << 8));
        m->lut_sum  = (uint16_t)((uint16_t)p[102] | ((uint16_t)p[103] << 8));
    } else { m->lut_wait = 0u; m->lut_cnt = 0u; m->lut_sum = 0u; }
    if (m->caps & FPGA_CAP_SELFCHK) {
        /* FW >= 0x041F: abs 12..19 nese identitu bitstreamu (frequency_x100000 tam bylo od FW 0x0400 vzdy 0
         * a nize se stejne prepocita z dt) a abs 97/98 kontrolni rozdil pocitani hran. */
        m->build_time = rd_le32(p + 0);
        m->build_git  = rd_le32(p + 4);
        m->ccd_p      = (int8_t)p[85];
        m->ccd_s      = (int8_t)p[86];
        m->frequency_x100000 = 0u;
    } else {
        m->build_time = 0u; m->build_git = 0u; m->ccd_p = 0; m->ccd_s = 0;
    }

    if (m->caps & FPGA_CAP_DT) {
        /* 🔴 FW >= 0x0400 (carry-chain TDC): FPGA frequency_x100000 NEPOCITA (0), nese
         * presne celociselne hodnoty a poměr dela HOST. dt v jednotkach T_clk/16384.
         * abs 100 tdc_status | 101..106 dt_b (u48) | 108..111 edges_b | 118..125 dt_a (u64) */
        m->tdc_status = p[88];
        m->dt_b_ps    = dt_units_to_ps(rd_le48(p + 89), m->clk_mhz);
        m->edges_b    = rd_le32(p + 96);
        m->dt_units   = rd_le64(p + 106) & 0xFFFFFFFFFFFFull;
        m->gate_ps    = dt_units_to_ps(m->dt_units, m->clk_mhz);
        m->gate2_ps   = m->gate_ps;
        m->regr_used  = 0u; m->f_regr_hz = 0.0; m->rg_ok = 0u;
        m->codes_ok   = 0u;
        if (m->caps & FPGA_CAP_CODES) {      /* abs 68..75: kody TDC okna (jen kdyz neni REGR) */
            m->code_a_end = (uint16_t)(p[56] | ((p[57] & 1u) << 8));
            m->code_a_st  = (uint16_t)(p[58] | ((p[59] & 1u) << 8));
            m->code_b_end = (uint16_t)(p[60] | ((p[61] & 1u) << 8));
            m->code_b_st  = (uint16_t)(p[62] | ((p[63] & 1u) << 8));
            m->codes_ok   = 1u;
        }
        if (m->caps & FPGA_CAP_REGR) {
            if (m->caps & FPGA_CAP_RAWREG) {
                /* FW >= 0x0426: surove soucty segmentu, deli STM (rg_raw_to_mean). Rozlozeni viz spi_app.v. */
                const uint8_t syb[7] = { p[31], p[80], p[81], p[82], p[83], p[84], p[95] };   /* B: abs 43, 92..96, 107 */
                uint32_t xa = rd_le32(p + 57), xb = rd_le32(p + 15);
                m->rg_n[0] = rd_le24(p + 54);  m->rg_n[1] = rd_le24(p + 12);
                m->rg_ok   = (uint8_t)(((xa >> 31) & 1u) | (((xb >> 31) & 1u) << 1));
                rg_raw_to_mean(m->rg_n[0], xa & 0x03FFFFFFu, rd_le48(p + 61) & 0x3FFFFFFFFFFull, rd_le48(p + 67), rd_leN(p + 73, 7),
                               &m->rg_xm[0], &m->rg_ym[0]);
                rg_raw_to_mean(m->rg_n[1], xb & 0x03FFFFFFu, rd_le48(p + 19) & 0x3FFFFFFFFFFull, rd_le48(p + 25), rd_leN(syb, 7),
                               &m->rg_xm[1], &m->rg_ym[1]);
                if (m->rg_n[0] < 2u) m->rg_ok &= (uint8_t)~1u;
                if (m->rg_n[1] < 2u) m->rg_ok &= (uint8_t)~2u;
            } else {
            /* regresni blok abs 68..96 (p + 56..84), viz spi_app.v */
            m->rg_n[0]  = rd_le24(p + 56);  m->rg_xm[0] = rd_le40(p + 59);  m->rg_ym[0] = rd_le48(p + 64);
            m->rg_n[1]  = rd_le24(p + 70);  m->rg_xm[1] = rd_le40(p + 73);  m->rg_ym[1] = rd_le48(p + 78);
            m->rg_ok    = (uint8_t)(p[84] & 3u);
            }
            if (m->rg_ok == 3u && m->edge_count > 0u && m->gate_ps > 0u) {
                s_rg_win++; s_rg_nl[0] = m->rg_n[0]; s_rg_nl[1] = m->rg_n[1];
                double f2 = (double)m->edge_count * 1e12 / (double)m->gate_ps;
                m->rg_xdiv = (m->caps & (FPGA_CAP_XM12 | FPGA_CAP_RAWREG)) ? 4096u : 256u;   /* audit 4; RAWREG: rg_raw_to_mean dava 1/4096 */
                double fr = fpga_freq_regr_hz2(m->rg_xm[0], m->rg_xm[1], m->rg_ym[0], m->rg_ym[1], m->clk_mhz, m->rg_xdiv);
                s_rg_f2 = f2;
                /* Optimalizace 3 (test T3 na hostu): sigma rozdilu d se uci ze VSECH oken s platnou regresi, ne jen z prijatych --
                 * uceni z prijatych dava useknute rozdeleni (sigma podhodnocena, mez se sama zuzuje) a po skoku sumu zdroje mez
                 * odmitala ~15 % oken trvale. |d| se oreze na FPGA_REGR_MAX_REL (vzacne odlehle okno mez nerozhodi). Rozhoduje se
                 * mezi spoctenou PRED zapocitanim tohoto okna. */
                int rg_in = (m->rg_n[0] >= FPGA_REGR_MIN_N && m->rg_n[1] >= FPGA_REGR_MIN_N && fr > 0.0);
                double d = rg_in ? (fr / f2 - 1.0) : 0.0;
                double lim = fpga_freq_regr_limit();
                if (rg_in) {
                    double ad = fabs(d); if (ad > FPGA_REGR_MAX_REL) ad = FPGA_REGR_MAX_REL;
                    s_rg_ewma = (s_rg_adapt_n == 0u) ? ad : s_rg_ewma + (ad - s_rg_ewma) * (1.0 / 64.0);
                    s_rg_adapt_n++;
                }
                if (rg_in && fabs(d) < lim) {
                    /* efektivni delka okna N / f_regr: vsechny dalsi vypocty (hi-res, akumulatory, datalog,
                     * IPC, SCPI) pak dostanou regresni kmitocet bez dalsich zmen. Rozdil proti skutecne
                     * delce je <~ 1e-8 relativne (ns u okna 0,25 s) -- pro ucetnictvi casu zanedbatelny. */
                    double g = (double)m->edge_count * 1e12 / fr;
                    m->gate_ps = (uint64_t)(g + 0.5);
                    m->f_regr_hz = fr; m->regr_used = 1u;
                    s_rg_used++; s_rg_fr = fr; s_rg_s1 += d; s_rg_s2 += d * d;
                    s_rg_rej_run = 0u;
                } else {
                    s_rg_rej++;
                    if (++s_rg_rej_run >= FPGA_REGR_REJ_RESET) { s_rg_ewma = 0.0; s_rg_adapt_n = 0u; s_rg_rej_run = 0u; }
                }
            }
        }
        m->gate_time_ns       = m->gate_ps / 1000u;               /* informativne (zobrazeni) */
        m->frequency_x100000  = freq_x1e5_from(m->edge_count, m->gate_ps);
        m->freq16_x100000     = freq_x1e5_from(m->edges_b,   m->dt_b_ps);   /* CH_B */
    } else {
        /* stary FW / emulator: presne okno z floor(dt*2,5 ns) (F-0186) */
        m->tdc_status = 0u;
        m->dt_b_ps    = 0u;
        m->edges_b    = 0u;
        m->gate_ps    = fpga_freq_dt_ticks(m->gate_time_ns) * FPGA_LEGACY_TICK_PS;
        m->gate2_ps   = m->gate_ps;
        m->regr_used  = 0u; m->f_regr_hz = 0.0; m->rg_ok = 0u;
        m->dt_units   = 0u;
    }
}

/* ═══ Protokol v3 (docs/PROTOKOL_V3.md, krok 4c, v0.35.0) ═══════════════════════════════════════════════════════════════
 * Detekce verze (kap. 6.2): po startu / ztrate linku jedna transakce 1024 B s NULOVYM MOSI (MAGIC 0 -> v2 i v3 ji zahodi).
 * Prvnich 128 B s VER 0x02 a CRC 0..125 = FPGA v2 (PHY v2 vraci svuj ramec 8x dokola) -> dal kod v2 vyse beze zmeny;
 * VER 0x03 a CRC bloku 0 = FPGA v3 -> tato vetev:
 *   * kazda transakce = ACK s REC_ACK (dalsi ocekavany zaznam); prijem fpga_v3_link_take (prekryv ramcu = duplicity,
 *     mezera = preruseni oken, reset FPGA = resynchronizace),
 *   * zaznamy -> okna fpga_v3_grp_feed (K = hradlo / L), okno VYBRANEHO kanalu -> fpga_meas_t se stejnymi poli jako v2
 *     (konzumenti beze zmeny): edge_count/gate2_ps dvoubodove, f_regr_hz = LSQ pres teziste segmentu, teziste prvniho
 *     segmentu v rg_xm/rg_ym[0] (fazovy zaznam fpga_phrec teleskopuje jako u v2), druhy kanal = sekundarni slot,
 *   * vyber kanalu a hradla je LOKALNI (FPGA meri vzdy oba kanaly po segmentech), ztratu signalu hlida STM.
 * Buffery 1024 B jsou staticke (L-0186: 1 KB na zasobniku FpgaTasku nechal 36 B): s3_* jen FpgaTask, s3c_* prikazy z
 * libovolneho tasku pod SPI mutexem. */
#define FR3_GAP_US       200u       /* mezera mezi transakcemi >= 150 us (FPGA sklada dalsi ramec, kap. 2) */
#define S3_Q             8u         /* fronta hotovych oken vybraneho kanalu */
#define S3_LOST_MS       2500u      /* ztrata signalu: zadne okno dele nez 2,5 s + hradlo (jako v2) */
/* v0.35.1: transakce nejdriv po 40 ms (25/s >= 7/s potrebnych, kap. 6.1). FpgaTask vola poll pri hradle 50 ms po 10 ms
 * (pravidlo v2: mereni precist driv, nez ho FPGA prepise) -- ve v3 se nic neprepise (kruh 6,4 s) a okna se vydavaji
 * z fronty po jednom; bez meze az ~90 transakci 1 KB/s (~7 % CPU v cekaci smycce, ~5 % casu maskovane znacky pri CS). */
#define S3_POLL_MS       40u
static uint8_t  s3_tx[FPGA_V3_LEN], s3_rx[FPGA_V3_LEN];       /* FpgaTask (poll, kalibrace) */
static uint8_t  s3c_tx[FPGA_V3_LEN], s3c_rx[FPGA_V3_LEN];     /* prikazy (UART, FpgaTask) -- jen pod s_spi_mtx */
static uint32_t s3_mseq = 0u;
static fpga_v3_link_t s3_link;
static fpga_v3_grp_t  s3_grp;
static fpga_v3_hdr_t  s3_h;
static uint8_t  s3_h_ok = 0u;
static fpga_v3_rec_t  s3_rec[FPGA_V3_REC_MAX];
static fpga_meas_t s3_q[S3_Q];
static uint8_t  s3_qr = 0u, s3_qw = 0u;
static fpga_v3_win_t s3_wb;          /* posledni okno druheho kanalu (sekundarni slot) */
static uint8_t  s3_wb_ok = 0u;
static uint32_t s3_wseq = 0u;        /* poradi okna = `sequence` pro konzumenty; mezera v oknech = dira v SEQ */
static uint8_t  s3_brk = 1u;         /* pred dalsim oknem je preruseni */
static uint8_t  s3_chan = 0u, s3_gate = 2u;
static uint32_t s3_last_win_ms = 0u;
static uint8_t  s3_lost = 0u;
static uint32_t s3_frames = 0u, s3_wins = 0u, s3_qdrop = 0u, s3_pages = 0u;
static uint32_t s3_xfer_ms = 0u;   /* HAL_GetTick posledni transakce poll_v3 */

static bool xfer3_raw(const uint8_t *tx, uint8_t *rx)
{
    cs_low();
    delay_us(FPGA_CS_GAP_US);
#if FPGA_SPI_USE_DMA
    HAL_StatusTypeDef st;
    if (fpga_spi_dma_ready()) st = fpga_spi_dma_txrx(tx, rx, (uint16_t)FPGA_V3_LEN, 100) ? HAL_OK : HAL_ERROR;
    else                      st = HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)tx, rx, (uint16_t)FPGA_V3_LEN, 100);
#else
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)tx, rx, (uint16_t)FPGA_V3_LEN, 100);
#endif
    delay_us(FPGA_CS_GAP_US);
    cs_high();
    delay_us(FR3_GAP_US);
    return (st == HAL_OK);
}

static bool spi_lock(uint32_t ms)
{
    if (s_spi_mtx != NULL && osKernelGetState() == osKernelRunning) return osMutexAcquire(s_spi_mtx, ms) == osOK;
    return true;
}
static void spi_unlock(void)
{
    if (s_spi_mtx != NULL && osKernelGetState() == osKernelRunning) osMutexRelease(s_spi_mtx);
}

static bool xfer3(const uint8_t *tx, uint8_t *rx)
{
    if (!spi_lock(100)) return false;
    bool ok = xfer3_raw(tx, rx);
    spi_unlock();
    return ok;
}

/* K segmentu na okno = hradlo / L (50 ms / 25 ms = 2 ... 1 s = 40) */
static uint32_t s3_k(void)
{
    const uint32_t L = (s3_h_ok && s3_h.seg_len_ms) ? s3_h.seg_len_ms : 25u;
    const uint32_t k = fpga_gate_idx_ms(s3_gate) / L;
    return k ? k : 1u;
}
static void s3_regroup(void)
{
    fpga_v3_grp_init(&s3_grp, s3_k());
    s3_grp.seg_ms = (s3_h_ok && s3_h.seg_len_ms) ? s3_h.seg_len_ms : 25u;
    s3_qr = s3_qw = 0u; s3_wb_ok = 0u; s3_brk = 1u;
}
static void s3_reset(void)
{
    fpga_v3_link_init(&s3_link);
    s3_regroup();
    s3_last_win_ms = HAL_GetTick();
}

/* Detekce verze FPGA (kap. 6.2). Vraci 0 (nic), 2 nebo 3. */
static uint8_t proto_detect(void)
{
    memset(s3_tx, 0, sizeof s3_tx);
    if (!xfer3(s3_tx, s3_rx)) { g_xfer_ok = 0u; return 0u; }
    g_xfer_ok = 1u; g_last_rx0 = s3_rx[0];
    if (s3_rx[0] != FR_MAGIC) return 0u;
    if (s3_rx[1] == FR_VERSION && fpga_crc16_check(s3_rx, FR_CRC_LEN, (uint16_t)((uint16_t)s3_rx[126] | ((uint16_t)s3_rx[127] << 8))))
        return 2u;
    if (s3_rx[1] == FPGA_V3_VER && (fpga_v3_blk_ok(s3_rx) & 1u)) return 3u;
    return 0u;
}

/* stav TDC ve v2 bitech (FPGA_TDC_*) z TDC_STATUS v3 (kap. 3.1: bit0 platna, bit1 selhala, bit2 sber) */
static uint8_t s3_tdc_v2(const fpga_v3_hdr_t *h)
{
    uint8_t s = 0u;
    if (h->tdc_status[0] & 1u) s |= FPGA_TDC_CAL_A;
    if (h->tdc_status[1] & 1u) s |= FPGA_TDC_CAL_B;
    if ((h->tdc_status[0] | h->tdc_status[1]) & 2u) s |= FPGA_TDC_CAL_FAIL;
    if ((h->tdc_status[0] | h->tdc_status[1]) & 4u) s |= FPGA_TDC_CAL_BUSY;
    return s;
}

/* Pole hlavicky, ktera nesou i okna (a diagnosticky snimek ramce bez okna). caps = v2 bity, jejichz pole jsou platna. */
#define S3_CAPS16 ((uint16_t)(FPGA_CAP_DT | FPGA_CAP_CHAN | FPGA_CAP_SELFCHK | FPGA_CAP_CLK | FPGA_CAP_PPS | FPGA_CAP_DIAG0 | \
                              FPGA_CAP_STMCAL | FPGA_CAP_REFCTL | 0x0002u))
/* v0.38.0 (FW 0x0503, etapa E5): predelicka kanalu. Volba 0xFF = automaticky: kazdy ramec odhad ze sondy (/16 za 1 ms),
 * log2 P s hysterezi (fpga_v3_range_select); prikaz az po shode DVOU ramcu (sonda se cte zive -> roztrzene cteni) a jen
 * kdyz predchozi zmena uz plati (PRESC bit7 = 0). Kanal B se zdrojem z pinu A (SET_CONFIG 0x09) = P 1 (sirka pulzu,
 * zpozdeni A/B). Prepnuti = preruseni souvisleho useku (statistika, fazovy zaznam). */
static uint8_t  s3_pr_mode[2] = { 0xFFu, 0xFFu };
static uint8_t  s3_pr_cand[2] = { 0xFFu, 0xFFu };
static uint32_t s3_pr_sw[2];
static bool s3_cfg_any(uint8_t id, uint8_t v1, uint8_t v2);
static void s3_range_ctl(const fpga_v3_hdr_t *h);

static void s3_hdr_to_meas(const fpga_v3_hdr_t *h, fpga_meas_t *m)
{
    m->fw_version = h->fw_version;
    m->caps       = S3_CAPS16;
    m->clk_status = h->clk_status; m->clk_loss = h->clk_loss; m->clk_mhz = h->clk_mhz ? h->clk_mhz : 100u;
    m->build_time = h->build_time; m->build_git = h->build_git;
    m->diag0_ok   = 1u; m->coh_st = h->coh_st; m->coh_cnt = h->coh_cnt;
    m->bub_p = h->bub[s3_chan & 1u]; m->bub_s = h->bub[(s3_chan ^ 1u) & 1u];
    m->ovl_p = h->ovl[s3_chan & 1u]; m->ovl_s = h->ovl[(s3_chan ^ 1u) & 1u];
    m->tdc_status = s3_tdc_v2(h);
    m->lut_wait = (uint8_t)(h->cal_wait & 3u); m->lut_cnt = h->lut_cnt; m->lut_sum = h->lut_sum;
    m->pps_ok = 1u; m->pps_seq = (uint8_t)h->pps[0].pseq;
    m->pps_fine = (uint8_t)fpga_v3_pps_fine(h, 0, &m->pps_ts, &m->pps_frac);   /* v0.37.0: jemny cas (IDES8) */
    m->pps_epoch = FPGA_V3_PPS_EPOCH(h->pps_status);
    m->channel_id = s3_chan;
    m->phase_status = fpga_gate_idx_to_win(s3_gate);          /* echo hradla jako v2 (zde lokalni volba) */
}

static void s3_win_to_meas(const fpga_v3_win_t *w, fpga_meas_t *m)
{
    memset(m, 0, sizeof *m);
    s3_hdr_to_meas(&s3_h, m);
    const uint32_t clk = m->clk_mhz;
    s3_wseq += s3_brk ? 2u : 1u;                                   /* preruseni = dira v SEQ -> fpga_stat_break, fazovy zaznam */
    s3_brk = 0u;
    m->sequence = s3_wseq;
    m->measurement_status = 0x03u;
    m->status_flags = (uint8_t)(ST_DATA_VALID | ST_DATA_FRESH);
    m->edge_count = w->edges;                                     /* periody (v0.38.0: hrany x P) */
    m->presc_log2 = w->p_log2;
    m->dt_units   = w->gate_units & 0xFFFFFFFFFFFFull;
    m->gate2_ps   = dt_units_to_ps(m->dt_units, clk);
    m->gate_ps    = m->gate2_ps;
    m->cc_ok = 1u; m->f_cc_hz = w->f_2pt_hz; m->gate_cc_ps = m->gate2_ps;   /* dvoubodove mezi prvnimi znackami teleskopuje */
    /* LSQ pres teziste segmentu (vaha N): efektivni delka N / f_lsq jako regrese v2; mez 1e-6 proti dvoubodovemu (pojistka) */
    if (w->lsq_ok && w->f_lsq_hz > 0.0 && w->f_2pt_hz > 0.0 && fabs(w->f_lsq_hz / w->f_2pt_hz - 1.0) < 1e-6) {
        m->f_regr_hz = w->f_lsq_hz; m->regr_used = 1u;
        m->gate_ps = (uint64_t)((double)w->edges * 1e12 / w->f_lsq_hz + 0.5);
    }
    /* teziste prvniho segmentu relativne k prvni znacce okna (fpga_phrec: rg_xm v 1/4096 hrany, rg_ym v 1/16 jednotky) */
    m->rg_xdiv  = 4096u;
    m->rg_n[0]  = w->n0; m->rg_n[1] = w->n0;
    m->rg_xm[0] = (uint64_t)((w->xc0 - (double)w->x_first) * 4096.0 + 0.5);
    m->rg_ym[0] = (uint64_t)((w->tc0 - (double)w->t_first) * 16.0 + 0.5);
    m->rg_ok    = (uint8_t)((w->n0 >= FPGA_REGR_MIN_N && m->regr_used) ? 3u : 0u);
    m->ccd_p = (int8_t)((w->flags_or & FPGA_V3_R_CCD) ? 2 : 0);
    if (s3_wb_ok) {
        m->edges_b = (uint32_t)s3_wb.edges;
        m->dt_b_ps = dt_units_to_ps(s3_wb.gate_units & 0xFFFFFFFFFFFFull, clk);
        m->ccd_s   = (int8_t)((s3_wb.flags_or & FPGA_V3_R_CCD) ? 2 : 0);
    }
    m->gate_time_ns      = m->gate_ps / 1000u;
    m->frequency_x100000 = freq_x1e5_from(m->edge_count, m->gate_ps);
    m->freq16_x100000    = freq_x1e5_from(m->edges_b, m->dt_b_ps);
}

/* ── jednorazove deje (v0.36.0, FW 0x0501, docs/PROTOKOL_V3.md kap. 10.5) ────────────────────────────────────────────
 * Udalosti ze zaznamu (fpga_v3_ev_from_rec) bezi jen pri zapnuti (UART `fpga ev on`, `fpga ab on`, `fpga pw on|cal`),
 * cele ve FpgaTasku (s3_take_records). UART zada vynulovani pocitadlem generace (kazda promenna ma jednoho zapisovatele)
 * a cte kopie pod zakazem preruseni.
 *   * kruh 256 udalosti s casem proti posledni 1PPS z hlavicky (tataz osa FPGA; PTS FW 0x0500 = hruby cas -> 10 ns),
 *   * parovani (fpga_v3_ab_feed): udalosti A segmentu se drzi do zaznamu B tehoz segmentu (FPGA posila A, pak B);
 *     zaznam neplatny / se znackami pri CS = hrana mohla chybet -> rozpracovany start pryc,
 *   * po prepnuti zdroje B (SET_CONFIG 0x09) se paruje az za drzenim B (FLAGS bit0) -- starsi zaznamy jsou z puvodniho
 *     zdroje. */
#define S3E_RING   256u
#define S3E_AB     64u
#define S3E_MAX_S  10.0                 /* start bez stopu do 10 s se zahodi */
static fpga_ev_rec_t  s3e_ring[S3E_RING];
static uint32_t       s3e_w = 0u;       /* udalosti celkem (index zapisu) */
static double         s3e_ab[S3E_AB];
static uint32_t       s3e_abw = 0u;
static fpga_v3_evu_t  s3e_u;
static fpga_v3_pr_t   s3e_pr;           /* parovani po zaznamech (fpga_v3_pr_rec, host test) */
static fpga_v3_stat_t s3e_st;
static uint8_t        s3e_act = 0u;
static volatile uint8_t s3e_on = 0u, s3e_pair = FPGA_EVP_OFF;   /* zapisuje UART */
static volatile uint8_t s3e_gen[4];     /* UART: vynulovat kruh / intervaly / zpozdeni, prepnuti zdroje B */
static uint8_t        s3e_seen[4];
static uint8_t        s3e_bsrc = 0u;    /* posledni odeslany SET_CONFIG 0x09 */

/* rozbaleny cas [T/16384] -> ps (cele sekundy zvlast, bez preteceni 64 b) */
static uint64_t s3e_ps(uint64_t t_u, uint32_t clk)
{
    const uint64_t upu = (uint64_t)clk * 16384u, ups = upu * 1000000u;
    return (t_u / ups) * 1000000000000ull + ((t_u % ups) * 1000000u) / upu;
}
static int64_t s3e_s48(uint64_t d)
{
    d &= 0xFFFFFFFFFFFFull;
    return (d & (1ull << 47)) ? (int64_t)d - (int64_t)(1ull << 48) : (int64_t)d;
}

static void s3e_apply(uint32_t clk)
{
    const uint8_t act = (uint8_t)(s3e_on || s3e_pair != FPGA_EVP_OFF);
    const uint64_t max_u = (uint64_t)(S3E_MAX_S * 1e12 / fpga_v3_unit_ps(clk));
    uint32_t pm;
    if (act && !s3e_act) {
        fpga_v3_evu_init(&s3e_u);
        pm = __get_PRIMASK(); __disable_irq(); fpga_v3_pr_init(&s3e_pr, max_u); __set_PRIMASK(pm);
    }
    s3e_act = act;
    if (s3e_gen[0] != s3e_seen[0]) {
        s3e_seen[0] = s3e_gen[0];
        pm = __get_PRIMASK(); __disable_irq(); s3e_w = 0u; __set_PRIMASK(pm);
    }
    if (s3e_gen[1] != s3e_seen[1]) {
        s3e_seen[1] = s3e_gen[1];
        pm = __get_PRIMASK(); __disable_irq();
        fpga_v3_ab_init(&s3e_pr.ab, max_u); s3e_pr.pa_have = 0u; fpga_v3_stat_init(&s3e_st); s3e_abw = 0u;
        __set_PRIMASK(pm);
    }
    if (s3e_gen[2] != s3e_seen[2]) {
        s3e_seen[2] = s3e_gen[2];
        pm = __get_PRIMASK(); __disable_irq(); fpga_v3_stat_init(&s3e_pr.sk); __set_PRIMASK(pm);
    }
    if (s3e_gen[3] != s3e_seen[3]) {
        s3e_seen[3] = s3e_gen[3];
        pm = __get_PRIMASK(); __disable_irq(); fpga_v3_pr_switch(&s3e_pr); __set_PRIMASK(pm);
    }
}

static void s3e_push(const fpga_v3_ev_t *e, uint8_t fl, uint32_t clk, double up)
{
    fpga_ev_rec_t o;
    memset(&o, 0, sizeof o);
    o.t_ps = s3e_ps(e->t_u, clk);
    o.per_ps = e->per_u * up;
    o.n = e->n; o.seg = e->seg; o.ch = e->ch; o.kind = e->kind; o.flags = fl;
    for (int i = 0; i < 2; i++) {                       /* 1PPS pred hranou: [0] posledni, jinak [1] predchozi */
        if (!(s3_h.pps[i].pflags & 1u)) continue;
        const int64_t d = s3e_s48(e->t_u - s3_h.pps[i].pts);
        o.pps_ps = (int64_t)((double)d * up); o.pseq = s3_h.pps[i].pseq; o.pps_ok = 1u;
        if (d >= 0) break;
    }
    const uint32_t pm = __get_PRIMASK();
    __disable_irq();
    s3e_ring[s3e_w % S3E_RING] = o; s3e_w++;
    __set_PRIMASK(pm);
}

static void s3e_rec(const fpga_v3_rec_t *r, uint32_t clk, uint32_t now)
{
    const double up = fpga_v3_unit_ps(clk);
    fpga_v3_ev_t e[2];
    const uint32_t ne = fpga_v3_ev_from_rec(&s3e_u, r, clk, now, e);
    if (s3e_on) for (uint32_t k = 0; k < ne; k++) s3e_push(&e[k], r->flags, clk, up);
    const uint8_t pr = s3e_pair;
    if (pr == FPGA_EVP_OFF) return;
    int64_t dt[4];
    const uint32_t pm = __get_PRIMASK();
    __disable_irq();                                    /* UART cte s3e_pr.ab.lost a s3e_pr.sk */
    const uint32_t nd = fpga_v3_pr_rec(&s3e_pr, r, e, ne, pr == FPGA_EVP_SKEW, up, dt, 4u);
    for (uint32_t k = 0; k < nd; k++) {
        const double ps = (double)dt[k] * up;
        fpga_v3_stat_add(&s3e_st, ps);
        s3e_ab[s3e_abw % S3E_AB] = ps; s3e_abw++;
    }
    __set_PRIMASK(pm);
}

/* Prijaty ramec DATA: nove zaznamy -> okna -> fronta */
static void s3_take_records(uint32_t n_ok)
{
    uint32_t i0 = 0u; int gap = 0;
    const uint32_t n = fpga_v3_link_take(&s3_link, &s3_h, n_ok, &i0, &gap);
    if (s3_h.seg_len_ms && s3_h.seg_len_ms != s3_grp.seg_ms) s3_regroup();   /* zmena L: okna znovu */
    if (gap) { fpga_v3_grp_break(&s3_grp, 0u); fpga_v3_grp_break(&s3_grp, 1u); s3_wb_ok = 0u; s3_brk = 1u; }
    const uint32_t clk = s3_h.clk_mhz ? s3_h.clk_mhz : 100u;
    s3e_apply(clk);
    if (gap) { const uint32_t pm = __get_PRIMASK(); __disable_irq(); fpga_v3_pr_gap(&s3e_pr); __set_PRIMASK(pm); }
    const uint32_t now = HAL_GetTick();
    for (uint32_t i = 0; i < n; i++) {
        if (s3e_act) s3e_rec(&s3_rec[i0 + i], clk, now);
        fpga_v3_win_t w;
        if (!fpga_v3_grp_feed(&s3_grp, &s3_rec[i0 + i], clk, &w)) continue;
        if (w.ch != (s3_chan & 1u)) { s3_wb = w; s3_wb_ok = 1u; continue; }
        if ((uint8_t)(s3_qw - s3_qr) >= S3_Q) { s3_qr++; s3_qdrop++; s3_brk = 1u; }   /* fronta plna: nejstarsi pryc */
        s3_win_to_meas(&w, &s3_q[s3_qw % S3_Q]);
        s3_qw++; s3_wins++;
        s3_last_win_ms = HAL_GetTick();
    }
}

/* ── emulator FPGA v3 (fpgasim v3): ramec skladany PO transakci, ACK zpracovany az po slozeni (jako RTL, kap. 6.1) ── */
static fpga_v3_emu_t s3_emu;
static uint8_t  s3_emu_f[FPGA_V3_LEN];
static uint32_t s3_emu_ms = 0u, s3_emu_rem = 0u;
static double   s3_emu_hz = 0.0, s3_emu_hzb = 0.0;
static void sim3_produce(const uint8_t *tx, uint8_t *rx)
{
    memcpy(rx, s3_emu_f, FPGA_V3_LEN);
    const uint32_t now = HAL_GetTick();
    uint32_t el = (now - s3_emu_ms) + s3_emu_rem;
    s3_emu_ms = now;
    uint32_t nseg = el / s3_emu.seg_ms;
    s3_emu_rem = el % s3_emu.seg_ms;
    if (nseg > 2u * FPGA_V3_EMU_RING) nseg = 2u * FPGA_V3_EMU_RING;   /* dlouha pauza: kruh stejne pretece */
    /* kmitocet A s driftem jako v2 emulator (ppb/h), B pevne */
    const double h = (double)(now - s_sim_t0) / 3600000.0;
    s3_emu.f_hz[0] = s3_emu_hz * (1.0 + (double)s_sim_drift * 1e-9 * h);
    for (uint32_t i = 0; i < nseg; i++) fpga_v3_emu_segment(&s3_emu);
    fpga_v3_emu_frame(&s3_emu, s3_emu_f);
    /* 1PPS z emulatoru v2 (smycka GPSDO ladi OCXO podle kodu DAC): poradi + hruby cas do hlavicky, blok 0 znovu zapecetit */
    sim_pps_step(now);
    for (int i = 0; i < 4; i++) s3_emu_f[68 + i] = (uint8_t)(s_sim_pps_seq >> (8 * i));
    for (int i = 0; i < 4; i++) s3_emu_f[100 + i] = (uint8_t)(s_sim_pps_ts >> (8 * i));
    {   const uint64_t pts = s_sim_pps_pts;                     /* v0.37.0: jemny cas jako FW 0x0502 */
        for (int i = 0; i < 6; i++) s3_emu_f[72 + i] = (uint8_t)(pts >> (8 * i)); }
    s3_emu_f[78] = s_sim_pps_seq ? (uint8_t)(FPGA_V3_PF_VALID | FPGA_V3_PF_FINE) : 0u;
    s3_emu_f[79] = 0u;                                          /* d */
    s3_emu_f[50] = (uint8_t)(0x10u | FPGA_V3_PPS_ST_LOCK | (s_sim_pps_seq ? 0x02u : 0u));   /* epocha 1, PCLK bezi */
    s3_emu_f[26] |= (uint8_t)(FPGA_V3_CAP_PPS_IDES >> 16);      /* CAPS bit17 */
    fpga_v3_blk_seal(s3_emu_f);
    fpga_v3_emu_rx(&s3_emu, tx);
}

void fpga_sim_v3_set(int on, double hz_a, double hz_b, double ph_b_s)
{
    if (!on) { s_sim_v3 = 0u; s_sim_on = 0u; s_proto = 0u; return; }
    s3_emu_hz  = (hz_a > 0.0) ? hz_a : 10e6;
    s3_emu_hzb = (hz_b > 0.0) ? hz_b : 0.0;
    fpga_v3_emu_init(&s3_emu, s3_emu_hz, s3_emu_hzb, 30.0, HAL_GetTick() | 1u);
    if (ph_b_s > 0.0) fpga_v3_emu_set_phase(&s3_emu, 1, s3_emu.t_next[0] + ph_b_s);   /* B = A + faze (jednorazove deje) */
    s3_emu_ms = HAL_GetTick(); s3_emu_rem = 0u;
    fpga_v3_emu_frame(&s3_emu, s3_emu_f);
    s_sim_t0 = HAL_GetTick(); s_sim_pps_next_ms = s_sim_t0 + 1000u; s_sim_nopps = 0u;
    s_sim_on = 1u; s_sim_v3 = 1u; s_proto = 3u;
    s3_reset();
}
int fpga_sim_v3_active(void) { return s_sim_on && s_sim_v3; }

/* Jedna transakce v3 ve FpgaTasku: ACK + prijem; vrati true, kdyz je k dispozici okno vybraneho kanalu */
static bool poll_v3(fpga_meas_t *out)
{
    if (s3_qr == s3_qw && (uint32_t)(HAL_GetTick() - s3_xfer_ms) >= S3_POLL_MS) {
        s3_xfer_ms = HAL_GetTick();
        fpga_v3_build_ack(s3_tx, ++s3_mseq, s3_link.expect);
        if (s_sim_on) { sim3_produce(s3_tx, s3_rx); g_xfer_ok = 1u; }
        else if (!xfer3(s3_tx, s3_rx)) { g_xfer_ok = 0u; s_link_ok = 0u; return false; }
        else g_xfer_ok = 1u;
        g_last_rx0 = s3_rx[0];
        uint32_t n_ok = 0u;
        const int rc = fpga_v3_parse(s3_rx, &s3_h, s3_rec, &n_ok);
        if (rc != FPGA_V3_OK) {
            if (rc == FPGA_V3_E_CRC0) { g_rx_crc++; g_rx_crc_last_ms = HAL_GetTick(); }
            s_link_ok = 0u;
            return false;
        }
        if (s3_h.blk_ok != 0x0Fu) { g_rx_crc++; g_rx_crc_last_ms = HAL_GetTick(); }   /* vadny blok 1-3: zaznamy prijdou znovu */
        s3_h_ok = 1u; s_link_ok = 1u; s3_frames++;
        if (s3_h.type == FPGA_V3_T_DATA) s3_take_records(n_ok);
        s3_range_ctl(&s3_h);                                       /* v0.38.0: automaticka predelicka */
        {   /* diagnosticky snimek ramce (verze, hodiny, TDC, tabulka, 1PPS) i bez noveho okna */
            fpga_meas_t f;
            memset(&f, 0, sizeof f);
            uint32_t pm = __get_PRIMASK();
            __disable_irq();
            if (s_frame_seen) f = s_frame;
            __set_PRIMASK(pm);
            s3_hdr_to_meas(&s3_h, &f);
            pps_feed(&f);
            pm = __get_PRIMASK();
            __disable_irq();
            s_frame = f; s_frame_seen = 1u;
            __set_PRIMASK(pm);
        }
    }
    const uint32_t now = HAL_GetTick();
    if (s3_qr == s3_qw) {
        /* ztrata signalu vybraneho kanalu: zadne okno dele nez 2,5 s + hradlo -> SIGNAL_LOST (jako watchdog v2) */
        if (!s3_lost && (now - s3_last_win_ms) > S3_LOST_MS + fpga_gate_idx_ms(s3_gate)) {
            s3_lost = 1u; s3_brk = 1u;
            uint32_t pm = __get_PRIMASK();
            __disable_irq();
            if (s_last_seen) { s_last.error_flags |= FPGA_ERR_SIGNAL_LOST; s_last.measurement_status &= (uint8_t)~0x01u; }
            __set_PRIMASK(pm);
        }
        return false;
    }
    fpga_meas_t m = s3_q[s3_qr % S3_Q];
    s3_qr++;
    s3_lost = 0u;
    /* samokontrola (jako v2): hodiny FPGA, koherence 100/10 MHz, mez primeho mereni, kontrolni pocitani hran segmentu */
    int chk = FPGA_CHK_OK;
    {
        const int loss_new = (s_prev_loss != 0xFFFFu) && (m.clk_loss > s_prev_loss);
        s_prev_loss = m.clk_loss;
        if (!(m.clk_status & FPGA_CLK_OK) || (m.clk_status & FPGA_CLK_RECOVER) || loss_new) chk = FPGA_CHK_CLOCK;
        const int coh_new = (s_prev_coh != 0xFFFFu) && (m.coh_cnt > s_prev_coh);
        s_prev_coh = m.coh_cnt;
        if (chk == FPGA_CHK_OK && coh_new && !(m.clk_status & 0x08u)) chk = FPGA_CHK_COH;
        /* v0.38.0: s predelickou je edge_count v periodach -> mez primeho mereni plati pro DELENY kmitocet */
        if (chk == FPGA_CHK_OK && !s_sim_on && m.gate2_ps > 0u &&
            (double)m.edge_count * 1e12 / (double)m.gate2_ps / (double)(1u << (m.presc_log2 & 7u)) > FPGA_DIRECT_MAX_HZ)
            chk = FPGA_CHK_FMAX;
        /* v0.38.0: pojistka rozsahu (PROTOKOL_V3 kap. 6.4) -- sonda /16 vidi skutecny kmitocet i tam, kde deleny signal
         * prime pocitani i kontrolni pocitani stejne podvzorkuji (shodny, ale spatny pocet). Dokud automaticky rozsah
         * nepreleze na vetsi P, okno se nebere. */
        if (chk == FPGA_CHK_OK && !s_sim_on && s3_h_ok && (s3_h.caps & FPGA_V3_CAP_PRESC) &&
            FPGA_V3_PROBE_HZ(s3_h.probe[s3_chan & 1u]) / (double)(1u << (m.presc_log2 & 7u)) > FPGA_DIRECT_MAX_HZ)
            chk = FPGA_CHK_FMAX;
        if (chk == FPGA_CHK_OK && (m.ccd_p > 1 || m.ccd_p < -1)) chk = FPGA_CHK_COUNT;
        if (chk == FPGA_CHK_COUNT)       s_chk_count++;
        else if (chk == FPGA_CHK_CLOCK)  s_chk_clock++;
        else if (chk == FPGA_CHK_FMAX)   s_chk_fmax++;
        else if (chk == FPGA_CHK_COH)    s_chk_coh++;
    }
    s_dbg_frames++; s_dbg_valid++; s_dbg_new++;
    const uint32_t gap = fpga_seq_gap(g_last_seq, m.sequence);
    fpga_phrec_feed(&m, gap, (chk == FPGA_CHK_OK) ? 1 : 0);
#if FPGA_STATS_USE_CC
    if (m.cc_ok && m.gate_cc_ps > 0u) m.gate_ps = m.gate_cc_ps;
#endif
    {
        uint32_t pm = __get_PRIMASK();
        __disable_irq();
        s_frame = m; s_frame_seen = 1u;
        if (chk == FPGA_CHK_OK) { s_last = m; s_last_seen = 1u; }
        __set_PRIMASK(pm);
    }
    s_poll_mc  = 0;
    s_poll_chk = chk;
    s_poll_gap = gap;
    if (gap == FPGA_SEQ_RESYNC) s_seq_resync++;
    else if (gap != 0u)        { s_seq_gaps++; s_seq_missed += gap; }
    if (out) *out = m;
    g_last_seq = m.sequence;
    return true;
}

static bool v3_cfg(uint8_t id, uint8_t v1, uint8_t v2);
static bool v3_page(uint8_t kind, uint16_t page, uint32_t *items, uint16_t *n);

bool fpga_freq_poll(fpga_meas_t *out)
{
    static uint8_t tx[FR_LEN], rx[FR_LEN];

    if (!g_init_ok) return false;   /* selftest neprosel -> neposilat nic (kontrakt 7.1) */
    /* protokol v3 (krok 4c): verzi FPGA zjisti prvni transakce 1024 B; v3 ma vlastni vetev, v2 pokracuje nize beze zmeny */
    if (s_sim_on && s_sim_v3) return poll_v3(out);
    if (!s_sim_on && s_proto == 0u) {
        s_proto = proto_detect();
        if (s_proto == 0u) { s_link_ok = 0u; return false; }
        printf("fpga: protokol v%u\n", (unsigned)s_proto);
        if (s_proto == 3u) s3_reset();
    }
    if (!s_sim_on && s_proto == 3u) return poll_v3(out);

    /* ⚠️ JEDINY rozdil emulace: odkud se vezme obsah `rx`. VSE nize (MAGIC, CRC,
     * TYPE, parse_data, latch, VALID/FRESH/SEQ) bezi bez zmeny — prave proto je
     * emulace uzitecna. Na SPI se pritom NESAHA, takze bezici emulace nemuze
     * rozhodit pripadnou skutecnou FPGA na sbernici. */
    if (s_sim_on) {
        sim_produce(rx);
        g_xfer_ok = 1;
    } else {
        /* ACK posledni potvrzene seq (potvrdi predchozi + vyclockuje aktualni ramec) */
        build_frame(TYPE_ACK, g_last_seq, NULL, 0, tx);
        if (!xfer(tx, rx)) { g_xfer_ok = 0; s_link_ok = 0; return false; }
        g_xfer_ok = 1;
    }
    g_last_rx0 = rx[0];                                /* diagnostika: co prislo na MISO */

    if (rx[0] != FR_MAGIC) { s_link_ok = 0; return false; }

    uint16_t crc_rx   = (uint16_t)rx[126] | ((uint16_t)rx[127] << 8);
    if (!fpga_crc16_check(rx, FR_CRC_LEN, crc_rx)) { g_rx_crc++; g_rx_crc_last_ms = HAL_GetTick(); s_link_ok = 0; return false; }

    s_link_ok = 1;   /* platny ramec dorazil -> link je ziva (i kdyz neni nove mereni) */

    uint8_t type   = rx[2];
    uint8_t status = rx[3];
    if (type != TYPE_DATA) return false;

    /* Latch KAZDEHO platneho DATA ramce (i kdyz neni fresh/valid) -> STM vidi
     * error_flags/SIGNAL_LOST/phase_status i pri ztrate signalu (kdy VALID=0).
     * Parse do lokalu + kopie pod kratkym IRQ-off: s_last cte i UiTask pres
     * fpga_freq_get_last() (okno Citac) — bez toho by mohl videt roztrzenou
     * `fpga_meas_t` strukturu (vic nez jedna 32bit operace, velikost neni
     * dulezita — roste s v2 poli, viz fpga_freq.h). */
    fpga_meas_t tmp;
    parse_data(rx, &tmp);
    pps_feed(&tmp);   /* 1PPS nezavisi na mereni (VALID/FRESH) -> kazdy platny DATA ramec */

    /* Chybne napocitane okno (hrana navic/chybi, metastabilita detekce hrany
     * ve FPGA): rozhodne se PRED latchem. Pri 10 MHz / 0,25 s = skok +-4 Hz.
     * Pojistka: 3 takova okna za sebou uz neni porucha citani, ale skutecna
     * zmena signalu -> prijmout a vzit za novou referenci. */
    int is_new = (status & ST_DATA_VALID) && (status & ST_DATA_FRESH)
                 && tmp.sequence != g_last_seq;
    s_dbg_frames++;
    if (status & ST_DATA_VALID) s_dbg_valid++;
    if (is_new) s_dbg_new++;
    /* Zmena KANALU (A <-> B): okno jineho signalu nema s referenci minuleho nic spolecneho, jinak by
     * prvni az tri okna po prepnuti byla vyhlasena za chybne napocitana (`fpga_freq_miscount`). */
    if ((tmp.caps & FPGA_CAP_CHAN) && tmp.channel_id != s_prev_chan) {
        s_prev_chan = tmp.channel_id; s_mc_ref = 0u; s_mc_run = 0u;
    }
    /* Samokontrola okna (STATUS #283) -- PRED miscountem: okno, ktere neprojde, se nesmi stat ani referenci.
     * Pocet vypadku 100 MHz se sleduje v KAZDEM ramci (ramce chodi castji nez mereni), takze se vypadek
     * pripise prvnimu mereni, ktere ho ma za sebou. Echo hradla se meni az s merenim (spi_app latchuje
     * phase_status s novym merenim), proto se porovnava jen mezi merenimi: okno, behem ktereho se hradlo
     * zmenilo, ma legitimne jinou delku a neposuzuje se. */
    int chk = FPGA_CHK_OK;
    if (tmp.caps & FPGA_CAP_SELFCHK) {
        /* jen NARUST: pokles = nove nahrana FPGA (pocitadlo zacina od 0), to neni vypadek */
        int loss_new = (s_prev_loss != 0xFFFFu) && (tmp.clk_loss > s_prev_loss);
        s_prev_loss = tmp.clk_loss;
        if (is_new && (!(tmp.clk_status & FPGA_CLK_OK) || (tmp.clk_status & FPGA_CLK_RECOVER) || loss_new))
            chk = FPGA_CHK_CLOCK;
    }
    /* FW >= 0x0425: prisny hlidac koherence -- jen NARUST citace (nova FPGA zacina od 0) */
    if (tmp.diag0_ok) {
        int coh_new = (s_prev_coh != 0xFFFFu) && (tmp.coh_cnt > s_prev_coh);
        s_prev_coh = tmp.coh_cnt;
        /* audit 4: pri externi referenci (clk_status bit3) nejsou 100 MHz a REF_10MHz koherentni -- FPGA 0x0425 hlidac vypina,
         * STM pro jistotu chybu koherence v tom rezimu ignoruje */
        if (is_new && chk == FPGA_CHK_OK && coh_new && !(tmp.clk_status & 0x08u)) chk = FPGA_CHK_COH;
    }
    /* Audit 2026-10-20: kmitocet nad mezi primeho mereni = okno muze mit spatny pocet period bez priznaku -> odmitnout */
    /* Kriticka analyza 2: NE pro emulator (`fpgasim on 1400000000` testuje formatovani do GHz) */
    if (is_new && chk == FPGA_CHK_OK && !s_sim_on && (tmp.caps & FPGA_CAP_DT) && tmp.gate2_ps > 0u && tmp.edge_count > 0u) {
        double fmax = (tmp.fw_version >= 0x0425u) ? FPGA_DIRECT_MAX_HZ : FPGA_DIRECT_MAX_HZ_OLD;
        double f2 = (double)tmp.edge_count * 1e12 / (double)tmp.gate2_ps;
        if (f2 > fmax) chk = FPGA_CHK_FMAX;
    }
    /* Kriticka analyza 2: totez pro SEKUNDARNI slot (druhy kanal) -- jinak by B nad mezi ukazoval spatny kmitocet bez priznaku.
     * Primarni mereni zustava platne, sekundarni slot se zneplatni (freq16 = 0, edges_b = 0). */
    if (!s_sim_on && (tmp.caps & FPGA_CAP_DT) && tmp.dt_b_ps > 0u && tmp.edges_b > 0u) {
        double fmax = (tmp.fw_version >= 0x0425u) ? FPGA_DIRECT_MAX_HZ : FPGA_DIRECT_MAX_HZ_OLD;
        double fb = (double)tmp.edges_b * 1e12 / (double)tmp.dt_b_ps;
        if (fb > fmax) { tmp.freq16_x100000 = 0u; tmp.edges_b = 0u; if (is_new) s_chk_fmax_b++; }
    }
    if (is_new) {
        uint8_t win = (tmp.caps & FPGA_CAP_CHAN) ? (uint8_t)(tmp.phase_status & 7u) : 0xFEu;
        int win_changed = (win != s_prev_win);
        s_prev_win = win;
        if (chk == FPGA_CHK_OK && (tmp.measurement_status & 0x01u) && !(tmp.error_flags & FPGA_ERR_SIGNAL_LOST)) {
            if ((tmp.caps & FPGA_CAP_SELFCHK) && (tmp.ccd_p > 1 || tmp.ccd_p < -1))
                chk = FPGA_CHK_COUNT;
            else if ((tmp.caps & FPGA_CAP_CHAN) && (tmp.caps & FPGA_CAP_DT) && !win_changed)
                chk = fpga_freq_window_check(tmp.edge_count, tmp.gate2_ps, fpga_win_code_ps(win));
        }
        if (chk == FPGA_CHK_COUNT || chk == FPGA_CHK_WINDOW) {
            /* 2026-10-21: jedno okno zamitnute pocitanim se objevuje po KAZDEM startu (i FW 0x0424, data 2026-10-18) a pak uz
             * neprirusta -> pamatuj kdy (poradi mereni od bootu, cas, SEQ, rozdil), "FPGA MERI SPATNE" jen za zamitnuti po rozbehu */
            s_chk_last_ms = HAL_GetTick(); s_chk_last_seq = tmp.sequence; s_chk_last_ccd = tmp.ccd_p; s_chk_last_nth = s_dbg_new;
            if (s_dbg_new > FPGA_CHK_STARTUP_MEAS) s_chk_late++;
        }
        if (chk == FPGA_CHK_COUNT)       s_chk_count++;
        else if (chk == FPGA_CHK_CLOCK)  s_chk_clock++;
        else if (chk == FPGA_CHK_WINDOW) s_chk_window++;
        else if (chk == FPGA_CHK_FMAX)   s_chk_fmax++;
        else if (chk == FPGA_CHK_COH)    s_chk_coh++;
    }

    int mc = 0;
    if (chk == FPGA_CHK_OK && is_new && (tmp.measurement_status & 0x01u)
        && !(tmp.error_flags & FPGA_ERR_SIGNAL_LOST)) {
        uint32_t mul = fpga_freq_hires_mul(tmp.frequency_x100000, tmp.edge_count, tmp.gate_ps);
        uint64_t uhz = fpga_freq_hires_uhz(tmp.frequency_x100000, tmp.edge_count, tmp.gate_ps);
        mc = (s_mc_run < 3u) ? fpga_freq_miscount(uhz, s_mc_ref, tmp.gate_ps, mul) : 0;
        if (mc != 0) {
            s_mc_run++;
            if (mc > 0) s_mc_pos++; else s_mc_neg++;
        } else {
            s_mc_run = 0u;
            if (uhz > 0u && mul != 0u) s_mc_ref = uhz;
        }
    } else if ((is_new && chk == FPGA_CHK_OK) || (tmp.error_flags & FPGA_ERR_SIGNAL_LOST)) {
        s_mc_ref = 0u; s_mc_run = 0u;                  /* bez platneho mereni -> bez reference */
    }

    /* Odmitnute mereni (miscount nebo samokontrola) se nesmi dostat do `s_last` ani POZDEJI: FPGA tentyz
     * vysledek posila v kazdem dalsim ramci (stejna SEQUENCE, uz bez FRESH), az do dalsiho mereni. Driv se
     * tak odmitnute okno latchlo hned v pristim pollu (~25 ms) a videl ho web, SCPI i okna UI.
     * Diagnostika ramce (verze, identita, hlidac hodin, TDC) se proto drzi zvlast v `s_frame`. */
    if (is_new && (mc != 0 || chk != FPGA_CHK_OK)) { s_rej_seq = tmp.sequence; s_rej_on = 1u; }
    else if (is_new)                                 s_rej_on = 0u;
    /* Audit 2026-10-20 (E1): fazovy zaznam (kazde NOVE mereni; dira v SEQ, odmitnuti nebo okno bez regrese usek prerusi) */
    if (is_new) {
        fpga_phrec_feed(&tmp, fpga_seq_gap(g_last_seq, tmp.sequence), (mc == 0 && chk == FPGA_CHK_OK) ? 1 : 0);
#if FPGA_STATS_USE_CC
        if (tmp.cc_ok && tmp.gate_cc_ps > 0u) tmp.gate_ps = tmp.gate_cc_ps;
#endif
    }
    {
        uint32_t pm = __get_PRIMASK();
        __disable_irq();
        s_frame = tmp;
        s_frame_seen = 1;
        __set_PRIMASK(pm);
    }
    if (!(s_rej_on && tmp.sequence == s_rej_seq)) {
        uint32_t pm = __get_PRIMASK();
        __disable_irq();
        s_last = tmp;
        s_last_seen = 1;
        __set_PRIMASK(pm);
    }

    if (!is_new) return false;
    s_poll_mc  = mc;
    s_poll_chk = chk;

    /* F-0193: NOVE nestaci — musi i NAVAZOVAT. Dira = mereni, ktere FPGA
     * prepsala drive, nez jsme ho precetli; spocitat a ohlasit volajicimu. */
    s_poll_gap = fpga_seq_gap(g_last_seq, tmp.sequence);
    if (mc == 0 && tmp.codes_ok && (tmp.measurement_status & 0x01u) && !(tmp.error_flags & FPGA_ERR_SIGNAL_LOST)) {
        fpga_inl_rec_t *r = &s_inl[s_inl_w % FPGA_INL_N];
        r->seq = tmp.sequence; r->edges = (uint32_t)tmp.edge_count; r->gate_ps = tmp.gate_ps;
        r->code_end = tmp.code_a_end; r->code_st = tmp.code_a_st;
        r->gap = (s_poll_gap != 0u) ? 1u : 0u; r->pad = 0u;
        s_inl_w++;
    }
    if (s_poll_gap == FPGA_SEQ_RESYNC)  s_seq_resync++;
    else if (s_poll_gap != 0u)        { s_seq_gaps++; s_seq_missed += s_poll_gap; }

    if (out) *out = tmp;
    g_last_seq = tmp.sequence;                          /* dalsi ACK potvrdi tuto seq */
    return true;
}

int  fpga_freq_poll_miscount(void) { return s_poll_mc; }

void fpga_freq_miscount_stats(uint32_t *pos, uint32_t *neg)
{
    if (pos) *pos = s_mc_pos;
    if (neg) *neg = s_mc_neg;
}

uint32_t fpga_seq_gap(uint32_t prev, uint32_t cur)
{
    if (prev == 0xFFFFFFFFu) return 0u;                /* prvni mereni po bootu */
    uint32_t d = cur - prev;                           /* modulo 2^32 */
    if (d == 1u) return 0u;
    if (d >= 2u && d <= FPGA_SEQ_GAP_MAX) return d - 1u;
    return FPGA_SEQ_RESYNC;                            /* velky skok nebo navrat zpet */
}

uint32_t fpga_freq_seq_gap(void) { return s_poll_gap; }

void fpga_freq_seq_stats(uint32_t *gaps, uint32_t *missed, uint32_t *resync)
{
    if (gaps)   *gaps   = s_seq_gaps;
    if (missed) *missed = s_seq_missed;
    if (resync) *resync = s_seq_resync;
}

int fpga_freq_window_check(uint64_t edges, uint64_t dt_ps, uint64_t gate_ps)
{
    if (edges < 2u || dt_ps == 0u || gate_ps == 0u) return FPGA_CHK_OK;
    uint64_t per = dt_ps / edges;                       /* perioda signalu [ps] */
    if (per * 2u > gate_ps) return FPGA_CHK_OK;         /* < 2 hran na hradlo: okno se protahuje legitimne */
    uint64_t tol = 3u * per + FPGA_WIN_TOL_PS;
    uint64_t d = (dt_ps > gate_ps) ? dt_ps - gate_ps : gate_ps - dt_ps;
    return (d <= tol) ? FPGA_CHK_OK : FPGA_CHK_WINDOW;
}

uint64_t fpga_win_code_ps(uint8_t win)
{
    switch (win) {
    case FPGA_WIN_50MS:  return  50000000000ull;
    case FPGA_WIN_100MS: return 100000000000ull;
    case FPGA_WIN_250MS: return 250000000000ull;
    case FPGA_WIN_500MS: return 500000000000ull;
    case FPGA_WIN_1S:    return 1000000000000ull;
    default:             return 0u;
    }
}

int fpga_freq_poll_check(void) { return s_poll_chk; }

void fpga_freq_diag_fill(uint32_t *w, uint32_t n)
{
    fpga_meas_t f;
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    f = s_frame;
    __set_PRIMASK(pm);
    uint32_t v[40] = {0};
    v[0]  = (uint32_t)f.fw_version | ((uint32_t)f.caps << 16);
    v[1]  = (uint32_t)f.clk_status | ((uint32_t)f.clk_loss << 8) | ((uint32_t)f.clk_mhz << 16) | ((uint32_t)f.tdc_status << 24);
    v[2]  = (uint32_t)f.channel_id | ((uint32_t)f.measurement_status << 8) | ((uint32_t)f.status_flags << 16) | ((uint32_t)f.phase_status << 24);
    v[3]  = f.error_flags;
    v[4]  = f.sequence;
    v[5]  = (uint32_t)f.edge_count;
    v[6]  = (uint32_t)f.gate_ps;  v[7]  = (uint32_t)(f.gate_ps >> 32);
    v[8]  = (uint32_t)f.gate2_ps; v[9]  = (uint32_t)(f.gate2_ps >> 32);
    v[10] = (uint32_t)f.dt_b_ps;  v[11] = (uint32_t)(f.dt_b_ps >> 32);
    v[12] = f.edges_b;
    v[13] = (uint32_t)(uint8_t)f.ccd_p | ((uint32_t)(uint8_t)f.ccd_s << 8) | ((uint32_t)f.rg_ok << 16) | ((uint32_t)f.regr_used << 24);
    v[14] = f.rg_n[0]; v[15] = f.rg_n[1];
    v[16] = s_chk_count; v[17] = s_chk_clock; v[18] = s_chk_window;
    v[19] = s_mc_pos;    v[20] = s_mc_neg;
    v[21] = (uint32_t)s_rej_on | ((uint32_t)(uint8_t)s_poll_chk << 8) | ((uint32_t)s_link_ok << 16) | ((uint32_t)g_last_rx0 << 24);
    v[22] = g_rx_crc;  v[23] = s_seq_gaps;
    v[24] = s_dbg_frames; v[25] = s_dbg_new; v[26] = s_dbg_valid;
    v[27] = s_rej_seq; v[28] = (uint32_t)s_frame_seen | ((uint32_t)s_last_seen << 8) | ((uint32_t)s_cfg_sent_chan << 16) | ((uint32_t)s_cfg_sent_win << 24);
    v[29] = (uint32_t)(uint16_t)f.code_a_end | ((uint32_t)(uint16_t)f.code_a_st << 16);
    v[30] = (uint32_t)f.rg_ym[0]; v[31] = (uint32_t)(f.rg_ym[0] >> 32);
    v[32] = (uint32_t)f.rg_ym[1]; v[33] = (uint32_t)(f.rg_ym[1] >> 32);
    v[34] = (uint32_t)f.rg_xm[0]; v[35] = (uint32_t)f.rg_xm[1];
    /* Diagnostika I2C1 (2026-10-11, test 60/200/400 kHz): aktualni TIMINGR, ADS1115 vzorky a chyby (soucet AIN0..3), chyby TMP117 0x49, chyby Si5356 */
    {
      extern volatile uint32_t g_si5356_err_cnt;
      extern volatile int16_t g_ads_raw_last[4];
      uint32_t ads_s = 0, ads_e = 0;
      for (int k = SENS_ADS0; k <= SENS_ADS3; k++) { ads_s += g_sensors[k].samples; ads_e += g_sensors[k].err_total; }
      v[36] = hi2c1.Instance->TIMINGR;
      (void)ads_s;
      v[37] = ((uint32_t)(uint16_t)g_ads_raw_last[1]) | ((uint32_t)(uint16_t)g_ads_raw_last[0] << 16);   /* surove AIN1 | AIN0 */
      (void)ads_e;
      /* chyby po kanalech AIN0..AIN3 (kazda saturuje na 255) */
      v[38] = 0;
      for (int k = 0; k < 4; k++) { uint32_t e = g_sensors[SENS_ADS0 + k].err_total; if (e > 255u) e = 255u; v[38] |= e << (8 * k); }
      v[39] = (g_sensors[SENS_T49].err_total & 0xFFFFu) | ((g_si5356_err_cnt & 0xFFFFu) << 16);
    }
    for (uint32_t i = 0; i < n && i < 40u; i++) w[i] = v[i];
}

void fpga_freq_check_last(fpga_chk_last_t *o)
{
    if (o == NULL) return;
    o->ms = s_chk_last_ms; o->seq = s_chk_last_seq; o->nth = s_chk_last_nth; o->ccd = s_chk_last_ccd;
    o->late = s_chk_late; o->startup_meas = FPGA_CHK_STARTUP_MEAS;
}

void fpga_freq_check_stats(uint32_t *count, uint32_t *clock, uint32_t *window)
{
    if (count)  *count  = s_chk_count;
    if (clock)  *clock  = s_chk_clock;
    if (window) *window = s_chk_window;
}

/* FW 0x0425 (audit 2026-10-20): odmitnuta okna nad mezi primeho mereni a s chybou koherence hodin */
void fpga_freq_check_stats2(uint32_t *fmax, uint32_t *coh)
{
    if (fmax) *fmax = s_chk_fmax + s_chk_fmax_b;   /* primarni + sekundarni slot */
    if (coh)  *coh  = s_chk_coh;
}

void fpga_freq_format_build(uint32_t build_time, uint32_t build_git, char *buf, int buflen)
{
    if (buf == NULL || buflen <= 0) return;
    if (build_time == 0u) { snprintf(buf, (size_t)buflen, "neznama (sestaveno mimo build.tcl?)"); return; }
    /* unix -> obcansky kalendar (H. Hinnant, days_from_civil inverzne), UTC */
    uint32_t days = build_time / 86400u, sod = build_time % 86400u;
    int32_t z = (int32_t)days + 719468;
    int32_t era = z / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int32_t y = (int32_t)yoe + era * 400;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp = (5u * doy + 2u) / 153u;
    uint32_t d = doy - (153u * mp + 2u) / 5u + 1u;
    uint32_t m = (mp < 10u) ? mp + 3u : mp - 9u;
    if (m <= 2u) y++;
    snprintf(buf, (size_t)buflen, "%04ld-%02lu-%02lu %02lu:%02lu:%02lu UTC git %07lx%s",
             (long)y, (unsigned long)m, (unsigned long)d,
             (unsigned long)(sod / 3600u), (unsigned long)((sod / 60u) % 60u), (unsigned long)(sod % 60u),
             (unsigned long)(build_git & 0x0FFFFFFFu), (build_git & 0x80000000u) ? "+" : "");
}

bool fpga_freq_get_frame(fpga_meas_t *out)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    uint8_t seen = s_frame_seen;
    if (seen && out) *out = s_frame;
    __set_PRIMASK(pm);
    return seen != 0;
}

bool fpga_freq_signal_lost(void)
{
    return s_last_seen && (s_last.error_flags & FPGA_ERR_SIGNAL_LOST);
}

bool fpga_freq_get_last(fpga_meas_t *out)
{
    /* Kratky IRQ-off (memcpy fpga_meas_t) = vzajemne vylouceni s latchem ve
     * fpga_freq_poll() bez zavislosti na FreeRTOS API (drzi to driver cisty). */
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    uint8_t seen = s_last_seen;
    if (seen && out) *out = s_last;
    __set_PRIMASK(pm);
    return seen != 0;
}

/* Prahy prepnuti zdroje S HYSTEREZI (x1e5): nahoru na /16 nad ~380 MHz, zpet na
 * /4 az pod ~360 MHz. Bez hystereze mereni sumici kolem prahu preskakovalo mezi
 * zdroji s ruznym rozlisenim (posledni cislice "blikaly" mezi dvema hodnotami). */
#define FPGA_SEL_UP_X1E5    38000000000000ULL   /* 380 MHz: /4 -> /16 */
#define FPGA_SEL_DOWN_X1E5  36000000000000ULL   /* 360 MHz: /16 -> /4 */

/* Ciste jadro volby zdroje (bez stavu -> unit-testovatelne v selftestu).
 * Vraci novy use16 pro dany predchozi stav + mereni. */
int fpga_freq_select_core(int use16, const fpga_meas_t *m)
{
    bool f4_err = (m->error_flags & FPGA_ERR_MEAS) != 0;
    bool f16_ok = !(m->status2 & FPGA_ST2_DIV16_ERR);

    if (use16) {
        /* zpet na /4 jen kdyz /4 meri a je bezpecne pod prahem (hystereze),
         * nebo kdyz /16 sam chybuje a /4 je pouzitelny */
        if (!f4_err && (m->frequency_x100000 < FPGA_SEL_DOWN_X1E5 || !f16_ok))
            use16 = 0;
    } else {
        if ((f4_err || m->frequency_x100000 >= FPGA_SEL_UP_X1E5) && f16_ok)
            use16 = 1;
    }
    return use16;
}

/* ── Overeni nasobitele reciproke dvojice (viz .h) ────────────────────────────
 * 🔴 HISTORIE: hi-res dopocet nasobil `edge_count` NAPEVNO ctyrmi (predpoklad,
 * ze je to pocet period vetve /4). Emulator `fpgasim` ale pocita periody
 * NEDELENEHO signalu, takze `fpgasim on 10000000` ukazoval 40 MHz (opraveno
 * commitem a6c0128). Tahle funkce je od te doby JEDINY zdroj pravdy o
 * nasobiteli — kdo si ho odvodi sam, tu chybu si zopakuje. */
uint32_t fpga_freq_hires_mul(uint64_t x100000, uint64_t edges, uint64_t gate_ps)
{
    if (gate_ps == 0u || edges == 0u) return 0u;
    uint64_t ref = x100000 / 100000ull;                  /* cele Hz, autoritativne z ramce */
    if (ref == 0u) return 0u;
    static const uint32_t MUL[] = { 1u, 4u, 16u };
    for (unsigned i = 0; i < sizeof MUL / sizeof MUL[0]; i++) {
        if (edges > 4000000000ull / MUL[i]) continue;    /* pojistka proti preteceni */
        uint64_t v = (uint64_t)((double)(edges * MUL[i]) * 1e12 / (double)gate_ps);
        uint64_t d = (v > ref) ? (v - ref) : (ref - v);
        if (d * 1000ull <= ref) return MUL[i];           /* shoda do 0,1 % */
    }
    return 0u;                                           /* nesedi zadny -> hi-res nepouzivat */
}

int fpga_freq_miscount(uint64_t uhz, uint64_t uhz_ref, uint64_t gate_ps, uint32_t mul)
{
    if (uhz == 0u || uhz_ref == 0u || gate_ps == 0u || mul == 0u) return 0;
    /* krok jedne hrany [µHz] = mul / gate_s Hz = mul · 1e18 / gate_ps µHz */
    double step = (double)mul * 1e18 / (double)gate_ps;
    double d    = (double)uhz - (double)uhz_ref;   /* rozdil je maly, double staci */
    double k    = floor(d / step + 0.5);
    if (k == 0.0 || fabs(k) > 2.0) return 0;
    if (fabs(d - k * step) > step * 0.125) return 0;  /* neni cely nasobek kroku */
    return (int)k;
}

uint64_t fpga_freq_dt_ticks(uint64_t gate_ns)
{
    /* round(gate_ns · 1000 / TICK_PS). Pro gate_ns = floor(dt · 2,5) vyjde PRESNE dt:
     * sudy dt -> 0,4·g = dt, lichy -> 0,4·g = dt - 0,2. Bez preteceni do ~9e12 s. */
    return (gate_ns * 2000ull + FPGA_LEGACY_TICK_PS) / (2ull * FPGA_LEGACY_TICK_PS);
}

uint64_t fpga_freq_scaled(uint64_t n, uint64_t t_ps, int frac)
{
    if (t_ps == 0u || n == 0u) return 0u;
    /* n * 1e12 = (n * 1e6) * 1e6: prvni faktor se vejde (n <= 4e9 -> 4e15), druhy se
     * dodela 6 cislicemi dlouheho deleni. rem < t_ps => rem*10 nepretece (t_ps <= ~1e16). */
    uint64_t a = n * 1000000ull;
    uint64_t v = a / t_ps, rem = a % t_ps;
    for (int i = 0; i < 6 + frac; i++) { rem *= 10u; v = v * 10u + rem / t_ps; rem %= t_ps; }
    return v;
}

uint64_t fpga_freq_hires_uhz(uint64_t x100000, uint64_t edges, uint64_t gate_ps)
{
    uint32_t mul = fpga_freq_hires_mul(x100000, edges, gate_ps);
    if (mul == 0u || gate_ps == 0u) return x100000 * 10ull;    /* fallback: x1e5 -> µHz je x10 */
    /* µHz = 6 desetin; `edges·mul` <= 4e9 (guard ve `fpga_freq_hires_mul`). */
    return fpga_freq_scaled(edges * mul, gate_ps, 6);
}

double fpga_freq_hires_hz(uint64_t x100000, uint64_t edges, uint64_t gate_ps)
{
    uint32_t mul = fpga_freq_hires_mul(x100000, edges, gate_ps);
    if (mul == 0u || gate_ps == 0u) return 0.0;
    /* `edges·mul` <= 4e9 a gate_ps jsou v double presne; zaokrouhluje jen nasobeni
     * a deleni, tedy ~2e-16 relativne. Okno je PRESNE v ps (ne floor v ns). */
    return (double)(edges * mul) * 1e12 / (double)gate_ps;
}

/* ── Akumulátor měření (F-0171/F-0172, viz fpga_freq.h) ──────────────────────
 * Cykly se drží v jednotkách 1e-5 cyklu (`cyc_e5`), aby se do jednoho celého
 * čísla vešla přesná cesta (`edges·mul`, celé periody) i záložní cesta z
 * `x100000` (kmitočet ×1e5 · hradlo). Rozsah: 1,4 GHz = 1,4e14 jednotek/s,
 * perioda datalogu nejvýš 3600 s → 5e17, tedy hluboko pod mezí 2^62 níže.
 * F-0186: délka oken se sčítá v PIKOSEKUNDÁCH z přesných ticků (3600 s =
 * 3,6e15 ps), ne v `gate_time_ns` — to FPGA posílá zaokrouhlené dolů. */
typedef struct { uint64_t cyc_e5; uint64_t gate_ps; uint32_t n; } fpga_acc_t;
static fpga_acc_t s_acc[FPGA_ACC_N];
/* #27: fronta hotovych vzorku statistiky (producent FpgaTask, konzument UiTask),
 * indexy pod PRIMASK. */
#define FPGA_STAT_RING 16u
static fpga_acc_t s_stat_ring[FPGA_STAT_RING];
static uint8_t    s_stat_w = 0u, s_stat_r = 0u;
static uint32_t   s_stat_drop = 0u;

/* Cilova delka vzorku statistiky [ps]. Vzorek je hotovy pri Σhradel >= tohle.
 * Default 1 s (= 4 okna po 0,25 s, puvodni chovani). Laditelne `gatestat <ms>`:
 * delsi -> lepsi rozliseni (TDC chyba fixni, deli se delsim oknem), ale pomalejsi
 * obnova; kratsi az ~250 ms (jedno FPGA okno) -> rychle, ale zasumenejsi. Pyramida
 * Allana si τ0 bere ze skutecne delky vzorku (tau0_scale), takze se prizpusobi. */
static uint64_t   s_stat_target_ps = 1000000000000ull;   /* 1 s */
void fpga_stat_set_target_ms(uint32_t ms)
{
    if (ms < 250u)    ms = 250u;      /* floor = jedno FPGA okno (0,25 s) */
    if (ms > 100000u) ms = 100000u;   /* strop 100 s */
    s_stat_target_ps = (uint64_t)ms * 1000000000ull;
}
uint32_t fpga_stat_target_ms(void) { return (uint32_t)(s_stat_target_ps / 1000000000ull); }

void fpga_acc_add(uint64_t x100000, uint64_t edges, uint64_t gate_ps)
{
    if (gate_ps == 0u || x100000 == 0u) return;
    uint64_t cyc_e5;
    uint32_t mul = fpga_freq_hires_mul(x100000, edges, gate_ps);
    if (mul != 0u) {
        cyc_e5 = edges * mul * 100000ull;              /* přesně: celé periody */
    } else {
        /* Násobitel neověřen (větev /16, starý rámec) → cykly z x1e5 a hradla.
         * V double: 1,4e14 · 2,5e8 = 3,5e22 se do uint64 nevejde. */
        double c = (double)x100000 * (double)gate_ps * 1e-12;
        cyc_e5 = (uint64_t)(c + 0.5);
    }
    /* Krátký IRQ-off místo FreeRTOS API — tentýž idiom jako `fpga_freq_get_last`
     * (driver zůstává nezávislý na RTOS). Konzumenti (UiTask, defaultTask) čtou
     * víceslovní strukturu, takže bez vyloučení by viděli roztržený stav. */
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    for (int i = 0; i < FPGA_ACC_N; i++) {
        /* Konzument, který dlouho neodebírá (zastavený datalog), nesmí přetéct
         * uint64 — nad 2^62 (~9 h při 1,4 GHz) se jeho okno zahodí. */
        if (s_acc[i].cyc_e5 > (1ull << 62)) memset(&s_acc[i], 0, sizeof s_acc[i]);
        s_acc[i].cyc_e5  += cyc_e5;
        s_acc[i].gate_ps += gate_ps;
        s_acc[i].n++;
    }
    /* #27: vzorek statistiky je hotovy, jakmile Σhradel >= 1 s - hradlo/2, tedy
     * `2·Σ + g >= 2 s` (bez odcitani — pri hradle > 2 s by 2e9 - g podteklo).
     * Pri 0,25 s to jsou vzdy prave 4 mereni. */
    fpga_acc_t *st = &s_acc[FPGA_ACC_STATS];
    if (2ull * st->gate_ps + gate_ps >= 2ull * s_stat_target_ps) {   /* Σhradel >= cil (gatestat) */
        uint8_t nw = (uint8_t)((s_stat_w + 1u) % FPGA_STAT_RING);
        if (nw == s_stat_r) {                            /* plno -> zahodit nejstarsi */
            s_stat_r = (uint8_t)((s_stat_r + 1u) % FPGA_STAT_RING);
            s_stat_drop++;
        }
        s_stat_ring[s_stat_w] = *st;
        s_stat_w = nw;
        memset(st, 0, sizeof *st);
    }
    __set_PRIMASK(pm);
}

int fpga_stat_pop(double *hz, double *tau_s)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    if (s_stat_r == s_stat_w) { __set_PRIMASK(pm); return 0; }
    fpga_acc_t a = s_stat_ring[s_stat_r];
    s_stat_r = (uint8_t)((s_stat_r + 1u) % FPGA_STAT_RING);
    __set_PRIMASK(pm);
    /* f = (cyc_e5 · 1e-5) / (gate_ps · 1e-12) = cyc_e5 · 1e7 / gate_ps */
    if (hz)    *hz    = (a.gate_ps != 0u) ? (double)a.cyc_e5 * 1e7 / (double)a.gate_ps : 0.0;
    if (tau_s) *tau_s = (double)a.gate_ps * 1e-12;
    return 1;
}

void fpga_stat_break(void)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    /* F-0195: nulovat VŠECHNY akumulatory, ne jen STATS — DATALOG (index 1)
     * plni `datalog.c` jednou za periodu (`fpga_acc_take`) a bez tohoto
     * nulovani by pres hranici zmeny signalu/diry v SEQUENCE tise smichal
     * cykly a hradla DVOU ruznych kmitoctu do jednoho zaznamu s prizankem
     * freq_avg=1, ktery pak vypada jako cisty prumer (viz L-0101). */
    for (int i = 0; i < FPGA_ACC_N; i++) memset(&s_acc[i], 0, sizeof s_acc[i]);
    __set_PRIMASK(pm);
}

void fpga_stat_flush(void)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    for (int i = 0; i < FPGA_ACC_N; i++) memset(&s_acc[i], 0, sizeof s_acc[i]);
    s_stat_r = s_stat_w;                              /* fronta prazdna */
    __set_PRIMASK(pm);
}

uint32_t fpga_stat_drops(void) { return s_stat_drop; }

uint32_t fpga_acc_take(int which, double *hz, double *gate_s)
{
    if (hz) *hz = 0.0;
    if (gate_s) *gate_s = 0.0;
    if (which < 0 || which >= FPGA_ACC_N) return 0u;
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    fpga_acc_t a = s_acc[which];
    memset(&s_acc[which], 0, sizeof s_acc[which]);
    __set_PRIMASK(pm);
    /* f = (cyc_e5 · 1e-5) / (gate_ps · 1e-12) = cyc_e5 · 1e7 / gate_ps */
    if (hz && a.n != 0u && a.gate_ps != 0u)
        *hz = (double)a.cyc_e5 * 1e7 / (double)a.gate_ps;
    if (gate_s) *gate_s = (double)a.gate_ps * 1e-12;
    return a.n;
}

uint64_t fpga_freq_select(const fpga_meas_t *m, int *used16)
{
    /* /4 ma nejlepsi rozliseni; nad ~380 MHz je pin28 (/4 -> ~95 MHz) u stropu -> /16.
     * Sticky stav (jediny konzument = FpgaTask): drzi zvoleny zdroj, prepina jen
     * pri prekroceni prahu s hysterezi nebo pri chybe aktivniho zdroje. */
    static int s_use16 = 0;
    /* FW >= 0x041E: `freq16` neni odbocka /16, ale DRUHY KANAL — nikdy se na nej neprepina (uzivatel
     * zvolil kanal tlacitkem); headline = primarni slot = vybrany kanal. */
    if (m->caps & FPGA_CAP_CHAN) { s_use16 = 0; if (used16) *used16 = 0; return m->frequency_x100000; }
    s_use16 = fpga_freq_select_core(s_use16, m);
    if (used16) *used16 = s_use16;
    return s_use16 ? m->freq16_x100000 : m->frequency_x100000;
}

/* Selftest hystereze /4<->/16 na syntetickych ramcich (cista logika, nemeni
 * runtime stav FpgaTasku). Soucast UART "selftest". */
bool fpga_freq_select_selftest(void)
{
    fpga_meas_t m;
    memset(&m, 0, sizeof m);
    int u = 0, ok = 1;
    #define SEL_MHZ(x) ((uint64_t)(x) * 1000000ULL * 100000ULL)

    m.frequency_x100000 = SEL_MHZ(100);                      /* 100 MHz -> /4 */
    u = fpga_freq_select_core(u, &m); ok &= (u == 0);
    m.frequency_x100000 = SEL_MHZ(390);                      /* 390 MHz -> /16 */
    u = fpga_freq_select_core(u, &m); ok &= (u == 1);
    m.frequency_x100000 = SEL_MHZ(370);                      /* 370: hystereze -> drzi /16 */
    u = fpga_freq_select_core(u, &m); ok &= (u == 1);
    m.frequency_x100000 = SEL_MHZ(350);                      /* 350: pod 360 -> zpet /4 */
    u = fpga_freq_select_core(u, &m); ok &= (u == 0);
    m.frequency_x100000 = SEL_MHZ(370);                      /* 370 z /4 strany -> drzi /4 */
    u = fpga_freq_select_core(u, &m); ok &= (u == 0);
    m.error_flags = FPGA_ERR_MEAS;                           /* /4 chybuje -> /16 */
    u = fpga_freq_select_core(u, &m); ok &= (u == 1);
    m.error_flags = 0; m.status2 = FPGA_ST2_DIV16_ERR;       /* /16 chybuje -> zpet /4 */
    u = fpga_freq_select_core(u, &m); ok &= (u == 0);
    m.frequency_x100000 = SEL_MHZ(500);                      /* vysoko, ale /16 vadny -> drzi /4 */
    u = fpga_freq_select_core(u, &m); ok &= (u == 0);

    #undef SEL_MHZ

    /* F-0186: ticky z gate_time_ns = floor(dt · 2,5 ns) (spi_app.v:507) a hi-res
     * kmitocet z nich — lichy i sudy dt, i dlouhe okno (21,5 s = 8,6e9 ticku). */
    {   static const uint64_t DT[] = { 100000000ull, 100000001ull, 8600000001ull, 1ull, 2ull };
        for (unsigned i = 0; i < sizeof DT / sizeof DT[0]; i++)
            ok &= (fpga_freq_dt_ticks((DT[i] * 5u) >> 1) == DT[i]);
        /* 10 MHz nedeleny (mul 1): 2 500 000 hran za 100 000 001 ticku (lichy dt).
         * Presne f = 2,5e6 · 4e8 / 100000001 = 9 999 999,900000001 Hz; z floor hradla
         * (250 000 002 ns) by vyslo 9 999 999,92 — o 2e-9 vys. */
        double f = fpga_freq_hires_hz(999999990000ull, 2500000ull, 100000001ull * FPGA_LEGACY_TICK_PS);
        ok &= (fabs(f / 9999999.900000001 - 1.0) < 1e-15);
    }
    /* 2026-10-03: FW >= 0x0400 — dt v jednotkach T_clk/16384 -> ps, kmitocet pocita
     * parser. 10 MHz: 2,5e6 hran za 0,25 s = 4,096e11 jednotek = PRESNE 2,5e11 ps. */
    {   uint8_t rx[FR_LEN];
        memset(rx, 0, sizeof rx);
        uint8_t *p = &rx[FR_PAYLOAD];
        sim_put_le64(p + 8, 2500000ull);                  /* edge_count (CH_A) */
        p[49] = 0x04; p[50] = 0x23;                       /* fw 0x0400, caps 0x0023 */
        p[88] = 0x03;                                     /* tdc_status: obe tabulky platne */
        sim_put_le64(p + 106, 409600000000ull);           /* dt_a */
        for (int i = 0; i < 6; i++) p[89 + i] = (uint8_t)(409600000000ull >> (8 * i));   /* dt_b */
        for (int i = 0; i < 4; i++) p[96 + i] = (uint8_t)(5000000u >> (8 * i));          /* edges_b */
        fpga_meas_t pm;
        parse_data(rx, &pm);
        ok &= (pm.gate_ps == 250000000000ull);
        ok &= (pm.gate_time_ns == 250000000ull);
        ok &= (pm.frequency_x100000 == 1000000000000ull);  /* 10 MHz x 1e5 */
        ok &= (pm.freq16_x100000    == 2000000000000ull);  /* CH_B 20 MHz */
        ok &= (pm.tdc_status == 0x03u && pm.edges_b == 5000000u);
        ok &= (fpga_freq_hires_mul(pm.frequency_x100000, pm.edge_count, pm.gate_ps) == 1u);
        ok &= (fpga_freq_scaled(2500000ull, 250000000000ull, 3) == 10000000000ull);   /* 10 MHz x 1e3 */
        /* lichy pocet jednotek: 1 jednotka = 0,6103515625 ps zaokrouhleno */
        ok &= (dt_units_to_ps(1ull, 100u) == 1ull && dt_units_to_ps(3ull, 100u) == 2ull);
        /* Obecny prevod jednotek podle kmitoctu hodin, ktery hlasi ramec (caps bit12, abs 99): ramec s 125 MHz (tak bezely
         * FW 0x0421/0x0422): 0,25 s = 5,12e11 jednotek (0,48828125 ps) = PRESNE 2,5e11 ps. Ramec bez bitu (nebo se spatnym
         * bajtem 0) se bere jako 100 MHz. */
        {   uint8_t r2[FR_LEN];
            memset(r2, 0, sizeof r2);
            uint8_t *q = &r2[FR_PAYLOAD];
            sim_put_le64(q + 8, 2500000ull);
            q[49] = 0x04; q[50] = 0x23; q[51] = 0x10;                       /* fw 0x0400, caps 0x1023 (DT, CLK) */
            q[87] = 125u;
            q[88] = 0x03;
            sim_put_le64(q + 106, 512000000000ull);                         /* dt_a = 0,25 s * 125 MHz * 16384 */
            for (int i = 0; i < 6; i++) q[89 + i] = (uint8_t)(512000000000ull >> (8 * i));
            for (int i = 0; i < 4; i++) q[96 + i] = (uint8_t)(5000000u >> (8 * i));
            fpga_meas_t p2;
            parse_data(r2, &p2);
            ok &= (p2.clk_mhz == 125u && p2.gate_ps == 250000000000ull && p2.frequency_x100000 == 1000000000000ull);
            ok &= (dt_units_to_ps(1ull, 125u) == 0ull && dt_units_to_ps(3ull, 125u) == 1ull && dt_units_to_ps(2048ull, 125u) == 1000ull);
            q[87] = 0u;                                                     /* poskozeny bajt: ne deleni nulou, ale 100 MHz */
            parse_data(r2, &p2);
            ok &= (p2.gate_ps == dt_units_to_ps(512000000000ull, 100u));
        }
    }
    /* 2026-10-06: chybne napocitane okno. 10 MHz, 0,25 s, mul 1 -> krok 4 Hz =
     * 4 000 000 µHz. +1 hrana = +4 Hz, -1 = -4 Hz; sum TDC (~mHz) ani realna zmena
     * mimo cely nasobek se za miscount vydavat nesmi. */
    {   const uint64_t R = 10000000000000ull, G = 250000000000ull;   /* 10 MHz v µHz, 0,25 s */
        ok &= (fpga_freq_miscount(R + 4000000ull, R, G, 1u) == 1);
        ok &= (fpga_freq_miscount(R - 4000000ull, R, G, 1u) == -1);
        ok &= (fpga_freq_miscount(R + 8000000ull, R, G, 1u) == 2);
        ok &= (fpga_freq_miscount(R + 4000123ull, R, G, 1u) == 1);   /* + sum 0,1 mHz */
        ok &= (fpga_freq_miscount(R + 7000ull,    R, G, 1u) == 0);   /* sum TDC */
        ok &= (fpga_freq_miscount(R + 2000000ull, R, G, 1u) == 0);   /* pul kroku */
        ok &= (fpga_freq_miscount(R + 16000000ull, R, G, 1u) == 0);  /* +4 kroky = zmena */
        ok &= (fpga_freq_miscount(R + 16000000ull, R, G, 4u) == 1);  /* /4 vetev: krok 16 Hz */
        ok &= (fpga_freq_miscount(R, 0u, G, 1u) == 0);
    }
    /* 2026-10-10 (STATUS #283): samokontrola. Delka okna proti hradlu: 10 MHz/0,25 s sedi i s okrajem +-1 us
     * a dvema periodami; okno 6,3x delsi (pripad L-0141, 1 578 628 Hz misto 10 MHz) neprojde; signal s mene nez
     * 2 hranami na hradlo se neposuzuje. Pak parse ramce FW 0x041F (identita, hlidac hodin, rozdil pocitani). */
    {   const uint64_t G = 250000000000ull;
        ok &= (fpga_freq_window_check(2500002u, 250000100000ull, G) == FPGA_CHK_OK);
        ok &= (fpga_freq_window_check(2500000u, G - 900000ull, G) == FPGA_CHK_OK);
        ok &= (fpga_freq_window_check(2500002u, G + 1300000ull, G) == FPGA_CHK_OK);       /* 1 us + 3 periody */
        ok &= (fpga_freq_window_check(2500002u, G + 1400000ull, G) == FPGA_CHK_WINDOW);
        ok &= (fpga_freq_window_check(2500002u, 1583670000000ull, G) == FPGA_CHK_WINDOW);
        ok &= (fpga_freq_window_check(3u, 3000000000000ull, G) == FPGA_CHK_OK);           /* 1 Hz: protazene okno */
        ok &= (fpga_win_code_ps(FPGA_WIN_50MS) == 50000000000ull && fpga_win_code_ps(7u) == 0u);
        char b[64];
        fpga_freq_format_build(0x6ACA21F9u, 0x8D526C00u, b, sizeof b);
        ok &= (strcmp(b, "2026-10-10 11:31:05 UTC git d526c00+") == 0);
        fpga_freq_format_build(951782400u, 0x0ABCDEFu, b, sizeof b);                    /* prestupny den */
        ok &= (strcmp(b, "2000-02-29 00:00:00 UTC git 0abcdef") == 0);
        uint8_t rx[FR_LEN];
        memset(rx, 0, sizeof rx);
        uint8_t *p = &rx[FR_PAYLOAD];
        for (int i = 0; i < 4; i++) { p[i] = (uint8_t)(0x6ACA21F9u >> (8 * i)); p[4 + i] = (uint8_t)(0x8D526C00u >> (8 * i)); }
        sim_put_le64(p + 8, 2500000ull);
        p[49] = 0x04; p[50] = 0xE2; p[51] = 0x06;             /* fw 0x041F caps 0x06E2 (DT, REGR, CHAN, SELFCHK) */
        p[48] = 0x1F;
        p[52] = FPGA_CLK_OK | FPGA_CLK_FAULT; p[53] = 3u;     /* hodiny OK, 3 vypadky */
        p[85] = 0xFEu; p[86] = 1u;                            /* rozdil -2 / +1 */
        sim_put_le64(p + 106, 409600000000ull);
        fpga_meas_t pm;
        parse_data(rx, &pm);
        ok &= (pm.build_time == 0x6ACA21F9u && pm.build_git == 0x8D526C00u);
        p[52] = FPGA_CLK_OK | FPGA_CLK_REF_EXT | FPGA_CLK_EXT_PRESENT;                 /* FW 0x0420: bity reference */
        { fpga_meas_t pr; parse_data(rx, &pr);
          ok &= ((pr.clk_status & FPGA_CLK_REF_EXT) && (pr.clk_status & FPGA_CLK_EXT_PRESENT) && !(pr.clk_status & FPGA_CLK_EXT_LOST)); }
        p[52] = FPGA_CLK_OK | FPGA_CLK_FAULT;
        ok &= (pm.ccd_p == -2 && pm.ccd_s == 1 && pm.clk_loss == 3u && pm.clk_status == 0x03u);
        ok &= (pm.frequency_x100000 == 1000000000000ull && pm.gate2_ps == G);   /* identita nepreplacne kmitocet */
    }
    /* 2026-10-07 (FW 0x0411): regresni blok. f = (xB - xA) / (yB - yA); okno 0,25 s, N = 2 500 000 hran,
     * stredy segmentu 2 000 000 hran a 0,2 s od sebe -> 10 MHz presne. Konzistentni regrese (rozdil 1e-8)
     * nahradi delku okna efektivni N / f_regr; nekonzistentni (1e-6) se zamitne a zustane dvoubodova. */
    {   uint8_t rx[FR_LEN];
        /* L-0186: mez prijeti je od E0 adaptivni (fpga_freq_regr_limit) a na bezici desce se nauci na ~5 ppb -> okno sc=1
         * (1e-8) by se zamitlo a test padal jen podle toho, kolik oken FPGA uz dodala. Test proto bezi s vychozi mezi
         * (stav zahrivani, FPGA_REGR_MAX_REL) a zive pocitadla i adaptace se na konci vrati -- drive je reset smazal. */
        const uint32_t sv_win = s_rg_win, sv_used = s_rg_used, sv_rej = s_rg_rej, sv_nl0 = s_rg_nl[0], sv_nl1 = s_rg_nl[1];
        const uint32_t sv_an = s_rg_adapt_n, sv_rr = s_rg_rej_run;
        const double   sv_fr = s_rg_fr, sv_f2 = s_rg_f2, sv_s1 = s_rg_s1, sv_s2 = s_rg_s2, sv_ew = s_rg_ewma;
        s_rg_ewma = 0.0; s_rg_adapt_n = 0u; s_rg_rej_run = 0u;
        for (int sc = 0; sc < 4; sc++) {
            memset(rx, 0, sizeof rx);
            uint8_t *p = &rx[FR_PAYLOAD];
            const double f0 = 1.0e7 * (1.0 + (sc == 0 ? 0.0 : (sc == 1 ? 1.0e-8 : (sc == 2 ? 1.0e-6 : 1.0e-7))));
            sim_put_le64(p + 8, 2500000ull);                  /* edge_count */
            p[49] = 0x04; p[50] = 0xE2;                       /* fw 0x0400, caps 0x00E2 (DT + REGR) */
            sim_put_le64(p + 106, 409600000000ull);           /* dt_a = 0,25 s -> gate_ps 2,5e11 */
            for (int i = 0; i < 6; i++) p[89 + i] = (uint8_t)(409600000000ull >> (8 * i));   /* dt_b */
            for (int i = 0; i < 4; i++) p[96 + i] = (uint8_t)(5000000u >> (8 * i));          /* edges_b */
            const uint64_t xa = 5000ull * 256ull, xb = (5000ull + 2000000ull) * 256ull;
            const uint64_t ya = 160000000000ull;               /* y*16, zacatek segmentu A */
            const double   dy_ps = 2000000.0 / f0 * 1e12;       /* ps mezi stredy */
            const uint64_t yb = ya + (uint64_t)(dy_ps * 1024.0 / 625.0 * 16.0 + 0.5);
            for (int i = 0; i < 3; i++)  { p[56 + i] = (uint8_t)(2000u >> (8 * i)); p[70 + i] = (uint8_t)(2000u >> (8 * i)); }   /* n */
            for (int i = 0; i < 5; i++)  { p[59 + i] = (uint8_t)(xa >> (8 * i));    p[73 + i] = (uint8_t)(xb >> (8 * i)); }      /* xm */
            for (int i = 0; i < 6; i++)  { p[64 + i] = (uint8_t)(ya >> (8 * i));    p[78 + i] = (uint8_t)(yb >> (8 * i)); }      /* ym */
            p[84] = 3u;
            fpga_meas_t pm;
            parse_data(rx, &pm);
            ok &= (pm.rg_ok == 3u && pm.rg_n[0] == 2000u && pm.rg_xm[1] == xb && pm.rg_ym[1] == yb);
            ok &= (pm.gate2_ps == 250000000000ull);
            ok &= (fabs(fpga_freq_regr_hz(pm.rg_xm[0], pm.rg_xm[1], pm.rg_ym[0], pm.rg_ym[1], pm.clk_mhz) / f0 - 1.0) < 1e-9);
            if (sc <= 1) {          /* konzistentni: gate_ps = N / f_regr */
                const uint64_t g = (uint64_t)(2500000.0 * 1e12 / f0 + 0.5);
                ok &= (pm.regr_used == 1u && (pm.gate_ps > g ? pm.gate_ps - g : g - pm.gate_ps) <= 2u);
            } else {                /* 1e-7 a 1e-6 > mez 5e-8: zamitnuto, zustava dvoubodova */
                ok &= (pm.regr_used == 0u && pm.gate_ps == 250000000000ull);
            }
        }
        /* FW 0x0426 (caps bit3): SUROVE SOUCTY. 10 MHz, souvisla rada n = 2001 znacek: A od indexu 4000, B od 2 004 000;
         * cas znacky = index * P, P = 163 840 jednotek (100 ns). Teziste A = 5000 hran, B = 2 005 000 hran -> f = 10 MHz presne.
         * Overuje rozlozeni bajtu (B je rozhazeny v abs 24..43, 92..96, 107), oriznuti edge_count na u32 a dopocet xm/ym. */
        {   memset(rx, 0, sizeof rx);
            uint8_t *p = &rx[FR_PAYLOAD];
            #define PUTN(o, v, nb) do { uint64_t _v = (uint64_t)(v); for (int _i = 0; _i < (nb); _i++) { p[(o) + _i] = (uint8_t)_v; _v >>= 8; } } while (0)
            const uint64_t P = 163840ull, n = 2001ull, S = n * (n - 1u) / 2u;
            const uint64_t x0a = 4000ull, x0b = 2004000ull;
            PUTN(8, 2500000ull, 4);                                  /* edge_count u32 (abs 20) */
            p[49] = 0x04; p[48] = 0x26; p[50] = 0xEA; p[51] = 0x7E;  /* fw 0x0426, caps 0x7EEA (DT, REGR, RAWREG, ...) */
            p[87] = 100u;                                            /* abs 99: hodiny 100 MHz (caps bit12) */
            PUTN(106, 409600000000ull, 6);                           /* dt_a = 0,25 s */
            PUTN(54, n, 3); PUTN(57, x0a | 0x80000000ull, 4); PUTN(61, S, 6); PUTN(67, x0a * P, 6); PUTN(73, (S * P) >> 3, 7);
            PUTN(12, n, 3); PUTN(15, x0b | 0x80000000ull, 4); PUTN(19, S, 6); PUTN(25, x0b * P, 6);
            {   uint64_t syb = (S * P) >> 3; p[31] = (uint8_t)syb; for (int i = 0; i < 5; i++) p[80 + i] = (uint8_t)(syb >> (8 * (i + 1))); p[95] = (uint8_t)(syb >> 48); }
            #undef PUTN
            fpga_meas_t pm;
            parse_data(rx, &pm);
            ok &= (pm.edge_count == 2500000ull && pm.rg_ok == 3u && pm.rg_n[0] == 2001u && pm.rg_n[1] == 2001u);
            ok &= (pm.rg_xm[0] == 5000ull * 4096ull && pm.rg_xm[1] == 2005000ull * 4096ull);
            ok &= (pm.rg_ym[0] == 16ull * P * 5000ull && pm.rg_ym[1] == 16ull * P * 2005000ull);
            ok &= (pm.regr_used == 1u && fabs(pm.f_regr_hz / 1.0e7 - 1.0) < 1e-12);
        }
        {   const uint32_t pm = __get_PRIMASK();               /* selftest nesmi zkreslit statistiku UART `regr` ani adaptaci meze */
            __disable_irq();
            s_rg_win = sv_win; s_rg_used = sv_used; s_rg_rej = sv_rej; s_rg_nl[0] = sv_nl0; s_rg_nl[1] = sv_nl1;
            s_rg_adapt_n = sv_an; s_rg_rej_run = sv_rr;
            s_rg_fr = sv_fr; s_rg_f2 = sv_f2; s_rg_s1 = sv_s1; s_rg_s2 = sv_s2; s_rg_ewma = sv_ew;
            __set_PRIMASK(pm);
        }
        ok &= (fpga_freq_regr_hz(100u, 100u, 5u, 9u, 100u) == 0.0 && fpga_freq_regr_hz(100u, 200u, 9u, 5u, 100u) == 0.0);
    }
    /* F-0193: souvislost SEQUENCE — navazuje, dira, preteceni uint32, mez diry,
     * navrat zpet a prvni mereni po bootu (sentinel). */
    ok &= (fpga_tdccal_selftest() != 0);                  /* FW 0x0427: vzorec kalibracni tabulky proti zlatym vektorum z FPGA simulace */
    ok &= (fpga_seq_gap(5u, 6u) == 0u);
    ok &= (fpga_seq_gap(5u, 9u) == 3u);
    ok &= (fpga_seq_gap(0xFFFFFFFEu, 0u) == 1u);          /* ...FE, (FF chybi), 0 */
    ok &= (fpga_seq_gap(0xFFFFFFFEu, 0xFFFFFFFFu) == 0u);
    ok &= (fpga_seq_gap(5u, 5u + FPGA_SEQ_GAP_MAX) == FPGA_SEQ_GAP_MAX - 1u);
    ok &= (fpga_seq_gap(5u, 6u + FPGA_SEQ_GAP_MAX) == FPGA_SEQ_RESYNC);
    ok &= (fpga_seq_gap(100u, 3u) == FPGA_SEQ_RESYNC);    /* reset FPGA */
    ok &= (fpga_seq_gap(0xFFFFFFFFu, 12345u) == 0u);      /* jeste zadne mereni */
    /* 2026-10-14: rezerva TDC retezu (fpga_tdc_rez_eval). 268 tapu: A 252 -> 16 OK, B 258 -> 10 POZOR (stav z desky po studenem
     * startu); hranice 15/14 a 8/7; udalosti za koncem (last = max. kod) rezervu nepocitaji a davaji vzdy KRITICKE. */
    {   fpga_tdc_cal_t c;
        fpga_tdc_rez_t  r;
        memset(&c, 0, sizeof c);
        c.taps = 268u;
        c.last[0] = 252u; c.last[1] = 258u;
        fpga_tdc_rez_eval(&c, &r);
        ok &= (r.rez_tap[0] == 16 && r.lvl[0] == FPGA_TDC_REZ_OK && r.rez_tap[1] == 10 && r.lvl[1] == FPGA_TDC_REZ_POZOR);
        c.last[0] = 253u; c.last[1] = 254u;                                   /* 15 = OK, 14 = POZOR */
        fpga_tdc_rez_eval(&c, &r);
        ok &= (r.lvl[0] == FPGA_TDC_REZ_OK && r.lvl[1] == FPGA_TDC_REZ_POZOR);
        c.last[0] = 260u; c.last[1] = 261u;                                   /* 8 = POZOR, 7 = KRITICKE */
        fpga_tdc_rez_eval(&c, &r);
        ok &= (r.lvl[0] == FPGA_TDC_REZ_POZOR && r.lvl[1] == FPGA_TDC_REZ_KRIT && r.rez_tap[1] == 7);
        c.last[0] = 268u; c.last[1] = 300u;                                   /* za koncem bez udalosti: rezerva 0, nikdy zaporna */
        fpga_tdc_rez_eval(&c, &r);
        ok &= (r.rez_tap[0] == 0 && r.rez_tap[1] == 0 && r.lvl[0] == FPGA_TDC_REZ_KRIT && r.lvl[1] == FPGA_TDC_REZ_KRIT);
        c.last[0] = 511u; c.last[1] = 511u;                                   /* udalosti za koncem: last = max. adresa */
        c.ovf[0] = 1000u; c.ovf[1] = 65536u;                                  /* 954 ppm / 62500 ppm = presne 1/16 */
        fpga_tdc_rez_eval(&c, &r);
        ok &= (r.rez_tap[0] == 0 && r.ovf_ppm[0] == 953u && r.lvl[0] == FPGA_TDC_REZ_KRIT);
        ok &= (r.ovf_ppm[1] == 62500u && r.lvl[1] == FPGA_TDC_REZ_KRIT);
        c.ovf[1] = 1u; c.ovf[0] = 0u; c.last[0] = 254u;                       /* jedina udalost za koncem = KRITICKE; bez ni a 14 tapu POZOR */
        fpga_tdc_rez_eval(&c, &r);
        ok &= (r.ovf_ppm[1] == 0u && r.lvl[1] == FPGA_TDC_REZ_KRIT && r.lvl[0] == FPGA_TDC_REZ_POZOR);
    }
    printf("fpga: select hystereze + ticky okna + miscount + regrese + souvislost SEQ + rezerva TDC selftest %s\n", ok ? "OK" : "FAIL");
    return ok != 0;
}

void fpga_freq_format_val(uint64_t v, char *buf, int buflen)
{
    uint64_t ipart = v / 100000ULL;
    uint32_t frac  = (uint32_t)(v % 100000ULL);      /* 5 desetinnych mist */

    /* Celociselna cast s teckou po trojicich (cesky styl) */
    char rev[24];
    int n = 0;
    if (ipart == 0) {
        rev[n++] = '0';
    } else {
        while (ipart > 0 && n < (int)sizeof(rev)) { rev[n++] = (char)('0' + (ipart % 10)); ipart /= 10; }
    }
    char grp[40];
    int gi = 0;
    for (int i = 0; i < n && gi < (int)sizeof(grp) - 1; i++) {
        grp[gi++] = rev[n - 1 - i];
        int remaining = n - 1 - i;
        if (remaining > 0 && (remaining % 3) == 0 && gi < (int)sizeof(grp) - 1) grp[gi++] = '.';
    }
    grp[gi] = '\0';

    snprintf(buf, buflen, "%s,%05luHz", grp, (unsigned long)frac);
}

/* uint64 -> decimal string (nano-printf neumi %llu) */
static int u64_to_str(uint64_t v, char *out)
{
    char rev[24];
    int n = 0;
    if (v == 0) { out[0] = '0'; out[1] = '\0'; return 1; }
    while (v > 0 && n < (int)sizeof(rev)) { rev[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (int i = 0; i < n; i++) out[i] = rev[n - 1 - i];
    out[n] = '\0';
    return n;
}

void fpga_freq_format_info(const fpga_meas_t *m, int use16, char *buf, int buflen)
{
    char g[24];
    u64_to_str(m->gate_time_ns, g);
    uint8_t present = m->phase_status & 0x0F;          /* zive faze (ideal 0xF) */
    uint8_t fine    = (m->phase_status >> 4) & 0x0F;   /* videne jemne kody (ideal 0xF) */
    /* ⚠️ BRING-UP nove desky (2026-09-30, dva symetricke kanaly, FPGA_PROTOCOL_V3_NAVRH.md):
     * prechodny FPGA top.v (FW_VERSION 0x0300) vubec nema 4fazovy TDC (carry-chain
     * je "stupen 3", zatim jen koncept v PHASE_CAL_DESIGN.md) -> phase_status je
     * VZDY 0x00, tedy "PH:0/0". NENI to porucha ani chybejici fazova hrana -
     * je to ocekavany stav, dokud nepribude skutecny TDC. NEHLEDAT v tom vadu
     * hardwaru (viz CLAUDE.md "HW OBVINEN - A BYL NEVINNY") - zkontroluj nejdriv
     * FW_VERSION v `status`/`fpgaraw`, jestli bezi prechodny bring-up bitstream. */

    /* chybovy tag (priorita: ztrata > overflow > div chyby) */
    const char *etag = "";
    if      (m->error_flags & FPGA_ERR_SIGNAL_LOST) etag = " LOST";
    else if (m->error_flags & FPGA_ERR_OVERFLOW)    etag = " OVF";
    else if (m->error_flags & FPGA_ERR_MEAS)        etag = " E/4";
    else if (m->status2     & FPGA_ST2_DIV16_ERR)   etag = " E/16";

    /* ⚠️ "SIM" JAKO PRVNI — info radek je videt na hlavni obrazovce i v diagnostice,
     * takze emulovana data nesmi jit zamenit za merena ani letmym pohledem. */
    if (m->caps & FPGA_CAP_DT) {
        /* novy FW: misto PH (4fazovy vernier, tady nema vyznam) stav TDC */
        const char *tdc = (m->tdc_status & FPGA_TDC_CAL_FAIL)  ? "FAIL"
                        : (m->tdc_status & FPGA_TDC_CAL_BUSY)  ? "CAL"
                        : ((m->tdc_status & (FPGA_TDC_CAL_A | FPGA_TDC_CAL_B)) !=
                           (FPGA_TDC_CAL_A | FPGA_TDC_CAL_B))  ? "NOCAL"
                        : (m->tdc_status & (FPGA_TDC_SHORT_A | FPGA_TDC_SHORT_B)) ? "SHORT" : "OK";
        snprintf(buf, buflen, "%sCH%c TDC:%s GATE:%sNS SEQ:%lu%s",
                 s_sim_on ? "SIM " : "", fpga_meas_chb_primary(m) ? 'B' : 'A', tdc, g,
                 (unsigned long)m->sequence, etag);
        return;
    }
    snprintf(buf, buflen, "%s%s PH:%X/%X GATE:%sNS SEQ:%lu%s",
             s_sim_on ? "SIM " : "",
             use16 ? "/16" : "/4", present, fine, g, (unsigned long)m->sequence, etag);
}

/* ── TDC: kalibrace a CAL report (FW >= 0x0400) ───────────────────────────── */
/* Nastavi zakladni okno FPGA (SET_CONFIG 0x01): 0=100 ms, 1=250 ms, 2=1 s, od FW 0x041E i 3=50 ms, 4=500 ms.
 * Delsi okno = min. nezavislych TDC chyb na hranach oken -> lepsi syrove rozliseni
 * (1 s okno ~2x lepsi nez CM7 akumulace 4×250 ms, protoze ma 1 par hran misto 4). */
bool fpga_freq_set_window(uint8_t mode)
{
    if (!g_init_ok || s_sim_on) return false;
    if (mode > 4u) mode = 1u;               /* neplatne -> 250 ms jako FPGA */
    uint8_t tx[FR_LEN], rx[FR_LEN], pl[2] = { 0x01u, mode };
    build_frame(TYPE_SET_CONFIG, 0, pl, 2, tx);
    return xfer(tx, rx);
}

uint8_t fpga_gate_idx_to_win(uint8_t idx)
{
    static const uint8_t W[5] = { FPGA_WIN_50MS, FPGA_WIN_100MS, FPGA_WIN_250MS, FPGA_WIN_500MS, FPGA_WIN_1S };
    return W[(idx < 5u) ? idx : 2u];
}

uint32_t fpga_gate_idx_ms(uint8_t idx)
{
    static const uint32_t MS[5] = { 50u, 100u, 250u, 500u, 1000u };
    return MS[(idx < 5u) ? idx : 2u];
}

bool fpga_freq_set_channel(uint8_t ch)
{
    if (!g_init_ok || s_sim_on) return false;
    uint8_t tx[FR_LEN], rx[FR_LEN], pl[2] = { 0x03u, (uint8_t)(ch & 1u) };
    build_frame(TYPE_SET_CONFIG, 0, pl, 2, tx);
    return xfer(tx, rx);
}

/* FW >= 0x0425 (caps bit14): SET_CONFIG 0x05 -- bit0 maskovani vnitrnich znacek pri CS, bit1 LED heartbeat (PIN29). */
bool fpga_freq_set_diag(uint8_t mask_cs, uint8_t led_hb)
{
    if (!g_init_ok || s_sim_on) return false;
    if (s_proto == 3u) return v3_cfg(FPGA_V3_CFG_DIAG, (uint8_t)((mask_cs & 1u) | ((led_hb & 1u) << 1)), 0u);
    uint8_t tx[FR_LEN], rx[FR_LEN], pl[2] = { 0x05u, (uint8_t)((mask_cs & 1u) | ((led_hb & 1u) << 1)) };
    build_frame(TYPE_SET_CONFIG, 0, pl, 2, tx);
    return xfer(tx, rx);
}

bool fpga_freq_set_ref(uint8_t ext)
{
    if (!g_init_ok || s_sim_on) return false;
    if (s_proto == 3u) return v3_cfg(FPGA_V3_CFG_REF, (uint8_t)(ext & 1u), 0u);
    uint8_t tx[FR_LEN], rx[FR_LEN], pl[2] = { 0x04u, (uint8_t)(ext & 1u) };
    build_frame(TYPE_SET_CONFIG, 0, pl, 2, tx);
    return xfer(tx, rx);
}

/* Co jsme FPGA naposledy POSLALI (0xFF = nic / zapomenuto). Echo v ramci (channel_id, phase_status) se
 * obnovuje az s dalsim merenim, takze bez signalu na vybranem kanalu zustane STARE: po prepnuti A -> B (B bez
 * signalu) a zpet na A by echo porad rikalo „A“ a STM by povel nikdy neposlal — FPGA by zustala na B
 * (nalezeno testem na desce 2026-10-09). Proto se posila, kdyz se lisi pozadavek od posledniho odeslaneho NEBO
 * od echa (to druhe chyti reset FPGA, ktery na STM nevidi). Promenne jsou nad `fpga_freq_restart`. */

bool fpga_freq_cfg_sync(uint8_t want_chan, uint8_t want_gate_idx)
{
    static uint32_t s_last_ms = 0u;
    if (s_proto == 3u || s_sim_v3) {
        /* v3: FPGA meri oba kanaly vzdy a po segmentech -> kanal i hradlo voli STM lokalne (okna ze segmentu) */
        want_chan &= 1u;
        if (want_gate_idx > 4u) want_gate_idx = 2u;
        if (want_chan == s3_chan && want_gate_idx == s3_gate) return false;
        s3_chan = want_chan; s3_gate = want_gate_idx;
        s3_regroup();
        return true;
    }
    fpga_meas_t m;
    if (!fpga_freq_get_last(&m) || !(m.caps & FPGA_CAP_CHAN)) return false;   /* starsi FW/emulator: nic k nastaveni */
    uint32_t now = HAL_GetTick();
    uint8_t want_win = fpga_gate_idx_to_win(want_gate_idx);
    want_chan &= 1u;
    bool diff_chan = (s_cfg_sent_chan != want_chan);
    bool diff_win  = (s_cfg_sent_win  != want_win);
    /* ZMENA pozadavku se posle hned; opakovani podle echa az po 1,5 s (echo se obnovuje s mereni). */
    if (!diff_chan && !diff_win && (uint32_t)(now - s_last_ms) < 1500u) return false;
    bool sent = false;
    if (diff_chan || (m.channel_id & 1u) != want_chan) {
        if (fpga_freq_set_channel(want_chan)) { s_cfg_sent_chan = want_chan; sent = true; }
    }
    if (diff_win || (m.phase_status & 7u) != want_win) {
        if (fpga_freq_set_window(want_win)) { s_cfg_sent_win = want_win; sent = true; }
    }
    if (sent) s_last_ms = now;
    return sent;
}

bool fpga_freq_chan_pending(const fpga_meas_t *m, uint8_t want_chan)
{
    return (m->caps & FPGA_CAP_CHAN) && ((m->channel_id & 1u) != (want_chan & 1u));
}

bool fpga_freq_tdc_cal_start(void)
{
    if (!g_init_ok || s_sim_on) return false;
    if (s_proto == 3u) return v3_cfg(FPGA_V3_CFG_CAL, 0x01u, 0u);    /* p1 bit0 start, maska 0 = oba kanaly (stridave) */
    uint8_t tx[FR_LEN], rx[FR_LEN], pl[2] = { 0x02u, 0u };
    /* FPGA spousti kalibraci NABEZNOU HRANOU cal_mode: nejdriv 0 (arm), pak 1. */
    build_frame(TYPE_SET_CONFIG, 0, pl, 2, tx);
    if (!xfer(tx, rx)) return false;
    pl[1] = 1u;
    build_frame(TYPE_SET_CONFIG, 0, pl, 2, tx);
    return xfer(tx, rx);
}

/* ── Rezerva TDC retezu (viz fpga_freq.h) ───────────────────────────────────────────────────────────────────── */
void fpga_tdc_rez_eval(const fpga_tdc_cal_t *c, fpga_tdc_rez_t *r)
{
    r->taps = c->taps;
    for (int ch = 0; ch < 2; ch++) {
        uint32_t ev  = c->ovf[ch];
        uint32_t ppm = (uint32_t)(((uint64_t)ev * 1000000ull) >> FPGA_TDC_CAL_LOG2);
        int16_t  rez = 0;
        uint8_t  lvl;
        if (ev == 0u) {
            /* nic za koncem: `last` je nejvyssi skutecny kod retezu, zbytek do `taps` je rezerva */
            rez = (c->taps > c->last[ch]) ? (int16_t)(c->taps - c->last[ch]) : (int16_t)0;
            lvl = (rez >= FPGA_TDC_REZ_OK_TAPS)   ? FPGA_TDC_REZ_OK
                : (rez >= FPGA_TDC_REZ_WARN_TAPS) ? FPGA_TDC_REZ_POZOR : FPGA_TDC_REZ_KRIT;
        } else {
            /* hrany uz dosly za konec: `last` je kod "za koncem" (max. adresa), z nej rezerva nejde. Uroven musi byt
             * MONOTONNI se zkracovanim retezu: v simulaci (38 ps/tap) je rezerva 7 tapu = KRITICKE bez zhorseni sigmy,
             * o ~1 ps/tap kratsi retez uz ma 3,9 % udalosti za koncem, takze to nesmi vyjit mirneji nez rezerva 7. */
            lvl = FPGA_TDC_REZ_KRIT;
        }
        r->rez_tap[ch] = rez;
        r->ovf_ev[ch]  = ev;
        r->ovf_ppm[ch] = ppm;
        r->lvl[ch]     = lvl;
    }
}

static fpga_tdc_rez_t s_rez;               /* posledni vyhodnoceni; psat smi FpgaTask i UartTask (`tdc`), cist kdokoli */

/* Kopie ~40 B pod PRIMASK (< 1 us): ctenar nikdy neuvidi pul zapisu a nikdo nesmi cekat/spinout. */
bool fpga_tdc_rez_get(fpga_tdc_rez_t *r)
{
    if (r == NULL) return false;
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    *r = s_rez;
    __set_PRIMASK(pm);
    return r->valid != 0u;
}

static void rez_store(const fpga_tdc_cal_t *c)
{
    fpga_tdc_rez_t t;
    fpga_tdc_rez_eval(c, &t);
    t.valid = 1u;
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    t.n   = s_rez.n + 1u;
    s_rez = t;
    __set_PRIMASK(pm);
}

/* FW >= 0x0427: stav kalibrace TDC v STM (sluzba nize, `fpga_freq_tdccal_poll`). Deklarace zde, protoze je cte CAL report. */
#define TYPE_LUT_WRITE 0x0A
static uint32_t s_ch_h[2][FPGA_TDCCAL_MAX_NC];
static uint16_t s_ch_l[2][FPGA_TDCCAL_MAX_NC];
static fpga_tdccal_stats_t s_cal_stats[2];
static fpga_tdccal_info_t  s_cal_info;
static uint32_t s_wait_polls = 0u;

static bool tdc_report_v3(fpga_tdc_cal_t *out);
bool fpga_freq_tdc_report(fpga_tdc_cal_t *out)
{
    if (!g_init_ok || s_sim_on || out == NULL) return false;
    if (s_proto == 3u) return tdc_report_v3(out);
    uint8_t tx[FR_LEN], rx[FR_LEN];
    bool locked = false, ok = false;
    if (s_spi_mtx != NULL && osKernelGetState() == osKernelRunning) {
        if (osMutexAcquire(s_spi_mtx, 200) != osOK) return false;
        locked = true;
    }
    /* Zadost + odpoved MUSI jit pod jednim zamkem: FPGA postavi CAL ramec az po
     * zadosti a VYSLE ho v PRISTI transakci; mezitim by FpgaTask svym ACK pollem
     * ten ramec vycetl a zahodil (typ != DATA). */
    build_frame(TYPE_CAL, 0, NULL, 0, tx);
    if (xfer_raw(tx, rx)) {
        for (int i = 0; i < 3 && !ok; i++) {
            delay_us(200);
            build_frame(TYPE_ACK, g_last_seq, NULL, 0, tx);
            if (!xfer_raw(tx, rx)) break;
            if (rx[0] != FR_MAGIC || rx[2] != TYPE_CAL) continue;
            if (!fpga_crc16_check(rx, FR_CRC_LEN, (uint16_t)((uint16_t)rx[126] | ((uint16_t)rx[127] << 8)))) continue;
            for (int ch = 0; ch < 2; ch++) {
                const uint8_t *q = &rx[FR_PAYLOAD + 12 * ch];
                out->ovf[ch]  = rd_le32(q + 0);
                out->peak[ch] = rd_le32(q + 4);
                out->nz[ch]   = (uint16_t)(q[8]  | (q[9]  << 8));
                out->last[ch] = (uint16_t)(q[10] | (q[11] << 8));
            }
            {   /* FW >= 0x0427 (S2): ovf/peak/nz/last pocita STM z histogramu posledni kalibrace (FPGA je neposila) */
                fpga_meas_t cf;
                if (fpga_freq_get_frame(&cf) && (cf.caps & FPGA_CAP_STMCAL)) {
                    for (int ch = 0; ch < 2; ch++) {
                        if (!s_cal_info.stats_valid) { out->ovf[ch] = 0u; out->peak[ch] = 0u; out->nz[ch] = 0u; out->last[ch] = 0u; continue; }
                        out->ovf[ch] = s_cal_stats[ch].ovf; out->peak[ch] = s_cal_stats[ch].peak;
                        out->nz[ch]  = s_cal_stats[ch].nz;  out->last[ch] = s_cal_stats[ch].last;
                    }
                }
            }
            /* B1a: nejvyssi set tap A/B z CAL bajtu 46..49 (mimo 12-byte blok) */
            out->maxtap[0] = (uint16_t)(rx[FR_PAYLOAD + 34] | (rx[FR_PAYLOAD + 35] << 8));
            out->maxtap[1] = (uint16_t)(rx[FR_PAYLOAD + 36] | (rx[FR_PAYLOAD + 37] << 8));
            out->status   = rx[FR_PAYLOAD + 24];
            out->cal_mode = rx[FR_PAYLOAD + 25];
            {   /* FW >= 0x0410: abs 50..51 = [4:0] log2(adres), [5] DUAL, [6] STRIDE==1, [15:7] pocet tapu */
                uint16_t cfg = (uint16_t)(rx[FR_PAYLOAD + 38] | (rx[FR_PAYLOAD + 39] << 8));
                out->cfg_valid = (cfg & 0x1Fu) != 0u;
                out->naddr   = out->cfg_valid ? (uint16_t)(1u << (cfg & 0x1Fu)) : 256u;
                out->dual    = (uint8_t)((cfg >> 5) & 1u);
                out->stride1 = (uint8_t)((cfg >> 6) & 1u);
                out->taps    = out->cfg_valid ? (uint16_t)(cfg >> 7) : 256u;
            }
            ok = true;
        }
    }
    if (locked) osMutexRelease(s_spi_mtx);
    /* Vcasne varovani rezervy retezu (fpga_freq.h) — mimo SPI zamek. Jen z DOKONCENE kalibrace: behem ni FPGA
     * diagnostiku maze a znovu sklada (last/ovf by byly castecne a vyrobily falesny verdikt). */
    if (ok && !(out->status & FPGA_TDC_CAL_BUSY) &&
        (out->status & (FPGA_TDC_CAL_A | FPGA_TDC_CAL_B)) == (FPGA_TDC_CAL_A | FPGA_TDC_CAL_B))
        rez_store(out);
    return ok;
}

static bool tdc_hist_v3(uint16_t k, uint32_t *a, uint32_t *b);
bool fpga_freq_tdc_hist(uint16_t k, uint32_t *a, uint32_t *b)
{
    if (!g_init_ok || s_sim_on || a == NULL || b == NULL || k > 1023u) return false;
    if (s_proto == 3u) return tdc_hist_v3(k, a, b);
    /* [12] = 1 | k[9:8] << 1 (starsi FW bere jen [12] == 1 a k < 256), [14] = k[7:0] */
    uint8_t tx[FR_LEN], rx[FR_LEN];
    uint8_t p12 = (uint8_t)(1u | (((k >> 8) & 3u) << 1));
    uint8_t pl[3] = { p12, 0u, (uint8_t)(k & 0xFFu) };
    bool locked = false, ok = false;
    if (s_spi_mtx != NULL && osKernelGetState() == osKernelRunning) {
        if (osMutexAcquire(s_spi_mtx, 200) != osOK) return false;
        locked = true;
    }
    /* Stejny vzor jako fpga_freq_tdc_report: zadost + odpoved pod JEDNIM zamkem.
     * FPGA nastavi adresu hist_k pri zpracovani zadosti a CAL ramec slozi az pak,
     * takze hodnota v odpovedi uz patri k `k` (BRAM cteni trva ns, ramec us). */
    build_frame(TYPE_CAL, 0, pl, 3, tx);
    if (xfer_raw(tx, rx)) {
        for (int i = 0; i < 3 && !ok; i++) {
            delay_us(200);
            build_frame(TYPE_ACK, g_last_seq, NULL, 0, tx);
            if (!xfer_raw(tx, rx)) break;
            if (rx[0] != FR_MAGIC || rx[2] != TYPE_CAL) continue;
            if (!fpga_crc16_check(rx, FR_CRC_LEN, (uint16_t)((uint16_t)rx[126] | ((uint16_t)rx[127] << 8)))) continue;
            if (rx[38] != p12 || rx[39] != (uint8_t)(k & 0xFFu)) continue;
            *a = (uint32_t)rx[40] | ((uint32_t)rx[41] << 8) | ((uint32_t)rx[42] << 16);
            *b = (uint32_t)rx[43] | ((uint32_t)rx[44] << 8) | ((uint32_t)rx[45] << 16);
            ok = true;
        }
    }
    if (locked) osMutexRelease(s_spi_mtx);
    return ok;
}


/* ═══ Kalibrace TDC v STM (FW >= 0x0427, caps bit4; audit 2026-10-21 S1/S2, L-0188) ═══════════════════════════════════
 * FPGA po sberu histogramu ceka (abs 112 bit0/1). Tady: precist histogramy (fpga_freq_tdc_hist), spocitat tabulky
 * (fpga_tdccal.c), zapsat (TYPE 0x0A LUT_WRITE, 1 polozka na ramec), overit pocitadlo + kontrolni soucet z DATA ramce a
 * potvrdit (SET_CONFIG 0x06 = 1). Blokuje FpgaTask ~0,6 s (hlida watchdog_kick_fpga); FPGA behem toho nemeri (hold). */

static bool tdccal_cfg06(uint8_t v)
{
    uint8_t tx[FR_LEN], rx[FR_LEN], pl[2] = { 0x06u, v };
    build_frame(TYPE_SET_CONFIG, 0, pl, 2, tx);
    return xfer(tx, rx);
}

static bool tdccal_put(uint8_t ch, uint16_t a, uint16_t lutv)
{
    uint8_t tx[FR_LEN], rx[FR_LEN];
    const uint16_t neg = (uint16_t)(lutv & 0x3FFFu);
    const uint8_t pl[3] = { (uint8_t)(a & 0xFFu),
                            (uint8_t)(((ch & 1u) << 7) | (((a >> 8) & 1u) << 6) | ((neg >> 8) & 0x3Fu)),
                            (uint8_t)(neg & 0xFFu) };
    build_frame(TYPE_LUT_WRITE, 0, pl, 3, tx);
    return xfer(tx, rx);
}

static fpga_meas_t s_tc_f0, s_tc_tmp, s_tc_fr;      /* velke struktury mimo zasobnik FpgaTasku (sluzba bezi jen z nej) */
static fpga_tdc_cal_t s_tc_cr, s_tc_c2;

static bool tdccal_run(void)
{
    const uint32_t t_start = HAL_GetTick();
    fpga_meas_t *const f0 = &s_tc_f0;
    if (!fpga_freq_get_frame(f0)) return false;
    const uint8_t mask = f0->lut_wait & 3u;
    fpga_tdc_cal_t *const crp = &s_tc_cr;
    memset(crp, 0, sizeof *crp);
    if (!fpga_freq_tdc_report(crp)) { s_cal_info.last_err = 3u; s_cal_info.fails++; return true; }
    uint32_t nc = crp->naddr ? crp->naddr : 256u;
    if (nc > FPGA_TDCCAL_MAX_NC) nc = FPGA_TDCCAL_MAX_NC;
    s_cal_info.naddr = (uint16_t)nc;
    uint8_t err = 0u;
    for (int attempt = 0; attempt < 3; attempt++) {
        err = 0u;
        if (attempt > 0) s_cal_info.retries++;
        if (!tdccal_cfg06(0u)) { err = 3u; continue; }          /* zacatek: vynuluje pocitadlo a soucet ve FPGA */
        /* 1) histogramy obou kanalu (jedna zadost vraci A i B) */
        bool rd_ok = true;
        for (uint32_t k = 0; k < nc && rd_ok; k++) {
            if ((k & 31u) == 0u) watchdog_kick_fpga();
            uint32_t a = 0u, b = 0u; bool ok = false;
            for (int r = 0; r < 3 && !ok; r++) ok = fpga_freq_tdc_hist((uint16_t)k, &a, &b);
            if (!ok) { rd_ok = false; break; }
            s_ch_h[0][k] = a; s_ch_h[1][k] = b;
        }
        if (!rd_ok) { err = 3u; continue; }
        /* 2) tabulky + kontrola souctu (= 2^LOG2 udalosti) */
        bool hist_ok = true;
        for (int ch = 0; ch < 2; ch++) {
            if (!(mask & (1u << ch))) continue;
            const uint64_t tot = fpga_tdccal_lut(s_ch_h[ch], nc, FPGA_TDC_CAL_LOG2, crp->dual, s_ch_l[ch]);
            fpga_tdccal_stats(s_ch_h[ch], nc, &s_cal_stats[ch]);
            if (tot != (1ull << FPGA_TDC_CAL_LOG2)) hist_ok = false;
        }
        if (!hist_ok) { err = 1u; break; }                       /* histogram nekompletni -> neprepisovat, nechat FPGA cekat */
        /* 3) zapis */
        uint16_t chk = 0u; uint32_t cnt = 0u; bool wr_ok = true;
        for (uint32_t k = 0; k < nc && wr_ok; k++) {
            if ((k & 31u) == 0u) watchdog_kick_fpga();
            for (int ch = 0; ch < 2; ch++) {
                if (!(mask & (1u << ch))) continue;
                if (!tdccal_put((uint8_t)ch, (uint16_t)k, s_ch_l[ch][k])) { wr_ok = false; break; }
                chk = (uint16_t)(chk + fpga_tdccal_chk((uint8_t)ch, (uint16_t)k, (uint16_t)(s_ch_l[ch][k] & 0x3FFFu)));
                cnt++;
            }
        }
        if (!wr_ok) { err = 3u; continue; }
        /* 4) overeni: DATA ramec nese pocitadlo a soucet toho, co FPGA prijala (odpoved na posledni ramec az v dalsim prenosu) */
        (void)fpga_freq_poll(&s_tc_tmp);
        osDelay(1);
        (void)fpga_freq_poll(&s_tc_tmp);
        if (!fpga_freq_get_frame(&s_tc_fr) || s_tc_fr.lut_cnt != (uint16_t)cnt || s_tc_fr.lut_sum != chk) { err = 2u; continue; }
        /* 5) potvrzeni */
        if (!tdccal_cfg06(1u)) { err = 3u; continue; }
        bool done = false;
        for (int i = 0; i < 20 && !done; i++) {
            osDelay(10);
            watchdog_kick_fpga();
            memset(&s_tc_c2, 0, sizeof s_tc_c2);
            if (fpga_freq_tdc_report(&s_tc_c2) && !(s_tc_c2.status & FPGA_TDC_CAL_BUSY) &&
                (s_tc_c2.status & (FPGA_TDC_CAL_A | FPGA_TDC_CAL_B)) == (FPGA_TDC_CAL_A | FPGA_TDC_CAL_B)) done = true;
        }
        if (!done) { err = 4u; continue; }
        s_cal_info.runs++;
        s_cal_info.last_err = 0u;
        s_cal_info.stats_valid = 1u;
        s_cal_info.last_ms = HAL_GetTick() - t_start;
        s_cal_info.last_tick = HAL_GetTick() ? HAL_GetTick() : 1u;
        return true;
    }
    s_cal_info.fails++;
    s_cal_info.last_err = err;
    s_cal_info.last_ms = HAL_GetTick() - t_start;
    return true;
}

/* Prikaz v3 (SET_CONFIG): jedna transakce pod zamkem; prijaty ramec se nepouzije (zaznamy prijdou znovu, ACK se neposunul) */
/* SET_CONFIG do FPGA, nebo pri emulaci primo do emulatoru (bez SPI) */
static bool s3_cfg_any(uint8_t id, uint8_t v1, uint8_t v2)
{
    if (s_sim_on) {
        fpga_v3_build_cfg(s3c_tx, ++s3_mseq, id, v1, v2);
        fpga_v3_emu_rx(&s3_emu, s3c_tx);
        return true;
    }
    return v3_cfg(id, v1, v2);
}

static void s3_range_ctl(const fpga_v3_hdr_t *h)
{
    if (!(h->caps & FPGA_V3_CAP_PRESC)) return;
    for (uint8_t ch = 0u; ch < 2u; ch++) {
        const uint8_t cur = (uint8_t)(h->presc[ch] & 0x07u);
        if (h->presc[ch] & FPGA_V3_PRESC_PEND) { s3_pr_cand[ch] = 0xFFu; continue; }
        uint8_t want = (s3_pr_mode[ch] != 0xFFu) ? s3_pr_mode[ch] : fpga_v3_range_probe(cur, h->probe[ch]);
        if (ch == 1u && (h->cfg_echo & 0x18u)) want = 0u;
        if (want == cur) { s3_pr_cand[ch] = 0xFFu; continue; }
        if (s3_pr_cand[ch] != want) { s3_pr_cand[ch] = want; continue; }
        if (s3_cfg_any(FPGA_V3_CFG_PRESC, ch, want)) { s3_pr_sw[ch]++; s3_brk = 1u; }
        s3_pr_cand[ch] = 0xFFu;
    }
}

int fpga_freq_presc(uint8_t ch, uint8_t mode)
{
    if (ch > 1u || (mode != 0xFFu && mode > FPGA_V3_RANGE_MAXLOG2)) return -1;
    s3_pr_mode[ch] = mode; s3_pr_cand[ch] = 0xFFu;
    return (s3_h_ok && (s3_h.caps & FPGA_V3_CAP_PRESC)) ? 1 : 0;
}

static bool v3_cfg(uint8_t id, uint8_t v1, uint8_t v2)
{
    if (!spi_lock(200)) return false;
    fpga_v3_build_cfg(s3c_tx, ++s3_mseq, id, v1, v2);
    bool ok = xfer3_raw(s3c_tx, s3c_rx);
    spi_unlock();
    return ok;
}

/* Stranka (TYPE 0xA0, kap. 5): zadost + az 3 ACK transakce pod JEDNIM zamkem (jinak by stranku vycetl poll FpgaTasku).
 * items[] = polozky u24, *n = pocet podle hlavicky. Bloky stranky: [0] k, [1] SEQ[7:0], od [2] 84 polozek po 3 B. */
static bool v3_page(uint8_t kind, uint16_t page, uint32_t *items, uint16_t *n)
{
    if (!spi_lock(300)) return false;
    bool ok = false;
    fpga_v3_build(s3c_tx, FPGA_V3_T_PAGE, ++s3_mseq, (const uint8_t[3]){ kind, (uint8_t)page, (uint8_t)(page >> 8) }, 3u);
    if (xfer3_raw(s3c_tx, s3c_rx)) {
        for (int i = 0; i < 3 && !ok; i++) {
            fpga_v3_build_ack(s3c_tx, ++s3_mseq, s3_link.expect);
            if (!xfer3_raw(s3c_tx, s3c_rx)) break;
            fpga_v3_hdr_t h;
            if (fpga_v3_parse(s3c_rx, &h, NULL, NULL) != FPGA_V3_OK || h.type != FPGA_V3_T_PAGE) continue;
            if (s3c_rx[116] != kind || (uint16_t)(s3c_rx[117] | (s3c_rx[118] << 8)) != page || h.blk_ok != 0x0Fu) continue;
            const uint16_t cnt = (uint16_t)(s3c_rx[119] | (s3c_rx[120] << 8));
            for (uint32_t k = 0; k < FPGA_V3_LUTW_MAX; k++) {
                const uint8_t *b = s3c_rx + (1u + k / FPGA_V3_LUTW_PER_BLK) * FPGA_V3_BLK;
                const uint8_t *e = b + 2u + 3u * (k % FPGA_V3_LUTW_PER_BLK);
                items[k] = (k < cnt) ? ((uint32_t)e[0] | ((uint32_t)e[1] << 8) | ((uint32_t)e[2] << 16)) : 0u;
            }
            *n = cnt;
            ok = (s3c_rx[FPGA_V3_BLK] == 1u && s3c_rx[FPGA_V3_BLK + 1] == (uint8_t)h.seq);
            s3_pages++;
        }
    }
    spi_unlock();
    return ok;
}

/* Kalibrace TDC v3 (kap. 6.3): FPGA ceka (CAL_WAIT) s histogramem jednoho kanalu, druhy meri. STM precte 3 stranky
 * histogramu, spocita tabulku (fpga_tdccal_lut, jako v2), zapise ji DAVKAMI po 252 polozkach (LUT_WRITE), overi
 * LUT_CNT/LUT_SUM a potvrdi -> FPGA aktivuje novou banku na hranici segmentu. Blokuje FpgaTask ~100 ms (ne ~0,6 s jako v2). */
static uint32_t s3_items[FPGA_V3_LUTW_MAX];
static uint8_t  s3_lw_ch[FPGA_V3_LUTW_MAX];
static uint16_t s3_lw_a[FPGA_V3_LUTW_MAX], s3_lw_n[FPGA_V3_LUTW_MAX];
static bool v3_hdr_now(fpga_v3_hdr_t *h)
{
    if (!spi_lock(200)) return false;
    fpga_v3_build_ack(s3c_tx, ++s3_mseq, s3_link.expect);
    bool ok = xfer3_raw(s3c_tx, s3c_rx) && (fpga_v3_parse(s3c_rx, h, NULL, NULL) == FPGA_V3_OK);
    spi_unlock();
    return ok;
}
static bool tdccal_run_v3(uint8_t ch)
{
    const uint32_t t_start = HAL_GetTick();
    uint8_t err = 0u;
    uint32_t nc = 0u;
    /* konfigurace TDC ze souhrnne stranky (polozka 3 = [4:0] log2 adres, [5] DUAL) */
    uint16_t n = 0u;
    if (!v3_page(0u, 0u, s3_items, &n) || n < 4u) { s_cal_info.last_err = 3u; s_cal_info.fails++; return true; }
    const uint32_t cfg = s3_items[3];
    nc = 1u << (cfg & 0x1Fu);
    if (nc > FPGA_TDCCAL_MAX_NC) nc = FPGA_TDCCAL_MAX_NC;
    const uint8_t dual = (uint8_t)((cfg >> 5) & 1u);
    s_cal_info.naddr = (uint16_t)nc;
    for (int attempt = 0; attempt < 3; attempt++) {
        err = 0u;
        if (attempt > 0) s_cal_info.retries++;
        watchdog_kick_fpga();
        /* 1) histogram kanalu ch: stranky po 252 polozkach */
        bool rd_ok = true;
        for (uint16_t p = 0; p * FPGA_V3_LUTW_MAX < nc && rd_ok; p++) {
            uint16_t cnt = 0u;
            rd_ok = v3_page((uint8_t)(1u + ch), p, s3_items, &cnt);
            for (uint32_t k = 0; rd_ok && k < cnt && p * FPGA_V3_LUTW_MAX + k < nc; k++) s_ch_h[ch][p * FPGA_V3_LUTW_MAX + k] = s3_items[k];
        }
        if (!rd_ok) { err = 3u; continue; }
        /* 2) tabulka */
        const uint64_t tot = fpga_tdccal_lut(s_ch_h[ch], nc, FPGA_TDC_CAL_LOG2, dual, s_ch_l[ch]);
        fpga_tdccal_stats(s_ch_h[ch], nc, &s_cal_stats[ch]);
        if (tot != (1ull << FPGA_TDC_CAL_LOG2)) { err = 1u; break; }
        /* 3) zacatek zapisu, 4) davky LUT_WRITE */
        if (!v3_cfg(FPGA_V3_CFG_LUT, 0u, 0u)) { err = 3u; continue; }
        uint16_t sum = 0u; bool wr_ok = true;
        for (uint32_t a = 0; a < nc && wr_ok; a += FPGA_V3_LUTW_MAX) {
            uint32_t m = nc - a; if (m > FPGA_V3_LUTW_MAX) m = FPGA_V3_LUTW_MAX;
            for (uint32_t k = 0; k < m; k++) { s3_lw_ch[k] = ch; s3_lw_a[k] = (uint16_t)(a + k); s3_lw_n[k] = (uint16_t)(s_ch_l[ch][a + k] & 0x3FFFu); }
            if (!spi_lock(200)) { wr_ok = false; break; }
            (void)fpga_v3_build_lutw(s3c_tx, ++s3_mseq, s3_lw_ch, s3_lw_a, s3_lw_n, m, &sum);
            wr_ok = xfer3_raw(s3c_tx, s3c_rx);
            spi_unlock();
        }
        if (!wr_ok) { err = 3u; continue; }
        /* 5) overeni: hlavicka nese pocitadlo a soucet (odpoved na posledni ramec az v dalsi transakci) */
        fpga_v3_hdr_t h;
        /* LUT_CNT_B: LUT_SUM chybu bitu kanalu u cele tabulky nevidi (512 x 2^14 = 0 mod 2^16) */
        if (!v3_hdr_now(&h) || !v3_hdr_now(&h) || h.lut_cnt != (uint16_t)nc || h.lut_sum != sum ||
            h.lut_cnt_b != (uint16_t)(ch ? nc : 0u)) { err = 2u; continue; }
        const uint8_t bank0 = (uint8_t)((h.tdc_status[ch] >> 4) & 1u), valid0 = (uint8_t)(h.tdc_status[ch] & 1u);
        /* 6) potvrzeni -> nova banka na hranici segmentu (<= L) */
        if (!v3_cfg(FPGA_V3_CFG_LUT, 1u, (uint8_t)(1u << ch))) { err = 3u; continue; }   /* p2 = maska: jen tento kanal */
        bool done = false;
        for (int i = 0; i < 20 && !done; i++) {
            osDelay(5);
            if (v3_hdr_now(&h) && !(h.cal_wait & (1u << ch)) && (h.tdc_status[ch] & 1u) &&
                (!valid0 || ((h.tdc_status[ch] >> 4) & 1u) != bank0)) done = true;
        }
        if (!done) { err = 4u; continue; }
        s_cal_info.runs++;
        s_cal_info.last_err = 0u;
        s_cal_info.stats_valid = 1u;
        s_cal_info.last_ms = HAL_GetTick() - t_start;
        s_cal_info.last_tick = HAL_GetTick() ? HAL_GetTick() : 1u;
        return true;
    }
    s_cal_info.fails++;
    s_cal_info.last_err = err;
    s_cal_info.last_ms = HAL_GetTick() - t_start;
    return true;
}
static bool tdccal_poll_v3(void)
{
    if (!s3_h_ok || (s3_h.cal_wait & 3u) == 0u) { s_wait_polls = 0u; return false; }
    if (++s_wait_polls < 2u) return false;                             /* 2 ramce za sebou */
    s_wait_polls = 0u;
    const uint8_t ch = (s3_h.cal_wait & 1u) ? 0u : 1u;                 /* nejdriv A, druhy kanal az v dalsim behu */
    const bool r = tdccal_run_v3(ch);
    s3_regroup();                                                      /* kalibrace blokovala: okna znovu, preruseni */
    return r;
}

/* ── jednorazove deje: rozhrani pro UART (v0.36.0) ── */
void fpga_freq_ev_enable(int on)
{
    if (on) { s3e_gen[0]++; s3e_on = 1u; }
    else s3e_on = 0u;
}

int fpga_freq_ev_pair(uint8_t mode)
{
    static const uint8_t bs[4] = { 0u, 0u, 3u, 1u };    /* SET_CONFIG 0x09 podle rezimu */
    if (mode > FPGA_EVP_SKEW) mode = FPGA_EVP_OFF;
    if (s_proto != 3u) return -2;
    int rc = 1;
    const uint8_t want = bs[mode];
    const uint8_t cap = (uint8_t)(s3_h_ok && (s3_h.caps & FPGA_V3_CAP_BSRC));
    if (s_sim_on) rc = 2;
    else if (want != 0u && !cap) return -1;
    else if (cap && want != s3e_bsrc) {
        s3e_gen[3]++;                                   /* parovat az za drzenim B (pred prikazem: zadne zavody) */
        if (!v3_cfg(FPGA_V3_CFG_BSRC, want, 0u)) return 0;
        s3e_bsrc = want;
    }
    if (mode == FPGA_EVP_AB || mode == FPGA_EVP_PW) s3e_gen[1]++;
    if (mode == FPGA_EVP_SKEW) s3e_gen[2]++;
    s3e_pair = mode;
    return rc;
}

void fpga_freq_ev_info(fpga_ev_info_t *o)
{
    if (o == NULL) return;
    memset(o, 0, sizeof *o);
    fpga_v3_stat_t st, sk;
    const uint32_t pm = __get_PRIMASK();
    __disable_irq();
    st = s3e_st; sk = s3e_pr.sk;
    o->ev_total = s3e_w; o->ab_lost = s3e_pr.ab.lost; o->ab_fill = s3e_abw;
    __set_PRIMASK(pm);
    o->ev_fill = (o->ev_total < S3E_RING) ? o->ev_total : S3E_RING;
    if (o->ab_fill > S3E_AB) o->ab_fill = S3E_AB;
    o->proto = s_proto; o->sim = (uint8_t)(s_sim_on && s_sim_v3);
    o->ev_on = s3e_on; o->pair = s3e_pair; o->bsrc = s3e_bsrc;
    o->ab_n = st.n; o->ab_mean = fpga_v3_stat_mean(&st); o->ab_sd = fpga_v3_stat_sd(&st); o->ab_min = st.min; o->ab_max = st.max;
    o->sk_n = sk.n; o->sk_mean = fpga_v3_stat_mean(&sk); o->sk_sd = fpga_v3_stat_sd(&sk); o->sk_min = sk.min; o->sk_max = sk.max;
    if (s3_h_ok) {
        o->fw = s3_h.fw_version; o->caps = s3_h.caps; o->echo = (uint8_t)((s3_h.cfg_echo >> 3) & 3u);
        o->pps_seq = s3_h.pps[0].pseq;
    }
}

int fpga_freq_ev_get(uint32_t back, fpga_ev_rec_t *o)
{
    int ok = 0;
    const uint32_t pm = __get_PRIMASK();
    __disable_irq();
    const uint32_t fill = (s3e_w < S3E_RING) ? s3e_w : S3E_RING;
    if (o != NULL && back < fill) { *o = s3e_ring[(s3e_w - 1u - back) % S3E_RING]; ok = 1; }
    __set_PRIMASK(pm);
    return ok;
}

int fpga_freq_ab_get(uint32_t back, double *ps)
{
    int ok = 0;
    const uint32_t pm = __get_PRIMASK();
    __disable_irq();
    const uint32_t fill = (s3e_abw < S3E_AB) ? s3e_abw : S3E_AB;
    if (ps != NULL && back < fill) { *ps = s3e_ab[(s3e_abw - 1u - back) % S3E_AB]; ok = 1; }
    __set_PRIMASK(pm);
    return ok;
}

/* RAW_CAPTURE: zadost (p0 = kanal), cekani na pocet (souhrnna stranka polozka 5), stranky druhu 3 po 252 polozkach
 * (126 znacek, dve polozky u24 = dolni a horni polovina u48). Vlastni buffer stranky (s3_items patri kalibraci). */
static uint32_t s3r_items[FPGA_V3_LUTW_MAX];
/* v0.39.0: cnt znacek z okna RAW v BSRAM (stranky druhu 3) do statistiky; st->n, *first a *prev navazuji pres volani
 * (hluboky zaznam po oknech). @return 0 ok, -1 stranka nedorazila */
static int s3_raw_pages(uint32_t cnt, uint32_t clk, fpga_raw_cb_t cb, void *ctx, fpga_raw_info_t *st,
                        fpga_v3_stat_t *ds, uint64_t *first, uint64_t *prev)
{
    const double up = fpga_v3_unit_ps(clk);
    uint32_t taken = 0u;
    for (uint16_t p = 0; (uint32_t)p * FPGA_V3_LUTW_MAX < 2u * cnt; p++) {
        uint16_t m = 0u;
        bool ok = false;
        for (int a = 0; a < 3 && !ok; a++) ok = v3_page(3u, p, s3r_items, &m);
        if (!ok) return -1;
        for (uint32_t k = 0; k + 1u < m && taken < cnt; k += 2u) {
            const uint64_t mk = (uint64_t)s3r_items[k] | ((uint64_t)s3r_items[k + 1u] << 24);
            const double d = st->n ? (double)((mk - *prev) & 0xFFFFFFFFFFFFull) * up : 0.0;
            if (st->n) fpga_v3_stat_add(ds, d); else *first = mk;
            if (cb) cb(ctx, st->n, s3e_ps(mk, clk), d);
            *prev = mk; st->n++; taken++;
        }
    }
    return 0;
}

/* audit 2026-10-23 (F-0272): T_RAW s potvrzenim. FPGA (FW 0x0505) zvysi pri kazdem prijatem prikazu pocitadlo v bajtu
 * 127 (Gray, mod 16; bit7 = kanal zaznamu); odmitnuty prikaz (bezi jiny rezim) ho nezmeni -- driv STM vzal stara data
 * jineho zaznamu nebo kanalu. Stav v ramci je o transakci starsi, hlavicka se proto cte opakovane. FPGA bez CAPS bit21
 * (FW < 0x0505) pocitadlo nema: prikaz se jen posle. @return 1 prijato (*h = hlavicka po prijeti), 0 odmitnuto, -3 SPI */
static int s3_raw_cmd(uint8_t p0, uint32_t arg, fpga_v3_hdr_t *h)
{
    if (!v3_hdr_now(h) || !v3_hdr_now(h)) return -3;
    const bool ack = (h->caps & FPGA_V3_CAP_PSRAM) != 0u;
    const uint8_t a0 = FPGA_V3_DACK_N(h->deep_ack);
    if (!spi_lock(200)) return -3;
    fpga_v3_build(s3c_tx, FPGA_V3_T_RAW, ++s3_mseq,
                  (const uint8_t[4]){ p0, (uint8_t)arg, (uint8_t)(arg >> 8), (uint8_t)(arg >> 16) }, 4u);
    const bool sent = xfer3_raw(s3c_tx, s3c_rx);
    spi_unlock();
    if (!sent) return -3;
    if (!ack) return 1;
    for (int i = 0; i < 20; i++) {
        osDelay(2);
        if (v3_hdr_now(h) && FPGA_V3_DACK_N(h->deep_ack) != a0) return 1;
    }
    return 0;
}

/* hluboky zaznam: STOP (rezim 1 pri bezicim zaznamu; zbytek FIFO se dopise) a cekani na RUN = 0. @return 1 stoji, 0 ne, -3 */
static int s3_deep_stop(uint8_t ch, fpga_v3_hdr_t *h)
{
    if (s3_raw_cmd((uint8_t)((ch & 1u) | (1u << 1)), 0u, h) < 0) return -3;
    for (int i = 0; i < 50 && (h->deep_st & FPGA_V3_DS_RUN); i++) {
        osDelay(2);
        if (!v3_hdr_now(h)) return -3;
    }
    return (h->deep_st & FPGA_V3_DS_RUN) ? 0 : 1;
}

/* v0.39.0 (FW 0x0505): hluboky RAW do PSRAM -- viz fpga_freq.h */
int fpga_freq_v3_raw_deep(uint8_t ch, uint32_t n, uint32_t timeout_ms, fpga_raw_cb_t cb, void *ctx, fpga_raw_info_t *st,
                          uint8_t *deep_st)
{
    fpga_raw_info_t z;
    uint8_t dz;
    if (st == NULL) st = &z;
    if (deep_st == NULL) deep_st = &dz;
    memset(st, 0, sizeof *st);
    *deep_st = 0u;
    if (s_proto != 3u || s_sim_on) return -2;
    fpga_v3_hdr_t h;
    if (!v3_hdr_now(&h)) return -3;
    if (!(h.caps & FPGA_V3_CAP_PSRAM) || !(h.deep_st & FPGA_V3_DS_RDY)) { *deep_st = h.deep_st; return -4; }
    if (n == 0u || n > FPGA_V3_DEEP_MAX) n = FPGA_V3_DEEP_MAX;
    int rc;
    if ((h.deep_st & FPGA_V3_DS_RUN) && (rc = s3_deep_stop(ch, &h)) <= 0) return (rc < 0) ? -3 : -5;   /* drivejsi zaznam */
    rc = s3_raw_cmd((uint8_t)((ch & 1u) | (1u << 1)), n, &h);
    if (rc <= 0) { *deep_st = h.deep_st; return (rc < 0) ? -3 : -5; }
    const uint32_t t0 = HAL_GetTick();
    while ((h.deep_st & FPGA_V3_DS_RUN) && (HAL_GetTick() - t0) < timeout_ms) {   /* konec zaznamu: RUN zhasne */
        osDelay(100);                                      /* kazdy dotaz je transakce SPI (maska pri CS = mezera) */
        (void)v3_hdr_now(&h);
    }
    int part = 0;
    if (h.deep_st & FPGA_V3_DS_RUN) {                      /* timeout: zastavit a precist, co se zapsalo */
        if ((rc = s3_deep_stop(ch, &h)) <= 0) { *deep_st = h.deep_st; return (rc < 0) ? -3 : 0; }
        part = 1;
    }
    *deep_st = h.deep_st;
    st->dst = h.deep_st;
    st->cnt = h.deep_n;
    if (((h.deep_ack & FPGA_V3_DACK_CH) != 0u) != ((ch & 1u) != 0u)) return -5;   /* zaznam jineho kanalu */
    const uint32_t clk = h.clk_mhz ? h.clk_mhz : 100u;
    fpga_v3_stat_t ds;
    fpga_v3_stat_init(&ds);
    uint64_t first = 0u, prev = 0u;
    for (uint32_t w = 0u; (uint64_t)w * 512u < st->cnt; w++) {
        rc = s3_raw_cmd((uint8_t)((ch & 1u) | (2u << 1)), w, &h);
        if (rc <= 0) return (rc < 0) ? -3 : -5;
        const uint32_t t1 = HAL_GetTick();
        bool ok = false;
        do {                                               /* okno: 24 davek z PSRAM, desitky us */
            osDelay(2);
            ok = v3_hdr_now(&h) && (h.deep_st & FPGA_V3_DS_WDONE) && !(h.deep_st & FPGA_V3_DS_WLOAD) && h.deep_win == (uint16_t)w;
        } while (!ok && (HAL_GetTick() - t1) < 200u);
        if (!ok || (h.deep_st & FPGA_V3_DS_WERR)) return -1;
        const uint32_t cw = (st->cnt - w * 512u > 512u) ? 512u : st->cnt - w * 512u;
        if (s3_raw_pages(cw, clk, cb, ctx, st, &ds, &first, &prev) < 0) return -1;
    }
    const double up = fpga_v3_unit_ps(clk);
    st->d_mean = fpga_v3_stat_mean(&ds); st->d_sd = fpga_v3_stat_sd(&ds); st->d_min = ds.min; st->d_max = ds.max;
    st->span_ps = st->n ? (double)((prev - first) & 0xFFFFFFFFFFFFull) * up : 0.0;
    return (!part && st->n >= n && !(*deep_st & FPGA_V3_DS_OVF)) ? 1 : 0;
}

int fpga_freq_v3_raw(uint8_t ch, uint32_t timeout_ms, fpga_raw_cb_t cb, void *ctx, fpga_raw_info_t *st)
{
    fpga_raw_info_t z;
    if (st == NULL) st = &z;
    memset(st, 0, sizeof *st);
    if (s_proto != 3u || s_sim_on) return -2;
    fpga_v3_hdr_t h;
    const int rc = s3_raw_cmd((uint8_t)(ch & 1u), 0u, &h);
    if (rc <= 0) return (rc < 0) ? -3 : -5;
    const uint32_t t0 = HAL_GetTick();
    do {
        osDelay(20);
        uint16_t n = 0u;
        if (v3_page(0u, 0u, s3r_items, &n) && n > 5u) st->cnt = s3r_items[5];
    } while (st->cnt < 512u && (HAL_GetTick() - t0) < timeout_ms);
    if (st->cnt > 512u) st->cnt = 512u;
    if (v3_hdr_now(&h)) st->dst = h.deep_st;
    const uint32_t clk = h.clk_mhz ? h.clk_mhz : 100u;
    fpga_v3_stat_t ds;
    fpga_v3_stat_init(&ds);
    uint64_t first = 0u, prev = 0u;
    if (s3_raw_pages(st->cnt, clk, cb, ctx, st, &ds, &first, &prev) < 0) return -1;
    const double up = fpga_v3_unit_ps(clk);
    st->d_mean = fpga_v3_stat_mean(&ds); st->d_sd = fpga_v3_stat_sd(&ds); st->d_min = ds.min; st->d_max = ds.max;
    st->span_ps = st->n ? (double)((prev - first) & 0xFFFFFFFFFFFFull) * up : 0.0;
    return (st->n >= 512u) ? 1 : 0;
}

/* Stav v3 pro UART `fpga v3` */
void fpga_freq_v3_info(fpga_v3_info_t *o)
{
    if (o == NULL) return;
    memset(o, 0, sizeof *o);
    o->proto = s_proto; o->sim = s_sim_v3;
    o->frames = s3_frames; o->recs = s3_link.recs; o->dups = s3_link.dups; o->gaps = s3_link.gaps;
    o->gap_recs = s3_link.gap_recs; o->resync = s3_link.resync; o->wins = s3_wins; o->qdrop = s3_qdrop; o->pages = s3_pages;
    o->k = s3_grp.k_target; o->seg_ms = s3_grp.seg_ms; o->chan = s3_chan; o->expect = s3_link.expect;
    o->pr_mode[0] = s3_pr_mode[0]; o->pr_mode[1] = s3_pr_mode[1]; o->pr_sw[0] = s3_pr_sw[0]; o->pr_sw[1] = s3_pr_sw[1];
    if (s3_h_ok) {
        o->fw = s3_h.fw_version; o->caps = s3_h.caps; o->rec_head = s3_h.rec_head; o->rec_first = s3_h.rec_first;
        o->ring_fill = s3_h.ring_fill; o->rx_crc_err = s3_h.rx_crc_err; o->rx_len_err = s3_h.rx_len_err;
        o->flags = s3_h.flags; o->tdc[0] = s3_h.tdc_status[0]; o->tdc[1] = s3_h.tdc_status[1]; o->cal_wait = s3_h.cal_wait;
        o->pps_status = s3_h.pps_status; o->pps_pflags = s3_h.pps[0].pflags; o->pps_d = s3_h.pps[0].d;
        o->presc[0] = s3_h.presc[0]; o->presc[1] = s3_h.presc[1]; o->probe[0] = s3_h.probe[0]; o->probe[1] = s3_h.probe[1];
        o->deep_st = s3_h.deep_st; o->deep_wsel = s3_h.deep_wsel; o->deep_n = s3_h.deep_n;
    }
}

/* CAL report v3: souhrnna stranka (druh 0) -> maxtap, konfigurace TDC; stav z hlavicky; ovf/peak/nz/last z histogramu (STM) */
static bool tdc_report_v3(fpga_tdc_cal_t *out)
{
    uint16_t n = 0u;
    if (!v3_page(0u, 0u, s3_items, &n) || n < 4u) return false;
    fpga_v3_hdr_t h;
    if (!v3_hdr_now(&h)) return false;
    for (int ch = 0; ch < 2; ch++) {
        if (s_cal_info.stats_valid) {
            out->ovf[ch] = s_cal_stats[ch].ovf; out->peak[ch] = s_cal_stats[ch].peak;
            out->nz[ch]  = s_cal_stats[ch].nz;  out->last[ch] = s_cal_stats[ch].last;
        } else { out->ovf[ch] = 0u; out->peak[ch] = 0u; out->nz[ch] = 0u; out->last[ch] = 0u; }
    }
    out->maxtap[0] = (uint16_t)s3_items[1];
    out->maxtap[1] = (uint16_t)s3_items[2];
    out->status    = s3_tdc_v2(&h);
    out->cal_mode  = 0u;
    const uint16_t cfg = (uint16_t)s3_items[3];
    out->cfg_valid = (cfg & 0x1Fu) != 0u;
    out->naddr     = out->cfg_valid ? (uint16_t)(1u << (cfg & 0x1Fu)) : 256u;
    out->dual      = (uint8_t)((cfg >> 5) & 1u);
    out->stride1   = (uint8_t)((cfg >> 6) & 1u);
    out->taps      = out->cfg_valid ? (uint16_t)(cfg >> 7) : 256u;
    if (!(out->status & FPGA_TDC_CAL_BUSY) && (out->status & (FPGA_TDC_CAL_A | FPGA_TDC_CAL_B)) == (FPGA_TDC_CAL_A | FPGA_TDC_CAL_B))
        rez_store(out);
    return true;
}

/* hist[k] obou kanalu v3: stranka k / 252 druhu 1 (A) a 2 (B). UART `tdc hist` cte adresy po jedne -> posledni stranka
 * kazdeho kanalu se drzi 2 s (512 adres = 6 stranek misto 1024). */
static uint32_t s3_hc[2][FPGA_V3_LUTW_MAX];
static uint16_t s3_hc_n[2];
static int32_t  s3_hc_page[2] = { -1, -1 };
static uint32_t s3_hc_ms[2];
static bool tdc_hist_v3(uint16_t k, uint32_t *a, uint32_t *b)
{
    const uint16_t p = (uint16_t)(k / FPGA_V3_LUTW_MAX), i = (uint16_t)(k % FPGA_V3_LUTW_MAX);
    const uint32_t now = HAL_GetTick();
    for (int ch = 0; ch < 2; ch++) {
        if (s3_hc_page[ch] != (int32_t)p || (now - s3_hc_ms[ch]) > 2000u) {
            s3_hc_page[ch] = -1;
            if (!v3_page((uint8_t)(1 + ch), p, s3_hc[ch], &s3_hc_n[ch])) return false;
            s3_hc_page[ch] = (int32_t)p; s3_hc_ms[ch] = now;
        }
        if (i >= s3_hc_n[ch]) return false;
    }
    *a = s3_hc[0][i];
    *b = s3_hc[1][i];
    return true;
}

bool fpga_freq_tdccal_poll(void)
{
    if (!g_init_ok || s_sim_on) return false;
    if (s_proto == 3u) return tdccal_poll_v3();
    fpga_meas_t *const f = &s_tc_f0;
    if (!fpga_freq_get_frame(f) || !(f->caps & FPGA_CAP_STMCAL) || (f->lut_wait & 3u) == 0u) { s_wait_polls = 0u; return false; }
    s_wait_polls++;
    const bool both = ((f->lut_wait & 3u) == 3u);
    if (!((both && s_wait_polls >= 2u) || s_wait_polls >= 20u)) return false;   /* 2 ramce za sebou (nebo 1 kanal > ~1 s) */
    s_wait_polls = 0u;
    return tdccal_run();
}

void fpga_freq_tdccal_info(fpga_tdccal_info_t *o)
{
    if (o == NULL) return;
    const uint32_t pm = __get_PRIMASK();
    __disable_irq();
    *o = s_cal_info;
    __set_PRIMASK(pm);
}

bool fpga_freq_raw_xfer(uint8_t *rx_frame)
{
    if (!g_init_ok) return false;
    if (s_proto == 3u && !s_sim_on) {                      /* v3: 1024 B, volajici dostane prvnich FPGA_FRAME_LEN B */
        if (!spi_lock(200)) return false;
        fpga_v3_build_ack(s3c_tx, ++s3_mseq, s3_link.expect);
        const bool ok = xfer3_raw(s3c_tx, s3c_rx);
        if (ok) memcpy(rx_frame, s3c_rx, FR_LEN);
        spi_unlock();
        return ok;
    }
    uint8_t tx[FR_LEN];
    build_frame(TYPE_ACK, g_last_seq, NULL, 0, tx);   /* posleme platny ACK ramec */
    return xfer(tx, rx_frame);
}

/* ── Takt SPI za behu (STATUS #314): `spi <MHz>` v UART. Zvoli nejvyssi stupen kernel/2^n <= cil a pod SPI mutexem
 * prepise prescaler pres HAL_SPI_Init (stav READY -> MspInit/piny se NEMENI). Nepersistuje se, po resetu plati
 * FPGA_SCK_TARGET_HZ. Vraci skutecny takt [Hz], 0 = zamitnuto/selhalo. ── */
static const struct { uint32_t e, d; } k_sck_pre[8] = {
    { SPI_BAUDRATEPRESCALER_2, 2 }, { SPI_BAUDRATEPRESCALER_4, 4 }, { SPI_BAUDRATEPRESCALER_8, 8 }, { SPI_BAUDRATEPRESCALER_16, 16 },
    { SPI_BAUDRATEPRESCALER_32, 32 }, { SPI_BAUDRATEPRESCALER_64, 64 }, { SPI_BAUDRATEPRESCALER_128, 128 }, { SPI_BAUDRATEPRESCALER_256, 256 } };

uint32_t fpga_freq_sck_hz(void) { return g_sck_hz; }

/* Krok 7: SPI DMA za behu (UART `spi dma on|off`). on = 1 zapnout (prvni zapnuti nastavi DMA streamy a NVIC), 0 vypnout,
 * -1 jen zjistit. Pod SPI mutexem: prepnuti nikdy neprobehne uprostred prenosu. @return 1 = DMA pouzivano. */
int fpga_freq_spi_dma(int on)
{
#if FPGA_SPI_USE_DMA
    if (on >= 0 && g_init_ok) {
        if (!spi_lock(200)) return fpga_spi_dma_ready() ? 1 : 0;
        if (on) {
            if (fpga_spi_dma_hw_init(&hspi2) && fpga_spi_dma_init(&hspi2)) fpga_spi_dma_enable(true);
        } else fpga_spi_dma_enable(false);
        spi_unlock();
    }
    return fpga_spi_dma_ready() ? 1 : 0;
#else
    (void)on;
    return 0;
#endif
}

/* Rychlost vystupu SCK (PI1) a MOSI (PB15) ctena z OSPEEDR (0 LOW .. 3 VERY_HIGH), pro UART `spi` (zkouska 25 MHz). */
void fpga_freq_spi_pin_speed(uint8_t *sck, uint8_t *mosi)
{
    if (sck)  *sck  = (uint8_t)((GPIOI->OSPEEDR >> (1u * 2u)) & 3u);
    if (mosi) *mosi = (uint8_t)((GPIOB->OSPEEDR >> (15u * 2u)) & 3u);
}

uint32_t fpga_freq_sck_stage(uint32_t i, uint32_t *kernel_hz)
{
    uint32_t k = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI123);
    if (kernel_hz) *kernel_hz = k;
    return (i < 8u) ? k / k_sck_pre[i].d : 0u;
}

uint32_t fpga_freq_set_sck(uint32_t target_hz)
{
    if (!g_init_ok || target_hz == 0u || target_hz > FPGA_SCK_MAX_HZ) return 0u;
    uint32_t k = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI123), chosen = SPI_BAUDRATEPRESCALER_256, div = 256u;
    for (int i = 0; i < 8; i++) if (k / k_sck_pre[i].d <= target_hz) { chosen = k_sck_pre[i].e; div = k_sck_pre[i].d; break; }
    bool locked = false;
    if (s_spi_mtx != NULL && osKernelGetState() == osKernelRunning) {
        if (osMutexAcquire(s_spi_mtx, 200) != osOK) return 0u;
        locked = true;
    }
    uint32_t prev = hspi2.Init.BaudRatePrescaler;
    hspi2.Init.BaudRatePrescaler = chosen;
    if (HAL_SPI_Init(&hspi2) != HAL_OK) {
        hspi2.Init.BaudRatePrescaler = prev;
        (void)HAL_SPI_Init(&hspi2);
        if (locked) osMutexRelease(s_spi_mtx);
        return 0u;
    }
    g_sck_hz = k / div;
    if (locked) osMutexRelease(s_spi_mtx);
    return g_sck_hz;
}

void fpga_freq_format_status(char *buf, int buflen)
{
    uint32_t mhz_i = g_sck_hz / 1000000u;
    uint32_t mhz_f = (g_sck_hz % 1000000u) / 10000u;   /* 2 desetinna mista MHz */

    if (!s_link_ok) {
        /* bring-up diagnostika: prosel HW prenos? co je na MISO?
         * HAL:ERR  -> SPI periferie/piny/clock (HAL_SPI_TransmitReceive selhal)
         * RX0:FF   -> MISO ctene jako 1 (FPGA nebudi / nezapojeno / nedokonfig.)
         * RX0:00   -> MISO ctene jako 0 (totez, opacny klid)
         * RX0:A5   -> MAGIC OK ale CRC/typ selhal -> viz CRC pocitadlo */
        snprintf(buf, buflen, "SPI %lu.%02luMHZ NOLINK HAL:%s RX0:%02X CRC:%lu",
                 (unsigned long)mhz_i, (unsigned long)mhz_f,
                 g_xfer_ok ? "OK" : "ERR", (unsigned)g_last_rx0,
                 (unsigned long)g_rx_crc);
        return;
    }

    char seq[16];
    if (g_last_seq == 0xFFFFFFFFu) { seq[0] = '-'; seq[1] = '\0'; }
    else snprintf(seq, sizeof(seq), "%lu", (unsigned long)g_last_seq);

    snprintf(buf, buflen, "SPI %lu.%02luMHZ LINK:OK SEQ:%s CRC:%lu",
             (unsigned long)mhz_i, (unsigned long)mhz_f,
             seq, (unsigned long)g_rx_crc);
}
