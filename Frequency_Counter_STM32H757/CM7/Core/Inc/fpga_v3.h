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
    fpga_v3_rec_t ring[FPGA_V3_EMU_RING];
} fpga_v3_emu_t;

void     fpga_v3_emu_init(fpga_v3_emu_t *e, double fa_hz, double fb_hz, double sigma_ps, uint32_t seed);
void     fpga_v3_emu_segment(fpga_v3_emu_t *e);              /* jeden segment = zaznam A + zaznam B */
void     fpga_v3_emu_frame(fpga_v3_emu_t *e, uint8_t *miso);  /* DATA ramec od rec_ack (jako RTL) */
void     fpga_v3_emu_rx(fpga_v3_emu_t *e, const uint8_t *mosi);   /* prijme ramec STM (ACK posune rec_ack) */

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
