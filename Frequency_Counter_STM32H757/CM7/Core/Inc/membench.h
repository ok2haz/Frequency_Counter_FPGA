/**
 * @file    membench.h
 * @brief   Benchmark vsech pameti (interni i externi): rychlost zapisu/cteni
 *          + hledani chybnych bitu. Okno PAMETI (s_view=43) a UART `membench`.
 *
 * PROC: „pamet funguje" se dosud vedelo jen nepriamo (displej kresli, log se
 * zapise). Tenhle modul to zmeri a hlavne rekne, KTERE bity selhavaji — maska
 * bitovych pozic ukaze rovnou na konkretni datovou linku, coz je u SDRAM/QSPI
 * na vlastni desce ta informace, kvuli ktere se to dela.
 *
 * ⚠️⚠️ BEZPECNOST: testuje se VYHRADNE pamet, kterou nikdo jiny nepouziva —
 * volne regiony podle linkeru (DTCM, SRAM1 v D2, horni cast SRAM4), vyhrazeny
 * SDRAM scratch a vyhrazeny sektor ve W25Q (`W25Q_BENCH_BASE`). Interni FLASH
 * se JEN CTE (zapis/erase do banky, ze ktere se zaroven vykonava kod, by cip
 * zastavil na sbernici). Kdyz pribude cil, MUSI se doložit, ze je opravdu volny.
 *
 * ⚠️ VLAKNA: cely beh trva jednotky sekund a musi bezet v **UartTasku** — ten
 * jediny neni hlidany watchdogem (stejne omezeni jako `sd_export_run`). UI proto
 * jen nastavi `g_membench_req` a vysledky cte z hotoveho snapshotu.
 */
#ifndef INC_MEMBENCH_H_
#define INC_MEMBENCH_H_

#include <stdint.h>

#define MEMBENCH_TARGETS  6u   /* ⚠️ 7 -> 6: SRAM4/D3 odebrana, viz membench.c (shazovala CM4) */
#define MEMBENCH_NAME_N  14u
#define MEMBENCH_MSG_N   26u
#define MEMBENCH_PAT_N    5u   /* 0x00 / 0xFF / 55-AA / adresa / nahodny */
/* `alias_off` = prekryv zjisten, ale jeho vzdalenost nelze urcit (obsah kontrolni
 * bunky neni platny offset -> prepsal ji nekdo jiny nez benchmark). */
#define MEMBENCH_ALIAS_UNKNOWN  0xFFFFFFFFu

/** Nazvy vzoru (index = poradi v `pat_err`). */
extern const char *const membench_pat_name[MEMBENCH_PAT_N];

/* Vysledek jednoho cile. Rychlosti v kB/s (ne MB/s) — u QSPI by MB/s bylo
 * jednociferne a rozdil mezi behy by se ztratil v zaokrouhleni. */
typedef struct {
    char     name[MEMBENCH_NAME_N];  /* „SDRAM", „W25Q QSPI", ... */
    char     msg[MEMBENCH_MSG_N];    /* „OK" / „8 chyb. bitu" / duvod preskoceni */
    uint32_t addr;                   /* pocatecni adresa (0 = neni pametove mapovane) */
    uint32_t size_b;                 /* velikost TESTOVANEHO BLOKU */
    /* Celkova kapacita dane pameti. ⚠️ Skoro nikdy se nerovna `size_b` — testovat
     * jde jen to, co nikdo nepouziva (u SDRAM 4 MB scratch z 32 MB: zbytek drzi
     * framebuffery a linker sekce `.sdram`). Bez tohohle udaje vypadal sloupec
     * „velikost" jako kapacita cipu a mátl (nahlaseno pri HW testu 2026-08-23). */
    uint32_t total_b;
    uint32_t write_kbs;              /* 0 = necteno (read-only cil) */
    uint32_t read_kbs;
    uint32_t bit_errors;             /* pocet chybnych BITU pres vsechny vzory */
    uint32_t err_bitmask;            /* ktere bitove pozice selhaly (0 = zadna) */
    uint32_t first_err_addr;         /* adresa prvni neshody (jen kdyz bit_errors>0) */
    /* ── Rozlisovaci diagnostika (pridano 2026-08-23) ────────────────────────
     * Souhrnne cislo „N chybnych bitu" rekne, ZE je neco spatne, ale ne CO.
     * Tahle trojice rozlisi tri uplne jine priciny, ktere vypadaly stejne:
     *   pat_err[]      — kdyz selze JEN vzor „adresa", jsou vadne ADRESNI linky
     *                    (ne bunky); kdyz selzou vsechny, jsou vadna data.
     *   first_err_*    — skutecne precteno vs. cekano: nahodne bity = rozpad
     *                    obsahu, cizi platna hodnota = prekryv/alias.
     *   alias_off      — vzdalenost v BAJTECH, po ktere se adresy opakuji (dve
     *                    ruzne adresy = tataz bunka); 0 = adresni linky OK,
     *                    `MEMBENCH_ALIAS_UNKNOWN` = prekryv/cizi zapis zjisten,
     *                    ale vzdalenost se urcit neda (viz addr_lines_test).
     *   retain_err     — chyby po 1 s DRZENI dat (jen DRAM). Nenulove = pamet
     *                    NEUDRZI obsah, tedy prilis pomaly refresh. */
    uint32_t pat_err[MEMBENCH_PAT_N];
    uint32_t first_err_got, first_err_want;
    uint32_t alias_off;
    uint32_t retain_err;
    uint8_t  retain_done;            /* 1 = retencni test probehl (jen DRAM) */
    /* 1 = behem TOHOTO cile prestala odpovidat CM4. ⚠️ Samotne „stall:CM4" v logu
     * nerekne, ktery cil to zpusobil — a dohledavat to znamena dalsi kolo na HW. */
    uint8_t  killed_cm4;
    /* Bitmaska prekryvu MEZI framebuffery (jen SDRAM, jen kdyz `alias_off != 0`):
     * bit0 = FB0 sdili pamet s FB2, bit1 = FB1 s off-screen canvas poolem.
     * ⚠️ Tohle je ta opravdu drahá otázka — kdyby to platilo, triple buffering je
     * fakticky double a projevuje se to blikanim, ktere nikdo nespojuje s pameti. */
    uint8_t  fb_alias;
    uint8_t  tested;                 /* 1 = probehlo (i kdyz s chybami) */
    uint8_t  writable;               /* 0 = jen cteni -> write_kbs/bit_errors nemaji smysl */
    uint8_t  skipped;                /* 1 = preskoceno, duvod v `msg` */
} membench_result_t;

typedef struct {
    uint8_t  running;                /* 1 = prave bezi (UI ma cekat) */
    uint8_t  cur;                    /* index prave testovaneho cile */
    uint8_t  n;                      /* pocet cilu = MEMBENCH_TARGETS */
    uint8_t  done_once;              /* 1 = uz aspon jednou probehlo (jinak jsou vysledky prazdne) */
    uint32_t prog_pct;               /* 0..100 */
    uint32_t total_bit_errors;       /* soucet pres vsechny cile */
    char     phase[28];              /* „SDRAM: vzor 55/AA" — pro UI i UART */
    membench_result_t r[MEMBENCH_TARGETS];
} membench_state_t;

/* ── Pozadavek UI -> UartTask (vzor `g_sd_req`, viz sd_export.h) ──────────────
 * UiTask je hlidany watchdogem a benchmark bezi sekundy -> tlacitko jen nastavi
 * priznak, praci udela UartTask v `membench_service()`. */
#define MEMBENCH_REQ_NONE  0u
#define MEMBENCH_REQ_RUN   1u
extern volatile uint8_t g_membench_req;

/** @return snapshot vysledku. Bezpecne z UiTasku — jen cteni, nic neblokuje. */
const membench_state_t *membench_state(void);

/** Obslouzi cekajici pozadavek. Vola VYHRADNE UartTask ve sve smycce. */
void membench_service(void);

/** ⚠️ BLOKUJE (jednotky sekund) — jen z UartTasku. Vyplni `membench_state()`. */
void membench_run(void);

/** Pure-logic selftest (generatory vzoru + pocitani chybnych bitu), bez pameti
 *  a bez HW. @return 1 = PASS. */
int membench_selftest(void);

#endif /* INC_MEMBENCH_H_ */
