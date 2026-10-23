/* fpga_v3.h -- protokol SPI v3 (docs/PROTOKOL_V3.md): ramec 1024 B = 4 bloky x 256 B, zaznamy spojitych segmentu.
 *
 * CISTA LOGIKA BEZ HAL (preklada se i na hostu, tests/host/t_fpga_v3.c):
 *   - CRC bloku, skladani ramce MOSI (ACK, SET_CONFIG, LUT_WRITE po az 252 polozkach),
 *   - parser ramce MISO (hlavicka bloku 0, zaznamy z bloku 1-3 jen do prvniho vadneho bloku),
 *   - EMULATOR v3 (sklada ramce MISO tak, jak je bude skladat RTL: kruh zaznamu, REC_FIRST od posledniho ACK,
 *     CRC bloku, znacka bloku) -- aby se STM strana dala vyvijet a testovat bez noveho bitstreamu (L-0099),
 *   - vrstva SEGMENTY -> OKNO: z K po sobe jdoucich segmentu jednoho kanalu okno pro statistiku
 *     (dvoubodove mezi prvnimi znackami sousednich oken = teleskopuje; nejmensi ctverce pres teziste segmentu).
 *     Nizky kmitocet (perioda > L): prazdne segmenty okno prodlouzi az k pristi hrane (reciprocni citac);
 *     ztrata signalu = mezera bez hrany > 3 ocekavane periody, nejvys max_gap_ms (2,5 s jako v2).
 * 2026-10-22: do driveru (fpga_freq.c) NEZAPOJENO -- cesta v2 se nemeni.
 *
 * Jednotka casu v ramci = T_clk/16384 (100 MHz: 0,6103515625 ps), jako ve v2. */
#ifndef FPGA_V3_H
#define FPGA_V3_H

#include <stdint.h>
#include <stdbool.h>

#define FPGA_V3_LEN            1024u
#define FPGA_V3_BLK            256u
#define FPGA_V3_NBLK           4u
#define FPGA_V3_MAGIC          0xA5u
#define FPGA_V3_VER            0x03u
#define FPGA_V3_REC_LEN        31u
#define FPGA_V3_REC_PER_BLK    8u
#define FPGA_V3_REC_MAX        24u     /* 3 bloky x 8 */
#define FPGA_V3_LUTW_PER_BLK   84u     /* polozek LUT_WRITE / stranky na blok (3 B) */
#define FPGA_V3_LUTW_MAX       252u
#define FPGA_V3_P_MAX          242u    /* parametry prikazu v bloku 0: bajty 12..253 */

/* TYPE -- cislovani z FW 0x0427 (rozhodnuti uzivatele 2026-10-22) */
#define FPGA_V3_T_SETCFG       0x01u
#define FPGA_V3_T_ACK          0x06u
#define FPGA_V3_T_START        0x08u
#define FPGA_V3_T_STOP         0x09u
#define FPGA_V3_T_LUTW         0x0Au
#define FPGA_V3_T_RAW          0x0Bu
#define FPGA_V3_T_DATA         0x80u
#define FPGA_V3_T_PAGE         0xA0u
/* SET_CONFIG id (p0) */
#define FPGA_V3_CFG_CAL        0x02u
#define FPGA_V3_CFG_REF        0x04u
#define FPGA_V3_CFG_DIAG       0x05u
#define FPGA_V3_CFG_LUT        0x06u   /* 0 = zacatek zapisu, 1 = potvrzeni = prepnuti banky */
#define FPGA_V3_CFG_PRESC      0x07u
#define FPGA_V3_CFG_SEGLEN     0x08u
#define FPGA_V3_CFG_BSRC       0x09u   /* FW 0x0501: zdroj kanalu B -- bit0 = pin A misto pinu B, bit1 = invertovat */
#define FPGA_V3_CAP_BSRC       (1u << 20)   /* CAPS: SET_CONFIG 0x09 (echo bajt 66 bit3..4) */
/* v0.37.0 (FW 0x0502, etapa E4): 1PPS s jemnym casem pres IDES8 (krok 1,25 ns, PROTOKOL_V3 kap. 3.1 bajty 50, 68..95) */
#define FPGA_V3_CAP_PPS_IDES   (1u << 17)   /* CAPS: PTS 1PPS je jemny cas (PFLAGS bit1) */
/* v0.38.0 (FW 0x0503, etapa E5): predelicka vstupu P = 2^p (p 0..4), SET_CONFIG 0x07 p1 = kanal, p2 = p; plati od
 * hranice segmentu, 2 segmenty kolem zmeny neplatne. log2 P segmentu v bajtu CH zaznamu (bity 6:4), X pocita DELENE hrany. */
#define FPGA_V3_CAP_PRESC      (1u << 18)   /* CAPS: predelicka pin_cnt3 */
/* v0.39.0 (FW 0x0505): hluboky RAW zaznam do vestavene PSRAM. T_RAW p0: bit0 kanal, bity 2:1 rezim (0 = 512 do BSRAM,
 * 1 = hluboky N znacek, 2 = nacist okno w do BSRAM); p1..p3 = N (u24) / w (u16). Okno se cte strankami druhu 3. */
#define FPGA_V3_CAP_PSRAM      (1u << 21)
#define FPGA_V3_DEEP_MAX       699050u      /* 4 MB / 6 B */
#define FPGA_V3_DS_RDY         0x01u        /* DEEP_ST (bajt 128): PSRAM pripravena (kalibrace zapisu prosla) */
#define FPGA_V3_DS_BAD         0x02u        /* zadna latence zapisu nesedela */
#define FPGA_V3_DS_RUN         0x04u        /* hluboky zaznam bezi */
#define FPGA_V3_DS_OVF         0x08u        /* FIFO preteklo (znacky zahozeny) */
#define FPGA_V3_DS_WLOAD       0x10u        /* nacita se okno */
#define FPGA_V3_DS_WDONE       0x20u        /* okno nacteno */
#define FPGA_V3_DS_GAP         0x40u        /* audit F-0270: v zaznamu jsou mezery (kanal neznackoval: hold, maska pri CS) */
#define FPGA_V3_DS_WERR        0x80u        /* audit F-0268: okno s chybou cteni (0 znacek) */
#define FPGA_V3_DACK_N(a)      ((uint8_t)((a) & 0x0Fu))   /* bajt 127: prijate prikazy T_RAW (Gray, mod 16) */
#define FPGA_V3_DACK_CH        0x80u        /* bajt 127 bit7: kanal posledniho prijateho zaznamu */
#define FPGA_V3_PS_W0          4u           /* bajt 129 bity 4:0 = latence zapisu PSRAM - W0 [bajty]; bit5 faze cteni */
#define FPGA_V3_REC_PLOG2(r)   ((uint8_t)(((r)->ch >> 4) & 7u))   /* log2 P segmentu zaznamu */
#define FPGA_V3_PRESC_PEND     0x80u        /* PRESC A/B (bajty 64/65): zmena ceka na hranici segmentu */
#define FPGA_V3_PROBE_HZ(p)    ((double)(p) * 16000.0)   /* PROBE A/B: hrany /16 za 1 ms -> kmitocet [Hz] */
/* automaticky rozsah (navrh v3/fpga_range.c, E5): cil f/P <= 30 MHz (TDC ~20 Mznacek/s, prime pocitani ~40 MHz),
 * hystereze: nahoru kdyz f/P > 33 MHz, dolu kdyz f/(P/2) < 27 MHz; strop /16 */
#define FPGA_V3_RANGE_UP_HZ    33.0e6
#define FPGA_V3_RANGE_DOWN_HZ  27.0e6
#define FPGA_V3_RANGE_MAXLOG2  4u
#define FPGA_V3_PF_VALID       0x01u        /* PFLAGS: hrana platna */
#define FPGA_V3_PF_FINE        0x02u        /* PFLAGS: PTS z IDES8 (jinak hruby cas x 16384) */
#define FPGA_V3_PPS_ST_LOCK    0x01u        /* PPS_STATUS: PCLK bezi (rPLL zamcena a uvolnena) */
#define FPGA_V3_PPS_ST_NOFINE  0x04u        /* PPS_STATUS: posledni hrana pri zamcene PLL bez jemneho casu */
#define FPGA_V3_PPS_EPOCH(st)  ((uint8_t)((uint8_t)(st) >> 4))   /* uvolneni PCLK mod 16: zmena = jina osa jemneho casu */
/* FLAGS ramce (blok 0, bajt 3) */
#define FPGA_V3_F_NEWREC       (1u << 0)
#define FPGA_V3_F_RINGOVF      (1u << 1)
#define FPGA_V3_F_CAL          (1u << 2)
#define FPGA_V3_F_FAST         (1u << 3)
#define FPGA_V3_F_RXERR        (1u << 4)
#define FPGA_V3_F_CMDDONE      (1u << 5)
/* FLAGS zaznamu */
#define FPGA_V3_R_INVALID      (1u << 0)
#define FPGA_V3_R_PRESC        (1u << 1)
#define FPGA_V3_R_BUBBLE       (1u << 2)
#define FPGA_V3_R_CS           (1u << 3)
#define FPGA_V3_R_CCD          (1u << 4)
#define FPGA_V3_R_SAT          (1u << 5)
#define FPGA_V3_R_FAST         (1u << 6)

/* sirky poli zaznamu (rozbaluji se modularne, fpga_v3_grp_feed) */
#define FPGA_V3_X0_BITS        40u
#define FPGA_V3_T0_BITS        48u

/* navratove kody parseru */
#define FPGA_V3_OK             0
#define FPGA_V3_E_MAGIC        (-1)
#define FPGA_V3_E_VER          (-2)
#define FPGA_V3_E_CRC0         (-3)
#define FPGA_V3_E_LEN          (-4)

typedef struct {
    uint32_t pseq;               /* poradi hrany 1PPS */
    uint64_t pts;                /* cas hrany [T/16384], u48 */
    uint8_t  pflags;
    int8_t   d;                  /* FW 0x0502: jemny - hruby cas [takty] (diagnostika osy a poradi bitu IDES8) */
} fpga_v3_pps_t;

/* hlavicka (blok 0) ramce MISO -- offsety viz PROTOKOL_V3.md kap. 3.1 */
typedef struct {
    uint8_t  type, flags;
    uint32_t seq;
    uint16_t frame_len;
    uint8_t  nblk;
    uint8_t  blk_ok;             /* bit k = CRC bloku k sedi (spocita parser) */
    uint32_t build_time, build_git;
    uint16_t fw_version;
    uint8_t  clk_mhz, seg_len_ms;
    uint32_t caps, ts10m;
    uint32_t rec_head, rec_first;
    uint8_t  nrec;
    uint16_t ring_fill;
    uint8_t  clk_status, clk_loss, coh_st, coh_cnt;
    uint8_t  tdc_status[2], pps_status, cal_wait;
    uint16_t rx_crc_err, rx_len_err;
    uint32_t rx_last;
    uint16_t lut_cnt, lut_sum;
    uint16_t lut_cnt_b;          /* polozky s bitem kanalu B (bajty 122..123) */
    uint8_t  presc[2], cfg_echo;
    fpga_v3_pps_t pps[2];        /* [0] posledni, [1] predchozi */
    uint32_t pps_coarse[2];
    uint16_t probe[2];
    uint8_t  bub[2], ovl[2];
    uint32_t deep_n;             /* v0.39.0: znacek v PSRAM (bajty 124..126) */
    uint8_t  deep_st, deep_wsel; /* bajt 128 (FPGA_V3_DS_*), 129 latence zapisu PSRAM (W0 + n) a faze cteni (bit5) */
    uint8_t  deep_ack;           /* audit F-0272: bajt 127 (FPGA_V3_DACK_*) */
    uint16_t deep_win;           /* bajty 130..131: posledni (nacitane) okno */
} fpga_v3_hdr_t;

/* zaznam segmentu (31 B v ramci) */
typedef struct {
    uint16_t seg_idx;            /* dolnich 16 b cisla segmentu */
    uint8_t  ch;                 /* 0 = A, 1 = B */
    uint8_t  flags;              /* FPGA_V3_R_* */
    uint32_t n;                  /* casovane hrany (u24) */
    uint64_t x0;                 /* absolutni index prvni hrany (u40) */
    uint64_t t0;                 /* absolutni znacka prvni hrany [T/16384] (u48) */
    uint64_t sx;                 /* sum(x - x0) (u48) */
    uint64_t st;                 /* sum(t - t0) [T/16384] (u56) */
} fpga_v3_rec_t;

/* ---- ramec ---- */
uint16_t fpga_v3_blk_crc(const uint8_t *blk);                  /* CRC-16/CCITT-FALSE bajtu 0..253 bloku */
void     fpga_v3_blk_seal(uint8_t *blk);                       /* zapise CRC na 254..255 (LE) */
/* 1PPS hrany i (0 posledni, 1 predchozi) v taktech + zlomek [1/16384]: jemny cas (vraci 1), kdyz FPGA ma CAPS bit17,
 * PCLK bezi a hrana ma PFLAGS bit1; jinak hruby citac z bajtu 100.. (zlomek 0, vraci 0). */
int      fpga_v3_pps_fine(const fpga_v3_hdr_t *h, int i, uint32_t *ticks, uint16_t *frac);
/* v0.38.0: log2 P pro odhad kmitoctu f_hz (<= 0 = beze zmeny) s hysterezi proti aktualnimu cur_log2p */
uint8_t  fpga_v3_range_select(uint8_t cur_log2p, double f_hz);
/* audit F-0273: log2 P ze sondy PROBE (hrany /16 za 1 ms); PROBE = 0 (f < 16 kHz nebo bez signalu) -> 0 */
uint8_t  fpga_v3_range_probe(uint8_t cur_log2p, uint16_t probe);
uint8_t  fpga_v3_blk_ok(const uint8_t *frame);                 /* bit k = CRC bloku k sedi */
void     fpga_v3_rec_pack(uint8_t *dst, const fpga_v3_rec_t *r);    /* 31 B */
void     fpga_v3_rec_unpack(fpga_v3_rec_t *r, const uint8_t *src);

/* MOSI: hlavicka + parametry p[0..np-1] od bajtu 12, CRC vsech bloku. np <= FPGA_V3_P_MAX. */
void     fpga_v3_build(uint8_t *f, uint8_t type, uint32_t seq, const uint8_t *p, uint32_t np);
void     fpga_v3_build_ack(uint8_t *f, uint32_t seq, uint32_t rec_ack);
void     fpga_v3_build_cfg(uint8_t *f, uint32_t seq, uint8_t id, uint8_t v1, uint8_t v2);
/* LUT_WRITE: polozky v blocich 1-3 (kodovani z FW 0x0427); vraci pocet zabalenych (<= 252) a pricte je do *sum
 * stejnym vzorcem, jaky vraci FPGA v LUT_SUM (fpga_tdccal_chk). */
uint32_t fpga_v3_build_lutw(uint8_t *f, uint32_t seq, const uint8_t *ch, const uint16_t *addr, const uint16_t *neg,
                            uint32_t n, uint16_t *sum);

/* MISO: rozebere hlavicku (vyzaduje platny blok 0) a u TYPE DATA zaznamy. *n_ok = pocet SOUVISLE platnych zaznamu
 * od REC_FIRST (konci na prvnim vadnem bloku nebo nesedici znacce bloku) -> o tolik se smi posunout ACK. */
int      fpga_v3_parse(const uint8_t *f, fpga_v3_hdr_t *h, fpga_v3_rec_t *rec, uint32_t *n_ok);

/* ---- emulator FPGA v3 ---- */
#define FPGA_V3_EMU_RING       128u    /* zaznamu (64 segmentu = 1,6 s pri 25 ms); FPGA ma 512 */
typedef struct {
    double   f_hz[2];            /* kmitocet kanalu A/B; <= 0 = bez signalu (zaznamy N = 0) */
    double   sigma_ps;           /* bily sum znacky */
    uint32_t seg_ms;             /* L */
    uint32_t clk_mhz;
    uint32_t seg_idx;            /* pristi segment */
    uint32_t rec_head;           /* pocet vytvorenych zaznamu (= poradi pristiho) */
    uint32_t rec_ack;            /* posledni potvrzeny (STM: REC_ACK) */
    uint32_t seq;
    uint8_t  ovf;
    uint64_t rng;
    double   t_next[2];          /* cas pristi hrany od zacatku aktualniho segmentu [s] */
    uint64_t x_next[2];          /* absolutni index pristi hrany */
    uint8_t  p_log2[2], p_req[2], p_inv[2];   /* v0.38.0: predelicka jako RTL (SET_CONFIG 0x07, 2 neplatne segmenty) */
    fpga_v3_rec_t ring[FPGA_V3_EMU_RING];
} fpga_v3_emu_t;

void     fpga_v3_emu_init(fpga_v3_emu_t *e, double fa_hz, double fb_hz, double sigma_ps, uint32_t seed);
void     fpga_v3_emu_segment(fpga_v3_emu_t *e);              /* jeden segment = zaznam A + zaznam B */
void     fpga_v3_emu_frame(fpga_v3_emu_t *e, uint8_t *miso);  /* DATA ramec od rec_ack (jako RTL) */
void     fpga_v3_emu_rx(fpga_v3_emu_t *e, const uint8_t *mosi);   /* prijme ramec STM (ACK posune rec_ack) */
/* faze kanalu ch: pristi hrana za t_s od zacatku aktualniho segmentu (napr. B = A + zpozdeni pri stejnem kmitoctu) */
void     fpga_v3_emu_set_phase(fpga_v3_emu_t *e, uint8_t ch, double t_s);

/* ---- prijem zaznamu na STM ----
 * FPGA sklada ramec PRED zpracovanim ACK z teze transakce (kap. 6.1), takze kazdy zaznam prijde normalne DVAKRAT
 * (REC_FIRST < ocekavany = prekryv, ne chyba). REC_FIRST > ocekavany = mezera (preteceni kruhu): okna prerusit.
 * REC_HEAD + 1 < ocekavany = FPGA zacala znovu od 0 (reset / nove nahrani): resynchronizace. */
typedef struct {
    uint32_t expect;             /* dalsi ocekavany zaznam = REC_ACK do dalsi transakce */
    uint8_t  have;               /* expect je platny (po prvnim ramci) */
    uint32_t frames, recs, dups, gaps, gap_recs, resync;
} fpga_v3_link_t;
void     fpga_v3_link_init(fpga_v3_link_t *L);
/* Ramec DATA (po fpga_v3_parse): vrati pocet NOVYCH zaznamu, prvni je rec[*i0]; *gap = 1 -> pred nimi je mezera
 * (nebo prvni ramec / resynchronizace) -> fpga_v3_grp_break obou kanalu. */
uint32_t fpga_v3_link_take(fpga_v3_link_t *L, const fpga_v3_hdr_t *h, uint32_t n_ok, uint32_t *i0, int *gap);

/* ---- jednorazove deje (STM): udalosti z hran zaznamu (docs/PROTOKOL_V3.md kap. 10.5) ----
 * Zaznam segmentu nese PRESNE cas prvni hrany (T0); pri N = 2 i druhe (T0 + ST, index X0 + SX); pri N >= 3 jen souhrn
 * (pocet, stredni perioda ST/SX). Casuji se jen hrany >= 50 ns od predchozi casovane; delsi sekvenci zachyti RAW_CAPTURE. */
#define FPGA_V3_EV_EDGE        1u      /* presny cas jedne hrany */
#define FPGA_V3_EV_BURST       2u      /* souhrn segmentu (N >= 3): presna prvni hrana, pocet, stredni perioda */
typedef struct {
    uint8_t  ch, kind;
    uint16_t seg;                /* SEG_IDX zaznamu */
    uint32_t n;                  /* hran (EDGE: 1) */
    uint64_t x;                  /* rozbaleny index (prvni) hrany */
    uint64_t t_u;                /* rozbaleny cas (prvni) hrany [T/16384] (osa FPGA, spolecna pro A, B i 1PPS) */
    double   per_u;              /* BURST: stredni perioda [jednotky], jinak 0 */
} fpga_v3_ev_t;
typedef struct {
    uint8_t  have;               /* cas: SPOLECNY pro oba kanaly (jedna osa FPGA) */
    uint64_t t_raw, t_un;
    uint32_t ms;
    uint8_t  xhave[2];           /* index hrany: po kanalech */
    uint64_t x_raw[2], x_un[2];
} fpga_v3_evu_t;
void     fpga_v3_evu_init(fpga_v3_evu_t *u);
/* Platny zaznam (FLAGS bit0 = 0, N >= 1) -> udalosti do out[0..1]: N = 1 -> EDGE, N = 2 -> 2x EDGE, N >= 3 -> BURST.
 * now_ms = cas STM pri prijmu: T0 (48 b) se rozbali i pres pauzu > 171,8 s (pocet preteceni z uplynuleho casu).
 * @return pocet udalosti (0 = zaznam bez hran / neplatny) */
uint32_t fpga_v3_ev_from_rec(fpga_v3_evu_t *u, const fpga_v3_rec_t *r, uint32_t clk_mhz, uint32_t now_ms, fpga_v3_ev_t out[2]);

/* Parovani start/stop (jako jednorazovy citac intervalu): start = prvni hrana A po predchozim stopu, stop = prvni hrana B
 * po startu (cas > start); start starsi nez max_u se zahodi. Udalosti JEDNE hranice segmentu (A i B) se predaji spolu
 * (fpga_v3_ab_feed je seradi v case). U souhrnu (BURST) se bere jen prvni, presna hrana. Sirka pulzu = totez s kanalem B
 * na pinu A invertovanem (SET_CONFIG 0x09 = 3): A nabezne, B sestupne hrany. */
typedef struct {
    uint8_t  armed;              /* 1 = ceka na stop */
    uint64_t t_start;
    uint64_t max_u;
    uint32_t n, lost;            /* intervalu / zahozenych startu (bez stopu do max_u) */
} fpga_v3_ab_t;
void     fpga_v3_ab_init(fpga_v3_ab_t *p, uint64_t max_u);
/* @return pocet intervalu zapsanych do dt_u[0..max_out-1] [jednotky] */
uint32_t fpga_v3_ab_feed(fpga_v3_ab_t *p, const fpga_v3_ev_t *ev, uint32_t n, int64_t *dt_u, uint32_t max_out);

/* Prubezna statistika intervalu [ps] (Welford: rozptyl i pri velke stredni hodnote, napr. intervaly sekund v ps) */
typedef struct { uint32_t n; double mean, m2, min, max; } fpga_v3_stat_t;
void     fpga_v3_stat_init(fpga_v3_stat_t *s);
void     fpga_v3_stat_add(fpga_v3_stat_t *s, double v);
double   fpga_v3_stat_mean(const fpga_v3_stat_t *s);
double   fpga_v3_stat_sd(const fpga_v3_stat_t *s);
/* Kalibrace zpozdeni A/B (B = pin A neinvertovany, SET_CONFIG 0x09 = 1): prvni hrana B minus prvni hrana A teze hranice
 * segmentu, jen |d| < lim_u (stejna hrana). @return 1 = zapocteno */
int      fpga_v3_skew_add(fpga_v3_stat_t *s, const fpga_v3_ev_t *ea, const fpga_v3_ev_t *eb, int64_t lim_u, double unit_ps);

/* Parovani po zaznamech (ovladac, FW 0x0501): FPGA posila kazdy segment jako A, pak B. Udalosti A segmentu cekaji na
 * zaznam B tehoz segmentu a predaji se spolu (fpga_v3_ab_feed). Zaznam neplatny (FLAGS bit0) nebo se znackami pri CS
 * (bit3) = hrana mohla chybet -> rozpracovany start pryc. Po prepnuti zdroje B (fpga_v3_pr_switch) se paruje az za
 * zaznamem B s drzenim (bit0; starsi zaznamy jsou z puvodniho zdroje), nejvys 80 zaznamu B (2 s). Rezim skew = misto
 * intervalu zpozdeni B - A prvnich hran teze hranice segmentu do sk (|d| < 50 ns). */
typedef struct {
    fpga_v3_ab_t   ab;
    fpga_v3_stat_t sk;           /* zpozdeni B - A [ps] (rezim skew) */
    fpga_v3_ev_t   pa[2];        /* udalosti A cekajici na zaznam B */
    uint32_t       pan, skip_n;
    uint16_t       pa_seg;
    uint8_t        pa_have, skip;
} fpga_v3_pr_t;
void     fpga_v3_pr_init(fpga_v3_pr_t *p, uint64_t max_u);
void     fpga_v3_pr_switch(fpga_v3_pr_t *p);             /* zdroj B prepnut */
void     fpga_v3_pr_gap(fpga_v3_pr_t *p);                /* mezera v prijmu zaznamu */
/* zaznam r a jeho udalosti e[0..ne-1] (fpga_v3_ev_from_rec) -> intervaly dt_u[0..max_out-1] [jednotky]
 * @return pocet intervalu */
uint32_t fpga_v3_pr_rec(fpga_v3_pr_t *p, const fpga_v3_rec_t *r, const fpga_v3_ev_t *e, uint32_t ne, int skew,
                        double unit_ps, int64_t *dt_u, uint32_t max_out);

/* ---- segmenty -> okno ---- */
typedef struct {
    uint8_t  ch;
    uint32_t seg_first;          /* cislo prvniho segmentu okna: dolnich 16 b z ramce (NErozbaleno, rozdily mod 2^16) */
    uint32_t nseg, n_marks;
    uint64_t edges;              /* period mezi prvnimi znackami tohoto a pristiho okna (dvoubodove, teleskopuje) */
    uint64_t gate_units;         /* odpovidajici cas [T/16384] */
    double   f_2pt_hz;           /* edges / gate */
    double   f_lsq_hz;           /* nejmensi ctverce pres teziste segmentu okna (vaha N) */
    uint8_t  lsq_ok;
    uint8_t  flags_or;           /* OR priznaku zaznamu okna */
    double   xc0, tc0;           /* teziste PRVNIHO segmentu okna (absolutni, pro fazovy zaznam) */
    uint64_t x_first, t_first;   /* prvni znacka okna (rozbalene X, T) -- teziste relativne k oknu = xc0 - x_first */
    uint32_t n0;                 /* znacek prvniho segmentu okna */
    uint8_t  p_log2;             /* v0.38.0: log2 P okna; edges, xc0, x_first a kmitocty jsou uz v PERIODACH (x P) */
} fpga_v3_win_t;

typedef struct {
    uint8_t  have_prev;          /* rozbaleni X0/T0 */
    uint64_t x_prev_raw, t_prev_raw, x_prev, t_prev;
    uint16_t seg_last;           /* posledni segment kanalu (kontrola souvislosti) */
    uint8_t  open;
    uint32_t k, n_tot;
    uint32_t seg_first;
    uint64_t x_first, t_first;
    double   sw, swx, swt, swxx, swxt;
    double   xc0, tc0;
    uint32_t n0;
    uint8_t  flags_or;
    uint32_t empty;              /* prazdnych segmentu od posledni hrany v okne */
    double   per_u;              /* odhad periody [jednotky / hranu]; 0 = neznamy (pak plati max_gap_ms) */
    uint8_t  p_log2;             /* v0.38.0: log2 P rozpracovaneho okna (zmena P = okno zahodit) */
} fpga_v3_grp_ch_t;

typedef struct {
    uint32_t k_target;           /* segmentu na okno (hradlo / L); okno konci na prvni hrane po K segmentech */
    uint32_t seg_ms;             /* L [ms]: vychozi 25, ovladac nastavi z hlavicky (seg_len_ms) */
    uint32_t max_gap_ms;         /* nejdelsi mezera bez hrany uvnitr okna: vychozi 2500 (ztrata signalu v2) */
    fpga_v3_grp_ch_t c[2];
} fpga_v3_grp_t;

void     fpga_v3_grp_init(fpga_v3_grp_t *g, uint32_t k_target);
void     fpga_v3_grp_break(fpga_v3_grp_t *g, uint8_t ch);   /* mezera / neplatny zaznam: okno zahodit */
/* @return 1 = do *out prislo hotove okno (uzavrene prvnim zaznamem s hranou po K segmentech = zacatek dalsiho okna).
 * Zahodi rozpracovane okno: neplatny zaznam, nesouvisle SEG_IDX, mezera bez hrany nad limit (ztrata signalu). */
int      fpga_v3_grp_feed(fpga_v3_grp_t *g, const fpga_v3_rec_t *r, uint32_t clk_mhz, fpga_v3_win_t *out);

/* jednotka casu [ps] pro clk_mhz (0 = 100 MHz) */
double   fpga_v3_unit_ps(uint32_t clk_mhz);

/* Selftest (na cili i hostu): CRC bloku, zaznam, MOSI/LUT_WRITE, emulator -> parser -> okno (kmitocet), vadny blok
 * (opakovani od ACK), preteceni kruhu, rozbaleni T0 pres 2^48. @return true = PASS */
bool     fpga_v3_selftest(void);
int      fpga_v3_selftest_fail_line(void);   /* radek prvni neuspesne kontroly (0 = PASS) */

#endif /* FPGA_V3_H */
