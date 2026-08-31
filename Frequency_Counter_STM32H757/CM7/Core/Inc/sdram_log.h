#ifndef INC_SDRAM_LOG_H_
#define INC_SDRAM_LOG_H_

/**
 * @file    sdram_log.h
 * @brief   Datova cache mereni v SDRAM — dlouha PRESNA historie pro analyzu.
 *
 * PROC: dnesni statistika drzi jen decimacni pyramidu v RAM (`s_tr[]`/ADEV
 * stages) — ta je uzasne levna, ale je to ZTRATOVA komprese. Na Allanovu
 * odchylku pri dlouhych tau, spektrogram nebo proklad je potreba SUROVA rada
 * vzorku. 16 MB v SDRAM staci na **1 048 576 zaznamu** po 16 B.
 *
 * CO SE UKLADA: kmitocet v **µHz**, dopocteny z reciproke dvojice
 * `edges`/`gate_ns` (hi-res, ~7 platnych desetin — vic, nez nese zaokrouhlene
 * `x100000`). 1 µHz je hluboko pod rozlisenim TDC (~0,1 Hz), takze ulozeni
 * nic neztrati.
 *
 * ⚠️ PROC NE SUROVA DVOJICE `edges`/`gate_ns`: `edge_count` muze byt pocet
 * period DELENE vetve (/4) nebo neděleného signalu (emulator) — sam o sobe je
 * tedy DVOJZNACNY a ctenar by si musel nasobitel hadat. Presne to uz jednou
 * zpusobilo, ze `fpgasim on 10000000` ukazoval 40 MHz (commit a6c0128) a pri
 * psani teto cache se to zopakovalo. Nasobitel proto overuje
 * `fpga_freq_hires_mul()` PRI ZAPISU a do logu jde uz jednoznacne cislo.
 *
 * ⚠️ EPOCHA: `gate_ns` a priznak SIM jsou vlastnosti KONFIGURACE, ne vzorku —
 * drzi je `sdram_log_stat_t`, ne kazdy zaznam (usetri to 8 B/zaznam = dvojnasobna
 * historie). Zmena brany nebo prechod REAL<->SIM ring VYNULUJE, takze log nikdy
 * nemicha nesouměřitelné vzorky. Je to tataz politika, kterou uz ma web SPA
 * („buffer se zahodí při změně brány") i Allan/trend pyramida v screen_main.
 *
 * ⚠️ VLAKNA: jeden producent (FpgaTask, `sdram_log_put`) a libovolne mnoho
 * ctenaru (`sdram_log_get`/`_snapshot`). Ring je bezzamkovy — `head` se zvedne
 * AZ PO zapisu zaznamu (`__DMB()` mezi tim), takze ctenar nikdy neuvidi
 * rozepsany zaznam. Pri pretoceni muze byt nejstarsi zaznam prepsan pod rukama;
 * `sdram_log_get` to hlasi navratovou hodnotou 0.
 *
 * ⚠️ CACHE: region je MPU 3 = Normal WBWA (cacheable). Dokud plni CPU, je to
 * koherentni. **Az se log bude plnit SPI pres DMA (protokol v2, STATUS #62),
 * MUSI konzument pred ctenim volat `sdram_log_invalidate()`** — DMA obchazi
 * D-cache uplne stejne jako DMA2D u framebufferu.
 *
 * ⚠️ PAMET: `sdram_log_init()` region SAM OVERI (vzory + adresni aliasing).
 * Duvod: `membench` testuje aliasing jen do 2 MB, ale tenhle region zacina
 * na 0xC1000000 — tedy DALEKO nad overenym rozsahem. Kdyby se adresy opakovaly
 * (otevrene podezreni na pajku `FMC_A9`/`PF15`, viz CLAUDE.md), log by se tise
 * prepisoval a analyza by pocitala nesmysly. Pri selhani se log NEZAPNE.
 */

#include <stdint.h>

/* Priznaky v zaznamu (`flags`). */
#define SDRAM_LOG_F_A_VALID  (1u << 0)   /* kanal A nese platne mereni */
#define SDRAM_LOG_F_B_VALID  (1u << 1)   /* kanal B nese platne mereni */
#define SDRAM_LOG_F_STALE    (1u << 2)   /* SIGNAL_LOST / mrtvy link v okamziku vzorku */

/* Jeden vzorek = OBA kanaly. 32 B -> 262 144 zaznamu na 8 MB.
 * ⚠️ Velikost MUSI zustat mocnina 2 — index se pak maskuje, ne deli.
 * ⚠️ PROC oba kanaly v JEDNOM zaznamu (a ne dva ringy): mnou se ve stejnem
 * hradle, takze spolecny zaznam **zarucuje parovani** — bez nej by se rady pri
 * vypadku jednoho kanalu rozesly a fazi, pomer ani krizovou korelaci uz z toho
 * nespocitas. Dva samostatne ringy by mely tutez kapacitu a slabsi zaruku.
 * ⚠️ Do prechodu na dvoukanalovou desku plni jen kanal A; `flags` rekne, ktery
 * kanal je platny, takze stara i nova data jdou cist stejnym kodem. */
typedef struct {
    uint32_t seq;       /* SEQUENCE z FPGA ramce — z mezer v rade poznas vypadek */
    uint32_t t_ms;      /* HAL_GetTick() pri prijeti — hruba casova osa */
    uint64_t fa_uhz;    /* kanal A [µHz] s OVERENYM nasobitelem (viz hlavicka) */
    uint64_t fb_uhz;    /* kanal B [µHz]; 0 dokud neni dvoukanalova deska */
    uint32_t flags;     /* SDRAM_LOG_F_* */
    uint32_t reserved;  /* 0 — dorovnani na 32 B, misto pro budouci pole */
} sdram_log_rec_t;

/** Stav logu pro diagnostiku (UART `sdramlog`). */
typedef struct {
    uint8_t  ready;      /* 1 = pamet overena a log bezi */
    uint8_t  wrapped;    /* 1 = ring se uz pretocil (nejstarsi data prepsana) */
    uint8_t  sim;        /* 1 = obsah pochazi z emulatoru `fpgasim`, ne z FPGA */
    uint64_t gate_ns;    /* delka hradla teto epochy = tau0 pro Allanovu odchylku */
    uint32_t capacity;   /* kolik zaznamu se vejde */
    uint32_t count;      /* kolik jich je ulozenych (<= capacity) */
    uint32_t total;      /* kolik jich CELKEM proslo (roste i po pretoceni) */
    uint32_t dropped;    /* kolik se zahodilo, protoze log nebyl ready */
    char     fail[48];   /* proc se log nezapnul (prazdne = OK) */
} sdram_log_stat_t;

/** Overi pamet regionu a zapne log. Volat JEDNOU pred spustenim FpgaTasku.
 *  @return 1 = pamet OK a log bezi, 0 = region vadny -> log VYPNUTY (viz `fail`). */
int  sdram_log_init(void);

/** Ulozi jeden vzorek (vola VYHRADNE producent = FpgaTask). Levne: zapis 16 B.
 *
 *  Prevod ramce na `f_uhz` udela volajici pres `fpga_freq_hires_uhz()` — jediny
 *  zdroj pravdy o nasobiteli (viz fpga_freq.h).
 *
 *  `gate_ns` a `sim` jsou vlastnosti EPOCHY: pri jejich zmene se ring vynuluje,
 *  aby log nemichal nesouměřitelné vzorky. U `gate_ns` se ignoruje ppm kolisani
 *  (rozhoduje az zmena o >10 %, tedy skutecne prepnuti presetu brany). */
void sdram_log_put(uint32_t seq, uint64_t fa_uhz, uint64_t fb_uhz, uint32_t flags,
                   uint32_t t_ms, uint64_t gate_ns, uint8_t sim);

/** Precte `i`-ty NEJNOVEJSI zaznam (i=0 je posledni).
 *  @return 1 = platny, 0 = mimo rozsah nebo prepsan behem cteni. */
int  sdram_log_get(uint32_t i, sdram_log_rec_t *out);

/** Kopie souvisleho useku od `i`-teho nejnovejsiho smerem do minulosti.
 *  Rychlejsi nez `sdram_log_get` ve smycce (jedna kontrola konzistence).
 *  @return kolik zaznamu se skutecne zkopirovalo. */
uint32_t sdram_log_read_back(uint32_t i, sdram_log_rec_t *out, uint32_t n);

/** Aktualni stav (kapacita, pocet, chyby). */
void sdram_log_stat(sdram_log_stat_t *out);

/** Zahodi obsah (index na nulu). Data se nemazou, jen prestanou byt viditelna. */
void sdram_log_reset(void);

/** Zneplatni D-cache nad celym logem. ⚠️ Nutne JEN kdyz log plni DMA
 *  (protokol v2) — pri plneni z CPU je to zbytecne. */
void sdram_log_invalidate(void);

/** Pure-logic selftest indexovani ringu (bez SDRAM, bezi i bez HW).
 *  @return 1 = PASS. */
int  sdram_log_selftest(void);

#endif /* INC_SDRAM_LOG_H_ */
