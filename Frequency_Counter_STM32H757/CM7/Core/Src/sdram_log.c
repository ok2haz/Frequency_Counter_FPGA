/**
 * @file    sdram_log.c
 * @brief   Datova cache mereni v SDRAM (viz sdram_log.h pro zduvodneni a smlouvu).
 */

#include "sdram_log.h"
#include "main.h"          /* SCB_InvalidateDCache_by_Addr, __DMB */
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "cmsis_os.h"      /* osDelay v `sdram_log_allan_report` (UartTask) */

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
/* Cislo epochy pro cteni od kurzoru (`sdram_log_read_abs`): roste s kazdym vynulovanim `s_head`.
 * Zapisuje ho ten, kdo nuluje `s_head` (producent; vyjimka `sdram_log_reset_force`, viz .h). */
static volatile uint32_t s_epoch;
/* Pozadavek na vynulovani z CIZI ulohy. Zapisuje ho kdokoli, konzumuje VYHRADNE
 * producent v `sdram_log_put` — tim zustane `s_head` v rukach jedine ulohy
 * (audit F-0118, vzor `g_membench_req`). */
static volatile uint8_t  s_reset_req;
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
/* ⚠️ Pohled na region po 32bitovych SLOVECH (test pameti + cache maintenance).
 * `may_alias` tu NENI kosmetika: `s_buf` je pole `sdram_log_rec_t`, takze pristup
 * pres `uint32_t*` je z pohledu normy poruseni strict-aliasing pravidel — hlasi to
 * `-Wstrict-aliasing=2` z auditniho receptu (CLAUDE.md „Staticky audit"). U testu
 * pameti je „syrova slova" prave ten zamer, takze se kompilatoru rekne pravda:
 * tenhle ukazatel smi alias cokoli. Bez toho smi GCC povazovat zapis pres slovo za
 * nesouvisejici se zaznamy — a prave na tom by test aliasu tise prestal fungovat. */
typedef uint32_t __attribute__((may_alias)) sdram_word_t;

/* ⚠️ NEJEN framebuffery (audit F-0117): `.sdram` (bg_cache, glyph atlas) lezi
 * od logu presne 8 MB, tedy JEDEN adresni bit (HADDR[23]) — a poskozeny
 * bg_cache se projevi jako problikavani cele plochy, protoze se zapisuje
 * jednou v `screen_main_init()` a pak uz jen cte. Presne ten symptom se
 * v tomhle projektu hledal tri kola ve vykreslovacim kodu (STATUS #138/#141),
 * takze vynechat ho z teto kontroly by bylo to nejdrazsi mozne opomenuti. */
static int aliases_reserved_sdram(void)
{
    static const struct { uint32_t addr; const char *name; } REG[] = {
        { 0xC0000000u, "FB0"    },
        { 0xC0100000u, "FB1"    },
        { 0xC0200000u, "FB2"    },
        { 0xC0300000u, "canvas" },
        { 0xC0800000u, ".sdram" },   /* bg_cache, glyph atlas, g_mask_a/b */
    };
    volatile sdram_word_t *log0 = (volatile sdram_word_t *)(void *)s_buf;
    for (unsigned i = 0; i < sizeof REG / sizeof REG[0]; i++) {
        volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)REG[i].addr;
        flush_word(fb);
        uint32_t save = *fb;
        *log0 = ~save;                      /* zarucene jina hodnota nez v cili */
        flush_word(log0);
        flush_word(fb);
        int hit = (*fb != save);
        *fb = save;                         /* vratit VZDY, i pri nalezu */
        flush_word(fb);
        if (hit) {
            snprintf(s_fail, sizeof s_fail, "ALIAS na %s (0x%08lX)!",
                     REG[i].name, (unsigned long)REG[i].addr);
            return 1;
        }
    }
    return 0;
}

static int region_selfcheck(void)
{
    volatile sdram_word_t *base = (volatile sdram_word_t *)(void *)s_buf;
    const uint32_t words = SDRAM_LOG_BYTES / 4u;

    if (aliases_reserved_sdram()) return 0;

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

static uint32_t epoch_step(uint32_t e);   /* nize u rec_still_valid (F-0238) */

void sdram_log_put(uint32_t seq, uint64_t fa_uhz, uint64_t fb_uhz, uint32_t flags,
                   uint32_t t_ms, uint64_t gate_ns, uint8_t sim)
{
    if (!s_ready) { s_dropped++; return; }
    /* Pozadavek na reset z cizi ulohy — konzumuje ho VYHRADNE producent, aby
     * `s_head` nemel dva zapisovatele (audit F-0118). Musi to byt PRED `h = s_head`
     * nize, jinak by prave vznikl ten race, ktery to ma odstranit. */
    if (s_reset_req) { s_reset_req = 0u; s_head = 0; }
    /* Zmena epochy (REAL<->SIM nebo jina brana) zahodi obsah: nesouměřitelné
     * vzorky se nesmi michat — jinak by Allan pocital pres dve ruzna tau0 a
     * vysledek by vypadal duveryhodne a byl spatne. Tataz politika jako
     * u Allan/trend pyramidy v screen_main a u bufferu ve web SPA. */
    sim = sim ? 1u : 0u;
    if (sim != s_sim || gate_changed(gate_ns, s_gate_ns)) {
        s_sim = sim; s_gate_ns = gate_ns; s_head = 0;
    }
    if (s_head == 0u) {           /* prvni zaznam epochy (i po startu) -> ctenar od kurzoru pozna novou radu */
        uint32_t e = s_epoch;
        /* 🔴 F-0238: `s_epoch` je v .bss, takze by kazdy boot zacinal epochou 1 a klient navazujici kurzorem by po
         * restartu desky nepoznal, ze jde o jinou radu. Prvni epocha po startu se proto bere z cyklu CPU v okamziku
         * PRVNIHO mereni: FPGA meri nezavisle na bootu STM32, takze tento okamzik je nahodny v radu stovek ms
         * (~1e8 cyklu). DWT bezi od bootu (bootled/runtime stats); kdyby nebezel, vyjde 1 jako drive. */
        if (e == 0u) { e = DWT->CYCCNT * 2654435761u; e ^= e >> 15; }
        s_epoch = epoch_step(e);
    }
    uint32_t h = s_head;
    sdram_log_rec_t *r = &s_buf[h & (SDRAM_LOG_CAP - 1u)];
    r->seq = seq; r->t_ms = t_ms;
    r->fa_uhz = fa_uhz; r->fb_uhz = fb_uhz;
    r->flags = flags;   r->reserved = 0;
    __DMB();                 /* zaznam viditelny PRED zvednutim head (viz .h) */
    s_head = h + 1u;
}

/* Je zkopirovany zaznam s absolutnim indexem `a` jeste platny, kdyz po kopii hlava ukazuje `head`?
 * 🔴 F-0237: slot zaznamu `a` prepisuje zapis indexu `a + CAP` a ten probiha, kdyz `s_head == a + CAP`
 * (hlava se zvedne az PO zapisu). Neplatne je proto uz `head - a >= CAP`, ne az `> CAP` — drive projel
 * rozepsany nejstarsi zaznam jako platny. Ciste, kryje selftest. */
static int rec_still_valid(uint32_t head, uint32_t a)
{
    return (head - a) < SDRAM_LOG_CAP;
}

/* Dalsi cislo epochy: 1..2^30-1, nikdy 0 (0 = "klient nezna"). Strop 2^30: CM4 parsuje `epoch` z URL pres
 * `atol` (long = 32 b), takze cislo musi zustat kladne i po mnoha epochach. Ciste, kryje selftest. */
static uint32_t epoch_step(uint32_t e)
{
    e = (e + 1u) & 0x3FFFFFFFu;
    return e ? e : 1u;
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
    __DMB();                 /* kopie dat PRED kontrolou hlavy (prekladac nesmi cteni s_buf presunout za ni) */
    /* Producent mohl mezitim prepsat prave tenhle (nejstarsi) zaznam. */
    if (!rec_still_valid(s_head, abs)) return 0;
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
    __DMB();                 /* kopie dat PRED kontrolou hlavy */
    /* Jedna kontrola na konci: prepsal producent behem cteni neco, co jsme vzali? */
    if (!rec_still_valid(s_head, abs - (got - 1u))) return 0;
    return got;
}

/* Okno [oldest, head) v absolutnich indexech; ciste, kryje selftest. */
static void win_of(uint32_t head, uint32_t *oldest)
{
    *oldest = (head > SDRAM_LOG_CAP) ? (head - SDRAM_LOG_CAP) : 0u;
}

uint32_t sdram_log_read_abs(uint32_t from, sdram_log_rec_t *out, uint32_t n,
                            uint32_t *first, sdram_log_win_t *w)
{
    uint32_t ep = s_epoch;
    uint32_t h  = s_head;
    uint32_t lo;
    win_of(h, &lo);
    if (w != NULL) {
        w->epoch = ep; w->head = h; w->oldest = lo;
        w->gate_ns = s_gate_ns; w->sim = s_sim; w->ready = s_ready;
    }
    if (first != NULL) *first = (from < lo) ? lo : from;
    if (!s_ready || out == NULL || n == 0u) return 0;
    uint32_t a0 = (from < lo) ? lo : from;
    if (a0 >= h) return 0;                        /* nic noveho */
    uint32_t got = h - a0;
    if (got > n) got = n;
    for (uint32_t k = 0; k < got; k++) out[k] = s_buf[(a0 + k) & (SDRAM_LOG_CAP - 1u)];
    __DMB();
    /* Behem kopie mohl producent (a) prepsat nejstarsi ctene zaznamy po pretoceni, nebo (b) zahajit
     * novou epochu (vynulovat `s_head`). V obou pripadech je kopie nedoveryhodna -> 0, klient se zepta znovu. */
    if (s_epoch != ep || s_head < h || !rec_still_valid(s_head, a0)) {
        if (w != NULL) w->epoch = s_epoch;
        return 0;
    }
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

/* ── Reset obsahu: POZADAVEK, ne zapis (audit F-0118) ─────────────────────────
 * 🔴 Driv to byl holy `s_head = 0` volany z UartTasku — tedy DRUHY zapisovatel
 * `s_head`, presto ze kontrakt v hlavicce deklaruje JEDINEHO producenta a cela
 * bezzamkovost na tom stoji. Producent dela `h = s_head; … s_head = h + 1u;`,
 * tedy read-modify-write s bodem preempce uprostred: kdyby reset padl mezi ty
 * dva kroky, producent by hodnotu OBNOVIL a reset by se ticho ztratil.
 * Poskozeni dat to nezpusobovalo (zaznam mel platny slot a ctenari maji kontrolu
 * `s_head - abs > CAP`), ale deklarovany invariant neplatil — a pristi uprava se
 * o nej muze opret. Vzor je tentyz, jaky uz pouziva `membench` (`g_membench_req`).
 *
 * ⚠️ `s_head` tim zustava ve vlastnictvi JEDINE ulohy (FpgaTask). Cena je, ze
 * reset se projevi az s pristim vzorkem — pri mrtvem linku by tedy „nezabral",
 * proto ma volajici k dispozici `sdram_log_reset_done()` a jako posledni instanci
 * `sdram_log_reset_force()`. Rozhodovani o timeoutu patri volajicimu, ktery ma
 * pristup k RTOS; tenhle modul zustava bez zavislosti na scheduleru. */
void sdram_log_reset_request(void) { s_reset_req = 1u; }

int sdram_log_reset_done(void) { return s_reset_req ? 0 : 1; }

/* Fallback pro pripad, ze producent NEBEZI (mrtvy SPI link, zastavene mereni).
 * ⚠️ Tady se invariant „jeden zapisovatel" VEDOME porusuje — smi se to jen kdyz
 * je dolozeno, ze producent neprisel (viz timeout u volajiciho), a volajici to
 * MUSI uzivateli rict, aby „vynulovano" neznamenalo dvakrat neco jineho. */
void sdram_log_reset_force(void) { s_reset_req = 0u; s_head = 0; }

void sdram_log_invalidate(void)
{
    /* 🔴 CLEAN+INVALIDATE, ne samotna invalidace (audit F-0011). Oblast je MPU R3 =
     * WBWA a dnes ji plni CPU (FpgaTask), takze cerstve zaznamy mohou byt jeste
     * SPINAVE v D-cache. Samotna invalidace by je zahodila — konzument, ktery se
     * ridi doporucenim v hlavicce, by tim prisel prave o ta nejnovejsi data.
     * Clean navic nic nestoji, kdyz log plni DMA (zadne spinave radky nejsou). */
    SCB_CleanInvalidateDCache_by_Addr((sdram_word_t *)(void *)s_buf, (int32_t)SDRAM_LOG_BYTES);
}

/* ── Selftest indexovani (ciste logicky, bez SDRAM) ───────────────────────────
 * Overuje prave to, co se snadno rozbije: prevod "i-ty nejnovejsi" na absolutni
 * index pred i po pretoceni ringu. */

/* ══════════════ Allan z .measlog: OVERLAPPING s krokem tau0' na CELE historii (STATUS #321, audit A2 V5) ═══════════════
 * Pyramida v screen_main.c ma na stage s jedinou hodnotu za 10^s s, takze n clenu na tau = 1000 s je po 10 h jen ~35.
 * Tady se z .measlog (surova mereni, ~18 h) pocita overlapping ADEV/MDEV/HDEV s krokem 1 s (tau0'), tedy u tau = 1000 s
 * ~34 000 clenu. Clenu je vic, ale jsou silne korelovane, takze spolehlivost roste mene (SP1065: EDF jen ~1,5-3x).
 *
 * Jednoprusuhovy proud: faze X_n a jeji soucet P_n (P_n = sum_{i<n} X_i) v kruhovych polich delky 3*MMAX+1; novy clen kazdeho
 * m konci nejnovejsim bodem n:   ADEV  d = X_n - 2X_{n-m} + X_{n-2m}          AVAR = sum d^2 / (2 m^2 t0^2 n)
 *                                HDEV  d = X_n - 3X_{n-m} + 3X_{n-2m} - X_{n-3m}   HVAR = sum d^2 / (6 m^2 t0^2 n)
 *                                MDEV  d = P_n - 3P_{n-m} + 3P_{n-2m} - P_{n-3m}   MVAR = sum d^2 / (2 m^4 t0^2 n)
 * Pamet 2 x 3001 double = 48 kB (.bss), cas ~0,3 s na 262 144 zaznamu. Beh jen v UartTasku (`allan log`), nesmi se prekryvat
 * se selftestem (priznak s_alg_busy). Mezera (seq != prev+1, zaznam bez platneho mereni, skok f_ref > 1e-4) zacne novy segment:
 * soucty zustanou, zadny clen nepresahne mezeru. */
#define ALG_MMAX  1000u
#define ALG_L     (3u * ALG_MMAX + 1u)
#define ALG_NM    16u
static const uint16_t ALG_M[ALG_NM] = {1, 2, 3, 5, 7, 10, 20, 30, 50, 70, 100, 200, 300, 500, 700, 1000};
typedef struct { double sa, sh, sm; uint32_t na, nh, nm; } alg_acc_t;
static double    s_alg_x[ALG_L], s_alg_p[ALG_L];
static alg_acc_t s_alg_acc[ALG_NM];
static uint32_t  s_alg_n;                    /* bodu faze v aktualnim segmentu */
static double    s_alg_cx, s_alg_cp;         /* X_{n-1}, P_{n-1} (po pushi: X_n, P_n) */
static volatile uint8_t s_alg_busy;

static void alg_clear(void) { memset(s_alg_acc, 0, sizeof s_alg_acc); s_alg_n = 0u; }
static void alg_segment(void) { s_alg_n = 0u; }

/* Prida dalsi bod faze (y = prumerna frakcni odchylka bloku, t0 = delka bloku [s]); prvni bod segmentu ma X = 0 (y se ignoruje). */
static void alg_push(double y, double t0)
{
    uint32_t n = s_alg_n;
    if (n == 0u) { s_alg_cx = 0.0; s_alg_cp = 0.0; }
    else         { s_alg_cp += s_alg_cx; s_alg_cx += y * t0; }
    s_alg_x[n % ALG_L] = s_alg_cx; s_alg_p[n % ALG_L] = s_alg_cp;
    for (uint32_t i = 0; i < ALG_NM; i++) {
        uint32_t m = ALG_M[i];
        alg_acc_t *a = &s_alg_acc[i];
        if (n >= 2u * m) {
            double d = s_alg_x[n % ALG_L] - 2.0 * s_alg_x[(n - m) % ALG_L] + s_alg_x[(n - 2u * m) % ALG_L];
            a->sa += d * d; a->na++;
        }
        if (n >= 3u * m) {
            double d3 = s_alg_x[n % ALG_L] - 3.0 * s_alg_x[(n - m) % ALG_L] + 3.0 * s_alg_x[(n - 2u * m) % ALG_L]
                      - s_alg_x[(n - 3u * m) % ALG_L];
            double dp = s_alg_p[n % ALG_L] - 3.0 * s_alg_p[(n - m) % ALG_L] + 3.0 * s_alg_p[(n - 2u * m) % ALG_L]
                      - s_alg_p[(n - 3u * m) % ALG_L];
            a->sh += d3 * d3; a->nh++;
            a->sm += dp * dp; a->nm++;
        }
    }
    s_alg_n = n + 1u;
}

static double alg_dev(const alg_acc_t *a, uint32_t m, double t0, int kind)   /* 0 ADEV, 1 MDEV, 2 HDEV; 0 = malo clenu */
{
    double mm = (double)m;
    if (kind == 0) return (a->na >= 2u) ? sqrt(a->sa / (2.0 * mm * mm * t0 * t0 * (double)a->na)) : 0.0;
    if (kind == 1) return (a->nm >= 2u) ? sqrt(a->sm / (2.0 * mm * mm * mm * mm * t0 * t0 * (double)a->nm)) : 0.0;
    return (a->nh >= 2u) ? sqrt(a->sh / (6.0 * mm * mm * t0 * t0 * (double)a->nh)) : 0.0;
}

static void alg_sci(char *b, size_t n, double v)
{
    if (!(v > 0.0)) { snprintf(b, n, "--"); return; }
    int e = (int)floor(log10(v));
    double mant = v / pow(10.0, (double)e);
    int mi = (int)(mant * 100.0 + 0.5);
    if (mi >= 1000) { mi = 100; e++; }
    unsigned ae = (unsigned)((e < 0) ? -e : e) % 100u;       /* %-ovani: jen kvuli -Wformat-truncation (rozsah je tak jako tak < 1e+-40) */
    snprintf(b, n, "%u.%02uE%c%02u", ((unsigned)mi / 100u) % 10u, (unsigned)mi % 100u, (e < 0) ? '-' : '+', ae);
}

/* Pure-logic kontrola proti primemu vypoctu (N bodu, m = 1, 3, 5): totez co SP1065 z fazi X (MDEV pres S_k = sum X_i). */
static int alg_selftest(void)
{
    if (s_alg_busy) return 1;                    /* bezi vypocet z UartTasku -> nesahat na sdilena pole */
    s_alg_busy = 1u;
    static double xd[130];
    uint32_t r = 31337u;
    alg_clear(); alg_segment();
    const uint32_t NP = 120u;
    for (uint32_t k = 0; k < NP; k++) {
        r = r * 1103515245u + 12345u;
        double y = 1e-9 * ((double)(r >> 8) / 16777216.0 - 0.5);
        alg_push(y, 1.0);
        xd[k] = s_alg_cx;
    }
    int ok = 1;
    static const uint16_t TM[3] = {1, 3, 5};
    for (int t = 0; t < 3; t++) {
        uint32_t m = TM[t], i = 0;
        while (ALG_M[i] != m) i++;
        double sa = 0.0, sh = 0.0, sm = 0.0; uint32_t na = 0, nh = 0, nm = 0;
        for (uint32_t k = 0; k + 2u * m < NP; k++) {
            double d = xd[k + 2u * m] - 2.0 * xd[k + m] + xd[k]; sa += d * d; na++;
        }
        for (uint32_t k = 0; k + 3u * m < NP; k++) {
            double d3 = xd[k + 3u * m] - 3.0 * xd[k + 2u * m] + 3.0 * xd[k + m] - xd[k]; sh += d3 * d3; nh++;
            double s0 = 0.0, s1 = 0.0, s2 = 0.0;
            for (uint32_t q = 0; q < m; q++) { s0 += xd[k + q]; s1 += xd[k + m + q]; s2 += xd[k + 2u * m + q]; }
            double dm = s2 - 2.0 * s1 + s0; sm += dm * dm; nm++;
        }
        const alg_acc_t *a = &s_alg_acc[i];
        ok &= (a->na == na) && (a->nh == nh) && (a->nm == nm);
        ok &= (fabs(a->sa / sa - 1.0) < 1e-9) && (fabs(a->sh / sh - 1.0) < 1e-9) && (fabs(a->sm / sm - 1.0) < 1e-6);
    }
    /* nova mezera: po zlomu prvni 2m bodu zadny clen (m = 1: ADEV az s tretim bodem) */
    uint32_t na0 = s_alg_acc[0].na;
    alg_segment(); alg_push(0.0, 1.0); alg_push(0.0, 1.0);
    ok &= (s_alg_acc[0].na == na0);
    alg_push(0.0, 1.0);
    ok &= (s_alg_acc[0].na == na0 + 1u);
    s_alg_busy = 0u;
    return ok;
}

int sdram_log_allan_report(void)
{
    if (s_alg_busy) { printf("ALLAN z logu: zaneprazdneno (bezi jiny vypocet)\n"); return 0; }
    static sdram_log_rec_t buf[128];
    sdram_log_win_t w;
    uint32_t first = 0;
    (void)sdram_log_read_abs(0u, NULL, 0u, &first, &w);
    if (!w.ready || w.head <= w.oldest + 10u) { printf("ALLAN z logu: log je prazdny nebo nebezi (`sdramlog`)\n"); return 0; }
    double gate_s = (double)w.gate_ns * 1e-9;
    if (!(gate_s > 0.0)) { printf("ALLAN z logu: neznama brana\n"); return 0; }
    uint32_t agg = (gate_s >= 1.0) ? 1u : (uint32_t)(1.0 / gate_s + 0.5);
    double t0 = (double)agg * gate_s;                       /* delka bloku = tau0' */

    s_alg_busy = 1u;
    /* pruchod 1: referencni kmitocet = prumer prvnich <= 2048 platnych zaznamu (aby X bylo male, ne ramp) */
    double fsum = 0.0; uint32_t fn = 0u, idx = w.oldest;
    for (uint32_t c = 0; c < 16u && idx < w.head; c++) {
        uint32_t got = sdram_log_read_abs(idx, buf, 128u, &first, &w);
        if (got == 0u) break;
        for (uint32_t k = 0; k < got; k++)
            if ((buf[k].flags & SDRAM_LOG_F_A_VALID) && !(buf[k].flags & (SDRAM_LOG_F_STALE | SDRAM_LOG_F_SPIKE)) && buf[k].fa_uhz != 0u) {
                fsum += (double)buf[k].fa_uhz * 1e-6; fn++;
            }
        idx += got;
    }
    if (fn == 0u) { s_alg_busy = 0u; printf("ALLAN z logu: v logu neni platne mereni kanalu A\n"); return 0; }
    double fref = fsum / (double)fn;

    alg_clear(); alg_segment();
    uint32_t epoch = w.epoch, segs = 1u, gaps = 0u, blocks = 0u, valid = 0u;
    uint32_t bc = 0u, prev_seq = 0u; int have_prev = 0; double bsum = 0.0; int in_seg = 0;
    idx = w.oldest;
    int aborted = 0;
    while (idx < w.head) {
        uint32_t got = sdram_log_read_abs(idx, buf, 128u, &first, &w);
        if (got == 0u || w.epoch != epoch) { aborted = 1; break; }     /* producent prepsal ctena data / nova epocha */
        if (first != idx) { have_prev = 0; bc = 0u; bsum = 0.0; in_seg = 0; alg_segment(); gaps++; }
        for (uint32_t k = 0; k < got; k++) {
            const sdram_log_rec_t *q = &buf[k];
            int ok_rec = (q->flags & SDRAM_LOG_F_A_VALID) && !(q->flags & (SDRAM_LOG_F_STALE | SDRAM_LOG_F_SPIKE)) && q->fa_uhz != 0u;
            int cont = have_prev && (q->seq == prev_seq + 1u);
            if (!ok_rec || (have_prev && !cont)) {                      /* mezera -> novy segment */
                if (in_seg || bc) { gaps++; segs++; }
                bc = 0u; bsum = 0.0; in_seg = 0; alg_segment();
            }
            have_prev = ok_rec; prev_seq = q->seq;
            if (!ok_rec) continue;
            valid++;
            bsum += (double)q->fa_uhz * 1e-6;
            if (++bc < agg) continue;
            double fav = bsum / (double)bc; bc = 0u; bsum = 0.0;
            double y = (fav - fref) / fref;
            if (fabs(y) > 1e-4) { fref = fav; y = 0.0; gaps++; segs++; alg_segment(); in_seg = 0; }   /* jiny signal */
            alg_push(y, t0); in_seg = 1; blocks++;
        }
        idx += got;
    }
    s_alg_busy = 0u;
    printf("ALLAN z .measlog%s: epocha %lu, brana %lu ms, %lu zaznamu (%lu platnych, %lu s), blok %lu zaznamu = tau0' %lu ms, "
           "%lu bloku, %lu mezer/segmentu %lu%s\n",
           w.sim ? " (EMULACE!)" : "", (unsigned long)w.epoch, (unsigned long)(w.gate_ns / 1000000u),
           (unsigned long)(w.head - w.oldest), (unsigned long)valid, (unsigned long)((double)valid * gate_s),
           (unsigned long)agg, (unsigned long)(t0 * 1000.0 + 0.5), (unsigned long)blocks, (unsigned long)gaps,
           (unsigned long)segs, aborted ? "  <== PRERUSENO (log se menil)" : "");
    printf("  tau [s]     n(ADEV)      ADEV        MDEV        HDEV\n");
    for (uint32_t i = 0; i < ALG_NM; i++) {
        const alg_acc_t *a = &s_alg_acc[i];
        if (a->na < 2u) continue;
        char sa[16], sm[16], sh[16];
        alg_sci(sa, sizeof sa, alg_dev(a, ALG_M[i], t0, 0));
        alg_sci(sm, sizeof sm, alg_dev(a, ALG_M[i], t0, 1));
        alg_sci(sh, sizeof sh, alg_dev(a, ALG_M[i], t0, 2));
        double tau = (double)ALG_M[i] * t0;
        printf("  %5lu.%03lu  %9lu   %-10s  %-10s  %-10s\n", (unsigned long)tau, (unsigned long)((tau - floor(tau)) * 1000.0 + 0.5),
               (unsigned long)a->na, sa, sm, sh);
        osDelay(1);
    }
    printf("  (overlapping s krokem tau0'; cleny jsou korelovane -> nejistota klesa pomaleji nez 1/sqrt(n); cf. `meas`/okno ALLAN = pyramida)\n");
    return 1;
}

int sdram_log_selftest(void)
{
    uint32_t a;
    if (!alg_selftest()) return 0;   /* V5: proudovy overlapping Allan == primy vypocet */
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
    /* Okno pro cteni od kurzoru (`sdram_log_read_abs`): pred pretocenim zacina nulou, po nem
     * se posouva s hlavou o kapacitu (nejstarsi dostupny = head - CAP). */
    uint32_t lo;
    win_of(0u, &lo);                       if (lo != 0u) return 0;
    win_of(SDRAM_LOG_CAP, &lo);            if (lo != 0u) return 0;
    win_of(SDRAM_LOG_CAP + 7u, &lo);       if (lo != 7u) return 0;
    /* F-0237: hranice platnosti — zaznam `a` je neplatny uz pri hlave a+CAP (jeho slot se prave zapisuje). */
    if (!rec_still_valid(100u, 100u - 1u))                     return 0;   /* nejnovejsi */
    if (!rec_still_valid(SDRAM_LOG_CAP + 6u, 7u))              return 0;   /* nejstarsi dostupny */
    if (rec_still_valid(SDRAM_LOG_CAP + 7u, 7u))               return 0;   /* jeho slot se prave prepisuje */
    if (rec_still_valid(SDRAM_LOG_CAP + 8u, 7u))               return 0;   /* uz prepsany */
    /* F-0238: cislo epochy nikdy 0 (= "klient nezna") a zustava pod 2^30 (CM4 ho cte pres atol). */
    if (epoch_step(0u) != 1u || epoch_step(41u) != 42u)        return 0;
    if (epoch_step(0x3FFFFFFFu) != 1u)                         return 0;   /* preteceni masky -> 1, ne 0 */
    if (epoch_step(0xFFFFFFFFu) != 1u)                         return 0;   /* semeno z CYCCNT mimo rozsah */
    if (epoch_step(0x9E3779B9u) >= 0x40000000u)                return 0;
    return 1;
}
