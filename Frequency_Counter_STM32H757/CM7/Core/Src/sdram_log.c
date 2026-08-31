/**
 * @file    sdram_log.c
 * @brief   Datova cache mereni v SDRAM (viz sdram_log.h pro zduvodneni a smlouvu).
 */

#include "sdram_log.h"
#include "main.h"          /* SCB_InvalidateDCache_by_Addr, __DMB */
#include <string.h>
#include <stdio.h>

/* Hranice regionu z linkeru (sekce `.measlog` v STM32H757BITX_FLASH.ld). Adresa
 * se ZAMERNE nepise natvrdo — kdyz se region v linkeru posune nebo zmensi, kod
 * se prizpusobi a linker hlida preteceni.
 * ⚠️ Sekce NESMI zacinat na `.sdram` — sekce `.sdram` v linkeru ma wildcard
 * `*(.sdram*)`, ktery by ji spolkl driv a 16MB pole by skoncilo v 8MB regionu. */
extern uint32_t _smeaslog;
extern uint32_t _emeaslog;

/* Vlastni uloziste v te sekci. Velikost = cely region minus zarovnani.
 * ⚠️ Pole je deklarovane pres sekci, ne pres pevnou adresu — jinak by linker
 * o obsazenosti regionu nevedel. */
#define SDRAM_LOG_BYTES   (8u * 1024u * 1024u)
#define SDRAM_LOG_CAP     (SDRAM_LOG_BYTES / sizeof(sdram_log_rec_t))   /* 262 144 */

static sdram_log_rec_t s_buf[SDRAM_LOG_CAP]
    __attribute__((section(".measlog"), aligned(32)));

/* ⚠️ Kapacita MUSI byt mocnina 2 — index se pak maskuje, ne deli. Delenim by
 * kazdy zapis stal ~20 cyklu navic a `put` bezi v FpgaTasku. */
_Static_assert((SDRAM_LOG_CAP & (SDRAM_LOG_CAP - 1u)) == 0u,
               "SDRAM_LOG_CAP musi byt mocnina 2 (index se maskuje)");
_Static_assert(sizeof(sdram_log_rec_t) == 32u, "zaznam musi zustat 32 B (oba kanaly)");

static volatile uint32_t s_head;      /* kolik zaznamu CELKEM proslo (nikdy neklesa) */
static volatile uint32_t s_dropped;   /* zahozeno, protoze log nebyl ready */
static uint8_t  s_ready;
static uint8_t  s_sim;       /* 1 = obsah je z emulatoru `fpgasim` (viz sdram_log_put) */
static uint64_t s_gate_ns;   /* brana teto epochy = tau0; zmena vynuluje ring */
static char     s_fail[48];

/* ── Overeni pameti regionu ───────────────────────────────────────────────────
 * `membench` testuje adresni aliasing jen do 2 MB, ale tenhle region zacina az
 * na 0xC1000000. Kdyby se adresy opakovaly (otevrene podezreni na pajku
 * `FMC_A9`/`PF15`), log by se tise prepisoval a analyza by pocitala nesmysly —
 * coz je HORSI nez log nemit. Proto se pri initu region overi a pri selhani
 * se log NEZAPNE.
 *
 * Testuje se:
 *   1) ADRESNI ALIASING pres CELY region — do ruznych mocnin 2 se zapise ruzna
 *      hodnota a pak se VSECHNY zkontroluji. Kdyby dve adresy byly tataz bunka,
 *      pozdejsi zapis prepise drivejsi a kontrola to chytne.
 *   2) DATOVE LINKY na nekolika mistech (0x00/0xFF/0x55AA/adresa-v-adrese).
 * ⚠️ Cte se `volatile` + s cache maintenance, jinak by verify mohl cist z cache
 * misto z SDRAM a vadu by NIKDY neodhalil (stejna past jako u membenche). */
/* Sondy = mocniny 2 od 32 B po polovinu regionu (8 B slova: 2^3 .. 2^21 slova).
 * ⚠️ Pocet MUSI dosahnout az na konec regionu. Puvodni hodnota 12 pokryvala jen
 * 2^3..2^14 slov = 64 kB, takze test hlasil "bez aliasu", aniz by se vubec podival
 * do pasma, kde se alias PODEZIRA (2 MB, pajka FMC_A9/PF15) — dokonale falesne
 * uklidneni. `_Static_assert` nize to uz nedovoli. */
#define ALIAS_PROBES 24u

/* Vynuti pruchod do SDRAM pro JEDNO slovo. ⚠️ Zamerne se NEudrzuje cache nad
 * celym regionem — 16 MB je 524288 cache-line operaci a delalo by se to pri
 * kazdem vzoru zbytecne. Sonda se dotyka jen par slov, takze staci jejich
 * radky. Bez teto udrzby by verify cetl z D-cache misto z SDRAM a vadu by
 * NIKDY neodhalil (tataz past, pred kterou varuje membench). */
static void flush_word(volatile uint32_t *p)
{
    __DSB();
    SCB_CleanDCache_by_Addr((uint32_t *)(void *)p, 4);
    SCB_InvalidateDCache_by_Addr((uint32_t *)(void *)p, 4);
    __DSB();
}

/* 2^3 (32 B) az 2^20 slov (4 MB) = 18 sond pri 8MB regionu; 24 necha rezervu.
 * Podminka nize je odvozena od SDRAM_LOG_BYTES — pri zvetseni regionu ji zvys. */
_Static_assert((3u + ALIAS_PROBES) >= 21u,
               "ALIAS_PROBES nedosahne na konec regionu — alias by zustal neodhaleny");

/* ── Alias na FRAMEBUFFERY ────────────────────────────────────────────────────
 * Kontrola vyse hleda opakovani adres UVNITR regionu. Kdyby ale mel cip mensi
 * skutecnou kapacitu, nez predpokladame, mohla by se 0xC1000000 mapovat rovnou
 * na 0xC0000000 = FB0 a log by prepisoval OBRAZ (a obraz log). To je nejdrazsi
 * mozny nasledek, tak se testuje zvlast.
 * Sonda je REVERZIBILNI: puvodni slovo FB se vzdy vrati.
 * ⚠️ Bezi soubezne s kreslenim UiTasku, takze kdyby prave do toho slova zapsal,
 * vratime o snimek stary pixel v levem hornim rohu. Jeden pixel, jednou pri
 * bootu — a kdyby alias existoval, je obraz rozbity uz tak jako tak. */
static int aliases_framebuffer(void)
{
    static const uint32_t FB[] = { 0xC0000000u, 0xC0100000u, 0xC0200000u };
    volatile uint32_t *log0 = (volatile uint32_t *)(void *)s_buf;
    for (unsigned i = 0; i < sizeof FB / sizeof FB[0]; i++) {
        volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)FB[i];
        flush_word(fb);
        uint32_t save = *fb;
        *log0 = ~save;                      /* zarucene jina hodnota nez v FB */
        flush_word(log0);
        flush_word(fb);
        int hit = (*fb != save);
        *fb = save;                         /* vratit VZDY, i pri nalezu */
        flush_word(fb);
        if (hit) {
            snprintf(s_fail, sizeof s_fail, "ALIAS na FB%u (0x%08lX)!",
                     i, (unsigned long)FB[i]);
            return 1;
        }
    }
    return 0;
}

static int region_selfcheck(void)
{
    volatile uint32_t *base = (volatile uint32_t *)(void *)s_buf;
    const uint32_t words = SDRAM_LOG_BYTES / 4u;

    if (aliases_framebuffer()) return 0;

    /* 1) ADRESNI ALIASING pres cely region: na kazdou mocninu 2 jina hodnota,
     * pak se VSECHNY zkontroluji. Kdyby dve adresy byly tataz bunka, pozdejsi
     * zapis prepise drivejsi a kontrola to chytne. */
    uint32_t offs[ALIAS_PROBES];
    uint32_t n = 0;
    for (uint32_t off = 8u; off < words && n < ALIAS_PROBES; off <<= 1) offs[n++] = off;
    base[0] = 0xA5A5FFFFu;
    for (uint32_t i = 0; i < n; i++) base[offs[i]] = 0xA5A50000u + i;
    flush_word(&base[0]);
    for (uint32_t i = 0; i < n; i++) flush_word(&base[offs[i]]);
    if (base[0] != 0xA5A5FFFFu) {
        snprintf(s_fail, sizeof s_fail, "zapis/cteni selhalo @+0");
        return 0;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (base[offs[i]] != 0xA5A50000u + i) {
            snprintf(s_fail, sizeof s_fail, "ADRESY SE OPAKUJI po %lu kB",
                     (unsigned long)(offs[i] * 4u / 1024u));
            return 0;
        }
    }

    /* 2) DATOVE LINKY — vzory na nekolika mistech regionu (stuck-at, zkraty). */
    static const uint32_t PAT[] = { 0x00000000u, 0xFFFFFFFFu, 0x55555555u, 0xAAAAAAAAu };
    const uint32_t spots[] = { 0u, words / 4u, words / 2u, words - 1u };
    for (unsigned p = 0; p < sizeof PAT / sizeof PAT[0]; p++) {
        for (unsigned k = 0; k < sizeof spots / sizeof spots[0]; k++) {
            base[spots[k]] = PAT[p];
            flush_word(&base[spots[k]]);
        }
        for (unsigned k = 0; k < sizeof spots / sizeof spots[0]; k++)
            if (base[spots[k]] != PAT[p]) {
                snprintf(s_fail, sizeof s_fail, "vzor 0x%08lX selhal @+%lu kB",
                         (unsigned long)PAT[p], (unsigned long)(spots[k] * 4u / 1024u));
                return 0;
            }
    }
    return 1;
}

int sdram_log_init(void)
{
    s_head = 0; s_dropped = 0; s_ready = 0; s_sim = 0; s_gate_ns = 0; s_fail[0] = '\0';

    /* Pojistka proti rozjeti linkeru a kodu. */
    if ((uint32_t)(uintptr_t)&_emeaslog - (uint32_t)(uintptr_t)&_smeaslog < SDRAM_LOG_BYTES) {
        snprintf(s_fail, sizeof s_fail, "linker region mensi nez buffer");
        return 0;
    }
    if (!region_selfcheck()) return 0;

    s_ready = 1;
    return 1;
}

/* Zmenila se brana natolik, ze uz to neni tataz epocha? `gate_time_ns` z ramce
 * kolisa o ppm (250046966 vs 250048179), takze porovnavat na rovnost by ring
 * mazalo pri KAZDEM vzorku. Prah 10 % bezpecne oddeli presety brany
 * (0,1 / 1 / 10 / 100 s) od toho kolisani. */
static int gate_changed(uint64_t a, uint64_t b)
{
    if (a == 0u || b == 0u) return a != b;
    uint64_t d = (a > b) ? (a - b) : (b - a);
    return d * 10u > ((a < b) ? a : b);
}

void sdram_log_put(uint32_t seq, uint64_t fa_uhz, uint64_t fb_uhz, uint32_t flags,
                   uint32_t t_ms, uint64_t gate_ns, uint8_t sim)
{
    if (!s_ready) { s_dropped++; return; }
    /* Zmena epochy (REAL<->SIM nebo jina brana) zahodi obsah: nesouměřitelné
     * vzorky se nesmi michat — jinak by Allan pocital pres dve ruzna tau0 a
     * vysledek by vypadal duveryhodne a byl spatne. Tataz politika jako
     * u Allan/trend pyramidy v screen_main a u bufferu ve web SPA. */
    sim = sim ? 1u : 0u;
    if (sim != s_sim || gate_changed(gate_ns, s_gate_ns)) {
        s_sim = sim; s_gate_ns = gate_ns; s_head = 0;
    }
    uint32_t h = s_head;
    sdram_log_rec_t *r = &s_buf[h & (SDRAM_LOG_CAP - 1u)];
    r->seq = seq; r->t_ms = t_ms;
    r->fa_uhz = fa_uhz; r->fb_uhz = fb_uhz;
    r->flags = flags;   r->reserved = 0;
    __DMB();                 /* zaznam viditelny PRED zvednutim head (viz .h) */
    s_head = h + 1u;
}

/* Prevede "i-ty nejnovejsi" na absolutni index; 0 = mimo rozsah. */
static int idx_of(uint32_t head, uint32_t i, uint32_t *abs_out)
{
    uint32_t have = (head < SDRAM_LOG_CAP) ? head : SDRAM_LOG_CAP;
    if (i >= have) return 0;
    *abs_out = head - 1u - i;
    return 1;
}

int sdram_log_get(uint32_t i, sdram_log_rec_t *out)
{
    if (!s_ready || out == NULL) return 0;
    uint32_t h = s_head, abs;
    if (!idx_of(h, i, &abs)) return 0;
    *out = s_buf[abs & (SDRAM_LOG_CAP - 1u)];
    /* Producent mohl mezitim prepsat prave tenhle (nejstarsi) zaznam. */
    if (s_head - abs > SDRAM_LOG_CAP) return 0;
    return 1;
}

uint32_t sdram_log_read_back(uint32_t i, sdram_log_rec_t *out, uint32_t n)
{
    if (!s_ready || out == NULL || n == 0u) return 0;
    uint32_t h = s_head, abs;
    if (!idx_of(h, i, &abs)) return 0;
    /* ⚠️ Dolni mez cteni: po pretoceni uz nejstarsi platny zaznam NENI index 0,
     * ale `h - CAP`. Bez teto meze by se cetlo pod nej (do dat, ktera producent
     * uz prepsal) a zaverecna kontrola by pak zahodila CELOU davku misto toho,
     * aby vratila platnou cast. */
    uint32_t oldest = (h > SDRAM_LOG_CAP) ? (h - SDRAM_LOG_CAP) : 0u;
    uint32_t got = 0;
    while (got < n) {
        uint32_t a = abs - got;
        out[got++] = s_buf[a & (SDRAM_LOG_CAP - 1u)];
        if (a == oldest) break;                   /* dosli jsme na nejstarsi platny */
    }
    /* Jedna kontrola na konci: prepsal producent behem cteni neco, co jsme vzali? */
    if (s_head - (abs - (got - 1u)) > SDRAM_LOG_CAP) return 0;
    return got;
}

void sdram_log_stat(sdram_log_stat_t *out)
{
    if (out == NULL) return;
    uint32_t h = s_head;
    out->ready    = s_ready;
    out->capacity = SDRAM_LOG_CAP;
    out->total    = h;
    out->count    = (h < SDRAM_LOG_CAP) ? h : SDRAM_LOG_CAP;
    out->wrapped  = (h > SDRAM_LOG_CAP) ? 1u : 0u;
    out->sim      = s_sim;
    out->gate_ns  = s_gate_ns;
    out->dropped  = s_dropped;
    snprintf(out->fail, sizeof out->fail, "%s", s_fail);
}

void sdram_log_reset(void) { s_head = 0; }

void sdram_log_invalidate(void)
{
    SCB_InvalidateDCache_by_Addr((uint32_t *)(void *)s_buf, (int32_t)SDRAM_LOG_BYTES);
}

/* ── Selftest indexovani (ciste logicky, bez SDRAM) ───────────────────────────
 * Overuje prave to, co se snadno rozbije: prevod "i-ty nejnovejsi" na absolutni
 * index pred i po pretoceni ringu. */
int sdram_log_selftest(void)
{
    uint32_t a;
    /* prazdny log */
    if (idx_of(0u, 0u, &a)) return 0;
    /* 3 zaznamy, jeste bez pretoceni */
    if (!idx_of(3u, 0u, &a) || a != 2u) return 0;   /* nejnovejsi */
    if (!idx_of(3u, 2u, &a) || a != 0u) return 0;   /* nejstarsi */
    if (idx_of(3u, 3u, &a))             return 0;   /* mimo rozsah */
    /* presne plny */
    if (!idx_of(SDRAM_LOG_CAP, 0u, &a) || a != SDRAM_LOG_CAP - 1u) return 0;
    if (!idx_of(SDRAM_LOG_CAP, SDRAM_LOG_CAP - 1u, &a) || a != 0u) return 0;
    if (idx_of(SDRAM_LOG_CAP, SDRAM_LOG_CAP, &a))                  return 0;
    /* po pretoceni: viditelnych je jen poslednich CAP */
    uint32_t h = SDRAM_LOG_CAP + 5u;
    if (!idx_of(h, 0u, &a) || a != h - 1u)                    return 0;
    if (!idx_of(h, SDRAM_LOG_CAP - 1u, &a) || a != h - SDRAM_LOG_CAP) return 0;
    if (idx_of(h, SDRAM_LOG_CAP, &a))                         return 0;
    /* maskovani indexu = mocnina 2 */
    if (((SDRAM_LOG_CAP - 1u) & SDRAM_LOG_CAP) != 0u) return 0;
    return 1;
}
