/**
 * @file    membench.c
 * @brief   Viz membench.h.
 */
#include "membench.h"
#include "w25q.h"
#include "w25q_map.h"
#include "cmsis_os2.h"         /* osDelay, osMutexAcquire — QSPI zamek + ustupovani */
#include "freertos_shared.h"   /* qspiMutexHandle */
#include "ipc_shared.h"        /* g_ipc.cm4.heartbeat — presne prirazeni padu CM4 (jen CTENI) */
#include "stm32h7xx_hal.h"
#include <string.h>
#include <stdio.h>

/* ── Kde se testuje ──────────────────────────────────────────────────────────
 * ⚠️ Kazdy pametove mapovany cil MUSI byt doložitelne VOLNY (viz komentar u
 * jednotlivych adres). Velikosti jsou zvolene tak, aby jeden nepreruseny prubeh
 * trval radove desitky ms — UartTask sice neni hlidany watchdogem, ale bezi na
 * priorite Normal, takze by dlouhym blokem vyhladovel UiTask (BelowNormal) a ten
 * uz hlidany JE (`watchdog_kick_ui`). Mezi vzory a cili se navic ustupuje
 * scheduleru (`osDelay`). */

/* DTCM (0x20000000, 128 kB). Linker sem NIC neumistuje (`_estack` je v RAM_D1,
 * FreeRTOS haldy jsou v .bss = RAM_D1) — overeno v mapfile: sekce DTCMRAM je
 * prazdna. TCM se z principu NEcachuje -> zadna cache maintenance. */
#define DTCM_TEST_ADDR   0x20000000u
#define DTCM_TEST_SIZE   (64u * 1024u)

/* SRAM1 v domene D2 (0x30000000, 128 kB). Podle CM7 linkeru sem CM7 nic nelinkuje
 * (D2 split: SRAM1 = CM7 diagnostika, SRAM2+3 = CM4/ETH). Pouziva ji jen UART
 * `ram write/read` na 0x30001000, coz je diagnostika na vyzadani.
 * ⚠️ HISTORIE (2026-08-23): tenhle cil SHAZOVAL CM4. Ne kvuli chybe v testu — SRAM1
 * proste NEBYLA volna: `lwipopts.h` mela `LWIP_RAM_HEAP_POINTER (0x30004000)`
 * prevzatou z ST prikladu pro jednojadrovy H7, takze halda lwIP lezela uprostred
 * teto oblasti. Benchmark ji prepsal -> CM4 spadla a s vypnutym IWDG2 uz nenabehla.
 * Opraveno v `lwipopts.h` (halda je ted v `.bss` CM4 = SRAM2); tim se SRAM1 stala
 * skutecne volnou. ⚠️ Tyz problem mel i UART `ram write` — jen si toho nikdo nevsiml,
 * protoze se nespoustel za behu ETH. */
#define SRAM1_TEST_ADDR  0x30001000u
#define SRAM1_TEST_SIZE  (64u * 1024u)

/* ⚠️ SRAM4 / D3 (0x38000000) se netestuje.
 * Do 2026-08-23 se testovala jeji „volna" horni polovina (0x38008000, 16 kB, nad
 * `sizeof(ipc_shared_t)`). Odebrana proto, ze SRAM4 je pamet, ve ktere ZIJE
 * MEZIJADROVE SPOJENI (IPC snapshot + cmd/resp ringy) — hnat do ni sekundy
 * provozu je proti pravidlu z hlavicky modulu („testuje se VYHRADNE pamet, kterou
 * nikdo jiny nepouziva"); argument „horni polovina je volna" plati o ADRESACH, ne
 * o sbernici. Prinos 16 kB je proti tomu riziku nulovy.
 * ⚠️⚠️ ALE POZOR NA PRICINU: odebrani D3 padani CM4 **NEVYRESILO** — pri dalsim behu
 * spadla znovu. Puvodni domnenka, ze za to muze D3, byla tedy MYLNA a nesmi se
 * citovat jako fakt. Skutecna pricina se hleda dal (viz `cm4_hb` nize). */

/* SDRAM scratch (0xC0400000) = MPU region 1, tentyz blok, ktery pouziva UART
 * `sdram write/read` a screenshot. MIMO triple-buffer region 0 (FB0/FB1/FB2
 * @0xC0000000) i mimo linker sekci .sdram (@0xC0800000). Cacheable WBWA ->
 * cache maintenance je POVINNA, jinak by verify cetl z cache a chybu pameti
 * vubec nevidel.
 * ⚠️ ZMENSENO 4 MB -> 512 kB (2026-08-23, na zaklade HW behu): pri 4 MB se
 * behem testu ROZBIJELO ZOBRAZENI. Neni to prepisem framebufferu (ten lezi v
 * region 0 a kontroluje se, viz `sdram_safety_check`), ale PROPUSTNOSTI FMC:
 * LTDC musi z teze SDRAM nepretrzite cist 800x480x2 pri ~60 Hz (~46 MB/s) a
 * nekolikasekundovy test mu bere pasmo -> podteceni FIFO a rozsypany obraz.
 * 512 kB = ~0,4 s celkoveho provozu misto ~3 s, a je to i pod zmerenou hranici
 * prekryvu adres (viz nize), takze test nehlasi falesne chyby. */
#define SDRAM_TEST_ADDR  0xC0400000u
#define SDRAM_TEST_SIZE  (512u * 1024u)
#define SDRAM_TOTAL      (32u * 1024u * 1024u)   /* FMC: 9 col + 13 row x 4 banky x 16 bit */
/* Retencni test: maly blok (rychle overeni) drzeny dostatecne dlouho na to, aby
 * se projevil prilis pomaly refresh. 1 s je s rezervou nad bezne specifikovanou
 * dobou obnovy cele matice (64 ms). */
#define RETAIN_TEST_SIZE (256u * 1024u)
#define RETAIN_HOLD_MS   1000u

/* AXI SRAM (RAM_D1) — jediny cil, ktery NEMA volnou oblast (je tam .bss/.data
 * a haldy), takze se testuje vlastni staticky buffer. Zarovnany na 32 B kvuli
 * `SCB_*DCache_by_Addr`. */
#define AXI_TEST_SIZE    (32u * 1024u)
static uint32_t s_axi_buf[AXI_TEST_SIZE / 4u] __attribute__((aligned(32)));

/* Interni FLASH — JEN CTENI (viz membench.h). Cte se zacatek banky 1, tedy
 * skutecny obraz CM7. */
#define IFLASH_TEST_ADDR 0x08000000u
#define IFLASH_TEST_SIZE (256u * 1024u)

/* Velikost bloku, po kterem se ustupuje scheduleru pri pomalych (QSPI) fazich. */
#define QSPI_CHUNK       256u

volatile uint8_t g_membench_req = MEMBENCH_REQ_NONE;
static membench_state_t s_st;

/* Zahazovaci cil cteciho pruchodu. ⚠️ Bez zapisu do `volatile` by GCC celou
 * cteci smycku vyhodil jako mrtvy kod a „rychlost cteni" by vysla nesmyslne
 * vysoka (merilo by se prazdno). Zamerne v souborovem rozsahu — jako lokalni
 * `static` na nej pada -Wunused-but-set-variable. */
static volatile uint32_t s_read_sink;

const membench_state_t *membench_state(void) { return &s_st; }

/* ── Presne prirazeni padu CM4 konkretnimu cili ──────────────────────────────
 * ⚠️ `g_cm4_alive` se pro tohle NEHODI: defaultTask ho odvozuje z okna ~3 s, takze
 * smrt CM4 se v nem projevi az o nekolik sekund pozdeji — a padne to na cil, ktery
 * zrovna bezi (typicky ten posledni a nejdelsi, W25Q s erase). Presne tak vzniklo
 * mylne obvineni W25Q pri behu 2026-08-23.
 * Tady se proto cte SYROVY citac heartbeatu, ktery CM4 zvedа 5x/s. */
static uint32_t cm4_hb(void)
{
    return (g_ipc.cm4.magic == IPC_MAGIC) ? g_ipc.cm4.heartbeat : 0u;
}

/* Ceka az ~400 ms, jestli heartbeat povyroste. @return 1 = CM4 zije.
 * Kdyz cil trval dele nez tep (200 ms), vraci se hned — cena je pak nulova. */
static int cm4_beats(uint32_t hb_before)
{
    for (int k = 0; k < 40; k++) {
        if (cm4_hb() != hb_before) return 1;
        osDelay(10);
    }
    return 0;
}

/* ── Vzory ───────────────────────────────────────────────────────────────────
 * Kazdy chyta jinou tridu vady, proto jich je vic nez jeden:
 *   ZEROS/ONES   — trvale zaseknuty bit (stuck-at-1 / stuck-at-0).
 *   CHECKER      — zkrat mezi sousednimi datovymi linkami (55/AA se lisi v KAZDEM bitu).
 *   ADDR         — „adresa v adrese": chyba ADRESNIH linek. Bez nej by se dva
 *                  ruzne adresovane, ale shodne zapsane bloky tvarily jako OK.
 *   PRNG         — data-zavisle jevy (crosstalk pri prepinani mnoha linek naraz).
 * ⚠️ Vzor MUSI byt CISTA FUNKCE indexu — verify si hodnotu dopocita znovu, takze
 * se nikde nedrzi kopie ocekavanych dat (jinak bychom potrebovali druhou stejne
 * velkou pamet, a ta by mohla byt vadna taky). */
typedef enum { PAT_ZEROS = 0, PAT_ONES, PAT_CHECKER, PAT_ADDR, PAT_PRNG, PAT_N } pat_t;
_Static_assert((unsigned)PAT_N == MEMBENCH_PAT_N, "pat_err[] neodpovida poctu vzoru");

const char *const membench_pat_name[MEMBENCH_PAT_N] =
    { "0x00", "0xFF", "55/AA", "adresa", "nahodny" };
#define PAT_NAME membench_pat_name

static uint32_t pat_word(pat_t p, uint32_t idx)
{
    switch (p) {
        case PAT_ZEROS:   return 0u;
        case PAT_ONES:    return 0xFFFFFFFFu;
        case PAT_CHECKER: return (idx & 1u) ? 0xAAAAAAAAu : 0x55555555u;
        case PAT_ADDR:    return idx;
        default: {
            /* xorshift nad indexem — deterministicke a bez stavu (viz vyse). */
            uint32_t x = idx * 2654435761u + 0x9E3779B9u;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            return x;
        }
    }
}

static uint32_t popcount32(uint32_t v)
{
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    v = (v + (v >> 4)) & 0x0F0F0F0Fu;
    return (v * 0x01010101u) >> 24;
}

/* Zapocita jednu neshodu do vysledku. Oddelene od smycek, aby verify vypadal
 * stejne pro pamet i pro QSPI buffer. `pi` = index vzoru (pro `pat_err`). */
static void note_error(membench_result_t *r, int pi, uint32_t addr,
                       uint32_t got, uint32_t want)
{
    uint32_t diff = got ^ want;
    if (r->bit_errors == 0u) {
        r->first_err_addr = addr;
        r->first_err_got  = got;      /* skutecne precteno — nejcennejsi udaj: */
        r->first_err_want = want;     /* nahodne bity vs. cizi platna hodnota */
    }
    r->bit_errors  += popcount32(diff);
    r->err_bitmask |= diff;
    if (pi >= 0 && pi < (int)PAT_N) r->pat_err[pi] += popcount32(diff);
}

/* ── Mereni casu ─────────────────────────────────────────────────────────────
 * DWT CYCCNT (uz zapnuty v `bootled_init`) ma rozliseni jednoho taktu, ale pri
 * 480 MHz pretece za ~8,9 s. Vsechny RAM faze jsou radove ms, takze je to
 * bezpecne; QSPI (erase v stovkach ms) se meri v `HAL_GetTick`. */
static uint32_t kbs_from_cycles(uint32_t bytes, uint32_t cycles)
{
    if (cycles == 0u) return 0u;
    /* kB/s = bytes/1024 / (cycles/f) = bytes*f / (1024*cycles). Poradi operaci je
     * zvolene tak, aby se to veslo do 64 bitu a neztratilo presnost delenim. */
    uint64_t v = (uint64_t)bytes * (uint64_t)SystemCoreClock;
    return (uint32_t)(v / ((uint64_t)cycles * 1024u));
}
static uint32_t kbs_from_ms(uint32_t bytes, uint32_t ms)
{
    if (ms == 0u) ms = 1u;                 /* pod rozlisenim tiku -> nelzi o nekonecnu */
    return (uint32_t)(((uint64_t)bytes * 1000u) / ((uint64_t)ms * 1024u));
}

/* ── Cache ───────────────────────────────────────────────────────────────────
 * ⚠️ KLICOVE PRO SPRAVNOST, ne pro rychlost: bez `clean` po zapisu by data
 * zustala v D-cache a do pameti se vubec nedostala; bez `invalidate` pred ctenim
 * by verify precetl zpatky prave tu cache. Vada pameti by se pak NIKDY
 * neprojevila a benchmark by hlasil OK na rozbite RAM.
 * Adresy/delky se zarovnavaji na 32 B (velikost cache line) — CMSIS to vyzaduje. */
static void cache_clean(const void *p, uint32_t n)
{
    uint32_t a = (uint32_t)p & ~31u;
    uint32_t len = (n + ((uint32_t)p - a) + 31u) & ~31u;
    SCB_CleanDCache_by_Addr((uint32_t *)a, (int32_t)len);
}
static void cache_invalidate(const void *p, uint32_t n)
{
    uint32_t a = (uint32_t)p & ~31u;
    uint32_t len = (n + ((uint32_t)p - a) + 31u) & ~31u;
    SCB_InvalidateDCache_by_Addr((uint32_t *)a, (int32_t)len);
}

/* ── Test jednoho pametove mapovaneho cile ───────────────────────────────────
 * `cached` = 1 pro oblasti s D-cache (AXI SRAM, SRAM1, SDRAM); 0 pro TCM a pro
 * MPU non-cacheable D3. */
/* Blok, po kterem se ustupuje scheduleru pri dlouhych (netimovanych) pruchodech.
 * ⚠️ Bez toho drzel 4 MB pruchod UartTask (Normal) stovky ms v kuse a vyhladovel
 * UiTask (BelowNormal) -> po benchmarku padalo `touch: I2C4 nereaguje ... recovery`
 * (nahlaseno pri HW testu 2026-08-23). 64 kB je kompromis: ~0,5 ms prace na blok. */
#define YIELD_WORDS  (8u * 1024u)       /* 32 kB — jemneji kvuli pasmu pro LTDC */

/* Blok pro MERENI RYCHLOSTI. Zamerne PEVNY (nezavisi na velikosti cile): musi
 * probehnout v jednom kuse bez `osDelay`, jinak by se do casu zapocital spanek.
 * ⚠️ U malych cilu (DTCM 64 kB, AXI buffer 32 kB) se meri cely blok, takze cislo
 * je vic ovlivnene cache nez u velkych — mezi pametmi to neni uplne fer srovnani. */
#define SPEED_WORDS  (64u * 1024u)      /* 256 kB */

/* ⚠️ ROZBALENI SMYCKY — bez nej benchmark NEMERIL PAMET, ale sam sebe.
 * Pri jednom `p[i]` na iteraci vysly DTCM (zero-wait-state TCM) i SRAM1 (pomalejsi
 * D2 sbernice) shodne ~187 MB/s = ~10 taktu na 4B slovo (zmereno 2026-08-23). Takova
 * shoda je fyzikalne nemozna — uzkym hrdlem byla rezie smycky (inkrement, porovnani,
 * skok) kolem jedineho `volatile` pristupu, ne pamet. Rozbaleni po 8 tu rezii rozpusti
 * a cisla zacnou jednotlive pameti rozlisovat.
 * ⚠️ `volatile` MUSI zustat: bez nej by prekladac zapisy preskladal nebo vyhodil. */
#define SPEED_UNROLL 8u

/* ⚠️ NEJLEPSI Z N PRUCHODU, ne jediné mereni. SDRAM sdili FMC s LTDC, ktery z teze
 * pameti nepretrzite cte framebuffer (~46 MB/s) — kolik pasma zbyde na nas, zavisi
 * na tom, co je zrovna na obrazovce a kam padne merici okno. Projevilo se to
 * neopakovatelnosti: zapis do SDRAM vysel ve dvou po sobe jdoucich bezich
 * 38 a 25 MB/s, PRESTOZE zapisova smycka byla v obou identicka (2026-08-23).
 * Ruseni muze mereni jen ZPOMALIT, takze minimum z nekolika pruchodu je z principu
 * blizsi skutecne propustnosti nez prumer nebo jediny vzorek. */
#define SPEED_PASSES 3u

/* ── Test ADRESNICH linek ────────────────────────────────────────────────────
 * Vzory dat odhali vadne BUNKY, ale ne vadnou adresaci: kdyby nefungovala jedna
 * adresni linka, dva ruzne offsety by mirily do teze bunky a data by se tise
 * prepisovala. Klasicky test zapise unikatni hodnotu na kazdou MOCNINU DVOU
 * (= prave jedna adresni linka v jednicce) a na offset 0; kdyz se pak nektera
 * hodnota lisi, ta linka je vadna nebo se region prekryva sam se sebou.
 * @return vzdalenost prekryvu v BAJTECH, 0 = adresni linky OK. */
static uint32_t addr_lines_test(volatile uint32_t *p, uint32_t words,
                                 void *base, uint32_t size, int cached)
{
    const uint32_t BASE_MARK = 0xFFFFFFFFu;   /* odlisna od kazdeho offsetu */
    p[0] = BASE_MARK;
    for (uint32_t o = 1u; o < words; o <<= 1) p[o] = o;
    __DSB();
    if (cached) { cache_clean(base, size); cache_invalidate(base, size); }

    /* Bunku 0 nekdo prepsal. ⚠️ Na kazdy offset `o` se zapisovalo prave `o`, takze
     * to, CO v bunce 0 zbylo, JE ten offset, ktery se do ni trefil — vypiseme ho
     * rovnou misto neurciteho „nekde je kolize".
     * ⚠️ ALE OVERIT, ze je to VUBEC platny offset (mocnina dvou uvnitr bloku):
     * kdyz bunku prepsal nekdo jiny nez nas test (cizi zapis, rozjete druhe jadro),
     * je tam libovolne cislo a `*4` by z nej udelalo nesmyslnou — nebo pretecenou —
     * „vzdalenost prekryvu". Radeji priznat, ze vzdalenost nezname. */
    if (p[0] != BASE_MARK) {
        uint32_t v = p[0];
        if (v != 0u && (v & (v - 1u)) == 0u && v < words) return v * 4u;
        return MEMBENCH_ALIAS_UNKNOWN;
    }
    for (uint32_t o = 1u; o < words; o <<= 1)
        if (p[o] != o) return o * 4u;
    return 0u;
}

/* ── Retencni test (JEN pro DRAM) ────────────────────────────────────────────
 * SRAM drzi obsah, dokud je napajena; DRAM ne — kazda bunka je kondenzator a
 * musi se periodicky obnovovat (FMC to dela sam, rychlosti danou REFRESH_COUNT).
 * Kdyz je obnovovani prilis pomale, data se rozpadaji az PO NEJAKE DOBE — coz
 * bezny „zapis a hned over" test NEODHALI. Tenhle zapise blok, `hold_ms` pocka
 * a teprve pak overi. @return pocet chybnych bitu. */
static uint32_t retention_test(membench_result_t *r, void *base, uint32_t size,
                                uint32_t hold_ms, int cached)
{
    volatile uint32_t *p = (volatile uint32_t *)base;
    uint32_t words = size / 4u;
    for (uint32_t i = 0; i < words; i++) p[i] = pat_word(PAT_PRNG, i);
    __DSB();
    /* Clean PRED cekanim: data musi byt SKUTECNE v pameti, ne v cache — jinak by
     * test meril retenci cache, ne DRAM. */
    if (cached) cache_clean(base, size);
    osDelay(hold_ms);
    if (cached) cache_invalidate(base, size);   /* az ted, aby se cetlo z pameti */

    /* ⚠️ Pres `note_error`, ne vlastnim scitanim: jinak by se retencni chyby
     * nedostaly do `pat_err[]` a soucet „podle vzoru" by nesedel na celkovy pocet
     * chybnych bitu (retence pouziva vzor PRNG, takze tam patri). Volajici uz proto
     * NESMI `retain_err` k `bit_errors` pricitat znovu. */
    uint32_t before = r->bit_errors;
    for (uint32_t i = 0; i < words; i++) {
        uint32_t got = p[i], want = pat_word(PAT_PRNG, i);
        if (got != want) note_error(r, PAT_PRNG, (uint32_t)(uintptr_t)&p[i], got, want);
    }
    return r->bit_errors - before;
}

/* ── Bezpecnostni pojistka pro SDRAM ─────────────────────────────────────────
 * ⚠️ PROC VUBEC: prvni HW beh ukazal, ze se adresy v teto SDRAM OPAKUJI (dve
 * ruzne adresy = tataz bunka, viz `alias_off` a poznamka v CLAUDE.md). Kdyz se
 * adresy prekryvaji, „testuju jen vyhrazenou oblast" prestava platit — zapis do
 * scratche muze skoncit ve framebufferu. Staticky se to zaridit neda, protoze
 * zavisi na HW; proto se to PRED kazdym testem ZMERI.
 *
 * Sonda je JEDNOSLOVNA a REVERZIBILNI: puvodni obsah obou adres se vrati, takze
 * i kdyz sondujeme primo framebuffer, zustane nedotceny (v nejhorsim jeden pixel
 * na jeden snimek).
 * @return 1 = `a` a `b` jsou tataz fyzicka bunka. */
static int cells_alias(volatile uint32_t *a, volatile uint32_t *b)
{
    const uint32_t M1 = 0x5A5A1234u, M2 = 0xA5A54321u;
    cache_invalidate((void *)a, 4); cache_invalidate((void *)b, 4);
    uint32_t sav_a = *a, sav_b = *b;

    *a = M1; __DSB(); cache_clean((void *)a, 4);
    *b = M2; __DSB(); cache_clean((void *)b, 4);
    cache_invalidate((void *)a, 4);
    int alias = (*a == M2);          /* zapis do `b` prepsal `a` -> tataz bunka */

    /* Uklid. Pri aliasu je `sav_a == sav_b` (cetlo se z teze bunky), takze na
     * poradi zapisu nezalezi a obsah se obnovi spravne tak jako tak. */
    *a = sav_a; __DSB(); cache_clean((void *)a, 4);
    *b = sav_b; __DSB(); cache_clean((void *)b, 4);
    return alias;
}

/* Oblasti SDRAM, ktere se NESMI dotknout — kdyz se s nimi testovaci blok
 * prekryva, test se PRESKOCI. Adresy podle mapy SDRAM v CLAUDE.md. */
static const uint32_t SDRAM_PROTECTED[] = {
    0xC0000000u,   /* FB0 — framebuffer, ze ktereho prave scanuje LTDC */
    0xC0100000u,   /* FB1 */
    0xC0200000u,   /* FB2 */
    0xC0300000u,   /* off-screen canvas pool */
    0xC0800000u,   /* linker sekce .sdram — bg_cache (predrenderovane pozadi), glow */
};

/* Zmeri, po jake vzdalenosti se adresy opakuji. Skenuje jen UVNITR region 1
 * (64 kB..2 MB), aby sonda nesahala do sekce `.sdram`.
 * @return vzdalenost v bajtech, 0 = do 2 MB se nic neopakuje. */
static uint32_t sdram_alias_span(volatile uint32_t *base)
{
    for (uint32_t off = 64u * 1024u; off <= 2u * 1024u * 1024u; off <<= 1)
        if (cells_alias(base, base + off / 4u)) return off;
    return 0u;
}

/* Overi, ze je bezpecne testovat `size` bajtu od `SDRAM_TEST_ADDR`.
 * @return 1 = bezpecne (a `*out_size` je pripadne zmenseny), 0 = NETESTOVAT. */
static int sdram_safety_check(membench_result_t *r, uint32_t *out_size)
{
    volatile uint32_t *base = (volatile uint32_t *)SDRAM_TEST_ADDR;

    for (unsigned i = 0; i < sizeof SDRAM_PROTECTED / sizeof SDRAM_PROTECTED[0]; i++) {
        if (cells_alias(base, (volatile uint32_t *)SDRAM_PROTECTED[i])) {
            snprintf(r->msg, sizeof r->msg, "kolize s 0x%08lX!",
                     (unsigned long)SDRAM_PROTECTED[i]);
            r->skipped = 1;
            return 0;               /* radeji netestovat nez rozbit obraz */
        }
    }

    /* Prekryv uvnitr testovaneho bloku by delal falesne „chybne bity" (dva ruzne
     * indexy by si prepisovaly hodnotu), takze blok zkratime pod tu hranici. */
    uint32_t span = sdram_alias_span(base);
    r->alias_off = span;            /* v bajtech; 0 = do 2 MB se nic neopakuje */
    if (span && *out_size > span) *out_size = span;

    /* ── Krizova kontrola CHRANENYCH oblasti MEZI SEBOU ──────────────────────
     * ⚠️ Kdyz se adresy opakuji, nejde jen o to, jestli je bezpecne testovat —
     * dulezitejsi otazka je, jestli si tim uz dnes NELEZOU FRAMEBUFFERY navzajem.
     * `FB0` (0xC0000000) a `FB2` (0xC0200000) se lisi PRAVE JEN v HADDR[21], tedy
     * v bitu, ktery pri prekryvu po 2 MB vypada jako nefunkcni. Kdyby sdilely
     * pamet, triple buffering by byl fakticky double a projevovalo by se to
     * blikanim/trhanim, ktere by nikdo nespojoval s pameti.
     * Sonda je stejne reverzibilni jako vyse (jedno slovo, obsah se vraci). */
    if (span) {
        volatile uint32_t *fb0 = (volatile uint32_t *)0xC0000000u;
        volatile uint32_t *fb2 = (volatile uint32_t *)0xC0200000u;
        volatile uint32_t *fb1 = (volatile uint32_t *)0xC0100000u;
        volatile uint32_t *can = (volatile uint32_t *)0xC0300000u;
        r->fb_alias = (uint8_t)((cells_alias(fb0, fb2) ? 1u : 0u)
                              | (cells_alias(fb1, can) ? 2u : 0u));
    }
    return 1;
}

static void bench_ram(membench_result_t *r, void *base, uint32_t size, int cached)
{
    volatile uint32_t *p = (volatile uint32_t *)base;
    uint32_t words = size / 4u;
    uint32_t sp_words = (words < SPEED_WORDS) ? words : SPEED_WORDS;
    uint32_t sp_bytes = sp_words * 4u;

    /* --- 1) Adresni linky (nejdriv — je to levne a rozlisi to uplne jinou vadu).
     *        ⚠️ Nuluj jen kdyz se neco naslo: u SDRAM uz hodnotu nastavil
     *        `sdram_safety_check` (zmeril prekryv i MIMO zkraceny blok) a bylo by
     *        skoda ji prepsat nulou jen proto, ze uvnitr bloku uz kolize neni. */
    uint32_t al = addr_lines_test(p, words, base, size, cached);
    if (al) r->alias_off = al;

    /* --- 2) Rychlost na PEVNEM bloku, v jednom kuse (bez `osDelay`, jinak by se
     *        do casu zapocital spanek). Vzorem je index, tedy skutecne se menici
     *        data — konstanta by u SDRAM mohla vyjit neprirozene rychle. Cteni je
     *        SAMOSTATNY pruchod se souctem do `volatile`; kdyby se merilo rovnou
     *        pri porovnavani, cislo by neslo rychlost pameti, ale rychlost
     *        porovnavaci smycky. */
    /* Vlastni blok, aby merici `w` nestinilo `i` v pozdejsich vzorovych smyckach. */
    {
        uint32_t best_w = 0xFFFFFFFFu, best_r = 0xFFFFFFFFu;
        for (unsigned pass = 0; pass < SPEED_PASSES; pass++) {
        /* Kazdy pruchod zacina se stejne prazdnou cache -> pasy jsou srovnatelne. */
        if (cached) cache_invalidate(base, sp_bytes);
        uint32_t w, t0 = DWT->CYCCNT;
        for (w = 0; w + SPEED_UNROLL <= sp_words; w += SPEED_UNROLL) {
            p[w     ] = w;      p[w + 1u] = w + 1u; p[w + 2u] = w + 2u; p[w + 3u] = w + 3u;
            p[w + 4u] = w + 4u; p[w + 5u] = w + 5u; p[w + 6u] = w + 6u; p[w + 7u] = w + 7u;
        }
        for (; w < sp_words; w++) p[w] = w;
        __DSB();
        { uint32_t dt = DWT->CYCCNT - t0; if (dt < best_w) best_w = dt; }
        if (cached) { cache_clean(base, sp_bytes); cache_invalidate(base, sp_bytes); }

        uint32_t acc = 0;
        t0 = DWT->CYCCNT;
        /* ⚠️ JEDEN SOUCET na ctveřici (`acc += p[w] + p[w+1] + p[w+2] + p[w+3]`),
         * NE osm samostatnych `acc += p[w+n];`. V Debug buildu (-O0) zije `acc` na
         * zasobniku, takze kazdy samostatny prikaz znamena cely round-trip
         * nacti-pricti-uloz; souctovy tvar nacte ctyri hodnoty do registru, secte je
         * tam a `acc` sahne jen jednou.
         * ⚠️ Zmereno, ne odhadnuto — a vyvratilo to moji puvodni uvahu (myslel jsem si
         * pravy opak): samostatne prikazy srazily cteni DTCM 263 -> 202 MB/s, AXI
         * 227 -> 182, SRAM1 214 -> 173. Nemenit zpatky bez zmereni. */
        for (w = 0; w + SPEED_UNROLL <= sp_words; w += SPEED_UNROLL) {
            acc += p[w     ] + p[w + 1u] + p[w + 2u] + p[w + 3u];
            acc += p[w + 4u] + p[w + 5u] + p[w + 6u] + p[w + 7u];
        }
        for (; w < sp_words; w++) acc += p[w];
        { uint32_t dt = DWT->CYCCNT - t0; if (dt < best_r) best_r = dt; }
        s_read_sink = acc;
        }
        r->write_kbs = kbs_from_cycles(sp_bytes, best_w);
        r->read_kbs  = kbs_from_cycles(sp_bytes, best_r);
    }

    /* --- 3) Chybne bity: vsechny vzory, zapis CELEHO bloku + zpetne overeni.
     *        Zamerne „zapis vse, pak over vse" (ne po blocich): jen tak je mezi
     *        zapisem a ctenim dost casu, aby se projevil rozpad obsahu. */
    for (int pi = 0; pi < (int)PAT_N; pi++) {
        snprintf(s_st.phase, sizeof s_st.phase, "%s: vzor %s", r->name, PAT_NAME[pi]);
        for (uint32_t i = 0; i < words; i++) {
            p[i] = pat_word((pat_t)pi, i);
            if ((i & (YIELD_WORDS - 1u)) == (YIELD_WORDS - 1u)) osDelay(1);
        }
        __DSB();
        if (cached) { cache_clean(base, size); cache_invalidate(base, size); }
        for (uint32_t i = 0; i < words; i++) {
            uint32_t got = p[i], want = pat_word((pat_t)pi, i);
            if (got != want) note_error(r, pi, (uint32_t)(uintptr_t)&p[i], got, want);
            if ((i & (YIELD_WORDS - 1u)) == (YIELD_WORDS - 1u)) osDelay(1);
        }
    }
    r->tested = 1;
}

/* ── Interni FLASH: jen cteni ────────────────────────────────────────────────
 * Zapis/erase do banky, ze ktere se soucasne vykonava kod, zastavi sbernici, a
 * druha banka patri CM4 -> zapis se ZAMERNE nedela vubec.
 * Misto hledani chybnych bitu se obraz precte DVAKRAT (s invalidovanou cache
 * mezi tim) a soucty se porovnaji. ⚠️ To odhali NESTABILNI cteni, ne trvale
 * spatny bit — ten by dal pokazde stejny (chybny) soucet. Trvalou vadu na H7
 * stejne zachyti ECC flash pameti, ktera hlasi chybu sama. */
static uint32_t flash_sum(const uint32_t *p, uint32_t words)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < words; i++) s = (s << 1) ^ (s >> 31) ^ p[i];   /* rotace + XOR */
    return s;
}
static void bench_iflash(membench_result_t *r)
{
    const uint32_t *p = (const uint32_t *)IFLASH_TEST_ADDR;
    uint32_t words = r->size_b / 4u;

    cache_invalidate(p, r->size_b);
    uint32_t t0 = DWT->CYCCNT;
    uint32_t a = flash_sum(p, words);
    r->read_kbs = kbs_from_cycles(r->size_b, DWT->CYCCNT - t0);

    osDelay(1);
    cache_invalidate(p, r->size_b);
    uint32_t b = flash_sum(p, words);

    r->tested = 1;
    if (a == b) snprintf(r->msg, sizeof r->msg, "cteni stabilni");
    else      { snprintf(r->msg, sizeof r->msg, "CTENI NESTABILNI!"); r->bit_errors = 1; }
}

/* ── W25Q (externi QSPI) ─────────────────────────────────────────────────────
 * ⚠️ DESTRUKTIVNI pro `W25Q_BENCH_BASE` (vyhrazeny scratch, viz w25q_map.h).
 * Jen 2 vzory: kazdy stoji cely erase cyklus (8 sektoru x 50-400 ms), pet vzoru
 * jako u RAM by test protahlo na desitky sekund. ADDR chyta adresni linky,
 * CHECKER datove — to jsou u externi sbernice ty dve tridy, o ktere jde.
 * ⚠️ NOR flash umi jen 1->0, takze pred KAZDYM vzorem musi byt erase; bez nej
 * by druhy zapis dal soucin obou vzoru a test by hlasil neexistujici chyby. */
static void bench_w25q(membench_result_t *r)
{
    static uint32_t buf[QSPI_CHUNK / 4u];
    const uint32_t size = W25Q_BENCH_SIZE;
    const pat_t pats[2] = { PAT_ADDR, PAT_CHECKER };

    if (osMutexAcquire(qspiMutexHandle, 2000u) != osOK) {
        r->skipped = 1; snprintf(r->msg, sizeof r->msg, "flash obsazena");
        return;
    }
    if (!w25q_init()) {
        osMutexRelease(qspiMutexHandle);
        r->skipped = 1; snprintf(r->msg, sizeof r->msg, "flash neodpovida");
        return;
    }

    uint32_t w_ms = 0, r_ms = 0, w_bytes = 0, r_bytes = 0;
    int fail = 0;

    for (int k = 0; k < 2 && !fail; k++) {
        snprintf(s_st.phase, sizeof s_st.phase, "W25Q: vzor %s", PAT_NAME[pats[k]]);

        for (uint32_t s = 0; s < W25Q_BENCH_SECTORS && !fail; s++)
            if (!w25q_erase_sector(W25Q_BENCH_BASE + s * W25Q_SECTOR_SIZE)) fail = 1;

        uint32_t t0 = HAL_GetTick();
        for (uint32_t off = 0; off < size && !fail; off += QSPI_CHUNK) {
            for (uint32_t i = 0; i < QSPI_CHUNK / 4u; i++)
                buf[i] = pat_word(pats[k], (off / 4u) + i);
            if (!w25q_write(W25Q_BENCH_BASE + off, (const uint8_t *)buf, QSPI_CHUNK)) fail = 1;
        }
        w_ms += HAL_GetTick() - t0; w_bytes += size;

        t0 = HAL_GetTick();
        for (uint32_t off = 0; off < size && !fail; off += QSPI_CHUNK) {
            if (!w25q_read(W25Q_BENCH_BASE + off, (uint8_t *)buf, QSPI_CHUNK)) { fail = 1; break; }
            for (uint32_t i = 0; i < QSPI_CHUNK / 4u; i++) {
                uint32_t want = pat_word(pats[k], (off / 4u) + i);
                if (buf[i] != want)
                    note_error(r, (int)pats[k], W25Q_BENCH_BASE + off + i * 4u, buf[i], want);
            }
        }
        r_ms += HAL_GetTick() - t0; r_bytes += size;
        osDelay(1);
    }
    osMutexRelease(qspiMutexHandle);

    if (fail) { r->skipped = 1; snprintf(r->msg, sizeof r->msg, "chyba SPI prenosu"); return; }
    r->write_kbs = kbs_from_ms(w_bytes, w_ms);
    r->read_kbs  = kbs_from_ms(r_bytes, r_ms);
    r->tested = 1;
}

/* ── Sestaveni tabulky cilu + prubeh ─────────────────────────────────────────── */
static void set_target(membench_result_t *r, const char *name, uint32_t addr,
                       uint32_t size, uint32_t total, uint8_t writable)
{
    memset(r, 0, sizeof *r);
    snprintf(r->name, sizeof r->name, "%s", name);
    r->addr = addr; r->size_b = size; r->total_b = total; r->writable = writable;
}

void membench_run(void)
{
    memset(&s_st, 0, sizeof s_st);
    s_st.running = 1;
    s_st.n = MEMBENCH_TARGETS;

    /* ⚠️ `total` = kapacita CELE pameti, `size` = kolik z ni jde otestovat (tj.
     * kolik neni obsazene). Rozdil je casto radovy — viz komentar u `total_b`. */
    membench_result_t *r = s_st.r;
    set_target(&r[0], "DTCM",      DTCM_TEST_ADDR,  DTCM_TEST_SIZE,  128u*1024u, 1);
    set_target(&r[1], "AXI SRAM",  (uint32_t)(uintptr_t)s_axi_buf,
                                    AXI_TEST_SIZE,                   512u*1024u, 1);
    set_target(&r[2], "SRAM1 D2",  SRAM1_TEST_ADDR, SRAM1_TEST_SIZE, 128u*1024u, 1);
    set_target(&r[3], "SDRAM",     SDRAM_TEST_ADDR, SDRAM_TEST_SIZE, SDRAM_TOTAL, 1);
    /* Bank1 (1 MB) = obraz CM7; bank2 patri CM4 a odsud se necte. */
    set_target(&r[4], "FLASH bank1", IFLASH_TEST_ADDR, IFLASH_TEST_SIZE, 1024u*1024u, 0);
    set_target(&r[5], "W25Q QSPI", W25Q_BENCH_BASE, W25Q_BENCH_SIZE, W25Q_SIZE_BYTES, 1);

    /* ⚠️ D2 SRAM ma VLASTNI hodinovy signal (AHB2ENR), ktery CubeMX pro CM7 nikde
     * nezapina — bez nej by cely region cetl same nuly a benchmark by hlasil
     * „vadnou pamet" na zdrave desce. Enable je idempotentni.
     * ⚠️ ZAMERNE varianta **C1** (`RCC_C1->AHB2ENR`), ne spolecna `RCC->AHB2ENR`:
     * na dvoujadrovem H7 ma kazde jadro vlastni sadu povolovacich bitu a nas
     * zajima jen prideleni pro CM7. Sahat na spolecny registr, kdyz zaroven resime
     * padani CM4, je zbytecne riziko. */
    __HAL_RCC_C1_D2SRAM1_CLK_ENABLE();

    uint8_t cm4_dead = 0;        /* aby se vinikem oznacil jen PRVNI postizeny cil */

    for (uint8_t i = 0; i < MEMBENCH_TARGETS; i++) {
        s_st.cur = i;
        s_st.prog_pct = (uint32_t)i * 100u / MEMBENCH_TARGETS;
        snprintf(s_st.phase, sizeof s_st.phase, "%s...", r[i].name);
        /* ⚠️ Stav heartbeatu CM4 PRED kazdym cilem — viz `cm4_hb`. Kdyz benchmark
         * druhe jadro shodi, samotne „stall:CM4" v logu nerekne KTERY cil to udelal. */
        uint32_t hb_before = cm4_hb();

        switch (i) {
            case 0: bench_ram(&r[0], (void *)DTCM_TEST_ADDR,  DTCM_TEST_SIZE,  0); break;
            case 1: bench_ram(&r[1], s_axi_buf,               AXI_TEST_SIZE,   1); break;
            case 2: bench_ram(&r[2], (void *)SRAM1_TEST_ADDR, SRAM1_TEST_SIZE, 1); break;
            case 3: {
                /* ⚠️ NEJDRIV pojistka, teprve pak zapis — SDRAM sdili cip s
                 * framebuffery a merenim se overuje, ze se testovaci blok s
                 * nicim neprekryva (viz `sdram_safety_check`). */
                uint32_t sz = SDRAM_TEST_SIZE;
                snprintf(s_st.phase, sizeof s_st.phase, "SDRAM: kontrola adres");
                if (!sdram_safety_check(&r[3], &sz)) break;   /* msg + skipped uz nastaveny */
                r[3].size_b = sz;                             /* at tabulka ukaze, co se opravdu testovalo */
                bench_ram(&r[3], (void *)SDRAM_TEST_ADDR, sz, 1);
                /* ⚠️ Retence JEN pro SDRAM — je to jedina DRAM v systemu. U SRAM by
                 * test nemel co odhalit (drzi obsah bez obnovovani) a jen by
                 * prodluzoval beh o sekundu na cil. */
                snprintf(s_st.phase, sizeof s_st.phase, "SDRAM: retence 1 s");
                uint32_t rt = (RETAIN_TEST_SIZE < sz) ? RETAIN_TEST_SIZE : sz;
                r[3].retain_err  = retention_test(&r[3], (void *)SDRAM_TEST_ADDR,
                                                  rt, RETAIN_HOLD_MS, 1);
                r[3].retain_done = 1;   /* `bit_errors` uz zvysil `note_error` uvnitr */
                break;
            }
            case 4: bench_iflash(&r[4]); break;
            default: bench_w25q(&r[5]); break;
        }

        /* ⚠️ Oznac JEN prvni cil, po kterem heartbeat prestal rust — jakmile je CM4
         * mrtva, hlasily by to uz vsechny nasledujici a vinik by se ztratil. */
        if (!cm4_dead && hb_before != 0u && !cm4_beats(hb_before)) {
            r[i].killed_cm4 = 1u;
            cm4_dead = 1u;
        }

        if (r[i].msg[0] == '\0') {
            if (r[i].killed_cm4)       snprintf(r[i].msg, sizeof r[i].msg, "SHODIL CM4!");
            else if (r[i].bit_errors == 0u) snprintf(r[i].msg, sizeof r[i].msg, "OK");
            else snprintf(r[i].msg, sizeof r[i].msg, "%lu chybnych bitu",
                          (unsigned long)r[i].bit_errors);
        }
        s_st.total_bit_errors += r[i].bit_errors;
        osDelay(1);
    }

    s_st.prog_pct  = 100;
    s_st.cur       = 0;
    s_st.done_once = 1;
    s_st.running   = 0;
    snprintf(s_st.phase, sizeof s_st.phase, s_st.total_bit_errors ? "NALEZENY CHYBY" : "hotovo, bez chyb");
}

void membench_service(void)
{
    if (g_membench_req != MEMBENCH_REQ_RUN) return;
    g_membench_req = MEMBENCH_REQ_NONE;
    membench_run();
}

/* ── Selftest (bez pameti a bez HW) ──────────────────────────────────────────
 * Testuje to, co muze tise selhat a pritom vypadat spravne: generatory vzoru
 * (kdyby PAT_ADDR vracel konstantu, adresni chyby by se NIKDY nenasly) a
 * pocitani chybnych bitu. */
int membench_selftest(void)
{
    int ok = 1;

    ok &= (pat_word(PAT_ZEROS, 123) == 0u);
    ok &= (pat_word(PAT_ONES, 123) == 0xFFFFFFFFu);
    ok &= (pat_word(PAT_CHECKER, 0) == 0x55555555u);
    ok &= (pat_word(PAT_CHECKER, 1) == 0xAAAAAAAAu);
    /* ADDR musi byt ruzny pro ruzne indexy — jinak nechytne adresni linky. */
    ok &= (pat_word(PAT_ADDR, 7) == 7u);
    ok &= (pat_word(PAT_ADDR, 8) != pat_word(PAT_ADDR, 7));
    /* PRNG musi byt deterministicky (verify si hodnotu dopocitava znovu). */
    ok &= (pat_word(PAT_PRNG, 42) == pat_word(PAT_PRNG, 42));
    ok &= (pat_word(PAT_PRNG, 42) != pat_word(PAT_PRNG, 43));

    ok &= (popcount32(0u) == 0u);
    ok &= (popcount32(0xFFFFFFFFu) == 32u);
    ok &= (popcount32(0x80000001u) == 2u);

    membench_result_t r; memset(&r, 0, sizeof r);
    note_error(&r, PAT_ADDR, 0x1000u, 0x00u, 0x03u);  /* 2 chybne bity, pozice 0 a 1 */
    ok &= (r.bit_errors == 2u);
    ok &= (r.err_bitmask == 0x03u);
    ok &= (r.first_err_addr == 0x1000u);
    /* Prvni chyba si MUSI zapamatovat i hodnoty — bez nich nejde rozlisit rozpad
     * obsahu od prekryvu adres (viz komentar u first_err_got v membench.h). */
    ok &= (r.first_err_got == 0x00u && r.first_err_want == 0x03u);
    note_error(&r, PAT_ONES, 0x2000u, 0x00u, 0x80u);  /* dalsi bit, adresa se NEsmi prepsat */
    ok &= (r.bit_errors == 3u);
    ok &= (r.err_bitmask == 0x83u);
    ok &= (r.first_err_addr == 0x1000u);
    ok &= (r.first_err_got == 0x00u && r.first_err_want == 0x03u);   /* porad ta PRVNI */
    /* Rozpad po vzorech: 2 bity vzoru „adresa", 1 bit vzoru 0xFF. Kdyby se scitalo
     * do jednoho kose, nesla by odlisit vada adresnich linek od vady bunek. */
    ok &= (r.pat_err[PAT_ADDR] == 2u);
    ok &= (r.pat_err[PAT_ONES] == 1u);
    ok &= (r.pat_err[PAT_ZEROS] == 0u);

    /* Prevody rychlosti: 1024 B za 1 ms = 1000 kB/s. */
    ok &= (kbs_from_ms(1024u, 1u) == 1000u);
    ok &= (kbs_from_cycles(1024u, 0u) == 0u);       /* deleni nulou neprojde */

    return ok;
}
