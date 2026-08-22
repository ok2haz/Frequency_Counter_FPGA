/**
 * @file theme.c
 * @brief Runtime palety: TMAVA (default) + SVETLA + 2 varianty tmave (STREDNI /
 *        OBRYS, lisi se jen vzhledem NORMAL tlacitek) + KONTRAST (samostatna
 *        vysoce kontrastni). Index 0..4 = UI_THEME_* (theme.h); persist v g_theme_idx.
 *
 * Prepnuti = zmena ukazatele g_ui_theme (ui_theme_select). Volajici pak MUSI
 * zneplatnit predrenderovane cache (bg_cache — screen_main_invalidate) a
 * prekreslit; jinak zustane stary podklad. Zadne dalsi zavislosti.
 */

#include <ui/theme.h>

static const ui_theme_t THEME_DARK = {
    .bg0        = PRIM_RGB(0x06, 0x09, 0x0E),
    .bg1        = PRIM_RGB(0x0A, 0x0F, 0x17),
    .bg_card    = PRIM_RGB(0x0B, 0x10, 0x18),
    .hdr_top    = PRIM_RGB(0x08, 0x0C, 0x12),
    .hdr_bot    = PRIM_RGB(0x06, 0x09, 0x0E),
    .line       = PRIM_RGB(0x15, 0x20, 0x30),
    .line_hi    = PRIM_RGB(0x1F, 0x2D, 0x42),
    .axis       = PRIM_RGB(0x5A, 0x68, 0x78),
    .ink        = PRIM_RGB(0xE3, 0xED, 0xF7),
    .ink2       = PRIM_RGB(0x9A, 0xA6, 0xB4),
    .ink3       = PRIM_RGB(0x7C, 0x88, 0x96),
    .ink4       = PRIM_RGB(0x3A, 0x48, 0x58),
    .ink5       = PRIM_RGB(0x1E, 0x27, 0x36),
    .acc        = PRIM_RGB(0x38, 0xBD, 0xF8),
    .acc_soft   = PRIM_RGB(0x7D, 0xD3, 0xFC),
    .ok         = PRIM_RGB(0x34, 0xD3, 0x99),
    .ok_soft    = PRIM_RGB(0x86, 0xEF, 0xAC),
    .ok_bg      = PRIM_RGB(0x0C, 0x1E, 0x15),
    .ok_border  = PRIM_RGB(0x1D, 0x4D, 0x2E),
    .warn       = PRIM_RGB(0xFB, 0xBF, 0x24),
    .bad        = PRIM_RGB(0xF8, 0x71, 0x71),
    .violet     = PRIM_RGB(0xA7, 0x8B, 0xFA),
    .btn_run_top    = PRIM_RGB(0x0F, 0x2C, 0x1C),
    .btn_run_border = PRIM_RGB(0x2E, 0x64, 0x42),
    /* ACTIVE = "vybrano/aktivni". 2026-08-20 ZJASNENO nad NORMAL: kdyz NORMAL
     * tlacitka zesvetlela do modra, tmava navy uz nevystupovala. Jasny sytejsi
     * modry podklad + accent (sky) ramecek shodny s accent textem -> vybrane
     * tlacitko "sviti" nad klidne modrymi NORMAL. */
    .btn_act_top    = PRIM_RGB(0x21, 0x54, 0x8F),
    .btn_act_border = PRIM_RGB(0x38, 0xBD, 0xF8),
    /* STOP = cervena obdoba RUN (stejne jasy, jen cerveny odstin). */
    .btn_stop_top    = PRIM_RGB(0x2C, 0x0F, 0x14),
    .btn_stop_border = PRIM_RGB(0x64, 0x2E, 0x36),
    /* NORMAL = SVETLE MODRE tlacitko (2026-08-20, iterace "vice svetle modre";
     * postupne 0x0A0F17 -> 0x102C4C -> ted svetlejsi navy vypln + svetle skymodry
     * ramecek). ⚠️ Vypln je STROP svetlosti: svetle-sedy text INK_2 (jednoradkove,
     * mono_22) na ni jeste cte (~4,6:1), INK_3 (dvouradkovy label, sans_18) uz jen
     * ~3:1 — svetlejsi vypln by chtela BILY text NORMAL tlacitka (dnes se nemeni).
     * ACTIVE se lisi accent textem. Varianty: STREDNI vlastni bridlici, OBRYS jen
     * ramecek (dedi tuto vypln). */
    .btn_norm_top    = PRIM_RGB(0x15, 0x3C, 0x66),
    .btn_norm_border = PRIM_RGB(0x55, 0xAA, 0xE6),
    .freq_stop_bg    = PRIM_RGBA(0xF8, 0x71, 0x71, 72),   /* ~28 % (2026-08-15: 38 bylo pri STOP sotva videt) */
};

/* Svetla paleta: stejne role, prevracene jasy; akcenty ztmavene kvuli kontrastu
 * na svetlem podkladu (sky/emerald/amber/red v "600" odstinech). */
static const ui_theme_t THEME_LIGHT = {
    .bg0        = PRIM_RGB(0xE9, 0xEE, 0xF3),
    .bg1        = PRIM_RGB(0xDD, 0xE5, 0xEC),
    .bg_card    = PRIM_RGB(0xF7, 0xFA, 0xFC),
    .hdr_top    = PRIM_RGB(0xD8, 0xE0, 0xE8),
    .hdr_bot    = PRIM_RGB(0xE9, 0xEE, 0xF3),
    .line       = PRIM_RGB(0xC0, 0xCB, 0xD6),
    .line_hi    = PRIM_RGB(0xA8, 0xB6, 0xC4),
    .axis       = PRIM_RGB(0x64, 0x72, 0x80),
    .ink        = PRIM_RGB(0x14, 0x1E, 0x28),
    .ink2       = PRIM_RGB(0x38, 0x46, 0x54),
    .ink3       = PRIM_RGB(0x5A, 0x68, 0x76),
    .ink4       = PRIM_RGB(0x94, 0xA2, 0xB0),
    .ink5       = PRIM_RGB(0xC4, 0xCE, 0xD8),
    .acc        = PRIM_RGB(0x02, 0x84, 0xC7),
    .acc_soft   = PRIM_RGB(0x03, 0x69, 0xA1),
    .ok         = PRIM_RGB(0x05, 0x96, 0x69),
    .ok_soft    = PRIM_RGB(0x10, 0xB9, 0x81),
    .ok_bg      = PRIM_RGB(0xDA, 0xF2, 0xE6),
    .ok_border  = PRIM_RGB(0x7F, 0xCF, 0xA8),
    .warn       = PRIM_RGB(0xB4, 0x53, 0x09),
    .bad        = PRIM_RGB(0xDC, 0x26, 0x26),
    .violet     = PRIM_RGB(0x7C, 0x3A, 0xED),
    .btn_run_top    = PRIM_RGB(0xD2, 0xEB, 0xDC),
    .btn_run_border = PRIM_RGB(0x59, 0xA8, 0x77),
    .btn_act_top    = PRIM_RGB(0xD4, 0xE4, 0xF5),
    .btn_act_border = PRIM_RGB(0x6E, 0x96, 0xC2),
    .btn_stop_top    = PRIM_RGB(0xF6, 0xD7, 0xD7),
    .btn_stop_border = PRIM_RGB(0xC9, 0x7A, 0x7A),
    /* NORMAL = puvodni svetly vzhled (= BG_1/LINE). */
    .btn_norm_top    = PRIM_RGB(0xDD, 0xE5, 0xEC),
    .btn_norm_border = PRIM_RGB(0xC0, 0xCB, 0xD6),
    /* Na svetlem podkladu staci nizsi alfa (tmava cervena vic "prekryva"). */
    .freq_stop_bg    = PRIM_RGBA(0xDC, 0x26, 0x26, 58),   /* svetle tema: 30 -> 58 (viz tmave) */
};

/* KONTRAST (2026-08-19): zvlast vysoce kontrastni schema pro maximalni citelnost.
 * Cerne pozadi (0x000000) + bily text + SYTE JASNE akcenty (cyan/zelena/zluta/
 * cervena) + tlacitka = cerna vypln s JASNYM barevnym obrysem. Nejde o "hezke"
 * schema, ale o citelnost (odlesk, slunce, zrakove postizeni). */
static const ui_theme_t THEME_HICONTRAST = {
    .bg0        = PRIM_RGB(0x00, 0x00, 0x00),
    .bg1        = PRIM_RGB(0x00, 0x00, 0x00),
    .bg_card    = PRIM_RGB(0x00, 0x00, 0x00),
    .hdr_top    = PRIM_RGB(0x00, 0x00, 0x00),
    .hdr_bot    = PRIM_RGB(0x00, 0x00, 0x00),
    .line       = PRIM_RGB(0x8A, 0x8A, 0x8A),
    .line_hi    = PRIM_RGB(0xFF, 0xFF, 0xFF),
    .axis       = PRIM_RGB(0xD0, 0xD0, 0xD0),
    .ink        = PRIM_RGB(0xFF, 0xFF, 0xFF),
    .ink2       = PRIM_RGB(0xE6, 0xE6, 0xE6),
    .ink3       = PRIM_RGB(0xC4, 0xC4, 0xC4),
    .ink4       = PRIM_RGB(0x8C, 0x8C, 0x8C),
    .ink5       = PRIM_RGB(0x50, 0x50, 0x50),
    .acc        = PRIM_RGB(0x00, 0xE5, 0xFF),   /* jasna cyan */
    .acc_soft   = PRIM_RGB(0x9B, 0xF3, 0xFF),
    .ok         = PRIM_RGB(0x00, 0xFF, 0x6A),   /* jasna zelena */
    .ok_soft    = PRIM_RGB(0x8C, 0xFF, 0xB4),
    .ok_bg      = PRIM_RGB(0x00, 0x2E, 0x18),
    .ok_border  = PRIM_RGB(0x00, 0xFF, 0x6A),
    .warn       = PRIM_RGB(0xFF, 0xD4, 0x00),   /* jasna zluta */
    .bad        = PRIM_RGB(0xFF, 0x3B, 0x3B),   /* jasna cervena */
    .violet     = PRIM_RGB(0xC8, 0x9B, 0xFF),
    /* Tlacitka = cerna vypln + JASNY barevny obrys (max. kontrast). */
    .btn_run_top    = PRIM_RGB(0x00, 0x1A, 0x0D),
    .btn_run_border = PRIM_RGB(0x00, 0xFF, 0x6A),
    .btn_act_top    = PRIM_RGB(0x00, 0x1A, 0x26),
    .btn_act_border = PRIM_RGB(0x00, 0xE5, 0xFF),
    .btn_stop_top    = PRIM_RGB(0x26, 0x00, 0x00),
    .btn_stop_border = PRIM_RGB(0xFF, 0x3B, 0x3B),
    /* NORMAL = cerna vypln + BILY obrys (nejvyssi mozny kontrast tlacitka). */
    .btn_norm_top    = PRIM_RGB(0x00, 0x00, 0x00),
    .btn_norm_border = PRIM_RGB(0xFF, 0xFF, 0xFF),
    .freq_stop_bg    = PRIM_RGBA(0xFF, 0x3B, 0x3B, 96),   /* vyssi alfa = zretelnejsi STOP */
};

/* Dve varianty TMAVEHO schematu, ktere se lisi JEN vzhledem NORMAL tlacitek —
 * postavene za behu kopii THEME_DARK + override btn_norm_*, aby se cela 40-polozkova
 * paleta neduplikovala (a nerozjela pri budouci zmene tmaveho schematu). */
static ui_theme_t s_theme_softbtn;   /* STREDNI: svetlejsi VYPLN tlacitek */
static ui_theme_t s_theme_frame;     /* OBRYS: jen svetlejsi RAMECEK, vypln zustava tmava */
static bool       s_variants_built = false;

static void build_variants(void)
{
    /* STREDNI: tlacitka "vystoupi" z pozadi svetlejsi bridlicovou vyplni.
     * Zamerne jen stredne svetle (ne az svetle) -> svetle-sedy text INK_2/INK_3
     * z button.c na nich dal cte, netreba menit ink. */
    s_theme_softbtn = THEME_DARK;
    s_theme_softbtn.btn_norm_top    = PRIM_RGB(0x2C, 0x37, 0x46);
    s_theme_softbtn.btn_norm_border = PRIM_RGB(0x44, 0x54, 0x68);

    /* OBRYS: vypln zustava tmava jako pozadi, ale ramecek je jasny ocelovy ->
     * tlacitka jsou zretelne ohranicena, aniz by ztmavla plocha. */
    s_theme_frame = THEME_DARK;
    s_theme_frame.btn_norm_border = PRIM_RGB(0x4C, 0x60, 0x76);

    s_variants_built = true;
}

const ui_theme_t *g_ui_theme = &THEME_DARK;

void ui_theme_select(int idx)
{
    if (!s_variants_built) build_variants();
    switch (idx) {
    case UI_THEME_LIGHT:        g_ui_theme = &THEME_LIGHT;      break;
    case UI_THEME_DARK_SOFTBTN: g_ui_theme = &s_theme_softbtn;  break;
    case UI_THEME_DARK_FRAME:   g_ui_theme = &s_theme_frame;    break;
    case UI_THEME_HICONTRAST:   g_ui_theme = &THEME_HICONTRAST; break;
    default:                    g_ui_theme = &THEME_DARK;       break;
    }
}
