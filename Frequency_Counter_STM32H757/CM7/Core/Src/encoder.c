/**
 * @file    encoder.c
 * @brief   Rotacni encoder + tlacitko (viz encoder.h pro smlouvu a zduvodneni).
 */

#include "encoder.h"
#include "gpio_guard.h"   /* gpio_cfg_lock — PA8/PA9/PC13 lezi na portech, kam sahá i CM4 (ETH) */
#include "main.h"
#include "tim.h"          /* htim1 — TIM1 konfiguruje `.ioc`/MX_TIM1_Init, viz encoder_init */
#include <string.h>

#define ENC_BTN_PORT   GPIOC
#define ENC_BTN_PIN    GPIO_PIN_13

/* 🔴 TIM1 v encoder modu 3 pocita OBE hrany OBOU kanalu = 4 kroky na jednu
 * zapadku bezneho detentovaneho encoderu. Kdyby se na HW ukazalo, ze jedna
 * zapadka dava 1 nebo 2 kroky, zmen to TADY a nikde jinde — cely zbytek UI
 * pracuje uz jen se zapadkami. Overeni: UART `enc` vypisuje SUROVE kroky. */
#define ENC_DIV_DEFAULT   4
static uint8_t s_div = ENC_DIV_DEFAULT;

#define ENC_LONG_MS          1000u   /* zadani UI §5: „Dlouhy stisk (1 s)" */
#define ENC_DOUBLE_MS         400u
#define ENC_DEBOUNCE_MS        20u
#define ENC_SPEED_WINDOW_MS   250u   /* okno pro `steps_per_s` */

static uint8_t  s_init;
static uint16_t s_last_cnt;
static int32_t  s_rem;            /* zbytek kroku, ktery jeste nedal celou zapadku */
static uint8_t  s_btn;            /* debouncovany stav (1 = stisknuto) */
static uint32_t s_btn_t;          /* cas posledni PRIJATE zmeny stavu */
static uint8_t  s_long_fired;     /* dlouhy stisk uz ohlasen -> uvolneni nedela short */
static uint32_t s_last_short_t;
static uint8_t  s_seen;
static uint32_t s_spd_t;
static int32_t  s_spd_acc;
static uint16_t s_spd;
static uint32_t s_ev_count;   /* monotonni: kolik udalosti modul vydal */
static int32_t  s_step_total;  /* monotonni: soucet zapadek */

void encoder_init(void)
{
    if (s_init) return;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();

    /* ── GPIO se nastavuje ZNOVU, a je to ZAMER, ne zbytek po F-0122 ───────────
     * `HAL_TIM_Encoder_MspInit()` v generovanem `tim.c` konfiguruje PA8/PA9 uplne
     * stejne (AF_PP, PULLUP, LOW, AF1_TIM1) — ale BEZ `gpio_cfg_lock()`.
     * A `GPIOA` je sdileny port: CM4 na nem ma ETH (PA1 REF_CLK, PA2 MDIO,
     * PA7 CRS_DV). `HAL_GPIO_Init` dela nad `MODER`/`AFR` NEATOMICKY
     * read-modify-write, takze ztraceny zapis jednoho jadra tise vrati cizi pin —
     * presne trida vady, ktera na `GPIOG` shodila displej (PG8) i sit (PG11).
     * Tohle opakovane nastaveni je tedy jedina verze, ktera je proti tomu zavodu
     * chranena, a je idempotentni, takze bezi klidne po MspInitu.
     * ⚠️ NEODSTRANUJ to jako duplikaci. Duplikace je tu jen v hodnotach; jediny
     * vlastnik ZAPISU pod zamkem je tenhle modul. */
    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_8 | GPIO_PIN_9;      /* CH1 / CH2 */
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_PULLUP;                  /* encoder spina na zem */
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF1_TIM1;
    gpio_cfg_lock();
    HAL_GPIO_Init(GPIOA, &g);
    gpio_cfg_unlock();

    g.Pin = ENC_BTN_PIN; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLUP;
    g.Alternate = 0;
    gpio_cfg_lock();
    HAL_GPIO_Init(ENC_BTN_PORT, &g);
    gpio_cfg_unlock();

    /* ── TIM1 uz JE nakonfigurovany — z `.ioc` pres `MX_TIM1_Init()` ───────────
     * 🔴 Do 2026-09-19 tady byl blok SYROVYCH zapisu do `CR1`/`PSC`/`ARR`/`CCMR1`/
     * `CCER`/`SMCR`/`CNT`, ktery konfiguraci z `.ioc` prepsal — a protoze
     * `encoder_init()` bezi az z UiTasku, tedy PO `MX_TIM1_Init()` v `main()`,
     * rucni verze vzdy vyhrala. Byly to DVE konfigurace teze periferie, z nichz
     * jedna nikdy nenabyla ucinku (audit F-0122): kdo zmenil filtr nebo encoder
     * mod v CubeMX, nezmenil NIC a hledal proc. Existuje i commit `2f5c3db`,
     * ktery `.ioc` hodnoty "opravoval" v domneni, ze na nich zalezi.
     *
     * ✅ ZMERENO, ze jsou to tytez registry, takze prechod na HAL cestu neni zmena
     * chovani: `.ioc` dava `Prescaler=0` -> `PSC=0`, `Period=65535` -> `ARR=0xFFFF`,
     * `ENCODERMODE_TI12` -> `SMS=011` (mod 3, obe hrany obou kanalu),
     * `IC1Filter=IC2Filter=15` -> `IC1F=IC2F=15`, `ICSELECTION_DIRECTTI` ->
     * `CC1S=CC2S=01`. Jediny rozdil: `HAL_TIM_Encoder_Start` navic nastavi
     * `CC1E/CC2E`, ktere tu byly nulove. Pocitani to nemeni — encoder mod bere
     * `TI1FP1`/`TI2FP2` pres slave-mode controller a `CCxE` jen hradluje zapis do
     * `CCRx`, ktery necteme. ⚠️ Je to ale jediny rozdil, ktery se ma na HW overit
     * (UART `enc`: jedna zapadka = `kroku=1`).
     *
     * ⚠️ Filtr `ICxF = 15` (max) NENI kosmetika: mechanicke encodery zakmitavaji
     * a bez filtru by TIM pocital nesmysly. Ted ho drzi `.ioc` — pri praci
     * v CubeMX ho tam nechej. */
    if (htim1.Instance != TIM1) {
        /* L-0009: nespolehat na to, ze generovany kod probehl — overit dosazeny
         * stav. Kdyby regen vyhodil TIM1 z `.ioc`, `MX_TIM1_Init()` zmizi a tady
         * bychom startovali nenakonfigurovany timer. Radeji encoder NEZAPNOUT:
         * `s_init` zustane 0, `encoder_poll` hned vraci a UART `enc` neukaze ani
         * jeden krok — to je detekovatelne. Tichy pulfunkcni encoder by nebyl. */
        return;
    }
    __HAL_TIM_SET_COUNTER(&htim1, 0);
    if (HAL_TIM_Encoder_Start(&htim1, TIM_CHANNEL_ALL) != HAL_OK) return;  /* L-0003 */

    s_last_cnt = (uint16_t)TIM1->CNT;
    s_btn_t    = HAL_GetTick();
    s_spd_t    = s_btn_t;
    s_init     = 1;
}

void encoder_poll(encoder_ev_t *ev)
{
    if (ev == NULL) return;
    memset(ev, 0, sizeof *ev);
    if (!s_init) return;

    const uint32_t now = HAL_GetTick();

    /* ── Otaceni ──────────────────────────────────────────────────────────
     * Rozdil se pocita v uint16 a teprve pak se pretypuje na int16 — tim je
     * pretoceni citace (0xFFFF -> 0x0000) osetrene samo od sebe. */
    uint16_t c = (uint16_t)TIM1->CNT;
    int16_t  d = (int16_t)(uint16_t)(c - s_last_cnt);
    s_last_cnt = c;
    if (d != 0) {
        s_rem += d;
        /* ⚠️ Celociselne deleni v C utina k nule, takze zaporny zbytek funguje
         * spravne i pod nulou (-3/4 = 0, -5/4 = -1) a `s_rem` se nikdy neztrati. */
        int32_t det = s_rem / (int32_t)s_div;
        if (det != 0) {
            s_rem   -= det * (int32_t)s_div;
            ev->steps = (int16_t)det;
            s_seen    = 1;
        }
    }

    /* Rychlost otaceni pro adaptivni krok: zapadky za okno prepoctene na sekundu. */
    s_spd_acc += (ev->steps < 0) ? -ev->steps : ev->steps;
    if (now - s_spd_t >= ENC_SPEED_WINDOW_MS) {
        s_spd     = (uint16_t)((s_spd_acc * 1000) / (int32_t)(now - s_spd_t));
        s_spd_acc = 0;
        s_spd_t   = now;
    }
    ev->steps_per_s = s_spd;
    s_step_total += ev->steps;

    /* ── Tlacitko ─────────────────────────────────────────────────────────
     * Zmena stavu se prijme az `ENC_DEBOUNCE_MS` po te predchozi; `s_btn_t`
     * je tim padem zaroven cas zacatku drzeni. */
    uint8_t raw = (HAL_GPIO_ReadPin(ENC_BTN_PORT, ENC_BTN_PIN) == GPIO_PIN_RESET) ? 1u : 0u;
    if (raw != s_btn && (now - s_btn_t) >= ENC_DEBOUNCE_MS) {
        s_btn   = raw;
        s_btn_t = now;
        s_seen  = 1;
        if (raw) {
            s_long_fired = 0;                    /* zacatek stisku */
        } else if (!s_long_fired) {              /* uvolneni bez dlouheho stisku */
            ev->short_press = 1;
            if (now - s_last_short_t <= ENC_DOUBLE_MS) ev->double_click = 1;
            s_last_short_t = now;
        }
    }
    /* Dlouhy stisk se hlasi UZ BEHEM drzeni (ne az pri uvolneni), aby mel
     * uzivatel zpetnou vazbu v okamziku, kdy prah prekroci. */
    if (s_btn && !s_long_fired && (now - s_btn_t) >= ENC_LONG_MS) {
        s_long_fired   = 1;
        ev->long_press = 1;
    }
    if (ev->steps || ev->short_press || ev->long_press || ev->double_click) s_ev_count++;
}

void encoder_set_div(uint8_t d)
{
    if (d != 1u && d != 2u && d != 4u) return;   /* jina hodnota nedava smysl */
    s_div = d;
    s_rem = 0;                                    /* zbytek ze stareho delice zahodit */
}
uint8_t encoder_div(void) { return s_div; }

uint32_t encoder_event_count(void) { return s_ev_count; }
int32_t  encoder_step_total(void)  { return s_step_total; }

int encoder_seen(void) { return s_seen; }
