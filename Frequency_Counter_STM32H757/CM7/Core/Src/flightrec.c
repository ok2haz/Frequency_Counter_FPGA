/**
 * @file    flightrec.c
 * @brief   Flight recorder — kontext systemu tesne pred resetem. Viz flightrec.h.
 */
#include "flightrec.h"
#include "w25q.h"
#include "w25q_map.h"
#include "freertos_shared.h"   /* g_rtos_*, g_sensors, g_uptime_s, qspiMutexHandle */
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"
#include <stdio.h>
#include <stdlib.h>   /* abs — desetinna cast zapornych teplot ve vypisu */
#include <string.h>

/* Hlavicka dumpu (16 B, at je zarovnani stejne jako u vzorku). */
#define FR_MAGIC        0x46523031u   /* "FR01" */
#define FR_LOCK_MS      50u           /* QSPI mutex: dump se deje pri poruse, necekat dlouho */

/* Jeden vzorek = 16 B. Rucni serializace (jako datalog) — layout nezavisly na
 * kompilatoru, at se dump da precist i jinym nastrojem. */
typedef struct {
    uint16_t uptime_s;      /* uptime pri vzorku (orezany na 16 bit = ~18 h) */
    uint8_t  cpu_pct;
    uint8_t  flags;         /* FR_F_* */
    uint16_t heap_free_256; /* volny heap / 256 */
    uint16_t stack_min_8;   /* NEJMENSI volny stack ze vsech tasku / 8 */
    int16_t  t_ocxo_c10;    /* teplota OCXO [0,1 C] */
    int16_t  t_board_c10;
    uint16_t i2c_err;       /* soucet err_streak pres senzory (bez neosazeneho 0x4A) */
    uint16_t spare;
} fr_rec_t;

#define FR_F_GPS_FIX    (1u << 0)
#define FR_F_FPGA_LINK  (1u << 1)
#define FR_F_FREQ_STALE (1u << 2)
#define FR_F_CM4_ALIVE  (1u << 3)

static fr_rec_t  s_ring[FR_DEPTH];
static uint16_t  s_head;          /* kam se zapise pristi vzorek */
static uint16_t  s_count;
static uint32_t  s_next_ms;
static uint8_t   s_ready;         /* flash pripravena (predem smazany sektor) */
static uint8_t   s_have_dump;     /* ve flash je platny zaznam */
static uint32_t  s_write_off;     /* offset predem smazaneho ciloveho sektoru */
static uint32_t  s_read_off;      /* offset posledniho platneho dumpu */
static uint8_t   s_dumped;        /* uz jsme za tohohle behu dumpli (jen 1x) */

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* CRC-16/CCITT-FALSE — stejne jako datalog/w25q_store (jeden algoritmus v projektu). */
static uint16_t fr_crc16(const uint8_t *d, uint32_t n)
{
    uint16_t c = 0xFFFF;
    for (uint32_t i = 0; i < n; i++) {
        c ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

static void rec_pack(uint8_t *b, const fr_rec_t *r)
{
    put16(b + 0,  r->uptime_s);
    b[2] = r->cpu_pct;
    b[3] = r->flags;
    put16(b + 4,  r->heap_free_256);
    put16(b + 6,  r->stack_min_8);
    put16(b + 8,  (uint16_t)r->t_ocxo_c10);
    put16(b + 10, (uint16_t)r->t_board_c10);
    put16(b + 12, r->i2c_err);
    put16(b + 14, r->spare);
}

static void rec_unpack(const uint8_t *b, fr_rec_t *r)
{
    r->uptime_s      = get16(b + 0);
    r->cpu_pct       = b[2];
    r->flags         = b[3];
    r->heap_free_256 = get16(b + 4);
    r->stack_min_8   = get16(b + 6);
    r->t_ocxo_c10    = (int16_t)get16(b + 8);
    r->t_board_c10   = (int16_t)get16(b + 10);
    r->i2c_err       = get16(b + 12);
    r->spare         = get16(b + 14);
}

/* ── Flash: hlavicka dumpu (16 B) + FR_DEPTH vzorku po 16 B ─────────────────
 * seq roste s kazdym dumpem -> nejnovejsi = nejvyssi seq (stejny princip jako
 * datalog find_head, jen nad 64 sektory). */
static void hdr_pack(uint8_t *b, uint32_t seq, uint16_t n, uint32_t up, const char *reason)
{
    memset(b, 0, FR_REC_SIZE);
    put32(b + 0, FR_MAGIC);
    put32(b + 4, seq);
    put16(b + 8, n);
    /* uptime v okamziku dumpu (sekundy, orez na 16 bit) */
    put16(b + 10, (uint16_t)up);
    /* duvod: 2 bajty jako zkratka (prvni dva znaky) — cely retezec by se sem nevesel
     * a pro odliseni "stall"/"stack"/"test"/"mall" to staci. */
    b[12] = (uint8_t)(reason && reason[0] ? reason[0] : '?');
    b[13] = (uint8_t)(reason && reason[0] && reason[1] ? reason[1] : ' ');
    put16(b + 14, fr_crc16(b, 14));
}

static bool hdr_unpack(const uint8_t *b, uint32_t *seq, uint16_t *n, uint32_t *up, char *r2)
{
    if (get32(b + 0) != FR_MAGIC) return false;
    if (fr_crc16(b, 14) != get16(b + 14)) return false;
    if (seq) *seq = get32(b + 4);
    if (n)   *n   = get16(b + 8);
    if (up)  *up  = get16(b + 10);
    if (r2)  { r2[0] = (char)b[12]; r2[1] = (char)b[13]; r2[2] = '\0'; }
    return true;
}

void flightrec_init(void)
{
    s_ready = 0; s_have_dump = 0; s_head = 0; s_count = 0; s_dumped = 0;
    if (osMutexAcquire(qspiMutexHandle, 500u) != osOK) return;

    /* Najdi sektor s nejvyssim seq (= posledni dump) a prvni VOLNY (smazany). */
    uint32_t best_seq = 0; int have_best = 0; int free_idx = -1;
    for (uint32_t i = 0; i < W25Q_FLIGHTREC_SECTORS; i++) {
        uint8_t h[FR_REC_SIZE];
        uint32_t off = W25Q_FLIGHTREC_BASE + i * W25Q_SECTOR_SIZE;
        if (!w25q_read(off, h, sizeof h)) continue;
        uint32_t seq;
        if (hdr_unpack(h, &seq, NULL, NULL, NULL)) {
            if (!have_best || seq > best_seq) { have_best = 1; best_seq = seq; s_read_off = off; }
        } else if (free_idx < 0 && get32(h) == 0xFFFFFFFFu) {
            free_idx = (int)i;                      /* smazany -> pouzitelny hned */
        }
    }
    s_have_dump = have_best ? 1u : 0u;

    /* Cilovy sektor pro PRISTI dump. Kdyz zadny smazany neni, smaz nejstarsi
     * (nejnizsi index za poslednim) — deje se to jen jednou za 64 dumpu. */
    if (free_idx >= 0) {
        s_write_off = W25Q_FLIGHTREC_BASE + (uint32_t)free_idx * W25Q_SECTOR_SIZE;
        s_ready = 1;
    } else {
        s_write_off = W25Q_FLIGHTREC_BASE;
        if (w25q_erase_sector(s_write_off)) s_ready = 1;
    }
    osMutexRelease(qspiMutexHandle);
}

void flightrec_tick(void)
{
    uint32_t now = HAL_GetTick();
    if ((int32_t)(now - s_next_ms) < 0) return;
    s_next_ms = now + 1000u;

    fr_rec_t r;
    memset(&r, 0, sizeof r);
    r.uptime_s      = (uint16_t)g_uptime_s;
    r.cpu_pct       = (uint8_t)(g_rtos_cpu_pct > 255u ? 255u : g_rtos_cpu_pct);
    r.heap_free_256 = (uint16_t)(g_rtos_heap_free / 256u);

    /* Nejmensi volny stack ze vsech tasku — presne to, co u #18 zajima.
     * `osThreadGetStackSpace` je drahe (scan zasobniku), ale 1x/s pres 5 tasku
     * je zanedbatelne a bezi to v defaultTask. */
    uint32_t smin = 0xFFFFFFFFu;
    osThreadId_t th[8];
    uint32_t nt = osThreadEnumerate(th, 8u);
    for (uint32_t i = 0; i < nt; i++) {
        uint32_t sp = osThreadGetStackSpace(th[i]);
        if (sp && sp < smin) smin = sp;
    }
    r.stack_min_8 = (uint16_t)((smin == 0xFFFFFFFFu) ? 0u : (smin / 8u));

    r.t_ocxo_c10  = (int16_t)(g_sensors[SENS_T49].last * 10.0f);
    r.t_board_c10 = (int16_t)(g_sensors[SENS_T48].last * 10.0f);
    uint32_t ierr = 0;
    for (int i = 0; i < SENS_COUNT; i++)
        if (i != (int)SENS_T4A) ierr += g_sensors[i].err_streak;   /* 0x4A neosazen */
    r.i2c_err = (uint16_t)(ierr > 0xFFFFu ? 0xFFFFu : ierr);

    if (g_spi_ok)      r.flags |= FR_F_FPGA_LINK;
    if (g_freq_stale)  r.flags |= FR_F_FREQ_STALE;
    if (g_cm4_alive)   r.flags |= FR_F_CM4_ALIVE;

    s_ring[s_head] = r;
    s_head = (uint16_t)((s_head + 1u) % FR_DEPTH);
    if (s_count < FR_DEPTH) s_count++;
}

void flightrec_dump(const char *reason)
{
    if (!s_ready || s_dumped) return;   /* jen jednou za beh — sektor je jeden */
    s_dumped = 1;

    /* ⚠️ Kratky timeout: dumpuje se pri poruse a do IWDG resetu zbyva ~1,5 s.
     * Kdyz je QSPI zrovna obsazena, radeji nic nez zaseknout se tesne pred resetem. */
    if (osMutexAcquire(qspiMutexHandle, FR_LOCK_MS) != osOK) return;

    uint8_t buf[FR_REC_SIZE];
    hdr_pack(buf, (uint32_t)g_uptime_s + 1u, s_count, g_uptime_s, reason);
    if (w25q_write(s_write_off, buf, FR_REC_SIZE)) {
        /* Vzorky od NEJSTARSIHO po nejnovejsi. */
        uint16_t start = (uint16_t)((s_head + FR_DEPTH - s_count) % FR_DEPTH);
        for (uint16_t i = 0; i < s_count; i++) {
            rec_pack(buf, &s_ring[(start + i) % FR_DEPTH]);
            if (!w25q_write(s_write_off + FR_REC_SIZE + (uint32_t)i * FR_REC_SIZE,
                            buf, FR_REC_SIZE)) break;
        }
        s_have_dump = 1;
    }
    osMutexRelease(qspiMutexHandle);
}

bool flightrec_have(void) { return s_have_dump ? true : false; }

bool flightrec_report(void)
{
    if (!s_have_dump) return false;
    uint8_t b[FR_REC_SIZE];
    uint32_t seq, up; uint16_t n; char r2[4];

    if (osMutexAcquire(qspiMutexHandle, 500u) != osOK) return false;
    bool ok = w25q_read(s_read_off, b, sizeof b) && hdr_unpack(b, &seq, &n, &up, r2);
    osMutexRelease(qspiMutexHandle);
    if (!ok) return false;

    if (n > FR_DEPTH) n = FR_DEPTH;
    printf("FLIGHT RECORDER: %u vzorku, dump v uptime %lus, duvod '%s'\n",
           (unsigned)n, (unsigned long)up, r2);
    printf("  t[s]  CPU%%  heap    stack_min  OCXO  deska  I2Cerr  flags\n");
    for (uint16_t i = 0; i < n; i++) {
        if (osMutexAcquire(qspiMutexHandle, 500u) != osOK) break;
        ok = w25q_read(s_read_off + FR_REC_SIZE + (uint32_t)i * FR_REC_SIZE, b, sizeof b);
        osMutexRelease(qspiMutexHandle);
        if (!ok) break;
        fr_rec_t r; rec_unpack(b, &r);
        printf("  %5u %4u  %6lu  %7lu   %3d.%u %3d.%u  %5u   %c%c%c\n",
               (unsigned)r.uptime_s, (unsigned)r.cpu_pct,
               (unsigned long)r.heap_free_256 * 256u,
               (unsigned long)r.stack_min_8 * 8u,
               r.t_ocxo_c10 / 10, (unsigned)(abs(r.t_ocxo_c10) % 10),
               r.t_board_c10 / 10, (unsigned)(abs(r.t_board_c10) % 10),
               (unsigned)r.i2c_err,
               (r.flags & FR_F_FPGA_LINK)  ? 'F' : '-',
               (r.flags & FR_F_FREQ_STALE) ? 'S' : '-',
               (r.flags & FR_F_CM4_ALIVE)  ? '4' : '-');
        osDelay(2);   /* nezahlt konzoli (stejny vzor jako `sensors`) */
    }
    return true;
}
