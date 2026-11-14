/* hist_pyr.h - decimovana historie v RAM CM4 pro web (/api/log), TODO #251, v0.42.0. Cista logika bez zavislosti na HAL/lwIP,
 * sdilena zdrojem pro CM4 a testovana na PC (tests/host/t_hist_pyr.c).
 *
 * Princip: CM4 plni z kazdeho NOVEHO mereni ve snapshotu (kmitocet, teploty OCXO a desky, Vc, VBAT) pyramidu o HP_STAGES stupnich.
 * Stupen 0 = 10 s (stejne jako datalog), dalsi stupne jsou hruby soucet predchozich (periody HP_PERIOD_S). Kazdy bod nese prumer,
 * min/max kmitoctu (obalka) a prumery ostatnich velicin. Odpoved /api/log se pak da sestavit z RAM okamzite; kdyz pyramida pozadovane
 * obdobi nepokryva (po restartu CM4, po resetu historie), `hp_query` vrati -1 a handler pouzije puvodni cestu pres CM7/datalog.
 *
 * Historie NESMI prezit jako „pravda“ pres zmenu zdroje: volajici zavola `hp_reset` pri prechodu REAL<->SIM, pri dalsi inicializaci CM7
 * (CM7 mrtvy -> zivy) a `hp_feed` sam resetuje, kdyz se mezi dvema dodavkami vynechalo vic nez jedno 10s okno (diry v rade by posunuly
 * casovou osu bodu, protoze poloha bodu se odvozuje z poradi). */
#ifndef HIST_PYR_H
#define HIST_PYR_H

#include <stdint.h>
#include <string.h>

#define HP_STAGES   5u
/* Pocet bodu na stupen: stupne 0 a 1 jsou male (kratka okna jsou v prohlizeci), 2..4 jsou dimenzovane na okna, ktera SPA nacita z
 * datalogu: 24 h po 450 s (chunk 6 h, bod 450 s -> stupen 360 s: 250 bodu = 25 h), 7 d po 3150 s (stupen 2160 s: 280 bodu = 7 d),
 * 30 d po 13500 s (stupen 12960 s: 200 bodu = 30 d). Celkem 778 bodu = 21,8 kB RAM. */
static const uint16_t HP_NS[HP_STAGES]  = { 16u, 32u, 250u, 280u, 200u };
static const uint16_t HP_OFF[HP_STAGES] = { 0u, 16u, 48u, 298u, 578u };      /* zacatky stupnu v jednom poli */
#define HP_TOTAL    778u
#define HP_EMPTY16  ((int16_t)0x8000)                /* teplota nezaznamenana */

/* Perioda bodu jednotlivych stupnu [s] a nasobek vuci predchozimu stupni. Pokryti stupne = HP_NS * perioda:
 * 2,7 min, 32 min, 25 h, 7 d, 30 d. */
static const uint32_t HP_PERIOD_S[HP_STAGES] = { 10u, 60u, 360u, 2160u, 12960u };
static const uint8_t  HP_FACTOR[HP_STAGES]   = { 0u, 6u, 6u, 6u, 6u };

typedef struct {
    uint32_t t;            /* UTC [s] zacatku bodu; 0 = RTC nesrovnan */
    uint32_t fw[2];        /* prumer kmitoctu [Hz], double ulozeny jako dve slova (zadne 8B zarovnani na CM4) */
    float    dmin, dmax;   /* min/max kmitoctu relativne k prumeru [Hz] */
    int16_t  to, tb;       /* teplota OCXO / desky [0,01 C]; HP_EMPTY16 = neni */
    uint16_t vc, vbat;     /* Vc [mV], VBAT [mV]; 0 = neni */
} hp_pt_t;                 /* 28 B */

/* Akumulator jednoho bodu (pro stupen 0 plneny z mereni, pro vyssi stupne z uzavrenych bodu nizsiho stupne). */
typedef struct {
    uint32_t t;            /* t prvniho prispevku */
    uint32_t n;            /* pocet prispevku */
    double   fsum, fmin, fmax;
    int32_t  tosum, tbsum;  uint32_t nto, ntb;
    uint32_t vcsum, vbsum;  uint32_t nvc, nvb;
} hp_acc_t;

typedef struct {
    hp_pt_t  ring[HP_TOTAL];       /* stupen s zacina na ring + HP_OFF[s], ma HP_NS[s] bodu */
    uint16_t cnt[HP_STAGES];        /* kolik platnych bodu ve stupni */
    uint16_t head[HP_STAGES];       /* index PRISTIHO zapisu */
    hp_acc_t acc[HP_STAGES];        /* acc[0] = otevrene 10s okno; acc[s>0] = skladani bodu stupne s-1 */
    uint32_t cur_bucket;            /* cislo otevreneho 10s okna (sec / 10) */
    uint8_t  open;                  /* 1 = acc[0] ma data */
} hp_t;

static inline double hp_get_f(const hp_pt_t *p) { double d; memcpy(&d, p->fw, sizeof d); return d; }
static inline void   hp_set_f(hp_pt_t *p, double d) { memcpy(p->fw, &d, sizeof d); }

static inline void hp_acc_clear(hp_acc_t *a) { memset(a, 0, sizeof *a); }

static inline void hp_reset(hp_t *h) { memset(h, 0, sizeof *h); }

/* Pridani jednoho mereni do akumulatoru. */
static inline void hp_acc_add(hp_acc_t *a, uint32_t t, double f, double fmin, double fmax, int16_t to, int16_t tb,
                              uint16_t vc, uint16_t vbat, uint32_t weight)
{
    if (a->n == 0u) { a->t = t; a->fmin = fmin; a->fmax = fmax; }
    else { if (fmin < a->fmin) a->fmin = fmin; if (fmax > a->fmax) a->fmax = fmax; }
    a->fsum += f * (double)weight;
    a->n    += weight;
    if (to != HP_EMPTY16) { a->tosum += (int32_t)to * (int32_t)weight; a->nto += weight; }
    if (tb != HP_EMPTY16) { a->tbsum += (int32_t)tb * (int32_t)weight; a->ntb += weight; }
    if (vc)   { a->vcsum += (uint32_t)vc   * weight; a->nvc += weight; }
    if (vbat) { a->vbsum += (uint32_t)vbat * weight; a->nvb += weight; }
}

static inline void hp_acc_to_pt(const hp_acc_t *a, hp_pt_t *p)
{
    double avg = (a->n > 0u) ? a->fsum / (double)a->n : 0.0;
    p->t = a->t;
    hp_set_f(p, avg);
    p->dmin = (float)(a->fmin - avg);
    p->dmax = (float)(a->fmax - avg);
    p->to   = a->nto ? (int16_t)(a->tosum / (int32_t)a->nto) : HP_EMPTY16;
    p->tb   = a->ntb ? (int16_t)(a->tbsum / (int32_t)a->ntb) : HP_EMPTY16;
    p->vc   = a->nvc ? (uint16_t)(a->vcsum / a->nvc) : 0u;
    p->vbat = a->nvb ? (uint16_t)(a->vbsum / a->nvb) : 0u;
}

static inline void hp_push_stage(hp_t *h, unsigned s, const hp_pt_t *p);

/* Uzavre otevrene 10s okno do stupne 0 (a kaskadou do vyssich). */
static inline void hp_close_bucket(hp_t *h)
{
    if (!h->open) return;
    hp_pt_t p;
    hp_acc_to_pt(&h->acc[0], &p);
    hp_acc_clear(&h->acc[0]);
    h->open = 0u;
    hp_push_stage(h, 0u, &p);
}

/* Zapise uzavreny bod do stupne `s` a prida ho do akumulatoru stupne s+1; kdyz je akumulator plny, uzavre bod vyssiho stupne. */
static inline void hp_push_stage(hp_t *h, unsigned s, const hp_pt_t *p)
{
    h->ring[HP_OFF[s] + h->head[s]] = *p;
    h->head[s] = (uint16_t)((h->head[s] + 1u) % HP_NS[s]);
    if (h->cnt[s] < HP_NS[s]) h->cnt[s]++;
    if (s + 1u >= HP_STAGES) return;
    hp_acc_t *a = &h->acc[s + 1u];
    double f = hp_get_f(p);
    hp_acc_add(a, p->t, f, f + (double)p->dmin, f + (double)p->dmax, p->to, p->tb, p->vc, p->vbat, 1u);
    if (a->n >= HP_FACTOR[s + 1u]) {
        hp_pt_t q;
        hp_acc_to_pt(a, &q);
        hp_acc_clear(a);
        hp_push_stage(h, s + 1u, &q);
    }
}

/* Jedno NOVE mereni. `sec` = monotonni cas v sekundach (napr. uptime CM4), `t_unix` = UTC nebo 0. Vynechane okno => reset historie. */
static inline void hp_feed(hp_t *h, uint32_t sec, uint32_t t_unix, double f_hz, int16_t to, int16_t tb, uint16_t vc, uint16_t vbat)
{
    uint32_t b = sec / HP_PERIOD_S[0];
    if (h->open && b != h->cur_bucket) {
        hp_close_bucket(h);
        if (b > h->cur_bucket + 1u) hp_reset(h);          /* diru v rade nelze zalepit: poloha bodu = poradi */
    } else if (!h->open && h->cnt[0] > 0u && b > h->cur_bucket + 1u) {
        hp_reset(h);
    }
    h->cur_bucket = b;
    h->open = 1u;
    hp_acc_add(&h->acc[0], t_unix, f_hz, f_hz, f_hz, to, tb, vc, vbat, 1u);
}

/* Kolik sekund dozadu historie sahá (podle nejvyssiho plneho stupne). */
static inline uint32_t hp_span_s(const hp_t *h)
{
    uint32_t best = 0u;
    for (unsigned s = 0; s < HP_STAGES; s++) {
        uint32_t v = (uint32_t)h->cnt[s] * HP_PERIOD_S[s];
        if (v > best) best = v;
    }
    return best;
}

/* i-ty nejnovejsi uzavreny bod stupne s (0 = nejnovejsi); NULL, kdyz neexistuje. */
static inline const hp_pt_t *hp_at(const hp_t *h, unsigned s, unsigned i)
{
    if (s >= HP_STAGES || i >= h->cnt[s]) return NULL;
    unsigned idx = (unsigned)((h->head[s] + HP_NS[s] - 1u - i) % HP_NS[s]);
    return &h->ring[HP_OFF[s] + idx];
}

/* Sestavi do `out[0..n_max-1]` body od nejnovejsiho po nejstarsi, kazdy o delce `period_s` (>= 10 s), pocinaje bodem, ktery konci
 * `from_s` sekund pred koncem posledniho uzavreneho okna. Vraci pocet bodu (>= 1), nebo -1, kdyz pyramida pozadovane obdobi
 * (n_max bodu * period_s) nepokryva a ma se pouzit puvodni cesta pres datalog. Volba stupne: nejhrubsi stupen s periodou <= period_s,
 * ktery obdobi pokryva; body se sloucuji po g = period_s / perioda_stupne (zaokrouhleno dolu, min. 1). */
static inline int hp_query(const hp_t *h, uint32_t from_s, uint32_t period_s, unsigned n_max, hp_pt_t *out)
{
    if (n_max == 0u || period_s < HP_PERIOD_S[0]) return -1;
    int stage = -1;
    for (int s = (int)HP_STAGES - 1; s >= 0; s--) {
        if (HP_PERIOD_S[s] > period_s) continue;
        uint32_t g = period_s / HP_PERIOD_S[s];
        uint64_t need = ((uint64_t)from_s + (uint64_t)n_max * period_s) / HP_PERIOD_S[s];   /* bodu stupne od nejnovejsiho */
        (void)g;
        if (need + 1u <= h->cnt[s]) { stage = s; break; }
    }
    if (stage < 0) return -1;
    uint32_t P = HP_PERIOD_S[stage];
    uint32_t g = period_s / P;
    if (g == 0u) g = 1u;
    unsigned first = from_s / P;
    int n = 0;
    for (unsigned k = 0; k < n_max; k++) {
        hp_acc_t a; hp_acc_clear(&a);
        unsigned got = 0;
        for (unsigned j = 0; j < g; j++) {
            const hp_pt_t *p = hp_at(h, (unsigned)stage, first + k * g + j);
            if (!p) break;
            double f = hp_get_f(p);
            /* nejstarsi bod skupiny urcuje t (akumulator bere t prvniho prispevku, proto prochazime od nejnovejsiho a t prepisujeme) */
            hp_acc_add(&a, p->t, f, f + (double)p->dmin, f + (double)p->dmax, p->to, p->tb, p->vc, p->vbat, 1u);
            a.t = p->t;
            got++;
        }
        if (got == 0u) break;
        hp_acc_to_pt(&a, &out[n++]);
    }
    return n > 0 ? n : -1;
}

#endif /* HIST_PYR_H */
