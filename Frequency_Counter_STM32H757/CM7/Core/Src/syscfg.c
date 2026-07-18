/**
 * @file    syscfg.c
 * @brief   Viz syscfg.h.
 */
#include "syscfg.h"
#include "w25q.h"
#include "w25q_store.h"
#include "w25q_map.h"
#include "freertos_shared.h"   /* g_brightness, g_theme_light, g_tz_*, g_ui_cfg, ... */
#include "stm32h7xx_hal.h"     /* HAL_GetTick */
#include <string.h>

/* Verzovany blob (magic se zmeni pri nekompatibilni zmene layoutu; store sam
 * overuje CRC16 -> magic jen potvrzuje ze bajty patri syscfg). Pole zabalena
 * bez paddingu-zavislosti (cteme/zapisujeme celou strukturu, kompilator stejny). */
#define SYSCFG_BLOB_MAGIC   0x53434647u   /* "SCFG" */
#define SYSCFG_DEBOUNCE_MS  1500u         /* klid pred flash zapisem */

typedef struct {
    uint32_t magic;
    uint8_t  brightness;
    uint8_t  sound_muted;
    uint8_t  autodim_en;
    uint16_t autodim_sec;
    uint8_t  theme_light;
    uint8_t  lang_en;
    int8_t   tz_offset_h;
    uint8_t  tz_auto;
    uint8_t  ui_cfg;
} syscfg_blob_t;

static w25q_store_t s_store;

/* Naplni blob z aktualnich g_* globalu. */
static void pack(syscfg_blob_t *b)
{
    b->magic        = SYSCFG_BLOB_MAGIC;
    b->brightness   = g_brightness;
    b->sound_muted  = g_sound_muted;
    b->autodim_en   = g_autodim_en;
    b->autodim_sec  = g_autodim_sec;
    b->theme_light  = g_theme_light;
    b->lang_en      = g_lang_en;
    b->tz_offset_h  = g_tz_offset_h;
    b->tz_auto      = g_tz_auto;
    b->ui_cfg       = g_ui_cfg;
}

void syscfg_load(void)
{
    if (!w25q_init()) return;   /* flash nedostupna -> zustanou BKP/default hodnoty */
    if (!w25q_store_init(&s_store, W25Q_CONFIG_BASE, W25Q_CONFIG_SECTORS)) return;

    /* Warm reset: BKP uz drzi nejnovejsi -> flash NEpretezovat (byla by max o
     * debounce okno starsi). Store je pripraveny pro zapis. */
    if (g_syscfg_bkp_valid) return;

    /* Studeny start (BKP smazana power-cyclem): flash je autoritativni. */
    syscfg_blob_t b;
    uint32_t n = w25q_store_read(&s_store, &b, sizeof b);
    if (n != sizeof(b) || b.magic != SYSCFG_BLOB_MAGIC) return;   /* prazdno/nova flash */

    /* Sanitizace (CRC uz sedi, ale kdyby layout/verze poskodila rozsah). */
    g_brightness  = (b.brightness < 25) ? 25 : b.brightness;
    g_sound_muted = b.sound_muted ? 1 : 0;
    g_autodim_en  = b.autodim_en ? 1 : 0;
    g_autodim_sec = (b.autodim_sec >= 15 && b.autodim_sec <= 600) ? b.autodim_sec : 60;
    g_theme_light = b.theme_light ? 1 : 0;
    g_lang_en     = b.lang_en ? 1 : 0;
    g_tz_offset_h = (b.tz_offset_h < -12) ? -12 : (b.tz_offset_h > 14 ? 14 : b.tz_offset_h);
    g_tz_auto     = b.tz_auto ? 1 : 0;
    g_ui_cfg      = b.ui_cfg;
}

bool syscfg_save(void)
{
    if (!s_store.ready) return false;   /* syscfg_load nevolan / flash nedostupna */
    syscfg_blob_t b;
    pack(&b);
    return w25q_store_write(&s_store, &b, sizeof b);
}

void syscfg_flash_tick(void)
{
    if (!s_store.ready) return;

    static syscfg_blob_t snap;
    static uint8_t  have_snap = 0;
    static uint8_t  pending   = 0;
    static uint32_t change_ms = 0;

    syscfg_blob_t cur;
    pack(&cur);

    if (!have_snap) { snap = cur; have_snap = 1; return; }   /* baseline pri bootu, bez zapisu */

    if (memcmp(&cur, &snap, sizeof cur) != 0) {
        snap = cur; change_ms = HAL_GetTick(); pending = 1;   /* zmena -> resetuj debounce */
    } else if (pending && (uint32_t)(HAL_GetTick() - change_ms) >= SYSCFG_DEBOUNCE_MS) {
        if (syscfg_save()) pending = 0;   /* po klidu jeden zapis; pri chybe zkusi priste */
    }
}
