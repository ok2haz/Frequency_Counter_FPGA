/**
 * @file    setup.c
 * @brief   Uložit/načíst sestavu — viz setup.h.
 */
#include "setup.h"
#include "w25q.h"
#include "w25q_store.h"
#include "w25q_map.h"
#include "freertos_shared.h"   /* g_brightness, g_theme_idx, g_tz_*, g_ui_cfg, g_sys_cfg_dirty, qspiMutexHandle */
#include "meas_math.h"         /* g_meas_cfg */
#include "cmsis_os2.h"
#include <string.h>

#define SETUP_BOOK_MAGIC   0x53545031u   /* "STP1" */
#define SETUP_LOCK_MS      200u          /* manuální akce (tlačítko) — smí chvíli počkat */

/* Jeden slot = snapshot uživatelských nastavení. */
typedef struct {
    uint8_t  used;
    uint8_t  brightness, sound_muted, autodim_en;
    uint16_t autodim_sec;
    uint8_t  theme_idx, lang_en;   /* theme_idx 0..4 = UI_THEME_* (drive "theme_light" 0/1, stejny bajt) */
    int8_t   tz_offset_h;
    uint8_t  tz_auto, ui_cfg, anim_en;
    uint16_t fx_en;
    uint8_t  meas_math_en, meas_null_en, meas_limit_en, meas_alarm_en;
    double   meas_m, meas_b, meas_null_ref, meas_lo, meas_hi;
} setup_slot_t;

typedef struct {
    uint32_t     magic;
    setup_slot_t slots[SETUP_N];
} setup_book_t;

static w25q_store_t s_store;
static setup_book_t s_book;      /* RAM kopie (načtená při initu) */

/* Sanitizace slotu (proti poškozenému flash záznamu): clamp jasu/auto-dim/zóny/M.
 * Stejné meze jako syscfg. Pure — testováno v setup_selftest. */
static void slot_sanitize(setup_slot_t *s)
{
    if (s->brightness < 25) s->brightness = 25;
    if (!(s->autodim_sec >= 15 && s->autodim_sec <= 600)) s->autodim_sec = 300;
    if (s->tz_offset_h < -12) s->tz_offset_h = -12;
    else if (s->tz_offset_h > 14) s->tz_offset_h = 14;
    if (s->meas_m == 0.0) s->meas_m = 1.0;   /* 0 by byl mrtvý scale */
}

void setup_init(void)
{
    memset(&s_book, 0, sizeof s_book);
    s_book.magic = SETUP_BOOK_MAGIC;
    if (osMutexAcquire(qspiMutexHandle, SETUP_LOCK_MS) != osOK) return;
    if (w25q_store_init(&s_store, W25Q_SETUP_BASE, W25Q_SETUP_SECTORS)) {
        setup_book_t b;
        uint32_t n = w25q_store_read(&s_store, &b, sizeof b);
        if (n == sizeof b && b.magic == SETUP_BOOK_MAGIC) s_book = b;   /* jinak zůstává prázdný */
    }
    osMutexRelease(qspiMutexHandle);
}

uint8_t setup_used_mask(void)
{
    uint8_t m = 0;
    for (int i = 0; i < SETUP_N; i++) if (s_book.slots[i].used) m |= (uint8_t)(1u << i);
    return m;
}

/* Zapíše RAM knihu do flash (celý blob). */
static bool book_flush(void)
{
    if (!s_store.ready) return false;
    if (osMutexAcquire(qspiMutexHandle, SETUP_LOCK_MS) != osOK) return false;
    bool ok = w25q_store_write(&s_store, &s_book, sizeof s_book);
    osMutexRelease(qspiMutexHandle);
    return ok;
}

bool setup_save(int slot)
{
    if (slot < 0 || slot >= SETUP_N) return false;
    setup_slot_t *s = &s_book.slots[slot];
    memset(s, 0, sizeof *s);
    s->used         = 1;
    s->brightness   = g_brightness;
    s->sound_muted  = g_sound_muted;
    s->autodim_en   = g_autodim_en;
    s->autodim_sec  = g_autodim_sec;
    s->theme_idx    = g_theme_idx;
    s->lang_en      = g_lang_en;
    s->tz_offset_h  = g_tz_offset_h;
    s->tz_auto      = g_tz_auto;
    s->ui_cfg       = g_ui_cfg;
    s->anim_en      = g_anim_enabled ? 1u : 0u;
    s->fx_en        = (uint16_t)(g_fx_enabled & FX_ALL);
    s->meas_math_en  = g_meas_cfg.math_en ? 1u : 0u;
    s->meas_null_en  = g_meas_cfg.null_en ? 1u : 0u;
    s->meas_limit_en = g_meas_cfg.limit_en ? 1u : 0u;
    s->meas_alarm_en = g_meas_cfg.alarm_en ? 1u : 0u;
    s->meas_m        = g_meas_cfg.m;
    s->meas_b        = g_meas_cfg.b;
    s->meas_null_ref = g_meas_cfg.null_ref;
    s->meas_lo       = g_meas_cfg.lo;
    s->meas_hi       = g_meas_cfg.hi;
    return book_flush();
}

bool setup_load(int slot)
{
    if (slot < 0 || slot >= SETUP_N) return false;
    if (!s_book.slots[slot].used) return false;
    setup_slot_t s = s_book.slots[slot];   /* mutable kopie -> sanitizace */
    slot_sanitize(&s);
    g_brightness  = s.brightness;
    g_sound_muted = s.sound_muted ? 1 : 0;
    g_autodim_en  = s.autodim_en ? 1 : 0;
    g_autodim_sec = s.autodim_sec;
    g_theme_idx   = (uint8_t)(s.theme_idx & 0x07u);   /* 0..4 (UI_THEME_*) */
    g_lang_en     = s.lang_en ? 1 : 0;
    g_tz_offset_h = s.tz_offset_h;
    g_tz_auto     = s.tz_auto ? 1 : 0;
    g_ui_cfg      = s.ui_cfg;
    g_anim_enabled = s.anim_en ? 1 : 0;
    g_fx_enabled  = (uint16_t)(s.fx_en & FX_ALL);
    g_meas_cfg.math_en  = s.meas_math_en ? 1 : 0;
    g_meas_cfg.null_en  = s.meas_null_en ? 1 : 0;
    g_meas_cfg.limit_en = s.meas_limit_en ? 1 : 0;
    g_meas_cfg.alarm_en = s.meas_alarm_en ? 1 : 0;
    g_meas_cfg.m        = s.meas_m;
    g_meas_cfg.b        = s.meas_b;
    g_meas_cfg.null_ref = s.meas_null_ref;
    g_meas_cfg.lo       = s.meas_lo;
    g_meas_cfg.hi       = s.meas_hi;
    g_sys_cfg_dirty = 1;   /* načtené hodnoty se stanou i „aktuální" (auto-persist syscfg) */
    return true;
}

bool setup_erase(int slot)
{
    if (slot < 0 || slot >= SETUP_N) return false;
    memset(&s_book.slots[slot], 0, sizeof s_book.slots[slot]);
    return book_flush();
}

int setup_selftest(void)
{
    setup_slot_t s;
    memset(&s, 0, sizeof s);
    /* Out-of-range -> clamp na meze. */
    s.brightness = 10; s.autodim_sec = 5; s.tz_offset_h = 20; s.meas_m = 0.0;
    slot_sanitize(&s);
    if (s.brightness != 25 || s.autodim_sec != 300 || s.tz_offset_h != 14 || s.meas_m != 1.0) return 0;
    /* Pod dolní mezí zóny. */
    s.tz_offset_h = -30; slot_sanitize(&s);
    if (s.tz_offset_h != -12) return 0;
    /* Platné hodnoty zůstanou beze změny. */
    memset(&s, 0, sizeof s);
    s.brightness = 128; s.autodim_sec = 60; s.tz_offset_h = 2; s.meas_m = 2.0;
    slot_sanitize(&s);
    if (s.brightness != 128 || s.autodim_sec != 60 || s.tz_offset_h != 2 || s.meas_m != 2.0) return 0;
    return 1;
}
