/* Host test hist_pyr.h (TODO #251): pyramida historie na CM4.
 * cc -std=c11 -O2 -Wall -Wextra -ICM7/Core/Inc tests/host/t_hist_pyr.c -lm -o /tmp/t_hist_pyr && /tmp/t_hist_pyr */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "hist_pyr.h"

static int fails;
#define CK(c) do { if (!(c)) { printf("FAIL radek %d: %s\n", __LINE__, #c); fails++; } } while (0)
static int near(double a, double b, double tol) { return fabs(a - b) <= tol; }

static hp_t H;                       /* 20 kB mimo zasobnik */

/* Mereni jednou za sekundu: f = 1e7 + (sec % 100) * 0.01 [Hz], teploty konstantni, Vc/VBAT dle sekundy. */
static void feed_second(hp_t *h, uint32_t sec)
{
    hp_feed(h, sec, 1700000000u + sec, 1e7 + (double)(sec % 60u) * 0.01, 4500, 3000, (uint16_t)(1900u + sec % 7u), 3000u);
}

int main(void)
{
    hp_reset(&H);
    CK(sizeof(hp_pt_t) == 28u);
    CK(sizeof(hp_t) < 23000u);

    /* 1) stupen 0 po jednom 10s okne */
    for (uint32_t s = 0; s < 35u; s++) feed_second(&H, s);          /* okna 0,1,2 uzavrena (2 jeste otevrene -> uzavrena az dalsim) */
    CK(H.cnt[0] == 3u);                                             /* okna 0..2 uzavrena, okno 3 otevrene (s = 30..34) */
    {
        const hp_pt_t *p = hp_at(&H, 0, 0);                         /* nejnovejsi uzavrene = okno 2 (sec 20..29) */
        CK(p && p->t == 1700000020u);
        CK(p && near(hp_get_f(p), 1e7 + 0.245, 1e-9));              /* prumer z (20..29)*0.01 = 0.245 */
        CK(p && near(p->dmin, -0.045, 1e-4) && near(p->dmax, 0.045, 1e-4));
        CK(p && p->to == 4500 && p->tb == 3000 && p->vbat == 3000u);
    }

    /* 2) kaskada: po 60 s vznikne bod stupne 1 s prumerem a obalkou */
    hp_reset(&H);
    for (uint32_t s = 0; s < 130u; s++) feed_second(&H, s);          /* okna 0..11 uzavrena, 12 otevrene */
    CK(H.cnt[0] == 12u && H.cnt[1] == 2u);                          /* 2 minuty = 2 body stupne 1 */
    {
        const hp_pt_t *p = hp_at(&H, 1, 1);                         /* starsi minuta: sec 0..59 */
        CK(p && p->t == 1700000000u);
        CK(p && near(hp_get_f(p), 1e7 + 0.295, 1e-9));              /* prumer (0..59)*0.01 */
        CK(p && near(p->dmin, -0.295, 1e-4) && near(p->dmax, 0.295, 1e-4));
    }

    /* 3) delka historie: 2 dny souvisle -> vsechny stupne naplneny podle periody a kruh se otaci */
    hp_reset(&H);
    for (uint32_t s = 0; s < 2u * 86400u; s++) feed_second(&H, s);
    CK(H.cnt[0] == HP_NS[0] && H.cnt[1] == HP_NS[1] && H.cnt[2] == HP_NS[2]);   /* kruhy plne */
    CK(H.cnt[3] == 2u * 86400u / 2160u - 1u || H.cnt[3] == 2u * 86400u / 2160u - 2u);   /* 79 / 78 */
    CK(H.cnt[4] == 13u || H.cnt[4] == 12u);                          /* 2 dny / 3,6 h */
    CK(hp_span_s(&H) >= 78u * 2160u);

    /* 4) dotaz: posledni hodina po 60 s v 60 bodech; posledni den po 600 s v 140 bodech (jen kdyz pokryto) */
    hp_pt_t out[140];
    int n = hp_query(&H, 0u, 60u, 30u, out);
    CK(n == 30);
    CK(n == 30 && out[0].t > out[29].t);                            /* nejnovejsi prvni */
    CK(n == 30 && (out[0].t - out[1].t) == 60u);
    n = hp_query(&H, 0u, 450u, 48u, out);                           /* SPA 24 h: chunk 0, bod 450 s -> stupen 360 s */
    CK(n == 48);
    CK(n == 48 && out[0].t > out[47].t);
    n = hp_query(&H, 0u, 12960u, 10u, out);                         /* 10 * 3,6 h = 36 h: stupen 4 ma jen 13 bodu */
    CK(n == -1 || n == 10);
    n = hp_query(&H, 0u, 12960u, 140u, out);                        /* 21 dni: nepokryto */
    CK(n == -1);
    n = hp_query(&H, 0u, 5u, 10u, out);                             /* perioda pod 10 s nelze */
    CK(n == -1);
    n = hp_query(&H, 60u * 86400u, 60u, 10u, out);                  /* mimo historii */
    CK(n == -1);
    n = hp_query(&H, 0u, 60u, 0u, out);
    CK(n == -1);

    /* skladani skupin: perioda 120 s ze stupne 1 -> prumer dvou minut, min/max pres obe */
    hp_reset(&H);
    for (uint32_t s = 0; s < 400u; s++) feed_second(&H, s);          /* 6 minut uzavrenych (okna 0..38 -> 6 minut) */
    n = hp_query(&H, 0u, 120u, 2u, out);
    CK(n == 2);
    if (n == 2) {
        CK(near(hp_get_f(&out[0]), 1e7 + 0.295, 1e-6));             /* kazda minuta ma stejny prumer, tedy i skupina */
        CK(out[0].t > out[1].t && out[0].t - out[1].t == 120u);
        CK(near(out[0].dmin, -0.295, 1e-4) && near(out[0].dmax, 0.295, 1e-4));
    }

    {   /* poloha skupiny: t skupiny je zacatek NEJSTARSIHO bodu skupiny; from_s posouva zacatek dotazu */
        const hp_pt_t *a = hp_at(&H, 1, 0), *b = hp_at(&H, 1, 1), *c = hp_at(&H, 1, 2);
        n = hp_query(&H, 0u, 120u, 2u, out);
        CK(n == 2 && a && b && out[0].t == b->t);
        n = hp_query(&H, 60u, 60u, 2u, out);                        /* preskoc nejnovejsi minutu */
        CK(n == 2 && b && c && out[0].t == b->t && out[1].t == c->t);
    }
    {   /* hranice pokryti: s presne N uzavrenymi body stupne 1 jde pozadat o N (po hranici), o N+1 ne */
        hp_t *q = (hp_t *)malloc(sizeof *q);
        hp_reset(q);
        for (uint32_t s = 0; s < 6u * 60u + 5u; s++) feed_second(q, s);   /* uzavrenych minut: 6 */
        hp_pt_t o2[32];
        CK(q->cnt[1] == 6u);
        CK(hp_query(q, 0u, 60u, 5u, o2) == 5);                     /* need = 5 -> 5 + 1 <= 6 */
        CK(hp_query(q, 0u, 60u, 6u, o2) == -1);                    /* need = 6 -> 7 > 6: radeji datalog nez neuplna rada */
        free(q);
    }

    /* 5) diry: chybejici 10s okno -> reset historie */
    hp_reset(&H);
    for (uint32_t s = 0; s < 100u; s++) feed_second(&H, s);
    CK(H.cnt[0] >= 8u);
    feed_second(&H, 140u);                                          /* preskoceno okno 10..13 -> dira */
    CK(H.cnt[0] == 0u && H.cnt[1] == 0u);
    for (uint32_t s = 141u; s < 200u; s++) feed_second(&H, s);
    CK(H.cnt[0] >= 4u);

    hp_reset(&H);                                                   /* chybi PRAVE jedno okno (bucket 3): uz je to dira */
    for (uint32_t s = 0; s < 30u; s++) feed_second(&H, s);          /* okna 0,1 uzavrena, 2 otevrene */
    CK(H.cnt[0] == 2u);
    feed_second(&H, 40u);                                           /* okno 4 (3 chybi) */
    CK(H.cnt[0] == 0u);

    /* 6) jedno vynechane okno (bez mereni 10 s) NENI dira: 1 okno navic dovoleno? (b == cur + 1 -> v poradku, jinak reset) */
    hp_reset(&H);
    for (uint32_t s = 0; s < 30u; s++) feed_second(&H, s);          /* okna 0,1 uzavrena, 2 otevrene */
    feed_second(&H, 30u);                                           /* okno 3 sousedi s 2 */
    CK(H.cnt[0] == 3u);

    /* 7) neplatne teploty a chybejici Vc/VBAT se nepocitaji do prumeru */
    hp_reset(&H);
    for (uint32_t s = 0; s < 25u; s++) hp_feed(&H, s, 0u, 5e6, HP_EMPTY16, 2500, 0u, 0u);
    {
        const hp_pt_t *p = hp_at(&H, 0, 0);
        CK(p && p->to == HP_EMPTY16 && p->tb == 2500 && p->vc == 0u && p->vbat == 0u && p->t == 0u);
    }

    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
