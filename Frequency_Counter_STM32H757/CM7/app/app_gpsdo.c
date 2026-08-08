/**
 * @file app_gpsdo.c
 * @brief Glue: init libprim+HAL, drive the main and diagnostics screens, and
 *        route touch to the MENU / back buttons. Single-context (UiTask).
 */

#include "app_gpsdo.h"
#include "anim.h"                /* anim_t/anim_reset/anim_set/anim_step — sdileno se screen_main.c */
#include "screens/screen_main.h"
#include "hal/stm32/prim_stm32_hal.h"
#include "sensor_stat.h"        /* g_sensors[] (hodnota + valid + statistika) */
#include "fx_flags.h"           /* g_fx_enabled + FX_* — graficke efekty (OCXO budik, holdover kuzel, spektrogram) */
#include "gps.h"                /* gps_get() — zive GPS data do GNSS okna */
#include "w25q.h"               /* w25q_read_jedec — externi flash v okne PAMET */
#include "w25q_map.h"           /* W25Q_DATA_BASE/SIZE — okno Datalog */
#include "datalog.h"            /* datalog_get_status/set_enabled — okno Datalog */
#include "sensor_hist.h"        /* sensor_hist_series — RAM historie pro okno Grafy (#31) */
#include "alarm.h"              /* g_alarm_* pocitadla — okno Alarmy */
#include "fpga_freq.h"          /* fpga_freq_get_last/format_val — okno Citac */
#include "calib.h"              /* g_calib, calib_load/save — okno Kalibrace */
#include "meas_math.h"          /* g_meas_cfg + Math/limity (#43/#44) — okno MATH */
#include "meas_present.h"       /* perioda/jednotky/nominal/statistika/TFOM (#67) — okno MERENI */
#include "setup.h"              /* uloz/nacti sestavu — okno SESTAVY (s_view=33) */
#include "autocal.h"            /* autokalibrace / self-check — tlacitko AUTO-CAL v Kalibraci */
#include "syscfg.h"             /* syscfg_load — nastaveni z W25Q flash (prezije power-cycle) */
#include "cmsis_os2.h"          /* osThreadGetStackSpace (volny stack tasku) */
#include "FreeRTOS.h"           /* taskENTER_CRITICAL — atomicka publikace g_survey_* (S2) */
#include "task.h"
#include <prim/prim.h>
#include <ui/ui.h>
#include <stdio.h>
#include <string.h>
#include <math.h>    /* sinf/cosf — sky plot druzic (GPS okno) */
#include "version.h" /* FW_VERSION_FULL — okno "O pristroji" + splash (== UART) */

/* Firmware sensor globals (defined in freertos.c). */
extern volatile char    g_spi_text[64];          /* FPGA SPI status line */
extern volatile uint8_t g_spi_ok;                /* 1 = link alive */
extern volatile char    g_freq_info[64];         /* FPGA quality: gate/PH/SEQ line */
extern volatile uint8_t g_si5356_status;         /* reg 218: SYS_CAL/LOS_CLKIN/PLL_LOL */
extern volatile uint8_t g_si5356_ok;             /* 1 = status read OK */
extern volatile uint32_t g_rtos_heap_free;       /* free heap [B] */
extern volatile uint32_t g_rtos_heap_min;        /* min-ever-free heap [B] */
extern volatile uint32_t g_rtos_cpu_pct;         /* CPU load [%] */
extern volatile uint32_t g_uptime_s;             /* uptime [s] */
extern volatile char     g_rtc_text[24];         /* "YYYY-MM-DD HH:MM:SS" (RTC z LSE, sync z GPS) — UTC */
extern volatile char     g_rtc_text_local[24];   /* totez v lokalni zone (screensaver, okno Cas) */
extern volatile char     g_tz_label[8];          /* "UTC"/"UTC+2"/"CET"/"CEST" (pise rtc_app_tick) */
extern volatile int8_t   g_tz_offset_h;          /* casova zona -12..+14 h (okno Cas) */
extern volatile uint8_t  g_tz_auto;              /* 1 = AUTO CET/CEST (EU pravidlo) */
int rtc_cest_active(uint16_t y, uint8_t month, uint8_t day, uint8_t hour_utc); /* rtc.h (cista fce) */
extern volatile uint8_t  g_rtc_synced;           /* 1 = RTC srovnan z GPS */
extern volatile uint8_t  g_brightness;           /* jas displeje 0-255 (okno Nastaveni) */
extern volatile uint8_t  g_sound_muted;          /* 1 = zvuk vypnut */
extern volatile uint8_t  g_autodim_en;           /* 1 = auto-dim po necinnosti */
extern volatile uint16_t g_autodim_sec;          /* prodleva auto-dim [s] (preset 15..600) */
extern volatile uint8_t  g_theme_light;          /* 0 = tmave schema, 1 = svetle */
extern volatile uint8_t  g_lang_en;              /* 0 = cesky, 1 = english (texty postupne) */
/* g_anim_enabled je deklarovan v anim.h (sdileno se screen_main.c). */
extern volatile uint8_t  g_sys_cfg_dirty;        /* 1 = zmena jas/mute/dim -> persist do BKP */
/* Ulozeny vysledek self-survey (persist syscfg flash; survey_stop plni). */
extern volatile uint8_t  g_survey_valid;
extern volatile uint32_t g_survey_n;
extern volatile double   g_survey_lat, g_survey_lon;
extern volatile float    g_survey_alt, g_survey_spread;
extern volatile char     g_reset_text[12];       /* pricina posledniho resetu (main.c) */
extern volatile uint8_t  g_reset_bad;            /* 1 = watchdog reset (cervene) */
extern volatile char     g_crash_text[16];       /* crash black-box z BKP ("stack:UiTask") */
extern volatile uint8_t  g_selftest_res;         /* boot selftest: 0=--- 1=PASS 2=FAIL */
extern volatile uint8_t  g_selftest_detail[12];  /* per-test vysledky (poradi viz freertos_shared.h; drz = SELFTEST_N=12) */
extern volatile uint8_t  g_freq_stale;           /* 1 = ztrata signalu / mrtvy link (okno Citac) */
extern volatile uint8_t  g_cm4_absent;           /* 1 = CM4 (D2) nenabehl pri bootu */
int run_selftests(void);                         /* pure-logic testy — okno Selftest (SPUSTIT) */

/* FreeRTOS task handles (defined in freertos.c) — pro volny stack v System Health. */
extern osThreadId_t UiTaskHandle, FpgaTaskHandle, UartTaskHandle,
                    I2C4TaskHandle, defaultTaskHandle;

/* Linker symboly (adresy) pro vyuziti interni FLASH/RAM v okne PAMET. */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;

/* Si5356 status bits (reg 218 / 0xDA) — bitova mapa OVERENA z AN565 (drivejsi
 * bit2=LOS_CLKIN byla chyba). bit2 = LOS_XTAL: krystalovy vstup XA/XB je na
 * teto desce uzemneny (bez krystalu, 10 MHz jde do CLKIN pin 4) -> bit2 je
 * TRVALE 1 a IGNORUJE se. bit3 = skutecny LOS_CLKIN. ⚠️ PLL_LOL se pri
 * fyzicke ztrate vstupu NEasertuje (AN565: LOL = rozdil >5000 ppm na PFD)
 * -> ztratu 10 MHz reference hlasi prave LOS_CLKIN (bit3) = cervena. */
#define SI5356_SYS_CAL    (1u << 0)
#define SI5356_LOS_XTAL   (1u << 2)   /* bez krystalu trvale 1 — nehodnotit */
#define SI5356_LOS_CLKIN  (1u << 3)   /* ztrata 10 MHz na CLKIN (pin 4) */
#define SI5356_PLL_LOL    (1u << 4)

static prim_fb_t s_fb;
static int s_inited = 0;
static int s_view = 0;          /* 0=main 1=diag 2=gps ... 23=allan — plny seznam oken v CLAUDE.md */

/* Present coalescing: vysokofrekvencni ticky (clock/signal/freq) jen renderuji a
 * nastavi s_dirty; jeden flip pak udela app_gpsdo_flush() (UiTask ho vola na ~30Hz
 * gate). Snizi pocet VBR flipu + sjednoti copy-forward. Vzacne udalosti (touch,
 * prepnuti obrazovky, render/clear) prezentuji hned pres present_now(). */
static int s_dirty = 0;
static void present_now(void) { prim_stm32_present(); s_dirty = 0; }

/* Forward decl — goto_view/nav i touch je potrebuji pred definici nize. */
static void app_gpsdo_render_anim(void);       /* Animace/prepinace (s_view=24) */
static void app_gpsdo_render_animdemo(void);   /* Prehled vsech animaci ve smycce (s_view=25) */

/* ── Flash tlacitka/pilulky pri stisku (2px accent OBRYS pres prvek) ──────────
 * `rad` = zaobleni dle prvku (UI_DIM_BUTTON_RADIUS tlacitko / UI_DIM_PILL_RADIUS
 * pilulka). Obrys NEzakryva text — kresli se PRES uz vykresleny prvek (popisek
 * zustava), takze netreba label ani re-render obsahu. Neblokuje: nakresli + flipne,
 * akce volajiciho hned potom prekresli + flipne. Diky double-buffered vsync flipu
 * (present_now ceka na dokonceni PREDCHOZIHO) se obrys zobrazi 1 frame (~17 ms =
 * page-flip floor, mene fyzicky nejde).
 * ⚠️ VOLA SE JEN u in-place prvku, ktere po stisku ZUSTANOU (anim/cas/trend
 * toggly; RUN/GATE/CHAN maji vlastni flash). U NAVIGACNICH tlacitek/pilulek se
 * ZAMERNE nevola — tam by ten 1 frame navic jen zdrzoval prepnuti okna (2026-07-22,
 * odezva = sama zmena obrazovky). Obrys jde mimo dirty-rect copy-forward -> volat
 * jen pred akci, ktera dotceny rect prekresli. Gate g_anim_enabled. */
static void tap_flash_r(prim_rect_t r, int16_t rad)
{
    if (!g_anim_enabled || r.w <= 6 || r.h <= 6) return;
    prim_set_target(&s_fb);
    prim_reset_clip();
    prim_stroke_rect_rounded(r, rad, 2, UI_COLOR_ACC);
    present_now();
}
static inline void tap_flash(prim_rect_t r)      { tap_flash_r(r, UI_DIM_BUTTON_RADIUS); }
static inline void tap_flash_pill(prim_rect_t r) { tap_flash_r(r, UI_DIM_PILL_RADIUS); }

/* ── Spolecny prolog okna (TODO #12/A1) ──────────────────────────────────────
 * `app_gpsdo_init(); prim_set_target(&s_fb); prim_reset_clip();` byl doslova
 * zkopirovany v 24 render funkcich (radove dalsich ~70 radku) — kazda kopie
 * byla dalsi misto, kde se mohl rozejit init/target/clip. window_prep() to
 * sjednocuje; window_first(N) navic vyresi "je tohle prvni vstup do okna N?"
 * (zmena s_view) pro oken s live-redraw dispatchem (app_gpsdo_tick). Poradi
 * `s_view = N;` vuci prep() NEZALEZI (nezavisle stavy) — volajici ho muze
 * priradit pred i za window_prep()/window_first(), podle toho, co je citelnejsi. */
static void window_prep(void)
{
    app_gpsdo_init();
    prim_set_target(&s_fb);
    prim_reset_clip();
}
static int window_first(uint8_t view_id)
{
    window_prep();
    return (s_view != (int)view_id);
}

/* Back button on the diagnostics screen. */
/* Back button lives in the bottom bar, in the same slot as the main MENU. */
static const prim_rect_t BACK_RECT = {650, 417, 133, 61};
/* System Health footer = 4 tlacitka (2026-07-31 pridano GRAFY, #31): rozlozeni
 * SENZORY / DIAGNOSTIKA / NASTAVENI / GRAFY, sirky nestejne (DIAGNOSTIKA nejdelsi
 * label -> nejsirsi), vse pred BACK_RECT (x=650). */
static const prim_rect_t SENS_BTN_RECT = {18, 417, 140, 61};
static const prim_rect_t HEALTH_DIAG_BTN_RECT = {166, 417, 176, 61};
/* "GRAFY" tlacitko v System Health -> okno Grafy (casovy prubeh senzoru, s_view=29). */
static const prim_rect_t HEALTH_GRAPH_BTN_RECT = {498, 417, 138, 61};
/* SESTERSKA okna analyzy stability: ALLAN (s_view=23, log-log graf) <-> HISTOGRAM
 * (s_view=6, rozdeleni) <-> SPEKTROGRAM Δf (s_view=26). Prepina SDILENA ZALOZKA
 * (VIEW_TABS, segmented v patce vlevo) BEZ nav_push -> BACK z libovolneho vede
 * tam, odkud byla trojice otevrena (tap na Allan nahled na hl. obrazovce = main).
 * Patka: [zalozky vlevo] [ovladac konkretniho pohledu stred] [ZPET vpravo]. */
static const prim_rect_t VIEW_TABS_RECT = {18, 417, 300, 61};    /* zalozky ALLAN/HIST/SPEKTR */
static const char *const VIEW_TAB_LABELS[3] = {"ALLAN", "HIST", "SPEKTR"};
static const prim_rect_t LOGY_RECT       = {330, 417, 180, 61};  /* histogram: lin/log Y (footer stred) */
static const prim_rect_t ALLAN_METRIC_RECT = {330, 417, 300, 61}; /* allan: prepinac ADEV/TDEV/MTIE (footer stred) */
static const char *const ALLAN_METRIC_SEG[3] = {"ADEV", "TDEV", "MTIE"};

/* Sdilena zalozka (segmented) — vybrany = aktivni pohled (0=ALLAN 1=HIST 2=SPEKTR). */
static void view_tabs_render(int active)
{
    ui_segmented_t sc = {.rect = VIEW_TABS_RECT, .labels = VIEW_TAB_LABELS,
                         .n = 3, .selected = (uint8_t)active};
    ui_segmented_render(&sc);
}
/* Trend okno: RELATIVNI +/- casove okno (presety 10/20/30/60/120 s), hodnota mezi. */
static const prim_rect_t TREND_MINUS = {18, 417, 90, 61};
static const prim_rect_t TREND_PLUS  = {214, 417, 90, 61};
/* Presety casoveho okna trendu. ⚠️ int32_t (NE int16_t) — 60 dni = 5 184 000 s
 * by v int16 preteklo. Dlouha okna kresli decimacni pyramida (screen_main.c
 * trend_feed): krok se automaticky prizpusobi, u 30 d je ~18 h/vzorek. */
static const int32_t TREND_PRESETS[] = {
    60,                 /* 1 min  */
    600,                /* 10 min */
    3600,               /* 1 h    */
    21600,              /* 6 h    */
    86400,              /* 1 den  */
    604800,             /* 7 dni  */
    2592000,            /* 30 dni */
    5184000,            /* 60 dni */
};
#define TREND_PRESET_N ((int)(sizeof(TREND_PRESETS)/sizeof(TREND_PRESETS[0])))
static const prim_rect_t HIST_PLOT_RECT  = {26, 96, 540, 300};
static const prim_rect_t HIST_TABLE_RECT = {582, 100, 180, 292};
/* "NASTAVENI" tlacitko v System Health (footer, viz SENS_BTN_RECT skupina). */
static const prim_rect_t SET_BTN_RECT = {350, 417, 140, 61};
/* Footer Diagnostiky (hub pro technicka podokna): DIAGRAM | PAMET | SELFTEST.
 * Vse konci pred BACK_RECT (x=650) — footer pravidlo y>=416 patri tlacitkum. */
static const prim_rect_t DIAG_DIAGRAM_BTN_RECT = {18, 417, 160, 61};
static const prim_rect_t DIAG_MEM_BTN_RECT     = {190, 417, 150, 61};
static const prim_rect_t DIAG_ST_BTN_RECT      = {352, 417, 160, 61};
/* Ovladace v okne Nastaveni (2 sloupce jako diag): levy = Zvuk / Jas / Auto-dim,
 * pravy = Vzhled (schema) / Jazyk (+ rezerva na dalsi polozky).
 * ⚠️ TODO #11(1b) HOTOVO 2026-07-19: 56 px (6,6 mm) -> 64 px (7,5 mm, nad
 * doporucenym minimem 7 mm). MUTE/THEME/LANG mely v kartach dost rezervy
 * (button koncil 16-24 px pred spodnim okrajem karty) -> zmena JEN vysky
 * bez zasahu do karet c1/c4/c5. Jas (c2) a Auto-dim (c3) mely rezervu jen
 * 10-12 px -> karty c2/c3 se zvetsily o 8 px kazda (viz app_gpsdo_render_settings),
 * cerpano ze 42 px volneho prostoru pred paticnkou (bylo 368->410 px). */
static const prim_rect_t MUTE_RECT   = {230, 74, 148, 64};    /* Zvuk: zap/vyp */
static const prim_rect_t BR_MINUS    = {30, 188, 72, 64};     /* Jas - */
static const prim_rect_t BR_PLUS     = {110, 188, 72, 64};    /* Jas + */
static const prim_rect_t ADEN_RECT   = {30, 308, 140, 64};    /* Auto-dim zap/vyp */
static const prim_rect_t DIM_MINUS   = {186, 308, 56, 64};    /* prodleva - */
static const prim_rect_t DIM_PLUS    = {316, 308, 56, 64};    /* prodleva + */
static const prim_rect_t THEME_RECT  = {602, 74, 160, 64};    /* Vzhled: TMAVE/SVETLE */
static const prim_rect_t LANG_RECT   = {602, 166, 160, 64};   /* Jazyk: CESKY/ENGLISH */
/* Okno Cas (s_view=22, dlazdice v Menu): rezim AUTO CET/CEST vs rucni posun.
 * TODO #11(1b) HOTOVO: 56->64 px, vsude dost rezervy (viz komentare u volajicich). */
static const prim_rect_t TZ_AUTO_RECT = {30, 236, 200, 64};   /* AUTO <-> RUCNI */
static const prim_rect_t TZ_MINUS     = {30, 310, 72, 64};    /* rucni posun - */
static const prim_rect_t TZ_PLUS      = {250, 310, 72, 64};   /* rucni posun + */
static const prim_rect_t REF_RECT    = {410, 262, 372, 64};   /* Reference Si5356 (presunuto z Menu) */
static const prim_rect_t ABOUT_RECT  = {410, 340, 372, 64};   /* O pristroji (dolni pravy) */
static const prim_rect_t SETUP_ENTER_RECT = {18, 417, 200, 61};  /* Nastaveni footer -> okno SESTAVY */
/* Okno SESTAVY (s_view=33): vyber slotu (-/+) + ULOZIT/NACIST/SMAZAT ve footeru. */
static const prim_rect_t SET_SLOT_MINUS = {40, 116, 64, 64};
static const prim_rect_t SET_SLOT_PLUS  = {214, 116, 64, 64};
static const prim_rect_t SETUP_SAVE_RECT  = {18,  417, 150, 61};
static const prim_rect_t SETUP_LOAD_RECT  = {176, 417, 150, 61};
static const prim_rect_t SETUP_ERASE_RECT = {334, 417, 150, 61};

static bool in_rect(int16_t x, int16_t y, prim_rect_t r)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* ── Navigacni zasobnik (BACK se vraci k tomu, odkud okno OTEVRENO) ──────────
 * Pri kazdem forward prechodu se pushne aktualni s_view; BACK popne a vykresli.
 * Umoznuje "z okna oteviraneho z Menu -> BACK zpet do Menu" i vnoreni
 * (Menu->Nastaveni->O pristroji). app_gpsdo_render_main resetuje (koren). */
static uint8_t s_nav_stack[6];
static int     s_nav_sp = 0;
static void nav_push(uint8_t from) { if (s_nav_sp < 6) s_nav_stack[s_nav_sp++] = from; }
static void goto_view(uint8_t v)
{
    switch (v) {
    case 1:  app_gpsdo_render_diag();     break;   /* Diagnostika (spawnuje Komunikaci) */
    case 3:  app_gpsdo_render_health();   break;   /* Health (spawnuje senzory/pamet/nastaveni) */
    case 7:  app_gpsdo_render_settings(); break;   /* Nastaveni (spawnuje O pristroji) */
    case 12: app_gpsdo_render_menu();     break;   /* Menu rozcestnik */
    case 24: app_gpsdo_render_anim();     break;   /* Animace (spawnuje subokno prikladu) */
    default: app_gpsdo_render_main();     break;   /* koren */
    }
}
static void nav_back(void)
{
    uint8_t v = (s_nav_sp > 0) ? s_nav_stack[--s_nav_sp] : 0;
    goto_view(v);
}

void app_gpsdo_init(void)
{
    if (s_inited) return;
    /* Nastaveni z W25Q flash (prezije power-cycle) PRED tematem/renderem — pri
     * studenem startu (BKP smazana) je flash autoritativni pro jas/schema/zonu/... */
    syscfg_load();
    ui_theme_select(g_theme_light);   /* ulozene schema PRED prvnim renderem */
    prim_stm32_init(&s_fb);
    screen_main_init();
    calib_load();   /* W25Q CALIB store -> g_calib (blokujici, ~ms; prazdno = vychozi hodnoty) */
    setup_init();   /* W25Q SETUP store -> ulozene sestavy (okno SESTAVY) */
    s_inited = 1;
}

void app_gpsdo_render_main(void)
{
    window_prep();
    s_view = 0;
    s_nav_sp = 0;    /* hlavni obrazovka = koren navigace */
    screen_main_render();
    present_now();          /* flip hotovy snimek na displej (tearing-free) */
}

/* ── Diagnostics screen ─────────────────────────────────────── */

/* Rozklad float na (cele, des) bez %f (nano.specs), decimals 1..3. Ciste
 * cislo — sjednocuje aritmetiku ctyr drivejsich temer identickych kopii
 * (fmt_temp, fmt_d1, fmt_fN, fmt_minmax), viz TODO #12/A2. Vraci JEN cisla
 * (ne string): fmt_minmax kombinuje DVA vysledky do jednoho snprintf, stejne
 * jako puvodne — samostatny mezikrok "naformatuj do bufferu, pak %s%s slep"
 * by kaskadovite roztahl GCC -Wformat-truncation worst-case pres vic bufferu
 * (overeno pri prvnim pokusu: rostouci buf[24]->32 si vyzadalo i key[26]...). */
static void fixed_split(float v, int decimals, int32_t *whole, int32_t *frac)
{
    int32_t scale = 1;
    for (int i = 0; i < decimals; i++) scale *= 10;
    int32_t t = (int32_t)(v * (float)scale + (v >= 0.0f ? 0.5f : -0.5f));
    *whole = t / scale;
    *frac  = t % scale; if (*frac < 0) *frac = -*frac;
}

/* Jedna hodnota -> "cele.des" (decimals 1..3). ZADNA jednotka/sentinel navic
 * — to resi volajici (fmt_temp pridava " C", fmt_d1 ma "--" sentinel).
 * ⚠️ sgn: pro v v (-1;0) je cela cast 0 a %ld minus nevytiskne — bez explicitni
 * predpony by "-0.5" vyslo jako "0.5" (ZTRATA ZNAMENKA; pre-existujici chyba
 * vsech ctyr puvodnich fmt_* kopii, nalezena revizi 2026-07-19 — realne
 * zasahne zaporne teploty -0.99..-0.01 °C). */
static void fmt_fixed(char *buf, size_t n, float v, int decimals)
{
    int32_t w, f;
    fixed_split(v, decimals, &w, &f);
    const char *sgn = (v < 0.0f && w == 0 && f != 0) ? "-" : "";
    switch (decimals) {
    case 1: snprintf(buf, n, "%s%ld.%01ld", sgn, (long)w, (long)f); break;
    case 2: snprintf(buf, n, "%s%ld.%02ld", sgn, (long)w, (long)f); break;
    case 3: snprintf(buf, n, "%s%ld.%03ld", sgn, (long)w, (long)f); break;
    default: snprintf(buf, n, "%ld", (long)w); break;
    }
}

/* Teplota "23.45 C" (2 des. + jednotka). */
static void fmt_temp(char *buf, size_t n, float v)
{
    char num[16];
    fmt_fixed(num, sizeof num, v, 2);
    snprintf(buf, n, "%s C", num);
}

/* ── Dvousloupcový layout diagnostiky ──────────────────────────────────── */
#define DG_MX    18                              /* outer margin */
#define DG_GAP   12                              /* column gap */
#define DG_COLW  376                             /* column width */
#define DG_LX    DG_MX                           /* left column x */
#define DG_RX    (DG_MX + DG_COLW + DG_GAP)      /* right column x */
#define DG_LLBL  (DG_LX + 12)                    /* left col label x */
#define DG_RLBL  (DG_RX + 12)                    /* right col label x */
#define DG_LVAL  (DG_LX + DG_COLW - 14)          /* left col value right edge */
#define DG_RVAL  (DG_RX + DG_COLW - 14)          /* right col value right edge */

/* TODO #12/A4 (castecne): celoplosna karta jednoho okna existovala v 5 ruznych
 * variantach geometrie (y 58 vs 62, h 200..346) — spodni hrana kolisala 13-55 px
 * pred paticnkou bez zjevneho duvodu. Tyhle dve konstanty pojmenovavaji dve
 * SKUTECNE pouzivane (identicke) varianty (5+5 oken), aby se aspon nerozchazely
 * NAHODOU pri budouci uprave jednoho z nich. ⚠️ Zbyle vyjimky (About, Kalibrace,
 * Citac/Cas, Komunikace) maji vlastni obsah ruzne vysoky — sjednotit i je na
 * jednu spolecnou vysku by chtelo overit polohu radku v kazdem z nich zvlast
 * (riziko prekryvu); ponechano jako samostatny TODO. */
static const prim_rect_t DG_CARD_FULL_A = {DG_LX, 58, 764, 346};  /* Senzory/Pamet/Histogram/Allan/Trend */
static const prim_rect_t DG_CARD_FULL_B = {DG_LX, 62, 764, 300};  /* Reference/Holdover/Datalog/Alarmy/Selftest */
/* TODO #12/A4 dokonceno 2026-07-19: Citac + Cas mely stejnou geometrii, jen
 * duplikovanou (ne zamerne odlisnou jako zbyle vyjimky). O pristroji (2 karty
 * 200+130), Kalibrace (348) a Komunikace (320) zustavaji SAMOSTATNE — kazda
 * pouzita jen jednou, pojmenovani konstanty by tam nebylo DRY, jen navic
 * neduvod uvrstva (viz zasada "no premature abstraction"). Timhle je A4
 * uzavrena — vic skutecne duplicitni geometrie v souboru neni. */
static const prim_rect_t DG_CARD_FULL_C = {DG_LX, 62, 764, 340};  /* Citac/Cas */

/* GPS okno ma NEsymetricke sloupce: levy (Druzice: sky/bar prepinatelne dotykem)
 * je siroky, pravy (cas/poloha/timepulse/prijimac) zmenseny ~1/3. */
#define GPS_LX    DG_MX                           /* 18 */
#define GPS_LW    502                             /* levy sloupec (Druzice) — siroky */
#define GPS_LLBL  (GPS_LX + 12)                   /* 30 */
#define GPS_RX    532                             /* pravy sloupec x (right-aligned na 782) */
#define GPS_RW    250                             /* pravy sloupec sirka (~2/3 z 376) */
/* Tlacitko SURVEY (footer pravy sloupec, pred BACK@650) -> okno Self-survey (s_view=32). */
static const prim_rect_t GPS_SURVEY_BTN = {532, 417, 112, 61};
#define GPS_RLBL  (GPS_RX + 12)                   /* 544 */

/* GPS okno: karta Druzice — prepinani zobrazeni (bargraf <-> sky plot) dotykem.
 * Roztazena az po spodni okraj (160..478). */
static const prim_rect_t GPS_SAT_RECT = {GPS_LX, 160, GPS_LW, 318};
static bool s_gps_polar = false;   /* false = bargraf C/N0 (default), true = polarni sky plot */

/* A static label drawn once into the chrome (left, sans). */
/* Popisek (menu) řádku diagnostiky: STEJNÝ font jako živá hodnota (mono_18),
 * odlišený jen barvou — tlumená (INK_3) vs světlá hodnota (INK). */
static void dlabel(int16_t x, int16_t y, const char *s)
{
    prim_draw_text((prim_point_t){x, y}, s, &ui_font_mono_18, UI_COLOR_INK_3,
                   PRIM_ALIGN_LEFT);
}

/* ── Box-cisteny zivy text (TODO #12/A3) ─────────────────────────────────────
 * dval/dtext/dtext_c byly tri skoro identicke funkce lisici se jen zarovnanim
 * — a navic se rozchazely v geometrii boxu (dval `baseline-17`, ostatni dve
 * `baseline-16` — 1px nesrovnalost, objevena pri teto revizi). dtext_a je
 * spolecny zaklad (LEFT/CENTER/RIGHT), sjednoceny na `baseline-16`; dtext/
 * dtext_c jsou uz jen tenke obalky (volajici beze zmeny), dval zustava
 * vlastni specializovanou funkci (fixni font mono_18, barva dle `valid`,
 * "!" znacka pro neplatna data) postavenou nad stejnym zakladem. */
typedef enum { DTEXT_LEFT, DTEXT_CENTER, DTEXT_RIGHT } dtext_align_t;

static void dtext_a(int16_t pos, int16_t baseline, int16_t boxw, const char *v,
                    prim_color_t col, const prim_font_t *font, dtext_align_t align)
{
    int16_t box_x = (align == DTEXT_LEFT)   ? pos
                   : (align == DTEXT_CENTER) ? (int16_t)(pos - boxw / 2)
                   :                           (int16_t)(pos - boxw);
    prim_rect_t box = {box_x, (int16_t)(baseline - 16), boxw, 22};
    prim_fill_rect(box, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_set_clip(box);
    prim_align_t palign = (align == DTEXT_LEFT)   ? PRIM_ALIGN_LEFT
                         : (align == DTEXT_CENTER) ? PRIM_ALIGN_CENTER
                         :                            PRIM_ALIGN_RIGHT;
    prim_draw_text((prim_point_t){pos, baseline}, v, font, col, palign);
    prim_reset_clip();
}

/* Right-aligned live value: clear its box then redraw. valid==0 → dimmed + red "!". */
static void dval(int16_t xr, int16_t baseline, int16_t boxw, const char *v, int valid)
{
    dtext_a(xr, baseline, boxw, v, valid ? UI_COLOR_INK : UI_COLOR_INK_3,
           &ui_font_mono_18, DTEXT_RIGHT);
    if (!valid)
        prim_draw_text((prim_point_t){(int16_t)(xr - boxw + 2), baseline}, "!",
                       &ui_font_mono_18, UI_COLOR_BAD, PRIM_ALIGN_LEFT);
}

/* Left-aligned live text in a cleared box (status lines, colorized).
 * Text se OŘÍZNE na šířku boxu -> dlouhý řetězec (SPI stav, velké SEQ/CRC)
 * nepřeteče kartu. */
static void dtext(int16_t x, int16_t baseline, int16_t boxw, const char *v,
                  prim_color_t col, const prim_font_t *font)
{ dtext_a(x, baseline, boxw, v, col, font, DTEXT_LEFT); }

/* Center-aligned live text in a cleared box (GPS okno). */
static void dtext_c(int16_t cx, int16_t baseline, int16_t boxw, const char *v,
                    prim_color_t col, const prim_font_t *font)
{ dtext_a(cx, baseline, boxw, v, col, font, DTEXT_CENTER); }

/* ── Spolecna hlavicka okna: pozadi + BACK + nadpis ─────────────────────────
 * Tenhle blok byl doslova zkopirovany v 19 render funkcich — kazda kopie byla
 * dalsi misto, kde se mohly rozejit souradnice (viz historie oprav layoutu
 * 2026-07-18). Volajici pak uz jen dokresli sve karty/tlacitka.
 * title_y: WIN_TITLE_Y (38) pro vetsinu oken; okno Nastaveni ma hustsi layout
 * a nadpis o 4 px vys (WIN_TITLE_Y_TIGHT) — proto je to parametr, ne konstanta. */
#define WIN_TITLE_Y        38
#define WIN_TITLE_Y_TIGHT  34
static void window_chrome(const char *title, int16_t title_y)
{
    prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
              screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
    ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
    ui_button_render(&back);
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, title_y}, title,
                   &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
}

/* GPS souradnice -> "dd.ddddddH" (bez float v printf, integer extrakce). */
static void fmt_ll(float v, char pos, char neg, char *out, size_t n)
{
    char h = (v >= 0.0f) ? pos : neg;
    if (v < 0.0f) v = -v;
    long ud = (long)(v * 1000000.0f + 0.5f);
    snprintf(out, n, "%ld.%06ld%c", ud / 1000000, ud % 1000000, h);
}

/* float DOP/1-desetinne -> "1.7" (bez %f); "--" pro neplatne (<=0). Obracene
 * poradi argumentu (v,out,n) oproti fmt_fixed(buf,n,v,decimals) — puvodni
 * volani v draw_gps_values zustavaji beze zmeny. */
static void fmt_d1(float v, char *out, size_t n)
{
    if (v <= 0.0f) { snprintf(out, n, "--"); return; }
    fmt_fixed(out, n, v, 1);
}

/* Round a float reading to long without pulling in <math.h>. */
static long lround_f(float v) { return (long)(v >= 0.0f ? v + 0.5f : v - 0.5f); }

/* anim_t/anim_reset/anim_set/anim_step jsou v anim.h (sdileno se screen_main.c). */

/* Maidenhead lokator (10 znaku, 5 paru: field/square/subsquare/ext/ext2),
 * napr. "JN89NS85KN". Vstup = zem. sirka/delka [°]. Pary 3+5 velkymi pismeny. */
static void fmt_locator(float lat, float lon, char *out, size_t n)
{
    double lo = (double)lon + 180.0;   /* 0..360 */
    double la = (double)lat + 90.0;    /* 0..180 */
    if (lo < 0) lo = 0; else if (lo >= 360) lo = 359.999999;
    if (la < 0) la = 0; else if (la >= 180) la = 179.999999;
    char b[11];
    b[0] = (char)('A' + (int)(lo / 20.0));  b[1] = (char)('A' + (int)(la / 10.0));
    lo = fmod(lo, 20.0);  la = fmod(la, 10.0);
    b[2] = (char)('0' + (int)(lo / 2.0));   b[3] = (char)('0' + (int)(la / 1.0));
    lo = fmod(lo, 2.0);   la = fmod(la, 1.0);
    b[4] = (char)('A' + (int)(lo / (2.0 / 24.0)));  b[5] = (char)('A' + (int)(la / (1.0 / 24.0)));
    lo = fmod(lo, 2.0 / 24.0);   la = fmod(la, 1.0 / 24.0);
    b[6] = (char)('0' + (int)(lo / (2.0 / 240.0))); b[7] = (char)('0' + (int)(la / (1.0 / 240.0)));
    lo = fmod(lo, 2.0 / 240.0);  la = fmod(la, 1.0 / 240.0);
    b[8] = (char)('A' + (int)(lo / (2.0 / 5760.0)));b[9] = (char)('A' + (int)(la / (1.0 / 5760.0)));
    b[10] = '\0';
    snprintf(out, n, "%s", b);
}

/* Selftest cistych app helperu (Maidenhead lokator — nova, snadno chybova logika).
 * Bez sdileneho stavu -> bezpecne za behu; soucast UART "selftest"/boot selftestu. */
bool app_gpsdo_selftest(void)
{
    char loc[16]; int ok = 1;
    fmt_locator(49.52f, 17.55f, loc, sizeof loc);   /* interni bod -> field/square/subsquare */
    ok &= (strncmp(loc, "JN89SM", 6) == 0);
    fmt_locator(0.0f, 0.0f, loc, sizeof loc);        /* rovnik + nulty poledník -> pole "JJ" */
    ok &= (loc[0] == 'J' && loc[1] == 'J');
    printf("app: fmt_locator selftest %s (%s)\n", ok ? "OK" : "FAIL", loc);
    return ok != 0;
}

/* Compact "min/max" with 1 decimal from a sensor's stats. */
static void fmt_minmax(char *buf, size_t n, const sensor_stat_t *s)
{
    if (s->samples == 0) { snprintf(buf, n, "--/--"); return; }
    /* clamp na realny rozsah senzoru (teploty ±125.0) pred fmt_fixed — samo
     * o sobe by na tomto rozsahu nepretekly, klamp jen drzi vystup rozumny
     * pri necekane hodnote (senzor error apod). Puvodne se klampoval az
     * zaokrouhleny int (×10, hranice 9999 = 999.9); ekvivalentni na floatu. */
    float lo = s->min, hi = s->max;
    if (lo > 999.9f) lo = 999.9f; else if (lo < -999.9f) lo = -999.9f;
    if (hi > 999.9f) hi = 999.9f; else if (hi < -999.9f) hi = -999.9f;
    int32_t lw, lf, hw, hf;
    fixed_split(lo, 1, &lw, &lf);
    fixed_split(hi, 1, &hw, &hf);
    /* Klamp na VYSTUPU fixed_split (i kdyz uz vstupni float je klampnuty vyse):
     * GCC -Wformat-truncation nedokaze protahnout rozsahovou analyzu pres
     * float aritmetiku uvnitr fixed_split, takze bez tohohle vidi lw/hw jako
     * "cely int32" -> az 26 B do buf[24]. S explicitnim klampem primo na
     * hodnotach predanych do snprintf uz rozsah spocita (shoduje se s tim,
     * jak to delal puvodni kod pred timto sjednocenim). */
    if (lw > 999) lw = 999; else if (lw < -999) lw = -999;
    if (hw > 999) hw = 999; else if (hw < -999) hw = -999;
    /* sgn: stejna oprava ztraty znamenka u -0.x jako ve fmt_fixed (viz tam). */
    const char *ls = (lo < 0.0f && lw == 0 && lf != 0) ? "-" : "";
    const char *hs = (hi < 0.0f && hw == 0 && hf != 0) ? "-" : "";
    snprintf(buf, n, "%s%d.%d/%s%d.%d", ls, (int)lw, (int)lf, hs, (int)hw, (int)hf);
}

/* Change-detect: vrati 1 (a aktualizuje cache) kdyz se 'now' lisi od 'cache'. */
static int dchg(char *cache, size_t n, const char *now)
{
    if (strncmp(cache, now, n) == 0) return 0;
    strncpy(cache, now, n - 1);
    cache[n - 1] = '\0';
    return 1;
}

/* Redraw dynamic values. force=1 -> prekresli VSE (po blitu chrome jsou hodnoty
 * smazane); force=0 (tick) -> jen pole, ktera se ZMENILA -> usetri DMA2D fill +
 * cache invalidaci + CPU text u nemennych poli (vetsina). */
/* Vrati 1 pokud se NECO prekreslilo (-> volajici flipne present), jinak 0. */
static int draw_diag_values(int force)
{
    static char c_tv[4][20], c_tm[4][20], c_adc[4][20], c_mcu[2][20];
    static char c_spi[68], c_fpga[68], c_si[20], c_sys[5][20];
    char buf[24], key[26];
    int drew = force;   /* force -> vse se kresli */

    /* ── Levy sloupec: teploty (hodnota + min/max), poradi = labely v chrome:
     * STM board (0x48) / MCU jadro (ADC3) / OCXO (0x49) / FPGA board (0x4A).
     * ⚠️ Radkovy layout: label (DG_LLBL, sirka do ~140 px — nejdelsi "FPGA board"
     * ma 110 px @ mono_18) | min/max (DG_LLBL+140, 100 px) | hodnota (DG_LVAL,
     * 100 px). Puvodni min/max box zacinal na DG_LLBL+96=126 px — to je UVNITR
     * label "FPGA board"/"STM board"/"MCU jadro" (koncí ~129-140 px), takze
     * kazdy zivy prekres min/max SMAZAL (fill_rect pred textem) kus labelu ->
     * neciteny/uriznuty text. Posunuto + zuzeno tak, aby zadny box nezasahoval
     * do sousedniho textu (min. 20 px rezerva na obe strany). */
    static const sensor_id_t tid[4] = { SENS_T48, SENS_CORE_T, SENS_T49, SENS_T4A };
    static const int16_t     ty[4]  = { 104, 130, 156, 182 };
    for (int i = 0; i < 4; i++) {
        const sensor_stat_t *s = &g_sensors[tid[i]];
        fmt_minmax(buf, sizeof(buf), s);
        if (force || dchg(c_tm[i], sizeof(c_tm[i]), buf)) {
            dtext((int16_t)(DG_LLBL + 140), ty[i], 100, buf, UI_COLOR_INK_3, &ui_font_sans_18); drew = 1; }
        fmt_temp(buf, sizeof(buf), s->last);
        snprintf(key, sizeof(key), "%c%s", s->valid ? 'V' : 'X', buf);  /* vykresleni zalezi i na valid */
        if (force || dchg(c_tv[i], sizeof(c_tv[i]), key)) {
            dval(DG_LVAL, ty[i], 100, buf, s->valid); drew = 1; }
    }

    /* Napeti: ADS1115 AIN0..3 (258/284/310/336) + MCU VREF/VBAT (362/388). Roztec 26.
     * Box zuzen na 100 px (bylo 120) — realny obsah "1234 mV" ~88 px @ mono_18,
     * 120 px byla zbytecna rezerva ("blok siresi nez text uvnitr"). */
    for (int k = 0; k < 4; k++) {
        const sensor_stat_t *a = &g_sensors[SENS_ADS0 + k];
        snprintf(buf, sizeof(buf), "%ld mV", lround_f(a->last));
        snprintf(key, sizeof(key), "%c%s", a->valid ? 'V' : 'X', buf);
        if (force || dchg(c_adc[k], sizeof(c_adc[k]), key)) {
            dval(DG_LVAL, (int16_t)(258 + k * 26), 100, buf, a->valid); drew = 1; }
    }
    for (int k = 0; k < 2; k++) {
        const sensor_stat_t *mv = &g_sensors[SENS_VDDA + k];
        snprintf(buf, sizeof(buf), "%ld mV", lround_f(mv->last));
        snprintf(key, sizeof(key), "%c%s", mv->valid ? 'V' : 'X', buf);
        if (force || dchg(c_mcu[k], sizeof(c_mcu[k]), key)) {
            dval(DG_LVAL, (int16_t)(362 + k * 26), 100, buf, mv->valid); drew = 1; }
    }

    /* ── Pravy sloupec ── */
    /* FPGA: SPI status (barva dle g_spi_ok -> klic vc. ok) + merici kvalita. */
    char sig[68];
    snprintf(sig, sizeof(sig), "%c%s", g_spi_ok ? 'O' : 'X', (const char *)g_spi_text);
    if (force || dchg(c_spi, sizeof(c_spi), sig)) {
        /* mono_14 ZAMERNE (jediny 14px text v oknech): NOLINK radek ma az 38 zn
         * (38x10=380 px @16 > box 352 -> orizl by se RX0/CRC ocas; @14 = 304 px OK). */
        dtext(DG_RLBL, 104, DG_COLW - 24, (const char *)g_spi_text,
              g_spi_ok ? UI_COLOR_OK : UI_COLOR_BAD, &ui_font_mono_14); drew = 1; }
    /* ⚠️ TODO #11(2b): NEbumpovano — absolutni worst-case (vsechny chybove
     * priznaky + max SEQ/gate soucasne) uz na mono_16 preteka box 352 px,
     * na mono_18 jeste vic (overeno tabulkou fontu); realny obsah je kratsi,
     * ale bez jistoty to radsi nechat na soucasnem fontu. */
    if (force || dchg(c_fpga, sizeof(c_fpga), (const char *)g_freq_info)) {
        dtext(DG_RLBL, 132, DG_COLW - 24, (const char *)g_freq_info, UI_COLOR_INK_2, &ui_font_sans_16); drew = 1; }

    /* Reference Si5356: lock status (retezec 1:1 se statusem -> staci porovnat si).
     * LOS_CLKIN (bit3) = ztrata 10 MHz reference = CERVENA (LOL se pri fyzicke
     * ztrate vstupu neasertuje — viz SI5356_* definice). LOS_XTAL ignorovan. */
    const char *si; prim_color_t sic;
    if (!g_si5356_ok)                                   { si = "N/A (I2C)";   sic = UI_COLOR_INK_3; }
    else if (g_si5356_status & SI5356_LOS_CLKIN)        { si = "LOS CLKIN!";  sic = UI_COLOR_BAD; }
    else if (g_si5356_status & SI5356_PLL_LOL)          { si = "PLL UNLOCK!"; sic = UI_COLOR_BAD; }
    else if (g_si5356_status & SI5356_SYS_CAL)          { si = "CALIB...";    sic = UI_COLOR_VIOLET; }
    else                                                { si = "LOCK OK";     sic = UI_COLOR_OK; }
    if (force || dchg(c_si, sizeof(c_si), si)) {
        dtext(DG_RLBL, 206, DG_COLW - 24, si, sic, &ui_font_mono_18); drew = 1; }

    /* System / RTOS / RTC. Box zuzen na 110 px (bylo 150) — heap je max 32768 B
     * (configTOTAL_HEAP_SIZE) = "32768 B" ~77 px @ mono_18, "23:59:59"/"100 %"
     * jeste kratsi; 110 px necha ~30 px rezervu vc. mista na "!" pri neplatne
     * hodnote, misto 73 px prazdneho bloku navic. */
    snprintf(buf, sizeof(buf), "%lu B", (unsigned long)g_rtos_heap_free);
    if (force || dchg(c_sys[0], sizeof(c_sys[0]), buf)) { dval(DG_RVAL, 288, 110, buf, 1); drew = 1; }
    snprintf(buf, sizeof(buf), "%lu B", (unsigned long)g_rtos_heap_min);
    if (force || dchg(c_sys[1], sizeof(c_sys[1]), buf)) { dval(DG_RVAL, 314, 110, buf, 1); drew = 1; }
    snprintf(buf, sizeof(buf), "%lu %%", (unsigned long)g_rtos_cpu_pct);
    if (force || dchg(c_sys[2], sizeof(c_sys[2]), buf)) { dval(DG_RVAL, 340, 110, buf, 1); drew = 1; }
    { uint32_t s = g_uptime_s;
      snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu",
               (unsigned long)(s / 3600u), (unsigned long)((s / 60u) % 60u),
               (unsigned long)(s % 60u)); }
    if (force || dchg(c_sys[3], sizeof(c_sys[3]), buf)) { dval(DG_RVAL, 366, 110, buf, 1); drew = 1; }

    /* RTC: cas HH:MM:SS z g_rtc_text ("YYYY-MM-DD HH:MM:SS"). synced=0 -> ztlumeny
     * + "no GPS" (jeste nesrovnano z GPS). Klic vc. sync stavu (rozhoduje o barve). */
    { char rt[24]; uint8_t rsy;
      strncpy(rt, (const char *)g_rtc_text, sizeof rt - 1); rt[sizeof rt - 1] = '\0';
      rsy = g_rtc_synced;
      if (rsy && strlen(rt) >= 19) snprintf(buf, sizeof(buf), "%.8s", rt + 11);
      else                         snprintf(buf, sizeof(buf), "no GPS");
      snprintf(key, sizeof(key), "%c%s", rsy ? 'V' : 'X', buf);
      if (force || dchg(c_sys[4], sizeof(c_sys[4]), key)) { dval(DG_RVAL, 392, 110, buf, rsy); drew = 1; } }

    return drew;
}

void app_gpsdo_render_diag(void)
{
    int first = window_first(1);
    if (first) {
        /* First entry: draw the static chrome + labels exactly once. */
        s_view = 1;
        window_chrome("DIAGNOSTIKA", WIN_TITLE_Y);
        ui_button_t diagbtn = {.rect = DIAG_DIAGRAM_BTN_RECT, .variant = UI_BUTTON_NORMAL,
                               .label = "DIAGRAM"};
        ui_button_render(&diagbtn);
        ui_button_t membtn = {.rect = DIAG_MEM_BTN_RECT, .variant = UI_BUTTON_NORMAL,
                              .label = "PAMET"};
        ui_button_render(&membtn);
        ui_button_t stbtn = {.rect = DIAG_ST_BTN_RECT, .variant = UI_BUTTON_NORMAL,
                             .label = "SELFTEST"};
        ui_button_render(&stbtn);

        /* Left column: Teploty (vc. MCU jadra) + Napeti (ADS1115 + MCU).
         * ⚠️ FOOTER PRAVIDLO: spodni lista (y >= 416) je VZDY dedikovana
         * tlacitkum -> obsah konci <= 404. (Drivejsi 3. karta "MCU" sahala
         * do 472 = do listy; jeji radky jsou slouceny sem, roztec 26 px.) */
        ui_card_t c_temp = {.rect = {DG_LX, 58, DG_COLW, 144},
                            .header_label = "Teploty  (last  min/max)"};
        ui_card_render_chrome(&c_temp);
        dlabel(DG_LLBL, 104, "STM board");    /* TMP117 0x48 (I2C4) */
        dlabel(DG_LLBL, 130, "MCU jadro");    /* ADC3 interni senzor */
        dlabel(DG_LLBL, 156, "OCXO");         /* TMP117 0x49 (I2C1, FPGA deska) */
        dlabel(DG_LLBL, 182, "FPGA board");   /* TMP117 0x4A (I2C1, neosazen) */

        ui_card_t c_adc = {.rect = {DG_LX, 212, DG_COLW, 192},
                           .header_label = "Napeti (ADS1115 + MCU)"};
        ui_card_render_chrome(&c_adc);
        dlabel(DG_LLBL, 258, "OCXO_VC");      /* AIN0: ladici napeti OCXO */
        dlabel(DG_LLBL, 284, "RF_Level");     /* AIN1: uroven vstupniho signalu */
        dlabel(DG_LLBL, 310, "AIN2 (12V)");
        dlabel(DG_LLBL, 336, "AIN3 (5V)");
        dlabel(DG_LLBL, 362, "VREF");
        dlabel(DG_LLBL, 388, "VBAT");

        /* Right column: FPGA + Reference + System. */
        ui_card_t c_fpga = {.rect = {DG_RX, 58, DG_COLW, 92},
                            .header_label = "Komunikace + mereni FPGA"};
        ui_card_render_chrome(&c_fpga);

        ui_card_t c_ref = {.rect = {DG_RX, 160, DG_COLW, 72},
                           .header_label = "Reference Si5356 (4x100MHz)"};
        ui_card_render_chrome(&c_ref);

        ui_card_t c_sys = {.rect = {DG_RX, 242, DG_COLW, 162},
                           .header_label = "System / RTOS / RTC"};
        ui_card_render_chrome(&c_sys);
        dlabel(DG_RLBL, 288, "Heap free");
        dlabel(DG_RLBL, 314, "Heap min");
        dlabel(DG_RLBL, 340, "CPU");
        dlabel(DG_RLBL, 366, "Uptime");
        dlabel(DG_RLBL, 392, "RTC (UTC)");
    }
    /* present (flip) jen kdyz se neco prekreslilo (first=1 vzdy kresli chrome+vse). */
    if (draw_diag_values(first)) present_now();
}

/* Zive hodnoty GPS okna (s_view=2). force=1 po vykresleni chrome -> vse;
 * force=0 (tick) -> jen zmenena pole. Vrati 1 pokud se neco prekreslilo. */
static int draw_gps_values(int force)
{
    static char c_fix[16], c_sat[24], c_bars[64], c_dop[28], c_time[20], c_date[16];
    static char c_lat[24], c_lon[24], c_alt[20], c_tp[20], c_loc[24], c_rx[40];
    char buf[64], a[16], b[16];
    gps_data_t g;
    gps_get(&g);
    int drew = force;

    /* ── Radek 1 karty FIX (bez nadpisu): "FIX: 3D/2D/No signal" (velke, vlevo) +
     * Time Pulse (vpravo). Time Pulse: s fixem 100 kHz (GPSDO PLL ref, disc. na
     * GNSS), bez fixu 10 Hz (hold VC / holdover). ── */
    const char *fs; prim_color_t fc;
    if      (g.valid && g.fix_mode == 3) { fs = "FIX: 3D";        fc = UI_COLOR_OK; }
    else if (g.valid && g.fix_mode == 2) { fs = "FIX: 2D";        fc = UI_COLOR_OK; }
    else if (g.fix_quality > 0)          { fs = "FIX: OK";        fc = UI_COLOR_OK; }
    else                                 { fs = "FIX: No signal"; fc = UI_COLOR_INK_3; }
    if (force || dchg(c_fix, sizeof c_fix, fs)) {
        prim_fill_rect((prim_rect_t){GPS_LLBL, 74, 288, 30}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        prim_draw_text((prim_point_t){GPS_LLBL, 96}, fs, &ui_font_mono_25, fc, PRIM_ALIGN_LEFT);
        drew = 1; }

    const char *tp; prim_color_t tc;
    if (g.fix_quality) { tp = "Time Pulse 100 kHz"; tc = UI_COLOR_OK; }
    else               { tp = "Time Pulse 10 Hz";   tc = UI_COLOR_WARN; }
    if (force || dchg(c_tp, sizeof c_tp, tp)) {
        prim_fill_rect((prim_rect_t){300, 74, (int16_t)(GPS_LX + GPS_LW - 14 - 300), 30},
                       UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        prim_draw_text((prim_point_t){(int16_t)(GPS_LX + GPS_LW - 14), 96}, tp,
                       &ui_font_mono_18, tc, PRIM_ALIGN_RIGHT); drew = 1; }

    /* ── Radek 2 karty FIX: pocet druzic (vlevo) + HDOP/PDOP (vpravo, presunuto sem) ── */
    snprintf(buf, sizeof buf, "%u / %u druzic", g.num_sat, g.sats_in_view);
    if (force || dchg(c_sat, sizeof c_sat, buf)) {
        dtext(GPS_LLBL, 134, 200, buf, UI_COLOR_INK_3, &ui_font_sans_18); drew = 1; }
    fmt_d1(g.hdop, a, sizeof a); fmt_d1(g.pdop, b, sizeof b);
    snprintf(buf, sizeof buf, "HDOP %s   PDOP %s", a, b);
    if (force || dchg(c_dop, sizeof c_dop, buf)) {
        prim_fill_rect((prim_rect_t){300, 118, (int16_t)(GPS_LX + GPS_LW - 14 - 300), 24},
                       UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        /* ⚠️ TODO #11(2b): NEbumpovano — box ~206 px, i kratsi "HDOP 9.9 PDOP 9.9"
         * uz na mono_18 preteka (overeno tabulkou fontu), box neni dtext-clipovany
         * (jen fill_rect clear) -> preteceni by bylo VIDITELNE, ne tiche. */
        prim_draw_text((prim_point_t){(int16_t)(GPS_LX + GPS_LW - 14), 134}, buf,
                       &ui_font_mono_16, UI_COLOR_INK_3, PRIM_ALIGN_RIGHT); drew = 1; }
    /* naznak prepinani zobrazeni druzic (staticky) vpravo v hlavicce karty Druzice */
    if (force)
        prim_draw_text((prim_point_t){(int16_t)(GPS_LX + GPS_LW - 14), 186},
                       "TAP: bar/sky", &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_RIGHT);

    /* Druzice: JEDNO zobrazeni na plnou sirku sloupce — bargraf C/N0 nebo polarni
     * sky plot (az/el), prepinatelne dotykem (s_gps_polar). Barva = C/N0. */
    {
        gps_sat_t sv[GPS_MAX_SATS];
        /* RINEX písmeno souhvězdí (gps_constel_t: GPS/GLONASS/Galileo/BeiDou) —
         * prefix PRN (G05/R68/E12/C07) odliší souhvězdí; barva zůstává = C/N0. */
        static const char k_constel_ltr[GPS_CONSTEL_N] = { 'G', 'R', 'E', 'C' };
        int nsv = g.sat_count;                        /* uint8_t 0..GPS_MAX_SATS -> vzdy >=0 */
        if (nsv > GPS_MAX_SATS) nsv = GPS_MAX_SATS;   /* pojistka proti pretekani sv[] */
        for (int i = 0; i < nsv; i++) sv[i] = g.sats[i];
        for (int i = 1; i < nsv; i++) {               /* insertion sort podle snr desc */
            gps_sat_t t = sv[i]; int j = i - 1;
            while (j >= 0 && sv[j].snr < t.snr) { sv[j + 1] = sv[j]; j--; }
            sv[j + 1] = t;
        }
        int n = nsv > 14 ? 14 : nsv;                  /* bary: az 14 (siroky sloupec) */
        /* zmenovy klic: mod + pocet + hash az/el/snr VSECH druzic (+ C/N0 baru) */
        uint32_t skyh = s_gps_polar ? 0xA5u : 0x5Au;
        for (int i = 0; i < nsv; i++)
            skyh = skyh * 31u + sv[i].prn + (uint32_t)(sv[i].azim / 4u) * 7u
                 + (uint32_t)(sv[i].elev / 4u) * 13u + (sv[i].snr / 8u)
                 + (uint32_t)sv[i].constel * 17u;
        char key[64]; int kp = snprintf(key, sizeof key, "%d_%08lx", n, (unsigned long)skyh);
        for (int i = 0; i < n && kp < (int)sizeof key - 5; i++)
            kp += snprintf(key + kp, sizeof key - kp, ".%u", sv[i].snr);
        if (force || dchg(c_bars, sizeof c_bars, key)) {
            /* clear cele plochy druzic (194..476) — karta jde az po spodni okraj */
            prim_fill_rect((prim_rect_t){GPS_LLBL, 194, GPS_LW - 24, 282},
                           UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
            if (nsv == 0) {
                dtext_c((int16_t)(GPS_LX + GPS_LW / 2), 330, GPS_LW - 24,
                        "Hledam druzice...", UI_COLOR_WARN, &ui_font_sans_18);
            } else if (s_gps_polar) {
                /* ── Sky plot na plnou plochu: velky kruh r=132, stred=zenit, N nahore ── */
                const int16_t scx = (int16_t)(GPS_LX + GPS_LW / 2), scy = 332, sr = 132;
                /* prim_draw_circle (ne prim_draw_arc 0..360) — plny kruh nedela
                 * atan2f per-pixel (ring_sector); u r=132 to je ~70k atan2f/kruh. */
                prim_draw_circle((prim_point_t){scx, scy}, sr,                   1, UI_COLOR_LINE);
                prim_draw_circle((prim_point_t){scx, scy}, (int16_t)(sr * 2 / 3), 1, UI_COLOR_LINE);
                prim_draw_circle((prim_point_t){scx, scy}, (int16_t)(sr / 3),     1, UI_COLOR_LINE);
                prim_draw_line((prim_point_t){(int16_t)(scx - sr), scy},
                               (prim_point_t){(int16_t)(scx + sr), scy}, 1, UI_COLOR_LINE);
                prim_draw_line((prim_point_t){scx, (int16_t)(scy - sr)},
                               (prim_point_t){scx, (int16_t)(scy + sr)}, 1, UI_COLOR_LINE);
                prim_draw_text((prim_point_t){scx, (int16_t)(scy - sr + 15)}, "N",
                               &ui_font_mono_14, UI_COLOR_INK_3, PRIM_ALIGN_CENTER);
                prim_draw_text((prim_point_t){(int16_t)(scx + sr - 12), (int16_t)(scy + 5)}, "E",
                               &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_RIGHT);
                for (int i = 0; i < nsv; i++) {       /* tecky: azimut 0=N po smeru hodin */
                    float azr = (float)sv[i].azim * 0.0174533f;
                    float rr  = (float)sr * (float)(90 - (sv[i].elev > 90 ? 90 : sv[i].elev)) / 90.0f;
                    int16_t px = (int16_t)(scx + sinf(azr) * rr);
                    int16_t py = (int16_t)(scy - cosf(azr) * rr);
                    uint8_t snr = sv[i].snr;
                    prim_color_t col = (snr >= 38) ? UI_COLOR_OK :
                                       (snr >= 25) ? UI_COLOR_WARN :
                                       (snr > 0)   ? UI_COLOR_BAD : UI_COLOR_INK_4;
                    prim_fill_circle((prim_point_t){px, py}, (int16_t)(snr > 0 ? 6 : 3), col);
                    if (snr > 0) {                    /* PRN vedle tecky */
                        char pr[8]; snprintf(pr, sizeof pr, "%c%u",
                            (sv[i].constel < GPS_CONSTEL_N) ? k_constel_ltr[sv[i].constel] : '?', sv[i].prn);
                        prim_draw_text((prim_point_t){(int16_t)(px + 8), (int16_t)(py + 4)}, pr,
                                       &ui_font_mono_14, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
                    }
                }
            } else {
                /* ── C/N0 bargraf na plnou plochu (az 14 nejsilnejsich) ── */
                const int16_t base = 452;             /* dolni hrana sloupcu (u spodniho okraje) */
                const int16_t maxh = 238;             /* vyska pro C/N0 = 55 dB-Hz */
                const int16_t area = (int16_t)(GPS_LW - 24);
                int16_t slot = (int16_t)(area / n);
                int16_t bw = (int16_t)(slot * 2 / 3);
                if (bw < 3) bw = 3;
                for (int i = 0; i < n; i++) {
                    uint8_t snr = sv[i].snr;
                    prim_color_t col = (snr >= 38) ? UI_COLOR_OK :
                                       (snr >= 25) ? UI_COLOR_WARN :
                                       (snr > 0)   ? UI_COLOR_BAD : UI_COLOR_INK_4;
                    int16_t cx = (int16_t)(GPS_LLBL + i * slot + slot / 2);
                    int16_t h  = (int16_t)((snr > 55 ? 55 : snr) * maxh / 55);
                    if (h < 2) h = 2;
                    prim_fill_rect((prim_rect_t){(int16_t)(cx - bw / 2), (int16_t)(base - h),
                                   bw, h}, col, PRIM_BLEND_REPLACE);
                    if (snr > 0) {                    /* C/N0 nad sloupcem */
                        char sn[6]; snprintf(sn, sizeof sn, "%u", snr);
                        prim_draw_text((prim_point_t){cx, (int16_t)(base - h - 4)}, sn,
                                       &ui_font_mono_14, UI_COLOR_INK_3, PRIM_ALIGN_CENTER);
                    }
                    char pr[8]; snprintf(pr, sizeof pr, "%c%u",
                        (sv[i].constel < GPS_CONSTEL_N) ? k_constel_ltr[sv[i].constel] : '?', sv[i].prn);
                    prim_draw_text((prim_point_t){cx, 468}, pr, &ui_font_mono_14,
                                   UI_COLOR_INK_3, PRIM_ALIGN_CENTER);
                }
            }
            drew = 1;
        }
    }

    /* (HDOP/PDOP presunuty nahoru k FIX — radek 2 karty FIX.) */

    /* cas + datum z RTC (LSE, disciplinovany GPS). RTC tika i bez fixu -> karta
     * je vzdy zive; synced=0 (volny beh od bootu) ztlumime. g_rtc_text =
     * "YYYY-MM-DD HH:MM:SS" -> [0..9] datum, [11..18] cas. */
    char rt[24]; uint8_t rsy;
    strncpy(rt, (const char *)g_rtc_text, sizeof rt - 1); rt[sizeof rt - 1] = '\0';
    rsy = g_rtc_synced;
    snprintf(buf, sizeof buf, "%s UTC", (strlen(rt) >= 19) ? rt + 11 : "--:--:--");
    if (force || dchg(c_time, sizeof c_time, buf)) {
        dtext(GPS_RLBL, 104, GPS_RW - 24, buf, rsy ? UI_COLOR_INK : UI_COLOR_INK_3,
              &ui_font_mono_18); drew = 1; }
    snprintf(buf, sizeof buf, "%.10s", rt);   /* datum "YYYY-MM-DD" (sync stav nese barva casu) */
    if (force || dchg(c_date, sizeof c_date, buf)) {
        dtext(GPS_RLBL, 126, GPS_RW - 24, buf, UI_COLOR_INK_3, &ui_font_sans_18); drew = 1; }

    /* poloha */
    if (g.valid) fmt_ll(g.lat_deg, 'N', 'S', a, sizeof a); else snprintf(a, sizeof a, "--");
    snprintf(buf, sizeof buf, "Lat  %s", a);
    if (force || dchg(c_lat, sizeof c_lat, buf)) {
        dtext(GPS_RLBL, 190, GPS_RW - 24, buf, UI_COLOR_INK_3, &ui_font_mono_18); drew = 1; }
    if (g.valid) fmt_ll(g.lon_deg, 'E', 'W', a, sizeof a); else snprintf(a, sizeof a, "--");
    snprintf(buf, sizeof buf, "Lon  %s", a);
    if (force || dchg(c_lon, sizeof c_lon, buf)) {
        dtext(GPS_RLBL, 214, GPS_RW - 24, buf, UI_COLOR_INK_3, &ui_font_mono_18); drew = 1; }
    if (g.fix_quality) snprintf(buf, sizeof buf, "Alt  %ld m", lround_f(g.alt_m));
    else               snprintf(buf, sizeof buf, "Alt  --");
    if (force || dchg(c_alt, sizeof c_alt, buf)) {
        dtext(GPS_RLBL, 238, GPS_RW - 24, buf, UI_COLOR_INK_3, &ui_font_mono_18); drew = 1; }

    /* Lokator (Maidenhead grid) — karta bez nadpisu, jen "Locator <hodnota>"
     * (vetsim pismem, vycentrovano ve volne karte 252..320). */
    char loc[16];
    if (g.valid) fmt_locator(g.lat_deg, g.lon_deg, loc, sizeof loc);
    else         snprintf(loc, sizeof loc, "----------");
    snprintf(buf, sizeof buf, "Locator %s", loc);
    if (force || dchg(c_loc, sizeof c_loc, buf)) {
        dtext(GPS_RLBL, 294, GPS_RW - 24, buf, g.valid ? UI_COLOR_ACC : UI_COLOR_INK_3,
              &ui_font_mono_18); drew = 1; }

    /* Prijimac: zive statistiky linky (naparsovane vety + platne fixy) — rostou,
     * dokud GPS tece -> dukaz zivosti (staticke "NEO-7M" je v chrome). */
    snprintf(buf, sizeof buf, "Vet:%lu Fix:%lu",
             (unsigned long)g.sentences, (unsigned long)g.fixes);
    /* ⚠️ TODO #11(2b): NEbumpovano na mono_18 — pocitadla rostou bez limitu
     * (roky provozu), realny 7-8 mistny stav uz PRETEKA i pri soucasnem
     * mono_16 (226 px box, overeno tabulkou fontu) -> bump by to jen zhorsil. */
    if (force || dchg(c_rx, sizeof c_rx, buf)) {
        dtext(GPS_RLBL, 384, GPS_RW - 24, buf, UI_COLOR_INK_2, &ui_font_mono_16); drew = 1; }

    return drew;
}

/* GPS / GNSS okno (tap na GNSS pill, ZPET zpet na main). Zive (refresh ~2x/s
 * v app_gpsdo_tick). First entry kresli chrome, pak jen zmenena pole. */
void app_gpsdo_render_gps(void)
{
    int first = window_first(2);
    if (first) {
        s_view = 2;
        window_chrome("GNSS / GPS", WIN_TITLE_Y);

        /* Levy (siroky) sloupec: FIX (bez nadpisu — FIX/druzice/DOP/TimePulse jsou
         * uvnitr) + Druzice roztazena AZ PO SPODNI OKRAJ (vyjimka z footer pravidla —
         * BACK je vpravo, levy sloupec ho nekryje). */
        ui_card_t c_fix = {.rect = {GPS_LX, 58, GPS_LW, 96}};   /* bez header_label */
        ui_card_render_chrome(&c_fix);
        ui_card_t c_sat = {.rect = {GPS_LX, 160, GPS_LW, 318},   /* 160..478 = spodni okraj */
                           .header_label = "Druzice"};
        ui_card_render_chrome(&c_sat);
        /* Pravy (uzsi) sloupec: cas/poloha/lokator/prijimac. */
        ui_card_t c_time = {.rect = {GPS_RX, 58, GPS_RW, 76}, .header_label = "Cas / datum (UTC)"};
        ui_card_render_chrome(&c_time);
        ui_card_t c_pos = {.rect = {GPS_RX, 144, GPS_RW, 98}, .header_label = "Poloha"};
        ui_card_render_chrome(&c_pos);
        ui_card_t c_loc = {.rect = {GPS_RX, 252, GPS_RW, 68}};   /* Lokator — bez nadpisu */
        ui_card_render_chrome(&c_loc);
        ui_card_t c_rx = {.rect = {GPS_RX, 330, GPS_RW, 74},
                          .header_label = "Prijimac NEO-7M"};
        ui_card_render_chrome(&c_rx);
        ui_button_t sv = {.rect = GPS_SURVEY_BTN, .variant = UI_BUTTON_NORMAL, .label = "SURVEY >"};
        ui_button_render(&sv);
    }
    if (draw_gps_values(first)) present_now();
}

/* ── System Health okno (s_view=3) ────────────────────────────────────────
 * Otevre se tapem na SYS pill (vedle GNSS). Hlubsi pohled nez diagnostika:
 * RTOS pamet+CPU, volny stack tasku (osThreadGetStackSpace), I2C chybovost
 * (z g_sensors err citacu), stav linku (FPGA/Si5356/senzory) a napajeci vetve.
 * Live refresh ~2x/s pres app_gpsdo_tick (stejny first/values split jako diag). */

/* Agreguje I2C chybovost ze skupiny senzoru: streak = max souvislych chyb,
 * total = soucet chyb od bootu. (Health = streak==0 -> ted OK.) */
static void i2c_health(const sensor_id_t *ids, int n, uint16_t *streak, uint32_t *total)
{
    uint16_t st = 0; uint32_t tot = 0;
    for (int i = 0; i < n; i++) {
        const sensor_stat_t *s = &g_sensors[ids[i]];
        if (s->err_streak > st) st = s->err_streak;
        tot += s->err_total;
    }
    *streak = st; *total = tot;
}

/* Prekresli dynamicke hodnoty System Health. force=1 -> vse (po blitu chrome),
 * force=0 (tick) -> jen zmenena pole. Vrati 1 pokud neco kreslil. */
static int draw_health_values(int force)
{
    static char c_rtos[3][20], c_stk[5][16], c_i2c[2][40], c_lnk[3][40];
    char buf[36], key[40];   /* "I2C1 FPGA: CHYBA (<u32>)" az 29 zn. -> bez truncation */
    int drew = force;

    /* ── Levy sloupec: RTOS pamet / CPU / uptime ── */
    snprintf(buf, sizeof(buf), "%lu B", (unsigned long)g_rtos_heap_free);
    if (force || dchg(c_rtos[0], sizeof(c_rtos[0]), buf)) { dval(DG_LVAL, 104, 150, buf, 1); drew = 1; }
    snprintf(buf, sizeof(buf), "%lu B", (unsigned long)g_rtos_heap_min);
    if (force || dchg(c_rtos[1], sizeof(c_rtos[1]), buf)) { dval(DG_LVAL, 132, 150, buf, 1); drew = 1; }
    snprintf(buf, sizeof(buf), "%lu %%", (unsigned long)g_rtos_cpu_pct);
    if (force || dchg(c_rtos[2], sizeof(c_rtos[2]), buf)) { dval(DG_LVAL, 160, 150, buf, 1); drew = 1; }
    /* Uptime presunut do karty "System" (pravy sloupec) — c_rtos[3] nevyuzito. */

    /* ── Levy sloupec: volny stack tasku (high-water headroom, bajty) ── */
    osThreadId_t thr[5] = { UiTaskHandle, FpgaTaskHandle, UartTaskHandle,
                            I2C4TaskHandle, defaultTaskHandle };
    static const int16_t sy[5] = { 264, 292, 320, 348, 376 };
    for (int i = 0; i < 5; i++) {
        uint32_t fb = thr[i] ? osThreadGetStackSpace(thr[i]) : 0;
        snprintf(buf, sizeof(buf), "%lu B", (unsigned long)fb);
        /* < 64 B headroom = varovani (cerveny '!'); jinak svetla hodnota. */
        if (force || dchg(c_stk[i], sizeof(c_stk[i]), buf)) {
            dval(DG_LVAL, sy[i], 120, buf, fb >= 64); drew = 1; }
    }

    /* ── Pravy sloupec: I2C chybovost (celobarevne radky) ── */
    static const sensor_id_t i2c1_ids[5] = { SENS_T49, SENS_ADS0, SENS_ADS1,
                                             SENS_ADS2, SENS_ADS3 };  /* T4A vynechan (neosazen) */
    static const sensor_id_t i2c4_ids[1] = { SENS_T48 };
    uint16_t s1, s4; uint32_t t1, t4;
    i2c_health(i2c1_ids, 5, &s1, &t1);
    i2c_health(i2c4_ids, 1, &s4, &t4);
    snprintf(buf, sizeof(buf), "I2C1 FPGA: %s (%lu)", s1 ? "CHYBA" : "OK", (unsigned long)t1);
    snprintf(key, sizeof(key), "%c%s", s1 ? 'X' : 'O', buf);
    if (force || dchg(c_i2c[0], sizeof(c_i2c[0]), key)) {
        dtext(DG_RLBL, 104, DG_COLW - 24, buf, s1 ? UI_COLOR_BAD : UI_COLOR_OK, &ui_font_mono_18); drew = 1; }
    snprintf(buf, sizeof(buf), "I2C4 panel: %s (%lu)", s4 ? "CHYBA" : "OK", (unsigned long)t4);
    snprintf(key, sizeof(key), "%c%s", s4 ? 'X' : 'O', buf);
    if (force || dchg(c_i2c[1], sizeof(c_i2c[1]), key)) {
        dtext(DG_RLBL, 132, DG_COLW - 24, buf, s4 ? UI_COLOR_BAD : UI_COLOR_OK, &ui_font_mono_18); drew = 1; }

    /* ── Pravy sloupec: periferie / linky ── */
    snprintf(buf, sizeof(buf), "FPGA SPI: %s", g_spi_ok ? "LINK OK" : "NO LINK");
    snprintf(key, sizeof(key), "%c%s", g_spi_ok ? 'O' : 'X', buf);
    if (force || dchg(c_lnk[0], sizeof(c_lnk[0]), key)) {
        dtext(DG_RLBL, 200, DG_COLW - 24, buf, g_spi_ok ? UI_COLOR_OK : UI_COLOR_BAD, &ui_font_mono_18); drew = 1; }
    const char *si; prim_color_t sic;
    if (!g_si5356_ok)                            { si = "N/A (I2C)";   sic = UI_COLOR_INK_3; }
    else if (g_si5356_status & SI5356_LOS_CLKIN) { si = "LOS CLKIN!";  sic = UI_COLOR_BAD; }
    else if (g_si5356_status & SI5356_PLL_LOL)   { si = "PLL UNLOCK!"; sic = UI_COLOR_BAD; }
    else if (g_si5356_status & SI5356_SYS_CAL)   { si = "CALIB...";    sic = UI_COLOR_VIOLET; }
    else                                         { si = "LOCK OK";     sic = UI_COLOR_OK; }
    snprintf(buf, sizeof(buf), "Ref Si5356: %s", si);
    if (force || dchg(c_lnk[1], sizeof(c_lnk[1]), buf)) {
        dtext(DG_RLBL, 228, DG_COLW - 24, buf, sic, &ui_font_mono_18); drew = 1; }
    int nok = 0; for (int i = 0; i < SENS_COUNT; i++) if (g_sensors[i].valid) nok++;
    snprintf(buf, sizeof(buf), "Senzory: %d/%d OK", nok, (int)SENS_COUNT);
    snprintf(key, sizeof(key), "%c%s", (nok >= SENS_COUNT - 1) ? 'O' : 'X', buf);  /* -1: 0x4A neosazen */
    if (force || dchg(c_lnk[2], sizeof(c_lnk[2]), key)) {
        dtext(DG_RLBL, 256, DG_COLW - 24, buf,
              (nok >= SENS_COUNT - 1) ? UI_COLOR_OK : UI_COLOR_WARN, &ui_font_mono_18); drew = 1; }

    /* ── Pravy sloupec: karta System (napajeni souhrnne / uptime / reset / selftest).
     * Konkretni napeti vetvi jsou v Diagnostice + SENZORY; tady jen verdikt. ── */
    static char c_sys2[5][40];
    { const sensor_stat_t *v12 = &g_sensors[SENS_ADS2];
      const sensor_stat_t *v5  = &g_sensors[SENS_ADS3];
      const char *ps; prim_color_t pc;
      if (v12->samples == 0 || v5->samples == 0 || !v12->valid || !v5->valid) {
          ps = "Unkn"; pc = UI_COLOR_INK_3;
      } else {
          long m12 = lround_f(v12->last), m5 = lround_f(v5->last);
          int ok12 = (m12 > 10800 && m12 < 13200);    /* 12 V ±10 % */
          int ok5  = (m5  > 4500  && m5  < 5500);     /* 5 V ±10 % */
          if (ok12 && ok5) { ps = "OK";   pc = UI_COLOR_OK; }
          else             { ps = "FAIL"; pc = UI_COLOR_BAD; }
      }
      snprintf(buf, sizeof(buf), "Power supplies: %s", ps);
      /* TODO #11(2b) 2026-07-19: bump na mono_18 tam, kde se vejde (overeno
       * tabulkou fontu). Tenhle radek ("Power supplies: FAIL" max 20 zn.) i
       * Uptime nize maji velkou rezervu (352 px box, realny obsah <200 px). */
      if (force || dchg(c_sys2[0], sizeof(c_sys2[0]), buf)) {
          dtext(DG_RLBL, 328, DG_COLW - 24, buf, pc, &ui_font_mono_18); drew = 1; } }
    { uint32_t s = g_uptime_s;
      snprintf(buf, sizeof(buf), "Uptime: %lu:%02lu:%02lu", (unsigned long)(s / 3600u),
               (unsigned long)((s / 60u) % 60u), (unsigned long)(s % 60u));
      if (force || dchg(c_sys2[1], sizeof(c_sys2[1]), buf)) {
          dtext(DG_RLBL, 350, DG_COLW - 24, buf, UI_COLOR_INK_2, &ui_font_mono_18); drew = 1; } }
    if (g_crash_text[0])
        snprintf(buf, sizeof(buf), "Reset: %s %s", (const char *)g_reset_text,
                 (const char *)g_crash_text);
    else
        snprintf(buf, sizeof(buf), "Reset: %s", (const char *)g_reset_text);
    int rbad = g_reset_bad || g_crash_text[0];
    /* ⚠️ TODO #11(2b): NEbumpovano — nejdelsi radek Reset ("Reset: " +
     * g_reset_text[12] + " " + g_crash_text[16]) = 34 zn. = 340 px pri
     * mono_16 (bezva bez rezervy, box 352 px); pri mono_18 by to bylo
     * 374 px > 352 -> orizlo by se (overeno tabulkou fontu). */
    if (force || dchg(c_sys2[2], sizeof(c_sys2[2]), buf)) {
        dtext(DG_RLBL, 372, DG_COLW - 24, buf,
              rbad ? UI_COLOR_BAD : UI_COLOR_INK_3, &ui_font_mono_16); drew = 1; }
    /* Selftest + CM4 (D2) sdili jeden radek (oba kratke fixni retezce - "Selftest:
     * FAIL" max 14 zn. v boxu 190 px, "CM4:ABSENT" max 10 zn. v boxu 156 px;
     * pri mono_18 (advance 11) to je 154 resp. 110 px - porad s rezervou, zadne
     * riziko prekryvu, overeno tabulkou fontu). Drive samostatny radek CM4 na
     * baseline 416 kolidoval s paticnimi tlacitky (zacinaji na y=417). */
    snprintf(buf, sizeof(buf), "Selftest: %s",
             g_selftest_res == 1 ? "PASS" : (g_selftest_res == 2 ? "FAIL" : "---"));
    if (force || dchg(c_sys2[3], sizeof(c_sys2[3]), buf)) {
        dtext(DG_RLBL, 394, 190, buf,
              g_selftest_res == 1 ? UI_COLOR_OK
              : (g_selftest_res == 2 ? UI_COLOR_BAD : UI_COLOR_INK_3), &ui_font_mono_18);
        drew = 1; }

    /* CM4 (D2) boot: degradovany rezim kdyz nenabehl (prazdna bank2 / BCM4=0).
     * Amber (funguje, jen bez konektivity jadra) — konzistentni se SYS pill. */
    snprintf(buf, sizeof(buf), "CM4:%s", g_cm4_absent ? "ABSENT" : "OK");
    if (force || dchg(c_sys2[4], sizeof(c_sys2[4]), buf)) {
        dtext((int16_t)(DG_RLBL + 196), 394, (int16_t)(DG_COLW - 24 - 196), buf,
              g_cm4_absent ? UI_COLOR_WARN : UI_COLOR_OK, &ui_font_mono_18);
        drew = 1; }

    return drew;
}

void app_gpsdo_render_health(void)
{
    int first = window_first(3);
    if (first) {
        s_view = 3;
        window_chrome("SYSTEM HEALTH", WIN_TITLE_Y);
        ui_button_t sens = {.rect = SENS_BTN_RECT, .variant = UI_BUTTON_NORMAL, .label = "SENZORY"};
        ui_button_render(&sens);
        ui_button_t hdiag = {.rect = HEALTH_DIAG_BTN_RECT, .variant = UI_BUTTON_NORMAL,
                             .label = "DIAGNOSTIKA"};
        ui_button_render(&hdiag);
        ui_button_t set = {.rect = SET_BTN_RECT, .variant = UI_BUTTON_NORMAL, .label = "NASTAVENI"};
        ui_button_render(&set);
        ui_button_t grf = {.rect = HEALTH_GRAPH_BTN_RECT, .variant = UI_BUTTON_NORMAL, .label = "GRAFY"};
        ui_button_render(&grf);

        /* Levy: RTOS + Stack tasku. */
        ui_card_t c_rtos = {.rect = {DG_LX, 58, DG_COLW, 122},
                            .header_label = "RTOS / Pamet"};
        ui_card_render_chrome(&c_rtos);
        dlabel(DG_LLBL, 104, "Heap free");
        dlabel(DG_LLBL, 132, "Heap min");
        dlabel(DG_LLBL, 160, "CPU");
        /* Uptime presunut do karty "System" (pravy sloupec). */

        ui_card_t c_stk = {.rect = {DG_LX, 218, DG_COLW, 186},
                           .header_label = "Stack tasku (volno)"};
        ui_card_render_chrome(&c_stk);
        dlabel(DG_LLBL, 264, "UiTask");
        dlabel(DG_LLBL, 292, "FpgaTask");
        dlabel(DG_LLBL, 320, "UartTask");
        dlabel(DG_LLBL, 348, "I2C4Task");
        dlabel(DG_LLBL, 376, "Default");

        /* Pravy: I2C + Linky + Napajeni. */
        ui_card_t c_i2c = {.rect = {DG_RX, 58, DG_COLW, 86},
                           .header_label = "I2C sbernice (chyby)"};
        ui_card_render_chrome(&c_i2c);

        ui_card_t c_lnk = {.rect = {DG_RX, 154, DG_COLW, 116},
                           .header_label = "Periferie / linky"};
        ui_card_render_chrome(&c_lnk);

        ui_card_t c_pwr = {.rect = {DG_RX, 280, DG_COLW, 128},
                           .header_label = "System"};
        ui_card_render_chrome(&c_pwr);
        /* vsechny radky (Napajeni/Uptime/Reset/Selftest) kresli draw_health_values
         * celobarevne (dtext) — konkretni napeti jsou v Diagnostice/SENZORY */
    }
    if (draw_health_values(first)) present_now();
}

/* ── Podmenu vsech senzoru (s_view=4), otevre se z System Health ──────────
 * Cisty prehled AKTUALNICH hodnot, dva sloupce jako diagnostika: vlevo Teploty,
 * vpravo Napeti. Jmena (dlabel) kresli render_sensors jednou; zive hodnoty
 * (dval, zarovnane vpravo) tady. Bez min/max/avg/err — jen aktualni hodnota. */
#define SENS_R0   132                /* y prvniho radku */
#define SENS_DY    32                /* rozteč radku */

/* Rozmisteni: id senzoru + sloupec (xr value) + radek (y) + zda je to teplota. */
static const struct { uint8_t id; int16_t xr; int16_t y; uint8_t temp; }
SENS_ROW[SENS_COUNT] = {
    { SENS_T48,    DG_LVAL, SENS_R0 + 0 * SENS_DY, 1 },   /* leva: Teploty (poradi = labely: */
    { SENS_CORE_T, DG_LVAL, SENS_R0 + 1 * SENS_DY, 1 },   /* STM board / MCU jadro /         */
    { SENS_T49,    DG_LVAL, SENS_R0 + 2 * SENS_DY, 1 },   /* OCXO / FPGA board)              */
    { SENS_T4A,    DG_LVAL, SENS_R0 + 3 * SENS_DY, 1 },
    { SENS_ADS0,   DG_RVAL, SENS_R0 + 0 * SENS_DY, 0 },   /* prava: Napeti */
    { SENS_ADS1,   DG_RVAL, SENS_R0 + 1 * SENS_DY, 0 },
    { SENS_ADS2,   DG_RVAL, SENS_R0 + 2 * SENS_DY, 0 },
    { SENS_ADS3,   DG_RVAL, SENS_R0 + 3 * SENS_DY, 0 },
    { SENS_VDDA,   DG_RVAL, SENS_R0 + 4 * SENS_DY, 0 },
    { SENS_VBAT,   DG_RVAL, SENS_R0 + 5 * SENS_DY, 0 },
};

static int draw_sensors_values(int force)
{
    static char c[SENS_COUNT][20];
    char buf[20], key[24];
    int drew = force;
    for (int i = 0; i < SENS_COUNT; i++) {
        const sensor_stat_t *s = &g_sensors[SENS_ROW[i].id];
        if (s->samples == 0)       snprintf(buf, sizeof buf, "---");
        else if (SENS_ROW[i].temp) fmt_temp(buf, sizeof buf, s->last);        /* "23.45 C" */
        else                       snprintf(buf, sizeof buf, "%ld mV", lround_f(s->last));
        snprintf(key, sizeof key, "%c%s", s->valid ? 'V' : 'X', buf);   /* redraw i pri zmene valid */
        if (force || dchg(c[i], sizeof c[i], key)) {
            dval(SENS_ROW[i].xr, SENS_ROW[i].y, 150, buf, s->valid); drew = 1;
        }
    }
    return drew;
}

void app_gpsdo_render_sensors(void)
{
    int first = window_first(4);
    if (first) {
        s_view = 4;
        window_chrome("SENZORY", WIN_TITLE_Y);
        ui_card_t c = {.rect = DG_CARD_FULL_A,
                       .header_label = "Aktualni hodnoty senzoru"};
        ui_card_render_chrome(&c);

        /* podnadpisy sloupcu */
        prim_draw_text((prim_point_t){DG_LLBL, 104}, "TEPLOTY  [C]", &ui_font_mono_18,
                       UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_RLBL, 104}, "NAPETI  [mV]", &ui_font_mono_18,
                       UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        /* jmena senzoru (poradi musi sedet se SENS_ROW) */
        dlabel(DG_LLBL, SENS_R0 + 0 * SENS_DY, "STM board");
        dlabel(DG_LLBL, SENS_R0 + 1 * SENS_DY, "MCU jadro");
        dlabel(DG_LLBL, SENS_R0 + 2 * SENS_DY, "OCXO");
        dlabel(DG_LLBL, SENS_R0 + 3 * SENS_DY, "FPGA board");
        dlabel(DG_RLBL, SENS_R0 + 0 * SENS_DY, "OCXO_VC");
        dlabel(DG_RLBL, SENS_R0 + 1 * SENS_DY, "RF_Level");
        dlabel(DG_RLBL, SENS_R0 + 2 * SENS_DY, "12V vetev");
        dlabel(DG_RLBL, SENS_R0 + 3 * SENS_DY, "5V vetev");
        dlabel(DG_RLBL, SENS_R0 + 4 * SENS_DY, "VREF");
        dlabel(DG_RLBL, SENS_R0 + 5 * SENS_DY, "VBAT");
    }
    if (draw_sensors_values(first)) present_now();
}

/* ── Okno GRAFY (s_view=29): casovy prubeh teplot + napajeni (STATUS.md #31) ──
 * Vstup: tlacitko GRAFY v System Health. Kombinovany datovy zdroj:
 *   - KRATKA okna (<=1 h) z RAM decimacni pyramidy (sensor_hist.c) — hladke, od
 *     bootu, vsechny senzory;
 *   - DLOUHA okna (6 h / 1 den / 7 dni) z datalogu (W25Q) — jen veliciny, ktere
 *     datalog uklada (OCXO+deska teplota, OCXO Vc); ostatni serie se u dlouhych
 *     oken vynechaji (v grafu neni cara). Vpravo vertikalni bargrafy AKTUALNICH
 *     hodnot napajecich vetvi s nominalnim markerem (#32) — vzdy z g_sensors[]. */
#define GRAPH_MAXPTS 120
static float s_gbuf[GRAPH_MAXPTS];   /* sdilena serie (jen UiTask kresli) */

/* Presety casoveho okna + zdroj (dlog=1 -> datalog, jinak RAM). */
static const struct { int32_t secs; uint8_t dlog; } GRAPH_PRESETS[] = {
    {180,   0},   /* 3 min  (RAM)     */
    {900,   0},   /* 15 min (RAM)     */
    {3600,  0},   /* 1 h    (RAM)     */
    {21600, 1},   /* 6 h    (datalog) */
    {86400, 1},   /* 1 den  (datalog) */
    {604800,1},   /* 7 dni  (datalog) */
};
#define GRAPH_PRESET_N ((int)(sizeof(GRAPH_PRESETS)/sizeof(GRAPH_PRESETS[0])))
static int s_graph_idx = 1;   /* default 15 min */

static const prim_rect_t GRAPH_MINUS = {18, 417, 90, 61};
static const prim_rect_t GRAPH_PLUS  = {214, 417, 90, 61};
/* SESTERSKA dvojice (bez nav_push, jako ALLAN<->HIST): GRAFY (s_view=29, casovy
 * prubeh) <-> PREHLED KANALU (s_view=30, horizontalni bargrafy aktualnich hodnot).
 * Tlacitko ve footeru obou oken prepina mezi nimi; BACK z obou vede tam, odkud
 * byla dvojice otevrena (System Health). */
static const prim_rect_t GRAPH_BARS_BTN = {330, 417, 200, 61};  /* GRAFY -> PREHLED */
static const prim_rect_t HBARS_GRAF_BTN = {18,  417, 200, 61};  /* PREHLED -> GRAFY */

/* Karty: vlevo dva grafy (teploty nahore, OCXO Vc dole), vpravo bargrafy. */
static const prim_rect_t GRAPH_CARD_T  = {18, 58, 544, 176};   /* teploty */
static const prim_rect_t GRAPH_CARD_V  = {18, 242, 544, 168};  /* OCXO Vc  */
static const prim_rect_t GRAPH_CARD_B  = {576, 58, 208, 352};  /* bargrafy */
static const prim_rect_t GRAPH_PLOT_T  = {26, 96, 528, 106};   /* plocha grafu teplot */
static const prim_rect_t GRAPH_PLOT_V  = {26, 280, 528, 116};  /* plocha grafu Vc */

/* Serie teplot (poradi = legenda; barvy viz graph_line_col). */
static const struct { uint8_t id; const char *name; } GRAPH_TEMP[4] = {
    { SENS_T48,    "STM"  },
    { SENS_CORE_T, "MCU"  },
    { SENS_T49,    "OCXO" },
    { SENS_T4A,    "FPGA" },
};
/* Barva serie (UI_COLOR_* jsou runtime makra -> nelze do static init). */
static prim_color_t graph_line_col(int i)
{
    switch (i) { case 0: return UI_COLOR_ACC; case 1: return UI_COLOR_OK;
                 case 2: return UI_COLOR_WARN; default: return UI_COLOR_BAD; }
}

/* Vertikalni bargrafy vpravo — napajeci vetve + Vc, s nominalni hodnotou. */
static const struct { uint8_t id; const char *lab; float lo, hi, nom; } GRAPH_BAR[5] = {
    { SENS_ADS2, "12V", 10800.f, 13200.f, 12000.f },
    { SENS_ADS3, "5V",   4500.f,  5500.f,  5000.f },
    { SENS_VDDA, "REF",  2300.f,  2700.f,  2500.f },
    { SENS_VBAT, "BAT",  2500.f,  3300.f,  3000.f },
    { SENS_ADS0, "Vc",      0.f,  3300.f,  1650.f },
};

/* Datalog nabizi jen 3 veliciny -> mapovani senzor->pole (jinak -1 = neni). */
static int graph_dlog_field(uint8_t sens)
{ return sens == SENS_T49 ? 0 : sens == SENS_T48 ? 1 : sens == SENS_ADS0 ? 2 : -1; }

/* Nacte serii z DATALOGU (field 0=OCXO t, 1=deska t, 2=OCXO Vc) do out (oldest→new). */
static int graph_series_dlog(int field, int32_t win_s, float *out, int max_out,
                             float *mn, float *mx, int32_t *span_s)
{
    datalog_status_t st; datalog_get_status(&st);
    if (!st.ready || st.records < 2) return 0;
    int32_t nrec_win = win_s / (int32_t)DATALOG_PERIOD_S; if (nrec_win < 2) nrec_win = 2;
    int32_t nrec = (nrec_win < (int32_t)st.records) ? nrec_win : (int32_t)st.records;
    int npts = (int)nrec; if (npts > max_out) npts = max_out; if (npts < 2) return 0;
    int32_t stride = nrec / npts; if (stride < 1) stride = 1;
    float mnv = 1e30f, mxv = -1e30f, last = 0; int havelast = 0;
    for (int i = 0; i < npts; i++) {
        uint32_t fn = (uint32_t)((npts - 1 - i) * stride);   /* oldest→newest */
        datalog_rec_t r; float v = 0; int ok = 0;
        if (datalog_read_back(fn, &r)) {
            int16_t raw = (field == 0) ? r.t_ocxo_c100
                        : (field == 1) ? r.t_board_c100 : r.ocxo_vc_mv;
            ok = (raw != DATALOG_INVALID16);
            v  = (field == 2) ? (float)raw : (float)raw * 0.01f;
        }
        if (!ok) v = havelast ? last : 0;
        else     { last = v; havelast = 1; }
        out[i] = v;
        if (v < mnv) mnv = v;
        if (v > mxv) mxv = v;
    }
    if (mn) *mn = mnv;
    if (mx) *mx = mxv;
    if (span_s) *span_s = (int32_t)(npts - 1) * stride * (int32_t)DATALOG_PERIOD_S;
    return npts;
}

/* Jednotny fetch serie do s_gbuf (RAM nebo datalog dle use_dlog). */
static int graph_fetch(uint8_t sens, int32_t win_s, int use_dlog,
                       float *mn, float *mx, int32_t *span)
{
    if (use_dlog) {
        int f = graph_dlog_field(sens);
        if (f < 0) return 0;
        return graph_series_dlog(f, win_s, s_gbuf, GRAPH_MAXPTS, mn, mx, span);
    }
    return sensor_hist_series((sensor_id_t)sens, win_s, s_gbuf, GRAPH_MAXPTS,
                              mn, mx, NULL, span);
}

/* Polyline z s_gbuf (n bodu) do plochy `in`, mapovano [vmin..vmax] -> vyska. */
static void graph_plot(prim_rect_t in, int n, float vmin, float vmax, prim_color_t col)
{
    if (n < 2) return;
    float span = vmax - vmin; if (span < 1e-6f) span = 1.0f;
    int16_t px = in.x, py;
    { float t = (s_gbuf[0] - vmin) / span; if (t < 0) t = 0; if (t > 1) t = 1;
      py = (int16_t)(in.y + in.h - 1 - (int)(t * (in.h - 1))); }
    for (int i = 1; i < n; i++) {
        int16_t x = (int16_t)(in.x + (int32_t)i * (in.w - 1) / (n - 1));
        float t = (s_gbuf[i] - vmin) / span; if (t < 0) t = 0; if (t > 1) t = 1;
        int16_t y = (int16_t)(in.y + in.h - 1 - (int)(t * (in.h - 1)));
        prim_draw_line((prim_point_t){px, py}, (prim_point_t){x, y}, 2, col);
        px = x; py = y;
    }
}

/* Mrizka grafu: podklad BG_CARD + 3 vodorovne delici cary (ctvrtiny). */
static void graph_grid(prim_rect_t in)
{
    prim_fill_rect(in, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    for (int q = 1; q < 4; q++) {
        int16_t y = (int16_t)(in.y + (int32_t)q * (in.h - 1) / 4);
        prim_draw_line((prim_point_t){in.x, y},
                       (prim_point_t){(int16_t)(in.x + in.w - 1), y}, 1, UI_COLOR_INK_5);
    }
}

/* Graf teplot: sdilena osa pres vsechny dostupne serie + legenda s akt. hodnotou. */
static void graph_render_temps(int32_t win_s, int use_dlog)
{
    float gmn = 1e30f, gmx = -1e30f; int any = 0;
    int cnt[4];
    for (int k = 0; k < 4; k++) {
        float mn, mx;
        cnt[k] = graph_fetch(GRAPH_TEMP[k].id, win_s, use_dlog, &mn, &mx, NULL);
        if (cnt[k] >= 2) { if (mn < gmn) gmn = mn; if (mx > gmx) gmx = mx; any = 1; }
    }
    graph_grid(GRAPH_PLOT_T);
    if (!any) {
        prim_draw_text((prim_point_t){(int16_t)(GRAPH_PLOT_T.x + GRAPH_PLOT_T.w / 2),
                       (int16_t)(GRAPH_PLOT_T.y + GRAPH_PLOT_T.h / 2)},
                       "cekam na data", &ui_font_sans_18, UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
    } else {
        float pad = (gmx - gmn) * 0.08f; if (pad < 0.1f) pad = 0.1f;
        gmn -= pad; gmx += pad;
        for (int k = 0; k < 4; k++) {
            if (cnt[k] < 2) continue;
            int c2 = graph_fetch(GRAPH_TEMP[k].id, win_s, use_dlog, NULL, NULL, NULL);
            if (c2 >= 2) graph_plot(GRAPH_PLOT_T, c2, gmn, gmx, graph_line_col(k));
        }
        /* Y popisky (min/max osy) vlevo. */
        char b[16];
        fmt_fixed(b, sizeof b, gmx, 1);
        dtext(GRAPH_PLOT_T.x + 2, (int16_t)(GRAPH_PLOT_T.y + 14), 70, b, UI_COLOR_INK_3, &ui_font_mono_14);
        fmt_fixed(b, sizeof b, gmn, 1);
        dtext(GRAPH_PLOT_T.x + 2, (int16_t)(GRAPH_PLOT_T.y + GRAPH_PLOT_T.h - 4), 70, b, UI_COLOR_INK_3, &ui_font_mono_14);
    }
    /* Legenda: [•] name value, 4 polozky v rade pod grafem. */
    for (int k = 0; k < 4; k++) {
        int16_t lx = (int16_t)(GRAPH_CARD_T.x + 12 + k * 132);
        int16_t ly = 222;
        char v[24];
        const sensor_stat_t *s = &g_sensors[GRAPH_TEMP[k].id];
        int avail = (cnt[k] >= 2);
        prim_fill_circle((prim_point_t){(int16_t)(lx + 4), (int16_t)(ly - 5)}, 4,
                         avail ? graph_line_col(k) : UI_COLOR_INK_5);
        if (s->samples) { char n[12]; fmt_fixed(n, sizeof n, s->last, 1);
                          snprintf(v, sizeof v, "%s %s", GRAPH_TEMP[k].name, n); }
        else            snprintf(v, sizeof v, "%s --", GRAPH_TEMP[k].name);
        dtext((int16_t)(lx + 14), ly, 116, v,
              avail ? UI_COLOR_INK_2 : UI_COLOR_INK_4, &ui_font_mono_14);
    }
}

/* Graf OCXO ladiciho napeti (jedna serie, autoscale) + overlay min/max/okno. */
static void graph_render_vc(int32_t win_s, int use_dlog)
{
    float mn, mx; int32_t span = 0;
    int n = graph_fetch(SENS_ADS0, win_s, use_dlog, &mn, &mx, &span);
    graph_grid(GRAPH_PLOT_V);
    if (n < 2) {
        prim_draw_text((prim_point_t){(int16_t)(GRAPH_PLOT_V.x + GRAPH_PLOT_V.w / 2),
                       (int16_t)(GRAPH_PLOT_V.y + GRAPH_PLOT_V.h / 2)},
                       "cekam na data", &ui_font_sans_18, UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
        return;
    }
    float pad = (mx - mn) * 0.08f; if (pad < 1.0f) pad = 1.0f;
    graph_plot(GRAPH_PLOT_V, n, mn - pad, mx + pad, UI_COLOR_ACC);
    char b[16], o[40], sp[16];
    fmt_fixed(b, sizeof b, mx, 0);
    dtext(GRAPH_PLOT_V.x + 2, (int16_t)(GRAPH_PLOT_V.y + 14), 80, b, UI_COLOR_INK_3, &ui_font_mono_14);
    fmt_fixed(b, sizeof b, mn, 0);
    dtext(GRAPH_PLOT_V.x + 2, (int16_t)(GRAPH_PLOT_V.y + GRAPH_PLOT_V.h - 4), 80, b, UI_COLOR_INK_3, &ui_font_mono_14);
    screen_main_fmt_dur(sp, sizeof sp, span);
    snprintf(o, sizeof o, "%ld mV rozsah  ~%s", lround_f(mx - mn), sp);
    dtext((int16_t)(GRAPH_PLOT_V.x + GRAPH_PLOT_V.w - 200), (int16_t)(GRAPH_PLOT_V.y + 14),
          200, o, UI_COLOR_INK_3, &ui_font_mono_14);
}

/* Vertikalni bargrafy vpravo: aktualni hodnota vetve + nominalni marker (#32). */
static void graph_render_bars(void)
{
    for (int k = 0; k < 5; k++) {
        int16_t bx = (int16_t)(585 + k * 42);   /* uzsi bary (22 px), vetsi rozestup */
        const sensor_stat_t *s = &g_sensors[GRAPH_BAR[k].id];
        float lo = GRAPH_BAR[k].lo, hi = GRAPH_BAR[k].hi, nom = GRAPH_BAR[k].nom;
        float val = s->last;
        int16_t pct = (int16_t)((val - lo) * 100.f / (hi - lo));
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        int16_t nomp = (int16_t)((nom - lo) * 100.f / (hi - lo));
        int ok = s->valid && s->samples &&
                 (val > nom - (hi - lo) * 0.15f) && (val < nom + (hi - lo) * 0.15f);
        /* 55 segmentu (3 px, VGAP 1 -> plni stopu 219 px presne) = jemne rozliseni. */
        ui_bargraph_t bar = {
            .rect = {bx, 118, 22, 219}, .value_pct = s->samples ? pct : 0,
            .color = s->samples ? (ok ? UI_COLOR_OK : UI_COLOR_WARN) : UI_COLOR_INK_5,
            .vertical = 1, .segs = 55, .nominal_pct = nomp };
        ui_bargraph_render(&bar);
        /* hodnota nad barem (V) + popisek pod nim (stred = bx+11). */
        char v[12];
        if (s->samples) fmt_fixed(v, sizeof v, val / 1000.f, 1);
        else            snprintf(v, sizeof v, "--");
        dtext_c((int16_t)(bx + 11), 112, 42, v,
                s->valid ? UI_COLOR_INK : UI_COLOR_INK_3, &ui_font_mono_14);
        dtext_c((int16_t)(bx + 11), 356, 42, GRAPH_BAR[k].lab, UI_COLOR_INK_3, &ui_font_mono_14);
    }
}

/* Footer: presety okna (-/+) + hodnota + zdroj (RAM/datalog). */
static void graph_render_footer(void)
{
    ui_button_t m = {.rect = GRAPH_MINUS, .variant = UI_BUTTON_NORMAL, .label = "-"};
    ui_button_t p = {.rect = GRAPH_PLUS,  .variant = UI_BUTTON_NORMAL, .label = "+"};
    ui_button_render(&m);
    ui_button_render(&p);
    prim_fill_rect_rounded((prim_rect_t){112, 419, 98, 57}, 6, UI_COLOR_BG_CARD, PRIM_BLEND_OVER);
    char v[16];
    screen_main_fmt_dur(v, sizeof v, GRAPH_PRESETS[s_graph_idx].secs);
    prim_draw_text((prim_point_t){161, 450}, v, &ui_font_mono_22, UI_COLOR_INK, PRIM_ALIGN_CENTER);
    prim_draw_text((prim_point_t){161, 470},
                   GRAPH_PRESETS[s_graph_idx].dlog ? "datalog" : "RAM",
                   &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
    /* Prepinac na sesterske okno PREHLED KANALU (horizontalni bargrafy). */
    ui_button_t hb = {.rect = GRAPH_BARS_BTN, .variant = UI_BUTTON_NORMAL,
                      .label = "PREHLED >"};
    ui_button_render(&hb);
}

/* Prekresli VSECHEN dynamicky obsah (grafy + bargrafy). Kazdy kus si cisti box. */
static void graph_render_dynamic(void)
{
    int32_t win = GRAPH_PRESETS[s_graph_idx].secs;
    int dlog = GRAPH_PRESETS[s_graph_idx].dlog;
    graph_render_temps(win, dlog);
    graph_render_vc(win, dlog);
    graph_render_bars();
}

static void app_gpsdo_render_graphs(void)
{
    static uint32_t s_key;
    int first = window_first(29);
    if (first) {
        s_view = 29;
        window_chrome("GRAFY", WIN_TITLE_Y);
        ui_card_t ct = {.rect = GRAPH_CARD_T, .header_label = "Teploty [C] v case"};
        ui_card_render_chrome(&ct);
        ui_card_t cv = {.rect = GRAPH_CARD_V, .header_label = "OCXO ladici napeti [mV]"};
        ui_card_render_chrome(&cv);
        ui_card_t cb = {.rect = GRAPH_CARD_B, .header_label = "Napajeci vetve [V]"};
        ui_card_render_chrome(&cb);
        graph_render_footer();
    }
    /* Change-key: RAM okna se hybou 1x/s (uptime — cteni z RAM je levne). DATALOG
     * okna se renderuji JEN pri vstupu / zmene presetu (klic stabilni per preset):
     * jeden render dela az stovky blokujicich QSPI read_back -> periodicke
     * obnovovani by v UiTasku porusilo pravidlo "zadny spin >10 ms" (viz CLAUDE.md).
     * Dlouha historie je stejne minuty/hodiny stara, takze 1x pri otevreni staci. */
    uint32_t key;
    if (GRAPH_PRESETS[s_graph_idx].dlog)
        key = 0x20000000u | (uint32_t)s_graph_idx;                 /* stabilni per preset */
    else
        key = ((uint32_t)s_graph_idx << 24) ^ g_uptime_s;         /* 1x/s posun */
    if (first || key != s_key) {
        s_key = key;
        graph_render_dynamic();
        present_now();
    }
}

/* ── Okno PREHLED KANALU (s_view=30) — horizontalni bargrafy AKTUALNICH hodnot ──
 * Sesterske okno k GRAFY (#31): kdezto GRAFY ukazuje casovy prubeh, tady jsou
 * vsechny kanaly (teploty + napajeni + OCXO Vc + RF) jako vodorovne bary s
 * nominalnim markerem a cislenou hodnotou -> odchylka od ocekavane hodnoty na
 * prvni pohled. Vsechna data jsou REALNA (g_sensors[], SensorsTask 2 Hz), na
 * rozdil od simulovaneho headline. Prepina se tlacitkem (bez nav_push).
 *
 * Layout radku: [label vlevo] [track uprostred + fill + nominal] [hodnota vpravo].
 * Track/hodnota se prekresluji jen pri ZMENE (per-radek dchg-styl), label je
 * staticky (kresli se pri prvnim vstupu). */
#define HBAR_ROWS 10
static const int16_t HB_LBL_X   = 32;    /* label vlevo                     */
static const int16_t HB_TRACK_X = 210;   /* zacatek stopy baru              */
static const int16_t HB_TRACK_W = 452;   /* sirka stopy (210..662)          */
static const int16_t HB_VAL_XR  = 770;   /* prava hrana boxu hodnoty        */
static const int16_t HB_VAL_W   = 100;   /* box hodnoty (670..770)          */
static const prim_rect_t HB_CARD_T = {18, 58,  764, 150};   /* Teploty  */
static const prim_rect_t HB_CARD_V = {18, 216, 764, 194};   /* Napajeni */

/* Popis radku: id senzoru, label, rozsah baru [lo..hi], nominal (<=lo = zadny
 * marker/ok-warn, jen valid=zelena), scale (last -> zobrazovana jednotka),
 * pocet desetin, jednotka. RF (index 9) ma rf=1 = prepocet mV->dBm z g_calib. */
static const struct {
    uint8_t id; const char *lab; float lo, hi, nom, scale; uint8_t deci;
    const char *unit; uint8_t rf;
} HBAR[HBAR_ROWS] = {
    { SENS_T48,    "STM board",  0.f, 70.f,  -1.f, 1.f,     1, " C",   0 },
    { SENS_CORE_T, "MCU jadro",  0.f, 90.f,  -1.f, 1.f,     1, " C",   0 },
    { SENS_T49,    "OCXO",       0.f, 70.f,  -1.f, 1.f,     1, " C",   0 },
    { SENS_T4A,    "FPGA board", 0.f, 70.f,  -1.f, 1.f,     1, " C",   0 },
    { SENS_ADS2,   "12V vetev", 10800.f, 13200.f, 12000.f, 0.001f, 2, " V",   0 },
    { SENS_ADS3,   "5V vetev",   4500.f,  5500.f,  5000.f, 0.001f, 2, " V",   0 },
    { SENS_VDDA,   "REF 2V5",    2300.f,  2700.f,  2500.f, 0.001f, 2, " V",   0 },
    { SENS_VBAT,   "VBAT",       2500.f,  3300.f,  3000.f, 0.001f, 2, " V",   0 },
    { SENS_ADS0,   "OCXO Vc",       0.f,  3300.f,  1650.f, 0.001f, 2, " V",   0 },
    { SENS_ADS1,   "RF level",    -80.f,   10.f,  -100.f, 1.f,     1, " dBm", 1 },  /* nom < lo = zadny REF marker (RF nema ocekavanou hodnotu) */
};
/* Stred radku (y) pro index r (0..3 = karta Teploty, 4..9 = karta Napajeni). */
static int16_t hbar_cy(int r)
{
    return (r < 4) ? (int16_t)(110 + r * 28)          /* 110/138/166/194     */
                   : (int16_t)(266 + (r - 4) * 26);   /* 266..396 (6 radku)  */
}

/* ── Segmentovany horizontalni bar (#47 granularita) ─────────────────────────
 * Stopa je slozena z HB_SEGS malych segmentu; vyplneny usek = AKTUALNI hodnota,
 * plus barevne markery MIN / MAX / REFERENCNI (nominal) prekresli svuj segment
 * -> na prvni pohled aktualni hodnota vs. dosud videny rozsah [min,max] i
 * ocekavana (nominalni) hodnota. Barvy viz legenda (hbar_legend):
 *   AKT = zelena/amber vypln, REF = accent, MIN = violet, MAX = cervena. */
#define HB_SEGS  45          /* pocet segmentu (452 px / ~10 px na segment) */
#define HB_SGAP  2           /* mezera mezi segmenty [px]                   */

/* Prepocet SYROVE hodnoty senzoru (s->last/min/max) na zobrazovanou jednotku. */
static float hbar_disp(int r, float raw)
{
    if (HBAR[r].rf) {   /* AD8307: dBm = mV/slope + intercept */
        float slope = g_calib.ad8307_slope_mv_db; if (slope < 1e-3f) slope = 25.f;
        return raw / slope + g_calib.ad8307_intercept_dbm;
    }
    return raw * HBAR[r].scale;
}
/* Zobrazovana hodnota -> pct (0..100) v rozsahu baru [lo..hi]. */
static int16_t hbar_pct_disp(int r, float disp)
{
    float bar = (disp - HBAR[r].lo) / (HBAR[r].hi - HBAR[r].lo);
    if (bar < 0.f) bar = 0.f; else if (bar > 1.f) bar = 1.f;
    return (int16_t)(bar * 100.f + 0.5f);
}
/* Obdelnik segmentu i uvnitr stopy tr (vyska = stopa bez 1px okraje). */
static prim_rect_t hb_seg(prim_rect_t tr, int16_t i)
{
    int16_t sw = (int16_t)((tr.w - (HB_SEGS - 1) * HB_SGAP) / HB_SEGS);
    if (sw < 1) sw = 1;
    int16_t sx = (int16_t)(tr.x + i * (sw + HB_SGAP));
    return (prim_rect_t){ sx, (int16_t)(tr.y + 1), sw, (int16_t)(tr.h - 2) };
}
/* Segment, ktery kryje danou pct pozici (marker). */
static int16_t hb_marker_idx(int16_t pct)
{
    int16_t i = (int16_t)((int32_t)pct * HB_SEGS / 100);
    if (i >= HB_SEGS) i = (int16_t)(HB_SEGS - 1);
    if (i < 0) i = 0;
    return i;
}

/* Zobrazovana hodnota radku r + odpovidajici bar_pct (0..100). Vraci 1 kdyz
 * jsou platna data (samples>0). RF prepocita mV->dBm z kalibrace. */
static int hbar_value(int r, char *out, size_t n, int16_t *pct_out)
{
    const sensor_stat_t *s = &g_sensors[HBAR[r].id];
    if (!s->samples) { snprintf(out, n, "--"); *pct_out = 0; return 0; }
    float disp = hbar_disp(r, s->last);
    *pct_out = hbar_pct_disp(r, disp);
    char num[16]; fmt_fixed(num, sizeof num, disp, HBAR[r].deci);
    snprintf(out, n, "%s%s", num, HBAR[r].unit);
    return 1;
}

/* Kompaktni legenda barev markeru — ve footeru vpravo od tlacitka "< GRAFY"
 * (kresli se jednou). ⚠️ NELZE vedle nadpisu: window_chrome kresli titulek
 * CENTROVANE (SCREEN_W/2, mono_25) a "PREHLED KANALU" saha az ~x=505 -> kolize. */
static void hbar_legend(void)
{
    struct { const char *t; prim_color_t c; } it[4] = {
        { "AKT", UI_COLOR_OK },     { "REF", UI_COLOR_ACC },
        { "MIN", UI_COLOR_VIOLET }, { "MAX", UI_COLOR_BAD },
    };
    int16_t x = 250;                 /* za tlacitkem GRAFY (18..218), y = stred footeru */
    for (int i = 0; i < 4; i++) {
        prim_fill_rect((prim_rect_t){x, 440, 14, 14}, it[i].c, PRIM_BLEND_OVER);
        prim_draw_text((prim_point_t){(int16_t)(x + 18), 452}, it[i].t,
                       &ui_font_mono_16, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        x = (int16_t)(x + 78);
    }
}

/* Prekresli dynamickou cast JEDNOHO radku (track + segmenty + markery + hodnota).
 * Cely radek se nejdriv vycisti (REPLACE) -> mark_dirty pokryje copy-forward. */
static void hbar_row_draw(int r, int16_t pct, int16_t minp, int16_t maxp,
                          int valid, int samples, const char *val)
{
    int16_t cy = hbar_cy(r);
    /* Vycisti dynamickou zonu radku (za labelem az po hodnotu). */
    prim_fill_rect((prim_rect_t){HB_TRACK_X, (int16_t)(cy - 11),
                   (int16_t)(HB_VAL_XR - HB_TRACK_X), 22}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    /* Stopa (podklad). */
    prim_rect_t tr = {HB_TRACK_X, (int16_t)(cy - 7), HB_TRACK_W, 14};
    prim_fill_rect_rounded(tr, 3, UI_COLOR_INK_5, PRIM_BLEND_OVER);
    prim_stroke_rect_rounded(tr, 3, 1, UI_COLOR_LINE);
    if (samples) {
        int has_nom = (HBAR[r].nom > HBAR[r].lo);
        int ok = 1;
        if (has_nom) {
            float dev  = (HBAR[r].hi - HBAR[r].lo) * 0.15f;
            float disp = HBAR[r].lo + (HBAR[r].hi - HBAR[r].lo) * pct / 100.f;
            ok = (disp > HBAR[r].nom - dev) && (disp < HBAR[r].nom + dev);
        }
        /* AKTUALNI = vyplneny usek segmentu (zelena/amber dle odchylky). */
        prim_color_t base = valid ? (ok ? UI_COLOR_OK : UI_COLOR_WARN) : UI_COLOR_INK_4;
        int16_t lit = (int16_t)(((int32_t)pct * HB_SEGS + 50) / 100);
        for (int16_t i = 0; i < lit; i++)
            prim_fill_rect(hb_seg(tr, i), base, PRIM_BLEND_OVER);
        /* Markery kresli PRES vypln (at jsou videt i uvnitr zelene): */
        prim_fill_rect(hb_seg(tr, hb_marker_idx(minp)), UI_COLOR_VIOLET, PRIM_BLEND_OVER); /* MIN */
        prim_fill_rect(hb_seg(tr, hb_marker_idx(maxp)), UI_COLOR_BAD,    PRIM_BLEND_OVER); /* MAX */
        if (has_nom) {                                                  /* REFERENCNI (nominal) */
            int16_t refp = hbar_pct_disp(r, HBAR[r].nom);
            prim_fill_rect(hb_seg(tr, hb_marker_idx(refp)), UI_COLOR_ACC, PRIM_BLEND_OVER);
        }
    }
    /* Hodnota vpravo (vlastni box-clear). */
    dtext_a(HB_VAL_XR, (int16_t)(cy + 6), HB_VAL_W, val,
            samples ? (valid ? UI_COLOR_INK : UI_COLOR_INK_3) : UI_COLOR_INK_4,
            &ui_font_mono_16, DTEXT_RIGHT);
}

static void app_gpsdo_render_hbars(void)
{
    static int16_t  s_pct[HBAR_ROWS], s_minp[HBAR_ROWS], s_maxp[HBAR_ROWS];
    static char     s_val[HBAR_ROWS][16];
    int first = window_first(30);
    if (first) {
        s_view = 30;
        window_chrome("PREHLED KANALU", WIN_TITLE_Y);
        hbar_legend();
        ui_card_t ct = {.rect = HB_CARD_T, .header_label = "Teploty [C]"};
        ui_card_render_chrome(&ct);
        ui_card_t cv = {.rect = HB_CARD_V, .header_label = "Napajeni + RF"};
        ui_card_render_chrome(&cv);
        /* Staticke labely radku (kresli se jednou; dynamicka cast je vpravo). */
        for (int r = 0; r < HBAR_ROWS; r++)
            prim_draw_text((prim_point_t){HB_LBL_X, (int16_t)(hbar_cy(r) + 6)},
                           HBAR[r].lab, &ui_font_mono_16, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        /* Prepinac zpet na GRAFY (sesterske okno). */
        ui_button_t gb = {.rect = HBARS_GRAF_BTN, .variant = UI_BUTTON_NORMAL,
                          .label = "< GRAFY"};
        ui_button_render(&gb);
    }
    /* Per-radek zmena: prekresli jen radky, kde se zmenilo pct/min/max nebo text. */
    int drew = first;
    for (int r = 0; r < HBAR_ROWS; r++) {
        char val[16]; int16_t pct;
        const sensor_stat_t *s = &g_sensors[HBAR[r].id];
        int valid = s->valid;
        hbar_value(r, val, sizeof val, &pct);
        int16_t minp = s->samples ? hbar_pct_disp(r, hbar_disp(r, s->min)) : 0;
        int16_t maxp = s->samples ? hbar_pct_disp(r, hbar_disp(r, s->max)) : 0;
        if (first || pct != s_pct[r] || minp != s_minp[r] || maxp != s_maxp[r]
                  || strcmp(val, s_val[r]) != 0) {
            s_pct[r] = pct; s_minp[r] = minp; s_maxp[r] = maxp;
            snprintf(s_val[r], sizeof s_val[r], "%s", val);
            hbar_row_draw(r, pct, minp, maxp, valid, s->samples, val);
            drew = 1;
        }
    }
    if (drew) present_now();
}

/* ── Okno MATH / LIMITY (s_view=31, #43 Math Mx+B + #44 limit pass/fail) ──────
 * Aplikuje na mereny kmitocet (screen_main_freq_hz — DNES SIMULACE, po #2 realny)
 * transformaci Y = M*X + B (+ relativni NULL) a limitni pass/fail test. Konfigurace
 * v g_meas_cfg (meas_math.h); prubezne vyhodnoceni + alarm bezi i mimo okno
 * (app_gpsdo_tick_stats_sample -> g_meas_verdict -> alarm.c). Vstup: dlazdice
 * "Math/Limity" v Menu. */
static const double MATH_M_PRESETS[]    = {0.5, 1.0, 2.0, 10.0, 100.0};
#define MATH_M_N ((int)(sizeof(MATH_M_PRESETS)/sizeof(MATH_M_PRESETS[0])))
static const double MATH_BAND_PRESETS[] = {0.001, 0.01, 0.1, 1.0, 10.0};
#define MATH_BAND_N ((int)(sizeof(MATH_BAND_PRESETS)/sizeof(MATH_BAND_PRESETS[0])))
#define MATH_B_STEP 1.0                 /* krok offsetu B [Hz] na tap */
static int s_math_m_idx  = 1;           /* ×1.0 */
static int s_math_band_idx = 3;         /* ±1 Hz */

/* Rozvrzeni (dve karty). ⚠️ Tlacitka h=64 (7,5 mm) — projektovy standard dotyk.
 * cile z TODO #11 (56 px = 6,6 mm bylo pod hranici 7 mm). */
static const prim_rect_t MATH_CARD_A = {18, 58,  764, 182};   /* Vypocet Y=M*X+B  */
static const prim_rect_t MATH_CARD_B = {18, 246, 764, 166};   /* Limity pass/fail */
#define MATH_LBL_X    40
#define MATH_VAL_XR   760              /* prava hrana boxu X/Y hodnoty */
#define MATH_VAL_W    420
#define MATH_X_BASE   120
#define MATH_Y_BASE   152
/* Tlacitka karty A (rada, y=170 h=64 -> 170..234, karta konci 240). */
static const prim_rect_t MATH_BTN_MATH = {30, 170, 150, 64};
static const prim_rect_t MATH_BTN_M    = {192, 170, 120, 64};
static const prim_rect_t MATH_BTN_BM   = {324, 170, 64, 64};   /* B - */
static const prim_rect_t MATH_BOX_B    = {392, 170, 84, 64};   /* hodnota B */
static const prim_rect_t MATH_BTN_BP   = {480, 170, 64, 64};   /* B + */
static const prim_rect_t MATH_BTN_NULL = {556, 170, 204, 64};
/* Karta B (badge nahore, tlacitka y=346 h=64 -> 346..410, karta konci 412). */
static const prim_rect_t MATH_BADGE    = {30, 288, 190, 54};   /* verdikt PASS/FAIL */
static const prim_rect_t MATH_BTN_LIM  = {30, 346, 160, 64};
static const prim_rect_t MATH_BTN_BANDM= {200, 346, 120, 64};  /* pasmo - */
static const prim_rect_t MATH_BTN_BANDP= {330, 346, 120, 64};  /* pasmo + */
static const prim_rect_t MATH_BTN_ALRM = {460, 346, 200, 64};

/* Kmitocet [Hz] -> "10000000.00000 Hz" (bez %f — nano.specs; integer extrakce,
 * 5 desetin jako headline). Zaporne (po NULL) se znamenkem. */
static void fmt_hz(double v, char *out, size_t n)
{
    const char *sgn = (v < 0.0) ? "-" : "";
    double a = (v < 0.0) ? -v : v;
    if (a >= 4.2e9) { snprintf(out, n, "%s>4G Hz", sgn); return; }   /* uint32 strop */
    uint32_t whole = (uint32_t)a;
    uint32_t frac  = (uint32_t)((a - (double)whole) * 100000.0 + 0.5);
    if (frac >= 100000u) { whole++; frac -= 100000u; }
    snprintf(out, n, "%s%lu.%05lu Hz", sgn, (unsigned long)whole, (unsigned long)frac);
}

/* Dopocita UI preset indexy (M, pasmo) z g_meas_cfg — po nacteni z flash
 * (syscfg) drzi cfg hodnoty, ale indexy jsou app-lokalni. M je v cfg presne
 * (round-trip pres flash zachova bit-vzor), pasmo = (hi-lo)/2 s tolerance. */
static void math_sync_idx(void)
{
    s_math_m_idx = 1;                                   /* fallback x1.0 */
    for (int i = 0; i < MATH_M_N; i++)
        if (MATH_M_PRESETS[i] == g_meas_cfg.m) { s_math_m_idx = i; break; }
    double band = (g_meas_cfg.hi - g_meas_cfg.lo) * 0.5;
    for (int i = 0; i < MATH_BAND_N; i++) {
        double d = band - MATH_BAND_PRESETS[i]; if (d < 0) d = -d;
        if (d < MATH_BAND_PRESETS[i] * 0.01) { s_math_band_idx = i; break; }
    }
}

/* Pri zapnutych limitech nastavi meze = aktualni Y ± pasmo (bench "null then band"). */
static void math_recenter_limits(void)
{
    double y = meas_math_apply(&g_meas_cfg, screen_main_freq_hz());
    double band = MATH_BAND_PRESETS[s_math_band_idx];
    g_meas_cfg.lo = y - band;
    g_meas_cfg.hi = y + band;
}

/* Verdikt badge (zive — barva dle stavu). */
static void math_draw_badge(meas_verdict_t v)
{
    const char *t; prim_color_t bg;
    switch (v) {
    case MEAS_PASS: t = "PASS";    bg = UI_COLOR_OK;    break;
    case MEAS_LO:   t = "FAIL LO"; bg = UI_COLOR_BAD;   break;
    case MEAS_HI:   t = "FAIL HI"; bg = UI_COLOR_BAD;   break;
    default:        t = "---";     bg = UI_COLOR_INK_5; break;   /* limity vypnute */
    }
    prim_fill_rect(MATH_BADGE, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_fill_rect_rounded(MATH_BADGE, 8, bg, PRIM_BLEND_OVER);
    prim_draw_text((prim_point_t){(int16_t)(MATH_BADGE.x + MATH_BADGE.w / 2),
                   (int16_t)(MATH_BADGE.y + MATH_BADGE.h / 2 + 8)},
                   t, &ui_font_mono_22, UI_COLOR_BG_0, PRIM_ALIGN_CENTER);
}

/* Staticke prvky (karty + labely + poznamka) — jen pri prvnim vstupu. */
static void math_render_static(void)
{
    ui_card_t ca = {.rect = MATH_CARD_A, .header_label = "Vypocet   Y = M * X + B"};
    ui_card_render_chrome(&ca);
    ui_card_t cb = {.rect = MATH_CARD_B, .header_label = "Limity   (PASS / FAIL)"};
    ui_card_render_chrome(&cb);
    prim_draw_text((prim_point_t){MATH_LBL_X, MATH_X_BASE}, "X (mereno)",
                   &ui_font_mono_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){MATH_LBL_X, MATH_Y_BASE}, "Y (vysledek)",
                   &ui_font_mono_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){30, 462},
                   "Aplikuje se na mereny kmitocet (dnes simulace, viz #2).",
                   &ui_font_sans_16, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
}

/* Ovladace + Lo/Hi/pasmo text (stavove — pri vstupu a po kazdem tapu). */
static void math_render_controls(void)
{
    prim_fill_rect((prim_rect_t){26, 166, 748, 72},  UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_fill_rect((prim_rect_t){26, 284, 748, 128}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);

    /* Karta A: MATH / M / B-/B+/ NULL. */
    ui_button_t bm = {.rect = MATH_BTN_MATH,
                      .variant = g_meas_cfg.math_en ? UI_BUTTON_RUN : UI_BUTTON_NORMAL,
                      .label = g_meas_cfg.math_en ? "MATH ZAP" : "MATH VYP"};
    ui_button_render(&bm);
    char mb[16]; fmt_fixed(mb, sizeof mb, g_meas_cfg.m, 1);
    char ml[20]; snprintf(ml, sizeof ml, "M x%s", mb);
    ui_button_t bM = {.rect = MATH_BTN_M, .variant = UI_BUTTON_NORMAL, .label = ml};
    ui_button_render(&bM);
    ui_button_t bBm = {.rect = MATH_BTN_BM, .variant = UI_BUTTON_NORMAL, .label = "B -"};
    ui_button_t bBp = {.rect = MATH_BTN_BP, .variant = UI_BUTTON_NORMAL, .label = "B +"};
    ui_button_render(&bBm); ui_button_render(&bBp);
    prim_fill_rect_rounded(MATH_BOX_B, 6, UI_COLOR_BG_0, PRIM_BLEND_OVER);
    { char bv[16]; double b = g_meas_cfg.b; fmt_fixed(bv, sizeof bv, b < 0 ? -b : b, 1);
      char bt[20]; snprintf(bt, sizeof bt, "%s%s", b < 0 ? "-" : "+", bv);
      prim_draw_text((prim_point_t){(int16_t)(MATH_BOX_B.x + MATH_BOX_B.w / 2), 208},
                     bt, &ui_font_mono_16, UI_COLOR_INK, PRIM_ALIGN_CENTER); }
    ui_button_t bN = {.rect = MATH_BTN_NULL,
                      .variant = g_meas_cfg.null_en ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                      .label = g_meas_cfg.null_en ? "NULL ZAP" : "NULL"};
    ui_button_render(&bN);

    /* Karta B: Lo/Hi + pasmo text. */
    char tmp[28], line[40];
    fmt_hz(g_meas_cfg.lo, tmp, sizeof tmp); snprintf(line, sizeof line, "Lo  %s", tmp);
    prim_draw_text((prim_point_t){240, 308}, line, &ui_font_mono_16, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
    fmt_hz(g_meas_cfg.hi, tmp, sizeof tmp); snprintf(line, sizeof line, "Hi  %s", tmp);
    prim_draw_text((prim_point_t){240, 336}, line, &ui_font_mono_16, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
    fmt_hz(MATH_BAND_PRESETS[s_math_band_idx], tmp, sizeof tmp);
    snprintf(line, sizeof line, "Pasmo +/- %s", tmp);
    prim_draw_text((prim_point_t){560, 308}, line, &ui_font_mono_16, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);

    ui_button_t bL = {.rect = MATH_BTN_LIM,
                      .variant = g_meas_cfg.limit_en ? UI_BUTTON_RUN : UI_BUTTON_NORMAL,
                      .label = g_meas_cfg.limit_en ? "LIMITY ZAP" : "LIMITY VYP"};
    ui_button_render(&bL);
    ui_button_t bpm = {.rect = MATH_BTN_BANDM, .variant = UI_BUTTON_NORMAL, .label = "PASMO -"};
    ui_button_t bpp = {.rect = MATH_BTN_BANDP, .variant = UI_BUTTON_NORMAL, .label = "PASMO +"};
    ui_button_render(&bpm); ui_button_render(&bpp);
    ui_button_t bA = {.rect = MATH_BTN_ALRM,
                      .variant = g_meas_cfg.alarm_en ? UI_BUTTON_RUN : UI_BUTTON_NORMAL,
                      .label = g_meas_cfg.alarm_en ? "ALARM ZAP" : "ALARM VYP"};
    ui_button_render(&bA);
}

/* Zive: X, Y, verdikt badge, pocet FAIL (change-detect). */
static int math_render_live(int force)
{
    static char cX[28], cY[28], cV[24];
    double x = screen_main_freq_hz();
    double y = meas_math_apply(&g_meas_cfg, x);
    char bx[28], by[28];
    fmt_hz(x, bx, sizeof bx);
    fmt_hz(y, by, sizeof by);
    int drew = force;
    if (force || dchg(cX, sizeof cX, bx)) {
        dtext_a(MATH_VAL_XR, MATH_X_BASE, MATH_VAL_W, bx, UI_COLOR_INK, &ui_font_mono_18, DTEXT_RIGHT);
        drew = 1;
    }
    if (force || dchg(cY, sizeof cY, by)) {
        dtext_a(MATH_VAL_XR, MATH_Y_BASE, MATH_VAL_W, by, UI_COLOR_ACC, &ui_font_mono_18, DTEXT_RIGHT);
        drew = 1;
    }
    meas_verdict_t v = meas_limit_eval(&g_meas_cfg, y);
    char vb[24]; snprintf(vb, sizeof vb, "%d %u", (int)v, g_alarm_limit_fail);
    if (force || dchg(cV, sizeof cV, vb)) {
        math_draw_badge(v);
        char fc[20]; snprintf(fc, sizeof fc, "FAIL: %u", g_alarm_limit_fail);
        dtext_a(560, 336, 200, fc, UI_COLOR_INK_2, &ui_font_mono_16, DTEXT_LEFT);
        drew = 1;
    }
    return drew;
}

static void app_gpsdo_render_math(void)
{
    int first = window_first(31);
    if (first) {
        s_view = 31;
        math_sync_idx();                 /* preset indexy z (nactene) g_meas_cfg */
        window_chrome("MATH / LIMITY", WIN_TITLE_Y);
        math_render_static();
        math_render_controls();
    }
    if (math_render_live(first)) present_now();
}

/* ── Podokno PAMET (s_view=5), otevre se z System Health ──────────────────
 * Vyuziti pameti: interni FLASH/RAM (staticky z linker symbolu), RTOS heap
 * (live), externi SDRAM 32 MB + W25Q 64 MB (JEDEC). Staticke hodnoty se kresli
 * jednou (first); live se refreshuje jen RTOS heap. JEDEC se cte 1x pri otevreni
 * (⚠️ QSPI zatim bez mutexu — kolize s UART qspi* je nepravdepodobna, viz TODO). */
static int draw_mem_values(int force)
{
    static char c[20];
    char buf[24];
    int drew = force;
    /* POUZITE heap (konzistentni s "pouzite/celkem" hlavickou i FLASH/RAM sloupci):
     * used = celkem - volne. 32 KB = configTOTAL_HEAP_SIZE. */
    uint32_t total = 32u * 1024u;
    uint32_t used  = (total > g_rtos_heap_free) ? (total - g_rtos_heap_free) : 0u;
    snprintf(buf, sizeof buf, "%lu/32 KB", (unsigned long)(used / 1024u));
    if (force || dchg(c, sizeof c, buf)) {
        dval(DG_LVAL, SENS_R0 + 2 * SENS_DY, 175, buf, 1); drew = 1;   /* RTOS heap pouzite */
    }
    return drew;
}

void app_gpsdo_render_mem(void)
{
    int first = window_first(5);
    if (first) {
        s_view = 5;
        window_chrome("PAMET", WIN_TITLE_Y);
        ui_card_t card = {.rect = DG_CARD_FULL_A,
                          .header_label = "Vyuziti pameti  (pouzite / celkem)"};
        ui_card_render_chrome(&card);

        prim_draw_text((prim_point_t){DG_LLBL, 104}, "INTERNI", &ui_font_mono_18,
                       UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_RLBL, 104}, "EXTERNI", &ui_font_mono_18,
                       UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        dlabel(DG_LLBL, SENS_R0 + 0 * SENS_DY, "FLASH (CM7)");
        dlabel(DG_LLBL, SENS_R0 + 1 * SENS_DY, "RAM D1");
        dlabel(DG_LLBL, SENS_R0 + 2 * SENS_DY, "RTOS heap");
        dlabel(DG_RLBL, SENS_R0 + 0 * SENS_DY, "SDRAM (FMC)");
        dlabel(DG_RLBL, SENS_R0 + 1 * SENS_DY, "QSPI W25Q");
        dlabel(DG_RLBL, SENS_R0 + 2 * SENS_DY, "  JEDEC");

        char b[24];
        /* interni FLASH (CM7 bank 1024 KB): image = _sidata + velikost .data - 0x08000000 */
        uint32_t fl = ((uint32_t)&_sidata + ((uint32_t)&_edata - (uint32_t)&_sdata)) - 0x08000000u;
        snprintf(b, sizeof b, "%lu/1024 KB", (unsigned long)(fl / 1024u));
        dval(DG_LVAL, SENS_R0 + 0 * SENS_DY, 175, b, 1);
        /* interni RAM_D1 (512 KB): staticky .data + .bss */
        uint32_t rm = ((uint32_t)&_edata - (uint32_t)&_sdata) + ((uint32_t)&_ebss - (uint32_t)&_sbss);
        snprintf(b, sizeof b, "%lu/512 KB", (unsigned long)(rm / 1024u));
        dval(DG_LVAL, SENS_R0 + 1 * SENS_DY, 175, b, 1);
        /* externi (staticke velikosti) */
        dval(DG_RVAL, SENS_R0 + 0 * SENS_DY, 175, "32 MB", 1);   /* SDRAM FMC */
        dval(DG_RVAL, SENS_R0 + 1 * SENS_DY, 175, "64 MB", 1);   /* W25Q */
        uint32_t id = w25q_read_jedec();
        snprintf(b, sizeof b, "%06lX %s", (unsigned long)id, id == W25Q_JEDEC_ID ? "OK" : "--");
        dval(DG_RVAL, SENS_R0 + 2 * SENS_DY, 175, b, id == W25Q_JEDEC_ID);
    }
    if (draw_mem_values(first)) present_now();
}

/* Tlacitko lin/log Y v histogram okne (label odrazi aktualni stav osy). */
static void render_logy_btn(void)
{
    ui_button_t b = {.rect = LOGY_RECT, .variant = UI_BUTTON_NORMAL,
                     .label = screen_main_hist_logy() ? "Y: LOG" : "Y: LIN"};
    ui_button_render(&b);
}

/* ── Histogram okno (s_view=6): otevre se tapem na Allan kartu (hlavni obrazovka).
 * Leva cast = histogram distribuce y (mean/median/Gauss, lin/log Y), prava cast =
 * σy(τ) Allan tabulka. Ploty dela screen_main (ma data ring + ADEV pyramidu).
 * Change-key skip: prekresli se JEN pri zmene dat (stats_version, ~1x/s pri
 * RUN) nebo lin/log osy — pri STOPu tick nic nedela (zadny sort/Gauss/ADEV
 * naprazdno, zadny zbytecny flip). Vzorkovani bezi nezavisle na okne. */
void app_gpsdo_render_histogram(void)
{
    static uint32_t s_hist_key;
    int first = window_first(6);
    if (first) {
        s_view = 6;
        window_chrome("HISTOGRAM", WIN_TITLE_Y);
        ui_card_t card = {.rect = DG_CARD_FULL_A,
                          .header_label = "Rozdeleni y = (f-f0)/f0   |   Allan σy(τ)"};
        ui_card_render_chrome(&card);
        /* svisly delic mezi plotem a tabulkou */
        prim_draw_line((prim_point_t){572, 92}, (prim_point_t){572, 398}, 1, UI_COLOR_LINE);
        view_tabs_render(1);   /* zalozky ALLAN/HIST/SPEKTR (aktivni = HIST) */
    }
    uint32_t key = screen_main_stats_version()
                 ^ (screen_main_hist_logy() ? 0x80000000u : 0u);
    if (first || key != s_hist_key) {
        s_hist_key = key;
        render_logy_btn();                       /* label sleduje lin/log stav */
        screen_main_render_histogram(HIST_PLOT_RECT);
        screen_main_render_stats_table(HIST_TABLE_RECT);
        present_now();
    }
}

/* ── ALLAN fullscreen (s_view=23): velky log-log graf σy(τ) + tabulka. ───────
 * Otevira se tapem na Allan NAHLED na hlavni obrazovce (ktery od 2026-07-19
 * nema popisky os — tohle okno je ma). Tlacitko HISTOGRAM prepne na sesterske
 * okno rozdeleni (a tam zpet tlacitko ALLAN) — prepinani je BEZ nav_push,
 * takze BACK z obou vede tam, odkud byla dvojice otevrena. Sdili geometrii
 * plot/tabulka s histogramem (HIST_PLOT/TABLE_RECT). Zive pres change-key
 * stats_version (~1x/s pri RUN; vzorkovani bezi i mimo okno). */
/* Prepinac metriky (segmented) v okne ALLAN — vybrany = aktualni metrika. */
static void allan_metric_render(void)
{
    ui_segmented_t sc = {.rect = ALLAN_METRIC_RECT, .labels = ALLAN_METRIC_SEG,
                         .n = 3, .selected = (uint8_t)screen_main_allan_metric()};
    ui_segmented_render(&sc);
}

static void app_gpsdo_render_allan(void)
{
    static uint32_t s_allan_key;
    int first = window_first(23);
    if (first) {
        s_view = 23;
        /* ⚠️ Titulek jen ASCII — mono_25 (font nadpisu) NEMA recke glyfy σ/τ
         * (chybejici glyf se preskoci -> "ALLAN y()"); metriku nese prepinac dole
         * + Y osa. Header karty (sans_18, plny charset) je NEUTRALNI (metrika se
         * prepina) -> "Stabilita ...". */
        window_chrome("ALLAN", WIN_TITLE_Y);
        ui_card_t card = {.rect = DG_CARD_FULL_A,
                          .header_label = "Stabilita, log-log (X = τ [s])"};
        ui_card_render_chrome(&card);
        /* svisly delic mezi grafem a tabulkou (stejny jako v histogramu) */
        prim_draw_line((prim_point_t){572, 92}, (prim_point_t){572, 398}, 1, UI_COLOR_LINE);
        view_tabs_render(0);     /* zalozky ALLAN/HIST/SPEKTR (aktivni = ALLAN) */
        allan_metric_render();   /* prepinac ADEV/TDEV/MTIE (footer stred) */
    }
    uint32_t key = screen_main_stats_version();
    if (first || key != s_allan_key) {
        s_allan_key = key;
        screen_main_render_allan_big(HIST_PLOT_RECT);
        screen_main_render_stats_table(HIST_TABLE_RECT);
        present_now();
    }
}

/* Krok relativniho casoveho okna trendu (mezi presety). */
static void trend_secs_step(int dir)
{
    int cur = screen_main_trend_secs(), idx = 0;
    for (int k = 0; k < TREND_PRESET_N; k++) if (TREND_PRESETS[k] <= cur) idx = k;
    idx += dir;
    if (idx < 0) idx = 0; else if (idx >= TREND_PRESET_N) idx = TREND_PRESET_N - 1;
    screen_main_trend_set_secs(TREND_PRESETS[idx]);
}

/* -/+ tlacitka + hodnota okna mezi nimi (dolni lista trend okna). */
static void render_trend_scale_btns(void)
{
    ui_button_t m = {.rect = TREND_MINUS, .variant = UI_BUTTON_NORMAL, .label = "-"};
    ui_button_t p = {.rect = TREND_PLUS,  .variant = UI_BUTTON_NORMAL, .label = "+"};
    ui_button_render(&m);
    ui_button_render(&p);
    prim_fill_rect_rounded((prim_rect_t){112, 419, 98, 57}, 6, UI_COLOR_BG_CARD, PRIM_BLEND_OVER);
    char v[16];
    screen_main_fmt_dur(v, sizeof v, (int32_t)screen_main_trend_secs());   /* "10 min" / "6 h" / "30 d" */
    prim_draw_text((prim_point_t){161, 455}, v, &ui_font_mono_22, UI_COLOR_INK, PRIM_ALIGN_CENTER);
}

/* ── Trend fullscreen okno (s_view=9): tap na trend kartu na hlavni obrazovce.
 * Posledni s_trend_secs (30/60/120 s, tlacitka dole). Change-key skip. */
void app_gpsdo_render_trend(void)
{
    static uint32_t s_trend_key;
    int first = window_first(9);
    if (first) {
        s_view = 9;
        window_chrome("TREND  y = (f-f0)/f0", WIN_TITLE_Y);
        render_trend_scale_btns();
        ui_card_t card = {.rect = DG_CARD_FULL_A,
                          .header_label = "Frakcni odchylka v case"};
        ui_card_render_chrome(&card);
    }
    if (first || screen_main_stats_version() != s_trend_key) {
        s_trend_key = screen_main_stats_version();
        screen_main_render_trend_big((prim_rect_t){(int16_t)(DG_LX + 8), 96, (int16_t)(764 - 16), 300});
        present_now();
    }
}

/* ── Okno "O pristroji" (s_view=10): z Nastaveni. FW verze/build, autori, uptime,
 * selftest, (sériové cislo pozdeji z CALIB store). Staticke + uptime tick. */
void app_gpsdo_render_about(void)
{
    int first = window_first(10);
    if (first) {
        s_view = 10;
        window_chrome("O PRISTROJI", WIN_TITLE_Y);
        ui_card_t c1 = {.rect = {DG_LX, 62, 764, 200}, .header_label = "GPSDO / citac kmitoctu"};
        ui_card_render_chrome(&c1);
        prim_draw_text((prim_point_t){DG_LLBL, 108}, "Firmware:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 160), 108}, FW_VERSION_FULL, &ui_font_mono_18, UI_COLOR_INK, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 140}, "Build:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 160), 140}, __DATE__ " " __TIME__, &ui_font_mono_18, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 172}, "Autori:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 160), 172}, "OK2HAZ & OK2JNJ", &ui_font_mono_18, UI_COLOR_ACC, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 204}, "MCU:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 160), 204}, "STM32H757 (CM7+CM4) @ 480 MHz", &ui_font_mono_18, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 236}, "Serial:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 160), 236}, "(neprideleno)", &ui_font_mono_18, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);

        ui_card_t c2 = {.rect = {DG_LX, 274, 764, 130}, .header_label = "Stav"};
        ui_card_render_chrome(&c2);
        prim_draw_text((prim_point_t){DG_LLBL, 320}, "Uptime:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 356}, "Selftest:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    }
    /* zive: uptime (1x/s staci) + selftest verdikt */
    static char c_up[20], c_st[16];
    char buf[24];
    uint32_t s = g_uptime_s;
    snprintf(buf, sizeof buf, "%lu:%02lu:%02lu", (unsigned long)(s / 3600u),
             (unsigned long)((s / 60u) % 60u), (unsigned long)(s % 60u));
    if (first || dchg(c_up, sizeof c_up, buf)) {
        dtext((int16_t)(DG_LLBL + 160), 320, 300, buf, UI_COLOR_INK, &ui_font_mono_18);
        snprintf(buf, sizeof buf, "%s", g_selftest_res == 1 ? "PASS" : (g_selftest_res == 2 ? "FAIL" : "---"));
        dtext((int16_t)(DG_LLBL + 160), 356, 200, buf,
              g_selftest_res == 1 ? UI_COLOR_OK : (g_selftest_res == 2 ? UI_COLOR_BAD : UI_COLOR_INK_4),
              &ui_font_mono_18);
        (void)c_st;
        present_now();
    }
}

/* Zmena jasu o delta (krok), clamp [25..255] (nikdy uplna tma -> vzdy videt na ovladani).
 * Zapisuje g_brightness; HW aplikaci dela UiTask pod I2C4 mutexem. */
static void brightness_step(int delta)
{
    int v = (int)g_brightness + delta;
    if (v < 25) v = 25;
    if (v > 255) v = 255;
    g_brightness = (uint8_t)v;
    g_sys_cfg_dirty = 1;
}

/* Preset prodlevy auto-dim [s]; -/+ kroci mezi nimi. */
static const uint16_t DIM_PRESETS[] = {15, 30, 60, 120, 300, 600};
#define DIM_PRESET_N ((int)(sizeof(DIM_PRESETS) / sizeof(DIM_PRESETS[0])))

static void autodim_step(int dir)
{
    int i = 0;
    for (int k = 0; k < DIM_PRESET_N; k++) if (DIM_PRESETS[k] <= g_autodim_sec) i = k;
    i += dir;
    if (i < 0) i = 0; else if (i >= DIM_PRESET_N) i = DIM_PRESET_N - 1;
    g_autodim_sec = DIM_PRESETS[i];
    g_sys_cfg_dirty = 1;
}

/* ── Partial updaty ovladacu Nastaveni ──────────────────────────────────────
 * Tap na +/- apod. NEprekresluje cele okno (bg blit + 5 karet ~40-80 ms =
 * citelna latence), jen dotceny ovladac (~jednotky ms). Kazdy helper si vycisti
 * svou oblast (BG_CARD) POD hlavickou karty a prekresli button/hodnotu. */
static void settings_upd_mute(void)
{
    prim_fill_rect((prim_rect_t){28, 88, 184, 52}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    bool muted = g_sound_muted;
    if (muted) ui_icon_speaker_muted((prim_point_t){32, 84}, 40, UI_COLOR_BAD);
    else       ui_icon_speaker((prim_point_t){32, 84}, 40, UI_COLOR_OK);
    prim_draw_text((prim_point_t){88, 112}, muted ? "vypnut" : "zapnut",
                   &ui_font_sans_18, muted ? UI_COLOR_BAD : UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
    ui_button_t mb = {.rect = MUTE_RECT, .variant = UI_BUTTON_NORMAL,
                      .label = muted ? "ZAPNOUT" : "VYPNOUT"};
    ui_button_render(&mb);
}

/* Track/procenta jsou centrovane na stred BR_MINUS/PLUS (y=188,h=64 -> stred
 * 220; drive h=56 -> stred216). Posunuto +4 px se zvetsenim tlacitek.
 * Parametrizovano hodnotou (ne primo g_brightness) — item 1 animaci ("eased
 * jas"): skutecny HW backlight (g_brightness) se aplikuje OKAMZITE (UiTask,
 * ws_panel_set_backlight), ale VIZUALNI bar na obrazovce plynule dojizdi
 * (s_settings_br, viz settings_tick_jas) — hardwarove stmivani nesmi mit lag,
 * jen kresleny ukazatel. */
static void settings_draw_jas_bar(int16_t br)
{
    prim_fill_rect((prim_rect_t){194, 198, 188, 48}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_rect_t track = {196, 204, 118, 32};
    prim_fill_rect(track, UI_COLOR_BG_0, PRIM_BLEND_REPLACE);
    int16_t fillw = (int16_t)((int32_t)track.w * br / 255);
    if (fillw > 0)
        prim_fill_rect((prim_rect_t){track.x, track.y, fillw, track.h}, UI_COLOR_ACC, PRIM_BLEND_OVER);
    prim_stroke_rect_rounded(track, 2, 1, UI_COLOR_LINE);
    char pb[8]; snprintf(pb, sizeof pb, "%d%%", (int)br * 100 / 255);
    prim_draw_text((prim_point_t){324, 228}, pb, &ui_font_mono_22, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
}

/* Plny (okamzity) render na aktualni g_brightness — pouzito jen pri OTEVRENI
 * okna (kdy ma bar rovnou ukazovat spravnou hodnotu, zadny nabeh). */
static void settings_upd_jas(void) { settings_draw_jas_bar((int16_t)g_brightness); }

/* Eased dojezd baru k g_brightness (~20 Hz z app_gpsdo_tick_anim, jen s_view=7).
 * anim_step respektuje g_anim_enabled (VYP -> okamzity skok, chovani jako pred
 * timhle bodem). Kreslí se JEN pri zmene (anim_step vraci 0 v klidu). */
static anim_t s_settings_br;
static void settings_tick_jas(void)
{
    anim_set(&s_settings_br, (float)g_brightness);
    if (anim_step(&s_settings_br, 0.3f, 0.6f)) {
        prim_set_target(&s_fb);
        prim_reset_clip();
        settings_draw_jas_bar((int16_t)(s_settings_br.cur + 0.5f));
        s_dirty = 1;   /* flip odlozen na flush */
    }
}

/* Hodnota prodlevy centrovana mezi DIM_MINUS/PLUS (x=279, stejne jako drive);
 * y posunuto s ADEN_RECT/DIM_* (300->308, h56->64, stred 328->340). */
static void settings_upd_dim(void)
{
    ui_button_t adb = {.rect = ADEN_RECT, .variant = UI_BUTTON_NORMAL,
                       .label = g_autodim_en ? "ZAPNUTO" : "VYPNUTO"};
    ui_button_render(&adb);
    prim_fill_rect((prim_rect_t){244, 314, 72, 60}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    char tb[12]; snprintf(tb, sizeof tb, "%u s", (unsigned)g_autodim_sec);
    prim_draw_text((prim_point_t){279, 348}, tb, &ui_font_mono_22,
                   g_autodim_en ? UI_COLOR_INK_2 : UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
}

static void settings_upd_lang(void)
{
    ui_button_t lb = {.rect = LANG_RECT, .variant = UI_BUTTON_NORMAL,
                      .label = g_lang_en ? "ENGLISH" : "CESKY"};
    ui_button_render(&lb);
}

/* Krok rucniho posunu zony. V AUTO rezimu prvni stisk -/+ prepne na RUCNI a
 * naseje ho z prave platneho CET/CEST posunu (zadny skok na stare cislo). */
static void tz_step(int dir)
{
    if (g_tz_auto) {
        g_tz_auto = 0;
        g_tz_offset_h = (int8_t)(strncmp((const char *)g_tz_label, "CEST", 4) == 0 ? 2 : 1);
    }
    int tz = (int)g_tz_offset_h + dir;
    if (tz < -12) tz = -12;
    if (tz > 14)  tz = 14;
    g_tz_offset_h = (int8_t)tz;
    g_sys_cfg_dirty = 1;
}

/* ── Okno Nastaveni (s_view=7): otevre se z System Health -> "NASTAVENI".
 * DVOUSLOUPCOVE (jako diag): levy = Zvuk / Jas / Auto-dim, pravy = Vzhled
 * (tmave/svetle schema, runtime prepnuti palety) / Jazyk (infrastruktura;
 * texty se prepinaji postupne). Staticke (neni v ticku), prekresli se cele
 * pri tapu. Zapisuje g_* + dirty pro BKP persist (DR2 + DR6). */
void app_gpsdo_render_settings(void)
{
    window_prep();
    s_view = 7;
    window_chrome("NASTAVENI", WIN_TITLE_Y_TIGHT);
    anim_reset(&s_settings_br, (float)g_brightness);   /* bez nabehu pri OTEVRENI okna */

    /* ── Levy sloupec: Zvuk ── */
    ui_card_t c1 = {.rect = {DG_LX, 58, DG_COLW, 88},
                    .header_label = "Zvuk (alarmy)"};
    ui_card_render_chrome(&c1);
    settings_upd_mute();

    /* ── Levy sloupec: Jas ── (108 px, bylo 100 — button 56->64 px potreboval +8) */
    ui_card_t c2 = {.rect = {DG_LX, 156, DG_COLW, 108}, .header_label = "Jas displeje"};
    ui_card_render_chrome(&c2);
    ui_button_t bmin = {.rect = BR_MINUS, .variant = UI_BUTTON_NORMAL, .label = "-"};
    ui_button_t bplus = {.rect = BR_PLUS, .variant = UI_BUTTON_NORMAL, .label = "+"};
    ui_button_render(&bmin);
    ui_button_render(&bplus);
    settings_upd_jas();

    /* ── Levy sloupec: Auto-dim (zap/vyp + prodleva -/+) ── (posunuto 266->274,
     * 110 px misto 102 — stejny duvod jako c2; navazuje bez mezery navic). */
    ui_card_t c3 = {.rect = {DG_LX, 274, DG_COLW, 110},
                    .header_label = "Auto-dim (hodiny po necinnosti)"};
    ui_card_render_chrome(&c3);
    ui_button_t dmin = {.rect = DIM_MINUS, .variant = UI_BUTTON_NORMAL, .label = "-"};
    ui_button_t dplus = {.rect = DIM_PLUS, .variant = UI_BUTTON_NORMAL, .label = "+"};
    ui_button_render(&dmin);
    ui_button_render(&dplus);
    settings_upd_dim();

    /* ── Pravy sloupec: Vzhled (barevne schema) ── */
    ui_card_t c4 = {.rect = {DG_RX, 58, DG_COLW, 88}, .header_label = "Vzhled"};
    ui_card_render_chrome(&c4);
    prim_draw_text((prim_point_t){(int16_t)(DG_RX + 14), 112}, "Schema:",
                   &ui_font_sans_18, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
    ui_button_t thb = {.rect = THEME_RECT, .variant = UI_BUTTON_NORMAL,
                       .label = g_theme_light ? "SVETLE" : "TMAVE"};
    ui_button_render(&thb);

    /* ── Pravy sloupec: Jazyk ── */
    ui_card_t c5 = {.rect = {DG_RX, 156, DG_COLW, 82}, .header_label = "Jazyk / Language"};
    ui_card_render_chrome(&c5);
    settings_upd_lang();
    /* (Casova zona ma VLASTNI okno "Cas" — dlazdice v Menu, s_view=22.) */

    /* ── Reference Si5356 (presunuto z Menu dlazdice sem) ── */
    ui_button_t rb = {.rect = REF_RECT, .variant = UI_BUTTON_NORMAL, .label = "REFERENCE Si5356 >"};
    ui_button_render(&rb);

    /* ── O pristroji (tlacitko dolni pravy) ── */
    ui_button_t ab = {.rect = ABOUT_RECT, .variant = UI_BUTTON_NORMAL, .label = "O PRISTROJI >"};
    ui_button_render(&ab);

    /* ── Sestavy (uloz/nacti profil nastaveni) — footer vlevo od BACK ── */
    ui_button_t sb = {.rect = SETUP_ENTER_RECT, .variant = UI_BUTTON_NORMAL, .label = "SESTAVY >"};
    ui_button_render(&sb);

    present_now();
}

/* ── Screensaver hodiny (s_view=8): pri auto-dim misto ztlumene obrazovky velke
 * RTC hodiny na CERNEM pozadi (temer zhasnute pixely + jas 20/255 = setri panel).
 * Pozice se posouva s minutou (anti burn-in styl). Exit obnovi predchozi okno. */
static uint8_t s_prev_view = 0;
static char    s_saver_hms[10] = "";        /* zmenovy klic (prekresli 1x/s) */
static prim_rect_t s_saver_rect = {0,0,0,0}; /* minule kreslena oblast (k smazani) */

static void saver_draw(void)
{
    prim_set_target(&s_fb);
    prim_reset_clip();
    char rt[24];   /* lokalni zona (Nastaveni) — screensaver jsou "nastenne" hodiny */
    strncpy(rt, (const char *)g_rtc_text_local, sizeof rt - 1); rt[sizeof rt - 1] = '\0';
    /* "YYYY-MM-DD HH:MM:SS" -> datum [0..9], cas [11..18] */
    if (strlen(rt) < 19) return;
    if (strncmp(s_saver_hms, rt + 11, 8) == 0) return;   /* stejna sekunda */
    memcpy(s_saver_hms, rt + 11, 8); s_saver_hms[8] = '\0';

    /* drift pozice: kazdou minutu jinde (male kruzeni ±24 px) */
    int mi = (rt[14] - '0') * 10 + (rt[15] - '0');
    int16_t ox = (int16_t)(((mi % 5) - 2) * 12);
    int16_t oy = (int16_t)((((mi / 5) % 5) - 2) * 10);
    int16_t cx = (int16_t)(400 + ox), by = (int16_t)(250 + oy);   /* stred / baseline */

    /* smaz minulou oblast, pak spocitej novou (cas + datum pod nim).
     * Font hodin = ui_font_mono_75 (stejny jako headline kmitoctu na main). */
    if (s_saver_rect.w) prim_fill_rect(s_saver_rect, PRIM_RGB(0,0,0), PRIM_BLEND_REPLACE);
    char hh[3] = {rt[11], rt[12], 0}, mm[3] = {rt[14], rt[15], 0}, ss[3] = {rt[17], rt[18], 0};
    int16_t dw = prim_text_width("00", &ui_font_mono_75);
    int16_t colw = 28;                                   /* mezera na dvojtecku */
    int16_t total = (int16_t)(3 * dw + 2 * colw);
    int16_t x = (int16_t)(cx - total / 2);
    s_saver_rect = (prim_rect_t){(int16_t)(x - 4), (int16_t)(by - 82),
                                 (int16_t)(total + 8), 146};
    prim_draw_text((prim_point_t){x, by}, hh, &ui_font_mono_75, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){(int16_t)(x + dw + colw), by}, mm,
                   &ui_font_mono_75, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){(int16_t)(x + 2 * (dw + colw)), by}, ss,
                   &ui_font_mono_75, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    /* dvojtecky rucne (mono_75 ma jen cislice) */
    for (int c = 0; c < 2; c++) {
        int16_t dx = (int16_t)(x + dw * (c + 1) + colw * c + colw / 2 - 5);
        prim_fill_rect((prim_rect_t){dx, (int16_t)(by - 50), 10, 10}, UI_COLOR_INK_3, PRIM_BLEND_OVER);
        prim_fill_rect((prim_rect_t){dx, (int16_t)(by - 24), 10, 10}, UI_COLOR_INK_3, PRIM_BLEND_OVER);
    }
    /* datum + casove pasmo ("2026-07-16 CEST", pasmo dle volby v okne Cas) —
     * mono_25 (vetsi nez drivejsi 18) at je citelne pres mistnost; porad se
     * vejde do mazaci oblasti s_saver_rect (siroka dle cislic, konci by+64). */
    char db[26];
    snprintf(db, sizeof db, "%.10s %s", rt, (const char *)g_tz_label);
    prim_draw_text((prim_point_t){cx, (int16_t)(by + 44)}, db,
                   &ui_font_mono_25, UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
    present_now();
}

void app_gpsdo_enter_screensaver(void)
{
    app_gpsdo_init();
    if (s_view == 8) return;
    s_prev_view = s_view;
    s_view = 8;
    prim_set_target(&s_fb);
    prim_reset_clip();
    prim_fill_rect((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                   PRIM_RGB(0, 0, 0), PRIM_BLEND_REPLACE);
    s_saver_hms[0] = '\0';
    s_saver_rect = (prim_rect_t){0, 0, 0, 0};
    saver_draw();
    present_now();   /* i kdyby RTC text jeste nebyl platny (saver_draw return) */
}

void app_gpsdo_exit_screensaver(void)
{
    if (s_view != 8) return;
    switch (s_prev_view) {                 /* obnov okno, ktere bylo pred usnutim */
    case 1:  app_gpsdo_render_diag();      break;
    case 2:  app_gpsdo_render_gps();       break;
    case 3:  app_gpsdo_render_health();    break;
    case 4:  app_gpsdo_render_sensors();   break;
    case 5:  app_gpsdo_render_mem();       break;
    case 6:  app_gpsdo_render_histogram(); break;
    case 7:  app_gpsdo_render_settings();  break;
    case 9:  app_gpsdo_render_trend();     break;
    case 10: app_gpsdo_render_about();     break;
    case 12: app_gpsdo_render_menu();      break;
    default: app_gpsdo_render_main();      break;
    }
}

/* ── Boot splash: logo + FW/build + prubeh selftestu. Cerne pozadi, velky
 * nazev + akcentni linka; radek "Selftest" se prekresluje z g_selftest_res.
 * ── Fade-in (item 9): volajici (StartUiTask) drzi splash pres 10 volani
 * app_gpsdo_boot_splash_tick() po 100 ms (~1 s) PRED hlavni obrazovkou — na
 * rozdil od bezneho UiTask cyklu je tohle jednorazova sekvence PRED
 * schedulerovou smyckou, takze plny redraw kazdy tik (misto dirty-rect) je
 * v poradku (10x pri bootu, ne za behu). Obsah se kresli s barvou linearne
 * interpolovanou cerna->cil (fade_color) — kazdy tik NEJDRIV vycisti cele
 * pozadi na cernou (ne jen prekresli pres predchozi), jinak by se AA hrany
 * textu kumulovaly pres sebe (na rozdil od digit-highlight/button-flash
 * tricku tady barva mezi tiky NENI identicka, takze "presna stejna kresba
 * dvakrat" trik nefunguje — potrebuje skutecny clear). */
#define SPLASH_FADE_TICKS 8   /* z 10 celkovych tiku — poslednich ~200 ms na cilove barve */
static int s_splash_frame = 0;

static prim_color_t fade_color(prim_color_t c, float t)   /* t=0 cerna, t=1 cilova barva */
{
    if (t >= 1.0f) return c;
    if (t <= 0.0f) return PRIM_RGB(0, 0, 0);
    return PRIM_RGB((uint8_t)(PRIM_R(c) * t), (uint8_t)(PRIM_G(c) * t), (uint8_t)(PRIM_B(c) * t));
}

static void splash_draw_content(float t)
{
    prim_fill_rect((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                   PRIM_RGB(0, 0, 0), PRIM_BLEND_REPLACE);
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 180}, "GPSDO",
                   &ui_font_mono_75, fade_color(UI_COLOR_ACC, t), PRIM_ALIGN_CENTER);
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 218}, "citac kmitoctu",
                   &ui_font_sans_18, fade_color(UI_COLOR_INK_2, t), PRIM_ALIGN_CENTER);
    prim_draw_line((prim_point_t){260, 240}, (prim_point_t){540, 240}, 2, fade_color(UI_COLOR_LINE_HI, t));
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 268}, FW_VERSION_FULL "   " __DATE__,
                   &ui_font_mono_18, fade_color(UI_COLOR_INK_3, t), PRIM_ALIGN_CENTER);
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 452}, "OK2HAZ & OK2JNJ",
                   &ui_font_mono_18, fade_color(UI_COLOR_INK_4, t), PRIM_ALIGN_CENTER);
}

static void splash_status(void)   /* prekresli JEN status radek (selftest) */
{
    prim_set_target(&s_fb);
    prim_reset_clip();
    prim_fill_rect((prim_rect_t){0, 300, UI_DIM_SCREEN_W, 40}, PRIM_RGB(0, 0, 0), PRIM_BLEND_REPLACE);
    const char *st; prim_color_t sc;
    if      (g_selftest_res == 1) { st = "Selftest: PASS";  sc = UI_COLOR_OK; }
    else if (g_selftest_res == 2) { st = "Selftest: FAIL";  sc = UI_COLOR_BAD; }
    else                          { st = "Selftest ...";    sc = UI_COLOR_INK_3; }
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 326}, st,
                   &ui_font_mono_18, sc, PRIM_ALIGN_CENTER);
    present_now();
}

void app_gpsdo_boot_splash(void)
{
    window_prep();
    s_view = 11;
    s_splash_frame = 0;
    if (!g_anim_enabled) {   /* VYP -> rovnou cilove barvy, zadny fade */
        splash_draw_content(1.0f);
        s_splash_frame = SPLASH_FADE_TICKS;
    } else {
        splash_draw_content(0.0f);   /* t=0: cerna na cerne = neviditelne (prvni snimek fade-in) */
    }
    splash_status();
}

void app_gpsdo_boot_splash_tick(void)
{
    if (s_view != 11) return;
    if (s_splash_frame < SPLASH_FADE_TICKS) {
        s_splash_frame++;
        prim_set_target(&s_fb);
        prim_reset_clip();
        splash_draw_content((float)s_splash_frame / (float)SPLASH_FADE_TICKS);
    }
    splash_status();   /* zivy selftest radek — beze zmeny, bezi kazdy tik i po dojetem fade */
}

/* ── Menu (rozcestnik, s_view=12): z hlavni obrazovky tlacitkem MENU. Mrizka 3×4.
 * Obsahuje SYSTEM/NASTROJE (kontextova okna GPS/Histogram/Trend jsou dostupna
 * primo z hl. obrazovky pres pilulku/tap, NEjsou tu). Staticke (neni v ticku). */
extern volatile uint8_t g_reboot_req;
static void app_gpsdo_render_reference(void);       /* fwd (volano z menu_activate) */
static void app_gpsdo_render_kalib(void);
static void app_gpsdo_render_holdover(void);
static void app_gpsdo_render_datalog(void);
static void app_gpsdo_render_alarms(void);
static void app_gpsdo_render_counter(void);
static void app_gpsdo_render_selftest(void);
static void app_gpsdo_render_cas(void);
static void app_gpsdo_render_anim(void);
static void app_gpsdo_render_math(void);   /* fwd (volano z menu_activate) — okno Math/limity */
static void app_gpsdo_render_confirm_restart(void);
static void app_gpsdo_render_waterfall(void);   /* Spektrogram Δf (s_view=26) */
static void waterfall_tick(void);
static void app_gpsdo_render_ribbon(void);       /* Status ribbon demo (s_view=28) */
static void app_gpsdo_render_efekty(void);       /* Prepinace grafickych efektu (s_view=27) */
/* Pozn.: NEJSOU dlazdice (dostupne z kontextu, kam patri): Senzory + Diagnostika
 * = tlacitka v System Health; O pristroji + Reference = tlacitka v Nastaveni;
 * Pamet + Selftest = tlacitka ve footeru Diagnostiky (technicky hub).
 * Diagnostika ZUSTAVA i dlazdici (caste pouziti). */
enum { ACT_DIAG = 1, ACT_SETTINGS, ACT_HEALTH, ACT_COUNTER,
       ACT_KALIB, ACT_HOLDOVER, ACT_DATALOG, ACT_ALARMS, ACT_CAS,
       ACT_ANIM,             /* Animace/demo (s_view=24) — drive Placeholder 1 */
       ACT_RIBBON,           /* Status ribbon demo (s_view=28) — drive Placeholder 3 */
       ACT_MATH };           /* Math/limity (s_view=31, #43/#44) — drive Placeholder 2 */
/* Menu 3×4 = 12 dlazdic (2026-07-19 rozsireno z 3×3=9; 4. rada = Animace/Math/Status
 * ribbon — vsech 12 slotu je dnes obsazenych realnymi funkcemi). w=248, gap 14; h=76, gap 10
 * (y=68/154/240/326 -> radek4 konci 402, 15 px pred footerem 417 — bylo
 * h=88/gap12/y=72/172/272, 4. radek by se do puvodni vysky nevesel bez
 * zmenseni). Sloupce x viz komentar u MENU_ITEMS nize. Dotykovy cil 76 px =
 * 8,9 mm, porad nad doporucenymi 7 mm. Restart NENI dlazdice — je ve footeru
 * vpravo vedle ZPET (MENU_RESTART_RECT) jako systemova akce. */
#define MENU_N 12
/* x = 14/276/538 (bylo 24/286/548, 2026-07-19): puvodni sloupce mely
 * NESYMETRICKY okraj — 24 px vlevo, ale jen 4 px vpravo (548+248=796,
 * 800-796=4) — cisty nevyuzity pruh napravo. 3×248 + 2×14(gap) = 744,
 * 800-744=56 volnych px -> symetricky rozdeleno 28/28, tj. 14 px na kazdou
 * stranu mrizky. Sirka dlazdic beze zmeny (56 volnych px uz je "spravedlive"
 * rozdelenych, ne ze by zbyvalo navic na vetsi dlazdice). */
static const struct { prim_rect_t rect; const char *label; uint8_t act; } MENU_ITEMS[MENU_N] = {
    { {14,  68, 248, 76}, "Diagnostika",   ACT_DIAG },
    { {276, 68, 248, 76}, "Nastaveni",     ACT_SETTINGS },
    { {538, 68, 248, 76}, "System Health", ACT_HEALTH },
    { {14, 154, 248, 76}, "Citac",         ACT_COUNTER },
    { {276,154, 248, 76}, "Holdover",      ACT_HOLDOVER },
    { {538,154, 248, 76}, "Datalog",       ACT_DATALOG },
    { {14, 240, 248, 76}, "Alarmy",        ACT_ALARMS },
    { {276,240, 248, 76}, "Kalibrace",     ACT_KALIB },
    { {538,240, 248, 76}, "Cas",           ACT_CAS },
    { {14, 326, 248, 76}, "Animace",       ACT_ANIM },
    { {276,326, 248, 76}, "Math/Limity",   ACT_MATH },
    { {538,326, 248, 76}, "Status ribbon", ACT_RIBBON },
};
/* Restart ve footeru (stejna urovan jako BACK_RECT {650,417}, vlevo od nej). */
static const prim_rect_t MENU_RESTART_RECT = {460, 417, 170, 61};

static void menu_activate(uint8_t act)
{
    switch (act) {
    case ACT_DIAG:      app_gpsdo_render_diag();      break;
    case ACT_SETTINGS:  app_gpsdo_render_settings();  break;
    case ACT_HEALTH:    app_gpsdo_render_health();    break;
    case ACT_COUNTER:   app_gpsdo_render_counter();   break;
    case ACT_KALIB:     app_gpsdo_render_kalib();     break;
    case ACT_HOLDOVER:  app_gpsdo_render_holdover();  break;
    case ACT_DATALOG:   app_gpsdo_render_datalog();   break;
    case ACT_ALARMS:    app_gpsdo_render_alarms();    break;
    case ACT_CAS:       app_gpsdo_render_cas();       break;
    case ACT_ANIM:      app_gpsdo_render_anim();      break;
    case ACT_RIBBON:    app_gpsdo_render_ribbon();    break;
    case ACT_MATH:      app_gpsdo_render_math();      break;
    default: break;   /* Restart neni ACT_* — footer tlacitko -> confirm okno (s_view=13) */
    }
}

/* Obsah Menu BEZ s_view/present — sdili ho render_menu a modalni dialog
 * potvrzeni restartu (ten si menu prekresli jako podklad pod ztmavenim). */
static void menu_draw_body(void)
{
    window_chrome("MENU", WIN_TITLE_Y);
    for (int i = 0; i < MENU_N; i++) {
        ui_button_t b = {.rect = MENU_ITEMS[i].rect, .label = MENU_ITEMS[i].label,
                         .variant = UI_BUTTON_NORMAL};
        ui_button_render(&b);
    }
    /* Restart ve footeru vpravo (vedle ZPET) — systemova akce mimo mrizku. */
    ui_button_t rst = {.rect = MENU_RESTART_RECT, .variant = UI_BUTTON_ACTIVE, .label = "RESTART"};
    ui_button_render(&rst);
}

void app_gpsdo_render_menu(void)
{
    window_prep();
    s_view = 12;
    menu_draw_body();
    present_now();
}

/* ── Potvrzeni restartu (s_view=13): modalni box "Opravdu restartovat?" Ano/Ne. ── */
static const prim_rect_t CONFIRM_NO  = {230, 250, 150, 64};
static const prim_rect_t CONFIRM_YES = {420, 250, 150, 64};
static void app_gpsdo_render_confirm_restart(void)
{
    window_prep();
    s_view = 13;
    /* Modalni dialog NAD menu. Menu si prekreslime sami: pri triple bufferingu
     * neni zarucene, co prave ziskany back buffer obsahuje, a alfa michani by
     * pak ztmavilo neznamy podklad. Teprve pres nej jde polopruhledna cerna.
     * (Driv tu byl REPLACE fill plnou cernou => menu zmizelo uplne, prestoze
     * komentar sliboval "ztlumene pozadi (ponech menu)".)
     * Pozn.: alfa < 0xFF vyrazuje DMA2D fast-path ve prim_fill_rect -> ztmaveni
     * je CPU pres 384k px. Bezi JEDNOU pri otevreni dialogu, ne v ticku. */
    menu_draw_body();
    prim_fill_rect((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                   PRIM_RGBA(0, 0, 0, 170), PRIM_BLEND_OVER);
    prim_fill_rect_rounded((prim_rect_t){190, 150, 420, 200}, UI_DIM_CARD_RADIUS,
                           UI_COLOR_BG_CARD, PRIM_BLEND_OVER);
    prim_stroke_rect_rounded((prim_rect_t){190, 150, 420, 200}, UI_DIM_CARD_RADIUS, 1, UI_COLOR_WARN);
    prim_draw_text((prim_point_t){400, 210}, "Opravdu restartovat?",
                   &ui_font_mono_25, UI_COLOR_INK, PRIM_ALIGN_CENTER);
    /* ⚠️ Zvyrazneny (ACTIVE) je NE, tedy BEZPECNA volba — u destruktivniho
     * potvrzeni ma byt vizualnim defaultem. Driv bylo ACTIVE na ANO, coz je
     * presne naopak; navic tenhle dialog uz jednou stal za bugem "bootuje do
     * trendu" (prst drzeny na ANO pres reset, viz s_touch_primed v UiTask). */
    ui_button_t no  = {.rect = CONFIRM_NO,  .variant = UI_BUTTON_ACTIVE, .label = "NE"};
    ui_button_t yes = {.rect = CONFIRM_YES, .variant = UI_BUTTON_NORMAL, .label = "ANO"};
    ui_button_render(&no);
    ui_button_render(&yes);
    present_now();
}

/* ── Reference (s_view=14): stav Si5356 + konfigurace 4×100 MHz vernier hodin. ── */
static void app_gpsdo_render_reference(void)
{
    int first = window_first(14);
    static char c_lock[24];
    if (first) {
        s_view = 14;
        window_chrome("REFERENCE  Si5356", WIN_TITLE_Y);
        ui_card_t c = {.rect = DG_CARD_FULL_B, .header_label = "Vernier reference (4-fazovy TDC)"};
        ui_card_render_chrome(&c);
        prim_draw_text((prim_point_t){DG_LLBL, 112}, "Vstup:",  &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL+150), 112}, "10 MHz -> VCO 2,2 GHz (N=220) /22", &ui_font_mono_18, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 148}, "Vystup:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL+150), 148}, "4x 100 MHz, faze 0/90/180/270 (2,5 ns)", &ui_font_mono_18, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 184}, "Pouziti:",&ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL+150), 184}, "reciproky citac FPGA, jemny krok 2,5 ns", &ui_font_mono_18, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 240}, "Stav (reg 218):", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 300}, "Presnost = ppm vstupnich 10 MHz (Si5356).", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        c_lock[0] = '\0';
    }
    /* zivy lock status (LOS_CLKIN bit3 = ztrata reference = cervena; LOS_XTAL
     * bit2 ignorovan — bez krystalu trvale 1, viz SI5356_* definice) */
    const char *st; prim_color_t sc;
    if      (!g_si5356_ok)                        { st = "N/A (I2C)";   sc = UI_COLOR_INK_3; }
    else if (g_si5356_status & SI5356_LOS_CLKIN)  { st = "LOS CLKIN!";  sc = UI_COLOR_BAD; }
    else if (g_si5356_status & SI5356_PLL_LOL)    { st = "PLL UNLOCK!"; sc = UI_COLOR_BAD; }
    else if (g_si5356_status & SI5356_SYS_CAL)    { st = "CALIB...";    sc = UI_COLOR_VIOLET; }
    else                                          { st = "LOCK OK";     sc = UI_COLOR_OK; }
    if (first || dchg(c_lock, sizeof c_lock, st))
        { dtext((int16_t)(DG_LLBL + 200), 240, 300, st, sc, &ui_font_mono_18); present_now(); }
}

/* ── Kalibrace (s_view=15): editovatelne konstanty AD8307 + ADS delice. ──────
 * Tap na -/+ meni g_calib HNED (zive - promita se do RF dBm / 12V-5V napeti
 * uz na dalsim vzorku), tlacitko ULOZIT persistuje do W25Q CALIB store
 * (calib_save, blokujici erase+write). ADC3 VREF + TDC krok zustavaji
 * read-only (HW konstanty, nejde je kalibrovat timto mechanismem). */

/* TODO #11(1b) HOTOVO 2026-07-19: radky 104/144/184/224 (roztec 40) -> 110/176/
 * 242/308 (roztec 66), tlacitka 50x34 -> 60x60 (7,0 mm, na hranici doporuceneho
 * minima). Vetsi roztec uvolnila 2 readonly radky (VREF/TDC) + status na
 * spodek karty — viz app_gpsdo_render_kalib. */
static const struct { volatile float *val; float step, lo, hi; int decimals;
                      const char *label, *unit; int16_t y; } KALIB_ROWS[4] = {
    { &g_calib.ad8307_slope_mv_db,     0.5f,  10.0f,   40.0f, 1, "AD8307 slope",     "mV/dB", 110 },
    { &g_calib.ad8307_intercept_dbm,   0.5f, -100.0f, -60.0f, 1, "AD8307 intercept", "dBm",   176 },
    { &g_calib.gain_12v,               0.010f, 4.000f, 5.500f, 3, "12V delic gain",  "x",     242 },
    { &g_calib.gain_5v,                0.005f, 1.500f, 2.500f, 3, "5V delic gain",   "x",     308 },
};
#define KALIB_BTN_W 60
#define KALIB_BTN_H 60
#define KALIB_MINUS_X 588
#define KALIB_PLUS_X  656
static prim_rect_t kalib_minus_rect(int16_t y) { return (prim_rect_t){KALIB_MINUS_X, (int16_t)(y - 30), KALIB_BTN_W, KALIB_BTN_H}; }
static prim_rect_t kalib_plus_rect(int16_t y)  { return (prim_rect_t){KALIB_PLUS_X,  (int16_t)(y - 30), KALIB_BTN_W, KALIB_BTN_H}; }

/* ── Hit-slop tlacitek -/+ ──────────────────────────────────────────────────
 * I po zvetseni na 60x60 (7,0 mm) pridavame drobny hit-slop do volneho mista:
 *   - vodorovne: MINUS az k boxu hodnoty (ten konci na x=572), PLUS az k
 *     vnitrnimu okraji karty (DG_LX+764-14 = 768); mezeru mezi tlacitky
 *     delime na pul (hranice 652), aby si nekradla doteky,
 *   - svisle +-3 px = na pul mezery mezi radky (roztec radku 66, tlacitko 60). */
#define KALIB_HIT_SY   3
#define KALIB_HIT_MID  652                       /* delici cara mezi -/+ */
static prim_rect_t kalib_minus_hit(int16_t y)
{
    return (prim_rect_t){576, (int16_t)(y - 30 - KALIB_HIT_SY),
                         KALIB_HIT_MID - 576, KALIB_BTN_H + 2 * KALIB_HIT_SY};
}
static prim_rect_t kalib_plus_hit(int16_t y)
{
    return (prim_rect_t){KALIB_HIT_MID, (int16_t)(y - 30 - KALIB_HIT_SY),
                         768 - KALIB_HIT_MID, KALIB_BTN_H + 2 * KALIB_HIT_SY};
}
static const prim_rect_t KALIB_SAVE_RECT = {18, 417, 220, 61};
static const prim_rect_t KALIB_AUTOCAL_RECT = {260, 417, 210, 61};   /* AUTO-CAL self-check */
static uint32_t s_kalib_spin_frame = 0;   /* pro spinner ikonu pri ULOZIT (item 6) */

static void kalib_row_redraw(int i)
{
    char vb[16], full[24];
    fmt_fixed(vb, sizeof vb, *KALIB_ROWS[i].val, KALIB_ROWS[i].decimals);
    snprintf(full, sizeof full, "%s %s", vb, KALIB_ROWS[i].unit);
    /* boxw=312: box konci na x=572, 16 px pred KALIB_MINUS_X(588) — siriji by
     * kazdy redraw hodnoty prekryl/smazal levy okraj tlacitka MINUS. */
    dtext((int16_t)(DG_LLBL + 230), KALIB_ROWS[i].y, 312, full, UI_COLOR_ACC, &ui_font_mono_18);
}

/* Status/napoveda radek: prazdne/vychozi = staticka napoveda (bylo samostatnym
 * radkem "Zmena se projevi..." — sloucen sem, uvolnil misto pro vetsi tlacitka).
 * Po editaci WARN "Zmeneno...", po ULOZIT OK/BAD vysledek zapisu. */
static void kalib_status_redraw(const char *msg, prim_color_t col)
{
    if (!msg || !msg[0]) { msg = "Zmena se projevi ihned; ULOZIT zapise do W25Q (prezije reset)."; col = UI_COLOR_INK_4; }
    dtext(DG_LLBL, 402, 740, msg, col, &ui_font_sans_16);
}

/* Krok jedne polozky o step (smer +1/-1), clamp <lo,hi>, prekresli jen tu
 * hodnotu + status radek (bez ulozeni - to az tlacitko ULOZIT). */
static void kalib_step(int i, int dir)
{
    float v = *KALIB_ROWS[i].val + (float)dir * KALIB_ROWS[i].step;
    if (v < KALIB_ROWS[i].lo) v = KALIB_ROWS[i].lo;
    if (v > KALIB_ROWS[i].hi) v = KALIB_ROWS[i].hi;
    *KALIB_ROWS[i].val = v;
    prim_set_target(&s_fb);
    prim_reset_clip();
    kalib_row_redraw(i);
    kalib_status_redraw("Zmeneno (neulozeno) — ULOZIT pro trvaly zapis.", UI_COLOR_WARN);
    present_now();
}

static void app_gpsdo_render_kalib(void)
{
    window_prep();
    s_view = 15;
    window_chrome("KALIBRACE", WIN_TITLE_Y);
    ui_button_t save = {.rect = KALIB_SAVE_RECT, .variant = UI_BUTTON_ACTIVE, .label = "ULOZIT"};
    ui_button_render(&save);
    ui_button_t acb = {.rect = KALIB_AUTOCAL_RECT, .variant = UI_BUTTON_NORMAL, .label = "AUTO-CAL"};
    ui_button_render(&acb);
    /* Karta 348 px (bylo 320) — 4 radky s 60px tlacitky (roztec66, radek1..4
     * konci 308+30=338) + 2 readonly radky (346/374) + status (402) uz presahly
     * puvodnich 320; 348 je konci presne pred paticnkou (62+348=410, footer 417). */
    ui_card_t c = {.rect = {DG_LX, 62, 764, 348}, .header_label = "Kalibracni konstanty"};
    ui_card_render_chrome(&c);

    for (int i = 0; i < 4; i++) {
        int16_t yy = KALIB_ROWS[i].y;
        prim_draw_text((prim_point_t){DG_LLBL, yy}, KALIB_ROWS[i].label, &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        ui_button_t mb = {.rect = kalib_minus_rect(yy), .variant = UI_BUTTON_NORMAL, .label = "-"};
        ui_button_render(&mb);
        ui_button_t pb = {.rect = kalib_plus_rect(yy), .variant = UI_BUTTON_NORMAL, .label = "+"};
        ui_button_render(&pb);
        kalib_row_redraw(i);
    }
    /* Read-only HW konstanty (nejdou timto mechanismem kalibrovat). Puvodni
     * samostatny radek "Zmena se projevi..." odstranen — sloucen do vychozi
     * napovedy status radku (kalib_status_redraw), uvolnil misto pro vetsi
     * tlacitka v radcich vyse. */
    prim_draw_text((prim_point_t){DG_LLBL, 346}, "ADC3 VREF", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 230), 346}, "VREFINT_CAL x 3300 / data (16-bit)", &ui_font_mono_18, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){DG_LLBL, 374}, "TDC jemny krok", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 230), 374}, "2,5 ns (Si5356 90 faze)", &ui_font_mono_18, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);

    kalib_status_redraw(NULL, UI_COLOR_INK_4);   /* NULL -> vychozi napoveda */
    present_now();
}

/* Maly radek "label: hodnota" v kartach novych oken. */
static void kv_row(int16_t y, const char *k, const char *v, prim_color_t vc)
{
    prim_draw_text((prim_point_t){DG_LLBL, y}, k, &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 250), y}, v, &ui_font_mono_18, vc, PRIM_ALIGN_LEFT);
}

/* Totez, ale pro ZIVE prekreslovane radky: nejdriv vycisti box hodnoty. Bez toho
 * by kratsi nova hodnota nechala ocas te predchozi ("1234 / 20" pres "1234 / 2043136")
 * a diky dirty-rect copy-forwardu by tam ten ocas i zustal. Label se nemeni ->
 * prekresluje se jen hodnota. Sirka 380 = po pravy vnitrni okraj karty
 * DG_CARD_FULL_B (DG_LX+764-14 minus DG_LLBL+250), vyska kryje ascent+descent
 * mono_18 (glyf zacina ~y-18, descender ~y+4). */
static void kv_row_live(int16_t y, const char *k, const char *v, prim_color_t vc, int first)
{
    prim_fill_rect((prim_rect_t){(int16_t)(DG_LLBL + 250), (int16_t)(y - 22), 380, 30},
                   UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    if (first)
        prim_draw_text((prim_point_t){DG_LLBL, y}, k, &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 250), y}, v, &ui_font_mono_18, vc, PRIM_ALIGN_LEFT);
}

/* Zive prekresleni JEN hodnoty s UZKYM clear boxem (210 px) — nezasahne do prvku
 * vpravo (OCXO budik v Holdoveru je od x=506; sirsi kv_row_live/380 by ho mazal).
 * Label kresli volajici jednou pres kv_row (na first) — tady se NEprekresluje,
 * aby se AA hrany labelu nescitaly. */
static void kv_row_narrow(int16_t y, const char *v, prim_color_t vc)
{
    prim_fill_rect((prim_rect_t){(int16_t)(DG_LLBL + 250), (int16_t)(y - 22), 210, 30},
                   UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 250), y}, v, &ui_font_mono_18, vc, PRIM_ALIGN_LEFT);
}

/* ── Holdover (s_view=16): stav disciplinace GPSDO (WARMUP/LOCK/HOLDOVER) z GPS
 * fixu + FPGA linku + timepulse. Zive (uptime tik). Zdroj OCXO teploty = 0x49. ── */
/* ── OCXO analogovy budik ladiciho napeti (Vc = AIN0, efekt FX_OCXO_GAUGE) ────
 * Pulkruhovy budik na PRAVE strane karty Holdover. Rozsah 0..OCXO_VC_FS_MV,
 * barevne zony (zelena stred = OK, amber, cervena u railu = OCXO dojizdi z
 * rozsahu EFC). Rychlejsi cteni "jsem u railu?" nez z holeho cisla. Uhly:
 * 180°=levy=min, 0°=pravy=max (ring_sector konvence: 0°=vychod, CCW). ⚠️ Arc i
 * rucicka jdou pres prim_internal_blend_px (mimo mark_dirty) -> spolehaji na
 * predchozi clear (fill REPLACE) kvuli copy-forwardu pres 3 buffery. */
#define OCXO_VC_FS_MV  5000        /* plny rozsah budiku [mV] (~0..5 V EFC) */
#define OCXO_G_CX      606
#define OCXO_G_CY      262
#define OCXO_G_R       94

/* Barevny obloukovy pas jako thick polyline — LEVNE. prim_draw_arc (ring_sector)
 * dela atan2f PER PIXEL pres cely bbox oblouku (~35k px/oblouk x 5 = desitky ms
 * na prekresleni budiku!); tady jen ~pocet segmentu thick line. r = stredni
 * polomer (tloustka th centrovana). Uhly: 0°=vpravo, CCW (jako ring_sector). */
static void gauge_arc(prim_point_t ctr, int16_t r, int16_t th, int a0, int a1, prim_color_t col)
{
    int steps = (a1 - a0) / 5; if (steps < 1) steps = 1;   /* ~5° na segment */
    prim_point_t prev = {0, 0}; int have = 0;
    for (int s = 0; s <= steps; s++) {
        float ang = (a0 + (float)(a1 - a0) * (float)s / (float)steps) * 0.01745329f;
        prim_point_t p = {(int16_t)(ctr.x + cosf(ang) * (float)r),
                          (int16_t)(ctr.y - sinf(ang) * (float)r)};
        if (have) prim_draw_line(prev, p, th, col);
        prev = p; have = 1;
    }
}

static void ocxo_gauge_draw(void)
{
    const sensor_stat_t *vc = &g_sensors[SENS_ADS0];
    float mv = vc->last; if (mv < 0.0f) mv = 0.0f;
    float v = mv / (float)OCXO_VC_FS_MV; if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
    prim_point_t ctr = {OCXO_G_CX, OCXO_G_CY};

    /* Clear cele plochy budiku (arc + label + hodnota) — nese je copy-forward. */
    prim_fill_rect((prim_rect_t){OCXO_G_CX - OCXO_G_R - 6, OCXO_G_CY - OCXO_G_R - 26,
                                 2 * OCXO_G_R + 12, OCXO_G_R + 62},
                   UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_draw_text((prim_point_t){OCXO_G_CX, OCXO_G_CY - OCXO_G_R - 8}, "OCXO Vc",
                   &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_CENTER);
    /* Zonove oblouky (uhel klesa s hodnotou: min vlevo 180°, max vpravo 0°). */
    int16_t th = 12;
    int16_t ar = (int16_t)(OCXO_G_R - th / 2);                 /* stredni polomer pasu (r_out 94, r_in 82) */
    gauge_arc(ctr, ar, th, 162, 180, UI_COLOR_BAD);            /* dolni rail (cervena) */
    gauge_arc(ctr, ar, th, 135, 162, UI_COLOR_WARN);           /* amber */
    gauge_arc(ctr, ar, th,  45, 135, UI_COLOR_OK);             /* stred (zelena) */
    gauge_arc(ctr, ar, th,  18,  45, UI_COLOR_WARN);           /* amber */
    gauge_arc(ctr, ar, th,   0,  18, UI_COLOR_BAD);            /* horni rail (cervena) */
    /* Rucicka. */
    float theta = (1.0f - v) * 180.0f * 0.01745329f;          /* rad */
    prim_draw_line(ctr, (prim_point_t){(int16_t)(OCXO_G_CX + cosf(theta) * (OCXO_G_R - 14)),
                                       (int16_t)(OCXO_G_CY - sinf(theta) * (OCXO_G_R - 14))},
                   3, UI_COLOR_ACC);
    prim_fill_circle(ctr, 6, UI_COLOR_INK_2);                 /* hub */
    /* Hodnota. */
    char vb[24];
    int mvv = (int)(mv + 0.5f);
    if (vc->valid) snprintf(vb, sizeof vb, "%d,%03d V", mvv / 1000, mvv % 1000);
    else           snprintf(vb, sizeof vb, "-- (stale)");
    prim_draw_text((prim_point_t){OCXO_G_CX, OCXO_G_CY + 30}, vb, &ui_font_mono_25,
                   vc->valid ? UI_COLOR_ACC : UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
}

/* ── Holdover drift-prediction kuzel (efekt FX_HOLD_CONE) ─────────────────────
 * Predpoved rozsahu frekvencni odchylky OCXO dopredu v case. Sirka obalky (a barva)
 * dle STAVU: LOCK = uzka zelena (GNSS disciplinuje), HOLDOVER = amber, roste s dobou
 * v holdoveru (nejistota), WARMUP = fialova, NO LOCK = siroka cervena. Ilustrativni
 * model — headline je simulace; realna data by dosadila zmereny drift + OCXO tempco.
 * ⚠️ Vypln/hrany jdou pres prim_internal_blend_px -> spolehaji na uvodni clear. */
#define CONE_X  30
#define CONE_Y  286
#define CONE_W  445
#define CONE_H  52
static void holdover_cone_draw(int state, uint32_t t_in_state_s)
{
    prim_fill_rect((prim_rect_t){CONE_X, CONE_Y, CONE_W, CONE_H}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    int16_t yc = (int16_t)(CONE_Y + CONE_H / 2);
    int16_t x0 = (int16_t)(CONE_X + 66);           /* "ted" (za popiskem) */
    int16_t x1 = (int16_t)(CONE_X + CONE_W - 4);   /* horizont */
    prim_draw_text((prim_point_t){CONE_X + 2, (int16_t)(yc - 7)}, "drift", &ui_font_sans_14, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){CONE_X + 2, (int16_t)(yc + 11)}, "OCXO",  &ui_font_sans_14, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    prim_draw_line((prim_point_t){x0, yc}, (prim_point_t){x1, yc}, 1, UI_COLOR_LINE);   /* osa */

    float wf; prim_color_t cc;                     /* pulsirka obalky (0..1) + barva dle stavu */
    switch (state) {
    case 1:  wf = 0.12f; cc = UI_COLOR_OK; break;                                        /* LOCK */
    case 2:  wf = 0.30f + (float)t_in_state_s / 600.0f * 0.20f; if (wf > 0.92f) wf = 0.92f;
             cc = UI_COLOR_WARN; break;                                                  /* HOLDOVER (roste) */
    case 0:  wf = 0.45f; cc = UI_COLOR_VIOLET; break;                                    /* WARMUP */
    default: wf = 0.92f; cc = UI_COLOR_BAD; break;                                       /* NO LOCK */
    }
    int16_t half = (int16_t)(wf * (float)(CONE_H / 2 - 2));
    int16_t cols = (int16_t)(x1 - x0); if (cols < 1) cols = 1;
    for (int16_t c = 0; c <= cols; c++) {          /* vypln kuzele (lin. rozevreni) */
        int16_t hx = (int16_t)((int32_t)half * c / cols);
        prim_fill_rect((prim_rect_t){(int16_t)(x0 + c), (int16_t)(yc - hx), 1, (int16_t)(2 * hx + 1)},
                       PRIM_ALPHA(cc, 0x22), PRIM_BLEND_OVER);
    }
    prim_draw_line((prim_point_t){x0, yc}, (prim_point_t){x1, (int16_t)(yc - half)}, 2, cc);   /* hrany */
    prim_draw_line((prim_point_t){x0, yc}, (prim_point_t){x1, (int16_t)(yc + half)}, 2, cc);
    prim_draw_text((prim_point_t){x1, (int16_t)(CONE_Y + 13)}, "+1 h", &ui_font_sans_14, UI_COLOR_INK_4, PRIM_ALIGN_RIGHT);
}

/* ── Warm-up / stabilizace OCXO ──────────────────────────────────────────────
 * OCXO potrebuje ~jednotky minut tepelne ustaleni, nez je disciplinace duveryhodna.
 * "Hotovo" = uptime >= WARMUP_MIN_S A tepelny sklon |dT/dt| < WARMUP_SLOPE (z RAM
 * historie 0x49 pres sensor_hist). Nahrazuje drivejsi naivni uptime<180 ve stavu
 * WARMUP. Sklon [°C/min] vraci ve *slope (smi byt NULL). */
#define WARMUP_MIN_S     300u     /* min. doba nabehu [s] */
#define WARMUP_SLOPE     0.08f    /* prah ustaleni [°C/min] */
static int warmup_ready(float *slope_c_min)
{
    float buf[16], mn, mx; int32_t span = 0;
    int n = sensor_hist_series(SENS_T49, 120, buf, 16, &mn, &mx, NULL, &span);
    float slope = (n >= 2 && span > 0) ? (buf[n - 1] - buf[0]) / (float)span * 60.0f : 0.0f;
    if (slope_c_min) *slope_c_min = slope;
    float a = slope < 0 ? -slope : slope;
    return (g_uptime_s >= WARMUP_MIN_S) && (a < WARMUP_SLOPE);
}

static void app_gpsdo_render_holdover(void)
{
    int first = window_first(16);
    static char c_st[16], c_gps[24], c_tp[16], c_ocxo[16], c_since[20];
    static uint32_t s_state_since;
    static int s_last_state = -1;
    static int32_t s_last_vc = -99999;   /* posl. vykreslene Vc [mV] pro OCXO budik */
    if (first) {
        s_view = 16;
        window_chrome("HOLDOVER", WIN_TITLE_Y);
        ui_card_t c = {.rect = DG_CARD_FULL_B, .header_label = "Stav disciplinace GPSDO"};
        ui_card_render_chrome(&c);
        prim_draw_text((prim_point_t){DG_LLBL, 116}, "Rezim:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 344},
                       "WARMUP=nabeh OCXO  LOCK=disc. z GNSS  HOLDOVER=drzi VC bez fixu",
                       &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        c_st[0] = c_gps[0] = c_tp[0] = c_ocxo[0] = c_since[0] = '\0';
        s_last_state = -1;
    }
    gps_data_t g; gps_get(&g);
    /* stav: WARMUP (OCXO tepelne neustaleny) -> LOCK (fix+link) -> HOLDOVER (ztrata po locku) */
    float wslope; int warm = warmup_ready(&wslope);
    int st; const char *sl; prim_color_t sc;
    int lock = (g.valid && g_spi_ok);
    if (lock)                              { st = 1; sl = "LOCK";     sc = UI_COLOR_OK; }
    else if (g.fixes > 0)                  { st = 2; sl = "HOLDOVER"; sc = UI_COLOR_WARN; }
    else if (!warm)                        { st = 0; sl = "WARMUP";   sc = UI_COLOR_VIOLET; }
    else                                   { st = 3; sl = "NO LOCK";  sc = UI_COLOR_BAD; }
    if (st != s_last_state) { s_last_state = st; s_state_since = g_uptime_s; }

    if (first || dchg(c_st, sizeof c_st, sl)) {
        prim_fill_rect((prim_rect_t){(int16_t)(DG_LLBL + 120), 92, 260, 34}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 120), 118}, sl, &ui_font_mono_25, sc, PRIM_ALIGN_LEFT);
    }
    char b[24];
    snprintf(b, sizeof b, "%s", g.valid ? (g.fix_mode == 3 ? "3D fix" : "2D fix") : (g.fixes > 0 ? "ztracen" : "zadny"));
    { prim_color_t vc = g.valid ? UI_COLOR_OK : UI_COLOR_INK_2; int chg = dchg(c_gps, sizeof c_gps, b);
      if (first) kv_row(160, "GPS lock:", b, vc); else if (chg) kv_row_narrow(160, b, vc); }
    snprintf(b, sizeof b, "%s", g.fix_quality ? "100 kHz (disc.)" : "10 Hz (hold)");
    { prim_color_t vc = g.fix_quality ? UI_COLOR_OK : UI_COLOR_WARN; int chg = dchg(c_tp, sizeof c_tp, b);
      if (first) kv_row(196, "Timepulse:", b, vc); else if (chg) kv_row_narrow(196, b, vc); }
    /* fmt_temp (stejne jako Diagnostika/Senzory) — driv se tu formatovalo inline
     * a rozchazelo se to se zbytkem UI ve DVOU vecech:
     *   1) desetinna CARKA misto tecky (jedine takove misto v celem UI),
     *   2) u ZAPORNYCH hodnot to davalo nesmysl: (long)(-5.3) = -5, zbytek
     *      (-5.3 - -5)*10 = -3 => "-5,-3 C"; pro -0.5 dokonce "0,-5 C" (ztrata
     *      znamenka). OCXO sice zaporne nebude, ale byla to tikajici mina. */
    /* OCXO teplota + tepelny sklon (warm-up indikator): "45.2 C +0.03/m". */
    { const sensor_stat_t *o = &g_sensors[SENS_T49];
      if (o->valid) { char t[12]; fmt_temp(t, sizeof t, o->last);
                      float a = wslope < 0 ? -wslope : wslope; int ai = (int)a;
                      int af = (int)((a - (float)ai) * 100.0f + 0.5f);
                      snprintf(b, sizeof b, "%s %c%d.%02d/m", t, wslope < 0 ? '-' : '+', ai, af); }
      else          snprintf(b, sizeof b, "--");
      prim_color_t oc = warm ? UI_COLOR_INK_2 : UI_COLOR_VIOLET;   /* fialova dokud nabiha */
      int chg = dchg(c_ocxo, sizeof c_ocxo, b);
      if (first) kv_row(232, "OCXO tepl.:", b, oc); else if (chg) kv_row_narrow(232, b, oc); }
    snprintf(b, sizeof b, "%lu s", (unsigned long)(g_uptime_s - s_state_since));
    int since_chg = dchg(c_since, sizeof c_since, b);
    if (first) kv_row(268, "V rezimu:", b, UI_COLOR_INK_2); else if (since_chg) kv_row_narrow(268, b, UI_COLOR_INK_2);
    if ((g_fx_enabled & FX_HOLD_CONE) && (first || since_chg))   /* drift-prediction kuzel (efekt), ~1 Hz */
        holdover_cone_draw(st, g_uptime_s - s_state_since);
    if (g_fx_enabled & FX_OCXO_GAUGE) {          /* analogovy budik OCXO Vc (efekt) */
        int32_t mv = (int32_t)(g_sensors[SENS_ADS0].last + 0.5f);
        int32_t d = mv - s_last_vc; if (d < 0) d = -d;
        if (first || d >= 15) { s_last_vc = mv; ocxo_gauge_draw(); }   /* prah 15 mV -> mene per-pixel arc redraw */
    }
    present_now();
}

/* ── Self-survey (s_view=32): firmwarove prumerovani polohy (Welford) ─────────
 * Prumeruje lat/lon/alt z platnych fixu; horizontalni rozptyl [m] = konvergence
 * (klesa s N). START posle i UBX-CFG-TMODE2 (survey-in, best-effort — timing RX).
 * Akumulace bezi na pozadi (app_gpsdo_tick) i mimo okno. */
#define SURVEY_MIN_DUR_S   3600u    /* UBX svinMinDur */
#define SURVEY_ACC_MM      2500u    /* UBX svinAccLimit (2,5 m) */
static struct {
    uint8_t  active;
    uint32_t n;
    uint32_t t_start_s;
    uint32_t last_fixes;           /* posl. videny g.fixes -> pocitej jen NOVE fixy */
    double   mlat, mlon, malt;      /* running mean (deg / m) */
    double   m2lat, m2lon;          /* Welford M2 (horizontalni rozptyl) */
    float    spread_m;              /* horizontalni std [m] */
} s_survey;

static void survey_accumulate(void)
{
    if (!s_survey.active) return;
    gps_data_t g; gps_get(&g);
    if (!g.valid || g.fixes == s_survey.last_fixes) return;   /* jen NOVY fix (ne 2x tyz) */
    s_survey.last_fixes = g.fixes;
    double lat = g.lat_deg, lon = g.lon_deg, alt = g.alt_m;
    s_survey.n++;
    double dlat = lat - s_survey.mlat; s_survey.mlat += dlat / (double)s_survey.n;
    s_survey.m2lat += dlat * (lat - s_survey.mlat);
    double dlon = lon - s_survey.mlon; s_survey.mlon += dlon / (double)s_survey.n;
    s_survey.m2lon += dlon * (lon - s_survey.mlon);
    double dalt = alt - s_survey.malt; s_survey.malt += dalt / (double)s_survey.n;
    if (s_survey.n >= 2) {                       /* std [deg]->[m]: lat 111320, lon *cos(lat) */
        double slat = sqrt(s_survey.m2lat / (double)s_survey.n) * 111320.0;
        double clat = cos(s_survey.mlat * 0.0174532925);
        double slon = sqrt(s_survey.m2lon / (double)s_survey.n) * 111320.0 * clat;
        s_survey.spread_m = (float)sqrt(slat * slat + slon * slon);
    }
}
static void survey_start(void)
{
    memset(&s_survey, 0, sizeof s_survey);
    s_survey.active = 1;
    s_survey.t_start_s = g_uptime_s;
    gps_survey_in_cmd(SURVEY_MIN_DUR_S, SURVEY_ACC_MM);   /* best-effort (timing RX) */
}
static void survey_stop(void)
{
    s_survey.active = 0;
    gps_survey_disable_cmd();
    if (s_survey.n >= 2) {   /* persist vysledek do syscfg flash (prezije power-cycle) */
        /* ⚠️ S2: g_survey_lat/lon jsou double (8 B = 2 STR). defaultTask (Normal, syscfg
         * flash save) muze preemptnout tenhle UiTask (BelowNormal) uprostred zapisu ->
         * trhany double -> nesmyslna souradnice do flashe. Kriticka sekce udela CELY
         * publish atomicky. Ctenar (syscfg) chranit netreba: UiTask (nizsi prio) ho
         * nemuze preemptnout, takze jeho cteni je vuci nam atomicke. valid nakonec. */
        taskENTER_CRITICAL();
        g_survey_n      = s_survey.n;
        g_survey_lat    = s_survey.mlat;
        g_survey_lon    = s_survey.mlon;
        g_survey_alt    = (float)s_survey.malt;
        g_survey_spread = s_survey.spread_m;
        g_survey_valid  = 1;
        taskEXIT_CRITICAL();
    }
}

static const prim_rect_t SURVEY_BTN = {18, 417, 220, 61};   /* START/STOP */

static void app_gpsdo_render_survey(void)
{
    int first = window_first(32);
    static char c_st[16], c_n[28], c_sp[20], c_pos[44];
    if (first) {
        s_view = 32;
        window_chrome("SELF-SURVEY", WIN_TITLE_Y);
        ui_card_t c = {.rect = DG_CARD_FULL_B, .header_label = "Prumerovani polohy (konvergence 1PPS)"};
        ui_card_render_chrome(&c);
        prim_draw_text((prim_point_t){DG_LLBL, 344},
                       "Prumeruje platne fixy; rozptyl klesa s N. UBX-CFG-TMODE2 = best-effort (timing RX).",
                       &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        c_st[0] = c_n[0] = c_sp[0] = c_pos[0] = '\0';
        /* Neni-li zivy survey a mame ulozeny vysledek (syscfg), zobraz ho (HOTOVO). */
        if (!s_survey.active && s_survey.n == 0 && g_survey_valid) {
            s_survey.n = g_survey_n; s_survey.mlat = g_survey_lat; s_survey.mlon = g_survey_lon;
            s_survey.malt = g_survey_alt; s_survey.spread_m = g_survey_spread;
        }
    }
    ui_button_t tg = {.rect = SURVEY_BTN,
                      .variant = s_survey.active ? UI_BUTTON_STOP : UI_BUTTON_RUN,
                      .label = s_survey.active ? "STOP" : "START"};
    if (first) ui_button_render(&tg);
    char b[44];
    snprintf(b, sizeof b, "%s", s_survey.active ? "BEZI" : (s_survey.n ? "HOTOVO" : "necinny"));
    if (first || dchg(c_st, sizeof c_st, b))
        kv_row_live(116, "Stav:", b, s_survey.active ? UI_COLOR_OK : (s_survey.n ? UI_COLOR_ACC : UI_COLOR_INK_3), first);
    snprintf(b, sizeof b, "%lu  (%lu s)", (unsigned long)s_survey.n,
             (unsigned long)(s_survey.active ? (g_uptime_s - s_survey.t_start_s) : 0u));
    if (first || dchg(c_n, sizeof c_n, b)) kv_row_live(152, "Vzorku:", b, UI_COLOR_INK_2, first);
    if (s_survey.n >= 2) { int mm = (int)(s_survey.spread_m * 1000.0f + 0.5f);
                           snprintf(b, sizeof b, "%d.%03d m", mm / 1000, mm % 1000); }
    else                 snprintf(b, sizeof b, "--");
    if (first || dchg(c_sp, sizeof c_sp, b))
        kv_row_live(188, "Rozptyl H:", b, (s_survey.n >= 2 && s_survey.spread_m < 2.0f) ? UI_COLOR_OK : UI_COLOR_WARN, first);
    if (s_survey.n) { char la[16], lo[16];
                      fmt_ll((float)s_survey.mlat, 'N', 'S', la, sizeof la);
                      fmt_ll((float)s_survey.mlon, 'E', 'W', lo, sizeof lo);
                      snprintf(b, sizeof b, "%s %s %dm", la, lo, (int)(s_survey.malt + 0.5)); }
    else            snprintf(b, sizeof b, "--");
    if (first || dchg(c_pos, sizeof c_pos, b)) kv_row_live(224, "Poloha:", b, UI_COLOR_INK_2, first);
    present_now();
}

/* ── Okno SESTAVY (s_view=33): uloz/nacti profil nastaveni (setup.c, W25Q) ─────
 * Vyber slotu 1..N (-/+), footer ULOZIT/NACIST/SMAZAT. Staticke (prekresli se na
 * tap). NACIST aplikuje i tema/jas (jako prepinac schematu). */
static int s_setup_slot = 0;   /* 0-based, UI 1-based */
static int s_setup_msg  = 0;   /* 0=nic 1=ulozeno 2=nacteno 3=smazano 4=chyba zapisu 5=prazdny slot */

/* Prekresli dynamicky obsah (cislo slotu + stav + prehled obsazenych + hlaska akce). */
static void setups_render_dynamic(void)
{
    uint8_t mask = setup_used_mask();
    int used = (mask >> s_setup_slot) & 1;
    /* Cislo slotu (mezi -/+). */
    prim_fill_rect((prim_rect_t){112, 120, 96, 56}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    char sn[8]; snprintf(sn, sizeof sn, "%d", s_setup_slot + 1);
    prim_draw_text((prim_point_t){160, 162}, sn, &ui_font_mono_52, UI_COLOR_INK, PRIM_ALIGN_CENTER);
    /* Stav slotu. */
    prim_fill_rect((prim_rect_t){300, 120, 470, 34}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_draw_text((prim_point_t){300, 150}, used ? "obsazen" : "prazdny", &ui_font_mono_25,
                   used ? UI_COLOR_OK : UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
    /* Prehled vsech slotu (1..N; obsazene cislem, prazdne teckou). */
    char ov[40]; int p = 0;
    for (int i = 0; i < SETUP_N && p < (int)sizeof ov - 3; i++) {
        ov[p++] = ((mask >> i) & 1) ? (char)('1' + i) : '.';
        ov[p++] = ' ';
    }
    ov[p] = '\0';
    prim_fill_rect((prim_rect_t){300, 210, 470, 30}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_draw_text((prim_point_t){300, 234}, ov, &ui_font_mono_25, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
    /* Hlaska posledni akce (uloz/nacti/smaz) — vc. chyby zapisu do flash. */
    prim_fill_rect((prim_rect_t){40, 278, 730, 30}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    if (s_setup_msg) {
        const char *m; prim_color_t col;
        switch (s_setup_msg) {
        case 1:  m = "Ulozeno.";                col = UI_COLOR_OK;    break;
        case 2:  m = "Nacteno (aplikovano).";   col = UI_COLOR_OK;    break;
        case 3:  m = "Smazano.";                col = UI_COLOR_INK_2; break;
        case 5:  m = "Slot je prazdny.";        col = UI_COLOR_WARN;  break;
        default: m = "Chyba zapisu do flash!";  col = UI_COLOR_BAD;   break;
        }
        prim_draw_text((prim_point_t){40, 300}, m, &ui_font_sans_18, col, PRIM_ALIGN_LEFT);
    }
}

static void app_gpsdo_render_setups(void)
{
    int first = window_first(33);
    if (first) {
        s_view = 33;
        window_chrome("SESTAVY", WIN_TITLE_Y);
        ui_card_t c = {.rect = DG_CARD_FULL_B, .header_label = "Ulozene profily nastaveni (slot 1-8)"};
        ui_card_render_chrome(&c);
        ui_button_t sm = {.rect = SET_SLOT_MINUS, .variant = UI_BUTTON_NORMAL, .label = "-"};
        ui_button_t sp = {.rect = SET_SLOT_PLUS,  .variant = UI_BUTTON_NORMAL, .label = "+"};
        ui_button_render(&sm); ui_button_render(&sp);
        prim_draw_text((prim_point_t){40, 236}, "Slot:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        ui_button_t bsv = {.rect = SETUP_SAVE_RECT,  .variant = UI_BUTTON_NORMAL, .label = "ULOZIT"};
        ui_button_t bld = {.rect = SETUP_LOAD_RECT,  .variant = UI_BUTTON_NORMAL, .label = "NACIST"};
        ui_button_t ber = {.rect = SETUP_ERASE_RECT, .variant = UI_BUTTON_NORMAL, .label = "SMAZAT"};
        ui_button_render(&bsv); ui_button_render(&bld); ui_button_render(&ber);
        prim_draw_text((prim_point_t){40, 344},
                       "Profil = jas/tema/jazyk/zvuk/zona/efekty/Math+limity. Ulozeno ve W25Q.",
                       &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    }
    setups_render_dynamic();
    present_now();
}

/* ── Datalog (s_view=17): stav logovani do W25Q DATA regionu. Zatim neaktivni
 * (roadmap [[w25q-flash]]) — okno je vstupni bod, ukazuje kapacitu + JEDEC. ── */
/* Footer okna Datalog: ZAPNOUT/VYPNOUT logovani (vlevo, vedle ZPET vpravo). */
static const prim_rect_t DL_TOGGLE_RECT = {18, 417, 220, 61};

static void app_gpsdo_render_datalog(void)
{
    int first = window_first(17);
    static char c_stav[24], c_rec[40], c_seq[16], c_err[16];
    if (first) {
        s_view = 17;
        window_chrome("DATALOG", WIN_TITLE_Y);
        ui_card_t c = {.rect = DG_CARD_FULL_B,
                       .header_label = "Zaznam stability (32 B / 10 s, kruhovy log)"};
        ui_card_render_chrome(&c);
        c_stav[0] = c_rec[0] = c_seq[0] = c_err[0] = '\0';
    }

    datalog_status_t st;
    datalog_get_status(&st);
    char b[40];

    /* Tlacitko nabizi AKCI (stejny princip jako footer RUN/STOP na hlavni
     * obrazovce): kdyz log bezi, nabizi VYPNOUT (cervene). */
    ui_button_t tg = {.rect = DL_TOGGLE_RECT,
                      .variant = st.enabled ? UI_BUTTON_STOP : UI_BUTTON_RUN,
                      .label = st.enabled ? "VYPNOUT" : "ZAPNOUT"};
    if (first) ui_button_render(&tg);

    snprintf(b, sizeof b, "%s (%s)", st.ready ? (st.enabled ? "BEZI" : "ZASTAVEN") : "NEDOSTUPNE",
             st.backend);
    if (first || dchg(c_stav, sizeof c_stav, b))
        kv_row_live(116, "Stav:", b, !st.ready ? UI_COLOR_BAD : (st.enabled ? UI_COLOR_OK : UI_COLOR_WARN), first);

    /* Zaznamy + kolik dni to pri 10 s/zaznam vydrzi nez se zacne prepisovat. */
    snprintf(b, sizeof b, "%lu / %lu%s", (unsigned long)st.records,
             (unsigned long)st.capacity_rec, st.wrapped ? " (prepis)" : "");
    if (first || dchg(c_rec, sizeof c_rec, b)) kv_row_live(152, "Zaznamu:", b, UI_COLOR_INK_2, first);

    if (first) {
        unsigned long dni = (unsigned long)((uint64_t)st.capacity_rec * DATALOG_PERIOD_S / 86400u);
        snprintf(b, sizeof b, "%lu dni (%lu MB)", dni,
                 (unsigned long)(W25Q_DATA_SIZE / (1024u * 1024u)));
        kv_row(188, "Kapacita:", b, UI_COLOR_INK_2);
    }

    snprintf(b, sizeof b, "%lu", (unsigned long)st.last_seq);
    if (first || dchg(c_seq, sizeof c_seq, b)) kv_row_live(224, "Posledni seq:", b, UI_COLOR_INK_2, first);

    snprintf(b, sizeof b, "%lu", (unsigned long)st.write_errors);
    if (first || dchg(c_err, sizeof c_err, b))
        kv_row_live(260, "Chyb zapisu:", b, st.write_errors ? UI_COLOR_WARN : UI_COLOR_INK_2, first);

    if (first)
        prim_draw_text((prim_point_t){DG_LLBL, 344},
                       "f + teploty + Vc + GPS lock; SD karta pripravena (datalog_sd.c).",
                       &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    present_now();
}

/* ── Alarmy (s_view=18): monitor alarmovych udalosti (co je hlidano + pocitadla).
 * Doplnuje Nastaveni (jen globalni mute) o PREHLED co spousti alarm + historii. ── */
static void app_gpsdo_render_alarms(void)
{
    int first = window_first(18);
    static char c_mute[12], c_f[12], c_g[12];
    if (first) {
        s_view = 18;
        window_chrome("ALARMY", WIN_TITLE_Y);
        ui_card_t c = {.rect = DG_CARD_FULL_B, .header_label = "Zvukove alarmy (beeper) — co je hlidano"};
        ui_card_render_chrome(&c);
        kv_row(116, "FPGA SIGNAL_LOST:", "hlidano (3x pip)", UI_COLOR_INK_2);
        kv_row(152, "Ztrata GPS locku:", "hlidano (2x pip)", UI_COLOR_INK_2);
        kv_row(188, "Frekv. limit:",     "hlidano (4x pip)", UI_COLOR_INK_2);   /* #44 hotovo: PASS->FAIL = 4x pip */
        prim_draw_text((prim_point_t){DG_LLBL, 260}, "Udalosti od startu:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 348},
                       "Vypnuti zvuku globalne v Nastaveni; mute plati i pro alarmy.",
                       &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        c_mute[0] = c_f[0] = c_g[0] = '\0';
    }
    char b[12];
    snprintf(b, sizeof b, "%s", g_sound_muted ? "MUTE" : "zapnut");
    if (first || dchg(c_mute, sizeof c_mute, b)) kv_row_live(224, "Zvuk:", b, g_sound_muted ? UI_COLOR_BAD : UI_COLOR_OK, first);
    snprintf(b, sizeof b, "%u", g_alarm_fpga_lost);
    if (first || dchg(c_f, sizeof c_f, b)) kv_row_live(292, "  FPGA ztrat:", b, g_alarm_fpga_lost ? UI_COLOR_WARN : UI_COLOR_INK_2, first);
    snprintf(b, sizeof b, "%u", g_alarm_gps_lost);
    if (first || dchg(c_g, sizeof c_g, b)) kv_row_live(320, "  GPS ztrat:", b, g_alarm_gps_lost ? UI_COLOR_WARN : UI_COLOR_INK_2, first);
    present_now();
}

/* Pasmo RF bargrafu (dBm) — sdileno oknem Animace/demo (anim_target_pct nize)
 * i realnym RF tickem (app_gpsdo_tick_signal, dal v souboru). Definovano tady,
 * protoze anim_target_pct je pouziva jako prvni. */
#define RF_DBM_MIN            (-80)      /* spodek bargrafu */
#define RF_DBM_MAX            (10)       /* vrch bargrafu (AD8307 zvlada az ~+17) */

/* ── Animace / demo (s_view=24): ukazka `anim` helperu (ease-out dojezd k cili)
 * na RF bargrafu. CIL skace (realny RF, nebo — bez signalu — demo sekvence
 * urovni), AKTUALNI ho plynule dojizdi. Plynula animace bezi z app_gpsdo_tick_anim
 * (~20 Hz z UiTask), NE z 2 Hz app_gpsdo_tick — proto je pohyb hladky. Prekresluje
 * se JEN zmeneny kus (ui_bargraph_update = jen zmenene segmenty + dtext s clearem),
 * takze animace je dirty-rect friendly a CPU dopad je zanedbatelny. ── */
static const prim_rect_t ANIM_BAR_RECT    = {40, 168, 704, 34};   /* label/value radek + stopa */
static const prim_rect_t ANIM_TOGGLE_RECT = {DG_LLBL, 78, 260, 50};  /* globalni ZAP/VYP animaci
                                                                       * (offset 78-62=16 od vrsku karty,
                                                                       * stejny jako MUTE_RECT v Nastaveni). */
static const prim_rect_t ANIM_DEMO_RECT = {18, 417, 300, 61};  /* footer: -> subokno prikladu
                                                                * vsech animaci ve smycce (s_view=25) */
static const prim_rect_t ANIM_FX_RECT   = {330, 417, 300, 61}; /* footer: -> prepinace efektu (s_view=27) */
static anim_t   s_anim_bar;              /* eased pct 0..100 */
static uint32_t s_anim_frame;            /* citac tiku (demo sekvence) */
static int16_t  s_anim_last_pct = -1;    /* posl. vykreslene lit pct (incremental) */
static char     s_anim_c_tgt[12], s_anim_c_cur[12];   /* dchg cache cil/aktualni */

/* Cilova hodnota bargrafu v %: realny RF pokud je vzorek, jinak demo sekvence
 * (skoky drzene ~2 s -> viditelny ease-out nabeh na kazdou uroven). */
static int16_t anim_target_pct(int *is_demo)
{
    const sensor_stat_t *rf = &g_sensors[SENS_ADS1];
    if (rf->samples != 0) {
        *is_demo = 0;
        float mv = rf->last; if (mv < 0.0f) mv = 0.0f;
        float dbm = mv / g_calib.ad8307_slope_mv_db + g_calib.ad8307_intercept_dbm;
        int16_t p = (int16_t)((dbm - (float)RF_DBM_MIN) * 100.0f / (float)(RF_DBM_MAX - RF_DBM_MIN));
        if (p < 0) p = 0; else if (p > 100) p = 100;
        return p;
    }
    *is_demo = 1;
    static const int16_t LV[] = {5, 90, 35, 100, 20, 70};
    uint32_t idx = (s_anim_frame / 40u) % (uint32_t)(sizeof(LV) / sizeof(LV[0]));  /* 40 tiku ~2 s */
    return LV[idx];
}

/* Globalni prepinac animaci (g_anim_enabled) — VYP zpusobi, ze VSECHNY anim_t
 * (na kterekoli obrazovce) skoci okamzite na cil (viz anim_step). Tlacitko zde
 * v Animaci, ne v Nastaveni (na prani — animace jsou "hraci", ne systemove). */
static void anim_toggle_redraw(void)
{
    ui_button_t tb = {.rect = ANIM_TOGGLE_RECT, .variant = UI_BUTTON_NORMAL,
                      .label = g_anim_enabled ? "ANIMACE: ZAPNUTO" : "ANIMACE: VYPNUTO"};
    ui_button_render(&tb);
}

static void app_gpsdo_render_anim(void)
{
    int first = window_first(24);
    if (first) {
        s_view = 24;
        window_chrome("ANIMACE / DEMO", WIN_TITLE_Y);
        ui_card_t c = {.rect = DG_CARD_FULL_B, .header_label = "anim helper — ease-out dojezd k cili"};
        ui_card_render_chrome(&c);
        anim_toggle_redraw();
        /* Footer: subokno prikladu VSECH animaci + prepinace grafickych efektu. */
        ui_button_t demo_btn = {.rect = ANIM_DEMO_RECT, .variant = UI_BUTTON_NORMAL,
                                .label = "PRIKLADY ANIMACI >"};
        ui_button_render(&demo_btn);
        ui_button_t fx_btn = {.rect = ANIM_FX_RECT, .variant = UI_BUTTON_NORMAL,
                              .label = "EFEKTY >"};
        ui_button_render(&fx_btn);

        s_anim_frame = 0;                        /* demo sekvence zacne od zacatku */
        int demo = 0;
        int16_t tgt = anim_target_pct(&demo);
        anim_reset(&s_anim_bar, 0.0f);           /* nabehne z 0 na cil (uvodni animace) */
        s_anim_last_pct = -1;                    /* vynutit plny prvni render segmentu */
        s_anim_c_tgt[0] = s_anim_c_cur[0] = '\0';  /* pri re-entry vynutit prekresleni cislic */

        /* Bargraf (plny render vcetne stopy) — dal se aktualizuje jen incrementalne. */
        ui_bargraph_t bg = {.rect = ANIM_BAR_RECT, .value_pct = 0,
                            .color = UI_COLOR_ACC, .label = "RF uroven (ease-out)",
                            .value_text = "0 %"};
        ui_bargraph_render(&bg);

        dlabel(DG_LLBL, 250, "Cil (skace):");
        dlabel(DG_LLBL, 286, "Aktualni (eased):");
        prim_draw_text((prim_point_t){DG_LLBL, 344},
                       demo ? "Bez RF signalu -> demo sekvence urovni. Cil skoci, bar plynule dojede."
                            : "Cil = realny RF (AD8307). Bar plynule sleduje merenou uroven.",
                       &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        (void)tgt;
    }
    /* Prvni vykresleni hodnot zaridi tick (nize) — tady jen chrome. */
    present_now();
}

/* ── Subokno "PRIKLADY ANIMACI" (s_view=25) ──────────────────────────────────
 * Sest dlazdic, kazda demonstruje jeden typ animace pouzity v UI, vse bezi
 * NEPRETRZITE ve smycce z app_gpsdo_tick_anim (~20 Hz). Ucel: overit, ze kazda
 * animace renderuje spravne, bez nutnosti trefit se do ~150ms flashe na realnem
 * tlacitku / rozeznat ji v jitrujicim mereni. ⚠️ Zamerne NEZAVISI na
 * g_anim_enabled — je to ukazka, ma se hybat i kdyz jsou animace v UI vypnute
 * (proto vlastni raw ease `ad_ease`, ne `anim_step`). Kazdy tik prekresli vnitrek
 * kazde dlazdice (clear BG_CARD -> obsah); obsah lezi cely uvnitr toho clearu
 * (REPLACE fill = mark_dirty) -> copy-forward pres 3 buffery v poradku. ── */
#define AD_TILE_W 375
#define AD_TILE_H 104
static const prim_rect_t AD_TILE[6] = {
    { 18,  68, AD_TILE_W, AD_TILE_H}, {407,  68, AD_TILE_W, AD_TILE_H},
    { 18, 182, AD_TILE_W, AD_TILE_H}, {407, 182, AD_TILE_W, AD_TILE_H},
    { 18, 296, AD_TILE_W, AD_TILE_H}, {407, 296, AD_TILE_W, AD_TILE_H},
};
static const char *const AD_HDR[6] = {
    "1. Ease-out bar",       "2. Pulsujici LED",
    "3. Flash tlacitka",     "4. Eased cislo",
    "5. Zvyrazneni cislice", "6. Prolinani (fade)",
};

/* Vnitrni obsahovy obdelnik dlazdice (pod hlavickou karty). */
static prim_rect_t ad_content(int i)
{
    prim_rect_t t = AD_TILE[i];
    return (prim_rect_t){(int16_t)(t.x + 12), (int16_t)(t.y + 40),
                         (int16_t)(t.w - 24), (int16_t)(t.h - 52)};
}

static uint32_t s_ad_frame;    /* citac tiku smycky */
static anim_t   s_ad_bar;      /* dlazdice 1: eased 0..100 */
static anim_t   s_ad_num;      /* dlazdice 4: eased cislo */
static int      s_ad_digit;    /* dlazdice 5: posledni cislice 0..9 */

/* Raw ease-out (ignoruje g_anim_enabled — demo se ma hybat vzdy). */
static float ad_ease(anim_t *a, float k)
{
    float d = a->target - a->cur;
    if (d < 0.5f && d > -0.5f) a->cur = a->target;
    else                       a->cur += d * k;
    return a->cur;
}

static void app_gpsdo_render_animdemo(void)
{
    window_prep();
    s_view = 25;
    window_chrome("PRIKLADY ANIMACI", WIN_TITLE_Y);
    for (int i = 0; i < 6; i++) {
        ui_card_t c = {.rect = AD_TILE[i], .header_label = AD_HDR[i]};
        ui_card_render_chrome(&c);
    }
    /* Reset stavu -> smycka od zacatku (obsah dokresli tick_animdemo ~20 Hz). */
    s_ad_frame = 0;
    anim_reset(&s_ad_bar, 0.0f);
    anim_reset(&s_ad_num, 0.0f);
    s_ad_digit = 0;
    present_now();
}

/* ── Citac (s_view=19): syrovy detail mereni FPGA — obe odbocky /4 a /16,
 * pocet hran, gate, SEQ, fazovy status (present/fine jako 4+4 indikatory)
 * a dekodovane chybove priznaky. Zive (~2x/s). Hlavni pouziti = bring-up
 * SPI linky a mereni (bohatsi nez jednoradkovy stav v Diagnostice). ── */

/* Hodnota okna Citac: dtext s pevnym x a sirkou po pravy okraj karty. */
#define CNT_VX  (DG_LLBL + 150)                        /* 180: value x */
#define CNT_VW  (DG_LX + 764 - 14 - CNT_VX)            /* po pravy vnitrni okraj */

/* 4 ctverecky pro nibble fazoveho statusu (zleva bit3..bit0 = faze 3..0).
 * seen=0 -> jen tlumeny obrys (zadny DATA ramec zatim nedorazil). */
static void cnt_nibble(int16_t x, int16_t baseline, uint8_t nib, int seen)
{
    for (int i = 3; i >= 0; i--) {
        prim_rect_t r = {x, (int16_t)(baseline - 16), 20, 20};
        prim_fill_rect(r, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        if (!seen)
            prim_stroke_rect_rounded(r, 4, 1, UI_COLOR_INK_4);
        else
            prim_fill_rect_rounded(r, 4, (nib & (1u << i)) ? UI_COLOR_OK : UI_COLOR_BAD,
                                   PRIM_BLEND_OVER);
        x = (int16_t)(x + 26);
    }
}

/* ── Okno MERENI (s_view=34) — prezentace mereni (#67) ────────────────────────
 * Perioda / odchylka v jednotkach (Hz/ppm/ppb/ppt) / auto-nominal / offset /
 * statistika N vzorku (mean/σ/p-p) / TFOM. Jadro = meas_present.c/h (pure-logic,
 * kryto selftestem). Sesterske k Citaci (footer "< CITAC" / "MERENI >", bez nav_push -> BACK
 * z obou vede do Menu). Vzorky statistiky pridava app_gpsdo_tick_stats_sample
 * (1/s, jen RUN). ⚠️ Nad screen_main_freq_hz() = DNES SIMULACE -> plny smysl po #2. */
static uint8_t    s_meas_mode = 0;             /* 0 = FREKV, 1 = PERIODA */
static mp_unit_t  s_meas_unit = MP_UNIT_PPB;
static mp_stats_t s_meas_stats;                /* akumulace v tick_stats_sample */
static const prim_rect_t CNT_MEAS_BTN  = {18,  417, 200, 61};   /* Citac -> MERENI */
static const prim_rect_t MEAS_MODE_BTN = {18,  417, 140, 61};
static const prim_rect_t MEAS_UNIT_BTN = {166, 417, 140, 61};
static const prim_rect_t MEAS_RST_BTN  = {314, 417, 130, 61};
static const prim_rect_t MEAS_CNT_BTN  = {452, 417, 190, 61};   /* MERENI -> Citac */

static void app_gpsdo_render_meas(void)
{
    int first = window_first(34);
    static char c_pri[48], c_nom[48], c_dev[48], c_off[48], c_tf[24],
                c_n[24], c_mean[48], c_sd[32], c_pp[32];
    if (first) {
        s_view = 34;
        window_chrome("MERENI  prezentace", WIN_TITLE_Y);
        ui_card_t c = {.rect = DG_CARD_FULL_C, .header_label = "Perioda / odchylka / statistika / TFOM"};
        ui_card_render_chrome(&c);
        ui_button_t bm = {.rect = MEAS_MODE_BTN, .variant = UI_BUTTON_NORMAL,
                          .label = s_meas_mode ? "PERIODA" : "FREKV."};
        ui_button_render(&bm);
        ui_button_t bu = {.rect = MEAS_UNIT_BTN, .variant = UI_BUTTON_NORMAL,
                          .label = mp_unit_label(s_meas_unit)};
        ui_button_render(&bu);
        ui_button_t br = {.rect = MEAS_RST_BTN, .variant = UI_BUTTON_NORMAL, .label = "RESET"};
        ui_button_render(&br);
        ui_button_t bc = {.rect = MEAS_CNT_BTN, .variant = UI_BUTTON_NORMAL, .label = "< CITAC"};
        ui_button_render(&bc);
        c_pri[0]=c_nom[0]=c_dev[0]=c_off[0]=c_tf[0]=c_n[0]=c_mean[0]=c_sd[0]=c_pp[0]='\0';
    }
    double hz  = screen_main_freq_hz();
    double nom = mp_nominal_auto(hz);
    int drew = first;
    char b[48], db[32];

    /* Primarni readout: FREKV (fmt_hz, double) nebo PERIODA v ns. */
    if (s_meas_mode) { double ns = mp_period_s(hz) * 1e9; fmt_fixed(db, sizeof db, (float)ns, 4);
                       snprintf(b, sizeof b, "%s ns", db); }
    else             fmt_hz(hz, b, sizeof b);
    if (first || dchg(c_pri, sizeof c_pri, b)) { kv_row_live(104, "Primarni:", b, UI_COLOR_INK, first); drew = 1; }

    /* Auto-nominal (nejblizsi kulata reference). */
    fmt_hz(nom, b, sizeof b);
    if (first || dchg(c_nom, sizeof c_nom, b)) { kv_row_live(138, "Nominal:", b, UI_COLOR_INK_2, first); drew = 1; }

    /* Odchylka ve zvolene jednotce. */
    fmt_fixed(db, sizeof db, (float)mp_deviation(hz, nom, s_meas_unit), 4);
    snprintf(b, sizeof b, "%s %s", db, mp_unit_label(s_meas_unit));
    if (first || dchg(c_dev, sizeof c_dev, b)) { kv_row_live(172, "Odchylka:", b, UI_COLOR_ACC, first); drew = 1; }

    /* Offset od nominalu [Hz]. */
    fmt_hz(hz - nom, b, sizeof b);
    if (first || dchg(c_off, sizeof c_off, b)) { kv_row_live(206, "Offset:", b, UI_COLOR_INK_2, first); drew = 1; }

    /* TFOM (odhad z kvality GPS + holdover/warmup). */
    { gps_data_t g; gps_get(&g);
      int holdover = (!g.valid && g.fixes > 0);
      int warmup   = !warmup_ready(NULL);   /* NULL = nezajima nas sklon, jen bool */
      mp_tfom_t tf = mp_tfom(g.valid, g.fix_mode, g.num_sat, g.hdop, holdover, warmup);
      snprintf(b, sizeof b, "%u  %s", (unsigned)tf.level, tf.label);
      if (first || dchg(c_tf, sizeof c_tf, b)) {
          prim_color_t tc = (tf.level <= 2) ? UI_COLOR_OK : (tf.level <= 6) ? UI_COLOR_WARN : UI_COLOR_BAD;
          kv_row_live(240, "TFOM:", b, tc, first); drew = 1;
      }
    }

    /* Statistika N vzorku (Welford; akumuluje tick_stats_sample 1/s jen RUN, RESET nuluje). */
    snprintf(b, sizeof b, "%lu", (unsigned long)s_meas_stats.n);
    if (first || dchg(c_n, sizeof c_n, b)) { kv_row_live(274, "Vzorku (N):", b, UI_COLOR_INK_2, first); drew = 1; }

    fmt_hz(s_meas_stats.mean, b, sizeof b);
    if (first || dchg(c_mean, sizeof c_mean, b)) { kv_row_live(308, "Prumer:", b, UI_COLOR_INK, first); drew = 1; }

    fmt_fixed(db, sizeof db, (float)mp_stats_sd(&s_meas_stats), 5);
    snprintf(b, sizeof b, "%s Hz", db);
    if (first || dchg(c_sd, sizeof c_sd, b)) { kv_row_live(342, "σ (n-1):", b, UI_COLOR_VIOLET, first); drew = 1; }

    fmt_fixed(db, sizeof db, (float)mp_stats_p2p(&s_meas_stats), 5);
    snprintf(b, sizeof b, "%s Hz", db);
    if (first || dchg(c_pp, sizeof c_pp, b)) { kv_row_live(384, "Peak-peak:", b, UI_COLOR_INK_2, first); drew = 1; }

    if (drew) present_now();
}

static void app_gpsdo_render_counter(void)
{
    int first = window_first(19);
    static char c_link[64], c_f4[48], c_f16[48], c_edge[24], c_gate[24], c_seq[16], c_err[24];
    static int  c_ph = -1;   /* posledni kresleny phase_status (-1 = jeste nic) */
    if (first) {
        s_view = 19;
        window_chrome("CITAC  detail mereni", WIN_TITLE_Y);
        ui_card_t c = {.rect = DG_CARD_FULL_C, .header_label = "FPGA reciproke mereni (SPI2)"};
        ui_card_render_chrome(&c);
        dlabel(DG_LLBL, 104, "SPI link");
        dlabel(DG_LLBL, 138, "f (/4)");     /* pin28, primar */
        dlabel(DG_LLBL, 172, "f (/16)");    /* pin27, rozsah */
        dlabel(DG_LLBL, 206, "Hrany");      /* edge_count = pocet period v okne */
        dlabel(DG_LLBL, 240, "Gate");
        dlabel(DG_LLBL, 274, "SEQ");
        dlabel(DG_LLBL, 308, "Chyby");
        dlabel(DG_LLBL, 342, "Faze");
        prim_draw_text((prim_point_t){(int16_t)(CNT_VX + 130), 342}, "fine",
                       &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 384},
                       "pin28 = /4 (primar)   pin27 = /16 (rozsah)   zdrave faze = 4+4 zelene",
                       &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        ui_button_t bmeas = {.rect = CNT_MEAS_BTN, .variant = UI_BUTTON_NORMAL, .label = "MERENI >"};
        ui_button_render(&bmeas);   /* -> sesterske okno MERENI (#67, bez nav_push) */
        c_link[0] = c_f4[0] = c_f16[0] = c_edge[0] = c_gate[0] = c_seq[0] = c_err[0] = '\0';
        c_ph = -1;
    }

    fpga_meas_t m;
    int seen = fpga_freq_get_last(&m) ? 1 : 0;
    int drew = first;
    char b[48];

    /* SPI status (stejny text jako Diagnostika, barva dle linky) — bez mezikopie,
     * status muze mit az 63 znaku (b je jen 48 B). Box CNT_VW=588 je (na rozdil
     * od uzsiho DG_COLW-24 v Diagnostice) dost velky i pro realny NOLINK radek
     * pri mono_18 (overeno tabulkou fontu — proto TADY bump, v Diagnostice ne). */
    if (first || dchg(c_link, sizeof c_link, (const char *)g_spi_text))
        { dtext(CNT_VX, 104, CNT_VW, (const char *)g_spi_text,
                g_spi_ok ? UI_COLOR_OK : UI_COLOR_BAD, &ui_font_mono_18); drew = 1; }

    /* Kmitocty obou odbocek (delicka uz zahrnuta ve FPGA). Chyba zdroje -> cervene. */
    if (seen) fpga_freq_format_val(m.frequency_x100000, b, sizeof b); else snprintf(b, sizeof b, "--");
    if (first || dchg(c_f4, sizeof c_f4, b)) {
        prim_color_t fc = !seen ? UI_COLOR_INK_3
                        : (m.error_flags & FPGA_ERR_MEAS) ? UI_COLOR_BAD
                        : (g_freq_stale ? UI_COLOR_INK_3 : UI_COLOR_INK);
        dtext(CNT_VX, 138, CNT_VW, b, fc, &ui_font_mono_18); drew = 1;
    }
    if (seen) fpga_freq_format_val(m.freq16_x100000, b, sizeof b); else snprintf(b, sizeof b, "--");
    if (first || dchg(c_f16, sizeof c_f16, b)) {
        prim_color_t fc = !seen ? UI_COLOR_INK_3
                        : (m.status2 & FPGA_ST2_DIV16_ERR) ? UI_COLOR_BAD
                        : (g_freq_stale ? UI_COLOR_INK_3 : UI_COLOR_INK);
        dtext(CNT_VX, 172, CNT_VW, b, fc, &ui_font_mono_18); drew = 1;
    }

    /* Hrany (pocet period v okne; < 2^32 pri gate ~21 s a pinu <= 100 MHz) */
    if (seen) snprintf(b, sizeof b, "%lu period", (unsigned long)m.edge_count);
    else      snprintf(b, sizeof b, "--");
    if (first || dchg(c_edge, sizeof c_edge, b))
        { dtext(CNT_VX, 206, CNT_VW, b, UI_COLOR_INK_2, &ui_font_mono_18); drew = 1; }

    /* Gate v ms (~250, kolisa — reciproke okno ceka na hrany) */
    if (seen) snprintf(b, sizeof b, "%lu.%03lu ms", (unsigned long)(m.gate_time_ns / 1000000ULL),
                       (unsigned long)((m.gate_time_ns % 1000000ULL) / 1000ULL));
    else      snprintf(b, sizeof b, "--");
    if (first || dchg(c_gate, sizeof c_gate, b))
        { dtext(CNT_VX, 240, CNT_VW, b, UI_COLOR_INK_2, &ui_font_mono_18); drew = 1; }

    if (seen) snprintf(b, sizeof b, "%lu", (unsigned long)m.sequence);
    else      snprintf(b, sizeof b, "--");
    if (first || dchg(c_seq, sizeof c_seq, b))
        { dtext(CNT_VX, 274, CNT_VW, b, UI_COLOR_INK_2, &ui_font_mono_18); drew = 1; }

    /* Dekodovane chybove priznaky (error_flags + status2) */
    if (!seen) snprintf(b, sizeof b, "--");
    else {
        b[0] = '\0';
        if (m.error_flags & FPGA_ERR_SIGNAL_LOST) strncat(b, "LOST ", sizeof b - strlen(b) - 1);
        if (m.error_flags & FPGA_ERR_OVERFLOW)    strncat(b, "OVF ",  sizeof b - strlen(b) - 1);
        if (m.error_flags & FPGA_ERR_MEAS)        strncat(b, "E/4 ",  sizeof b - strlen(b) - 1);
        if (m.status2     & FPGA_ST2_DIV16_ERR)   strncat(b, "E/16",  sizeof b - strlen(b) - 1);
        if (b[0] == '\0') snprintf(b, sizeof b, "zadne");
    }
    if (first || dchg(c_err, sizeof c_err, b)) {
        int ok = seen && strcmp(b, "zadne") == 0;
        dtext(CNT_VX, 308, CNT_VW, b, !seen ? UI_COLOR_INK_3 : (ok ? UI_COLOR_OK : UI_COLOR_BAD),
              &ui_font_mono_18); drew = 1;
    }

    /* Fazovy status: present[3:0] + fine_seen[3:0] (zdrave = vse zelene = 0xFF) */
    int ph_key = seen ? (int)m.phase_status : 0x100;   /* 0x100 = "bez dat" */
    if (first || ph_key != c_ph) {
        c_ph = ph_key;
        cnt_nibble((int16_t)CNT_VX,        342, (uint8_t)(seen ? (m.phase_status & 0x0F) : 0), seen);
        cnt_nibble((int16_t)(CNT_VX + 180), 342, (uint8_t)(seen ? (m.phase_status >> 4) : 0), seen);
        drew = 1;
    }

    if (drew) present_now();
}

/* ── Selftest (s_view=20): per-test vysledky pure-logic unit testu + tlacitko
 * SPUSTIT (run_selftests bezi v UiTasku — zadny HW, zadny sdileny stav, ~ms).
 * Plny redraw pri kazdem volani (staticke okno, neni v ticku). ── */
static const prim_rect_t ST_RUN_RECT = {18, 417, 180, 61};
static void app_gpsdo_render_selftest(void)
{
    window_prep();
    s_view = 20;
    window_chrome("SELFTEST", WIN_TITLE_Y);
    ui_button_t run = {.rect = ST_RUN_RECT, .variant = UI_BUTTON_ACTIVE, .label = "SPUSTIT"};
    ui_button_render(&run);
    ui_card_t c = {.rect = DG_CARD_FULL_B, .header_label = "Pure-logic unit testy (bezi i pri bootu)"};
    ui_card_render_chrome(&c);
    /* Poradi MUSI sedet s run_selftests / g_selftest_detail (freertos_shared.h). */
    #define ST_N 12                 /* = SELFTEST_N (freertos_shared.h) */
    static const char *NAMES[ST_N] = {
        "CRC16 (SPI protokol)",     /* crc16("123456789") == 0x29B1 */
        "Hystereze /4 <-> /16",     /* fpga_freq_select_core na syntetickych ramcich */
        "GPS parser (NMEA)",
        "Format + histogram",       /* fmt_frac + hist_h vektory (screen_main) */
        "Maidenhead lokator",
        "Kalendar + DST (zona)",    /* rtc_apply_tz prehoupnuti + EU CET/CEST hranice */
        "Datalog zaznam + CRC",     /* pack/unpack 32B zaznamu + kalendar->unix */
        "Math + limity",            /* meas_math: Mx+B, NULL, pass/fail (#43/#44) */
        "Sestava (sanitizace)",     /* setup: clamp jas/dim/zona/M (#54) */
        "Auto-cal (verdikt)",       /* autocal: PASS/WARN/FAIL pasma (#68) */
        "Prezentace mereni",        /* meas_present: perioda/nominal/jednotky/stat/TFOM (#67) */
        "SCPI parser",              /* scpi_selftest: case/kratka-dlouha forma/hierarchie (#25) */
    };
    int pass = 0;
    for (int i = 0; i < ST_N; i++) {
        /* Roztec 20 (12 testu, 2026-08-07 pridan SCPI): od 100 -> posledni na
         * 100+11*20=320; "Celkem" na 340 -> karta B konci na 362 (18 px rezerva). */
        int16_t yy = (int16_t)(100 + i * 20);
        dlabel(DG_LLBL, yy, NAMES[i]);
        uint8_t r = g_selftest_detail[i];
        const char *rs = (r == 1) ? "PASS" : (r == 2) ? "FAIL" : "---";
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 340), yy}, rs, &ui_font_mono_18,
                       (r == 1) ? UI_COLOR_OK : (r == 2) ? UI_COLOR_BAD : UI_COLOR_INK_4,
                       PRIM_ALIGN_LEFT);
        if (r == 1) pass++;
    }
    char b[24];
    if (g_selftest_res == 0) snprintf(b, sizeof b, "nespusten");
    else                     snprintf(b, sizeof b, "%d/%d %s", pass, ST_N, pass == ST_N ? "PASS" : "FAIL");
    dlabel(DG_LLBL, 340, "Celkem");
    prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 340), 340}, b, &ui_font_mono_18,
                   g_selftest_res == 0 ? UI_COLOR_INK_4 : (pass == ST_N ? UI_COLOR_OK : UI_COLOR_BAD),
                   PRIM_ALIGN_LEFT);
    #undef ST_N
    prim_draw_text((prim_point_t){DG_LLBL, 350},
                   "Destruktivni HW testy zvlast: UART qspitest / storetest.",
                   &ui_font_sans_16, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    present_now();
}

/* ── Cas / zona (s_view=22, vlastni dlazdice v Menu) ────────────────────────
 * RTC bezi VZDY v UTC (GPS sync); tady se voli ZOBRAZOVACI zona: AUTO CET/CEST
 * (EU pravidlo letniho casu, rtc_cest_active) nebo rucni posun -12..+14 h.
 * Zive UTC + lokalni cas (~2x/s v app_gpsdo_tick) = okamzity nahled volby. */

/* Efektivni zona jako text ("CEST"/"CET" v AUTO — pocitano zive z UTC data,
 * ne z g_tz_label ktery muze byt az 1 s pozadu; "UTC+2" v rucnim rezimu). */
static void cas_zone_value(char *out, size_t n)
{
    if (g_tz_auto) {
        char rt[24];
        strncpy(rt, (const char *)g_rtc_text, sizeof rt - 1); rt[sizeof rt - 1] = '\0';
        if (rt[0] >= '0' && rt[0] <= '9') {
            uint16_t y = (uint16_t)((rt[0]-'0')*1000 + (rt[1]-'0')*100 + (rt[2]-'0')*10 + (rt[3]-'0'));
            uint8_t mo = (uint8_t)((rt[5]-'0')*10 + (rt[6]-'0'));
            uint8_t dd = (uint8_t)((rt[8]-'0')*10 + (rt[9]-'0'));
            uint8_t hh = (uint8_t)((rt[11]-'0')*10 + (rt[12]-'0'));
            snprintf(out, n, "%s", rtc_cest_active(y, mo, dd, hh) ? "CEST" : "CET");
        } else {
            snprintf(out, n, "CET/CEST");   /* RTC jeste nebezi (pred prvnim tickem) */
        }
    } else {
        int tz = (int)g_tz_offset_h;
        if (tz == 0) snprintf(out, n, "UTC");
        else         snprintf(out, n, "UTC%+d", tz);
    }
}

/* Partial update rezimu: AUTO/RUCNI tlacitko + velka hodnota zony mezi -/+. */
/* Hodnota zony centrovana na stred TZ_MINUS/PLUS (y=310,h=64 -> stred342;
 * drive h=56 -> stred338). Posunuto +4 px se zvetsenim tlacitek. */
static void cas_upd_mode(void)
{
    /* ⚠️ Klíčové: REPLACE clear PŘED ui_button_render (bylo bez něj — bug,
     * viz níže). Tohle je JEDINÝ přepínač v appce, ktery meni VARIANTU (a tedy
     * barvu) tlačítka při partial redrawu bez předchozího blit_bg_region —
     * footer tlačítka (RUN/STOP…) mají blit_bg_region v screen_main_redraw_button,
     * ostatní partial-redraw tlačítka v appce (MUTE/AUTODIM/LANG) drží stále
     * stejnou variantu (NORMAL), takže i bez clearu vypadají OK.
     * Root cause: prim_fill_rect_rounded kreslí rohy přes aa_corner ->
     * prim_internal_blend_px, ktery pise PRIMO do framebufferu a OBCHAZI
     * mark_dirty (ten se vola jen z DMA2D d2d_fill/d2d_blit_ex). Rohy tlacitka
     * se tedy NIKDY nezaznamenaji jako "dirty" -> pri triple bufferingu
     * (copy-forward jen oznacenych oblasti) se do noveho back bufferu
     * nezkopiruji a zustane tam "duch" ze 2 snimku zpet — po zmene varianty
     * (jina barva) je to videt jako čtverečky v rozich tlačítka. REPLACE
     * clear pres CELY obdelnik pred kreslenim jde DMA2D rychlou cestou (viz
     * fill.c: blend==REPLACE), ktera mark_dirty VOLA — takze nasledny AA
     * blend rohu uz spada do JIZ oznacene (a tedy spravne kopirovane) oblasti,
     * presne jako to dela dtext_a pro text (viz komentar v prim_stm32_hal.c). */
    prim_fill_rect(TZ_AUTO_RECT, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    ui_button_t ab = {.rect = TZ_AUTO_RECT,
                      .variant = g_tz_auto ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                      .label = g_tz_auto ? "AUTO CET/CEST" : "RUCNI POSUN"};
    ui_button_render(&ab);
    char b[12];
    cas_zone_value(b, sizeof b);
    prim_fill_rect((prim_rect_t){106, 318, 140, 48}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_draw_text((prim_point_t){176, 350}, b, &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
}

static void app_gpsdo_render_cas(void)
{
    int first = window_first(22);
    static char c_utc[26], c_loc[34], c_sync[8];
    if (first) {
        s_view = 22;
        window_chrome("CAS  zobrazovaci zona", WIN_TITLE_Y);
        ui_card_t c = {.rect = DG_CARD_FULL_C,
                       .header_label = "Casova zona (RTC bezi v UTC z GPS)"};
        ui_card_render_chrome(&c);
        dlabel(DG_LLBL, 122, "UTC");
        dlabel(DG_LLBL, 156, "Lokalni");
        dlabel(DG_LLBL, 190, "GPS sync");
        ui_button_t mb = {.rect = TZ_MINUS, .variant = UI_BUTTON_NORMAL, .label = "-"};
        ui_button_t pb = {.rect = TZ_PLUS, .variant = UI_BUTTON_NORMAL, .label = "+"};
        ui_button_render(&mb);
        ui_button_render(&pb);
        cas_upd_mode();
        prim_draw_text((prim_point_t){DG_LLBL, 392},
                       "AUTO = EU letni cas (CET/CEST). -/+ prepne na rucni posun.",
                       &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        c_utc[0] = c_loc[0] = c_sync[0] = '\0';
    }
    int drew = first;
    char b[34];
    snprintf(b, sizeof b, "%.19s", (const char *)g_rtc_text);
    if (first || dchg(c_utc, sizeof c_utc, b))
        { dtext(200, 122, 400, b, UI_COLOR_INK_2, &ui_font_mono_18); drew = 1; }
    snprintf(b, sizeof b, "%.19s %s", (const char *)g_rtc_text_local, (const char *)g_tz_label);
    if (first || dchg(c_loc, sizeof c_loc, b))
        { dtext(200, 156, 480, b, UI_COLOR_INK, &ui_font_mono_18); drew = 1; }
    snprintf(b, sizeof b, "%s", g_rtc_synced ? "ANO" : "NE");
    if (first || dchg(c_sync, sizeof c_sync, b))
        { dtext(200, 190, 120, b, g_rtc_synced ? UI_COLOR_OK : UI_COLOR_WARN, &ui_font_mono_18); drew = 1; }
    if (drew) present_now();
}

/* ── Komunikace: blokove schema (s_view=21) ────────────────────────────────
 * Otevira se tlacitkem DIAGRAM v Diagnostice (ne z Menu). Uzly = staticke
 * ramecky (GPS/Senzory/STM32/Si5356/FPGA), spoje = barevne pravouhle (elbow)
 * trasy podle ziveho stavu + jeden popisek na kazde trase. Vsechny texty jsou
 * OREZANE do vlastniho boxu (prim_set_clip) -> nikdy nepretecou mimo okno.
 * Cely diagram se prekresli najednou pri zmene stavoveho klice (jednodussi
 * a bezpecnejsi nez mazat jen jednotlive cary).
 *
 * ⚠️ Sirka uzlu = text (mono_16, 10 px/znak monospace) + ~18 px padding na
 * kazdou stranu — puvodni uzly mely az 90-130 px prazdne rezervy navic (napr.
 * "SENZORY" 70 px textu v 160 px bloku). STM32/FPGA jsou o neco sirsi (150/160)
 * kvuli 2 vstupnim bodum (STM) a nejdelsimu textu (FPGA GW1NR-9, 120 px). ── */
static const prim_rect_t CD_GPS   = {30,  104, 140, 46};  /* x:30-170   stred 100, y 104-150 */
static const prim_rect_t CD_SENS  = {640, 104, 110, 46};  /* x:640-750  stred 695 */
static const prim_rect_t CD_STM   = {325, 196, 150, 54};  /* x:325-475  stred 400, y 196-250 */
static const prim_rect_t CD_GROUP = {158, 292, 524, 84};  /* carkovana skupina "FPGA deska" */
static const prim_rect_t CD_SI    = {170, 316, 120, 52};  /* x:170-290  stred 230, y 316-368 */
static const prim_rect_t CD_FPGA  = {510, 316, 160, 52};  /* x:510-670  stred 590 */

/* Cache 5 uzlu (rect + aktualni stavova barva) pro cd_pulse_tick (item 5) —
 * naplni cd_redraw_all po kazdem prekresleni, cte 20Hz pulse tik. */
static const prim_rect_t *const CD_NODE_RECT[5] = { &CD_GPS, &CD_SENS, &CD_STM, &CD_SI, &CD_FPGA };
/* Popisky uzlu (index = CD_NODE_RECT) — sdileno cd_redraw_all i cd_pulse_tick
 * (pulz obnovuje popisek v LED boxu u sirokych uzlu). */
static const char *const CD_LABEL[5] = {
    "GPS NEO-7M", "SENZORY", "STM32H757", "Si5356A", "FPGA GW1NR-9",
};
static prim_color_t s_cd_color[5];
static uint32_t     s_cd_pulse_frame = 0;

/* Uzel: zaobleny ramecek (vypln BG_0), OBRYS 2 px v barve stavu + stavova
 * "LED" tecka vpravo nahore -> stav uzlu je citelny i bez cteni popisku spoje.
 * Text mono_18 (TODO #11(2b), bylo mono_16) vystredeny, orezany do vnitrku.
 * ⚠️ Sirku i vysku box tb overit podle SKUTECNYCH glyfu (oy/h z font tabulky),
 * NE podle nominalniho ascent/descent fontu (ten je worst-case pro CELOU
 * znakovou sadu vc. diakritiky, realne pouzite znaky — velka pismena/cislice
 * — maji nizsi oy). Pro mono_18 velka pismena/cislice: oy=14 (skutecny vrsek
 * glyfu baseline-14), box top = baseline-18 -> 4 px rezerva; box bottom =
 * baseline+6, nejnizsi pouzity glyf konci baseline+1 -> 5 px rezerva. */
static void cd_node(prim_rect_t r, const char *label, prim_color_t status)
{
    prim_fill_rect_rounded(r, 10, UI_COLOR_BG_0, PRIM_BLEND_OVER);
    prim_stroke_rect_rounded(r, 10, 2, status);
    prim_fill_circle((prim_point_t){(int16_t)(r.x + r.w - 13), (int16_t)(r.y + 13)}, 4, status);
    prim_rect_t tb = {(int16_t)(r.x + 6), (int16_t)(r.y + r.h / 2 - 12), (int16_t)(r.w - 12), 24};
    prim_set_clip(tb);
    prim_draw_text((prim_point_t){(int16_t)(r.x + r.w / 2), (int16_t)(r.y + r.h / 2 + 6)},
                   label, &ui_font_mono_18, UI_COLOR_INK, PRIM_ALIGN_CENTER);
    prim_reset_clip();
}

/* Ramecek skupiny (carkovany) + popisek, ktery ramecek "prerusi" (fieldset styl)
 * — vizualne oddeluje komponenty na FPGA desce od zbytku systemu. */
static void cd_group(prim_rect_t r, const char *label)
{
    prim_point_t tl = {r.x, r.y}, tr = {(int16_t)(r.x + r.w), r.y};
    prim_point_t bl = {r.x, (int16_t)(r.y + r.h)}, br = {(int16_t)(r.x + r.w), (int16_t)(r.y + r.h)};
    prim_draw_line_dashed(tl, tr, 1, UI_COLOR_LINE, 6, 5);
    prim_draw_line_dashed(bl, br, 1, UI_COLOR_LINE, 6, 5);
    prim_draw_line_dashed(tl, bl, 1, UI_COLOR_LINE, 6, 5);
    prim_draw_line_dashed(tr, br, 1, UI_COLOR_LINE, 6, 5);
    int16_t tw = prim_text_width(label, &ui_font_sans_14);
    prim_fill_rect((prim_rect_t){(int16_t)(r.x + 12), (int16_t)(r.y - 8),
                                 (int16_t)(tw + 12), 16}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_draw_text((prim_point_t){(int16_t)(r.x + 18), (int16_t)(r.y + 4)}, label,
                   &ui_font_sans_14, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
}

/* Sipka u cile 'to' (dve kratke usecky) — smer dle vektoru from->to. */
static void cd_arrowhead(prim_point_t from, prim_point_t to, prim_color_t col)
{
    float dx = (float)(to.x - from.x), dy = (float)(to.y - from.y);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    float ux = dx / len, uy = dy / len, px = -uy, py = ux;
    prim_point_t a = {(int16_t)(to.x - ux * 12 + px * 6), (int16_t)(to.y - uy * 12 + py * 6)};
    prim_point_t b = {(int16_t)(to.x - ux * 12 - px * 6), (int16_t)(to.y - uy * 12 - py * 6)};
    prim_draw_line(to, a, 2, col);
    prim_draw_line(to, b, 2, col);
}

/* Pravouhla (elbow) trasa pres pts[0..n-1] (2-4 body, vzdy vodorovne/svisle
 * usecky) + sipka na poslednim segmentu. Manhattan routing misto primky —
 * cistsi schema, snadno se vyhne uzlum/popiskum. */
static void cd_path(const prim_point_t *pts, int n, prim_color_t col)
{
    for (int i = 0; i + 1 < n; i++) prim_draw_line(pts[i], pts[i + 1], 2, col);
    if (n >= 2) cd_arrowhead(pts[n - 2], pts[n - 1], col);
}

/* Popisek spoje na "pilulce" (chip): sirka se PRIZPUSOBI textu (zadny prazdny
 * blok navic), vypln BG_0 prekryje caru pod textem -> popisek sedi primo NA
 * spoji a zustava citelny. Nahradilo drivejsi pevne siroke boxy. */
static void cd_label_chip(int16_t cx, int16_t cy, const char *text, prim_color_t col)
{
    int16_t tw = prim_text_width(text, &ui_font_mono_14);
    prim_rect_t chip = {(int16_t)(cx - tw / 2 - 9), (int16_t)(cy - 15), (int16_t)(tw + 18), 21};
    prim_fill_rect_rounded(chip, 8, UI_COLOR_BG_0, PRIM_BLEND_OVER);
    prim_set_clip(chip);
    prim_draw_text((prim_point_t){cx, cy}, text, &ui_font_mono_14, col, PRIM_ALIGN_CENTER);
    prim_reset_clip();
}

/* Levy/pravy zarovnany popisek (bocni "external source" bloky OCXO/RF) —
 * stejny orez, jen jine zarovnani a ukotveni na x. Box (y-16, h=22) overen
 * proti SKUTECNYM glyfum OCXO/RF popisku (sans_18: velka pismena/cislice
 * oy=14 -> 2 px rezerva nahore; 'p' nejnizsi pouzity descender h-oy=4 ->
 * 2 px rezerva dole) — viz volajici cd_redraw_all, TODO #11(2b). */
static void cd_label_x(int16_t x, int16_t y, int16_t boxw, const char *text,
                       prim_color_t col, prim_align_t align, const prim_font_t *font)
{
    int16_t bx = (align == PRIM_ALIGN_RIGHT) ? (int16_t)(x - boxw) : x;
    prim_rect_t box = {bx, (int16_t)(y - 16), boxw, 22};
    prim_set_clip(box);
    prim_draw_text((prim_point_t){x, y}, text, font, col, align);
    prim_reset_clip();
}

static void cd_redraw_all(void)
{
    /* y zacina az pod hlavickou karty (card.y+PAD_Y+HEADER_H = 62+9+26=97), aby
     * kazdy redraw diagramu neorizl sestupne znaky popisku "Zive spoje..." */
    prim_fill_rect((prim_rect_t){DG_LX + 4, 98, 764 - 8, 282}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);

    gps_data_t g; gps_get(&g);
    uint16_t s1, s4; uint32_t t1, t4;
    static const sensor_id_t i2c1_ids[5] = { SENS_T49, SENS_ADS0, SENS_ADS1, SENS_ADS2, SENS_ADS3 };
    static const sensor_id_t i2c4_ids[1] = { SENS_T48 };
    i2c_health(i2c1_ids, 5, &s1, &t1);
    i2c_health(i2c4_ids, 1, &s4, &t4);
    char buf[32];

    /* ── Stavy vsech uzlu/spoju NAJEDNOU (barvi ramecek uzlu i jeho spoj) ── */
    prim_color_t c_gps  = g.valid ? UI_COLOR_OK : (g.sentences ? UI_COLOR_WARN : UI_COLOR_BAD);
    prim_color_t c_sens = (s1 || s4) ? UI_COLOR_BAD : UI_COLOR_OK;
    prim_color_t c_spi  = g_spi_ok ? UI_COLOR_OK : UI_COLOR_BAD;
    prim_color_t c_fpga = !g_spi_ok ? UI_COLOR_BAD : (g_freq_stale ? UI_COLOR_WARN : UI_COLOR_OK);
    /* Si5356: LOS_CLKIN (bit3) = skutecna ztrata 10 MHz (LOL se pri fyzicke
     * ztrate vstupu neasertuje). LOS_XTAL (bit2) ignorovan — bez krystalu. */
    prim_color_t c_si; const char *si_st;
    if      (!g_si5356_ok)                       { c_si = UI_COLOR_INK_3;  si_st = "N/A";    }
    else if (g_si5356_status & SI5356_LOS_CLKIN) { c_si = UI_COLOR_BAD;    si_st = "NO REF"; }
    else if (g_si5356_status & SI5356_PLL_LOL)   { c_si = UI_COLOR_BAD;    si_st = "UNLOCK"; }
    else if (g_si5356_status & SI5356_SYS_CAL)   { c_si = UI_COLOR_VIOLET; si_st = "CALIB";  }
    else                                         { c_si = UI_COLOR_OK;     si_st = "LOCK";   }
    prim_color_t c_clk; const char *clk_st;   /* OCXO -> CLKIN (tyz bit jako Si) */
    if      (!g_si5356_ok)                       { c_clk = UI_COLOR_INK_3; clk_st = "N/A"; }
    else if (g_si5356_status & SI5356_LOS_CLKIN) { c_clk = UI_COLOR_BAD;   clk_st = "LOS";  }
    else                                         { c_clk = UI_COLOR_OK;    clk_st = "OK";   }
    prim_color_t c_rf; const char *rf_st;
    if      (!g_spi_ok)   { c_rf = UI_COLOR_INK_3; rf_st = "?";      }
    else if (g_freq_stale){ c_rf = UI_COLOR_BAD;   rf_st = "NO SIG"; }
    else                  { c_rf = UI_COLOR_OK;    rf_st = "OK";     }

    /* ── Kresleni v poradi: skupina -> uzly -> spoje -> popisky (chip navrch,
     * aby prekryl caru pod sebou a zustal citelny). ── */
    cd_group(CD_GROUP, "FPGA deska");
    cd_node(CD_GPS,  CD_LABEL[0], c_gps);
    cd_node(CD_SENS, CD_LABEL[1], c_sens);
    cd_node(CD_STM,  CD_LABEL[2], UI_COLOR_ACC);   /* "my" uzel — akcentni, ne stavovy */
    cd_node(CD_SI,   CD_LABEL[3], c_si);
    cd_node(CD_FPGA, CD_LABEL[4], c_fpga);

    /* Externi zdroje (mimo desku) — jen popisek + stav, sipka vede dovnitr skupiny. */
    /* box 116->124 (TODO #11(2b)): "OCXO 10MHz" pri sans_18 potrebuje 115 px
     * (overeno tabulkou fontu) — v 116 by mel jen 1 px rezervy, 124 dava 9 px. */
    cd_label_x(28, 334, 124, "OCXO 10MHz", UI_COLOR_INK_3, PRIM_ALIGN_LEFT, &ui_font_sans_18);
    cd_label_x(28, 356, 116, clk_st, c_clk, PRIM_ALIGN_LEFT, &ui_font_mono_14);
    cd_label_x(772, 334, 86, "RF vstup", UI_COLOR_INK_3, PRIM_ALIGN_RIGHT, &ui_font_sans_18);
    cd_label_x(772, 356, 86, rf_st, c_rf, PRIM_ALIGN_RIGHT, &ui_font_mono_14);

    /* Spoje (pravouhle trasy; sbernice y=166 nad STM32, SPI2 elbow y=270). */
    { prim_point_t p[4] = {{100, 150}, {100, 166}, {345, 166}, {345, 196}}; cd_path(p, 4, c_gps);  }
    { prim_point_t p[4] = {{695, 150}, {695, 166}, {455, 166}, {455, 196}}; cd_path(p, 4, c_sens); }
    { prim_point_t p[4] = {{400, 250}, {400, 270}, {590, 270}, {590, 316}}; cd_path(p, 4, c_spi);  }
    { prim_point_t p[2] = {{148, 342}, {170, 342}};                          cd_path(p, 2, c_clk);  }
    { prim_point_t p[2] = {{290, 342}, {510, 342}};                          cd_path(p, 2, c_si);   }
    { prim_point_t p[2] = {{692, 342}, {670, 342}};                          cd_path(p, 2, c_rf);   }

    /* Popisky spoju — chip sedi PRIMO na care (prekryje ji), sirka dle textu. */
    snprintf(buf, sizeof buf, "UART/1PPS: %s",
             g.valid ? "FIX" : (g.sentences ? "NO FIX" : "--"));
    cd_label_chip(222, 171, buf, c_gps);
    snprintf(buf, sizeof buf, "I2C1/I2C4: %s", (s1 || s4) ? "CHYBA" : "OK");
    cd_label_chip(575, 171, buf, c_sens);
    snprintf(buf, sizeof buf, "SPI2: %s", g_spi_ok ? "LINK OK" : "NO LINK");
    cd_label_chip(495, 275, buf, c_spi);
    snprintf(buf, sizeof buf, "4x100MHz: %s", si_st);
    cd_label_chip(400, 347, buf, c_si);

    s_cd_color[0] = c_gps; s_cd_color[1] = c_sens; s_cd_color[2] = UI_COLOR_ACC;
    s_cd_color[3] = c_si;  s_cd_color[4] = c_fpga;   /* pro cd_pulse_tick (item 5) */
}

/* ── Pulsujici stavova LED (item 5) ───────────────────────────────────────────
 * cd_redraw_all() prekresluje CELY diagram jen pri zmene cd_state_key() (2 Hz
 * gate) — LED tecka by tak byla staticka. Uzly v poruchovem stavu (BAD/WARN)
 * navic ~20 Hz "dychaji" (trojuhelnikova vlna radiusu 4..6..4 px, perioda
 * ~2,4 s) — priblizi pozornost k tomu, co potrebuje reseni; OK/ACC uzly
 * zustavaji staticke (nerozptyluji). Kazdy tik nejdriv vycisti male okoli
 * tecky (UI_COLOR_BG_0 = vypln uzlu, viz cd_node) — kruh meni polomer (ne jen
 * barvu jako digit-highlight), takze potrebuje skutecny clear, ne jen
 * prekresleni identickeho tvaru. */
static int cd_is_alarm_color(prim_color_t c) { return c == UI_COLOR_BAD || c == UI_COLOR_WARN; }

static int cd_pulse_tick(void)
{
    if (!g_anim_enabled) return 0;      /* VYP -> staticka tecka z posledniho cd_redraw_all */
    s_cd_pulse_frame++;
    int phase = (int)(s_cd_pulse_frame % 24u);          /* 0..23, perioda ~1,2 s @ 20 Hz */
    int tri   = (phase < 12) ? phase : (24 - phase);    /* trojuhelnik 0..12..0 */
    int16_t radius = (int16_t)(4 + (tri * 2) / 12);     /* 4..6..4 px */

    int drew = 0;
    for (int i = 0; i < 5; i++) {
        if (!cd_is_alarm_color(s_cd_color[i])) continue;
        prim_rect_t r = *CD_NODE_RECT[i];
        prim_point_t c = {(int16_t)(r.x + r.w - 13), (int16_t)(r.y + 13)};
        /* ⚠️ Clear box MUSI pokryt MAX polomer (6) + AA okraj (~1 px) SYMETRICKY:
         * ±8 od stredu = 16x16. Puvodni 12x12 (jen do c.x+5/c.y+5) nechal on-axis
         * pixel kruhu na c.x+6/c.y+6 i AA ramp na ±7 NEsmazane -> pri zmensovani
         * polomeru zustavaly jako "duchove carky" vpravo/dole od LED. Box je cely
         * v interieru uzlu (netka se ani borderu, ani vstupnich spoju). */
        prim_rect_t box = {(int16_t)(c.x - 8), (int16_t)(c.y - 8), 16, 16};
        prim_fill_rect(box, UI_COLOR_BG_0, PRIM_BLEND_REPLACE);
        prim_fill_circle(c, radius, s_cd_color[i]);
        /* U sirokych uzlu (FPGA GW1NR-9) zasahuje popisek az do LED rohu -> clear
         * ukrojil horni pixely poslednich znaku. Obnovit je: clip na box + text
         * ve stejne pozici/poradi jako cd_node (kruh nejdriv, text nahoru). */
        prim_set_clip(box);
        prim_draw_text((prim_point_t){(int16_t)(r.x + r.w / 2), (int16_t)(r.y + r.h / 2 + 6)},
                       CD_LABEL[i], &ui_font_mono_18, UI_COLOR_INK, PRIM_ALIGN_CENTER);
        prim_reset_clip();
        drew = 1;
    }
    return drew;
}

/* Stavovy klic (1 znak/stav) — pri zmene se cely diagram prekresli. */
static uint32_t cd_state_key(void)
{
    gps_data_t g; gps_get(&g);
    uint16_t s1, s4; uint32_t t1, t4;
    static const sensor_id_t i2c1_ids[5] = { SENS_T49, SENS_ADS0, SENS_ADS1, SENS_ADS2, SENS_ADS3 };
    static const sensor_id_t i2c4_ids[1] = { SENS_T48 };
    i2c_health(i2c1_ids, 5, &s1, &t1);
    i2c_health(i2c4_ids, 1, &s4, &t4);
    uint32_t k = 0;
    k = k * 4u + (g.valid ? 2u : (g.sentences ? 1u : 0u));
    k = k * 2u + ((s1 || s4) ? 1u : 0u);
    k = k * 2u + (g_spi_ok ? 1u : 0u);
    k = k * 8u + (g_si5356_ok ? (g_si5356_status & 0x1Fu) : 0x1Fu);
    k = k * 2u + (g_freq_stale ? 1u : 0u);
    return k;
}

static void app_gpsdo_render_commdiag(void)
{
    int first = window_first(21);
    static uint32_t c_key = 0xFFFFFFFFu;
    if (first) {
        s_view = 21;
        window_chrome("KOMUNIKACE  blokove schema", WIN_TITLE_Y);
        ui_card_t c = {.rect = {DG_LX, 62, 764, 320}, .header_label = "Zive spoje (barva = stav)"};
        ui_card_render_chrome(&c);
        c_key = 0xFFFFFFFFu;   /* vynuti prvni redraw */
    }
    uint32_t k = cd_state_key();
    if (first || k != c_key) {
        c_key = k;
        cd_redraw_all();
        present_now();
    }
}

/* ── Vodopad / spektrogram odchylky kmitoctu (s_view=26, efekt FX_WATERFALL) ──
 * Vyhrazene okno: barevny pruh, X=cas (novy sloupec 2x/s z app_gpsdo_tick),
 * barva sloupce = frakcni odchylka Δf (heat mapa modra->cervena). Wrap styl —
 * hlava se posouva a pretaci -> jen 1 sloupec/vzorek = levne. Δf je zatim
 * SIMULACE (jako headline). ⚠️ Sloupce = opaque fill (DMA2D + mark_dirty). */
#define WF_X   40
#define WF_Y   102
#define WF_W   720
#define WF_H   278
static int16_t s_wf_head = 0;

static prim_color_t wf_heat(float v)   /* 0..1 -> modra->azurova->zelena->zluta->cervena */
{
    if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
    float r, g, bl;
    if      (v < 0.25f) { float t = v / 0.25f;           r = 0; g = t;     bl = 1; }
    else if (v < 0.50f) { float t = (v - 0.25f) / 0.25f; r = 0; g = 1;     bl = 1 - t; }
    else if (v < 0.75f) { float t = (v - 0.50f) / 0.25f; r = t; g = 1;     bl = 0; }
    else                { float t = (v - 0.75f) / 0.25f; r = 1; g = 1 - t; bl = 0; }
    return PRIM_RGB((uint8_t)(r * 255.0f), (uint8_t)(g * 255.0f), (uint8_t)(bl * 255.0f));
}

static void app_gpsdo_render_waterfall(void)
{
    window_prep();
    s_view = 26;
    window_chrome("SPEKTROGRAM Δf", WIN_TITLE_Y);
    prim_stroke_rect_rounded((prim_rect_t){(int16_t)(WF_X - 2), (int16_t)(WF_Y - 2),
                             (int16_t)(WF_W + 4), (int16_t)(WF_H + 4)}, 2, 1, UI_COLOR_LINE);
    if (g_fx_enabled & FX_WATERFALL) {
        prim_fill_rect((prim_rect_t){WF_X, WF_Y, WF_W, WF_H}, UI_COLOR_BG_0, PRIM_BLEND_REPLACE);
        for (int16_t i = 0; i < WF_H; i++)   /* svisla barevna legenda vlevo */
            prim_fill_rect((prim_rect_t){(int16_t)(WF_X - 22), (int16_t)(WF_Y + i), 12, 1},
                           wf_heat(1.0f - (float)i / (float)(WF_H - 1)), PRIM_BLEND_OVER);
        s_wf_head = 0;
        prim_draw_text((prim_point_t){WF_X, (int16_t)(WF_Y + WF_H + 20)},
                       "cas -> (novy sloupec 2x/s, barva = odchylka Δf; zatim SIMULACE)",
                       &ui_font_sans_14, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    } else {
        prim_fill_rect((prim_rect_t){WF_X, WF_Y, WF_W, WF_H}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        prim_draw_text((prim_point_t){(int16_t)(WF_X + WF_W / 2), (int16_t)(WF_Y + WF_H / 2)},
                       "Efekt vypnut (Animace -> EFEKTY)", &ui_font_sans_18, UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
    }
    view_tabs_render(2);   /* zalozky ALLAN/HIST/SPEKTR (aktivni = SPEKTR) */
    present_now();
}

static void waterfall_tick(void)
{
    if (!(g_fx_enabled & FX_WATERFALL)) return;
    /* Pri RUN krokuje simulaci uz app_gpsdo_tick_freq (20 Hz) -> zde nekrokovat
     * (jinak dvojity krok + inflace ADEV/trend statistiky, kterou 1Hz sampler cte).
     * Pri STOP nikdo jiny nekrokuje -> vodopad si posune sam, aby zil dal. */
    if (!screen_main_is_running()) screen_main_freq_sim_step();
    float v = screen_main_freq_dev_unit();
    prim_set_target(&s_fb); prim_reset_clip();
    prim_fill_rect((prim_rect_t){(int16_t)(WF_X + s_wf_head), WF_Y, 1, WF_H}, wf_heat(v), PRIM_BLEND_REPLACE);
    s_wf_head = (int16_t)((s_wf_head + 1) % WF_W);
    int16_t cw = 3; if (s_wf_head + cw > WF_W) cw = (int16_t)(WF_W - s_wf_head);   /* kurzor "ted" */
    if (cw > 0) prim_fill_rect((prim_rect_t){(int16_t)(WF_X + s_wf_head), WF_Y, cw, WF_H},
                               UI_COLOR_INK_4, PRIM_BLEND_REPLACE);
    present_now();
}

/* ── Prepinace grafickych efektu (s_view=27, otevira se z okna Animace) ───────
 * 6 tlacitek, kazde = jeden efekt: zelene (RUN) = zapnuto, cervene (STOP) =
 * vypnuto. Tap prepne bit g_fx_enabled (persist syscfg flash pres
 * syscfg_flash_tick). Efekt se projevi pri pristi navsteve sveho okna.
 * (Headline glow odstranen 2026-07-26 -> ciste 3x2 mrizka.) */
static const struct { prim_rect_t rect; uint16_t bit; const char *label; } FX_ITEMS[6] = {
    { {40, 100, 356, 64}, FX_WATERFALL,  "Spektrogram" },
    { {404,100, 356, 64}, FX_ALLAN_CONF, "Allan pas" },
    { {40, 172, 356, 64}, FX_HOLD_CONE,  "Holdover kuzel" },
    { {404,172, 356, 64}, FX_OCXO_GAUGE, "OCXO budik" },
    { {40, 244, 356, 64}, FX_SPARK_FILL, "Trend vypln" },
    { {404,244, 356, 64}, FX_SYS_XFADE,  "SYS prolinani" },
};
static void fx_btn_render(int i)
{
    int on = (g_fx_enabled & FX_ITEMS[i].bit) != 0;
    ui_button_t b = {.rect = FX_ITEMS[i].rect, .variant = on ? UI_BUTTON_RUN : UI_BUTTON_STOP,
                     .label = FX_ITEMS[i].label};
    ui_button_render(&b);
}
static void app_gpsdo_render_efekty(void)
{
    window_prep();
    s_view = 27;
    window_chrome("EFEKTY", WIN_TITLE_Y);
    prim_draw_text((prim_point_t){40, 84}, "Zeleny = zapnuto, cerveny = vypnuto (persist pres power-cycle).",
                   &ui_font_sans_14, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    for (int i = 0; i < 6; i++) fx_btn_render(i);
    present_now();
}

/* ── Status ribbon demo (s_view=28) ──────────────────────────────────────────
 * Ukazka trvale stavove listy: LED vsech podsystemu (GPS/FPGA/REF/SENS) na jeden
 * pohled. Zamysleno jako pruh na KAZDE obrazovce (zde jen demo v extra okne).
 * Zive z app_gpsdo_tick (change-key). */
static prim_color_t ribbon_led(int st) { return (st == 0) ? UI_COLOR_OK : (st == 1) ? UI_COLOR_WARN : UI_COLOR_BAD; }
static void ribbon_chip(int16_t x, int16_t y, const char *label, int st)
{
    prim_rect_t r = {x, y, 172, 54};
    prim_fill_rect_rounded(r, 12, UI_COLOR_BG_CARD, PRIM_BLEND_OVER);
    prim_stroke_rect_rounded(r, 12, 1, UI_COLOR_LINE);
    prim_fill_circle((prim_point_t){(int16_t)(x + 24), (int16_t)(y + 27)}, 8, ribbon_led(st));
    prim_draw_text((prim_point_t){(int16_t)(x + 44), (int16_t)(y + 33)}, label,
                   &ui_font_mono_18, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
}
static void ribbon_states(int *gps, int *fpga, int *ref, int *sens)
{
    gps_data_t g; gps_get(&g);
    *gps  = g.valid ? 0 : (g.fix_quality > 0 ? 1 : 2);
    *fpga = g_spi_ok ? (g_freq_stale ? 1 : 0) : 2;
    *ref  = g_si5356_ok ? (((g_si5356_status & 0x18u) != 0) ? 2 : 0) : 1;   /* 0x08 LOS_CLKIN | 0x10 PLL_LOL */
    int se = 0;
    for (int i = 0; i < SENS_COUNT; i++)
        if (i != (int)SENS_T4A && g_sensors[i].err_streak > 0) { se = 1; break; }
    *sens = se;
}
static void app_gpsdo_render_ribbon(void)
{
    static uint32_t s_key = 0xFFFFFFFFu;
    int gps, fpga, ref, sens;
    ribbon_states(&gps, &fpga, &ref, &sens);
    uint32_t key = (uint32_t)(gps | (fpga << 2) | (ref << 4) | (sens << 6));
    int first = window_first(28);
    if (first) {
        s_view = 28;
        window_chrome("STATUS RIBBON", WIN_TITLE_Y);
        prim_draw_text((prim_point_t){40, 96}, "Ukazka trvale stavove listy — LED vsech podsystemu na jeden pohled.",
                       &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){40, 302}, "Zamysleno jako pruh na KAZDE obrazovce (zde demo). Zelena=OK, amber=degradace, cervena=chyba.",
                       &ui_font_sans_14, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
    }
    if (first || key != s_key) {
        s_key = key;
        prim_fill_rect((prim_rect_t){30, 150, 740, 74}, UI_COLOR_BG_0, PRIM_BLEND_REPLACE);
        ribbon_chip(44,  160, "GPS",  gps);
        ribbon_chip(228, 160, "FPGA", fpga);
        ribbon_chip(412, 160, "REF",  ref);
        ribbon_chip(596, 160, "SENS", sens);
        present_now();
    }
}

void app_gpsdo_clear(void)
{
    window_prep();
    s_view = 0;
    prim_fill_rect((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                   UI_COLOR_BG_0, PRIM_BLEND_REPLACE);
    present_now();
}

void app_gpsdo_tick(void)
{
    survey_accumulate();   /* self-survey (#53) bezi na pozadi i mimo okno */
    if (s_view == 1) app_gpsdo_render_diag();     /* live refresh of diagnostics */
    else if (s_view == 2) app_gpsdo_render_gps();     /* live refresh of GPS/GNSS okna */
    else if (s_view == 3) app_gpsdo_render_health();  /* live refresh of system health */
    else if (s_view == 4) app_gpsdo_render_sensors(); /* live refresh of senzory podmenu */
    else if (s_view == 5) app_gpsdo_render_mem();     /* live refresh (RTOS heap) okna PAMET */
    else if (s_view == 6) app_gpsdo_render_histogram(); /* live/snapshot histogram mereni */
    else if (s_view == 23) app_gpsdo_render_allan();  /* fullscreen Allan graf (zivy) */
    else if (s_view == 8) saver_draw();               /* screensaver hodiny (1x/s dle RTC) */
    else if (s_view == 9) app_gpsdo_render_trend();   /* fullscreen trend (zivy) */
    else if (s_view == 10) app_gpsdo_render_about();  /* O pristroji (uptime tick) */
    else if (s_view == 14) app_gpsdo_render_reference(); /* Reference (zivy Si5356 lock) */
    else if (s_view == 16) app_gpsdo_render_holdover();  /* Holdover (zivy stav) */
    else if (s_view == 17) app_gpsdo_render_datalog();   /* Datalog (zivy pocet zaznamu) */
    else if (s_view == 18) app_gpsdo_render_alarms();    /* Alarmy (zivy mute + pocitadla) */
    else if (s_view == 19) app_gpsdo_render_counter();   /* Citac (zivy detail mereni FPGA) */
    else if (s_view == 21) app_gpsdo_render_commdiag();  /* Komunikace: blokove schema (zive) */
    else if (s_view == 22) app_gpsdo_render_cas();       /* Cas / zona (zivy UTC + lokalni) */
    else if (s_view == 26) waterfall_tick();             /* Spektrogram Δf (novy sloupec) */
    else if (s_view == 28) app_gpsdo_render_ribbon();    /* Status ribbon demo (zive LED) */
    else if (s_view == 29) app_gpsdo_render_graphs();    /* Grafy: casovy prubeh senzoru (#31) */
    else if (s_view == 30) app_gpsdo_render_hbars();     /* Prehled kanalu: horizontalni bargrafy */
    else if (s_view == 31) app_gpsdo_render_math();      /* Math/limity (zive X/Y/verdikt) */
    else if (s_view == 32) app_gpsdo_render_survey();    /* Self-survey (zive prumerovani polohy) */
    else if (s_view == 34) app_gpsdo_render_meas();      /* MERENI: perioda/jednotky/statistika/TFOM (#67) */
}

/* Hodinovy tik (~kazdych 100 ms): na hlavni obrazovce prekresli cas/datum z GPS
 * a (pri zmene sat/fix) horni listu (GNSS lock + pocet druzic). */
void app_gpsdo_tick_clock(uint32_t ms_since_boot)
{
    if (s_view != 0) return;
    prim_set_target(&s_fb);
    prim_reset_clip();
    if (screen_main_redraw_time(ms_since_boot)) s_dirty = 1;   /* flip odlozen na flush */
    if (screen_main_redraw_cpu(0)) s_dirty = 1;                /* CM7/CM4 CPU % (change-detect) */

    /* Horni lista (GNSS lock + druzice + HDOP) jen pri ZMENE GPS stavu — sat/fix/HDOP
     * se meni pomalu (~1 Hz z GGA), takze redraw headeru bezi vzacne (ne kazdy tik). */
    static int last_sat = -1, last_fixq = -1, last_hdop10 = -1;
    gps_data_t g;
    gps_get(&g);
    int hdop10 = (int)(g.hdop * 10.0f + 0.5f);   /* HDOP na 1 des. misto -> change-detect */
    if ((int)g.num_sat != last_sat || (int)g.fix_quality != last_fixq || hdop10 != last_hdop10
        || screen_main_sys_poll()) {   /* + zmena agregovaneho SYS zdravi -> prebarvi pilulku */
        last_sat = (int)g.num_sat;
        last_fixq = (int)g.fix_quality;
        last_hdop10 = hdop10;
        if (screen_main_redraw_header()) s_dirty = 1;
    }
}

/* RF vstupni vykon z AD8307 log-detektoru: ADS1115 AIN1 (SensorsTask fast-path
 * ~10 Hz -> g_sensors[SENS_ADS1], mV). Volat 10x/s z UiTasku, jen na hlavni
 * obrazovce. Flip jen pri zmene. AD8307: Vout ~ log(Pin), slope ~25 mV/dB,
 * intercept ~-84 dBm -> dBm = mV/slope + intercept. Bargraf mapuje pasmo
 * RF_DBM_MIN..MAX. Slope/intercept jsou editovatelna kalibrace (`g_calib`,
 * okno Kalibrace) — vychozi jsou datasheet hodnoty AD8307.
 * ⚠️ RF_DBM_MIN/MAX jsou definovane vyse (pred oknem Animace/demo, ktere je
 * pouziva jako prvni — anim_target_pct). */
void app_gpsdo_tick_signal(void)
{
    if (s_view != 0) return;             /* RF level je zivy HW udaj (bez RUN gate) */
    const sensor_stat_t *rf = &g_sensors[SENS_ADS1];
    if (rf->samples == 0) return;        /* jeste zadne mereni */
    float mv = rf->last; if (mv < 0.0f) mv = 0.0f;
    float dbm = mv / g_calib.ad8307_slope_mv_db + g_calib.ad8307_intercept_dbm;
    int32_t dbm10 = (int32_t)lround_f(dbm * 10.0f);
    int16_t pct = (int16_t)((dbm - (float)RF_DBM_MIN) * 100.0f / (float)(RF_DBM_MAX - RF_DBM_MIN));
    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;

    prim_set_target(&s_fb);
    prim_reset_clip();
    if (screen_main_redraw_signal(pct, dbm10)) s_dirty = 1;   /* flip odlozen na flush */
}

/* Okno Animace/demo (s_view=24): krok ease-out helperu + incrementalni
 * prekresleni JEN zmenenych segmentu bargrafu a dvou cislic (dtext si cisti
 * box -> mark_dirty -> copy-forward pres 3 buffery v poradku). Bez signalu
 * bezi demo sekvence urovni. */
static void tick_anim_demo(void)
{
    s_anim_frame++;

    int demo = 0;
    int16_t tgt = anim_target_pct(&demo);
    anim_set(&s_anim_bar, (float)tgt);
    int moved = anim_step(&s_anim_bar, 0.25f, 0.4f);   /* k=0.25 dojezd, snap 0.4 % */

    int16_t cur_pct = (int16_t)(s_anim_bar.cur + 0.5f);
    if (cur_pct < 0) cur_pct = 0; else if (cur_pct > 100) cur_pct = 100;

    prim_set_target(&s_fb);
    prim_reset_clip();

    int drew = 0;

    /* Bargraf: jen segmenty zmenene proti posledne vykreslenemu pct. */
    int16_t prev = (s_anim_last_pct < 0) ? 0 : s_anim_last_pct;
    if (s_anim_last_pct < 0 || cur_pct != prev) {
        if (ui_bargraph_update(&ANIM_BAR_RECT, prev, cur_pct) > 0 || s_anim_last_pct < 0) {
            char vt[12]; snprintf(vt, sizeof vt, "%d %%", cur_pct);
            /* Clear boxu value textu PRED prekreslenim (jinak kratsi hodnota nechá
             * ocas delší a AA hrany se scitaji) — stejny pattern jako redraw_signal. */
            prim_fill_rect((prim_rect_t){(int16_t)(ANIM_BAR_RECT.x + ANIM_BAR_RECT.w - 120),
                                         (int16_t)(ANIM_BAR_RECT.y - 2), 122, 20},
                           UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
            ui_bargraph_value(&ANIM_BAR_RECT, vt, UI_COLOR_ACC);
            drew = 1;
        }
        s_anim_last_pct = cur_pct;
    }

    /* Cislice cil/aktualni (dchg -> jen pri zmene; dtext si cisti box). */
    char b[12];
    snprintf(b, sizeof b, "%d %%", tgt);
    if (dchg(s_anim_c_tgt, sizeof s_anim_c_tgt, b)) {
        dtext((int16_t)(DG_LLBL + 230), 250, 120, b, UI_COLOR_INK_2, &ui_font_mono_18); drew = 1; }
    snprintf(b, sizeof b, "%d %%", cur_pct);
    if (dchg(s_anim_c_cur, sizeof s_anim_c_cur, b)) {
        dtext((int16_t)(DG_LLBL + 230), 286, 120, b, UI_COLOR_ACC, &ui_font_mono_18); drew = 1; }

    if (drew || moved) s_dirty = 1;   /* flip odlozen na flush */
}

/* Subokno "PRIKLADY ANIMACI" (s_view=25): jeden tik smycky — prekresli vnitrek
 * kazde ze 6 dlazdic. Vzdy neco kresli -> vzdy s_dirty. */
static void tick_animdemo(void)
{
    prim_set_target(&s_fb);
    prim_reset_clip();
    uint32_t f = ++s_ad_frame;
    char buf[16];

    /* 1. Ease-out bar: cil skace kazde ~2 s, aktualni plynule dojizdi. */
    {
        static const int16_t LV[6] = {10, 85, 40, 95, 25, 65};
        anim_set(&s_ad_bar, (float)LV[(f / 40u) % 6u]);
        float v = ad_ease(&s_ad_bar, 0.25f);
        prim_rect_t cr = ad_content(0);
        prim_fill_rect(cr, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        /* ⚠️ Value text baseline MUSI byt UVNITR cr (ne na cr.y = horni hrana),
         * jinak glyf ascentuje NAD clear oblast -> pri kazdem tiku se neprepise
         * a cisla se navrstvi do necitelneho chumlu. Baseline cr.y+15 = glyf cely
         * v cr; stopa posunuta niz (cr.y+28), aby se nekryly. */
        snprintf(buf, sizeof buf, "%d %%", (int)(v + 0.5f));
        prim_draw_text((prim_point_t){(int16_t)(cr.x + cr.w), (int16_t)(cr.y + 15)}, buf,
                       &ui_font_mono_18, UI_COLOR_INK_2, PRIM_ALIGN_RIGHT);
        prim_rect_t track = {cr.x, (int16_t)(cr.y + 28), cr.w, 18};
        prim_fill_rect_rounded(track, 6, UI_COLOR_BG_0, PRIM_BLEND_OVER);
        int16_t fw = (int16_t)(cr.w * v / 100.0f);
        if (fw > 4) prim_fill_rect_rounded((prim_rect_t){track.x, track.y, fw, track.h},
                                           6, UI_COLOR_ACC, PRIM_BLEND_OVER);
    }

    /* 2. Pulsujici LED: radius 6..12..6 (trojuhelnik), barva cykluje OK/WARN/BAD. */
    {
        prim_rect_t cr = ad_content(1);
        prim_fill_rect(cr, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        int phase = (int)(f % 48u);
        int tri = (phase < 24) ? phase : (48 - phase);   /* 0..24..0 */
        int16_t radius = (int16_t)(6 + (tri * 6) / 24);  /* 6..12..6 px */
        prim_color_t col; const char *nm;
        switch ((f / 60u) % 3u) {
            case 0:  col = UI_COLOR_OK;   nm = "stav: OK";    break;
            case 1:  col = UI_COLOR_WARN; nm = "stav: WARN";  break;
            default: col = UI_COLOR_BAD;  nm = "stav: CHYBA"; break;
        }
        prim_point_t c = {(int16_t)(cr.x + 24), (int16_t)(cr.y + cr.h / 2)};
        prim_fill_circle(c, radius, col);
        prim_draw_text((prim_point_t){(int16_t)(cr.x + 54), (int16_t)(cr.y + cr.h / 2 - 2)},
                       nm, &ui_font_mono_18, col, PRIM_ALIGN_LEFT);
    }

    /* 3. Flash tlacitka: staticke tlacitko, kazde ~1,5 s na 5 tiku accent obrys. */
    {
        prim_rect_t cr = ad_content(2);
        prim_fill_rect(cr, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        prim_rect_t btn = {(int16_t)(cr.x + 40), (int16_t)(cr.y + 4),
                           (int16_t)(cr.w - 80), (int16_t)(cr.h - 12)};
        prim_fill_rect_rounded(btn, 10, UI_COLOR_BG_0, PRIM_BLEND_OVER);
        prim_stroke_rect_rounded(btn, 10, 1, UI_COLOR_LINE);
        prim_draw_text((prim_point_t){(int16_t)(btn.x + btn.w / 2), (int16_t)(btn.y + btn.h / 2 - 2)},
                       "STISK", &ui_font_mono_18, UI_COLOR_INK_2, PRIM_ALIGN_CENTER);
        if ((f % 30u) < 5u)   /* flash okno: 5 z 30 tiku (~250 ms z 1,5 s) */
            prim_stroke_rect_rounded(btn, 10, 2, UI_COLOR_ACC);
    }

    /* 4. Eased cislo: skoci na novy cil kazde ~1,7 s, plynule dojede. */
    {
        prim_rect_t cr = ad_content(3);
        if (f % 34u == 1u)   /* novy cil (pseudonahodny, deterministicky) */
            anim_set(&s_ad_num, (float)((int)((f * 2654435761u) % 2000u) - 1000));
        float v = ad_ease(&s_ad_num, 0.2f);
        prim_fill_rect(cr, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        snprintf(buf, sizeof buf, "%+ld", lround_f(v));
        prim_draw_text((prim_point_t){(int16_t)(cr.x + cr.w / 2), (int16_t)(cr.y + cr.h / 2 + 6)},
                       buf, &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
    }

    /* 5. Zvyrazneni cislice: posledni cislice se meni a na 5 tiku problikne accent. */
    {
        prim_rect_t cr = ad_content(4);
        prim_fill_rect(cr, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        uint32_t chg = f % 25u;
        if (chg == 1u) s_ad_digit = (s_ad_digit + 1) % 10;
        const char *pref = "10 000 00";
        char last[2] = {(char)('0' + s_ad_digit), '\0'};
        int16_t pw = prim_text_width(pref, &ui_font_mono_25);
        int16_t lw = prim_text_width("0", &ui_font_mono_25);
        int16_t x0 = (int16_t)(cr.x + cr.w / 2 - (pw + lw) / 2);
        int16_t by = (int16_t)(cr.y + cr.h / 2 + 6);
        prim_draw_text((prim_point_t){x0, by}, pref, &ui_font_mono_25, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_color_t dc = (chg < 5u) ? UI_COLOR_ACC : UI_COLOR_INK_2;
        prim_draw_text((prim_point_t){(int16_t)(x0 + pw), by}, last, &ui_font_mono_25, dc, PRIM_ALIGN_LEFT);
    }

    /* 6. Prolinani (fade): text plynule prechazi cerna->accent->cerna. */
    {
        prim_rect_t cr = ad_content(5);
        prim_fill_rect(cr, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        int phase = (int)(f % 60u);
        int tri = (phase < 30) ? phase : (60 - phase);   /* 0..30..0 */
        float t = (float)tri / 30.0f;
        prim_draw_text((prim_point_t){(int16_t)(cr.x + cr.w / 2), (int16_t)(cr.y + cr.h / 2 + 6)},
                       "FADE", &ui_font_mono_25, fade_color(UI_COLOR_ACC, t), PRIM_ALIGN_CENTER);
    }

    s_dirty = 1;   /* neco se vzdy zmenilo -> flip pri flush */
}

/* Hlavni obrazovka (s_view=0): micro-flash tlacitka (item 3) + (dal se sem
 * pripoji eased statistiky/trend/digit-highlight). */
static void tick_anim_main(void)
{
    prim_set_target(&s_fb);
    prim_reset_clip();
    if (screen_main_button_flash_tick()) s_dirty = 1;
    if (screen_main_tick_stats_anim())   s_dirty = 1;
    if (screen_main_tick_trend_anim())   s_dirty = 1;
    if (screen_main_tick_sys_xfade())    s_dirty = 1;
}

/* Rychly tik animaci (~20 Hz z UiTask): dispatch dle otevreneho okna. Kazda
 * vetev je no-op mimo sve okno -> zanedbatelny dopad, kdyz zadne z nich neni
 * otevrene (typicky beh na hlavni obrazovce). */
void app_gpsdo_tick_anim(void)
{
    if      (s_view == 0)  tick_anim_main();
    else if (s_view == 24) tick_anim_demo();
    else if (s_view == 25) tick_animdemo();
    else if (s_view == 7)  settings_tick_jas();
    else if (s_view == 21) {
        prim_set_target(&s_fb);
        prim_reset_clip();
        if (cd_pulse_tick()) s_dirty = 1;
    }
}

/* Simulace kmitoctu (~20x/s, jen hlavni obrazovka): per-segment dirty redraw. */
void app_gpsdo_tick_freq(void)
{
    if (!screen_main_is_running()) return;    /* STOP -> cislo i statistika zamrznou */
    if (s_view != 0) {
        /* Mimo hlavni obrazovku (okno/screensaver): simulace bezi dal (jinak by
         * ADEV pyramida nikdy nedosahla dlouhych tau — vzorkuje se z s_freq_n),
         * jen se nekresli. */
        screen_main_freq_sim_step();
        return;
    }
    prim_set_target(&s_fb);
    prim_reset_clip();
    if (screen_main_redraw_freq()) s_dirty = 1;   /* flip odlozen na flush */
}

/* GPSDO statistika (jen hlavni obrazovka, jen RUN): vzorkovani frakcni odchylky (~1x/s). */
void app_gpsdo_tick_stats_sample(void)
{
    /* Vzorkuje se VZDY kdyz mereni bezi — nezavisle na zobrazenem okne (drive
     * jen na main -> Allan/histogram se zastavily pri screensaveru/oknech a
     * nikdy nedosahly dlouhych tau). Kresleni je gatovane zvlast (draw ticky). */
    if (!screen_main_is_running()) return;   /* STOP -> trend/Allan zamrznou */
    screen_main_stats_sample();
    mp_stats_add(&s_meas_stats, screen_main_freq_hz());   /* statistika okna MERENI (#67, RESET nuluje) */
    /* #44: prubezne vyhodnoceni limitu (nezavisle na oknu -> alarm hlida i mimo
     * okno MATH). Verdikt cte alarm.c (edge PASS->FAIL). Levne (1x/s). */
    g_meas_verdict = (uint8_t)meas_limit_eval(&g_meas_cfg,
                        meas_math_apply(&g_meas_cfg, screen_main_freq_hz()));
}

/* GPSDO statistika: zive prekresleni trend + offset/sigma (~1x/s, jen RUN). */
void app_gpsdo_tick_stats_draw(void)
{
    if (s_view != 0 || !screen_main_is_running()) return;
    prim_set_target(&s_fb);
    prim_reset_clip();
    if (screen_main_redraw_stats()) s_dirty = 1;   /* flip odlozen na flush */
}

/* GPSDO statistika: zive prekresleni Allan grafu (~1x/s, tezsi render, jen RUN). */
void app_gpsdo_tick_allan_draw(void)
{
    if (s_view != 0 || !screen_main_is_running()) return;
    prim_set_target(&s_fb);
    prim_reset_clip();
    if (screen_main_redraw_allan()) s_dirty = 1;
}

/* Present coalescing: jeden flip pro vsechny nahromadene zmeny (clock/signal/freq).
 * Vola UiTask na ~30Hz gate. Vrati 1 pokud flipnul. */
int app_gpsdo_flush(void)
{
    if (!s_dirty) return 0;
    present_now();
    return 1;
}

bool app_gpsdo_handle_touch(int16_t x, int16_t y)
{
    if (s_view == 0) {
        if (screen_main_hit_gnss(x, y)) { nav_push(0); app_gpsdo_render_gps(); return true; }   /* GNSS pill */
        if (screen_main_hit_sys(x, y))  { nav_push(0); app_gpsdo_render_health(); return true; }  /* SYS pill */
        if (screen_main_hit_allan(x, y)) { nav_push(0); app_gpsdo_render_allan(); return true; }  /* Allan nahled -> ALLAN okno */
        if (screen_main_hit_trend(x, y)) { nav_push(0); app_gpsdo_render_trend(); return true; }      /* trend -> fullscreen */
        int b = screen_main_hit_button(x, y);
        if (b == 0) {   /* ⚠️ DOCASNE: "Main SW" A/B prepinac layoutu (misto PERIOD/FREQ,
                         * viz footer_button_def + screen_main_toggle_layout) — cela
                         * mrizka meni geometrii, staci proto plny screen_main_render(). */
            screen_main_toggle_layout();
            prim_set_target(&s_fb);
            prim_reset_clip();
            screen_main_render();
            present_now();
            return true;
        }
        if (b == 4) { nav_push(0); app_gpsdo_render_menu(); return true; }   /* MENU -> rozcestnik */
        if (b >= 0) {                                /* RUN/GATE/CHAN (PERIOD/FREQ docasne vyrazen, viz b==0 vyse) */
            screen_main_button_action(b);
            prim_set_target(&s_fb);
            prim_reset_clip();
            screen_main_redraw_button(b);            /* only the pressed button */
            screen_main_button_flash_start(b);        /* micro-flash (item 3); no-op kdyz g_anim_enabled=0 */
            /* RUN/STOP nemeni titulek, zato meni PODBARVENI kmitoctu (STOP =
             * lehce cervene). Pri STOP uz 20Hz tick_freq nebezi, takze podklad
             * musime prekreslit tady — jinak by zustal ve stare barve. */
            if (b == 1) screen_main_redraw_freq_area();
            else        screen_main_redraw_title();
            present_now();
            return true;
        }
    } else {
        /* Diagnostika = technicky hub -> DIAGRAM / PAMET / SELFTEST podokna. */
        if (s_view == 1 && in_rect(x, y, DIAG_DIAGRAM_BTN_RECT)) {
            nav_push(1); app_gpsdo_render_commdiag();
            return true;
        }
        if (s_view == 1 && in_rect(x, y, DIAG_MEM_BTN_RECT)) {
            nav_push(1); app_gpsdo_render_mem();
            return true;
        }
        if (s_view == 1 && in_rect(x, y, DIAG_ST_BTN_RECT)) {
            nav_push(1); app_gpsdo_render_selftest();
            return true;
        }
        /* System Health -> tap na "SENZORY" / "DIAGNOSTIKA" / "NASTAVENI". */
        if (s_view == 3 && in_rect(x, y, SENS_BTN_RECT)) {
            nav_push(3); app_gpsdo_render_sensors();
            return true;
        }
        if (s_view == 3 && in_rect(x, y, HEALTH_DIAG_BTN_RECT)) {
            nav_push(3); app_gpsdo_render_diag();
            return true;
        }
        if (s_view == 3 && in_rect(x, y, SET_BTN_RECT)) {   /* Health -> Nastaveni */
            nav_push(3); app_gpsdo_render_settings();
            return true;
        }
        if (s_view == 3 && in_rect(x, y, HEALTH_GRAPH_BTN_RECT)) {   /* Health -> Grafy (#31) */
            nav_push(3); app_gpsdo_render_graphs();
            return true;
        }
        if (s_view == 29) {                                 /* okno Grafy: presety -/+ */
            if (in_rect(x, y, GRAPH_MINUS) && s_graph_idx > 0) {
                s_graph_idx--; s_view = 0xFF;   /* vynut full render (novy footer/okno) */
                app_gpsdo_render_graphs();
                return true;
            }
            if (in_rect(x, y, GRAPH_PLUS) && s_graph_idx < GRAPH_PRESET_N - 1) {
                s_graph_idx++; s_view = 0xFF;
                app_gpsdo_render_graphs();
                return true;
            }
            if (in_rect(x, y, GRAPH_BARS_BTN)) {   /* -> sesterske PREHLED KANALU (bez nav_push) */
                app_gpsdo_render_hbars();
                return true;
            }
        }
        if (s_view == 30 && in_rect(x, y, HBARS_GRAF_BTN)) {   /* PREHLED -> GRAFY (bez nav_push) */
            app_gpsdo_render_graphs();
            return true;
        }
        if (s_view == 19 && in_rect(x, y, CNT_MEAS_BTN)) {     /* CITAC -> MERENI (sesterske, bez nav_push) */
            app_gpsdo_render_meas();
            return true;
        }
        if (s_view == 34) {                                    /* okno MERENI: ovladace */
            if (in_rect(x, y, MEAS_MODE_BTN)) {                /* FREKV <-> PERIODA (meni label -> full render) */
                s_meas_mode ^= 1; s_view = 0xFF; app_gpsdo_render_meas(); return true;
            }
            if (in_rect(x, y, MEAS_UNIT_BTN)) {                /* cyklus jednotky (meni label -> full render) */
                /* Jen Hz/ppm/ppb/ppt (% MP_UNIT_REL=4) — REL (raw zlomek ~1e-9) by se
                 * ve fmt_fixed(,4) zobrazil vzdy jako 0.0000; pouzivatel zada tyto 4. */
                s_meas_unit = (mp_unit_t)((s_meas_unit + 1) % MP_UNIT_REL);
                s_view = 0xFF; app_gpsdo_render_meas(); return true;
            }
            if (in_rect(x, y, MEAS_RST_BTN)) {                 /* reset statistiky */
                mp_stats_reset(&s_meas_stats); app_gpsdo_render_meas(); return true;
            }
            if (in_rect(x, y, MEAS_CNT_BTN)) {                 /* MERENI -> CITAC (sesterske) */
                app_gpsdo_render_counter(); return true;
            }
        }
        if (s_view == 31) {                                 /* okno MATH / LIMITY: ovladace */
            int hit = 1;
            if (in_rect(x, y, MATH_BTN_MATH)) {
                g_meas_cfg.math_en = g_meas_cfg.math_en ? 0 : 1;
                if (g_meas_cfg.limit_en) math_recenter_limits();
            } else if (in_rect(x, y, MATH_BTN_M)) {
                s_math_m_idx = (s_math_m_idx + 1) % MATH_M_N;
                g_meas_cfg.m = MATH_M_PRESETS[s_math_m_idx];
                if (g_meas_cfg.limit_en) math_recenter_limits();
            } else if (in_rect(x, y, MATH_BTN_BM)) {
                g_meas_cfg.b -= MATH_B_STEP;
                if (g_meas_cfg.limit_en) math_recenter_limits();
            } else if (in_rect(x, y, MATH_BTN_BP)) {
                g_meas_cfg.b += MATH_B_STEP;
                if (g_meas_cfg.limit_en) math_recenter_limits();
            } else if (in_rect(x, y, MATH_BTN_NULL)) {
                if (g_meas_cfg.null_en) g_meas_cfg.null_en = 0;
                else                    meas_math_capture_null(&g_meas_cfg, screen_main_freq_hz());
                if (g_meas_cfg.limit_en) math_recenter_limits();
            } else if (in_rect(x, y, MATH_BTN_LIM)) {
                g_meas_cfg.limit_en = g_meas_cfg.limit_en ? 0 : 1;
                if (g_meas_cfg.limit_en) math_recenter_limits();
            } else if (in_rect(x, y, MATH_BTN_BANDM)) {
                if (s_math_band_idx > 0) s_math_band_idx--;
                math_recenter_limits();
            } else if (in_rect(x, y, MATH_BTN_BANDP)) {
                if (s_math_band_idx < MATH_BAND_N - 1) s_math_band_idx++;
                math_recenter_limits();
            } else if (in_rect(x, y, MATH_BTN_ALRM)) {
                g_meas_cfg.alarm_en = g_meas_cfg.alarm_en ? 0 : 1;
            } else {
                hit = 0;
            }
            if (hit) {
                prim_set_target(&s_fb); prim_reset_clip();
                math_render_controls();
                math_render_live(1);
                present_now();
                return true;
            }
        }
        if (s_view == 7) {                                  /* okno Nastaveni: ovladace */
            /* Partial updaty (latence): prekresli se JEN dotceny ovladac + flip.
             * Cele okno se kresli jen pri vstupu a pri zmene schematu. */
            #define SETTINGS_UPD(fn) do { prim_set_target(&s_fb); prim_reset_clip(); \
                                          fn(); present_now(); } while (0)
            if (in_rect(x, y, MUTE_RECT)) {
                g_sound_muted = g_sound_muted ? 0 : 1;
                g_sys_cfg_dirty = 1;
                SETTINGS_UPD(settings_upd_mute);
                return true;
            }
            /* Jas: g_brightness (a tedy HW backlight) se meni OKAMZITE; bar na
             * obrazovce jen dostane novy cil a plynule ho dojede (settings_tick_jas
             * z app_gpsdo_tick_anim, ~20 Hz). Prvni krok hned tady, aby stisk mel
             * viditelnou okamzitou odezvu (necekalo se na dalsi tik). */
            if (in_rect(x, y, BR_MINUS)) { brightness_step(-26); SETTINGS_UPD(settings_tick_jas); return true; }
            if (in_rect(x, y, BR_PLUS))  { brightness_step(+26); SETTINGS_UPD(settings_tick_jas); return true; }
            if (in_rect(x, y, ADEN_RECT)) {
                g_autodim_en = g_autodim_en ? 0 : 1;
                g_sys_cfg_dirty = 1;
                SETTINGS_UPD(settings_upd_dim);
                return true;
            }
            if (in_rect(x, y, DIM_MINUS)) { autodim_step(-1); SETTINGS_UPD(settings_upd_dim); return true; }
            if (in_rect(x, y, DIM_PLUS))  { autodim_step(+1); SETTINGS_UPD(settings_upd_dim); return true; }
            if (in_rect(x, y, THEME_RECT)) {                /* tmave <-> svetle schema */
                g_theme_light = g_theme_light ? 0 : 1;
                g_sys_cfg_dirty = 1;
                ui_theme_select(g_theme_light);
                screen_main_invalidate();                   /* bg_cache je v barvach stareho schematu */
                screen_main_init();                         /* prestavet HNED (settings bg blituje) */
                app_gpsdo_render_settings();
                return true;
            }
            if (in_rect(x, y, LANG_RECT)) {                 /* CZ <-> EN (texty postupne) */
                g_lang_en = g_lang_en ? 0 : 1;
                g_sys_cfg_dirty = 1;
                SETTINGS_UPD(settings_upd_lang);
                return true;
            }
            if (in_rect(x, y, REF_RECT))   { nav_push(7); app_gpsdo_render_reference(); return true; }
            if (in_rect(x, y, ABOUT_RECT)) { nav_push(7); app_gpsdo_render_about(); return true; }
            if (in_rect(x, y, SETUP_ENTER_RECT)) { nav_push(7); app_gpsdo_render_setups(); return true; }
            #undef SETTINGS_UPD
        }
        if (s_view == 33) {                                 /* okno SESTAVY: slot -/+, uloz/nacti/smaz */
            int redraw = 0, reload = 0;
            if (in_rect(x, y, SET_SLOT_MINUS) && s_setup_slot > 0)            { s_setup_slot--; s_setup_msg = 0; redraw = 1; }
            else if (in_rect(x, y, SET_SLOT_PLUS) && s_setup_slot < SETUP_N-1){ s_setup_slot++; s_setup_msg = 0; redraw = 1; }
            else if (in_rect(x, y, SETUP_SAVE_RECT))  { s_setup_msg = setup_save(s_setup_slot)  ? 1 : 4; redraw = 1; }
            else if (in_rect(x, y, SETUP_ERASE_RECT)) { s_setup_msg = setup_erase(s_setup_slot) ? 3 : 4; redraw = 1; }
            else if (in_rect(x, y, SETUP_LOAD_RECT))  { reload = setup_load(s_setup_slot) ? 1 : 0;
                                                        s_setup_msg = reload ? 2 : 5; redraw = 1; }
            if (reload) {   /* nactena sestava muze zmenit tema/jas -> plny refresh jako prepinac schematu */
                ui_theme_select(g_theme_light);
                screen_main_invalidate();
                screen_main_init();
                s_view = 0xFF;                              /* vynut full render okna v novem tematu */
                app_gpsdo_render_setups();
                return true;
            }
            if (redraw) {
                prim_set_target(&s_fb); prim_reset_clip();
                setups_render_dynamic();
                present_now();
                return true;
            }
        }
        if (s_view == 2 && in_rect(x, y, GPS_SURVEY_BTN)) {   /* GPS -> okno Self-survey */
            nav_push(2); app_gpsdo_render_survey();
            return true;
        }
        if (s_view == 2 && in_rect(x, y, GPS_SAT_RECT)) {  /* GPS: prepni bargraf <-> sky plot */
            s_gps_polar = !s_gps_polar;
            app_gpsdo_render_gps();                        /* change-key prekresli kartu Druzice */
            return true;
        }
        if (s_view == 32 && in_rect(x, y, SURVEY_BTN)) {   /* Self-survey: START/STOP */
            if (s_survey.active) survey_stop(); else survey_start();
            prim_set_target(&s_fb); prim_reset_clip();
            ui_button_t tg = {.rect = SURVEY_BTN,
                              .variant = s_survey.active ? UI_BUTTON_STOP : UI_BUTTON_RUN,
                              .label = s_survey.active ? "STOP" : "START"};
            ui_button_render(&tg);
            present_now();
            return true;
        }
        if (s_view == 24 && in_rect(x, y, ANIM_TOGGLE_RECT)) {   /* Animace: globalni ZAP/VYP */
            /* ⚠️ tap_flash az PO precteni stavu by nesvitilo pri VYP->ZAP prechodu
             * (gate g_anim_enabled=0). Flashni PRED zmenou stavu podle STAREHO. */
            tap_flash(ANIM_TOGGLE_RECT);
            g_anim_enabled = g_anim_enabled ? 0 : 1;
            g_sys_cfg_dirty = 1;
            prim_set_target(&s_fb);
            prim_reset_clip();
            anim_toggle_redraw();
            present_now();
            return true;
        }
        if (s_view == 24 && in_rect(x, y, ANIM_DEMO_RECT)) {   /* -> subokno prikladu vsech animaci */
            nav_push(24);
            app_gpsdo_render_animdemo();
            return true;
        }
        if (s_view == 24 && in_rect(x, y, ANIM_FX_RECT)) {     /* -> prepinace grafickych efektu */
            nav_push(24);
            app_gpsdo_render_efekty();
            return true;
        }
        if (s_view == 27) {                                    /* EFEKTY: prepni bit efektu */
            for (int i = 0; i < 6; i++)
                if (in_rect(x, y, FX_ITEMS[i].rect)) {
                    g_fx_enabled ^= FX_ITEMS[i].bit;           /* persist syscfg flash (debounced) */
                    prim_set_target(&s_fb); prim_reset_clip();
                    fx_btn_render(i);
                    present_now();
                    return true;
                }
        }
        if (s_view == 12) {                                /* Menu rozcestnik: dlazdice + Restart */
            if (in_rect(x, y, MENU_RESTART_RECT)) {
                app_gpsdo_render_confirm_restart();        /* potvrzeni (bez nav_push) */
                return true;
            }
            for (int i = 0; i < MENU_N; i++)
                if (in_rect(x, y, MENU_ITEMS[i].rect)) {
                    /* Vsech 12 dlazdic naviguje -> vzdy pushni Menu na zasobnik
                     * (BACK z otevreneho okna vede zpet do Menu). */
                    nav_push(12);
                    menu_activate(MENU_ITEMS[i].act);
                    return true;
                }
        }
        if (s_view == 13) {                                /* potvrzeni restartu = MODAL */
            if (in_rect(x, y, CONFIRM_YES)) { g_reboot_req = 1; return true; }   /* Ano -> defaultTask reset */
            if (in_rect(x, y, CONFIRM_NO))  { app_gpsdo_render_menu(); return true; }  /* Ne -> zpet do Menu */
            /* ⚠️ Modal: vsechny ostatni tapy polykame. Bez tohohle returnu se
             * propadly az na spolecny BACK_RECT nize — a ten je v tomhle okne
             * NEVIDITELNY (dialog nekresli window_chrome), takze vpravo dole byl
             * slepy hotspot, ktery dialog zrusil. Zavrit ho jde jen pres NE. */
            return false;
        }
        if (s_view == 9) {                                 /* trend: relativni +/- casove okno */
            int step = 0;
            if (in_rect(x, y, TREND_MINUS)) step = -1;
            else if (in_rect(x, y, TREND_PLUS)) step = +1;
            if (step) {
                tap_flash(step < 0 ? TREND_MINUS : TREND_PLUS); /* render_trend_scale_btns nize prekresli tlacitka -> bez ducha */
                trend_secs_step(step);
                prim_set_target(&s_fb); prim_reset_clip();
                render_trend_scale_btns();                 /* prekresli hodnotu okna */
                screen_main_render_trend_big((prim_rect_t){(int16_t)(DG_LX + 8), 96,
                                                           (int16_t)(764 - 16), 300});
                present_now();
                return true;
            }
        }
        if (s_view == 6 && in_rect(x, y, LOGY_RECT)) {     /* histogram: prepni lin/log Y */
            screen_main_hist_toggle_logy();
            app_gpsdo_render_histogram();   /* zmena osy zmeni change-key -> prekresli */
            return true;
        }
        /* Sdilene zalozky ALLAN/HIST/SPEKTR (bez nav_push — BACK z libovolneho
         * vede tam, odkud byla trojice otevrena, ne k sobe navzajem). */
        if ((s_view == 23 || s_view == 6 || s_view == 26) && in_rect(x, y, VIEW_TABS_RECT)) {
            int active = (s_view == 23) ? 0 : (s_view == 6) ? 1 : 2;
            ui_segmented_t tb = {.rect = VIEW_TABS_RECT, .labels = VIEW_TAB_LABELS,
                                 .n = 3, .selected = (uint8_t)active};
            int seg = ui_segmented_hit(&tb, x, y);
            if (seg >= 0 && seg != active) {
                if (seg == 0)      app_gpsdo_render_allan();
                else if (seg == 1) app_gpsdo_render_histogram();
                else               app_gpsdo_render_waterfall();
            }
            return true;
        }
        if (s_view == 23 && in_rect(x, y, ALLAN_METRIC_RECT)) {   /* prepinac ADEV/TDEV/MTIE */
            ui_segmented_t sc = {.rect = ALLAN_METRIC_RECT, .labels = ALLAN_METRIC_SEG,
                                 .n = 3, .selected = (uint8_t)screen_main_allan_metric()};
            int seg = ui_segmented_hit(&sc, x, y);
            if (seg >= 0 && seg != screen_main_allan_metric()) {
                screen_main_set_allan_metric(seg);
                prim_set_target(&s_fb); prim_reset_clip();
                allan_metric_render();                          /* prekresli prepinac */
                screen_main_render_allan_big(HIST_PLOT_RECT);   /* graf pro novou metriku */
                present_now();
            }
            return true;
        }
        if (s_view == 20 && in_rect(x, y, ST_RUN_RECT)) {  /* Selftest: spustit znovu */
            run_selftests();                /* pure-logic (~ms), bezpecne z UiTasku */
            app_gpsdo_render_selftest();    /* prekresli per-test vysledky */
            return true;
        }
        if (s_view == 22) {                                /* Cas: AUTO/RUCNI + posun -/+ */
            #define CAS_UPD() do { prim_set_target(&s_fb); prim_reset_clip(); \
                                   cas_upd_mode(); present_now(); } while (0)
            if (in_rect(x, y, TZ_AUTO_RECT)) {
                tap_flash(TZ_AUTO_RECT); /* cas_upd_mode nize prekresli tlacitko -> bez ducha */
                g_tz_auto = g_tz_auto ? 0 : 1;
                g_sys_cfg_dirty = 1;
                CAS_UPD();
                return true;
            }
            if (in_rect(x, y, TZ_MINUS)) { tz_step(-1); CAS_UPD(); return true; }
            if (in_rect(x, y, TZ_PLUS))  { tz_step(+1); CAS_UPD(); return true; }
            #undef CAS_UPD
        }
        if (s_view == 17 && in_rect(x, y, DL_TOGGLE_RECT)) {   /* Datalog: ZAPNOUT/VYPNOUT */
            datalog_set_enabled(!datalog_enabled());
            /* Persist resi syscfg_flash_tick sam: jeho shadow-diff porovnava CELY
             * blob (vcetne datalog_en), takze zmenu zachyti bez explicitniho
             * dirty priznaku. BKP (DR2/DR6) datalog_en nenese — je jen ve flash. */
            /* Meni se i tlacitko + popisky -> vynut PLNY render okna. window_first()
             * pozna "prvni vstup" podle zmeny s_view, takze ho docasne zneplatnime. */
            s_view = -1;
            app_gpsdo_render_datalog();
            return true;
        }
        if (s_view == 15) {                                /* Kalibrace: -/+ na 4 radcich + ULOZIT */
            for (int i = 0; i < 4; i++) {
                int16_t yy = KALIB_ROWS[i].y;
                /* _hit (ne _rect): vizual zustava 50x34, dotykovy cil je vetsi. */
                if (in_rect(x, y, kalib_minus_hit(yy))) { kalib_step(i, -1); return true; }
                if (in_rect(x, y, kalib_plus_hit(yy)))  { kalib_step(i, +1); return true; }
            }
            if (in_rect(x, y, KALIB_SAVE_RECT)) {
                /* calib_save() = erase+zapis sektoru W25Q, blokuje ~stovky ms
                 * a po tu dobu UiTask nekresli. Bez odezvy to vypada jako
                 * "tlacitko nereaguje" a uzivatel tapne znovu -> proto NEJDRIV
                 * vykreslit "Ukladam..." a FLIPNOUT, teprve pak zapisovat. */
                prim_set_target(&s_fb);
                prim_reset_clip();
                kalib_status_redraw("Ukladam do W25Q...", UI_COLOR_WARN);
                /* ⚠️ item 6 (spinner): calib_save() nize je JEDNO blokujici
                 * volani (w25q_store_write = erase+payload+hlavicka, viz
                 * w25q_store.c) bez zadneho yield bodu, ktery by šel odsud
                 * vyuzit — UiTask je pak jednovlaknove zaseknuty uvnitr
                 * calib_save() az do navratu, takze SKUTECNY vicesnimkovy
                 * spin behem zapisu neni mozny bez zasahu do sdileneho (a
                 * power-safety kriticky serazeneho) w25q_store. Misto
                 * predstirane animace je tu aspon staticka ikona (otoceni
                 * se meni mezi jednotlivymi stisky ULOZIT, ne behem jednoho). */
                int16_t tw = prim_text_width("Ukladam do W25Q...", &ui_font_sans_16);
                s_kalib_spin_frame++;
                int16_t ang = (int16_t)((s_kalib_spin_frame * 47u) % 360u);
                prim_draw_arc((prim_point_t){(int16_t)(DG_LLBL + tw + 20), 396}, 8, 3,
                             UI_COLOR_WARN, ang, 270);
                present_now();
                bool ok = calib_save();
                /* dtext uvnitr kalib_status_redraw si oblast nejdriv vyplni
                 * (fill_rect) -> mark_dirty sedi a copy-forward pres 3 buffery
                 * je v poradku i pro tenhle druhy, castecny redraw (smaze i
                 * spinner ikonu, ktera lezi uvnitr stejneho clear boxu). */
                kalib_status_redraw(ok ? "Ulozeno do W25Q." : "Chyba zapisu do flash!",
                                    ok ? UI_COLOR_OK : UI_COLOR_BAD);
                present_now();
                return true;
            }
            if (in_rect(x, y, KALIB_AUTOCAL_RECT)) {   /* self-check referenci/napajeni */
                autocal_run();
                prim_set_target(&s_fb); prim_reset_clip();
                ac_result_t w = AC_PASS;
                ac_result_t ch[4] = { g_autocal.vref, g_autocal.rail12, g_autocal.rail5, g_autocal.vbat };
                for (int i = 0; i < 4; i++) if (ch[i] > w && ch[i] != AC_NA) w = ch[i];
                prim_color_t col = (w == AC_FAIL) ? UI_COLOR_BAD : (w == AC_WARN) ? UI_COLOR_WARN : UI_COLOR_OK;
                kalib_status_redraw(autocal_summary(), col);
                present_now();
                return true;
            }
        }
        if (in_rect(x, y, BACK_RECT)) { nav_back(); return true; }   /* zpet k tomu, odkud otevreno */
    }
    return false;
}
