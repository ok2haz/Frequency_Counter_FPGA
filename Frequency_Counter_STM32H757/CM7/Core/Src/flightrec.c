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

/* Handly nasich tasku — stejny seznam jako v UART `status`, aby se ty dva udaje
 * o volnem stacku nemohly rozejit (viz vypocet `stack_min` nize). */
extern osThreadId_t defaultTaskHandle, UartTaskHandle, I2C4TaskHandle,
                    UiTaskHandle, FpgaTaskHandle;

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
static uint8_t   s_have_read;     /* s_read_off ukazuje na platny dump */
static uint32_t  s_seq_next;      /* seq pro PRISTI dump (nejvyssi nalezeny + 1) */
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
/* Duvod dumpu jako kod — plny nazev se do hlavicky nevejde, ale zkratka na dva
 * znaky nerozlisi "stall" od "stack" (viz hdr_pack). */
#define FR_REASON_TAG   0xA5u   /* b[13]: znacka, ze b[12] je KOD (ne pismeno) */
#define FR_REASON_MAX   48u     /* buffer volajiciho pro rozepsany duvod */
enum { FR_R_UNKNOWN = 0, FR_R_TEST, FR_R_STACK, FR_R_MALLOC, FR_R_STALL };

static uint8_t fr_reason_code(const char *reason)
{
    if (reason == NULL) return FR_R_UNKNOWN;
    if (strcmp(reason, "test")  == 0) return FR_R_TEST;
    if (strcmp(reason, "stack") == 0) return FR_R_STACK;
    if (strcmp(reason, "mall")  == 0) return FR_R_MALLOC;
    if (strcmp(reason, "stall") == 0) return FR_R_STALL;
    return FR_R_UNKNOWN;
}

static const char *fr_reason_name(uint8_t code)
{
    switch (code) {
    case FR_R_TEST:   return "test (rucni `flightrec test`, NE porucha)";
    case FR_R_STACK:  return "stack (pretekl zasobnik tasku)";
    case FR_R_MALLOC: return "malloc (vycerpany heap)";
    case FR_R_STALL:  return "stall (task prestal krmit watchdog)";
    default:          return "neznamy";
    }
}

static void hdr_pack(uint8_t *b, uint32_t seq, uint16_t n, uint32_t up, const char *reason)
{
    memset(b, 0, FR_REC_SIZE);
    put32(b + 0, FR_MAGIC);
    put32(b + 4, seq);
    put16(b + 8, n);
    /* uptime v okamziku dumpu (sekundy, orez na 16 bit) */
    put16(b + 10, (uint16_t)up);
    /* ⚠️ Duvod se uklada jako KOD, ne jako prvni dva znaky. Puvodni zkratka byla
     * k nicemu presne tam, kde na ni zalezi: "stall" i "stack" davaly shodne "st",
     * takze z dumpu neslo poznat, jestli slo o zaseknuty task nebo pretekly stack —
     * tedy prave ty dve pricinny, ktere se u #18 hledaji. (Komentar tu drive tvrdil,
     * ze to na odliseni staci; nestacilo.)
     * b[13] nese znacku noveho formatu, aby sel STARY dump precist dal (viz hdr_unpack). */
    b[12] = (uint8_t)fr_reason_code(reason);
    b[13] = FR_REASON_TAG;
    put16(b + 14, fr_crc16(b, 14));
}

static bool hdr_unpack(const uint8_t *b, uint32_t *seq, uint16_t *n, uint32_t *up, char *r2)
{
    if (get32(b + 0) != FR_MAGIC) return false;
    if (fr_crc16(b, 14) != get16(b + 14)) return false;
    if (seq) *seq = get32(b + 4);
    if (n)   *n   = get16(b + 8);
    if (up)  *up  = get16(b + 10);
    /* Duvod: novy format ma v b[13] znacku a v b[12] KOD; stary tam mel prvni dva
     * znaky retezce. Stare dumpy tak zustanou citelne (jen dvouznakove). */
    if (r2) {
        if (b[13] == FR_REASON_TAG) {
            const char *nm = fr_reason_name(b[12]);
            size_t k = 0; while (nm[k] && k < FR_REASON_MAX - 1u) { r2[k] = nm[k]; k++; }
            r2[k] = '\0';
        } else {
            r2[0] = (char)b[12]; r2[1] = (char)b[13]; r2[2] = '\0';   /* stary format */
        }
    }
    return true;
}

/* Vyliti dumpu, ktery predchozi beh zanechal v SDRAM (audit F-0018). Definice je
 * u `flightrec_dump`, kde je i cele zduvodneni dvoufazoveho zapisu. */
static void fr_flush_pending(void);

void flightrec_init(void)
{
    s_ready = 0; s_have_dump = 0; s_head = 0; s_count = 0; s_dumped = 0;
    s_have_read = 0; s_seq_next = 1u;
    if (osMutexAcquire(qspiMutexHandle, 500u) != osOK) return;

    /* Najdi sektor s nejvyssim seq (= posledni dump) a prvni VOLNY (smazany). */
    uint32_t best_seq = 0; int have_best = 0; int free_idx = -1;
    int best_i = -1;            /* index sektoru s nejvyssim seq — potreba pro F-0091 */
    for (uint32_t i = 0; i < W25Q_FLIGHTREC_SECTORS; i++) {
        uint8_t h[FR_REC_SIZE];
        uint32_t off = W25Q_FLIGHTREC_BASE + i * W25Q_SECTOR_SIZE;
        if (!w25q_read(off, h, sizeof h)) continue;
        uint32_t seq;
        if (hdr_unpack(h, &seq, NULL, NULL, NULL)) {
            if (!have_best || seq > best_seq) {
                have_best = 1; best_seq = seq; s_read_off = off; best_i = (int)i;
            }
        } else if (free_idx < 0 && get32(h) == 0xFFFFFFFFu) {
            free_idx = (int)i;                      /* smazany -> pouzitelny hned */
        }
    }
    /* ⚠️ seq NESMI byt odvozena od uptime — to se po resetu vraci k nule, takze by
     * novejsi dump mohl mit nizsi seq nez starsi a `init` by pak vyhrabal ten stary.
     * Proto navazujeme na nejvyssi nalezenou hodnotu. */
    s_have_dump = have_best ? 1u : 0u;
    s_have_read = s_have_dump;
    if (have_best) s_seq_next = best_seq + 1u;

    /* Cilovy sektor pro PRISTI dump. Kdyz zadny smazany neni, smaz NEJSTARSI —
     * a ten lezi na indexu ZA nejnovejsim, protoze se pise po rade.
     * 🔴 Do 2026-09-19 se v teto vetvi mazal natvrdo sektor 0 (audit F-0091), a to
     * bez ohledu na to, kde nejstarsi a nejnovejsi dump lezi. Komentar spravne
     * pravidlo dokonce v zavorce POPISOVAL („index za poslednim"), ale kod ho
     * neprovadel. Dusledek: boot 65 smazal sektor 0 spravne (byl nejstarsi), ale
     * od bootu 66 uz sektor 0 drzel NEJNOVEJSI dump — a init ho pokazde zahodil.
     * Od te chvile se pouzival vyhradne sektor 0 a kazdy dump se smazal pri
     * nasledujicim startu. Navenek to pusobilo jako rozpor dvou vypisu: `status`
     * hlasil „je ulozeny zaznam", `flightrec` rekl „zadny ulozeny zaznam" — presne
     * v okamziku, kdy se hleda pricina poruchy.
     * ✅ Sesterska `errlog_init()` v tomto souboru to uz delala spravne
     * (`ni = (best_i + 1) % SECTORS`) — tohle je tedy jen dorovnani dvojcete
     * (lekce L-0012). */
    if (free_idx >= 0) {
        s_write_off = W25Q_FLIGHTREC_BASE + (uint32_t)free_idx * W25Q_SECTOR_SIZE;
        s_ready = 1;
    } else {
        /* `best_i < 0` = ani jeden platny zaznam a pritom zadny smazany sektor
         * (same smeti) -> zacni od nuly, jinak sektor za nejnovejsim. */
        uint32_t ni = (best_i >= 0)
                    ? (((uint32_t)best_i + 1u) % W25Q_FLIGHTREC_SECTORS) : 0u;
        s_write_off = W25Q_FLIGHTREC_BASE + ni * W25Q_SECTOR_SIZE;
        if (w25q_erase_sector(s_write_off)) s_ready = 1;
    }
    /* Fáze 2 dvoufazoveho zapisu (audit F-0018): kdyz predchozi beh skoncil
     * pretečenim zasobniku, dump ceka v SDRAM — tady uz mutex drzime a flash je
     * pripravena, takze se vylije. Az TEDDY ma letovy zapisovac pro ten scenar smysl. */
    if (s_ready) fr_flush_pending();
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

    /* Nejmensi volny stack — presne to, co u #18 zajima.
     * ⚠️ VYSLOVNE JEN NASICH 5 TASKU, ne `osThreadEnumerate`. Ten vraci i vnitrni
     * vlakna FreeRTOS (IDLE, Tmr Svc), jejichz rezerva je mala a NEMENNA — minimum
     * pak vzdy hlasilo jejich konstantu (na HW 416 B pres boot i 200 s behu),
     * zatimco UART `status` nad nasimi tasky hlasil 736 B. Metrika tim byla
     * MASKOVANA: kdyby defaultTask klesl ze 736 B na 100 B (presne scenar #18),
     * recorder by dal ukazoval 416 a nikdo by si niceho nevsiml.
     * Seznam je zamerne tentyz jako v `status`, aby se ty dva udaje uz nerozesly.
     * `osThreadGetStackSpace` je drahe (scan zasobniku), ale 1x/s pres 5 tasku
     * je zanedbatelne a bezi to v defaultTask. */
    static const struct { const char *n; osThreadId_t *h; } TL[] = {
        {"default", &defaultTaskHandle}, {"Uart", &UartTaskHandle},
        {"I2C4",    &I2C4TaskHandle},    {"Ui",   &UiTaskHandle},
        {"Fpga",    &FpgaTaskHandle},
    };
    uint32_t smin = 0xFFFFFFFFu;
    uint16_t swho = 0xFFFFu;                      /* index nejtesnejsiho tasku */
    for (unsigned i = 0; i < sizeof(TL) / sizeof(TL[0]); i++) {
        if (*TL[i].h == NULL) continue;
        uint32_t sp = osThreadGetStackSpace(*TL[i].h);
        if (sp && sp < smin) { smin = sp; swho = (uint16_t)i; }
    }
    r.stack_min_8 = (uint16_t)((smin == 0xFFFFFFFFu) ? 0u : (smin / 8u));
    /* Do ted nevyuzity `spare`: KTERY task byl nejtesnejsi. Bez toho rekne dump
     * jen "nekomu doslo misto", ale ne komu — a to je u #18 ta podstatna cast. */
    r.spare = swho;

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

/* ── PREDANI DUMPU Z KONTEXTU VYJIMKY (audit F-0018) ─────────────────────────
 * 🔴 PROC: `flightrec_dump()` se vola i z `vApplicationStackOverflowHook`, ktery bezi
 * v kontextu vyjimky PendSV (`xPortPendSVHandler` -> `vTaskSwitchContext` ->
 * `taskCHECK_FOR_STACK_OVERFLOW`). Tam `osMutexAcquire` VZDY vrati `osErrorISR`,
 * takze se dump pro pretečeni zasobniku NIKDY neprovedl — a to je zrovna scenar,
 * kvuli kteremu letovy zapisovac vznikl (STATUS #18: „co se delo pred pretečenim").
 * Zapisovat z hooku bez mutexu NEJDE: `w25q wait_ready()` uvnitr vola `osDelay(1)`,
 * ktery v PendSV take neprojde, takze by z toho byl spin az do IWDG resetu.
 *
 * RESENI = dvoufazovy zapis, tedy tentyz vzor, jaky uz ma `errlog`:
 *   1. hook slozi dump do SDRAM (jen bajtove zapisy, zadny mutex, zadny osDelay),
 *   2. po restartu ho `flightrec_init()` vylije do flash — tam uz mutex drzime.
 *
 * ⚠️ PROC SDRAM a ne RAM: `.bss` maze `Reset_Handler` pri kazdem startu, takze by se
 * staging pri resetu ztratil. Sekce `.sdram` je NOLOAD, startup na ni nesaha a obsah
 * SDRAM prezije reset (tentyz duvod, proc boot musi framebuffer memsetovat na cerno).
 * ⚠️ `.sdram` je DEVICE pamet -> nezarovnany 32bitovy pristup je UsageFault bez ohledu
 * na `CCR.UNALIGN_TRP` (past F-0012). Bezpecne to je proto, ze `hdr_pack`/`rec_pack`
 * plni buffer VYHRADNE pres `put16`/`put32`, a ty zapisuji PO BAJTECH.
 * ⚠️ Po POWER-CYKLU je SDRAM nahodna -> platnost se overuje magicem (shoda naslepo
 * 1 : 4 miliardam) a delkou v rozsahu. Pro pretečeni zasobniku to staci: po nem
 * nasleduje IWDG reset, ne odpojeni napajeni. */
#define FR_PEND_MAGIC  0x46525031u   /* "FRP1" */

static struct {
    uint32_t magic;
    uint32_t bytes;                                   /* kolik `data` je platnych */
    uint8_t  data[(FR_DEPTH + 1u) * FR_REC_SIZE];     /* hlavicka + vzorky, layout FLASH */
} s_pend __attribute__((section(".sdram"), aligned(32)));

/* Slozi dump do SDRAM. Bezpecne z kontextu vyjimky: jen bajtove zapisy. */
static void fr_stage_pending(const char *reason)
{
    s_pend.magic = 0u;               /* naplo az na konci, at se necte rozepsany */
    hdr_pack(s_pend.data, s_seq_next, s_count, g_uptime_s, reason);
    uint16_t start = (uint16_t)((s_head + FR_DEPTH - s_count) % FR_DEPTH);
    for (uint16_t i = 0; i < s_count; i++)
        rec_pack(s_pend.data + (uint32_t)(i + 1u) * FR_REC_SIZE,
                 &s_ring[(start + i) % FR_DEPTH]);
    s_pend.bytes = (uint32_t)(s_count + 1u) * FR_REC_SIZE;
    __DMB();
    s_pend.magic = FR_PEND_MAGIC;
}

/* Vylije staged dump do flash. Ocekava DRZENY mutex (vola se z `flightrec_init`). */
static void fr_flush_pending(void)
{
    if (s_pend.magic != FR_PEND_MAGIC) return;
    if (s_pend.bytes < FR_REC_SIZE || s_pend.bytes > sizeof s_pend.data) {
        s_pend.magic = 0u;           /* nesmyslna delka -> zahodit, ne zapsat smeti */
        return;
    }
    for (uint32_t off = 0; off < s_pend.bytes; off += FR_REC_SIZE)
        if (!w25q_write(s_write_off + off, s_pend.data + off, FR_REC_SIZE)) return;
    s_read_off  = s_write_off;
    s_have_read = 1;
    s_have_dump = 1;
    s_seq_next++;
    s_pend.magic = 0u;               /* ulozeno */
    g_flightrec_staged++;            /* aby bylo videt, ze to slo touhle cestou */
}

void flightrec_dump(const char *reason)
{
    if (!s_ready || s_dumped) return;   /* jen jednou za beh — sektor je jeden */
    s_dumped = 1;

    /* ⚠️ Kratky timeout: dumpuje se pri poruse a do IWDG resetu zbyva ~1,5 s.
     * Kdyz je QSPI zrovna obsazena, radeji nic nez zaseknout se tesne pred resetem. */
    /* 🔴 NEUSPECH UZ NENI TICHY (audit F-0018, 2026-09-10). Z hooku pretečeni
     * stacku bezi tahle funkce v kontextu vyjimky PendSV (`xPortPendSVHandler`
     * -> `vTaskSwitchContext` -> `taskCHECK_FOR_STACK_OVERFLOW`), kde
     * `osMutexAcquire` VZDY vrati `osErrorISR` — dump se tedy pro pretečeni
     * stacku NIKDY neprovede. Nez se to prestavi na dvoufazovy zapis (jako ma
     * `errlog`: RAM ring + vyliti z ulohy), at je aspon VIDET, ze se zaznam
     * ztratil. Crash black-box v BKP funguje dal — zapisuje se driv. */
    /* Fáze 1: VZDY nejdriv do SDRAM. Kdyz se flash nepovede (nebo tu vubec nesmime
     * na mutex sahnout), zustane dump staged a vylije se po restartu. */
    fr_stage_pending(reason);
    /* Kontext vyjimky (hook pretečeni zasobniku / malloc): na mutex se ani nesaha,
     * `osMutexAcquire` by vratil `osErrorISR` a `w25q wait_ready` by spinoval
     * do IWDG. Staged dump uz je v SDRAM — vylije ho `flightrec_init` po restartu. */
    if (__get_IPSR() != 0u) return;
    if (osMutexAcquire(qspiMutexHandle, FR_LOCK_MS) != osOK) { g_flightrec_lost++; return; }

    uint8_t buf[FR_REC_SIZE];
    hdr_pack(buf, s_seq_next, s_count, g_uptime_s, reason);
    if (w25q_write(s_write_off, buf, FR_REC_SIZE)) {
        /* Vzorky od NEJSTARSIHO po nejnovejsi. */
        uint16_t start = (uint16_t)((s_head + FR_DEPTH - s_count) % FR_DEPTH);
        for (uint16_t i = 0; i < s_count; i++) {
            rec_pack(buf, &s_ring[(start + i) % FR_DEPTH]);
            if (!w25q_write(s_write_off + FR_REC_SIZE + (uint32_t)i * FR_REC_SIZE,
                            buf, FR_REC_SIZE)) break;
        }
        /* ⚠️ Bez tehle dvojice cetl `flightrec_report` porad z `s_read_off`, ktery
         * plni jen `init` z JIZ existujiciho dumpu — cerstve zapsany dump se tedy
         * neprecetl (pri prvnim behu dokonce z offsetu 0). */
        s_read_off  = s_write_off;
        s_have_read = 1;
        s_have_dump = 1;
        s_seq_next++;
        s_pend.magic = 0u;      /* zapsano hned, staging uz neni potreba */
    }
    osMutexRelease(qspiMutexHandle);
}

bool flightrec_have(void) { return s_have_dump ? true : false; }

bool flightrec_report(void)
{
    if (!s_have_dump || !s_have_read) return false;
    uint8_t b[FR_REC_SIZE];
    uint32_t seq, up; uint16_t n; char r2[FR_REASON_MAX];

    if (osMutexAcquire(qspiMutexHandle, 500u) != osOK) return false;
    bool ok = w25q_read(s_read_off, b, sizeof b) && hdr_unpack(b, &seq, &n, &up, r2);
    osMutexRelease(qspiMutexHandle);
    if (!ok) return false;

    if (n > FR_DEPTH) n = FR_DEPTH;
    printf("FLIGHT RECORDER: %u vzorku, dump v uptime %lus, duvod '%s'\n",
           (unsigned)n, (unsigned long)up, r2);
    printf("  t[s]  CPU%%  heap    stack_min kdo      OCXO  deska  I2Cerr  flags\n");
    for (uint16_t i = 0; i < n; i++) {
        if (osMutexAcquire(qspiMutexHandle, 500u) != osOK) break;
        ok = w25q_read(s_read_off + FR_REC_SIZE + (uint32_t)i * FR_REC_SIZE, b, sizeof b);
        osMutexRelease(qspiMutexHandle);
        if (!ok) break;
        /* Hlavicka se zapisuje PRVNI (at pri poruse prezije aspon duvod), takze
         * pri prerusenem dumpu muze byt zbytek jeste smazany — takove vzorky
         * neni co tisknout. */
        if (get32(b) == 0xFFFFFFFFu && get32(b + 4) == 0xFFFFFFFFu) break;
        fr_rec_t r; rec_unpack(b, &r);
        /* `spare` = index nejtesnejsiho tasku (0xFFFF u starsich dumpu, kde se
         * jeste neukladal — tam se vypise "?"). */
        static const char *TN[] = { "default", "Uart", "I2C4", "Ui", "Fpga" };
        const char *who = (r.spare < (sizeof TN / sizeof TN[0])) ? TN[r.spare] : "?";
        printf("  %5u %4u  %6lu  %7lu %-7s  %3d.%u %3d.%u  %5u   %c%c%c\n",
               (unsigned)r.uptime_s, (unsigned)r.cpu_pct,
               (unsigned long)r.heap_free_256 * 256u,
               (unsigned long)r.stack_min_8 * 8u, who,
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

/* ══════════════════════════════════════════════════════════════════════════
 * ERRLOG — trvaly zaznamnik chyb (rozhrani v `errlog.h`)
 *
 * ⚠️ PROC ZROVNA TADY a ne ve vlastnim `errlog.c`: novy `.c` se do buildu
 * NEDOSTANE bez `Close Project -> Open Project` v IDE (prelozi se, ale linker
 * hlasi `undefined reference`, protoze chybi v `Release` -> `subdir.mk`).
 * Hlavicky tenhle problem nemaji, takze API zustava v `errlog.h`.
 * Domenove to sem patri — `flightrec` uz resi tytez veci: zapis do W25Q pri
 * porse, predem smazany sektor a zakaz zapisu z exception kontextu.
 * ══════════════════════════════════════════════════════════════════════════ */

#include "errlog.h"
#include "datalog.h"      /* datalog_crc16, datalog_now_unix */

#define ERRLOG_RING_N       16u
#define ERRLOG_COOLDOWN_MS  60000u     /* max 1 zaznam na druh a minutu */
#define ERRLOG_PER_SECTOR   (W25Q_SECTOR_SIZE / ERRLOG_REC_SIZE)   /* 128 */
#define ERRLOG_CAPACITY     (W25Q_ERRLOG_SECTORS * ERRLOG_PER_SECTOR)
#define ERRLOG_KIND_MAX     ((uint8_t)ERRLOG_K_CFG)

static errlog_rec_t      s_el_ring[ERRLOG_RING_N];
static volatile uint32_t s_el_head_i, s_el_tail_i;      /* index do RAM ringu */
static volatile uint32_t s_el_dropped;                  /* ring byl plny */
static uint32_t          s_el_cool_next[ERRLOG_KIND_MAX + 1u];
static uint16_t          s_el_pending[ERRLOG_KIND_MAX + 1u];

static uint32_t s_el_write_off;    /* kam padne PRISTI zaznam */
static uint32_t s_el_seq_next = 1u;
static uint8_t  s_el_ready;

static void el_put32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
}
static uint32_t el_get32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

/* ⚠️ Rucni serializace (ne `memcpy` struktury) — format zaznamu ve flash pak
 * nezavisi na zarovnani a poradi bajtu prekladace. Stejne jako `datalog`. */
static void el_pack(const errlog_rec_t *r, uint8_t *b)
{
    memset(b, 0, ERRLOG_REC_SIZE);
    el_put32(b + 0,  r->seq);
    el_put32(b + 4,  r->t_unix);
    el_put32(b + 8,  r->uptime_s);
    el_put32(b + 12, r->a);
    el_put32(b + 16, r->b);
    b[20] = (uint8_t)r->repeat; b[21] = (uint8_t)(r->repeat >> 8);
    b[22] = r->kind;
    b[23] = r->sub;
    memcpy(b + 24, r->tag, ERRLOG_TAG_LEN);
    uint16_t c = datalog_crc16(b, 30);
    b[30] = (uint8_t)c; b[31] = (uint8_t)(c >> 8);
}

static bool el_unpack(const uint8_t *b, errlog_rec_t *r)
{
    uint32_t seq = el_get32(b);
    if (seq == 0xFFFFFFFFu) return false;                   /* volny (smazany) slot */
    uint16_t want = (uint16_t)b[30] | (uint16_t)((uint16_t)b[31] << 8);
    if (datalog_crc16(b, 30) != want) return false;         /* poskozeny zaznam */
    r->seq      = seq;
    r->t_unix   = el_get32(b + 4);
    r->uptime_s = el_get32(b + 8);
    r->a        = el_get32(b + 12);
    r->b        = el_get32(b + 16);
    r->repeat   = (uint16_t)b[20] | (uint16_t)((uint16_t)b[21] << 8);
    r->kind     = b[22];
    r->sub      = b[23];
    memcpy(r->tag, b + 24, ERRLOG_TAG_LEN);
    return true;
}

const char *errlog_kind_name(uint8_t kind)
{
    switch (kind) {
    case ERRLOG_K_BOOT:    return "BOOT";
    case ERRLOG_K_CRASH:   return "CRASH";
    case ERRLOG_K_I2C:     return "I2C";
    case ERRLOG_K_UART:    return "UART";
    case ERRLOG_K_SENSOR:  return "SENZOR";
    case ERRLOG_K_FPGA:    return "FPGA";
    case ERRLOG_K_REF:     return "REF";
    case ERRLOG_K_STORAGE: return "ULOZ";
    case ERRLOG_K_GPIO:    return "GPIO";
    case ERRLOG_K_NET:     return "SIT";
    case ERRLOG_K_CFG:     return "NASTAV";
    default:               return "?";
    }
}

void errlog_init(void)
{
    s_el_ready = 0; s_el_head_i = 0; s_el_tail_i = 0;
    s_el_seq_next = 1u; s_el_write_off = W25Q_ERRLOG_BASE;
    if (osMutexAcquire(qspiMutexHandle, 500u) != osOK) return;

    /* Nejnovejsi sektor pozna nejvyssi `seq` v jeho PRVNIM zaznamu; v nem se pak
     * najde prvni volny slot. ⚠️ `seq` NESMI byt odvozena od uptime — to se po
     * resetu vraci k nule (tatáz past, jakou ma v komentari `flightrec_init`). */
    uint32_t best = 0; int best_i = -1;
    for (uint32_t i = 0; i < W25Q_ERRLOG_SECTORS; i++) {
        uint8_t h[ERRLOG_REC_SIZE];
        errlog_rec_t r;
        if (!w25q_read(W25Q_ERRLOG_BASE + i * W25Q_SECTOR_SIZE, h, sizeof h)) continue;
        if (el_unpack(h, &r) && (best_i < 0 || r.seq > best)) { best = r.seq; best_i = (int)i; }
    }

    if (best_i < 0) {
        /* Prazdny (nebo nikdy nepouzity) log — zacni na zacatku regionu. */
        s_el_write_off = W25Q_ERRLOG_BASE;
        if (w25q_erase_sector(s_el_write_off)) s_el_ready = 1;
    } else {
        uint32_t base = W25Q_ERRLOG_BASE + (uint32_t)best_i * W25Q_SECTOR_SIZE;
        uint32_t slot = 0, maxseq = 0;
        for (; slot < ERRLOG_PER_SECTOR; slot++) {
            uint8_t h[ERRLOG_REC_SIZE];
            errlog_rec_t r;
            if (!w25q_read(base + slot * ERRLOG_REC_SIZE, h, sizeof h)) break;
            if (!el_unpack(h, &r)) break;               /* prvni volny/vadny = hlava */
            if (r.seq > maxseq) maxseq = r.seq;
        }
        s_el_seq_next = maxseq + 1u;
        if (slot >= ERRLOG_PER_SECTOR) {
            /* Sektor plny -> dalsi (s pretocenim) a predem ho smaz. */
            uint32_t ni = ((uint32_t)best_i + 1u) % W25Q_ERRLOG_SECTORS;
            s_el_write_off = W25Q_ERRLOG_BASE + ni * W25Q_SECTOR_SIZE;
            if (w25q_erase_sector(s_el_write_off)) s_el_ready = 1;
        } else {
            s_el_write_off = base + slot * ERRLOG_REC_SIZE;
            s_el_ready = 1;
        }
    }
    osMutexRelease(qspiMutexHandle);
}

bool errlog_put(uint8_t kind, uint8_t sub, uint32_t a, uint32_t b, const char *tag)
{
    if (kind == 0u || kind > ERRLOG_KIND_MAX) return false;

    uint32_t now = HAL_GetTick();
    uint32_t pm = __get_PRIMASK();
    __disable_irq();                       /* kratka sekce — smi bezet i z ISR */

    bool emit = false;
    if ((int32_t)(now - s_el_cool_next[kind]) >= 0) {
        s_el_cool_next[kind] = now + ERRLOG_COOLDOWN_MS;
        uint32_t nxt = (s_el_head_i + 1u) % ERRLOG_RING_N;
        if (nxt == s_el_tail_i) {
            s_el_dropped++;                /* ring plny — `tick` nestiha */
        } else {
            errlog_rec_t *r = &s_el_ring[s_el_head_i];
            r->seq = 0u;                   /* doplni `tick` az pri zapisu */
            r->t_unix = 0u;                /* dtto — parsovani casu nepatri do ISR */
            r->uptime_s = g_uptime_s;
            r->a = a; r->b = b;
            r->repeat = s_el_pending[kind];
            r->kind = kind; r->sub = sub;
            memset(r->tag, 0, ERRLOG_TAG_LEN);
            for (uint32_t i = 0; tag && tag[i] && i < ERRLOG_TAG_LEN; i++) r->tag[i] = tag[i];
            s_el_pending[kind] = 0u;
            s_el_head_i = nxt;
            emit = true;
        }
    } else if (s_el_pending[kind] < 0xFFFFu) {
        s_el_pending[kind]++;              /* opakovani se secte do PRISTIHO zaznamu */
    }

    __set_PRIMASK(pm);
    return emit;
}

/* F-0098: pripravenost obou zaznamniku. Bez toho se neuspesny init nikde
 * neprojevil — u flightrecu vubec, u errlogu az tim, ze se RAM ring (16 polozek)
 * naplni a zacne pocitat `errlog_dropped()`. */
int flightrec_ready(void) { return s_ready ? 1 : 0; }
int errlog_ready(void)    { return s_el_ready ? 1 : 0; }

/* Kolikrat se `errlog_tick` pokusil init zopakovat (F-0098). */
static uint32_t s_el_retries;
uint32_t errlog_init_retries(void) { return s_el_retries; }

/* Nejvys tolik pokusu a nejmene takhle daleko od sebe. 🔴 Strop je tu ZAMERNE:
 * `errlog_init()` dela sken vsech sektoru a muze skoncit `w25q_erase_sector`,
 * coz je 50-400 ms v defaultTasku (ten krmi watchdog). Nekonecne opakovani mrtve
 * flash by tedy bylo horsi nez sama vada — a kdyz to nevyjde po peti pokusech
 * v prubehu minuty, neni to prechodna kolize o mutex, ale skutecna porucha,
 * kterou uz jen hlasime. */
#define ERRLOG_RETRY_MAX  5u
#define ERRLOG_RETRY_MS   10000u

void errlog_tick(void)
{
    /* 🔴 ZACHRANA NEPOVEDENEHO INITU (audit F-0098). `errlog_init()` pri bootu
     * odchazi na `osMutexAcquire(...) != osOK`, takze jedna nestastna sekunda
     * (obsazena flash) znamenala, ze se do TRVALE historie chyb uz nikdy nic
     * nezapise — bez retry, bez pocitadla, bez radku v `status`. */
    if (!s_el_ready) {
        static uint32_t s_last_try;
        if (s_el_retries >= ERRLOG_RETRY_MAX) return;
        uint32_t now = HAL_GetTick();
        if (s_last_try != 0u && (now - s_last_try) < ERRLOG_RETRY_MS) return;
        s_last_try = now ? now : 1u;
        s_el_retries++;
        errlog_init();   /* nastavi `s_el_ready`, kdyz to vyjde */
        return;          /* vylevani ringu az pristi tik, tenhle uz byl drahy */
    }
    if (s_el_tail_i == s_el_head_i) return;

    /* ⚠️ Kratky timeout: defaultTask krmi watchdog a NESMI cekat na obsazenou
     * flash. Kdyz to nevyjde, zaznamy zustanou v ringu do dalsiho tiku. */
    if (osMutexAcquire(qspiMutexHandle, 10u) != osOK) return;

    uint32_t t_unix = datalog_now_unix();
    while (s_el_tail_i != s_el_head_i) {
        errlog_rec_t r = s_el_ring[s_el_tail_i];
        r.seq = s_el_seq_next;
        if (r.t_unix == 0u) r.t_unix = t_unix;

        /* Zacatek noveho sektoru -> smaz ho (zahodi 128 nejstarsich zaznamu). */
        if ((s_el_write_off % W25Q_SECTOR_SIZE) == 0u) {
            if (!w25q_erase_sector(s_el_write_off)) break;
        }
        uint8_t b[ERRLOG_REC_SIZE];
        el_pack(&r, b);
        if (!w25q_write(s_el_write_off, b, sizeof b)) break;

        s_el_seq_next++;
        s_el_write_off += ERRLOG_REC_SIZE;
        if (s_el_write_off >= W25Q_ERRLOG_BASE + W25Q_ERRLOG_SIZE) s_el_write_off = W25Q_ERRLOG_BASE;
        s_el_tail_i = (s_el_tail_i + 1u) % ERRLOG_RING_N;
    }
    osMutexRelease(qspiMutexHandle);
}

uint32_t errlog_count(void)
{
    uint32_t written = (s_el_seq_next > 1u) ? (s_el_seq_next - 1u) : 0u;
    /* 🔴 Strop NENI cela kapacita (audit F-0101). Sektor, do ktereho se prave
     * zapisuje, byl pri vstupu do nej CELY smazan, takze slotu ZA hlavou je az
     * `ERRLOG_PER_SECTOR - 1` prazdnych — a pri pretoceni kruhu se cteni dostane
     * presne tam. Driv se vracelo plnych 8192, takze `errlog dump 200` u naplneneho
     * logu skoncil driv, nez slibil, a okno CHYBY ukazovalo prazdne radky.
     * Ztrata dat to nebyla, jen nekonzistentni hlaseni — ale je to tatáz trida jako
     * F-0116: cislo, ktere tvrdi vic, nez je k dispozici. */
    uint32_t rel  = (s_el_write_off - W25Q_ERRLOG_BASE) % W25Q_ERRLOG_SIZE;
    uint32_t slot = (rel % W25Q_SECTOR_SIZE) / ERRLOG_REC_SIZE;   /* kolikaty slot v sektoru */
    uint32_t cap  = (uint32_t)ERRLOG_CAPACITY - (ERRLOG_PER_SECTOR - slot);
    return (written < cap) ? written : cap;
}

uint32_t errlog_dropped(void) { return s_el_dropped; }

bool errlog_read_back(uint32_t idx_from_newest, errlog_rec_t *out)
{
    if (!out || idx_from_newest >= errlog_count()) return false;
    uint32_t span = W25Q_ERRLOG_SIZE;
    uint32_t back = ((idx_from_newest + 1u) * ERRLOG_REC_SIZE) % span;
    uint32_t rel  = ((s_el_write_off - W25Q_ERRLOG_BASE) + span - back) % span;

    uint8_t b[ERRLOG_REC_SIZE];
    bool ok = false;
    /* ⚠️ KRATKY timeout (50 ms, ne 200): okno CHYBY vola tuhle funkci 8x za sebou
     * z UiTasku, ktery ma watchdog heartbeat s limitem 2,5 s. Pri 200 ms by
     * osm neuspesnych pokusu delalo 1,6 s cekani — zbytecne blizko limitu.
     * Kdyz je flash obsazena, radek se proste nevykresli; to je u prohlizece
     * prijatelne, zablokovany UiTask ne. */
    if (osMutexAcquire(qspiMutexHandle, 50u) == osOK) {
        ok = w25q_read(W25Q_ERRLOG_BASE + rel, b, sizeof b) && el_unpack(b, out);
        osMutexRelease(qspiMutexHandle);
    }
    return ok;
}

/* Kolik zaznamu se cte JEDNIM QSPI prikazem (16 x 32 B = 512 B). Buffer je
 * `static` zamerne: volajici je `ipc_errlog_service` z defaultTasku (zasobnik
 * 2560 B), takze 512 B na stacku by byla zbytecna ctvrtina — a `errlog_read_batch`
 * uz stejne neni reentrantni (drzi QSPI mutex). */
#define EL_BULK_RECS  16u

uint32_t errlog_read_batch(uint32_t from, uint32_t count, errlog_rec_t *out)
{
    if (!out || count == 0u) return 0u;
    uint32_t total = errlog_count();
    if (from >= total) return 0u;
    if (count > total - from) count = total - from;

    /* 🔴 JEDEN QSPI PRIKAZ NA SKUPINU, ne na zaznam (audit F-0095). Funkce se
     * jmenuje „batch" a drzi jeden mutex, ale uvnitr vydavala `w25q_read` pro
     * KAZDY 32B zaznam. Rezie takoveho cteni je v projektu ZMERENA:
     * `datalog.h` uvadi ~173 us/zaznam proti ~7 us na samotna data, tedy 25x vic
     * rezie nez prenosu. Davka 64 zaznamu tak delala ~11 ms nepreruseneho pollingu
     * v defaultTasku (krmi watchdog) pod drzenym QSPI mutexem. Presne vzor, kvuli
     * kteremu vznikl `datalog_read_bulk()` (F-0039, lekce L-0021) — a znovu se
     * na nej zapomnelo.
     * ⚠️ Zaznamy jdou od hlavy DOZADU, takze index `from+k` roste s klesajici
     * adresou. Skupina se proto cte od adresy NEJSTARSIHO clena a ve vystupu se
     * obraci. Pri prelomu konce regionu se skupina zkrati (zadny wrap uvnitr
     * jednoho cteni). */
    static uint8_t buf[EL_BULK_RECS * ERRLOG_REC_SIZE];
    const uint32_t span = W25Q_ERRLOG_SIZE;
    const uint32_t head = (s_el_write_off - W25Q_ERRLOG_BASE) % span;

    uint32_t got = 0;
    if (osMutexAcquire(qspiMutexHandle, 200u) != osOK) return 0u;
    while (got < count) {
        const uint32_t j = from + got;                 /* nejnovejsi clen skupiny */
        const uint32_t back  = ((j + 1u) * ERRLOG_REC_SIZE) % span;
        const uint32_t rel_j = (head + span - back) % span;   /* adresa zaznamu `j` */
        uint32_t n = count - got;
        if (n > EL_BULK_RECS) n = EL_BULK_RECS;
        /* Skupina lezi POD `rel_j` (starsi = nizsi adresa), takze se musi vejit. */
        const uint32_t maxn = rel_j / ERRLOG_REC_SIZE + 1u;
        if (n > maxn) n = maxn;
        const uint32_t start = rel_j - (n - 1u) * ERRLOG_REC_SIZE;
        if (!w25q_read(W25Q_ERRLOG_BASE + start, buf, n * ERRLOG_REC_SIZE)) break;
        uint32_t k = 0;
        for (; k < n; k++) {
            /* `buf` je vzestupne (od nejstarsiho), vystup chceme od nejnovejsiho. */
            if (!el_unpack(&buf[(n - 1u - k) * ERRLOG_REC_SIZE], &out[got + k])) break;
        }
        got += k;
        if (k < n) break;      /* prvni vadny/prazdny slot = konec citelne historie */
    }
    osMutexRelease(qspiMutexHandle);
    return got;
}

/* ⚠️ Vraci `false`, kdyz se nektery sektor nepodarilo smazat (audit F-0099).
 * Do 2026-09-17 se vysledek vsech 64 volani zahazoval `(void)` a stav se
 * bezpodminecne prohlasil za „smazano". Pri nedokoncenem mazani by se hlava
 * vratila na zacatek regionu, zatimco ve zbytku by zustaly STARE zaznamy
 * s VYSSIM `seq` — a pristi `errlog_init()` by je nasel jako „nejnovejsi"
 * a ustavil hlavu na spatnem miste. Sesterska `datalog_erase_all()`
 * (`datalog.c`) navratove hodnoty kontroluje uz dnes; tohle je L-0012. */
bool errlog_erase(void)
{
    if (osMutexAcquire(qspiMutexHandle, 2000u) != osOK) return false;
    bool ok = true;
    for (uint32_t i = 0; i < W25Q_ERRLOG_SECTORS; i++) {
        if (!w25q_erase_sector(W25Q_ERRLOG_BASE + i * W25Q_SECTOR_SIZE)) { ok = false; break; }
    }
    /* Stav se posouva JEN pri uplnem uspechu — jinak zustava puvodni hlava,
     * kterou uz `errlog_init()` jednou spravne nasel. */
    if (ok) {
        s_el_seq_next  = 1u;
        s_el_write_off = W25Q_ERRLOG_BASE;
        s_el_ready     = 1;
    }
    osMutexRelease(qspiMutexHandle);
    return ok;
}

/* Rozlozi `g_crash_text` na (druh padu, rozlisujici cast za oddelovacem).
 * Formaty vyrabi `rtc.c` z crash black-boxu: "stack:<task>", "stall:<task>",
 * "HF@<pc><typ>", "hal_err@<krok|HSE|LSE>", "assert:L<radek>", "malloc fail"
 * a "crash? <n>" pro neznamy druh.
 * @return `sub` pro zaznam (viz `ERRLOG_CRASH_*`), 0 = nerozpoznano.
 * >> Cisla MUSI odpovidat `kind` v crash black-boxu (`rtc.c`), aby se dve
 * cislovani nerozesla — proto je tabulka jmen hned vedle. */
#define ERRLOG_CRASH_STACK   1u
#define ERRLOG_CRASH_MALLOC  2u
#define ERRLOG_CRASH_STALL   3u
#define ERRLOG_CRASH_HF      4u
#define ERRLOG_CRASH_HAL     5u
#define ERRLOG_CRASH_ASSERT  6u

static const char *const EL_CRASH_NAME[7] = {
    "?", "stack", "malloc", "stall", "HardFault", "hal_err", "assert"
};

static uint8_t crash_split(const char *t, const char **rest)
{
    static const struct { const char *pfx; uint8_t sub; } P[] = {
        { "stack:",   ERRLOG_CRASH_STACK  },
        { "stall:",   ERRLOG_CRASH_STALL  },
        { "assert:",  ERRLOG_CRASH_ASSERT },
        { "hal_err@", ERRLOG_CRASH_HAL    },
        { "HF@",      ERRLOG_CRASH_HF     },
        { "malloc",   ERRLOG_CRASH_MALLOC },
    };
    for (unsigned i = 0; i < sizeof P / sizeof P[0]; i++) {
        size_t l = strlen(P[i].pfx);
        if (strncmp(t, P[i].pfx, l) == 0) {
            /* U "malloc fail" neni co rozlisovat — cely vyznam nese uz `sub`. */
            *rest = (P[i].sub == ERRLOG_CRASH_MALLOC) ? "" : (t + l);
            return P[i].sub;
        }
    }
    *rest = t;            /* nerozpoznano -> at se aspon ulozi, co tam bylo */
    return 0u;
}

void errlog_boot_record(void)
{
    /* Duvod resetu + (kdyz byl) crash z BKP. ⚠️ Musi bezet AZ po `MX_RTC_Init`,
     * ktera black-box dekoduje do `g_crash_text` a v BKP ho SMAZE — jinak by se
     * historie ztratila prave u te chyby, kvuli ktere tenhle log vznikl. */
    char tag[ERRLOG_TAG_LEN + 1];
    uint32_t i = 0;
    for (; i < ERRLOG_TAG_LEN && g_reset_text[i]; i++) tag[i] = (char)g_reset_text[i];
    tag[i] = '\0';
    /* Rate-limit se pri startu obchazi zamerne: boot je vzdy zajimavy. */
    s_el_cool_next[ERRLOG_K_BOOT] = HAL_GetTick();
    (void)errlog_put(ERRLOG_K_BOOT, g_display_init_step, RCC->RSR, 0u, tag);

    if (g_crash_text[0]) {
        char ct[ERRLOG_TAG_LEN + 1];
        uint32_t k = 0;
        /* >> DO 2026-09-19 SE TU KOPIROVALO PRVNICH 6 ZNAKU `g_crash_text`, tedy
         * PRESNE PREFIX S DVOJTECKOU (audit F-0092): ze "stall:UiTask" zbylo
         * "stall:" a ze "stack:UartTask" zbylo "stack:". Zmizelo tedy JMENO TASKU,
         * tedy presne to, kvuli cemu se crash black-box kdysi rozsiroval ("prosty
         * IWDG reset byl nemy — RSR rekl jen watchdog, ne ktery task").
         * Tag ma jen `ERRLOG_TAG_LEN` = 6 znaku a format zaznamu se menit nema,
         * takze se resi DELBA: druh padu jde do `sub` (jedna hodnota misto sesti
         * znaku prefixu) a do tagu se ulozi az ROZLISUJICI cast za oddelovacem.
         * `sub` bylo navic v `errlog.h` dokumentovane jako "kind", ale jediny
         * zapisovatel do nej posilal nulu — dokumentovane pole se nikdy neplnilo. */
        const char *rest = "";
        uint8_t     ckind = crash_split((const char *)g_crash_text, &rest);
        for (; k < ERRLOG_TAG_LEN && rest[k]; k++) ct[k] = rest[k];
        ct[k] = '\0';
        s_el_cool_next[ERRLOG_K_CRASH] = HAL_GetTick();
        (void)errlog_put(ERRLOG_K_CRASH, ckind, g_crash_cfsr, g_crash_bfar, ct);
    }
}

/* ── errlog_fmt_detail — JEDEN zdroj pravdy pro vyznam `a`/`b`/`sub` ──────────
 * PROC vznikla: okno CHYBY do 2026-09-13 tiskla `a`/`b` jako holá čísla ("12345/
 * 6789") bez popisku — vyznam se pritom U KAZDEHO druhu LISI (viz tabulka v
 * errlog.h) a citelny byl jen ze zdrojaku volajiciho `errlog_put`. Funkce tu
 * znalost soustredi na JEDNO misto, aby ji nemusel znat kazdy dalsi konzument
 * zvlast (displej, a nyni i IPC kanal pro web — viz `ipc_errlog_service`). */
static const char *el_gate_name(uint8_t idx)
{
    switch (idx & 3u) {
    case 0:  return "0,1s";
    case 1:  return "1s";
    case 2:  return "10s";
    default: return "100s";
    }
}

void errlog_fmt_detail(const errlog_rec_t *r, char *buf, size_t n)
{
    if (!buf || n == 0u) return;
    buf[0] = '\0';
    if (!r) return;

    /* Tag nemusi byt 0-terminovany (viz errlog.h) — kopie pro %s pouziti. */
    char tag[ERRLOG_TAG_LEN + 1];
    memcpy(tag, r->tag, ERRLOG_TAG_LEN);
    tag[ERRLOG_TAG_LEN] = '\0';

    switch (r->kind) {
    case ERRLOG_K_BOOT:
        snprintf(buf, n, "duvod=%s, bring-up krok %u", tag, (unsigned)r->sub);
        break;
    case ERRLOG_K_CRASH: {
        /* >> Driv se tisknulo VYHRADNE `CFSR`/`BFAR` (audit F-0092) — a ty jsou
         * nenulove JEN u HardFaultu; `rtc.c` je pro stack/stall/assert/hal_err
         * neplni. U nejcastejsich druhu padu tedy okno CHYBY i web ukazovaly
         * doslova "CRASH  CFSR=0x00000000 BFAR=0x00000000", tedy ZE se pad stal,
         * ale ne CO spadlo. */
        const char *kn = (r->sub < (sizeof EL_CRASH_NAME / sizeof EL_CRASH_NAME[0]))
                       ? EL_CRASH_NAME[r->sub] : "?";
        if (r->a || r->b)
            snprintf(buf, n, "%s %s, CFSR=0x%08lX BFAR=0x%08lX", kn, tag,
                     (unsigned long)r->a, (unsigned long)r->b);
        else if (tag[0])
            snprintf(buf, n, "%s %s", kn, tag);
        else
            snprintf(buf, n, "%s", kn);
        break; }
    case ERRLOG_K_I2C:
        snprintf(buf, n, "sbernice I2C%u, chyb=%lu, resetu touche=%lu",
                 (unsigned)r->sub, (unsigned long)r->a, (unsigned long)r->b);
        break;
    case ERRLOG_K_UART:
        if (r->sub == 0xFFu)
            snprintf(buf, n, "fronta GPS plna, zahozeno bajtu=%lu", (unsigned long)r->a);
        else
            snprintf(buf, n, "ORE=%lu FE=%lu NE=%lu PE=%lu", (unsigned long)r->a,
                     (unsigned long)(r->b & 0xFFu), (unsigned long)((r->b >> 8) & 0xFFu),
                     (unsigned long)((r->b >> 16) & 0xFFu));
        break;
    case ERRLOG_K_SENSOR: {
        const char *name = (r->sub < SENS_COUNT) ? g_sensor_desc[r->sub].label : "?";
        snprintf(buf, n, "%s neodpovida, chyb celkem=%lu, v rade=%lu",
                 name, (unsigned long)r->a, (unsigned long)r->b);
        break;
    }
    case ERRLOG_K_FPGA:
        snprintf(buf, n, "%s, CRC chyb celkem=%lu",
                 (r->sub == 2u) ? "ztrata signalu" : "ztrata linku", (unsigned long)r->a);
        break;
    case ERRLOG_K_REF: {
        uint8_t newbits = (uint8_t)(r->sub & (uint8_t)~r->b);
        if (newbits & 0x08u)      snprintf(buf, n, "ztrata 10MHz reference (LOS_CLKIN)");
        else if (newbits & 0x10u) snprintf(buf, n, "PLL nezamknuty (PLL_LOL)");
        else                      snprintf(buf, n, "sticky=0x%02X (bylo 0x%02lX)",
                                            (unsigned)r->sub, (unsigned long)r->b);
        break;
    }
    case ERRLOG_K_STORAGE:
        if (r->sub == 1u) snprintf(buf, n, "flash zaneprazdnena, chyb=%lu", (unsigned long)r->a);
        else              snprintf(buf, n, "zapis selhal, chyb=%lu, seq=%lu",
                                    (unsigned long)r->a, (unsigned long)r->b);
        break;
    case ERRLOG_K_GPIO:
        snprintf(buf, n, "pin %s ztratil konfiguraci, celkem oprav=%lu", tag, (unsigned long)r->a);
        break;
    case ERRLOG_K_NET:
        if (r->sub == 1u) snprintf(buf, n, "CM4 zaseklo, pocet=%lu", (unsigned long)r->a);
        else              snprintf(buf, n, "restart CM4 vyzadan");
        break;
    case ERRLOG_K_CFG:
        switch (r->sub) {
        case ERRLOG_CFG_GATE:
            snprintf(buf, n, "brana %s -> %s", el_gate_name((uint8_t)r->b), el_gate_name((uint8_t)r->a));
            break;
        case ERRLOG_CFG_CHAN:
            snprintf(buf, n, "kanal %s -> %s", r->b ? "B" : "A", r->a ? "B" : "A");
            break;
        case ERRLOG_CFG_LOGPER:
            snprintf(buf, n, "interval logu %lus -> %lus", (unsigned long)r->b, (unsigned long)r->a);
            break;
        case ERRLOG_CFG_LOGSTORE:
            snprintf(buf, n, "uloziste logu: %s -> %s",
                     datalog_store_name((uint8_t)r->b), datalog_store_name((uint8_t)r->a));
            break;
        case ERRLOG_CFG_CALIB:
            snprintf(buf, n, "ulozena kalibrace napeti");
            break;
        default:
            snprintf(buf, n, "%lu -> %lu", (unsigned long)r->a, (unsigned long)r->b);
            break;
        }
        break;
    default:
        snprintf(buf, n, "a=%lu b=%lu", (unsigned long)r->a, (unsigned long)r->b);
        break;
    }
}
