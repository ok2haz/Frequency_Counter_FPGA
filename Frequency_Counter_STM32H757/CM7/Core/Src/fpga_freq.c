/**
 * @file    fpga_freq.c
 * @brief   SPI2 master driver pro FPGA citac kmitoctu. Viz fpga_freq.h.
 */
#include "fpga_freq.h"
#include "stm32h7xx_hal.h"
#include "cmsis_os2.h"   /* SPI2 mutex: FpgaTask poll vs UART fpgaraw/fpgaloop */
#include <string.h>
#include <stdio.h>

extern SPI_HandleTypeDef hspi2;

/* === CS pin (PB12, drive manualni GPIO, active-low) === */
#define FPGA_CS_GPIO_Port   GPIOB
#define FPGA_CS_Pin         GPIO_PIN_12

/* === SPI rychlost ===
 * Kontrakt FPGA (GW1NR-9, oversampling SCK na 100 MHz): cil <= 6 MHz,
 * absolutni maximum ~10 MHz. NEPREKRACOVAT. */
#define FPGA_SCK_TARGET_HZ  1000000u   /* 1 MHz bring-up */
#define FPGA_SCK_MAX_HZ     10000000u  /* tvrdy strop dle kontraktu */
#if FPGA_SCK_TARGET_HZ > FPGA_SCK_MAX_HZ
#error "FPGA_SCK_TARGET_HZ prekracuje povolene maximum FPGA slave (~10 MHz)"
#endif

/* === Frame === */
#define FR_LEN        64
#define FR_MAGIC      0xA5
#define FR_VERSION    0x01
#define FR_PAYLOAD    12               /* offset payloadu */

/* TYPE */
#define TYPE_ACK      0x06
#define TYPE_START    0x08
#define TYPE_STOP     0x09
#define TYPE_DATA     0x80

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
static uint32_t g_sck_hz   = 0;
static uint8_t  s_link_ok  = 0;            /* 1 = posledni poll dostal platny ramec (MAGIC+CRC) */
static uint32_t g_rx_crc   = 0;            /* pocet ramcu se spatnym CRC */
static uint8_t  g_init_ok  = 0;            /* 1 = init+selftest OK, smime komunikovat */
static uint8_t  g_xfer_ok  = 0;            /* 1 = posledni HAL_SPI prenos vratil HAL_OK */
static uint8_t  g_last_rx0 = 0;            /* prvni prijaty bajt z MISO (bring-up diag) */
static fpga_meas_t s_last;                 /* posledni naparsovany DATA ramec (i nefresh/invalid) */
static uint8_t  s_last_seen = 0;           /* 1 = aspon jeden DATA ramec dorazil */

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

/* === CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, xorout 0) === */
static uint16_t crc16_ccitt(const uint8_t *d, int n)
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
    if (plen > 50) plen = 50;      /* klamp PRED zapisem PAYLOAD_LEN (konzistence pole vs. data) */
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
    uint16_t crc = crc16_ccitt(f, 62);
    f[62] = (uint8_t)(crc);        /* low byte */
    f[63] = (uint8_t)(crc >> 8);   /* high byte */
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

static bool xfer(const uint8_t *tx, uint8_t *rx)
{
    bool locked = false;
    if (s_spi_mtx != NULL && osKernelGetState() == osKernelRunning) {
        if (osMutexAcquire(s_spi_mtx, 100) != osOK) return false;
        locked = true;
    }

    cs_low();
    delay_us(FPGA_CS_GAP_US);                         /* CS low -> 1. SCK (>=1 us) */
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)tx, rx, FR_LEN, 100);
    delay_us(FPGA_CS_GAP_US);                         /* posledni SCK -> CS high (>=1 us) */
    cs_high();
    delay_us(FPGA_FRAME_GAP_US);                      /* pauza mezi ramci (>=20 us) */

    if (locked) osMutexRelease(s_spi_mtx);
    return (st == HAL_OK);
}

void fpga_freq_restart(void)
{
    if (!g_init_ok) return;
    uint8_t tx[FR_LEN], rx[FR_LEN];
    build_frame(TYPE_START, 0, NULL, 0, tx);
    xfer(tx, rx);
}

bool fpga_freq_link_ok(void)
{
    return s_link_ok != 0;
}

/* Akceptacni krok 1: CRC self-test. crc16("123456789") MUSI byt 0x29B1,
 * jinak nase CRC neodpovida FPGA a nesmime nic posilat. */
bool fpga_freq_crc_selftest(void)
{
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
    HAL_GPIO_Init(FPGA_CS_GPIO_Port, &gi);

    cs_high();   /* CS idle (deasserted) */

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
    m->sequence           = rd_le32(&rx[4]);
    m->status_flags       = rx[3];
}

bool fpga_freq_poll(fpga_meas_t *out)
{
    static uint8_t tx[FR_LEN], rx[FR_LEN];

    if (!g_init_ok) return false;   /* selftest neprosel -> neposilat nic (kontrakt 7.1) */

    /* ACK posledni potvrzene seq (potvrdi predchozi + vyclockuje aktualni ramec) */
    build_frame(TYPE_ACK, g_last_seq, NULL, 0, tx);
    if (!xfer(tx, rx))     { g_xfer_ok = 0; s_link_ok = 0; return false; }
    g_xfer_ok  = 1;
    g_last_rx0 = rx[0];                                /* diagnostika: co prislo na MISO */

    if (rx[0] != FR_MAGIC) { s_link_ok = 0; return false; }

    uint16_t crc_calc = crc16_ccitt(rx, 62);
    uint16_t crc_rx   = (uint16_t)rx[62] | ((uint16_t)rx[63] << 8);
    if (crc_calc != crc_rx) { g_rx_crc++; s_link_ok = 0; return false; }

    s_link_ok = 1;   /* platny ramec dorazil -> link je ziva (i kdyz neni nove mereni) */

    uint8_t type   = rx[2];
    uint8_t status = rx[3];
    if (type != TYPE_DATA) return false;

    /* Latch KAZDEHO platneho DATA ramce (i kdyz neni fresh/valid) -> STM vidi
     * error_flags/SIGNAL_LOST/phase_status i pri ztrate signalu (kdy VALID=0).
     * Parse do lokalu + kopie pod kratkym IRQ-off: s_last cte i UiTask pres
     * fpga_freq_get_last() (okno Citac) — bez toho by mohl videt roztrzeny
     * ramec (64 B, vic nez jedna 32bit operace). */
    fpga_meas_t tmp;
    parse_data(rx, &tmp);
    {
        uint32_t pm = __get_PRIMASK();
        __disable_irq();
        s_last = tmp;
        s_last_seen = 1;
        __set_PRIMASK(pm);
    }

    if (!(status & ST_DATA_VALID) || !(status & ST_DATA_FRESH)) return false;
    if (s_last.sequence == g_last_seq) return false;   /* neni nove */

    if (out) *out = s_last;
    g_last_seq = s_last.sequence;                       /* dalsi ACK potvrdi tuto seq */
    return true;
}

bool fpga_freq_signal_lost(void)
{
    return s_last_seen && (s_last.error_flags & FPGA_ERR_SIGNAL_LOST);
}

bool fpga_freq_get_last(fpga_meas_t *out)
{
    /* Kratky IRQ-off (~64 B memcpy) = vzajemne vylouceni s latchem ve
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

uint64_t fpga_freq_select(const fpga_meas_t *m, int *used16)
{
    /* /4 ma nejlepsi rozliseni; nad ~380 MHz je pin28 (/4 -> ~95 MHz) u stropu -> /16.
     * Sticky stav (jediny konzument = FpgaTask): drzi zvoleny zdroj, prepina jen
     * pri prekroceni prahu s hysterezi nebo pri chybe aktivniho zdroje. */
    static int s_use16 = 0;
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
    printf("fpga: select hystereze selftest %s\n", ok ? "OK" : "FAIL");
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

    /* chybovy tag (priorita: ztrata > overflow > div chyby) */
    const char *etag = "";
    if      (m->error_flags & FPGA_ERR_SIGNAL_LOST) etag = " LOST";
    else if (m->error_flags & FPGA_ERR_OVERFLOW)    etag = " OVF";
    else if (m->error_flags & FPGA_ERR_MEAS)        etag = " E/4";
    else if (m->status2     & FPGA_ST2_DIV16_ERR)   etag = " E/16";

    snprintf(buf, buflen, "%s PH:%X/%X GATE:%sNS SEQ:%lu%s",
             use16 ? "/16" : "/4", present, fine, g, (unsigned long)m->sequence, etag);
}

bool fpga_freq_raw_xfer(uint8_t *rx64)
{
    if (!g_init_ok) return false;
    uint8_t tx[FR_LEN];
    build_frame(TYPE_ACK, g_last_seq, NULL, 0, tx);   /* posleme platny ACK ramec */
    return xfer(tx, rx64);
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
