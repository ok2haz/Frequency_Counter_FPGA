/**
 * @file screen_main.c
 * @brief GPSDO main screen layout (libui composition).
 *
 * screen_main_init() pre-renders the static background (radial gradient + header
 * chrome) into a cache and is allowed direct libprim access for that boot-time
 * work. screen_main_render() composes ui_* components and renders the big number
 * directly over the background (no opaque cache block → no seam, no crop).
 *
 * All buffers are RGB565; libprim composites in ARGB and packs to RGB565.
 */

#include "screen_main.h"
#include "../anim.h"  /* anim_t/anim_reset/anim_set/anim_step — sdileno s app_gpsdo.c */
#include <ui/ui.h>
#include "sensor_stat.h"   /* g_sensors[] — agregace chyb do SYS pilulky */
#include "alarm.h"         /* g_mon_*_bad — prahovy monitor v SYS pilulce */
#include "freertos_shared.h"  /* g_freq_x100000/seq/valid/stale — realny kmitocet z FpgaTasku (#1) */
#include "fx_flags.h"      /* g_fx_enabled + FX_* — graficke efekty (SYS xfade, glow, spark fill, allan conf) */
#include "phase_noise.h"   /* pn_compute — L(f) fazovy sum z ringu s_y[] (#45) */
#include "../hal/stm32/prim_stm32_hal.h"  /* prim_stm32_fb_count — guardy „nekresli, je to stejne" */
#include "fpga_freq.h"     /* fpga_freq_hires_mul — overeni nasobitele reciproke dvojice */
#include <prim/prim.h>
#include "gps.h"     /* gps_get() — zive GNSS lock / pocet druzic / cas+datum v headeru */
#include <stdio.h>   /* snprintf pro cas/datum */
#include <string.h>  /* strncpy */
#include <math.h>    /* sqrtf/log10f/fabsf/powf/ceilf/floorf — GPSDO statistika (cold path, 1/s) */

/* RTC cas (defaultTask zapise pres rtc_app_tick) — hodiny v headeru z RTC, ne
 * GPS-direct: tikaji plynule i pri ztrate fixu (RTC bezi z LSE). */
extern volatile char    g_rtc_text[24];   /* "YYYY-MM-DD HH:MM:SS" */
extern volatile uint8_t g_sound_muted;    /* 1 = mute -> ikona v headeru */
/* Zdroje pro barvu SYS pilulky (agregace vsech chyb). */
extern volatile uint8_t  g_spi_ok;         /* FPGA link */
extern volatile uint8_t  g_freq_stale;     /* FPGA SIGNAL_LOST / mrtvy link */
extern volatile uint8_t  g_si5356_ok;      /* Si5356 status precten */
extern volatile uint8_t  g_si5356_status;  /* reg218: bit0 SYS_CAL, bit2 LOS_XTAL, bit3 LOS_CLKIN, bit4 PLL_LOL (AN565) */
extern volatile uint8_t  g_selftest_res;   /* 0=--- 1=PASS 2=FAIL */
extern volatile uint8_t  g_reset_bad;      /* 1 = posledni reset = watchdog/crash */
extern volatile uint8_t  g_cm4_absent;     /* 1 = CM4 (D2) nenabehl -> degradovane */
extern volatile uint8_t  g_cm4_alive;      /* 1 = CM4 heartbeat v IPC roste (bezi + mluvi) */
extern volatile uint8_t  g_cm4_cpu_pct;    /* CM4 vlastni zatez [%] z IPC heartbeatu -> "CM4:xx%" */
extern volatile uint32_t g_rtos_cpu_pct;   /* CM7 CPU vytizeni [%] (pocita UiTask) — header */
extern volatile uint8_t g_rtc_synced;     /* 1 = uz srovnano z GPS */
extern volatile uint8_t g_anim_enabled;   /* 1 = animace zapnute (okno Animace) */
extern volatile char    g_rtc_text_local[24];  /* cas v lokalni zone (rtc_app_tick) */
extern volatile char    g_tz_label[8];         /* "UTC" / "UTC+2" (label zony k datu) */

/* Ulozene UI nastaveni (persist v RTC BKP): nacte se jednou v screen_main_init,
 * pakuje se pri zmene tlacitka; defaultTask ho zapise do BKP. bit0 mode / bit1
 * chan / bity2:3 gate / bit4 running. */
extern volatile uint8_t g_ui_cfg;
extern volatile uint8_t g_ui_cfg_dirty;
/* Dalkovy SET (SCPI z UartTasku) -> aplikuje `screen_main_apply_cfg_req` v UiTasku.
 * Definice ve freertos.c, popis v freertos_shared.h (ten se sem zamerne neincluduje —
 * app vrstva si drzi jen tenhle uzky extern kontrakt, jako u ostatnich globalu). */
extern volatile uint8_t g_ui_cfg_req;
extern volatile uint8_t g_ui_cfg_req_pend;

/* Vytahne cas "HH:MM:SS" a datum "YYYY-MM-DD" z g_rtc_text_local (cas v
 * ZVOLENE casove zone z Nastaveni; UTC kdyz je zona 0 — label zony k tomu dava
 * g_tz_label). Dokud nebyl RTC srovnan z GPS, vraci placeholdery
 * "--:--:--" / "no GPS". time8/date10 = char[16]. */
static void rtc_time_date(char *time8, char *date10)
{
    char rt[24];
    strncpy(rt, (const char *)g_rtc_text_local, sizeof rt - 1); rt[sizeof rt - 1] = '\0';
    if (g_rtc_synced && strlen(rt) >= 19) {
        snprintf(time8,  16, "%.8s",  rt + 11);   /* "HH:MM:SS" */
        snprintf(date10, 16, "%.10s", rt);        /* "YYYY-MM-DD" */
    } else {
        snprintf(time8,  16, "--:--:--");
        snprintf(date10, 16, "no GPS");
    }
}

#ifndef SCR_SDRAM_SECTION
#  if defined(__GNUC__) && !defined(PRIM_HOST_BUILD)
#    define SCR_SDRAM_SECTION __attribute__((section(".sdram"), aligned(32)))
#  else
#    define SCR_SDRAM_SECTION
#  endif
#endif

static prim_pixel_t bg_cache[SCR_MAIN_BG_CACHE_W * SCR_MAIN_BG_CACHE_H] SCR_SDRAM_SECTION;
static bool cache_initialized = false;

/* Simulovany cas HH:MM:SS (aktualizuje screen_main_redraw_time z uptime). */
static char s_time_buf[16] = "14:32:07";

/* Footer button hit areas, set during footer render. Index: 0=PERIOD/FREQ
 * toggle, 1=RUN/STOP, 2=GATE, 3=CHAN, 4=MENU. */
#define SCR_BTN_COUNT 5
static prim_rect_t s_btn_rect[SCR_BTN_COUNT];

/* Interactive UI state (iteration-1: drives labels/title, no live measurement). */
static const char *MODE_NAME[2] = {"FREQUENCY", "PERIOD"};
static const char *CHAN_NAME[2] = {"CH A", "CH B"};
static const char *GATE_VAL[4]  = {"0,1 s", "1 s", "10 s", "100 s"};
/* Tataz sada v sekundach — pro rozpocet nejistoty (rozliseni ~ tdc/gate).
 * ⚠️ Drzet SYNCHRONNI s GATE_VAL: popisek a hodnota musi rikat totez. */
static const float GATE_SEC[4] = {0.1f, 1.0f, 10.0f, 100.0f};
static struct { int8_t mode; int8_t chan; int8_t gate; bool running; }
    st = {0, 1, 1, true};    /* FREQUENCY, CH B, 1 s, RUNNING po bootu (tlacitko "STOP") */
static uint8_t s_disp_recalc = 0;   /* 1 = prepnul se FREQ/PERIOD -> vynut rebuild formatu velkeho cisla */

const prim_pixel_t *screen_main_bg(void) { return bg_cache; }

/* RUN/STOP: ridi, zda bezi mereni (kmitocet, bargraf, statistika). */
double screen_main_gate_seconds(void) { return (double)GATE_SEC[st.gate & 3]; }

bool screen_main_is_running(void) { return st.running; }

/* ════════════════════════════════════════════════════════════════════════
 * DVA ROZLOZENI HLAVNI OBRAZOVKY (vraceno 2026-08-23 na prani uzivatele)
 *
 * HYBRIDNI (vychozi, `classic == false`) — ladene pro 4,3" panel: Allan zabira
 *   47 % sirky, ale CELOU vysku mrizky; vpravo tri karty statistiky s hodnotami
 *   v mono_18, pod nimi trend a dole RF bargraf (v.54). Cisla jsou vetsi a lip
 *   citelna z 30-35 cm.
 * KLASICKE (`classic == true`) — puvodni rozlozeni pred auditem pro 4,3": Allan
 *   53 % sirky, pravy sloupec je stohovany offset(v.54, mono_16) / trend /
 *   signal(v.43), vsechny mezery `SCR_MAIN_CARD_SECTION_GAP`. Vejde se vic
 *   grafu, ale cisla jsou mensi.
 *
 * ⚠️ Klasicke rozlozeni je ZAMRZLA vetev: nema easing statistik ani trendu
 * (tiky `screen_main_tick_stats_anim`/`_trend_anim` v nem hned vraci 0) a
 * zamerne se v nem uz nedelaji zmeny — kazda dalsi uprava hlavni obrazovky
 * miri do hybridniho. Historie: A/B vetev existovala uz 2026-07-19 (TODO #14),
 * 2026-08-22 byla odstranena ve prospech hybridniho a ted je vracena zpet jako
 * TRVALA volba uzivatele (prepinac v okne DISPLEJ, persist v syscfg).
 * Puvodne se prepinalo footer tlacitkem, ktere ale prekryvalo PERIOD/FREQ —
 * proto je prepinac ted v Nastaveni a footer si nechava svou funkci. */
static bool s_layout_classic = false;
void screen_main_set_layout_classic(int on)
{
    s_layout_classic = (on != 0);
}
int  screen_main_layout_is_classic(void) { return s_layout_classic ? 1 : 0; }

/* Hlavickove pilulky — rect zachyceny pri render_header; tap -> okno. */
static prim_rect_t s_gnss_pill_rect = {0, 0, 0, 0};  /* GNSS lock -> GPS okno */
static prim_rect_t s_sys_pill_rect  = {0, 0, 0, 0};  /* SYS ready -> System Health */

static bool pt_in(int16_t x, int16_t y, prim_rect_t r)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* ── Hit-slop pilulek ────────────────────────────────────────────────────────
 * Panel je 4,3" 800x480 => 8,54 px/mm (217 DPI). Pilulka je vizualne vysoka
 * UI_DIM_PILL_H = 46 px = 5,4 mm (2026-07-19: 30 -> 36 -> 42 -> 46) — porad pod
 * doporucenymi ~7 mm pro prst, a pritom je to JEDINA cesta z hlavni obrazovky
 * do GPS okna / System Health. Proto se navic rozsiruje TESTOVACI obdelnik:
 *   - svisle az k okrajum hlavicky (0..UI_DIM_HEADER_H) -> 46 px => ~52 px
 *     (6,1 mm, clampnuto pt_in_pill); vic nejde, nize uz zacina telo obrazovky
 *     a kradli bychom mu tapy,
 *   - vodorovne jen o POLOVINU mezery mezi pilulkami (UI_DIM_PILL_GAP/2), aby si
 *     sousedni pilulky (GNSS | SYS | SAT | HDOP jsou v jedne rade) nekradly doteky. */
#define PILL_SLOP_X  (UI_DIM_PILL_GAP / 2)
#define PILL_SLOP_Y  11

static bool pt_in_pill(int16_t x, int16_t y, prim_rect_t r)
{
    if (r.w == 0) return false;
    int16_t y0 = (int16_t)(r.y - PILL_SLOP_Y);
    int16_t y1 = (int16_t)(r.y + r.h + PILL_SLOP_Y);
    if (y0 < 2) y0 = 2;
    if (y1 > UI_DIM_HEADER_H - 2) y1 = UI_DIM_HEADER_H - 2;
    return x >= r.x - PILL_SLOP_X && x < r.x + r.w + PILL_SLOP_X
        && y >= y0 && y < y1;
}

bool screen_main_hit_gnss(int16_t x, int16_t y)
{
    return pt_in_pill(x, y, s_gnss_pill_rect);
}

bool screen_main_hit_sys(int16_t x, int16_t y)
{
    return pt_in_pill(x, y, s_sys_pill_rect);
}

/* Agregace zdravi systemu do SYS pilulky: 0=OK(zelena) 1=warn(amber) 2=chyba(cervena).
 * Cerpa ze VSECH chybovych zdroju. AMBER = degradace (funguje), RED = kriticke. */
/* Si5356 reg 218 — bitova mapa OVERENA z AN565 (drivejsi bit2=LOS_CLKIN byla
 * chyba; bit2 je LOS_XTAL = krystal, ktery na desce NENI osazen -> trvale 1,
 * benigni, IGNORUJE se). Skutecnou ztratu 10 MHz na CLKIN hlasi bit3 — a POZOR,
 * PLL_LOL se pri fyzicke ztrate vstupu NEasertuje (AN565: LOL = rozdil >5000 ppm
 * na PFD, ne odpojeny vstup) -> LOS_CLKIN je HLAVNI indikator ztraty reference. */
#define SI_SYS_CAL   0x01u
#define SI_LOS_XTAL  0x04u   /* bit2: bez krystalu trvale 1 — nehodnotit */
#define SI_LOS_CLKIN 0x08u   /* bit3: ztrata 10 MHz na CLKIN (pin 4) */
#define SI_PLL_LOL   0x10u
static int compute_sys_level(void)
{
    int lvl = 0;
    /* AMBER: degradovane, ale funguje */
    if (g_freq_stale || !g_spi_ok) lvl = 1;               /* FPGA SIGNAL_LOST / mrtvy link */
    if (!g_si5356_ok || (g_si5356_status & SI_SYS_CAL)) lvl = 1;  /* ref necteno / kalibruje */
    if (g_reset_bad) lvl = 1;                             /* watchdog/crash reset (zotaveno) */
    if (g_cm4_absent) lvl = 1;                            /* CM4 (D2) nenabehl -> degradovane */
    for (int i = 0; i < SENS_COUNT; i++)
        if (i != (int)SENS_T4A && g_sensors[i].err_streak > 0) { lvl = 1; break; }  /* 0x4A neosazen */
    /* Prahovy monitor (2026-08-17): VBAT na konci zivota, OCXO mimo teplotni
     * pasmo, σy@1s nad prahem. Vse AMBER = "neco se kazi, ale pristroj funguje" —
     * RED zustava vyhrazena ztrate reference a selftest FAILu, tedy stavum, kdy
     * pristroj bud nemeri, nebo mere spatne a nevi o tom. */
    if (g_mon_vbat_bad || g_mon_ocxo_bad || g_mon_adev_bad) lvl = 1;
    /* RED: kriticke (prebiji). LOS_CLKIN (bit3) = ztrata 10 MHz reference — LOL se
     * pri fyzicke ztrate vstupu NEasertuje (viz komentar u SI_* vyse), takze LOS je
     * tady nutny. SI_LOS_XTAL (bit2) se zamerne NEhodnoti (bez krystalu trvale 1). */
    if (g_si5356_ok && (g_si5356_status & (SI_LOS_CLKIN | SI_PLL_LOL))) lvl = 2;
    if (g_selftest_res == 2) lvl = 2;                     /* selftest FAIL */
    return lvl;
}

static int s_sys_level = -1;   /* posledni vykreslena uroven (pro poll zmeny) */
/* Efekt FX_SYS_XFADE: plynule prolinani barvy SYS pilulky pri zmene urovne.
 * s_sys_mix 0 = barva urovne s_sys_from_level, 1 = barva s_sys_level (usazeno). */
static int   s_sys_from_level = -1;
static float s_sys_mix        = 1.0f;
#define SYS_XFADE_STEP 0.14f   /* ~7 tiku @20 Hz -> ~0,35 s */

/* Vrati 1 pokud se uroven SYS zdravi zmenila od posledniho render_header -> volajici
 * (UiTask na hl. obrazovce) pak zavola screen_main_redraw_header. */
int screen_main_sys_poll(void)
{
    return compute_sys_level() != s_sys_level;
}

int screen_main_hit_button(int16_t x, int16_t y)
{
    for (int i = 0; i < SCR_BTN_COUNT; i++)
        if (pt_in(x, y, s_btn_rect[i])) return i;
    return -1;
}

void screen_main_button_action(int idx)
{
    switch (idx) {
    case 0: st.mode = (int8_t)(st.mode ? 0 : 1);          /* FREQ <-> PERIOD */
            s_disp_recalc = 1; break;                     /* prepocitej velke cislo (perioda 1/f) */
    case 1: st.running = !st.running;            break;   /* RUN <-> STOP */
    case 2: st.gate = (int8_t)((st.gate + 1) % 4); break; /* cycle gate */
    case 3: st.chan = (int8_t)(st.chan ? 0 : 1); break;   /* CH A <-> CH B */
    default: return;                                      /* 4 = MENU: nic k ulozeni */
    }
    /* Zapamatuj nastaveni -> defaultTask ho persistne do BKP (prezije warm reset). */
    g_ui_cfg = (uint8_t)((st.mode & 1) | ((st.chan & 1) << 1)
                         | ((st.gate & 3) << 2) | ((st.running ? 1 : 0) << 4));
    g_ui_cfg_dirty = 1;
}

/* ── Dalkove nastaveni stavu mereni (SCPI) ──────────────────────────────────
 * ⚠️ VLAKNA: `st` vlastni VYHRADNE UiTask. SCPI bezi v UartTasku, takze zapise
 * jen pozadavek (`g_ui_cfg_req`) a TAHLE funkce ho aplikuje — vola ji UiTask ze
 * sveho tiku. Vraci 1, kdyz se neco zmenilo (volajici pak prekresli footer).
 * Gettery nize ctou `st` bez zamku: jsou to jednotlive int8/bool (atomicke na
 * ARM) a SCPI je jen zobrazuje, takze pripadne zachyceni "pulky zmeny" nevadi. */
int screen_main_apply_cfg_req(void)
{
    if (!g_ui_cfg_req_pend) return 0;
    uint8_t c = g_ui_cfg_req;
    g_ui_cfg_req_pend = 0;

    int8_t mode = (int8_t)( c        & 1);
    int8_t chan = (int8_t)((c >> 1)  & 1);
    int8_t gate = (int8_t)((c >> 2)  & 3);
    bool   run  = ((c >> 4) & 1) != 0;
    if (mode == st.mode && chan == st.chan && gate == st.gate && run == st.running)
        return 0;                                  /* nic noveho -> zadny redraw */
    if (mode != st.mode) s_disp_recalc = 1;         /* FREQ<->PERIOD -> prepocet velkeho cisla */
    st.mode = mode; st.chan = chan; st.gate = gate; st.running = run;
    g_ui_cfg = c; g_ui_cfg_dirty = 1;              /* persist do BKP (jako z UI) */
    return 1;
}

/* ── Background pre-render (boot) ───────────────────────────── */

static void render_background_to_cache(void)
{
    prim_fb_t cache_fb;
    prim_fb_init(&cache_fb, bg_cache, SCR_MAIN_BG_CACHE_W, SCR_MAIN_BG_CACHE_H,
                 SCR_MAIN_BG_CACHE_W * sizeof(prim_pixel_t));
    prim_fb_t *prev = prim_get_target();
    prim_set_target(&cache_fb);

    prim_fill_gradient_radial(
        (prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
        (prim_point_t){UI_DIM_SCREEN_W / 2, UI_DIM_SCREEN_H / 2},
        0, 540, UI_COLOR_BG_1, UI_COLOR_BG_0);

    prim_fill_gradient_linear((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_HEADER_H},
                              UI_COLOR_BG_HEADER_TOP, UI_COLOR_BG_HEADER_BOT,
                              PRIM_GRAD_VERTICAL);
    prim_draw_line((prim_point_t){0, UI_DIM_HEADER_H - 1},
                   (prim_point_t){UI_DIM_SCREEN_W, UI_DIM_HEADER_H - 1},
                   1, UI_COLOR_LINE);
    prim_draw_line((prim_point_t){0, UI_DIM_SCREEN_H - UI_DIM_FOOTER_H},
                   (prim_point_t){UI_DIM_SCREEN_W, UI_DIM_SCREEN_H - UI_DIM_FOOTER_H},
                   1, UI_COLOR_LINE);

    prim_set_target(prev);
}

static void trend_drawn_invalidate(void);   /* fwd — definice u trend animace nize */

/* ⚠️ Krome bg_cache se zneplatnuje i guard trend grafu — po zmene tematu je
 * bg_cache i paleta jina, takze "stejne body" uz NEznamenaji stejnou kresbu. */
void screen_main_invalidate(void) { cache_initialized = false; trend_drawn_invalidate(); }

void screen_main_init(void)
{
    /* Jednou po bootu: aplikuj ulozene nastaveni (g_ui_cfg nacetl MX_RTC_Init z BKP
     * pred schedulerem). Guard je nezavisly na cache -> screen_main_invalidate
     * (reset cache) uz nastaveni znovu neprepise. */
    static bool s_cfg_loaded = false;
    if (!s_cfg_loaded) {
        s_cfg_loaded = true;
        uint8_t c   = g_ui_cfg;
        st.mode     = (int8_t)( c        & 1);
        st.chan     = (int8_t)((c >> 1)  & 1);
        st.gate     = (int8_t)((c >> 2)  & 3);
        st.running  = ((c >> 4) & 1) != 0;
    }
    if (cache_initialized) return;
    render_background_to_cache();
    cache_initialized = true;
}

/* ── Per-frame composition ──────────────────────────────────── */

static int16_t draw_word(int16_t x, int16_t y, const char *text,
                         const prim_font_t *font, prim_color_t color)
{
    prim_draw_text((prim_point_t){x, y}, text, font, color, PRIM_ALIGN_LEFT);
    return (int16_t)(x + prim_text_width(text, font));
}

/* Header: only the pills that fit (no overflow into the clock area).
 * HDR_PILL_LIMIT = zacatek "zony hodin": clear obdelnik sekundoveho redrawu
 * casu zacina na x=648 (time_x 794 - hodiny 120 - 26 na mute ikonu) a datovy
 * na x=644 (time_x-150) — a ten s pilulkami vysky 46 koliduje i SVISLE (pas
 * y 35..53 vs pilulky 5..51). Pilulka nesmi zasahnout ani do jedne zony,
 * jinak ji kazdy tik hodin/zmena data oreze. 640 = 644 - 4 px rezerva.
 * ⚠️ Fit-check byl komentarem vyse SLIBOVANY odjakziva, ale kod ho nedelal
 * (nalezeno revizi 2026-07-19): pri soubehu dlouhych stavu ("GNSS FIX" +
 * "SYS ERR") rada pretekala do clear zon. Pilulky jdou v poradi dulezitosti
 * -> pri pretlaku vypadne POSLEDNI (HOLD; v jedinem pretekajicim scenari
 * stejne nemuze byt holdover aktivni — "GNSS FIX" = zivy fix).
 * ⚠️ 2026-07-25 snizeno 640->590: mezi pilulky a hodiny pribyl dvouradkovy blok
 * vytizeni CPU (CM7/CM4, viz screen_main_redraw_cpu, x 592..640). Rezerva mensi
 * -> v nejhorsim souběhu dlouhych stavu vypadne CAL (staticky placeholder). */
#define HDR_PILL_LIMIT 580   /* 590 -> 580 (2026-08-15): CPU blok se rozsiril kvuli
                              * popiskum "CM7:"/"CM4:" misto "7:"/"4:" (56 px textu). */

/* Vykresli pilulku jen kdyz se CELA vejde pred HDR_PILL_LIMIT; pri vykresleni
 * posune x o sirku+GAP. Vraci 1 = vykresleno (volajici smi zachytit rect). */
static int hdr_pill_fit(ui_pill_t *p, int16_t *x)
{
    if ((int16_t)(*x + ui_pill_measure(p)) > HDR_PILL_LIMIT) return 0;
    p->x = *x;
    ui_pill_render(p);
    *x = (int16_t)(*x + p->computed_width + UI_DIM_PILL_GAP);
    return 1;
}

/* Linearni interpolace dvou RGB barev (t: 0=a, 1=b). Pro cross-fade SYS pilulky. */
static prim_color_t color_lerp(prim_color_t a, prim_color_t b, float t)
{
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    int ra = PRIM_R(a), ga = PRIM_G(a), ba = PRIM_B(a);
    return PRIM_RGB((uint8_t)(ra + (PRIM_R(b) - ra) * t),
                    (uint8_t)(ga + (PRIM_G(b) - ga) * t),
                    (uint8_t)(ba + (PRIM_B(b) - ba) * t));
}

/* SYS pilulka: barva urovne. Pri behu cross-fade (s_sys_mix<1) se barvy prolnou
 * z urovne s_sys_from_level do s_sys_level; text uz je cilovy (usadi ho plny
 * render_header). x nastavi volajici (hdr_pill_fit / tick). */
static ui_pill_variant_t sys_variant(int level)
{
    return (level == 0) ? UI_PILL_OK : (level == 1) ? UI_PILL_WARN : UI_PILL_BAD;
}
static void sys_pill_setup(ui_pill_t *p, int16_t y)
{
    const char *sysl = (s_sys_level == 0) ? "SYS OK"
                     : (s_sys_level == 1) ? "SYS !" : "SYS ERR";
    prim_color_t bg0, bd0, vl0, bg1, bd1, vl1;
    ui_pill_variant_colors(sys_variant(s_sys_from_level), &bg0, &bd0, &vl0);
    ui_pill_variant_colors(sys_variant(s_sys_level),      &bg1, &bd1, &vl1);
    float t = s_sys_mix;
    *p = (ui_pill_t){.y = y, .value = sysl, .override_style = true,
                     .ovr_bg     = color_lerp(bg0, bg1, t),
                     .ovr_border = color_lerp(bd0, bd1, t),
                     .ovr_value  = color_lerp(vl0, vl1, t)};
}

static void render_header(void)
{
    int16_t x = SCR_MAIN_HEADER_X;
    int16_t y = (UI_DIM_HEADER_H - UI_DIM_PILL_H) / 2;
    ui_pill_t p;

    /* Zive GPS: GNSS lock pill + pocet druzic (SAT pill) + datum z GPS. */
    gps_data_t g;
    gps_get(&g);
    char sat_v[8], date_v[16], hdop_v[8];
    const char *gnss_s; ui_pill_variant_t gnss_var;
    if      (g.valid && g.fix_mode == 3) { gnss_s = "GNSS 3D";  gnss_var = UI_PILL_OK; }
    else if (g.fix_quality > 0)          { gnss_s = "GNSS FIX"; gnss_var = UI_PILL_OK; }
    else if (g.sats_in_view > 0)         { gnss_s = "ACQUIRE";  gnss_var = UI_PILL_WARN; }
    else                                 { gnss_s = "NO GNSS";  gnss_var = UI_PILL_BAD; }
    snprintf(sat_v, sizeof sat_v, "%u", g.num_sat);
    /* HDOP z GPS (GGA/GSA): 1 des. misto, ceska carka. Bez fixu "--".
     * Cap 99,9 (vyssi HDOP = nesmyslny fix; zaroven omezi rozsah pro snprintf). */
    if (g.fix_quality > 0 && g.hdop > 0.0f) {
        int h10 = (int)(g.hdop * 10.0f + 0.5f);
        if (h10 < 0) h10 = 0; else if (h10 > 999) h10 = 999;   /* bound [0,999] -> snprintf bezpecne */
        snprintf(hdop_v, sizeof hdop_v, "%d,%d", h10 / 10, h10 % 10);
    } else {
        snprintf(hdop_v, sizeof hdop_v, "--");
    }
    /* GNSS/SAT pilulky zustavaji z GPS (odrazi fix); datum bere RTC (tika i bez fixu). */
    { char tdummy[16]; rtc_time_date(tdummy, date_v); }   /* header chce jen datum */

    p = (ui_pill_t){.y = y, .variant = gnss_var,
                    .value = gnss_s, .has_led = true};
    if (hdr_pill_fit(&p, &x))   /* vzdy se vejde (prvni), rect pro tap -> GPS okno */
        s_gnss_pill_rect = (prim_rect_t){p.x, p.y, p.computed_width, UI_DIM_PILL_H};
    else
        s_gnss_pill_rect = (prim_rect_t){0, 0, 0, 0};

    /* SYS pilulka barevne dle agregovaneho zdravi; pri zmene urovne se barva
     * PLYNULE prolne (efekt FX_SYS_XFADE) misto skoku — text/layout se usadi hned,
     * prolina se jen barva (dokresli screen_main_tick_sys_xfade @20 Hz). */
    int new_sys = compute_sys_level();
    if (new_sys != s_sys_level) {
        if (s_sys_level >= 0 && (g_fx_enabled & FX_SYS_XFADE)) {
            s_sys_from_level = s_sys_level;   /* rozjed prolinani ze stare barvy */
            s_sys_mix = 0.0f;
        } else {
            s_sys_from_level = new_sys;       /* prvni render / efekt VYP -> hned cilova */
            s_sys_mix = 1.0f;
        }
        s_sys_level = new_sys;
    }
    sys_pill_setup(&p, y);
    if (hdr_pill_fit(&p, &x))   /* rect pro tap -> System Health */
        s_sys_pill_rect = (prim_rect_t){p.x, p.y, p.computed_width, UI_DIM_PILL_H};
    else
        s_sys_pill_rect = (prim_rect_t){0, 0, 0, 0};

    p = (ui_pill_t){.y = y, .variant = UI_PILL_NORMAL, .value = sat_v,
                    .icon_render = ui_icon_sat_dish, .icon_size = 22,
                    .icon_color = UI_COLOR_OK_SOFT};
    hdr_pill_fit(&p, &x);

    p = (ui_pill_t){.y = y, .variant = UI_PILL_NORMAL,
                    .label = SCR_S_HDOP_L, .value = hdop_v};   /* reálné HDOP z GPS */
    hdr_pill_fit(&p, &x);

    /* HOLD pilulka: AMBER pri holdoveru (fix ztracen pote, co uz nekdy byl) —
     * nahrazuje drivejsi zvlastni "H" u casu. HOLD je PRED CAL (dulezitejsi: nese
     * zivy holdover stav) -> pri pretlaku vypadne az CAL, HOLD se vzdy vejde. */
    int hold = (!g.valid && g.fixes > 0);
    p = (ui_pill_t){.y = y, .variant = hold ? UI_PILL_WARN : UI_PILL_NORMAL,
                    .label = SCR_S_HOLD_L, .value = SCR_S_HOLD_V};
    hdr_pill_fit(&p, &x);

    /* CAL: KOMPAKTNI "ribbon" chip (LED + "CAL", bez hodnoty) — placeholder
     * kalibracniho stavu. Uzsi nez drivejsi "CAL 4 min" pilulka (~67 vs ~90 px),
     * takze se za HOLD pred CPU blok (x=592) v typickem stavu vejde; v nejhorsim
     * pretlaku ho fit-check vynecha (posledni = nejmene dulezity, HOLD zustane). */
    p = (ui_pill_t){.y = y, .variant = UI_PILL_NORMAL, .value = "CAL", .has_led = true};
    hdr_pill_fit(&p, &x);

    int16_t time_x = UI_DIM_SCREEN_W - SCR_MAIN_CLOCK_MARGIN;
    prim_draw_text((prim_point_t){time_x, 23}, s_time_buf, &ui_font_mono_25,
                   UI_COLOR_INK, PRIM_ALIGN_RIGHT);
    /* Label zony ("UTC"/"UTC+2") na radek DATA (vpravo dole) — mimo pilulky. */
    char dutc[24]; snprintf(dutc, sizeof dutc, "%s %s", date_v, (const char *)g_tz_label);
    prim_draw_text((prim_point_t){time_x, 46}, dutc, &ui_font_sans_14,
                   UI_COLOR_INK_3, PRIM_ALIGN_RIGHT);
    screen_main_redraw_cpu(1);   /* blok vytizeni CPU mezi pilulkami a hodinami */
}

static void render_body_title(void)
{
    int16_t x = UI_DIM_PADDING_X + 4;
    int16_t y = SCR_MAIN_TITLE_Y;
    x = draw_word(x, y, MODE_NAME[st.mode], &ui_font_mono_22, UI_COLOR_ACC);
    x = draw_word(x, y, "  ·  ",            &ui_font_mono_22, UI_COLOR_INK_4);
    x = draw_word(x, y, CHAN_NAME[st.chan], &ui_font_mono_22, UI_COLOR_INK_2);
    x = draw_word(x, y, "  ·  ",            &ui_font_mono_22, UI_COLOR_INK_4);
    x = draw_word(x, y, "GATE ",            &ui_font_mono_22, UI_COLOR_INK_2);
    x = draw_word(x, y, GATE_VAL[st.gate],  &ui_font_mono_22, UI_COLOR_INK_2);
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W - UI_DIM_PADDING_X, y},
                   SCR_S_TITLE_RIGHT, &ui_font_mono_18, UI_COLOR_INK_3,
                   PRIM_ALIGN_RIGHT);
}

/* Velke cislo kmitoctu: hodnota je REALNE mereni z FPGA (nebo emulatoru), pri
 * chybejicim mereni simulace jako fallback — viz `freq_advance()`. Pocet mist
 * je dany magnitudou mereni (`num_layout`, zero-pad na sirku formatu).
 * Prekresleni je PER-SEGMENT DIRTY (screen_main_redraw_freq): prekresli jen
 * skupiny cislic, ktere se zmenily (resp. ocas od nich) -> stabilni cela cast se
 * neprekresluje. Drzime zive + predchozi cislice (shadow) + deskriptor. */
/* ⚠️ Kapacita 12 segmentu (drive 8): dynamicky format je deli jemneji — cela cast
 * az 4 skupiny + zlomek az 5 (trojice + osamocena posledni duveryhodna cislice
 * kvuli podtrzeni + 2 nejiste). Nejhorsi pripad 9; 12 je rezerva. */
#define NUM_SEG_MAX 12
static ui_digit_segment_t s_num_seg[NUM_SEG_MAX];
static char               s_num_buf[NUM_SEG_MAX][8];    /* aktualni cislice (zive) */
static char               s_num_prev[NUM_SEG_MAX][8];   /* cislice z minuleho snimku (per-segment dirty) */
static ui_big_number_t    s_num;
static int                s_num_ready = 0;
/* Cachovana geometrie cisla (monospace -> konstantni; spocte se v num_layout,
 * redraw_freq uz neprochazi prim_text_width kazdy snimek). ⚠️ Prepocitava se pri
 * KAZDE zmene formatu (jina magnituda mereni), ne jen jednou pri bootu. */
static int16_t            s_num_w, s_num_left, s_num_top;
static int16_t            s_seg_x[NUM_SEG_MAX];   /* x-pozice zacatku kazde skupiny cislic */
static uint8_t            s_seg_len[NUM_SEG_MAX]; /* delka textu kazde skupiny (cache misto strlen v hot-path) */

/* Stav kmitoctu (integer matematika, bez float). N = vsechny cislice jako jedno
 * cele cislo v LSB = 10^-`s_freq_frac` Hz; desetinna carka je az v zobrazeni. */
static uint64_t s_freq_n      = 0;   /* aktualni FREKVENCE v LSB = 10^-s_freq_frac Hz. VZDY frekvence
                                       (i v rezimu PERIODA) — cte ji statistika, SIM walk, freq_hz. */
static uint64_t s_freq_center = 0;   /* stred pasma FREKVENCE v LSB (rad mereni; NE fixne 10 MHz) */
/* Tentyz stred, ale v Hz — pro rekonstrukci z datalogu (ta pocita y z Hz, ne
 * z LSB). Drzime obe formy, at se nikde nedopocitava zpetne z `s_freq_center`
 * (pocet desetinnych mist je vlastnost formatovani, ne mereni). */
static double   s_freq_nominal_hz = 0.0;
static int      s_freq_frac   = 0;   /* desetinna mista FREKVENCE (LSB exponent pro s_freq_n) */
static int      s_freq_int    = 0;   /* cele cislice FREKVENCE (rebuild formatu jen pri zmene radu) */
/* ── ZOBRAZENI (footer PERIOD/FREQ toggle) ── `s_disp_n` je to, co se skutecne
 * kresli: v rezimu FREQUENCY = `s_freq_n`; v rezimu PERIOD = 1/f prepoctena do
 * ns/us/ms. Statistika/SIM/Math to NEvidi (pracuji dal s frekvenci). */
static uint64_t s_disp_n      = 0;   /* zobrazovane cislo (sirka = s_disp_total) */
static int      s_disp_total  = 0;   /* celkovy pocet cislic zobrazeneho formatu */
static int      s_disp_frac   = 0;   /* desetinna mista zobrazeneho formatu */
static int      s_disp_int    = 0;   /* cele cislice zobrazeneho formatu */
static uint8_t  s_disp_period = 0;   /* 1 = rezim PERIODA */
/* s_disp_recalc deklarovano vyse (u `st`) — pouziva ho i screen_main_button_action. */
static const char *s_disp_unit = "Hz";   /* "Hz" / "ns" / "us" / "ms" */
static double   s_disp_unit_s  = 1.0;    /* zobrazena jednotka -> sekundy (perioda: 1e-9/1e-6/1e-3) */
static char     s_seps[NUM_SEG_MAX]; /* mutable separatory (num_layout: '.'/','/' '/SEP_NONE) */

/* ── #1: napojeni realnych/emulovanych dat FPGA na headline + statistiky ──────
 * s_freq_n (hinge, cte ho headline i vsechna statistika) je bud REALNY kmitocet
 * z FpgaTasku (g_freq_*), nebo — kdyz neni platne mereni — SIMULACE freq_step()
 * jako fallback (viditelne oznaceny). Format velkeho cisla se prizpusobuje
 * magnitude mereni (Hz..GHz). */
static uint32_t s_last_fpga_seq = 0;   /* posledni zpracovana SEQUENCE (kadence vzorku) */
static uint8_t  s_freq_is_sim   = 1;   /* 1 = headline zeny simulaci (fallback), 0 = realne mereni */
static uint8_t  s_freq_fmt_changed = 0;/* num_layout prestavel format -> nutny plny redraw zony */

static void freq_fill_segments(void);   /* fwd (num_build_for naplni pocatecni hodnotu) */

static uint64_t pow10_u64(int e)
{
    uint64_t p = 1;
    while (e-- > 0) p *= 10u;
    return p;
}

/* Max sirka zony velkeho cisla [px] — rozpocet pro vyber poctu desetin.
 * Zona lezi SVISLE mezi hlavickou a mrizkou (`s_num_top` = baseline-72, vyska 88
 * konci presne na `SCR_MAIN_GRID_Y`), takze VODOROVNE je volna cela obrazovka a
 * omezuji jen jeji okraje: 800 - 2×10 px rezerva = 780.
 * Rozpocet (mono_75 monospace = 45 px/cislici, separatory ~15-18 px, "Hz" ~43):
 *   10 MHz  -> 8 celych + 5 desetin = 13 cislic ≈ 676 px -> vejde se PLNYCH 5 desetin
 *   1,4 GHz -> 10 celych + 4 desetiny = 14 cislic ≈ 736 px -> jedna desetina ustoupi
 * ⚠️ Drive 720 px zbytecne ubiralo desetinne misto uz kolem 1 GHz. */
#define FREQ_MAX_W  780

/* ⚠️ Delicka mezi `edge_count` a skutecnym kmitoctem se NEPREDPOKLADA — overuje se
 * proti `frequency_x100000` (viz `freq_frame_to_lsb`). Realna FPGA hlasi pocet
 * period vetve /4, emulator neděleneho signalu; pevna konstanta by jednu z nich
 * zobrazila 4x spatne. Pin27 (/16) svuj pocet period v ramci NEMA -> tam hi-res
 * dopocet nejde vubec (viz `g_freq_hires`). */
/* Kolik desetin ma smysl zobrazit: z reciproke dvojice (edges/gate) se da spocitat
 * libovolne mnoho, ze zaokrouhleneho `x100000` jen 5.
 * `FREQ_FRAC_SIM` = zakladni (SIM) format: o jedno desetinne misto VELKYM fontem
 * vic nez x1e5, takze pred dvema malymi nejistymi cislicemi jsou ctyri velke.
 * Fabrikace to neni — SIM hodnotu si stejne generuje `freq_step()` a cislo je
 * viditelne oznacene markerem "SIM". */
#define FREQ_FRAC_HIRES 7
#define FREQ_FRAC_X1E5  5
#define FREQ_FRAC_SIM   6

static uint8_t s_freq_hires = 0;   /* 1 = format postaveny pro hi-res dopocet (7 desetin) */

/* Prevod mereni z FPGA ramce na vnitrni LSB (= 10^-`s_freq_frac` Hz).
 *
 * HI-RES (`hires`): reciproky citac meri f = N / Δt, takze z `edge_count` (presny
 * pocet period vetve /4) a `gate_time_ns` (skutecna delka okna) se da podil spocitat
 * na VIC desetin, nez nese zaokrouhlene `frequency_x100000`. Ty cislice navic jsou
 * SKUTECNE (podil realne zmerenych velicin) — nejsou to vymyslene nuly; jen lezi pod
 * sumem (rozliseni TDC 2,5 ns / 0,25 s okno ~ 0,1 Hz pri 10 MHz), a prave proto je
 * posledni mista kresli ztlumene (SIGMA/FLOOR).
 * ⚠️ Pocita se DLOUHYM DELENIM (cela cast + cislice po jedne), NE `num × 10^frac / den`:
 * to by pri 7 desetinach pretekl uint64 uz kolem 10 MHz (2,5e22 >> 1,8e19).
 * ⚠️ `num = edges × 4 × 1e9` je bezpecne (nejhorsi pripad ~8,6e18 pri 21,5 s okne),
 * presto se hlida stropem — pri prekroceni se degraduje na x1e5 misto tichého preteceni.
 *
 * FALLBACK (bez hi-res): 5 desetin z `x100000`. ⚠️ DELENIM `10^(5-frac)`, protoze
 * `x100000 × 10^frac / 1e5` by pri ~4 GHz pretekl (4e19 > 1,8e19). */
static uint64_t freq_frame_to_lsb(uint64_t x100000, uint64_t edges, uint64_t gate_ns, int hires)
{
    int frac = s_freq_frac;
    if (hires && gate_ns > 0u && edges > 0u) {
        /* ⚠️ NASOBITEL SE NEPREDPOKLADA, ALE OVERUJE — a to na JEDINEM miste
         * (`fpga_freq_hires_mul`, viz fpga_freq.h). `edge_count` muze byt pocet
         * period DELENE vetve (/4) NEBO neděleného signalu (emulator), takze
         * pevne „×4" davalo pri `fpgasim on 10000000` kmitocet 40 MHz. Autoritativni
         * je `frequency_x100000` z ramce; hi-res je jen JEMNEJSI ODECET TEHOZ,
         * ne druhy nezavisly vypocet. Kdyz nesedi zadny nasobitel, hi-res se
         * NEPOUZIJE — radeji 5 poctivych desetin nez 15 spatnych. */
        uint64_t mul = fpga_freq_hires_mul(x100000, edges, gate_ns);
        if (mul) {
            uint64_t num = edges * mul * 1000000000ull;
            uint64_t v   = num / gate_ns;
            uint64_t rem = num % gate_ns;
            for (int i = 0; i < frac; i++) {                  /* rem < gate_ns -> rem×10 nepretece */
                rem *= 10u;
                v    = v * 10u + rem / gate_ns;
                rem %= gate_ns;
            }
            return v;
        }
        /* zadny nasobitel nesedel -> spadni na x1e5 (nize) */
    }
    if (frac <= FREQ_FRAC_X1E5) return x100000 / pow10_u64(FREQ_FRAC_X1E5 - frac);
    return x100000 * pow10_u64(frac - FREQ_FRAC_X1E5);   /* format chce vic, data nemaji */
}

/* ── #1: dynamicky format velkeho cisla ──────────────────────────────────────
 * Poskladej segmenty (cela cast po skupinach 3 = tisicove '.', pak ',' a
 * `frac_digits` desetin) pro dany pocet celych cislic. Naplni s_num_seg/s_seps/
 * s_num + geometrii. Hodnoty cislic se doplni pozdeji freq_fill_segments().
 * @return skutecny pocet segmentu. */
static int num_layout(int int_digits, int frac_digits, int n_unc)
{
    if (int_digits < 1) int_digits = 1;
    if (int_digits > 12) int_digits = 12;
    if (frac_digits < 0) frac_digits = 0;
    if (frac_digits > FREQ_FRAC_HIRES) frac_digits = FREQ_FRAC_HIRES;

    /* ── Skladba segmentu (delka / uroven / podtrzeni) + separator ZA kazdym ──
     * CELA cast: skupiny po 3 zprava, oddelene TECKOU (ceske tisice).
     * ZLOMEK: taky po trojicich, ale oddelene MEZEROU (SI styl) — po desetinne
     *   carce se uz zadna tecka nekresli, aby nebylo pochyb, co je desetinny
     *   oddelovac. `mono_25` ma prazdny glyf mezery (advance 15, zadny ink).
     * ⚠️ Uvnitr trojice se skupina jeste deli tam, kde se meni VZHLED (posledni
     *   duveryhodna cislice = modre podtrzeni, pak SIGMA a FLOOR mensim fontem);
     *   takove predely dostanou `UI_BIGNUM_SEP_NONE`, takze cislice zustanou
     *   slepene a trojice se opticky nerozpadne.
     * ⚠️ #51: kolik cislic je NEJISTYCH uz NENI natvrdo 2 — `n_unc` odvozuje
     *   volajici (`num_build_for` -> `freq_uncertain_frac`) z ROZLISENI hradla
     *   reciprocniho citace (√2·tdc/gate, deterministicke — NE simulace). SIM
     *   fallback dava 2 (nezmeneny vzhled). Tady se hodnota jen sanituje na
     *   [1, frac-1]: aspon 1 duveryhodna desetina (nese modre podtrzeni) a aspon
     *   1 nejista (hi-res posledni misto lezi vzdy pod sumem). */
    int glen[NUM_SEG_MAX]; uint8_t glvl[NUM_SEG_MAX]; uint8_t gund[NUM_SEG_MAX];
    char gsep[NUM_SEG_MAX]; int gn = 0;

    int first = int_digits % 3; if (first == 0) first = 3;
    int rem = int_digits;
    glen[gn] = first; glvl[gn] = UI_DIGIT_CERTAIN; gund[gn] = 0; gsep[gn] = '.'; gn++; rem -= first;
    while (rem > 0 && gn < NUM_SEG_MAX - 1) {
        glen[gn] = 3; glvl[gn] = UI_DIGIT_CERTAIN; gund[gn] = 0; gsep[gn] = '.'; gn++; rem -= 3;
    }
    gsep[gn - 1] = (frac_digits > 0) ? ',' : UI_BIGNUM_SEP_NONE;   /* desetinna carka */

    /* Zlomek: `n_cert` duveryhodnych, pak SIGMA a FLOOR. Podtrzena je POSLEDNI
     * duveryhodna cislice (samostatny segment), nejiste jdou mensim fontem. */
    if (frac_digits < 2) n_unc = frac_digits;      /* 0/1 desetina -> vse nejiste */
    else { if (n_unc < 1) n_unc = 1; if (n_unc > frac_digits - 1) n_unc = frac_digits - 1; }
    int n_cert = frac_digits - n_unc;
    int p = 1;
    while (p <= frac_digits && gn < NUM_SEG_MAX) {
        uint8_t lvl = (p <= n_cert) ? UI_DIGIT_CERTAIN
                    : ((p == n_cert + 1) ? UI_DIGIT_SIGMA : UI_DIGIT_FLOOR);
        uint8_t und = (p == n_cert) ? 1u : 0u;
        int len = 0;
        while (p + len <= frac_digits) {                 /* rozsiruj, dokud se nic nemeni */
            int q = p + len;
            uint8_t qlvl = (q <= n_cert) ? UI_DIGIT_CERTAIN
                         : ((q == n_cert + 1) ? UI_DIGIT_SIGMA : UI_DIGIT_FLOOR);
            uint8_t qund = (q == n_cert) ? 1u : 0u;
            if (qlvl != lvl || qund != und) break;        /* zmena vzhledu -> novy segment */
            if (len > 0 && ((q - 1) % 3) == 0) break;     /* hranice trojice */
            len++;
        }
        int endpos = p + len - 1;
        glen[gn] = len; glvl[gn] = lvl; gund[gn] = und;
        /* Mezera jen na skutecne hranici trojice, jinak segmenty slepit. */
        gsep[gn] = (endpos % 3 == 0 && endpos < frac_digits) ? ' ' : UI_BIGNUM_SEP_NONE;
        gn++; p += len;
    }
    gsep[gn - 1] = UI_BIGNUM_SEP_NONE;                    /* za poslednim segmentem nic */

    for (int i = 0; i < gn - 1; i++) s_seps[i] = gsep[i];
    s_seps[(gn > 0) ? gn - 1 : 0] = '\0';

    s_disp_total = 0;
    for (int i = 0; i < gn; i++) {
        int L = glen[i];
        s_seg_len[i] = (uint8_t)L;
        memset(s_num_buf[i], '0', (size_t)L); s_num_buf[i][L] = '\0';
        s_num_seg[i].text           = s_num_buf[i];
        s_num_seg[i].level          = glvl[i];
        s_num_seg[i].with_underline = gund[i] ? true : false;
        s_disp_total += L;
    }
    s_disp_int  = int_digits;
    s_disp_frac = frac_digits;

    /* Nejiste cislice: MENSI font (`fade_font`) + tmavsi odstin (`ui_level_color`:
     * INK -> INK_4 -> INK_5). Posledni duveryhodna cislice ma modre podtrzeni
     * (UI_COLOR_ACC v `ui_big_number_render_tail`). */
    s_num = (ui_big_number_t){
        .x_center = UI_DIM_SCREEN_W / 2, .y_baseline = SCR_MAIN_NUMBER_Y_BASELINE,
        .main_font = &ui_font_mono_75, .fade_font = &ui_font_mono_52,
        .sep_font = &ui_font_mono_25, .decimal_font = &ui_font_mono_30,
        .unit_font = &ui_font_sans_32, .segments = s_num_seg, .segment_count = (int16_t)gn,
        .separators = s_seps, .sep_color = UI_COLOR_INK_3,
        .decimal_color = UI_COLOR_ACC, .unit = s_disp_unit, .unit_color = UI_COLOR_INK_2,
    };
    s_num_w    = ui_big_number_width(&s_num);
    s_num_left = (int16_t)(UI_DIM_SCREEN_W / 2 - s_num_w / 2);
    s_num_top  = (int16_t)(SCR_MAIN_NUMBER_Y_BASELINE - 72);
    for (int i = 0; i < gn; i++) s_seg_x[i] = ui_big_number_seg_x(&s_num, (int16_t)i);
    return gn;
}

/* ── #51: kolik trailing desetin je NEJISTYCH (kresli se fade fontem) ──────────
 * Reciprocni citac s TDC krokem 2,5 ns a hradlem `gate_ns` ma kvantizacni
 * ROZLISENI ~√2·tdc/gate (relativne) = deterministicka fyzika, NEZAVISLA na
 * simulaci headline. Prepocet na Hz -> pocet duveryhodnych desetin = kolik
 * desetinnych mist ma mistni hodnotu jeste nad rozlisenim. Zbytek = nejiste.
 *   - SIM (gate_ns==0): vracime 2 -> nezmeneny vzhled simulace.
 *   - REAL/emulator (gate_ns z FPGA ramce, ~250e6 = 0,25 s): spocitane z hradla,
 *     takze delsi hradlo -> vic duveryhodnych cislic (spravne chovani citace).
 * ⚠️ ZADNY `log10` (nano.specs bez float printf je jina vec, ale libm log10 by
 *   zbytecne tahlo float — staci nasobeni 0,1 v celociselne smycce).
 * Vraci pocet nejistych desetin; volajici (`num_layout`) ho jeste sanituje. */
#define FREQ_TDC_PS  2500.0    /* Si5356 4 faze po 90° = 2,5 ns krok TDC (HW konstanta) */
static int freq_uncertain_frac(uint64_t x100000, uint64_t gate_ns, int frac)
{
    if (frac < 2)      return frac;    /* 0/1 desetina -> vse nejiste */
    if (gate_ns == 0u) return 2;       /* SIM -> nezmeneny vzhled (4 velke + 2 male) */
    double hz = (double)x100000 / 100000.0;
    if (hz <= 0.0) return 2;
    double gate_s   = (double)gate_ns * 1e-9;
    double u_res    = 1.41421356 * (FREQ_TDC_PS * 1e-12) / gate_s;   /* relativni */
    double res_hz   = u_res * hz;                                    /* rozliseni v Hz */
    /* Duveryhodne desetiny = ta, jejichz mistni hodnota (0,1 / 0,01 / …) je jeste
     * >= rozliseni. */
    int    nc = 0; double pv = 0.1;
    for (int p = 1; p <= frac; p++) { if (pv >= res_hz) { nc = p; pv *= 0.1; } else break; }
    if (nc < 1)          nc = 1;         /* aspon 1 duveryhodna (nese podtrzeni) */
    if (nc > frac - 1)   nc = frac - 1;  /* aspon 1 nejista (hi-res posledni misto pod sumem) */
    return frac - nc;
}

/* Poskladej format pro dane mereni: urci pocet celych cislic a zvol NEJVIC desetin,
 * ktere (a) `max_frac` povoluje (kolik jich zdroj unese) a (b) vejdou se do
 * FREQ_MAX_W. Naplni pocatecni hodnotu a shadow.
 * Volat pri INITu a pri zmene magnitudy/zdroje. */
static void disp_update(void);   /* fwd — prepocet s_disp_n z s_freq_n dle rezimu */

static void num_build_for(uint64_t x100000, uint64_t edges, uint64_t gate_ns, int max_frac)
{
    uint64_t whole = x100000 / 100000ull;
    int int_digits = 1;
    for (uint64_t t = whole; t >= 10ull; t /= 10ull) int_digits++;

    /* ── 1) FREKVENCNI stav (drzi ho statistika / SIM walk / screen_main_freq_hz —
     *        VZDY, i v rezimu PERIODA). Frac = kolik nese zdroj (hi-res 7 / x1e5 5 / sim). ── */
    s_freq_int  = int_digits;
    s_freq_frac = (max_frac > FREQ_FRAC_HIRES) ? FREQ_FRAC_HIRES : max_frac;
    s_freq_n    = x100000 ? freq_frame_to_lsb(x100000, edges, gate_ns, s_freq_hires) : 0u;
    s_freq_nominal_hz = (double)whole;
    s_freq_center     = (whole > 0u) ? whole * pow10_u64(s_freq_frac) : s_freq_n;

    /* ── 2) ZOBRAZENI: FREQUENCY nebo PERIODA (footer toggle `st.mode`). ── */
    s_disp_period = (uint8_t)(st.mode & 1);
    if (!s_disp_period) {
        s_disp_unit = SCR_S_UNIT_HZ; s_disp_unit_s = 1.0;
        for (int frac = max_frac; ; frac--) {
            num_layout(int_digits, frac, freq_uncertain_frac(x100000, gate_ns, frac));
            if (s_num_w <= FREQ_MAX_W || frac == 0) break;
        }
        /* freq frac = to, co num_layout vybral dle FREQ_MAX_W (v tomto rezimu jsou
         * frekvence a zobrazeni identicke) */
        s_freq_frac = s_disp_frac;
        s_freq_n    = x100000 ? freq_frame_to_lsb(x100000, edges, gate_ns, s_freq_hires) : 0u;
        s_freq_center = (whole > 0u) ? whole * pow10_u64(s_freq_frac) : s_freq_n;
    } else {
        /* Perioda z 1/f (ne hi-res gate/edges — staci ~7 platnych cifer). */
        double f    = (double)x100000 / 100000.0;
        double t_ns = (f > 0.0) ? 1e9 / f : 0.0;
        double t_disp;
        /* SI predpony s / ms / us / ns / ps dle magnitudy periody (1 <= mantisa < 1000). */
        if      (t_ns >= 1e9) { t_disp = t_ns / 1e9;  s_disp_unit = "s";  s_disp_unit_s = 1.0;   }
        else if (t_ns >= 1e6) { t_disp = t_ns / 1e6;  s_disp_unit = "ms"; s_disp_unit_s = 1e-3;  }
        else if (t_ns >= 1e3) { t_disp = t_ns / 1e3;  s_disp_unit = "us"; s_disp_unit_s = 1e-6;  }
        else if (t_ns >= 1.0) { t_disp = t_ns;        s_disp_unit = "ns"; s_disp_unit_s = 1e-9;  }
        else                  { t_disp = t_ns * 1e3;  s_disp_unit = "ps"; s_disp_unit_s = 1e-12; }
        uint64_t p_whole = (uint64_t)t_disp;
        int p_int = 1;
        for (uint64_t t = p_whole; t >= 10ull; t /= 10ull) p_int++;
        for (int frac = FREQ_FRAC_HIRES; ; frac--) {
            num_layout(p_int, frac, 2);
            if (s_num_w <= FREQ_MAX_W || frac == 0) break;
        }
    }

    disp_update();   /* naplni s_disp_n (freq: = s_freq_n; period: 1/f -> jednotka) */
    freq_fill_segments();
    for (int i = 0; i < s_num.segment_count; i++) strcpy(s_num_prev[i], s_num_buf[i]);
    s_num_ready = 1;
}

/* Prepocet zobrazovaneho cisla `s_disp_n` z frekvence `s_freq_n` podle rezimu.
 * Vola se po KAZDE zmene s_freq_n bez rebuilu formatu (SIM krok, drzeny FPGA seq). */
static void disp_update(void)
{
    if (!s_disp_period) { s_disp_n = s_freq_n; return; }
    double f = (double)s_freq_n / (double)pow10_u64(s_freq_frac);   /* Hz */
    if (f <= 0.0) { s_disp_n = 0; return; }
    double t_disp = (1.0 / f) / s_disp_unit_s;                       /* v jednotce s_disp_unit */
    s_disp_n = (uint64_t)(t_disp * (double)pow10_u64(s_disp_frac) + 0.5);
}

/* Init: zakladni SIM format pro 10 MHz (bez hi-res dvojice), 6 desetin =
 * 4 velke + 2 male nejiste. */
static void num_build(void)
{
    num_build_for(10000000ull * 100000ull, 0u, 0u, FREQ_FRAC_SIM);   /* 10 MHz × 1e5 */
}

/* SIM fallback: mean-revert random walk kolem `s_freq_center` (posledni znamy rad).
 * Krok ~±0,05 Hz, navrat /32 -> pasmo ~±0,3 Hz: cela cast stabilni, desetinna mista
 * zivot.
 * ⚠️ Amplituda se POCITA Z `s_freq_frac`, ne pevne v LSB: LSB je 10^-frac Hz, takze
 * konstantni krok by pri jinem poctu desetin znamenal jinou FYZIKALNI amplitudu
 * (pri 5 desetinach by pevnych ±524288 LSB delalo ±5 Hz misto ±0,05 Hz a rozkmitalo
 * by i celou cast). Takhle zustava vzhled simulace stejny v kazdem formatu. */
static void freq_step(void)
{
    static uint32_t rng = 0xDEADBEEFu;
    rng = rng * 1664525u + 1013904223u;
    int64_t amp = (int64_t)pow10_u64(s_freq_frac) / 20;         /* ~0,05 Hz v LSB */
    if (amp < 1) amp = 1;                                       /* bez desetin: min. 1 LSB */
    int64_t r    = (int64_t)((rng >> 12) & 0xFFFFF) - 0x80000;  /* ±524288 */
    int64_t step = r * amp / 0x80000;                           /* -> ±amp */
    int64_t off  = (int64_t)s_freq_n - (int64_t)s_freq_center;
    off += step - (off / 32);                                   /* random walk + decay (/32) */
    int64_t v = (int64_t)s_freq_center + off;
    if (v < 0) v = 0;
    s_freq_n = (uint64_t)v;
    disp_update();   /* prepocitej zobrazovane cislo (period: 1/f) */
}

/* ── #1: aktualizace s_freq_n ze ZDROJE (real FPGA / emulator, jinak SIM fallback).
 * REAL (FpgaTask `g_freq_*`) ma prednost; bez platneho mereni -> `freq_step()`.
 * Pri zmene magnitudy prestavi format (num_build_for) a nahodi s_freq_fmt_changed
 * (redraw_freq pak udela plny redraw misto per-segment). Prechod REAL<->SIM resetuje
 * statistiku (nemichat nekompatibilni vzorky). Volat 1×/tik z redraw_freq (on-main)
 * NEBO screen_main_freq_sim_step (off-main) — nikdy obe zaroven (jinak dvojity krok).
 * ⚠️ Seqlock cteni: FpgaTask (Normal) muze preemptnout UiTask (BelowNormal) a jeho
 * zapis je atomicky (kriticka sekce) + `g_freq_seq` roste kazdou zmenou -> re-check
 * seq odhali soubezny commit bez nutnosti FreeRTOS kriticke sekce tady. */
static void freq_advance(void)
{
    if (!s_num_ready) num_build();   /* format musi existovat (off-main cesta nema ready-guard) */

    uint32_t seq; uint64_t x100000, edges, gate_ns; uint8_t valid, hires;
    do { seq = g_freq_seq; x100000 = g_freq_x100000; valid = g_freq_valid;
         edges = g_freq_edges; gate_ns = g_freq_gate_ns; hires = g_freq_hires; }
    while (seq != g_freq_seq);

    /* Prepnul se FREQUENCY <-> PERIOD (footer toggle) -> vynut rebuild formatu
     * z posledni znamé FREKVENCE (na novém mereni / SIM kroku nezavisle). */
    if (s_disp_recalc) {
        s_disp_recalc = 0;
        double f_hz = (double)s_freq_n / (double)pow10_u64(s_freq_frac);
        uint64_t fx = (f_hz > 0.0) ? (uint64_t)(f_hz * 100000.0 + 0.5)
                                   : (10000000ull * 100000ull);
        num_build_for(fx, 0u, 0u, s_freq_hires ? FREQ_FRAC_HIRES : FREQ_FRAC_SIM);
        s_freq_fmt_changed = 1;
        s_last_fpga_seq    = seq - 1u;   /* dalsi realne mereni znovu vyhodnot */
    }

    if (valid && x100000 > 0) {
        if (s_freq_is_sim) { s_freq_is_sim = 0; screen_main_stats_reset();
                             s_last_fpga_seq = seq - 1u; s_freq_fmt_changed = 1; }  /* SIM->REAL: sundej marker */
        if (seq != s_last_fpga_seq) {                 /* NOVE mereni */
            s_last_fpga_seq = seq;
            uint64_t whole = x100000 / 100000ull;
            int idg = 1; for (uint64_t t = whole; t >= 10ull; t /= 10ull) idg++;
            if (idg > 12) idg = 12;
            /* Prestav format pri zmene RADU nebo pri prepnuti /4<->/16: s /16 zmizi
             * `edge_count`, tedy i moznost hi-res dopoctu -> pocet desetin se MUSI
             * srazit, jinak by se dokreslovaly nuly, ktere mereni nenese. */
            if (idg != s_freq_int || hires != s_freq_hires) {
                s_freq_hires = hires;
                num_build_for(x100000, edges, gate_ns,
                              hires ? FREQ_FRAC_HIRES : FREQ_FRAC_X1E5);
                s_freq_fmt_changed = 1;
                screen_main_stats_reset();            /* jiny rad/zdroj -> nemichat s pyramidou */
            } else {
                s_freq_n = freq_frame_to_lsb(x100000, edges, gate_ns, hires);
                disp_update();
            }
        }
        /* stejny seq -> hodnota drzi (FPGA ~4/s, displej 20 Hz) */
        return;
    }

    if (!s_freq_is_sim) { s_freq_is_sim = 1; screen_main_stats_reset(); s_freq_fmt_changed = 1; }  /* REAL->SIM: ukaz marker */
    freq_step();
}

/* Rozlozi s_freq_n do segmentu (MSB first, zero-pad na pevnou sirku). */
static void freq_fill_segments(void)
{
    char d[20];
    memset(d, '0', sizeof d);   /* hardening: kdyby Σ s_seg_len > s_freq_total, cti '0' (ne smeti) */
    uint64_t v = s_disp_n;
    for (int i = s_disp_total - 1; i >= 0; i--) { d[i] = (char)('0' + (int)(v % 10u)); v /= 10u; }
    int p = 0, n = s_num.segment_count;
    for (int s = 0; s < n; s++) {
        int L = s_seg_len[s];                /* cachovana delka (num_layout) misto strlen */
        for (int k = 0; k < L; k++) s_num_buf[s][k] = d[p++];
        s_num_buf[s][L] = '\0';
    }
}

/* Obdelnik velkeho cisla (vc. jednotky) = clear/podbarvovaci zona. Shodny s
 * partial-redraw oblasti v screen_main_redraw_freq: vyska 88 konci presne nad
 * horni hranou karet mrizky (SCR_MAIN_GRID_Y), takze jim podbarveni nezasahuje
 * do okraju. Platny az po num_layout (cachovana geometrie; meni se s formatem). */
static prim_rect_t freq_area(void)
{
    return (prim_rect_t){(int16_t)(s_num_left - 2), s_num_top,
                         (int16_t)(s_num_w + 10), 88};
}

/* STOP -> lehke cervene podbarveni cele zony kmitoctu (mereni STOJI). Kresli se
 * jen kdyz je zastaveno; pri RUN zustava cisty gradient. */
static void freq_tint_if_stopped(void)
{
    if (st.running) return;
    prim_fill_rect(freq_area(), UI_COLOR_FREQ_STOP_BG, PRIM_BLEND_OVER);
}

/* Big number rendered directly over the gradient background. HW (DMA2D) glyph
 * blend zapnut JEN pro tohle velke cislo (mereny kmitocet) — ostatni text jede CPU. */
static void render_body_number(void)
{
    if (!s_num_ready) num_build();   /* jednou; jitterovany stav pak prezije full render */
    freq_tint_if_stopped();          /* az PO num_build — potrebuje cachovanou geometrii */
    prim_set_glyph_accel(1);
    ui_big_number_render(&s_num);
    prim_set_glyph_accel(0);
}

/* ── GPSDO statistika z MERENEHO kmitoctu ───────────────────────────────────
 * Frakcni odchylka y=(f-f0)/f0 (f0 = `s_freq_nominal_hz`, tj. rad mereni).
 * Zdroj je realne mereni z FPGA (nebo SIM fallback — viz `freq_advance`).
 * ⚠️ Vzorkuje se pri NOVEM mereni (`g_freq_seq`), v SIM rezimu 1x/s — kadenci ridi
 * `app_gpsdo_tick_stats_sample`. Pyramida ale porad predpoklada ~1 s rozestup
 * (τ0=1s); presny τ0 = skutecny rozestup az s MathTaskem (#27). Do (a) plocheho ring bufferu
 * (kratkodobe: trend 60s, offset, drift, σy@1s) a (b) decimacni pyramidy
 * (dlouhodoby Allan, tau 1..100000+ s, viz adev_feed). Prekresleni 1x/s. Float OK
 * (cold path; mimo no-float pravidlo pro protokol kmitoctu). */
/* Plochy ring = jen kratkodobe (trend 60s, offset, drift, σy@1s). DLOUHODOBY Allan
 * (tau az 100000 s / 100+ dni) resi decimacni pyramida nize. Vzorkuje se 1/s. */
#define STAT_N    120               /* 1/s -> 120 s (trend 60s + drift baseline) */
#define TREND_WIN 60                /* trend sparkline = posledni okno 60 s (1/s) */
static float s_y[STAT_N];
static int   s_y_head = 0, s_y_count = 0;
static void adev_feed(float v);     /* fwd — decimacni pyramida (dlouhodoby Allan) */
static void trend_feed(float v);    /* fwd — decimacni pyramida (dlouhodoby trend) */

static uint32_t s_stats_ver = 0;          /* verze dat: roste s kazdym vzorkem (change-key oken) */

static void stats_sample(void)
{
    /* off_n = odchylka v LSB (LSB=1e-7 Hz), f0=1e7 Hz -> y = off_n*1e-14 */
    int64_t off_n = (int64_t)s_freq_n - (int64_t)s_freq_center;
    float y = (float)off_n * 1e-14f;
    s_y[s_y_head] = y;                    /* plochy ring (kratkodobe) */
    s_y_head = (s_y_head + 1) % STAT_N;
    if (s_y_count < STAT_N) s_y_count++;
    adev_feed(y);                         /* decimacni pyramida (dlouhodoby Allan) */
    trend_feed(y);                        /* decimacni pyramida (dlouhodoby trend, az ~60 dni) */
    s_stats_ver++;                        /* histogram okno prekresli jen pri zmene */
}

/* Verze statistickych dat — histogram okno se prekresli jen kdyz se zmeni
 * (v okne se nevzorkuje -> obsah je konstantni, zadne 2x/s prazdne redraws). */
uint32_t screen_main_stats_version(void) { return s_stats_ver; }

/* σy@τ=1s pro prahovy monitor (alarm.c). Vraci 0, dokud neni aspon 2 bloky
 * vzorku — volajici to MUSI odlisit od "vynikajici stability", jinak by prazdna
 * statistika vypadala jako nula. `stats_adev` je definovana az nize, proto fwd. */
static float stats_adev(int m);
float screen_main_adev_1s(void) { return stats_adev(1); }

static float stat_at(int age)   /* age 0 = nejnovejsi */
{
    int idx = (s_y_head - 1 - age + 2 * STAT_N) % STAT_N;
    return s_y[idx];
}

static float stats_mean(int n)
{
    if (n > s_y_count) n = s_y_count;
    if (n <= 0) return 0.0f;
    float s = 0; for (int i = 0; i < n; i++) s += stat_at(i);
    return s / (float)n;
}

static float stats_pp(int n)
{
    if (n > s_y_count) n = s_y_count;
    if (n <= 0) return 0.0f;
    float mn = stat_at(0), mx = mn;
    for (int i = 1; i < n; i++) { float v = stat_at(i); if (v < mn) mn = v; if (v > mx) mx = v; }
    return mx - mn;
}

/* Non-overlapping ADEV plocheho ringu pro tau = m vzorku (tau0=1 s, 1/s). Pouziva
 * se pro σy@1s (m=1). Dlouhodoby Allan dela pyramida (adev_stage). >=2 bloky. */
static float stats_adev(int m)
{
    int blocks = s_y_count / m;
    if (blocks < 2) return 0.0f;
    float prev = 0; int have = 0; double acc = 0; int nd = 0;
    for (int b = 0; b < blocks; b++) {
        float bs = 0;
        for (int j = 0; j < m; j++) bs += stat_at(b * m + j);
        bs /= (float)m;
        if (have) { float d = bs - prev; acc += (double)d * (double)d; nd++; }
        prev = bs; have = 1;
    }
    return (nd > 0) ? sqrtf((float)(0.5 * acc / (double)nd)) : 0.0f;
}

/* Drift (df/dt) = linearni trend frakcni odchylky pres okno [1/s]. Metoda dvou
 * pulek: (prumer novejsi pulky - prumer starsi pulky) / odstup centroidu. */
static float stats_drift(void)
{
    int h = s_y_count / 2;
    if (h < 1) return 0.0f;
    float nm = 0, om = 0;
    for (int i = 0; i < h; i++) { nm += stat_at(i); om += stat_at(s_y_count - 1 - i); }
    nm /= (float)h; om /= (float)h;
    float dt = (float)h;                  /* odstup centroidu pulek [s] (vzorky × 1 s) */
    return (dt > 0.0f) ? (nm - om) / dt : 0.0f;
}

/* ── Decimacni pyramida pro DLOUHODOBY Allan (tau 1..100000 s, ohranicena pamet) ──
 * Vzorek y (1/s) jde do stage 0; po 10 vzorcich se jejich prumer posune do dalsi
 * stage (tau ×10). Stage s drzi prumery na tau=10^s s. Pokryje 100+ dni v ~640 B
 * (plochy buffer by chtel desitky MB). */
#define ADEV_STAGES 6                 /* tau = 1, 10, 100, 1k, 10k, 100k s */
#define ADEV_RING   24                /* prumeru na stage (na ADEV vypocet) */
typedef struct { float ring[ADEV_RING]; int16_t head, count; float acc; int16_t acc_n; } adev_stage_t;
static adev_stage_t s_adev[ADEV_STAGES];

/* Vlozi vzorek od zvolene stage vys (stage s ma tau = 10^s s). Bezny zivy vzorek
 * jde od stage 0 (tau0 = 1 s); rekonstrukce z datalogu od stage 1, protoze log
 * ma kadenci PRESNE 10 s = tau stage 1. */
static void adev_feed_from(int s0, float v)
{
    for (int s = s0; s < ADEV_STAGES; s++) {
        adev_stage_t *sg = &s_adev[s];    /* 'sg', ne 'st' — nekolidovat s globalnim UI stavem */
        sg->ring[sg->head] = v;
        sg->head = (int16_t)((sg->head + 1) % ADEV_RING);
        if (sg->count < ADEV_RING) sg->count++;
        sg->acc += v;
        if (++sg->acc_n < 10) return;             /* dalsi stage jeste nema co krmit */
        v = sg->acc / 10.0f; sg->acc = 0; sg->acc_n = 0;   /* dekadovy prumer -> dal */
    }
}

static void adev_feed(float v) { adev_feed_from(0, v); }

/* ── Rekonstrukce dlouhych tau z datalogu (STATUS.md G) ──────────────────────
 * Kazdy reboot dosud vynuloval celou ADEV pyramidu, takze dlouha tau (1k, 10k s)
 * se musela nabirat znovu od nuly — po restartu jsi o hodiny mereni prisel.
 * Datalog ale drzi kmitocet po 10 s klidne dny dozadu.
 *
 * ⚠️ KLICOVE: vzorek z logu se vklada od STAGE 1, ne od stage 0. Stage 1 ma
 * tau = 10 s, coz je PRESNE kadence datalogu, takze prevod je exaktni — zadne
 * prevzorkovani, zadna zmena tau0. Kdyby se log sypal do stage 0 (tau0 = 1 s),
 * vysla by sigma_y(tau) systematicky SPATNE o cely rad a pritom by vypadala
 * verohodne. Stage 0 zustava prazdna, dokud ji nenaplni zive vzorky — a to je
 * spravne: log zadna 1s data nema.
 *
 * ⚠️ Trend pyramida se ZAMERNE nerekonstruuje: decimuje po 4 (1/4/16 s), takze
 * 10s kadence loguje na zadnou jeji stage nesedne a musela by se prevzorkovat —
 * tim by se zkreslila casova osa. Dlouha okna trendu maji misto toho cist
 * datalog primo, stejnym vzorem jako okno GRAFY. */
void screen_main_adev_seed_10s(float y) { adev_feed_from(1, y); }

/* Nominal [Hz], proti kteremu se pocita frakcni odchylka y = (f - f0)/f0.
 * Je to tentyz stred, jaky pouziva `stats_sample` (jen v Hz misto v LSB), takze
 * rekonstrukce z logu a zive vzorky mluvi o TEMZE — jinak by se v jedne pyramide
 * michaly dve ruzne reference. 0 = velke cislo jeste nebylo inicializovano. */
double screen_main_freq_nominal(void)
{
    return s_num_ready ? s_freq_nominal_hz : 0.0;
}

/* ── #45: L(f) fazoveho sumu z ringu frakcnich fluktuaci `s_y[]` ──────────────
 * Spocita spektrum (phase_noise.c) z poslednich PN_NFFT vzorku (1/s) a vrati
 * L(f) na binu nejblizsim `target_hz`. Cold path — vola se jen pri renderu okna
 * ANALYZA (ne v tiku). Buffery `static` (nezatezovat stack UiTasku).
 * @return 1 = spocteno (>=PN_NFFT vzorku, ~64 s behu); 0 = zatim malo dat. */
int screen_main_phase_noise(double target_hz, double *f_used, double *l_dbc)
{
    if (s_y_count < PN_NFFT) return 0;
    static float      chron[STAT_N];        /* chronologicky (nejstarsi first) */
    static pn_point_t pts[PN_NBINS];
    int n = s_y_count;
    for (int i = 0; i < n; i++) chron[i] = stat_at(n - 1 - i);
    double f0 = (s_freq_nominal_hz > 0.0) ? s_freq_nominal_hz : 1e7;
    int np = pn_compute(chron, n, f0, 1.0, pts, PN_NBINS);
    if (np <= 0) return 0;
    int best = 0; double bd = 1e30;
    for (int i = 0; i < np; i++) {
        double d = pts[i].f_hz - target_hz; if (d < 0) d = -d;
        if (d < bd) { bd = d; best = i; }
    }
    if (f_used) *f_used = pts[best].f_hz;
    if (l_dbc)  *l_dbc  = pts[best].l_dbc;
    return 1;
}

static float adev_rat(const adev_stage_t *sg, int i)       /* i-ty nejstarsi prvek */
{
    int idx = (sg->head - sg->count + i + 2 * ADEV_RING) % ADEV_RING;
    return sg->ring[idx];
}

/* ── Decimacni pyramida pro DLOUHODOBY TREND (okno az ~60 dni) ────────────────
 * Plochy ring s_y[] pokryje jen 120 s; okno 30 dni by v nem chtelo 2,6 M vzorku
 * (~10 MB). Stejny princip jako ADEV pyramida vyse, ale JEMNEJSI decimace (×4
 * misto ×10) a delsi ringy -> hladsi krivka i u dlouhych oken. Stage s ma
 * rozliseni 4^s sekund a rozsah TR_RING*4^s:
 *   s=0: krok 1 s  rozsah 128 s      s=5: krok 1024 s  rozsah 1,5 dne
 *   s=1: krok 4 s  rozsah 8,5 min    s=6: krok 4096 s  rozsah 6,1 dne
 *   s=2: krok 16 s rozsah 34 min     s=7: krok 16384 s rozsah 24 dni
 *   s=3: krok 64 s rozsah 2,3 h      s=8: krok 65536 s rozsah 97 dni
 *   s=4: krok 256 s rozsah 9,1 h
 * Pamet ~4,7 kB (9 x 128 float) v RAM_D1 (512 kB) — zanedbatelne. */
#define TR_STAGES 9
#define TR_RING   128
#define TR_DECIM  4
typedef struct { float ring[TR_RING]; int16_t head, count; float acc; int16_t acc_n; } tr_stage_t;
static tr_stage_t s_tr[TR_STAGES];

static void trend_feed(float v)
{
    for (int s = 0; s < TR_STAGES; s++) {
        tr_stage_t *sg = &s_tr[s];
        sg->ring[sg->head] = v;
        sg->head = (int16_t)((sg->head + 1) % TR_RING);
        if (sg->count < TR_RING) sg->count++;
        sg->acc += v;
        if (++sg->acc_n < TR_DECIM) return;            /* vyssi stage jeste nema co krmit */
        v = sg->acc / (float)TR_DECIM; sg->acc = 0; sg->acc_n = 0;
    }
}

static int32_t tr_res(int s)            /* rozliseni stage [s/vzorek] = 4^s */
{
    int32_t r = 1; for (int i = 0; i < s; i++) r *= TR_DECIM; return r;
}

/* Nejnizsi (= nejjemnejsi) stage, jehoz rozsah pokryje pozadovane okno [s]. */
static int tr_pick(int32_t win_s)
{
    for (int s = 0; s < TR_STAGES; s++)
        if ((int64_t)TR_RING * tr_res(s) >= (int64_t)win_s) return s;
    return TR_STAGES - 1;
}

static float tr_at(int s, int age)      /* age 0 = nejnovejsi */
{
    const tr_stage_t *sg = &s_tr[s];
    int idx = (sg->head - 1 - age + 2 * TR_RING) % TR_RING;
    return sg->ring[idx];
}

/* Vynuluje veskerou akumulovanou statistiku "namerenych hodnot citacem":
 * plochy ring (trend 60s/offset/sigma@1s/drift), ADEV pyramidu (dlouhodoby
 * Allan) i trend pyramidu (dlouhodoby trend az ~60 dni). Nezasahuje `s_meas_stats`
 * (okno MERENI ma vlastni RESET tlacitko/mp_stats_reset — jina akumulace).
 * `s_stats_ver++` je NUTNE (ne jen memset) — histogram/Allan okna prekreslujou
 * jen pri zmene verze, bez inkrementu by po resetu zustal na displeji stary obsah
 * az do dalsiho realneho vzorku. Volitelne z UART "meas reset" (UartTask) i
 * tlacitka v okne Alarmy (UiTask) — cisty RAM zapis, zadny mutex netreba.
 * ⚠️ Nutny je i resync eased hodnot (Offset/σ/Drift + trend sparkline, jen v2
 * layout): `anim_step` dojizdi k CILI, takze bez resyncu by se po resetu cisla
 * ~1-2 s PLYNULE snasela k nule misto okamziteho skoku — vypadalo by to, jako
 * ze reset "chvili trva". Stejny duvod, proc je volaji plne rendery (radek
 * ~1490). Pri STARÉM layoutu jsou obe no-op. */
static void stats_anim_resync(void);   /* fwd — definice az u eased statistik */
static void trend_anim_resync(void);   /* fwd */

void screen_main_stats_reset(void)
{
    memset(s_y, 0, sizeof s_y);
    s_y_head = 0; s_y_count = 0;
    memset(s_adev, 0, sizeof s_adev);
    memset(s_tr, 0, sizeof s_tr);
    s_stats_ver++;
    stats_anim_resync();
    trend_anim_resync();
}

/* Doba [s] -> kompaktni text ("45 s" / "10 min" / "6 h" / "30 d"). */
static void fmt_dur(char *b, int n, int32_t s)
{
    if      (s < 60)    snprintf(b, n, "%ld s",   (long)s);
    else if (s < 3600)  snprintf(b, n, "%ld min", (long)(s / 60));
    else if (s < 86400) snprintf(b, n, "%ld h",   (long)(s / 3600));
    else                snprintf(b, n, "%ld d",   (long)(s / 86400));
}
void screen_main_fmt_dur(char *b, int n, int32_t s) { fmt_dur(b, n, s); }

/* Non-overlapping ADEV stage s pri decimaci m (tau = m*10^s s). */
/* ── Estimatory stability nad ringem jedne stage (τ0 = 10^s s) ───────────────
 * Ring drzi M kmitoctovych vzorku y_0..y_{M-1} (nejstarsi prvni, `adev_rat`).
 * Vsechny tri jsou OVERLAPPING (Riley, NIST SP1065) — z TYCHZ dat davaji vyrazne
 * lepsi konfidenci nez non-overlapping varianta, ktera tu byla do 2026-08-18:
 * ta pri tau = m·τ0 zahodila vetsinu moznych dvojic (pouzila jen M/m bloku misto
 * M-2m+1 prekryvajicich se). Na dlouhych tau, kde je vzorku nejmene, to byl
 * rozdil mezi "nekolik paru" a "radove vic".
 *
 *   ADEV  σ²  = 1/(2m²(M-2m+1))   Σ_j [ Σ_i (y_{i+m} - y_i) ]²
 *   HDEV  H²  = 1/(6m²(M-3m+1))   Σ_j [ Σ_i (y_{i+2m} - 2y_{i+m} + y_i) ]²
 *   MDEV  M²  = 1/(2m⁴(M-3m+2))   Σ_j [ Σ_i Σ_k (y_{k+m} - y_k) ]²
 *
 * K cemu to je (proc tri, a ne jen ADEV):
 *   MDEV rozlisi BILY a BLIKAVY fazovy sum, ktere maji v ADEV stejny sklon —
 *        ADEV je od sebe neodlisi, MDEV ano (jiny sklon).
 *   HDEV je imunni vuci LINEARNIMU DRIFTU (druhe diference), takze u OCXO se
 *        stárnutím ukaze skutecny sum misto driftove rampy.
 * Slozitost O(M·m²) pri M<=24 a m<=5 -> par set operaci, bezi 1x/s. */
#define ADEV_KIND_ADEV  0
#define ADEV_KIND_MDEV  1
#define ADEV_KIND_HDEV  2

static float adev_stage_kind(int s, int m, int kind)
{
    const adev_stage_t *sg = &s_adev[s];
    int M = sg->count;
    if (m < 1) m = 1;

    double acc = 0.0;
    int n = 0;

    if (kind == ADEV_KIND_HDEV) {
        if (M < 3 * m + 1) return 0.0f;
        for (int j = 0; j + 3 * m <= M - 1; j++) {
            double inner = 0.0;
            for (int i = j; i < j + m; i++)
                inner += (double)adev_rat(sg, i + 2 * m)
                       - 2.0 * (double)adev_rat(sg, i + m)
                       + (double)adev_rat(sg, i);
            acc += inner * inner; n++;
        }
        if (n == 0) return 0.0f;
        return sqrtf((float)(acc / (6.0 * (double)m * (double)m * (double)n)));
    }

    if (kind == ADEV_KIND_MDEV) {
        if (M < 3 * m + 1) return 0.0f;
        for (int j = 0; j + 3 * m - 1 <= M - 1; j++) {
            double inner = 0.0;
            for (int i = j; i < j + m; i++)
                for (int k = i; k < i + m; k++)
                    inner += (double)adev_rat(sg, k + m) - (double)adev_rat(sg, k);
            acc += inner * inner; n++;
        }
        if (n == 0) return 0.0f;
        double m4 = (double)m * (double)m * (double)m * (double)m;
        return sqrtf((float)(acc / (2.0 * m4 * (double)n)));
    }

    /* ADEV (overlapping) */
    if (M < 2 * m + 1) return 0.0f;
    for (int j = 0; j + 2 * m <= M - 1; j++) {
        double inner = 0.0;
        for (int i = j; i < j + m; i++)
            inner += (double)adev_rat(sg, i + m) - (double)adev_rat(sg, i);
        acc += inner * inner; n++;
    }
    if (n == 0) return 0.0f;
    return sqrtf((float)(acc / (2.0 * (double)m * (double)m * (double)n)));
}

static float adev_stage(int s, int m) { return adev_stage_kind(s, m, ADEV_KIND_ADEV); }

/* Format frakcni hodnoty jako "<sign>M,m×10⁻E" s HORNIM INDEXEM exponentu
 * (mono_14/16 maji plny charset vc. ⁰..⁹⁻⁺). with_sign: + pro kladne. */
static void fmt_frac(char *buf, int len, float v, int with_sign)
{
    static const char *const SUP[10] = {"⁰","¹","²","³","⁴","⁵","⁶","⁷","⁸","⁹"};
    float a = fabsf(v);
    if (a < 1e-15f) { snprintf(buf, len, "0"); return; }
    int e = 0;
    while (a < 1.0f && e < 30) { a *= 10.0f; e++; }   /* hodnoty <1 -> e>0 (10⁻e) */
    while (a >= 10.0f) { a /= 10.0f; e--; }
    int M = (int)a;
    int m = (int)((a - (float)M) * 10.0f + 0.5f);
    if (m >= 10) { m = 0; M++; }
    if (M >= 10) { M = 1; m = 0; e--; }   /* 9,96 -> zaokrouhleni pres dekadu: 10,0×10⁻⁹ -> 1,0×10⁻⁸ */
    int ae = (e < 0) ? -e : e;
    char es[12]; es[0] = '\0';
    if (ae >= 10) strcat(es, SUP[(ae / 10) % 10]);
    strcat(es, SUP[ae % 10]);
    const char *sign    = (v < 0) ? "-" : (with_sign ? "+" : "");
    const char *expsign = (e < 0) ? "⁺" : "⁻";
    /* %10u: M je 1..9 a m 0..9 vzdy — modulo to rekne i kompilatoru (zadny
     * teoreticky -Wformat-truncation na 11mistny int) */
    snprintf(buf, len, "%s%u,%u×10%s%s", sign, (unsigned)M % 10u, (unsigned)m % 10u,
             expsign, es);
}

/* Selftest cistych UI helperu: fmt_frac vektory (vc. zaokrouhleni pres dekadu)
 * + hist_h invarianty (peak=plna vyska, log zveda slabe biny). Zadny sdileny
 * stav -> bezpecne z UartTasku za behu. Soucast UART "selftest". */
static int16_t hist_h(float count, int peak, int16_t H, bool logy);   /* fwd */
bool screen_main_selftest(void)
{
    char b[24]; int ok = 1;
    fmt_frac(b, sizeof b, 1.23e-8f, 0);   ok &= (strcmp(b, "1,2×10⁻⁸") == 0);
    fmt_frac(b, sizeof b, -4.56e-11f, 0); ok &= (strcmp(b, "-4,6×10⁻¹¹") == 0);
    fmt_frac(b, sizeof b, 9.96e-9f, 0);   ok &= (strcmp(b, "1,0×10⁻⁸") == 0);   /* pres dekadu */
    fmt_frac(b, sizeof b, 0.0f, 0);       ok &= (strcmp(b, "0") == 0);
    fmt_frac(b, sizeof b, 2.5e-9f, 1);    ok &= (strcmp(b, "+2,5×10⁻⁹") == 0);
    ok &= (hist_h(0.0f, 10, 100, false) == 0);
    ok &= (hist_h(10.0f, 10, 100, false) == 100);
    ok &= (hist_h(10.0f, 10, 100, true) == 100);
    ok &= (hist_h(1.0f, 10, 100, true) > hist_h(1.0f, 10, 100, false));
    printf("ui: fmt_frac+hist_h selftest %s\n", ok ? "OK" : "FAIL");
    return ok != 0;
}

/* Rect karet pro zive prekresleni (zachyceno pri full renderu). */
static prim_rect_t s_allan_rect = {0,0,0,0};
static prim_rect_t s_trend_rect = {0,0,0,0};
static prim_rect_t s_small_rect = {0,0,0,0};

/* Tap do Allan karty (hlavni obrazovka) -> otevre histogram okno (analyza mereni). */
bool screen_main_hit_allan(int16_t x, int16_t y)
{
    return s_allan_rect.w != 0 && pt_in(x, y, s_allan_rect);
}

/* Tap do trend karty -> fullscreen trend (cela historie ringu, ne jen 60 s). */
int screen_main_focus_rects(prim_rect_t *out, int max)
{
    /* 🔴 Ctyri vstupy z hlavni obrazovky NEJSOU tlacitka (pilulky a karty), takze
     * je registr fokusu v app_gpsdo NEVIDI — `ui_button_render` jimi neprochazi.
     * Bez tohohle jsou GPS okno a fullscreen trend ENCODEREM NEDOSTUPNE, coz porusuje
     * pozadavek „encoder sam musi stacit" (UI_ENCODER_NAVRH.md §2.1).
     * Poradi = poradi encoderu; rect s w==0 znamena „prvek se prave nekresli"
     * (pilulka vypadla z rady kvuli HDR_PILL_LIMIT) a preskakuje se. */
    const prim_rect_t r[4] = { s_gnss_pill_rect, s_sys_pill_rect, s_allan_rect, s_trend_rect };
    int n = 0;
    for (int i = 0; i < 4 && n < max; i++)
        if (r[i].w > 0 && r[i].h > 0) out[n++] = r[i];
    return n;
}

bool screen_main_hit_trend(int16_t x, int16_t y)
{
    return s_trend_rect.w != 0 && pt_in(x, y, s_trend_rect);
}

/* ADEV body z decimacni pyramidy: per stage tau = {1,2,5}×10^s s (log spacing
 * 1,2,5,10,20,50,...). Delsi tau nabihaji jak roste historie -> osa se prodluzuje
 * az k 100000+ s (100 dni), pamet ohranicena. Sdili NAHLED na hlavni obrazovce
 * i velky graf (screen_main_render_allan_big). Vraci pocet bodu (<=max). */
/* ns (nepovinne, NULL-safe): pocet clenu sumy na kazdy tau bod — slouzi ke
 * konfidencnimu pasu (rel. nejistota ~ 1/sqrt(2*ns)). */
static int allan_metric_kind(void);   /* fwd — definice u prepinace metriky nize */
static int adev_points(float *taus, float *adevs, int *ns, int max)
{
    static const int SM[] = {1, 2, 5};
    int kind = allan_metric_kind();   /* krivka sleduje zvolenou metriku (fwd nize) */
    int np = 0;
    for (int s = 0; s < ADEV_STAGES; s++) {
        float dec = powf(10.0f, (float)s);          /* 1,10,100,1k,10k,100k */
        for (int mi = 0; mi < 3; mi++) {
            if (np >= max) return np;
            int m = SM[mi];
            float a = adev_stage_kind(s, m, kind);
            if (a <= 0.0f) continue;
            taus[np] = dec * (float)m; adevs[np] = a;
            /* Pocet clenu sumy = sirka konfidencniho pasu (~1/sqrt(2n)).
             * U OVERLAPPING variant je jich radove vic nez u puvodnich
             * non-overlapping bloku — pas je proto uzsi, a to opravnene. */
            if (ns) {
                int M = s_adev[s].count;
                int n = (kind == ADEV_KIND_ADEV) ? (M - 2 * m) : (M - 3 * m + 1);
                ns[np] = (n > 1) ? n : 1;
            }
            np++;
        }
    }
    return np;
}

/* Spolecne log-log mapovani ADEV krivky do 'inner' (+ markery). Y pevne dekady
 * 10^ALLAN_Y_MIN..10^(ALLAN_Y_MIN+ALLAN_Y_DEC), X dynamicky [tau_min..tau_max].
 * Sdili nahled (marker_r=2) i velky graf (marker_r=3). */
#define ALLAN_Y_MIN  (-10)
#define ALLAN_Y_DEC  4

/* ── Metrika Allan okna (segmented v okne ALLAN) ─────────────────────────────
 *   0 = ADEV  σy(τ)         — overlapping
 *   1 = MDEV  Mod σy(τ)     — rozlisi bily vs blikavy fazovy sum (ADEV ne)
 *   2 = HDEV  Hσy(τ)        — imunni vuci linearnimu driftu (druhe diference)
 *   3 = TDEV  σx(τ)         — od 2026-08-18 EXAKTNI: TDEV = τ·MDEV/√3 je DEFINICE.
 *                             Driv se pocital z ADEV, coz plati jen pri MDEV≈ADEV
 *                             (tedy ne u fazoveho sumu, kde se prave lisi).
 *   4 = MTIE                — porad jen ODHAD (√3·τ·ADEV): presny MTIE potrebuje
 *                             ULOZENOU FAZI (time error), kterou bez 1PPS TIC
 *                             (#36) nemame. Popisek to priznava.
 * Metrika meni jen KRIVKU + Y osu; σy(τ) tabulka zustava ADEV referenci. */
#define ALLAN_METRIC_N 5
static int s_allan_metric = 0;
void screen_main_set_allan_metric(int m)
{ s_allan_metric = (m < 0) ? 0 : (m >= ALLAN_METRIC_N ? ALLAN_METRIC_N - 1 : m); }
int  screen_main_allan_metric(void)      { return s_allan_metric; }

/* Ktery estimator se ma pro aktualni metriku pocitat nad ringem stage. */
static int allan_metric_kind(void)
{
    switch (s_allan_metric) {
    case 1:  return ADEV_KIND_MDEV;
    case 2:  return ADEV_KIND_HDEV;
    case 3:  return ADEV_KIND_MDEV;   /* TDEV se odvozuje z MDEV (definice) */
    default: return ADEV_KIND_ADEV;   /* 0 = ADEV, 4 = MTIE (odhad z ADEV) */
    }
}

/* Hodnota zobrazene metriky ze spocteneho estimatoru a tau. */
static float allan_metric_value(float tau, float base)
{
    switch (s_allan_metric) {
    case 3:  return tau * base * 0.5773503f;   /* TDEV = τ·MDEV/√3 (exaktni) */
    case 4:  return tau * base * 1.7320508f;   /* MTIE ~ √3·τ·ADEV (ODHAD) */
    default: return base;                      /* ADEV / MDEV / HDEV primo */
    }
}

/* Y rozsah [10^ymin .. 10^(ymin+dec)] dle metriky. ADEV pevny (10⁻⁶..10⁻¹⁰ jako
 * drive); TDEV/MTIE AUTO-RANGE dle skutecnych hodnot — jejich magnituda je
 * nepredvidatelna (τ·ADEV nasobky), pevny rozsah by krivku uspal na okraj osy. */
static void allan_metric_yrange(const float *vals, int np, int *ymin, int *dec)
{
    if (s_allan_metric == 0) { *ymin = ALLAN_Y_MIN; *dec = ALLAN_Y_DEC; return; }
    float lo = 1e30f, hi = -1e30f;
    for (int i = 0; i < np; i++) {
        if (vals[i] <= 0.0f) continue;
        float l = log10f(vals[i]);
        if (l < lo) lo = l;
        if (l > hi) hi = l;
    }
    if (lo > hi) { *ymin = -12; *dec = 5; return; }   /* fallback: zadna platna data */
    int y0 = (int)floorf(lo) - 1;                     /* 1 dekada rezervy dole */
    int y1 = (int)ceilf(hi) + 1;                      /* 1 dekada rezervy nahore */
    int d = y1 - y0; if (d < 2) d = 2; else if (d > 8) d = 8;
    *ymin = y0; *dec = d;
}

/* Popisek dekady "10⁻N" (horni index). Nahrazuje pevne SCR_ALLAN_Y_TICKS —
 * pro ADEV vraci identicke retezce, pro TDEV/MTIE dekady jineho rozsahu. */
static void allan_ylabel(char *buf, size_t n, int exp)
{
    static const char *const SUP[10] = {"⁰","¹","²","³","⁴","⁵","⁶","⁷","⁸","⁹"};
    int ae = exp < 0 ? -exp : exp;
    /* ⚠️ Kazdy horni index je v UTF-8 TRI bajty, ne jeden. Exponent ma nejvyse
     * dve cifry (osa jde ~10⁻⁶..10⁻¹⁶), takze `es` potrebuje 2×3 + NUL = 7 B.
     * Drive tu bylo `es[12]`: vejit se to vzdy veslo, ale kompilator to dokazat
     * nemohl a pri -Os hlasil -Wformat-truncation (2 + 3 znamenko + az 11 z `es`).
     * Presna velikost z toho dela kontrolovatelny fakt misto predpokladu.
     * Cely popisek: "10" + znamenko(3) + es(6) + NUL = 12 = `ylb[12]` u volajiciho. */
    char es[7]; es[0] = '\0';
    if (ae >= 10) strcat(es, SUP[(ae / 10) % 10]);
    strcat(es, SUP[ae % 10]);
    snprintf(buf, n, "10%s%s", exp < 0 ? "⁻" : "⁺", es);
}

/* Log-hodnota metriky -> pixel y v 'inner' (Y rozsah [ymin..ymin+dec] dekad). */
static int16_t allan_y(prim_rect_t inner, float log_val, int ymin, int dec)
{
    float ly = (log_val - (float)ymin) / (float)dec;
    if (ly < 0.0f) ly = 0.0f; else if (ly > 1.0f) ly = 1.0f;
    return (int16_t)(inner.y + (1.0f - ly) * inner.h);
}

/* Konfidencni pas (efekt FX_ALLAN_CONF): meke accent podbarveni mezi horni
 * (yup) a dolni (ylo) mezi ADEV odhadu. Per-sloupec svisla vypln mezi
 * interpolovanymi mezemi (np<=20 bodu -> levne). Kresli se POD krivku. */
static void allan_band_fill(const prim_point_t *pts, const int16_t *yup,
                            const int16_t *ylo, int np)
{
    for (int i = 1; i < np; i++) {
        int16_t x0 = pts[i - 1].x, x1 = pts[i].x;
        int16_t cols = (int16_t)(x1 - x0);
        if (cols < 1) cols = 1;
        for (int16_t c = 0; c <= cols; c++) {
            int16_t cx = (int16_t)(x0 + c);
            int16_t yu = (int16_t)(yup[i - 1] + (int32_t)(yup[i] - yup[i - 1]) * c / cols);
            int16_t yl = (int16_t)(ylo[i - 1] + (int32_t)(ylo[i] - ylo[i - 1]) * c / cols);
            if (yl < yu) { int16_t t = yu; yu = yl; yl = t; }
            prim_fill_rect((prim_rect_t){cx, yu, 1, (int16_t)(yl - yu + 1)},
                           PRIM_ALPHA(UI_COLOR_ACC, 0x22), PRIM_BLEND_OVER);
        }
    }
}

static void allan_plot_curve(prim_rect_t inner, const float *taus,
                             const float *vals, const int *ns, int np,
                             int16_t marker_r, int ymin, int dec)
{
    float lmin = log10f(taus[0]);                   /* nejkratsi tau = levy okraj */
    float lmax = log10f(taus[np - 1]);              /* nejdelsi tau = pravy okraj */
    float xspan = lmax - lmin;
    if (xspan < 1e-6f) xspan = 1.0f;
    prim_point_t pts[20];
    int16_t yup[20], ylo[20];
    if (np > 20) np = 20;
    for (int i = 0; i < np; i++) {
        float fx = (log10f(taus[i]) - lmin) / xspan;            /* 0..1 pres sirku */
        pts[i].x = (int16_t)(inner.x + fx * inner.w);
        pts[i].y = allan_y(inner, log10f(vals[i]), ymin, dec);
        /* Konfidencni mez: rel. pulsirka ~ 0,8/sqrt(paru) (1. rad, white FM);
         * pro TDEV/MTIE stejna relativni nejistota (jsou τ·ADEV nasobky). */
        float f = 0.0f;
        if (ns) { int nd = ns[i] < 1 ? 1 : ns[i]; f = 0.8f / sqrtf((float)nd); if (f > 0.9f) f = 0.9f; }
        yup[i] = allan_y(inner, log10f(vals[i] * (1.0f + f)), ymin, dec);
        ylo[i] = allan_y(inner, log10f(vals[i] * (1.0f - f)), ymin, dec);
    }
    if (ns && (g_fx_enabled & FX_ALLAN_CONF))        /* pas POD krivku */
        allan_band_fill(pts, yup, ylo, np);
    for (int i = 1; i < np; i++)
        prim_draw_line(pts[i - 1], pts[i], 2, UI_COLOR_ACC);
    for (int i = 0; i < np; i++)                     /* marker v kazdem tau bode */
        prim_fill_circle(pts[i], marker_r, UI_COLOR_ACC);
}

/* Kompletni log-log ADEV graf do 'area' (Y+X mrizka, dekadove popisky, krivka).
 * Sdili KARTA na hlavni obrazovce a fullscreen okno ALLAN. Popisky mono_16 v
 * OBOU (2026-07-19: karta byla mono_14, sjednoceno kvuli citelnosti — soucast
 * TODO #11(2b), aplikovano cileně na Allan). Rezervy (resl/resb) jsou
 * napocitane na sirku "10⁻¹⁰" pri mono_16 (advance 10+10+7+10+10=47 px + 6 px
 * mezera k ose = min. 53 px; pouzito 58 (par px navic rezervy). 'big' dal
 * rozlisuje jen odsazeni (okno ma vic mista nez karta v hlavni mrizce).
 * Rezervy na popisky si funkce bere z 'area' sama. */
static void allan_plot(prim_rect_t area, int big)
{
    const prim_font_t *lf = &ui_font_mono_16;
    prim_color_t lc  = UI_COLOR_INK_3;
    int16_t resl = 58;                    /* leva rezerva na "10⁻¹⁰" (mono_16, viz komentar vyse) */
    int16_t resb = big ? 34 : 26;          /* dolni pruh na dekadove popisky τ */
    int16_t rest = big ? 12 : 0;          /* horni odsazeni (okno) */
    prim_rect_t in = {(int16_t)(area.x + resl), (int16_t)(area.y + rest),
                      (int16_t)(area.w - resl - 10), (int16_t)(area.h - rest - resb)};

    float taus[20], adevs[20];
    int ns[20];
    int np = adev_points(taus, adevs, ns, 20);
    if (np < 2) {                                   /* jeste neni dost vzorku -> hlaska */
        prim_draw_text((prim_point_t){(int16_t)(in.x + in.w / 2),
                                      (int16_t)(in.y + in.h / 2 + 5)},
                       "Waiting for data...", &ui_font_sans_16,
                       UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
        return;
    }

    /* Transformuj na zvolenou metriku (ADEV/TDEV/MTIE) + urci Y rozsah (TDEV/MTIE
     * auto-range dle hodnot). Y mrizka + dekadove popisky (allan_ylabel). */
    float vals[20];
    for (int i = 0; i < np; i++) vals[i] = allan_metric_value(taus[i], adevs[i]);
    int ymin, dec; allan_metric_yrange(vals, np, &ymin, &dec);
    for (int j = 0; j <= dec; j++) {
        int16_t y = (int16_t)(in.y + (int32_t)j * in.h / dec);
        prim_draw_line((prim_point_t){in.x, y},
                       (prim_point_t){(int16_t)(in.x + in.w), y}, 1, UI_COLOR_LINE);
        char ylb[12]; allan_ylabel(ylb, sizeof ylb, ymin + dec - j);
        prim_draw_text((prim_point_t){(int16_t)(in.x - 6), (int16_t)(y + 5)},
                       ylb, lf, lc, PRIM_ALIGN_RIGHT);
    }

    /* X mrizka + popisky v MOCNINACH 10 — jen dekady v [tau_min..tau_max]. */
    float lmin = log10f(taus[0]);                   /* nejkratsi tau = levy okraj */
    float lmax = log10f(taus[np - 1]);              /* nejdelsi tau = pravy okraj */
    float xspan = lmax - lmin;
    if (xspan < 1e-6f) xspan = 1.0f;
    for (int e = (int)ceilf(lmin); e <= (int)floorf(lmax); e++) {
        float fx = ((float)e - lmin) / xspan;
        int16_t x = (int16_t)(in.x + fx * in.w);
        prim_draw_line((prim_point_t){x, in.y},
                       (prim_point_t){x, (int16_t)(in.y + in.h)}, 1, UI_COLOR_LINE);
        char dl[8];                                 /* tau_min >= 1 s -> e >= 0 (10^e) */
        long v = 1;
        for (int j = 0; j < e; j++) v *= 10;
        snprintf(dl, sizeof(dl), "%ld", v);
        prim_draw_text((prim_point_t){x, (int16_t)(in.y + in.h + (big ? 18 : 16))},
                       dl, lf, lc, PRIM_ALIGN_CENTER);
    }

    allan_plot_curve(in, taus, vals, ns, np, 3, ymin, dec);
}

/* Allan karta na hlavni obrazovce: vlevo pres vysku statistik+trendu (364×176,
 * finalni tvar 2026-07-19 — vyssi nez docasny nahled 100 px, ale ne pres celou
 * mrizku jako puvodne 242). MA plne osy (mono_14); jeste vetsi graf + σy(τ)
 * tabulku ma fullscreen okno ALLAN po tapu ('↗' v headeru). Header nese zivou
 * σy@1s. */
static void render_card_allan(prim_rect_t rect)
{
    s_allan_rect = rect;                          /* pro zive prekresleni */
    char hdr[40];
    char sig[24]; fmt_frac(sig, sizeof(sig), stats_adev(1), 0);  /* σy@1s (τ=1s, vzorky 1/s) */
    snprintf(hdr, sizeof(hdr), "@1s %s", sig);
    ui_card_t card = {.rect = rect, .header_label = "Allan σy(τ)",
                      .header_right = hdr,
                      .header_right_accent = UI_COLOR_ACC};
    ui_card_render_chrome(&card);
    /* Ztlumene '↗' hned za titulkem = naznak ze karta je klikaci (tap -> ALLAN okno). */
    int16_t hx = (int16_t)(rect.x + UI_DIM_CARD_PAD_X
                           + prim_text_width("Allan σy(τ)", &ui_font_sans_18) + 6);
    prim_draw_text((prim_point_t){hx, (int16_t)(rect.y + UI_DIM_CARD_PAD_Y + 16)},
                   "↗", &ui_font_sans_18, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
    /* Karta na hlavni obrazovce = VZDY ADEV (σy(τ)) preview — header je pevny
     * "Allan σy(τ)". TDEV/MTIE se prepina jen v okne ALLAN (s_allan_metric je
     * globalni, tak ho na dobu renderu karty docasne srovnam na ADEV). */
    int save = s_allan_metric; s_allan_metric = 0;
    allan_plot(ui_card_inner_rect(&card), 0);
    s_allan_metric = save;
}

/* Jedna mala karta: header label + hodnota. Hodnota mono_18 (2026-07-19, bylo
 * mono_16 — karty se roztahly na 1/3 sirky, takze je misto; mono_18 MA horni
 * indexy ⁰..⁹⁻ pro fmt_frac). Baseline in.y+15: glyf 18px zacina AZ POD
 * descenty headeru (sans_18 konci ~y_karty+30, glyf top = +32). */
static void draw_stat_card(prim_rect_t r, const char *label, const char *val, prim_color_t c)
{
    ui_card_t card = {.rect = r, .header_label = label};
    ui_card_render_chrome(&card);
    prim_rect_t in = ui_card_inner_rect(&card);
    /* Klasicke rozlozeni ma uzsi karty -> mensi font a jina baseline (viz
     * komentar u `screen_main_set_layout_classic`). */
    if (s_layout_classic)
        prim_draw_text((prim_point_t){in.x, (int16_t)(in.y + 11)}, val,
                       &ui_font_mono_16, c, PRIM_ALIGN_LEFT);
    else
        prim_draw_text((prim_point_t){in.x, (int16_t)(in.y + 15)}, val,
                       &ui_font_mono_18, c, PRIM_ALIGN_LEFT);
}

/* ── Eased Offset/σ/Drift (item 2) ───────────────────────────────────────────
 * Karty se plne prekresluji 1x/s (screen_main_redraw_stats). Misto okamziteho
 * skoku na novou hodnotu drzi 3 anim_t (raw float, PRED formatovanim fmt_frac)
 * — 20Hz tik (screen_main_tick_stats_anim) je plynule dojede a mezitim
 * prekresluje JEN hodnotu (box clear + text), ne cely chrome karty.
 * `stats_anim_resync()` se vola PRED plnym screen renderem (render_body_grid)
 * -> pri navratu na hlavni obrazovku po case v podnabidce se cislo NEukaze
 * zastarale (nedojete od doby, kdy tik nebezel), ale rovnou spravne. */
static anim_t     s_anim_off, s_anim_sig, s_anim_drift;
static prim_rect_t s_stat_card_rect[3];
static char        s_stat_cache[3][24];   /* cache = velikost zdroje (off/s1/dr[24]), viz STATUS.md #13 */

static void stats_anim_resync(void)
{
    anim_reset(&s_anim_off,   stats_mean(8));
    anim_reset(&s_anim_sig,   stats_adev(1));
    anim_reset(&s_anim_drift, stats_drift());
}

/* TRI uzke karty ze statistiky: Offset (klouzavy prumer y), σy@1s (ADEV tau=1s),
 * Drift (df/dt — rychlost ujizdeni kmitoctu). Hodnoty jsou EASED (s_anim_*.cur,
 * anim_set tady jen nastavi novy cil — krok dela 20Hz tik). */
static void draw_offset_sigma(prim_rect_t rect)
{
    s_small_rect = rect;                          /* pro zive prekresleni */
    int16_t gap = SCR_MAIN_CARD_SECTION_GAP;
    int16_t w   = (int16_t)((rect.w - 2 * gap) / 3);
    s_stat_card_rect[0] = (prim_rect_t){rect.x, rect.y, w, rect.h};
    s_stat_card_rect[1] = (prim_rect_t){(int16_t)(rect.x + w + gap), rect.y, w, rect.h};
    s_stat_card_rect[2] = (prim_rect_t){(int16_t)(rect.x + 2 * (w + gap)), rect.y, w, rect.h};

    char off[24], s1[24], dr[24];
    if (s_layout_classic) {
        /* Zamrzla vetev: presny puvodni RAW vypocet, bez easingu (a tedy i bez
         * `s_stat_cache`, ktery plni jen 20Hz tik hybridniho rozlozeni). */
        fmt_frac(off, sizeof(off), stats_mean(8), 1);
        fmt_frac(s1,  sizeof(s1),  stats_adev(1), 0);   /* σy@1s (τ=1s, 1/s) */
        fmt_frac(dr,  sizeof(dr),  stats_drift(), 1);   /* df/dt [1/s] */
    } else {
        anim_set(&s_anim_off,   stats_mean(8));
        anim_set(&s_anim_sig,   stats_adev(1));
        anim_set(&s_anim_drift, stats_drift());
        fmt_frac(off, sizeof(off), s_anim_off.cur,   1);
        fmt_frac(s1,  sizeof(s1),  s_anim_sig.cur,   0);   /* σy@1s (τ=1s, 1/s) */
        fmt_frac(dr,  sizeof(dr),  s_anim_drift.cur, 1);   /* df/dt [1/s] */
        strcpy(s_stat_cache[0], off); strcpy(s_stat_cache[1], s1); strcpy(s_stat_cache[2], dr);
    }
    draw_stat_card(s_stat_card_rect[0], SCR_S_OFFSET_L, off, UI_COLOR_OK);
    draw_stat_card(s_stat_card_rect[1], "σy 1s", s1, UI_COLOR_VIOLET);
    draw_stat_card(s_stat_card_rect[2], "Drift/s", dr, UI_COLOR_ACC);
}

/* Hodnota jedne stat karty bez chrome (jen box clear + text) — pro 20Hz partial
 * update mezi 1Hz plnymi redrawy vyse. Geometrie/baseline shodne s draw_stat_card. */
static void draw_stat_card_value(int idx, const char *val, prim_color_t c)
{
    prim_rect_t r = s_stat_card_rect[idx];
    ui_card_t card = {.rect = r};
    prim_rect_t in = ui_card_inner_rect(&card);
    int16_t base = (int16_t)(in.y + 15);            /* v2 only (viz tick nize) */
    prim_fill_rect((prim_rect_t){in.x, (int16_t)(base - 16), in.w, 22},
                   UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_draw_text((prim_point_t){in.x, base}, val, &ui_font_mono_18, c, PRIM_ALIGN_LEFT);
}

/* ~20 Hz z app_gpsdo tick_anim (s_view=0): eased dojezd Offset/σ/Drift. Jen v2
 * layout a jen kdyz karty uz byly aspon jednou vykresleny (rect.w>0) a mereni
 * bezi (STOP zamrzne stats jako jinde). Vraci 1 pokud neco prekreslil. */
int screen_main_tick_stats_anim(void)
{
    if (s_layout_classic) return 0;   /* zamrzla vetev — bez easingu (viz set_layout_classic) */
    if (!screen_main_is_running()) return 0;
    if (s_stat_card_rect[0].w == 0) return 0;

    int m0 = anim_step(&s_anim_off,   0.15f, 1e-13f);
    int m1 = anim_step(&s_anim_sig,   0.15f, 1e-13f);
    int m2 = anim_step(&s_anim_drift, 0.15f, 1e-13f);

    int drew = 0;
    char buf[24];
    if (m0) {
        fmt_frac(buf, sizeof buf, s_anim_off.cur, 1);
        if (strcmp(buf, s_stat_cache[0]) != 0) {
            strcpy(s_stat_cache[0], buf); draw_stat_card_value(0, buf, UI_COLOR_OK); drew = 1; }
    }
    if (m1) {
        fmt_frac(buf, sizeof buf, s_anim_sig.cur, 0);
        if (strcmp(buf, s_stat_cache[1]) != 0) {
            strcpy(s_stat_cache[1], buf); draw_stat_card_value(1, buf, UI_COLOR_VIOLET); drew = 1; }
    }
    if (m2) {
        fmt_frac(buf, sizeof buf, s_anim_drift.cur, 1);
        if (strcmp(buf, s_stat_cache[2]) != 0) {
            strcpy(s_stat_cache[2], buf); draw_stat_card_value(2, buf, UI_COLOR_ACC); drew = 1; }
    }
    return drew;
}

/* fwd — definovano nize (partial-redraw clear z bg_cache); potrebuje ho uz
 * screen_main_tick_trend_anim (item 4, eased sparkline). */
static void blit_bg_region(prim_rect_t r);

#define SPARK_N 21
static int16_t s_spark[SPARK_N];

/* ── Eased trend sparkline (item 4) ──────────────────────────────────────────
 * Misto okamziteho skoku krivky na novou sadu bodu kazdou sekundu (screen_main
 * _redraw_stats) drzime `s_spark_prev` (naposledy VYKRESLENA sada) a plynule
 * (20Hz tik) interpolujeme k nove `s_spark` (cil) pres TREND_ANIM_STEPS kroku
 * (~1s). Sigma band + header p-p NEjsou eased (tise se prebarvi az pri
 * dalsim 1Hz redrawu — subtilni podbarveni, na rozdil od krivky nerusi).
 * `trend_anim_resync()` (volano PRED render_card_trend z render_body_grid,
 * stejny vzor jako stats_anim_resync) zajisti, ze FULL render (otevreni okna /
 * navrat z podnabidky) ukaze cil OKAMZITE, ne zastarale nedojete misto. */
#define TREND_ANIM_STEPS 20
static int16_t     s_spark_prev[SPARK_N];
static prim_rect_t s_trend_inner;
static int         s_trend_n         = 0;
static int16_t     s_trend_sig_lo, s_trend_sig_hi;
static int         s_trend_phase     = TREND_ANIM_STEPS;   /* STEPS = "dojeto", tik je no-op */
static int         s_trend_resync_pending = 0;

/* ── Guard "kresba by byla BIT-IDENTICKA" (optimalizace 2026-08-30) ────────────
 * `screen_main_tick_trend_anim` bezel 20x/s a POKAZDE delal `blit_bg_region`
 * cele plochy grafu (398x100 = 80 kB pres DMA2D) + `trend_plot_draw` (sigma pas,
 * polyline s CPU antialiasingem, area vypln). Pritom interpolace mezi dvema
 * temer shodnymi sadami bodu se casto zaokrouhli na TYZ pixelovy prubeh —
 * kresba je pak bit za bitem stejna a je to cista prace navic. Pri stabilnim
 * GPSDO (coz je bezny stav) to plati pro vetsinu z 20 kroku dojezdu.
 * ⚠️ Klic MUSI obsahovat VSE, co kresbu ovlivnuje: body, meze sigma pasu
 * i priznak area vyplne — jinak by se preskocila zmena, ktera je videt.
 * (Ostatni 20Hz tiky uz svuj guard maly: stats pres `strcmp` formatovaneho
 * textu, headline pres per-segment `strcmp`, sys xfade pres `s_sys_mix`.) */
static int16_t     s_trend_drawn[SPARK_N];
static int         s_trend_drawn_n = -1;            /* -1 = neplatne -> kresli vzdy */
static int16_t     s_trend_drawn_lo, s_trend_drawn_hi;
static uint8_t     s_trend_drawn_fx;
/* Kolikrat uz se PRAVE TENTO obsah nakreslil. Preskakovat se smi az od
 * `prim_stm32_fb_count()` — do te doby ho nemaji vsechny buffery. */
static int8_t      s_trend_reps;

static uint8_t trend_fx_key(void) { return (uint8_t)((g_fx_enabled & FX_SPARK_FILL) != 0); }

static int trend_drawn_same(const int16_t *arr, int n, int16_t lo, int16_t hi)
{
    return s_trend_drawn_n == n && s_trend_drawn_lo == lo && s_trend_drawn_hi == hi
        && s_trend_drawn_fx == trend_fx_key()
        && memcmp(s_trend_drawn, arr, sizeof(int16_t) * (size_t)n) == 0;
}

static void trend_drawn_store(const int16_t *arr, int n, int16_t lo, int16_t hi)
{
    if (n > SPARK_N) n = SPARK_N;
    memcpy(s_trend_drawn, arr, sizeof(int16_t) * (size_t)n);
    s_trend_drawn_n  = n;
    s_trend_drawn_lo = lo;
    s_trend_drawn_hi = hi;
    s_trend_drawn_fx = trend_fx_key();
}

/* Zneplatni guard — po zmene tematu/palety (bg_cache i barvy jsou jine) nebo
 * kdykoli plocha grafu prestane platit. */
static void trend_drawn_invalidate(void) { s_trend_drawn_n = -1; s_trend_reps = 0; }

static void trend_anim_resync(void) { s_trend_resync_pending = 1; }

/* Sdileny obsah plochy grafu: sigma band + polyline + koncovy bod + regresni
 * cara. Pouziva jak plny render (nize), tak 20Hz eased tik (interpolovane
 * hodnoty). Regresni cara je LSQ fit arr[k] vs k — mapovani 1:1 se sparkline. */
static void trend_plot_draw(prim_rect_t inner, const int16_t *arr, int n,
                           int16_t sig_lo, int16_t sig_hi)
{
    ui_sparkline_t sp = {.inner = inner, .y_values = arr, .count = (int16_t)n,
        .show_sigma_band = true, .sigma_min = sig_lo, .sigma_max = sig_hi,
        .show_endpoint_marker = true,
        .fill_below = (g_fx_enabled & FX_SPARK_FILL) != 0,   /* area chart vypln (efekt) */
        .stroke_color = UI_COLOR_ACC};
    ui_sparkline_render(&sp);

    float si = 0, sv = 0, sii = 0, siv = 0;
    for (int k = 0; k < n; k++) {
        si += (float)k; sv += (float)arr[k];
        sii += (float)k * (float)k; siv += (float)k * (float)arr[k];
    }
    float denom = (float)n * sii - si * si;
    if (denom > 1e-3f || denom < -1e-3f) {
        float slope = ((float)n * siv - si * sv) / denom;
        float inter = (sv - slope * si) / (float)n;
        float v0 = inter, v1 = inter + slope * (float)(n - 1);
        if (v0 < 0) v0 = 0;
        if (v0 > 255) v0 = 255;
        if (v1 < 0) v1 = 0;
        if (v1 > 255) v1 = 255;
        int16_t y0 = (int16_t)(inner.y + inner.h - 1 - (int)(v0 * (inner.h - 1) / 255.0f));
        int16_t y1 = (int16_t)(inner.y + inner.h - 1 - (int)(v1 * (inner.h - 1) / 255.0f));
        prim_draw_line((prim_point_t){inner.x, y0},
                       (prim_point_t){(int16_t)(inner.x + inner.w - 1), y1},
                       1, UI_COLOR_OK);
    }
}

/* ~20 Hz z app_gpsdo tick_anim (s_view=0): eased dojezd sparkline krivky.
 * Prekresluje JEN plochu grafu (blit_bg_region + trend_plot_draw), ne cely
 * chrome karty. Konci sam (vraci 0), kdyz uz je fazi na cili. */
int screen_main_tick_trend_anim(void)
{
    if (s_layout_classic) return 0;   /* zamrzla vetev — bez easingu (viz set_layout_classic) */
    if (!screen_main_is_running()) return 0;
    if (s_trend_n == 0 || s_trend_phase >= TREND_ANIM_STEPS) return 0;

    s_trend_phase++;
    float t = (float)s_trend_phase / (float)TREND_ANIM_STEPS;
    int16_t disp[SPARK_N];
    for (int i = 0; i < s_trend_n; i++)
        disp[i] = (int16_t)(s_spark_prev[i] + (s_spark[i] - s_spark_prev[i]) * t + 0.5f);

    /* ⚠️ Ucetnictvi fáze se MUSI dokoncit i kdyz se nekresli (early return nize),
     * jinak by dalsi cyklus interpoloval od spatneho vychoziho bodu. */
    if (s_trend_phase >= TREND_ANIM_STEPS)             /* dojeto -> dalsi cyklus interpoluje ODSUD */
        memcpy(s_spark_prev, s_spark, sizeof(int16_t) * (size_t)s_trend_n);

    /* Interpolace se zaokrouhlila na TYZ pixelovy prubeh -> kresba by byla
     * bit-identicka. Preskoc ji (usetri blit 398x100 + CPU AA polyline) a
     * NEhlas zmenu, aby se kvuli tomu ani neflipoval snimek.
     *
     * 🔴 ALE AZ TEHDY, KDYZ UZ OBSAH MA KAZDY FRAMEBUFFER (`s_trend_reps`).
     * Jinak zustane jen v tom jednom, do ktereho se zrovna kreslilo, a jakmile
     * se cyklus dostane na ostatni, ukazou starsi krivku = PROBLIKAVANI.
     * ⚠️ Copy-forward to nezachrani: kopiruje sjednoceni dirty z poslednich
     * DVOU snimku, jenze kdyz se kvuli preskoceni neflipuje, dirty rect toho
     * jedineho kresleni z te historie vypadne driv, nez ho ostatni buffery
     * dostanou. (Prave tim jsem 2026-08-30 zpusobil problikavani trendu.) */
    int same = trend_drawn_same(disp, s_trend_n, s_trend_sig_lo, s_trend_sig_hi);
    if (same && s_trend_reps >= prim_stm32_fb_count()) return 0;

    blit_bg_region(s_trend_inner);
    trend_plot_draw(s_trend_inner, disp, s_trend_n, s_trend_sig_lo, s_trend_sig_hi);
    if (same) {
        if (s_trend_reps < 127) s_trend_reps++;    /* tentyz obsah do dalsiho bufferu */
    } else {
        trend_drawn_store(disp, s_trend_n, s_trend_sig_lo, s_trend_sig_hi);
        s_trend_reps = 1;                          /* novy obsah -> zatim v jednom bufferu */
    }
    return 1;
}

static void render_card_trend(prim_rect_t rect)
{
    s_trend_rect = rect;                          /* pro zive prekresleni */

    /* Sparkline: SPARK_N bodu pres POSLEDNICH 60 s (TREND_WIN), vlevo=nejstarsi.
     * (Buffer je delsi kvuli Allan, ale trend zustava 60s okno.) PEVNE meritko ±FS
     * -> stred 128 (neposkakuje pri sliding window). */
    const float FS = 4e-8f;                       /* full-scale frakcni odchylky */
    int win = (s_y_count < TREND_WIN) ? s_y_count : TREND_WIN;   /* posledni 60s okno */
    int n = (win < SPARK_N) ? win : SPARK_N;
    if (n < 2) n = 2;
    float sum = 0;
    for (int k = 0; k < n; k++) {
        int age = (n - 1 - k) * ((win > 1 ? win - 1 : 1)) / (n - 1);
        float v = stat_at(age);
        sum += v;
        float u = (v / FS + 1.0f) * 0.5f;         /* -FS..+FS -> 0..1 */
        if (u < 0.0f) u = 0.0f;
        if (u > 1.0f) u = 1.0f;
        s_spark[k] = (int16_t)(40.0f + u * 175.0f);
    }
    float mean = sum / (float)n, sd = stats_adev(1);   /* σy@1s pro sigma band */
    float ulo = ((mean - sd) / FS + 1.0f) * 0.5f;
    float uhi = ((mean + sd) / FS + 1.0f) * 0.5f;
    if (ulo < 0.0f) ulo = 0.0f;
    if (uhi > 1.0f) uhi = 1.0f;
    int16_t sig_lo = (int16_t)(40.0f + ulo * 175.0f);
    int16_t sig_hi = (int16_t)(40.0f + uhi * 175.0f);

    /* Header vpravo: p-p celeho 60s okna. */
    static char tr[48];
    char pp[24]; fmt_frac(pp, sizeof(pp), stats_pp(win), 0);
    snprintf(tr, sizeof(tr), "● %s p-p", pp);

    ui_card_t card = {.rect = rect, .header_label = SCR_S_TREND_L,
                      .header_right = tr, .header_right_accent = UI_COLOR_OK};
    ui_card_render_chrome(&card);
    /* Ztlumene '↗' za titulkem = naznak klikatelnosti (tap -> fullscreen trend). */
    int16_t hx = (int16_t)(rect.x + UI_DIM_CARD_PAD_X
                           + prim_text_width(SCR_S_TREND_L, &ui_font_sans_18) + 6);
    prim_draw_text((prim_point_t){hx, (int16_t)(rect.y + UI_DIM_CARD_PAD_Y + 16)},
                   "↗", &ui_font_sans_18, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
    prim_rect_t inner = ui_card_inner_rect(&card);

    if (s_layout_classic) {          /* zamrzla vetev — beze zmeny, bez easingu */
        trend_plot_draw(inner, s_spark, n, sig_lo, sig_hi);
        return;
    }

    s_trend_inner = inner; s_trend_sig_lo = sig_lo; s_trend_sig_hi = sig_hi;
    /* Zmena poctu bodu (n) by interpolaci mezi ruzne dlouhymi poli rozbila —
     * stejne jako resync/vypnute animace jen skoc rovnou na cil. */
    if (s_trend_resync_pending || !g_anim_enabled || n != s_trend_n) {
        s_trend_n = n;
        memcpy(s_spark_prev, s_spark, sizeof(int16_t) * (size_t)n);
        s_trend_resync_pending = 0;
        s_trend_phase = TREND_ANIM_STEPS;           /* uz na cili */
    } else {
        s_trend_phase = 0;                          /* novy cil -> rozjed dojezd od s_spark_prev */
    }
    trend_plot_draw(inner, s_spark_prev, s_trend_n, sig_lo, sig_hi);
    /* Guard 20Hz tiku musi vedet, CO je ted na obrazovce — jinak by prvni tik
     * po plnem renderu prekreslil totez znovu (nebo, hur, preskocil zmenu). */
    trend_drawn_store(s_spark_prev, s_trend_n, sig_lo, sig_hi);
}

/* Signal bargraf = REALNY vstupni vykon z AD8307 log-detektoru (ADS1115 AIN1;
 * SensorsTask fast-path ~10 Hz), zobrazeny v dBm. Prevod mV->dBm dela volajici
 * (app_gpsdo_tick_signal, konstanty AD8307). Drzime rect + hodnotu pro partial redraw. */
static prim_rect_t s_signal_rect = {0, 0, 0, 0};
static int16_t     s_signal_pct  = 0;
static int32_t     s_signal_dbm10 = -100000;  /* posl. zobrazene dBm×10 (<-99999 = jeste nic) */

/* Naformatuje dBm×10 do "-45.5 dBm" (bez %f); pro sentinel "--- dBm". */
static void fmt_dbm(char *buf, int n, int32_t dbm10)
{
    if (dbm10 <= -100000) { snprintf(buf, n, "--- dBm"); return; }
    long a = dbm10 < 0 ? -dbm10 : dbm10;
    snprintf(buf, n, "%s%ld.%ld dBm", dbm10 < 0 ? "-" : "", a / 10, a % 10);
}

/* Plne vykresleni signal karty (chrome + label + bar) — pro full render. */
static void draw_signal_card(prim_rect_t rect, int16_t pct)
{
    static char val[24];
    ui_card_t card = {.rect = rect};
    ui_card_render_chrome(&card);
    prim_rect_t inner = ui_card_inner_rect(&card);
    fmt_dbm(val, sizeof val, s_signal_dbm10);
    ui_bargraph_t bar = {.rect = inner, .value_pct = pct,
                         .color = UI_COLOR_OK, .label = SCR_S_SIGNAL_L,
                         .value_text = val};
    ui_bargraph_render(&bar);
}

static void render_card_signal(prim_rect_t rect)
{
    s_signal_rect = rect;                 /* zapamatuj pro partial redraw */
    draw_signal_card(rect, s_signal_pct);
}

/* Mrizka = HYBRID (layout pro 4,3" panel, finalni tvar 2026-07-19):
 *   vlevo  Allan graf pres CELOU vysku mrizky (364×242 — plne osy s popisky;
 *          predtim koncil nad signal radkem, ted se roztahl az dolu, protoze
 *          signal bargraf uz nejede pres levou cast),
 *   vpravo statistiky Offset/σ@1s/Drift (3× 125×64, hodnoty mono_18),
 *          POD nimi mini trend (398×100, tap -> fullscreen okno),
 *          a UPLNE DOLE RF signal bargraf (398×54) — bargraf je JEN v prave
 *          casti (drive pres celou sirku 776 px; zuzeno na pozadavek
 *          "bargraf jen v prave casti").
 * Pozn.: horni hrana mrizky MUSI zustat na SCR_MAIN_GRID_Y (166) — clear
 * oblast velkeho cisla (redraw_freq, s_num_top+88) konci presne na ni. */
/* ── KLASICKE rozlozeni (rekonstrukce puvodniho stavu pred auditem pro 4,3") ──
 * Allan 53 % sirky, pravy sloupec stohovany offset(v.54, mono_16) / trend /
 * signal(v.43), vsechny mezery `SCR_MAIN_CARD_SECTION_GAP`.
 * ⚠️ Pomer 53 je HARDCODED literal (ne makro — `SCR_MAIN_GRID_LEFT_RATIO` uz
 * slouzi hybridnimu rozlozeni s hodnotou 47). */
static void render_right_column_classic(prim_rect_t rect)
{
    int16_t gap = SCR_MAIN_CARD_SECTION_GAP;
    int16_t small_h = 54;
    int16_t signal_h = 43;
    int16_t trend_h = (int16_t)(rect.h - small_h - signal_h - 2 * gap);
    int16_t y = rect.y;
    draw_offset_sigma((prim_rect_t){rect.x, y, rect.w, small_h});
    y = (int16_t)(y + small_h + gap);
    render_card_trend((prim_rect_t){rect.x, y, rect.w, trend_h});
    y = (int16_t)(y + trend_h + gap);
    render_card_signal((prim_rect_t){rect.x, y, rect.w, signal_h});
}

static void render_body_grid_classic(void)
{
    int16_t right_margin = SCR_MAIN_GRID_MARGIN;
    int16_t allan_left = right_margin;
    int16_t grid_y = SCR_MAIN_GRID_Y;
    int16_t grid_h = (int16_t)(UI_DIM_BODY_H - (SCR_MAIN_GRID_Y - UI_DIM_BODY_Y) - 8);
    int16_t grid_w = (int16_t)(UI_DIM_SCREEN_W - right_margin - allan_left);
    int16_t left_w = (int16_t)((grid_w * 53) / 100);            /* puvodni pomer, pred zuzenim na 47 */
    int16_t right_w = (int16_t)(grid_w - left_w - SCR_MAIN_GRID_GAP);
    int16_t left_x = allan_left;
    int16_t right_x = (int16_t)(left_x + left_w + SCR_MAIN_GRID_GAP);
    render_card_allan((prim_rect_t){left_x, grid_y, left_w, grid_h});
    render_right_column_classic((prim_rect_t){right_x, grid_y, right_w, grid_h});
}

static void render_body_grid_hybrid(void)
{
    int16_t m       = SCR_MAIN_GRID_MARGIN;   /* vnejsi okraj (obe strany) */
    int16_t gap     = 12;                     /* svisla mezera */
    int16_t grid_y  = SCR_MAIN_GRID_Y;
    int16_t grid_h  = (int16_t)(UI_DIM_BODY_H - (SCR_MAIN_GRID_Y - UI_DIM_BODY_Y) - 8);  /* 242 */
    int16_t grid_w  = (int16_t)(UI_DIM_SCREEN_W - 2 * m);                                /* 792 */
    int16_t signal_h = 54;
    int16_t stats_h  = 64;
    int16_t trend_h  = (int16_t)(grid_h - stats_h - signal_h - 2 * gap);                 /* 100 */
    int16_t allan_w  = (int16_t)((grid_w * SCR_MAIN_GRID_LEFT_RATIO) / 100);             /* 372 */
    int16_t right_x  = (int16_t)(m + allan_w + SCR_MAIN_GRID_GAP);                       /* 390 */
    int16_t right_w  = (int16_t)(grid_w - allan_w - SCR_MAIN_GRID_GAP);                  /* 406 */

    render_card_allan((prim_rect_t){m, grid_y, allan_w, grid_h});
    /* Resync PRED draw_offset_sigma: tohle je FULL render (window open / navrat
     * z podnabidky) -> ukaz cil OKAMZITE (zadny "dojezd od stare hodnoty" pri
     * prvnim zobrazeni). Periodicky 1Hz redraw (screen_main_redraw_stats) volne
     * draw_offset_sigma BEZ resyncu -> tam uz se hodnota eased dojizdi. */
    stats_anim_resync();
    trend_anim_resync();
    draw_offset_sigma((prim_rect_t){right_x, grid_y, right_w, stats_h});
    render_card_trend((prim_rect_t){right_x, (int16_t)(grid_y + stats_h + gap),
                                    right_w, trend_h});
    render_card_signal((prim_rect_t){right_x,
                                     (int16_t)(grid_y + stats_h + gap + trend_h + gap),
                                     right_w, signal_h});
}

/* Vyber rozlozeni — viz `screen_main_set_layout_classic`. */
static void render_body_grid(void)
{
    if (s_layout_classic) render_body_grid_classic();
    else                  render_body_grid_hybrid();
}

/* Label/value/variant of footer button i, derived from the UI state. */
static void footer_button_def(int i, const char **label, const char **value,
                              ui_button_variant_t *var)
{
    *value = 0;
    switch (i) {
    /* Slot 0 = PERIOD/FREQ toggle. Label = AKCE (co se stiskem zapne), ne stav:
     * v rezimu FREQUENCY nabizi "PERIOD" a naopak (MODE_NAME[st.mode ? 0 : 1]). */
    case 0: *label = MODE_NAME[st.mode ? 0 : 1]; *var = UI_BUTTON_NORMAL; break;
    /* Label = AKCE, ne stav (2026-07-20): bezi-li mereni, tlacitko nabizi "STOP"
     * (cervene), pri zastavenem nabizi "RUN" (zelene). Drive to bylo obracene
     * (label = stav) — matouci, protoze zelene "RUN" svitilo prave kdyz uz bezi.
     * Stav mereni nese navic PODBARVENI velkeho kmitoctu (freq_tint_if_stopped). */
    case 1: *label = st.running ? "STOP" : SCR_S_BTN_RUN;
            *var = st.running ? UI_BUTTON_STOP : UI_BUTTON_RUN; break;
    case 2: *label = SCR_S_BTN_GATE_L; *value = GATE_VAL[st.gate];  *var = UI_BUTTON_NORMAL; break;
    case 3: *label = SCR_S_BTN_CHAN_L; *value = CHAN_NAME[st.chan]; *var = UI_BUTTON_NORMAL; break;
    default: *label = SCR_S_BTN_MENU;  *var = UI_BUTTON_NORMAL; break;
    }
}

/* Footer: PERIOD/FREQ toggle, RUN/STOP, GATE, CHAN, MENU. RUN is 1.5x wide. */
static void render_footer(void)
{
    int16_t fy = UI_DIM_SCREEN_H - UI_DIM_FOOTER_H;
    int16_t pad = 12;
    int16_t total_w = (int16_t)(UI_DIM_SCREEN_W - 2 * pad - 4 * UI_DIM_BUTTON_GAP);
    int16_t unit = (int16_t)(total_w * 10 / 55);            /* 5 slots, RUN = 1.5 */
    int16_t btn_h = (int16_t)(UI_DIM_FOOTER_H * 96 / 100);  /* >95 % of the row */
    int16_t btn_y = (int16_t)(fy + (UI_DIM_FOOTER_H - btn_h) / 2);
    int16_t x = pad;

    for (int i = 0; i < SCR_BTN_COUNT; i++) {
        int16_t w = (i == 1) ? (int16_t)(unit * 15 / 10) : unit;
        const char *l, *v; ui_button_variant_t var;
        footer_button_def(i, &l, &v, &var);
        ui_button_t b = {.rect = {x, btn_y, w, btn_h}, .variant = var,
                         .label = l, .value = v};
        ui_button_render(&b);
        s_btn_rect[i] = b.rect;
        x = (int16_t)(x + w + UI_DIM_BUTTON_GAP);
    }
}

/* Restore a rectangle from the static background cache (partial-redraw clear). */
static void blit_bg_region(prim_rect_t r)
{
    const prim_pixel_t *src = bg_cache + (int)r.y * SCR_MAIN_BG_CACHE_W + r.x;
    prim_blit(r, src, SCR_MAIN_BG_CACHE_W * (int16_t)sizeof(prim_pixel_t));
}

/* Redraw only the title row (clears it from the bg cache first). */
void screen_main_redraw_title(void)
{
    blit_bg_region((prim_rect_t){0, (int16_t)(SCR_MAIN_TITLE_Y - 18),
                                 UI_DIM_SCREEN_W, 28});
    render_body_title();
}

/* Cas + datum z RTC (LSE, disciplinovany GPS UTC). Tika plynule 1×/s i pri
 * ztrate fixu; pred prvnim GPS syncem "--:--:--" / "no GPS". Prekresli JEN oblast
 * casu a JEN kdyz se zmeni sekunda nebo datum (zadny zbytecny redraw -> zadny "px sum"). */
int screen_main_redraw_time(uint32_t ms_since_boot)
{
    (void)ms_since_boot;
    static char     last_time[16] = "";
    static char     last_date[26] = "";   /* "YYYY-MM-DD UTC+14" (datum + label zony) */
    static uint8_t  last_icons = 0xFF;

    /* Cas + datum z RTC (LSE, disciplinovany GPS) -> tika plynule i pri ztrate
     * fixu. Dokud nebyl srovnan z GPS, ukazuje "--:--:--" / "no GPS". */
    char tb[16], db[16];
    rtc_time_date(tb, db);

    /* Mikro-ikona mute (preskrtnuty reproduktor) primo vlevo od casu. Holdover
     * ukazuje AMBER "HOLD" pilulka (render_header), UTC je na radku data -> zadna
     * kolize s pilulkami (drivejsi "UTC"/"H" u casu do HOLD pilulky narazely). */
    uint8_t icons = (uint8_t)(g_sound_muted ? 1u : 0u);

    /* Radek data nese i label zony ("UTC+2") -> klic je SLOZENY retezec, aby se
     * prekreslil i pri zmene zony v Nastaveni beze zmeny samotneho data. */
    char dl[26]; snprintf(dl, sizeof dl, "%s %s", db, (const char *)g_tz_label);
    int tchg = (strcmp(tb, last_time) != 0);
    int dchg = (strcmp(dl, last_date) != 0);
    int ichg = (icons != last_icons);
    if (!tchg && !dchg && !ichg) return 0;
    strncpy(last_time, tb, sizeof last_time - 1); last_time[sizeof last_time - 1] = '\0';
    strncpy(last_date, dl, sizeof last_date - 1); last_date[sizeof last_date - 1] = '\0';
    last_icons = icons;
    strncpy(s_time_buf, tb, sizeof s_time_buf - 1); s_time_buf[sizeof s_time_buf - 1] = '\0';

    int16_t time_x = UI_DIM_SCREEN_W - SCR_MAIN_CLOCK_MARGIN;
    if (tchg || ichg) {   /* cas: baseline 23, glyf y 1..34 (datum @46 se nedotkne) */
        int16_t tw = prim_text_width(s_time_buf, &ui_font_mono_25);
        blit_bg_region((prim_rect_t){(int16_t)(time_x - tw - 26), 1, (int16_t)(tw + 32), 33});
        prim_draw_text((prim_point_t){time_x, 23}, s_time_buf, &ui_font_mono_25,
                       UI_COLOR_INK, PRIM_ALIGN_RIGHT);
        if (icons & 1u)   /* mute: preskrtnuty reproduktor tesne vlevo od casu */
            ui_icon_speaker_muted((prim_point_t){(int16_t)(time_x - tw - 22), 4}, 18, UI_COLOR_BAD);
    }
    if (dchg) {   /* datum + label zony (baseline 46, vpravo dole — mimo pilulky).
                   * Clear FIXNI sirkou (150 px > nejdelsi "YYYY-MM-DD UTC+14"):
                   * pri zkraceni labelu (UTC+14 -> UTC) by clear dle nove sirky
                   * nechal zbytky stareho delsiho textu. */
        blit_bg_region((prim_rect_t){(int16_t)(time_x - 150), 35, 156, 18});
        prim_draw_text((prim_point_t){time_x, 46}, dl, &ui_font_sans_14,
                       UI_COLOR_INK_3, PRIM_ALIGN_RIGHT);
    }
    return 1;
}

/* Dvouradkovy blok vytizeni CPU v headeru (mezi CAL pilulkou a hodinami, x 592..642):
 * CM7 (real, g_rtos_cpu_pct) NAHORE, CM4 DOLE — oboji nejmensim fontem (mono_14).
 * CM4 % (od 2026-08-14): CM4 meri VLASTNI idle-based zatez pres DWT (main.c),
 * publikuje ji v IPC heartbeatu (cm4_cpu_pct), CM7 ji cte (g_cm4_cpu_pct) ->
 * "CM4:xx%" kdyz zive / "CM4:--" (D2 ready, IPC ticho) / "CM4:off" (nenabehl). CM4 dnes
 * dela skoro nic -> typicky "4:0%"; s ETH/SCPI naskoci realna zatez sama.
 * ⚠️ CM4 mereni se projevi az po FLASHI bank2 aktualnim CM4 buildem. Zive z
 * tick_clock (change-detect na CM7 % i CM4 %). force=1 = plny render. */
#define CPU_HDR_R 642      /* pravy okraj bloku = tesne pred zonou hodin (datum od x=644) */
static uint32_t s_cpu_shown = 999;
static uint8_t  s_cm4_shown = 255;
static uint8_t  s_cm4_pct_shown = 255;   /* posledni vykreslene CM4 % (change-detect) */
/* Stav CM4 pro spodni radek: 0 = D2 ready ale IPC ticho ("CM4:--"), 1 = heartbeat
 * roste, CM4 mluvi pres IPC ("CM4:xx%"), 2 = nenabehl ("CM4:off"). */
static uint8_t cm4_state(void)
{
    if (g_cm4_absent) return 2;
    return g_cm4_alive ? 1u : 0u;
}
int screen_main_redraw_cpu(int force)
{
    uint32_t c7 = g_rtos_cpu_pct; if (c7 > 99) c7 = 99;
    uint8_t  c4st = cm4_state();
    uint32_t c4p = g_cm4_cpu_pct; if (c4p > 99) c4p = 99;
    if (!force && c7 == s_cpu_shown && c4st == s_cm4_shown && (uint8_t)c4p == s_cm4_pct_shown) return 0;
    s_cpu_shown = c7; s_cm4_shown = c4st; s_cm4_pct_shown = (uint8_t)c4p;
    blit_bg_region((prim_rect_t){582, 1, 61, 53});      /* podklad headeru pod blokem (konci na 643 < 644) */
    char l[12];
    prim_color_t col = (c7 < 70) ? UI_COLOR_OK : (c7 < 90) ? UI_COLOR_WARN : UI_COLOR_BAD;
    snprintf(l, sizeof l, "CM7:%lu%%", (unsigned long)c7);
    prim_draw_text((prim_point_t){CPU_HDR_R, 22}, l, &ui_font_mono_14, col, PRIM_ALIGN_RIGHT);
    /* CM4: "CM4:xx%" (barevne dle zateze, kdyz IPC ziva) / "CM4:--" (sede, D2 ready ale ticho)
     * / "CM4:off" (cervene, nenabehl). CM4 dnes dela skoro nic -> typicky "4:0%". */
    char l4[12]; prim_color_t col4;
    if (c4st == 1) {
        snprintf(l4, sizeof l4, "CM4:%lu%%", (unsigned long)c4p);
        col4 = (c4p < 70) ? UI_COLOR_OK : (c4p < 90) ? UI_COLOR_WARN : UI_COLOR_BAD;
    } else {
        snprintf(l4, sizeof l4, "%s", (c4st == 2) ? "CM4:off" : "CM4:--");
        col4 = (c4st == 2) ? UI_COLOR_BAD : UI_COLOR_INK_4;
    }
    prim_draw_text((prim_point_t){CPU_HDR_R, 45}, l4, &ui_font_mono_14, col4, PRIM_ALIGN_RIGHT);
    return 1;
}

/* Partial redraw horni listy z GPS: blitne header strip z bg cache a prekresli
 * pilulky (GNSS lock + pocet druzic) + cas/datum. Volat jen pri zmene GPS stavu
 * (sat/fix) — render_header cte gps_get() sam. */
int screen_main_redraw_header(void)
{
    blit_bg_region((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_HEADER_H});
    render_header();
    return 1;
}

/* Efekt FX_SYS_XFADE: po zmene urovne SYS pilulky (render_header nastavil
 * s_sys_mix=0) plynule prolne jeji barvu behem ~7 tiku. Prekresluje JEN pilulku
 * na miste — rect je uz usazeny plnym render_header, sirka se nemeni (stejny
 * text), takze sousedni pilulky se netykaji. ~20 Hz z tick_anim_main. Vrati 1
 * pokud kreslil (flip odlozen na flush). */
int screen_main_tick_sys_xfade(void)
{
    if (s_sys_mix >= 1.0f) return 0;               /* usazeno, neni co prolinat */
    if (s_sys_pill_rect.w == 0) { s_sys_mix = 1.0f; return 0; }  /* pilulka pretekla (neviditelna) */
    s_sys_mix += SYS_XFADE_STEP;
    if (s_sys_mix > 1.0f) s_sys_mix = 1.0f;
    blit_bg_region(s_sys_pill_rect);               /* podklad headeru pod pilulkou */
    ui_pill_t p; sys_pill_setup(&p, s_sys_pill_rect.y);
    p.x = s_sys_pill_rect.x;
    ui_pill_render(&p);
    return 1;
}

/* Incremental prekresleni signal bargrafu (animace ~10x/s, dBm krok 1): chrome/
 * pozadi/label/stopa jsou staticke z render_main; prekresli jen segmenty zmenene
 * od minula + value text (dBm s 1 des. mistem). Misto ~20 fillu jen rozdil. Vrati 1. */
int screen_main_redraw_signal(int16_t pct, int32_t dbm10)
{
    if (s_signal_rect.w == 0) return 0;
    int16_t old = s_signal_pct;       /* hodnota aktualne na obrazovce */
    s_signal_pct = pct;
    ui_card_t card = {.rect = s_signal_rect};
    prim_rect_t inner = ui_card_inner_rect(&card);
    int drew = 0;

    /* Value text (dBm z AD8307): prekresli jen pri zmene o >=0.1 dB (sum ADS
     * necha text v klidu; bar ma vlastni 1% granularitu). */
    if (s_signal_dbm10 <= -100000 || dbm10 != s_signal_dbm10) {
        s_signal_dbm10 = dbm10;
        char val[24];
        fmt_dbm(val, sizeof val, dbm10);
        /* siroky clear box (122 px) — pokryva i nejdelsi variantu textu */
        prim_fill_rect((prim_rect_t){(int16_t)(inner.x + inner.w - 120), (int16_t)(inner.y - 2),
                                     122, 20}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        ui_bargraph_value(&inner, val, UI_COLOR_OK);
        drew = 1;
    }

    /* Segmenty: jen rozdil old -> pct (rozsvit/zhasni). */
    if (ui_bargraph_update(&inner, old, pct) > 0) drew = 1;
    return drew;
}

/* Krok zdroje BEZ kresleni — voláno mimo hlavni obrazovku (jine okno /
 * screensaver), aby statistika (ring + ADEV pyramida) rostla 24/7 a Allan
 * dosahl dlouhych tau. Aktualizuje s_freq_n z realneho mereni (nebo SIM fallback).
 * Kresli se az zase na main. */
void screen_main_freq_sim_step(void)
{
    freq_advance();
}

/* Frakcni odchylka kmitoctu od stredu -> 0..1 (0,5 = na stredu). Pasmo ±0,4 Hz
 * mapuje na plny rozsah. Slouzi spektrogramu (vodopad Δf). */
float screen_main_freq_dev_unit(void)
{
    if (!s_num_ready) return 0.5f;
    int64_t off = (int64_t)s_freq_n - (int64_t)s_freq_center;
    float lsb_per_hz = (float)pow10_u64(s_freq_frac);   /* LSB = 10^-frac Hz */
    if (lsb_per_hz <= 0.0f) return 0.5f;
    float dev_hz = (float)off / lsb_per_hz;
    float u = 0.5f + dev_hz / 0.8f;                     /* ±0,4 Hz -> [0,1] */
    if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
    return u;
}

/* Aktualni zobrazovany kmitocet v Hz (double). s_freq_n je v LSB = 10^-frac Hz
 * -> Hz = s_freq_n / 10^frac. Zdroj pro Math/limity (#43/#44), Allan, drift.
 * Realny FPGA kmitocet (nebo emulator), pri fallbacku simulace (viz freq_advance). */
double screen_main_freq_hz(void)
{
    if (!s_num_ready) return 0.0;
    return (double)s_freq_n / (double)pow10_u64(s_freq_frac);
}

/* Vezme aktualni hodnotu ze zdroje (`freq_advance`: realne mereni / SIM fallback),
 * prepise cislice a prekresli cislo — PER-SEGMENT DIRTY: meni se hlavne nizke
 * cislice, takze staci prekreslit OCAS od prvni zmenene skupiny doprava; stabilni
 * vyssi cislice (cela cast) se neprekresluji. Obraz je pixel-identicky s full
 * redrawem, ale za zlomek zateze (typicky 2-4 mono_75 glyfy misto vsech).
 * Volat ~20x/s z hlavni obrazovky. @return 1 = kreslilo se (volajici flipne). */
int screen_main_redraw_freq(void)
{
    if (!s_num_ready) return 0;
    freq_advance();

    /* Zmena formatu (jina magnituda) NEBO prepnuti REAL<->SIM (marker) -> per-segment
     * dirty cesta nestaci, nutny PLNY redraw zony.
     * ⚠️ Cislice i shadow se MUSI naplnit i tady: pri prepnuti REAL<->SIM se format
     * nemeni (num_build_for se nevola), takze bez toho by se vykreslila jeste STARA
     * hodnota a spravna by naskocila az o snimek pozdeji. */
    if (s_freq_fmt_changed) {
        s_freq_fmt_changed = 0;
        freq_fill_segments();
        for (int i = 0; i < s_num.segment_count; i++) strcpy(s_num_prev[i], s_num_buf[i]);
        screen_main_redraw_freq_area();
        return 1;
    }

    freq_fill_segments();

    int from = -1;                                  /* prvni zmenena skupina cislic */
    for (int i = 0; i < s_num.segment_count; i++)
        if (strcmp(s_num_buf[i], s_num_prev[i]) != 0) { from = i; break; }
    if (from < 0) return 0;                          /* nic se nezmenilo -> neflipovat */

    int16_t x0 = s_seg_x[from];                      /* cachovana geometrie (num_build) */
    /* Obnov pozadi od x0 doprava (po pravy okraj cisla vc. jednotky) a prekresli
     * jen ocas. x0-2 zasahne max okraj predchoziho separatoru, jehoz inkoust je
     * uprostred advance -> bez rezidua. */
    /* vyska 88 (ne 92): konci nad horni hranou pravych karet (offset/σ) -> jejich
     * okraj uz neproblikava pri 20Hz prekreslovani kmitoctu. */
    blit_bg_region((prim_rect_t){ (int16_t)(x0 - 2), s_num_top,
                                  (int16_t)(s_num_left + s_num_w - x0 + 8), 88 });
    prim_set_glyph_accel(1);                 /* HW glyfy jen pro mereny kmitocet */
    ui_big_number_render_tail(&s_num, (int16_t)from);
    prim_set_glyph_accel(0);
    for (int i = from; i < s_num.segment_count; i++) strcpy(s_num_prev[i], s_num_buf[i]);
    return 1;
}

/* Plne prekresleni zony kmitoctu vcetne podbarveni stavu (RUN = cisty gradient,
 * STOP = lehce cervene). Vola se pri PREPNUTI RUN/STOP — tam se meni podklad,
 * ne cislice, takze per-segment dirty cesta (screen_main_redraw_freq) by nic
 * neprekreslila (a pri STOP uz stejne nebezi). */
void screen_main_redraw_freq_area(void)
{
    if (!s_num_ready) return;
    /* ⚠️ Cisti se SJEDNOCENI s PREDCHOZI zonou: pri zmene formatu (jina magnituda /
     * jiny pocet desetin) se sirka cisla zmeni a je vycentrovana, takze nove
     * `freq_area()` je jinde. Kdyby se cistilo jen ono, zbyly by po stranach
     * "duchove" starych cislic (typicky i stara jednotka Hz), protoze partial
     * redraw uz do te oblasti nikdy nesahne. */
    static prim_rect_t s_prev_area;
    prim_rect_t cur = freq_area();
    if (s_prev_area.w > 0) {
        int16_t x0 = (cur.x < s_prev_area.x) ? cur.x : s_prev_area.x;
        int16_t x1 = (int16_t)((cur.x + cur.w > s_prev_area.x + s_prev_area.w)
                               ? cur.x + cur.w : s_prev_area.x + s_prev_area.w);
        blit_bg_region((prim_rect_t){x0, cur.y, (int16_t)(x1 - x0), cur.h});
    } else {
        blit_bg_region(cur);
    }
    s_prev_area = cur;
    freq_tint_if_stopped();
    prim_set_glyph_accel(1);
    ui_big_number_render(&s_num);
    prim_set_glyph_accel(0);
    /* #1: SIM marker — headline žene simulace (žádné platné měření z FPGA/emulátoru).
     * Emulovaná data (fpgasim) jdou reálnou cestou (g_freq_valid=1) -> BEZ markeru. */
    if (s_freq_is_sim) {
        prim_rect_t fa = freq_area();
        prim_draw_text((prim_point_t){(int16_t)(fa.x + 2), (int16_t)(fa.y + 14)},
                       "SIM", &ui_font_mono_16, UI_COLOR_WARN, PRIM_ALIGN_LEFT);
    }
}

/* FX_HEAD_GLOW (bloom za kmitoctem pri cerstvem mereni) ODSTRANEN 2026-07-26 na
 * prani uzivatele — vcetne ovladaciho tlacitka v okne EFEKTY. K dohledani v git
 * historii (screen_main_tick_head_glow / head_glow_render). */

/* Navzorkuje aktualni frakcni odchylku do statistiky (plochy ring + pyramida, ~1x/s). */
void screen_main_stats_sample(void)
{
    stats_sample();
}

/* Zive prekresleni trend + offset/sigma (lehke; volat ~1x/s). Vrati 1. */
int screen_main_redraw_stats(void)
{
    if (s_trend_rect.w == 0) return 0;            /* jeste nebyl full render */
    blit_bg_region(s_trend_rect); render_card_trend(s_trend_rect);
    blit_bg_region(s_small_rect); draw_offset_sigma(s_small_rect);
    return 1;
}

/* Zive prekresleni Allan grafu (tezsi: grid + krivka; volat ~1x/s). Vrati 1. */
int screen_main_redraw_allan(void)
{
    if (s_allan_rect.w == 0) return 0;
    blit_bg_region(s_allan_rect); render_card_allan(s_allan_rect);
    return 1;
}

/* Prepinac lin/log Y osy histogramu (log zviditelni slabe biny/chvosty). */
static bool s_hist_logy = false;
bool screen_main_hist_logy(void)        { return s_hist_logy; }
void screen_main_hist_toggle_logy(void) { s_hist_logy = !s_hist_logy; }

/* Vyska sloupce/krivky v px: linearni (count/peak) nebo log (log10(1+count)/log10(1+peak)). */
static int16_t hist_h(float count, int peak, int16_t H, bool logy)
{
    if (count <= 0.0f) return 0;
    float frac = logy ? (log10f(1.0f + count) / log10f(1.0f + (float)peak))
                      : (count / (float)peak);
    if (frac > 1.0f) frac = 1.0f;
    int16_t h = (int16_t)(frac * (float)H);
    return (h < 1) ? 1 : h;
}

/* Histogram distribuce frakcni odchylky y (poslednich s_y_count vzorku, τ0=1s).
 * Auto-range [min..max], svisle sloupce; mean (zelena) + median (amber) cara +
 * Gaussova referencni krivka (ocekavane cetnosti pri normalnim rozdeleni) +
 * overlay N/mean/sigma/median. Lin/log Y (screen_main_hist_toggle_logy). Kresli
 * do 'rect' (uvnitr karty histogram okna) a sam si ho vycisti -> lze volat 2x/s.
 * Data plni stats_sample nezavisle na okne (sim krokuje i mimo main) -> ZIVY
 * (change-key v app_gpsdo prekresli pri kazdem novem vzorku, ~1x/s). */
void screen_main_render_histogram(prim_rect_t rect)
{
    prim_fill_rect(rect, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);   /* clear + dirty-rect */

    int n = s_y_count;
    if (n < 4) {
        prim_draw_text((prim_point_t){(int16_t)(rect.x + rect.w / 2),
                                      (int16_t)(rect.y + rect.h / 2)},
                       "Waiting for data...", &ui_font_sans_18, UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
        return;
    }

    /* Jedna kopie ringu do lokalniho pole (stat_at dela modulo — dal uz jen
     * linearni pristupy); poradi je pro min/max/mean/biny/median nepodstatne. */
    float srt[STAT_N];
    for (int i = 0; i < n; i++) srt[i] = stat_at(i);

    float mn = srt[0], mx = mn, sum = 0.0f;
    for (int i = 0; i < n; i++) { float v = srt[i]; if (v < mn) mn = v; if (v > mx) mx = v; sum += v; }
    float mean = sum / (float)n;
    float span = mx - mn;
    if (span < 1e-18f) span = 1e-18f;                    /* degenerace -> vyhni se /0 */

    /* binovani + rozptyl (sample stddev distribuce, ne ADEV) */
    #define HIST_BINS 24
    int bins[HIST_BINS] = {0};
    double var = 0.0;
    for (int i = 0; i < n; i++) {
        float v = srt[i];
        int b = (int)((v - mn) / span * (float)HIST_BINS);
        if (b < 0) b = 0; else if (b >= HIST_BINS) b = HIST_BINS - 1;
        bins[b]++;
        double d = (double)(v - mean); var += d * d;
    }
    float sd = (n > 1) ? (float)sqrt(var / (double)n) : 0.0f;
    int peak = 1;
    for (int b = 0; b < HIST_BINS; b++) if (bins[b] > peak) peak = bins[b];

    /* median: insertion sort in-place (n<=120, cold path — kresli se jen pri zmene dat) */
    for (int i = 1; i < n; i++) {
        float k = srt[i]; int j = i - 1;
        while (j >= 0 && srt[j] > k) { srt[j + 1] = srt[j]; j--; }
        srt[j + 1] = k;
    }
    float median = (n & 1) ? srt[n / 2] : 0.5f * (srt[n / 2 - 1] + srt[n / 2]);
    bool logy = s_hist_logy;

    /* plot area: leva rezerva na peak-count, dolni na y-popisky */
    prim_rect_t in = {(int16_t)(rect.x + 34), (int16_t)(rect.y + 26),
                      (int16_t)(rect.w - 42), (int16_t)(rect.h - 48)};

    prim_draw_line((prim_point_t){in.x, (int16_t)(in.y + in.h)},          /* baseline */
                   (prim_point_t){(int16_t)(in.x + in.w), (int16_t)(in.y + in.h)}, 1, UI_COLOR_LINE);
    char cb[16]; snprintf(cb, sizeof cb, "%d%s", peak, logy ? " log" : "");
    prim_draw_text((prim_point_t){(int16_t)(in.x - 6), (int16_t)(in.y + 6)}, cb,
                   &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_RIGHT);

    int16_t bw = (int16_t)(in.w / HIST_BINS); if (bw < 2) bw = 2;
    for (int b = 0; b < HIST_BINS; b++) {
        if (bins[b] == 0) continue;
        int16_t h = hist_h((float)bins[b], peak, in.h, logy);
        int16_t bx = (int16_t)(in.x + b * bw);
        prim_fill_rect((prim_rect_t){bx, (int16_t)(in.y + in.h - h),
                                     (int16_t)(bw - 1), h}, UI_COLOR_ACC, PRIM_BLEND_OVER);
    }

    /* Gaussova referencni krivka: ocekavane cetnosti N*binW*pdf(v) pri normalnim
     * rozdeleni (mean,sd) -> porovnani tvaru (pretazeni / tezke chvosty). */
    if (sd > 1e-18f) {
        const float binW = span / (float)HIST_BINS;
        const float kk = (float)n * binW / (sd * 2.5066283f);     /* N*binW/(sd*sqrt(2π)) */
        int16_t py_prev = 0, have_prev = 0;
        for (int px = 0; px <= in.w; px += 3) {
            float v = mn + (float)px / (float)in.w * span;
            float z = (v - mean) / sd;
            int16_t hy = hist_h(kk * expf(-0.5f * z * z), peak, in.h, logy);
            int16_t py = (int16_t)(in.y + in.h - hy);
            int16_t cx = (int16_t)(in.x + px);
            if (have_prev)
                prim_draw_line((prim_point_t){(int16_t)(cx - 3), py_prev},
                               (prim_point_t){cx, py}, 1, UI_COLOR_INK_2);
            py_prev = py; have_prev = 1;
        }
    }

    /* mean (zelena) + median (amber) svisle cary */
    int16_t mpx = (int16_t)(in.x + (int16_t)((mean - mn) / span * (float)in.w));
    prim_draw_line((prim_point_t){mpx, in.y}, (prim_point_t){mpx, (int16_t)(in.y + in.h)},
                   1, UI_COLOR_OK);
    int16_t dpx = (int16_t)(in.x + (int16_t)((median - mn) / span * (float)in.w));
    prim_draw_line((prim_point_t){dpx, in.y}, (prim_point_t){dpx, (int16_t)(in.y + in.h)},
                   1, UI_COLOR_WARN);

    /* X popisky: min (vlevo) / max (vpravo) ve frac notaci */
    char lb[24], rb[24];
    fmt_frac(lb, sizeof lb, mn, 1);
    fmt_frac(rb, sizeof rb, mx, 1);
    prim_draw_text((prim_point_t){in.x, (int16_t)(in.y + in.h + 16)}, lb,
                   &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){(int16_t)(in.x + in.w), (int16_t)(in.y + in.h + 16)}, rb,
                   &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_RIGHT);

    /* overlay: N/mean/sigma (radek 1, vpravo nahore) + median (radek 2, amber) */
    char ov[80], mb[24], sb[24];
    fmt_frac(mb, sizeof mb, mean, 1);
    fmt_frac(sb, sizeof sb, sd, 0);
    snprintf(ov, sizeof ov, "N=%d  x=%s  s=%s", n, mb, sb);
    prim_draw_text((prim_point_t){(int16_t)(rect.x + rect.w), (int16_t)(rect.y + 16)}, ov,
                   &ui_font_mono_18, UI_COLOR_INK_2, PRIM_ALIGN_RIGHT);
    char db[28]; fmt_frac(mb, sizeof mb, median, 1);
    snprintf(db, sizeof db, "med=%s", mb);
    prim_draw_text((prim_point_t){(int16_t)(rect.x + rect.w), (int16_t)(rect.y + 36)}, db,
                   &ui_font_mono_14, UI_COLOR_WARN, PRIM_ALIGN_RIGHT);
    #undef HIST_BINS
}

/* σy(τ) tabulka: Allanova deviace pro dekadova τ (1..10k s). τ=1s z kratkeho ringu
 * (stats_adev), delsi z decimacni pyramidy (adev_stage). "--" dokud neni dost dat.
 * Kresli do 'rect' + sam vycisti -> volatelne 2x/s vedle histogramu. */
void screen_main_render_stats_table(prim_rect_t rect)
{
    prim_fill_rect(rect, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    /* Baseline uvnitr cisteneho rectu — jinak by AA hrany nad rectem pri 2x/s
     * refreshi postupne tuhly (kresli se mimo clear oblast). sans_18 (TODO
     * #11(2b), bylo sans_16): skutecny oy velkych pismen je 14 (ne nominalni
     * ascent 18), takze pri baseline=rect.y+18 zustava 4 px rezervy nahore. */
    prim_draw_text((prim_point_t){rect.x, (int16_t)(rect.y + 18)}, "σy(τ)  Allan",
                   &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    static const char *TL[5] = {"1 s", "10 s", "100 s", "1 ks", "10 ks"};
    int16_t ry = (int16_t)(rect.y + 46);
    for (int i = 0; i < 5; i++) {
        float a = (i == 0) ? stats_adev(1) : adev_stage(i, 1);
        char vb[24];
        if (a > 0.0f) fmt_frac(vb, sizeof vb, a, 0);
        else { vb[0] = vb[1] = '-'; vb[2] = '\0'; }
        int16_t ty = (int16_t)(ry + i * 30);
        prim_draw_text((prim_point_t){rect.x, ty}, TL[i],
                       &ui_font_mono_14, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(rect.x + rect.w), ty}, vb,
                       &ui_font_mono_14, (a > 0.0f) ? UI_COLOR_INK_2 : UI_COLOR_INK_4,
                       PRIM_ALIGN_RIGHT);
    }
}

/* Fullscreen Allan graf (okno ALLAN, s_view=23): stejny renderer jako karta na
 * hlavni obrazovce (allan_plot), ale big=1 -> popisky mono_16 (na 4,3" citelne)
 * a vetsi rezervy. Sam si vycisti rect (REPLACE + dirty-rect) -> volatelne 2x/s;
 * osu "τ [s]" vysvetluje header karty v app_gpsdo (tady jen dekadova cisla). */
void screen_main_render_allan_big(prim_rect_t rect)
{
    prim_fill_rect(rect, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);   /* clear + dirty-rect */
    allan_plot(rect, 1);
}

/* Casove okno fullscreen trendu [s]. Prepinatelne tlacitky (presety 1 min..60 dni;
 * dlouha okna se kresli z decimacni pyramidy s_tr[], viz trend_feed). */
static int32_t s_trend_secs = 120;
void screen_main_trend_set_secs(int s) { if (s > 0) s_trend_secs = (int32_t)s; }
int  screen_main_trend_secs(void)      { return (int)s_trend_secs; }

/* Fullscreen trend: posledních s_trend_secs z decimacni pyramidy — vybere se
 * nejjemnejsi stage, ktery okno pokryje (tr_pick). Kdyz jeste nema dost dat
 * (vyssi stage se plni pomalu — napr. stage 8 az po 18 h behu), spadne se na
 * nejvyssi stage s aspon 2 vzorky, aby bylo VZDY videt neco misto "waiting".
 * Auto-scale s min/max popisky (fmt_frac), mrizka, overlay okno/krok/drift. */
void screen_main_render_trend_big(prim_rect_t rect)
{
    prim_fill_rect(rect, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);

    /* 'ts', ne 'st' — 'st' je globalni UI stav (stejny duvod jako 'sg' u ADEV). */
    int ts = tr_pick(s_trend_secs);
    while (ts > 0 && s_tr[ts].count < 2) ts--;   /* fallback na to, co uz mame */
    int32_t res = tr_res(ts);
    int32_t want = s_trend_secs / res;           /* kolik vzorku okno potrebuje */
    if (want > TR_RING) want = TR_RING;
    int n = s_tr[ts].count;
    if ((int32_t)n > want) n = (int)want;

    if (n < 2) {
        prim_draw_text((prim_point_t){(int16_t)(rect.x + rect.w / 2),
                                      (int16_t)(rect.y + rect.h / 2)},
                       "Waiting for data...", &ui_font_sans_18, UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
        return;
    }
    float mn = tr_at(ts, 0), mx = mn;
    for (int i = 1; i < n; i++) { float v = tr_at(ts, i); if (v < mn) mn = v; if (v > mx) mx = v; }
    float span = mx - mn;
    if (span < 1e-18f) span = 1e-18f;

    prim_rect_t in = {(int16_t)(rect.x + 8), (int16_t)(rect.y + 26),
                      (int16_t)(rect.w - 16), (int16_t)(rect.h - 52)};
    /* mrizka: 3 horizontaly (min/stred/max) */
    for (int j = 0; j <= 2; j++) {
        int16_t yy = (int16_t)(in.y + (int32_t)j * in.h / 2);
        prim_draw_line((prim_point_t){in.x, yy}, (prim_point_t){(int16_t)(in.x + in.w), yy},
                       1, UI_COLOR_LINE);
    }
    /* krivka: nejstarsi vlevo -> nejnovejsi vpravo */
    int16_t px_prev = 0, py_prev = 0;
    for (int i = 0; i < n; i++) {
        float v = tr_at(ts, n - 1 - i);
        int16_t px = (int16_t)(in.x + (int32_t)i * in.w / (n - 1));
        int16_t py = (int16_t)(in.y + in.h - (int16_t)((v - mn) / span * (float)in.h));
        if (i) prim_draw_line((prim_point_t){px_prev, py_prev}, (prim_point_t){px, py},
                              2, UI_COLOR_ACC);
        px_prev = px; py_prev = py;
    }
    /* posledni bod zvyrazneny */
    prim_fill_circle((prim_point_t){px_prev, py_prev}, 4, UI_COLOR_ACC_SOFT);

    /* Y popisky: max nahore / min dole (frac notace) */
    char lb[24];
    fmt_frac(lb, sizeof lb, mx, 1);
    prim_draw_text((prim_point_t){in.x, (int16_t)(in.y - 6)}, lb,
                   &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
    fmt_frac(lb, sizeof lb, mn, 1);
    prim_draw_text((prim_point_t){in.x, (int16_t)(in.y + in.h + 16)}, lb,
                   &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
    /* overlay: okno + skutecne pokryty cas + krok decimace (vpravo nahore).
     * "pokryto" = n*res muze byt MENE nez okno, dokud stage nenasbira data. */
    char ov[96], fb2[24], dw[16], dr[16], dc[16];
    fmt_dur(dw, sizeof dw, s_trend_secs);
    fmt_dur(dr, sizeof dr, res);
    fmt_dur(dc, sizeof dc, (int32_t)n * res);
    if ((int32_t)n * res < s_trend_secs - res)      /* jeste se sbira -> ukaz oboji */
        snprintf(ov, sizeof ov, "okno %s · zatim %s · krok %s", dw, dc, dr);
    else
        snprintf(ov, sizeof ov, "okno %s · krok %s · N=%d", dw, dr, n);
    prim_draw_text((prim_point_t){(int16_t)(rect.x + rect.w - 6), (int16_t)(in.y - 6)}, ov,
                   &ui_font_mono_18, UI_COLOR_INK_2, PRIM_ALIGN_RIGHT);
    fmt_frac(fb2, sizeof fb2, stats_drift(), 1);
    snprintf(ov, sizeof ov, "drift %s/min", fb2);
    prim_draw_text((prim_point_t){(int16_t)(rect.x + rect.w - 6), (int16_t)(in.y + in.h + 16)}, ov,
                   &ui_font_mono_14, UI_COLOR_INK_3, PRIM_ALIGN_RIGHT);
}

/* Redraw only one footer button (clears just its rect from the bg cache). */
void screen_main_redraw_button(int idx)
{
    if (idx < 0 || idx >= SCR_BTN_COUNT) return;
    prim_rect_t r = s_btn_rect[idx];
    blit_bg_region(r);
    const char *l, *v; ui_button_variant_t var;
    footer_button_def(idx, &l, &v, &var);
    ui_button_t b = {.rect = r, .variant = var, .label = l, .value = v};
    ui_button_render(&b);
}

/* ── Micro-flash tlacitka pri stisku (item 3) ────────────────────────────────
 * Kratky zvyrazneny obrys pres tlacitko (2-3 tiky ~20 Hz = ~120 ms), pak se
 * flash odstrani prekreslenim tlacitka (screen_main_redraw_button, ktery uz
 * dela blit_bg_region -> mark_dirty). Obrys je INSET (o 3 px mensi nez
 * tlacitko), takze lezi cely uvnitr dirty rectu, ktery uz zavolal volajici
 * (screen_main_redraw_button hned po zmacknuti) — zadny dalsi clear netreba
 * (viz CLAUDE.md: AA/stroke obsah musi byt uvnitr predchoziho REPLACE clearu). */
#define BTN_FLASH_FRAMES 2   /* 3 -> 2 (2026-08-15): pусobilo dlouze */
static int s_flash_idx    = -1;
static int s_flash_frames = 0;

void screen_main_button_flash_start(int idx)
{
    if (!g_anim_enabled) return;             /* globalni vypinac (okno Animace) -> zadny flash */
    if (idx < 0 || idx >= SCR_BTN_COUNT) return;
    prim_rect_t r = s_btn_rect[idx];
    prim_rect_t in = {(int16_t)(r.x + 3), (int16_t)(r.y + 3),
                      (int16_t)(r.w - 6), (int16_t)(r.h - 6)};
    prim_stroke_rect_rounded(in, 12, 2, UI_COLOR_ACC);
    s_flash_idx    = idx;
    s_flash_frames = BTN_FLASH_FRAMES;
}

int screen_main_button_flash_tick(void)
{
    if (s_flash_idx < 0) return 0;
    if (--s_flash_frames > 0) return 0;
    int idx = s_flash_idx;
    s_flash_idx = -1;
    screen_main_redraw_button(idx);   /* prekresleni tlacitka smaze flash obrys */
    return 1;
}

void screen_main_render(void)
{
    if (!cache_initialized) screen_main_init();
    prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
              bg_cache, UI_DIM_SCREEN_W * sizeof(prim_pixel_t));
    render_header();
    render_body_title();
    render_body_number();
    render_body_grid();
    render_footer();
}
