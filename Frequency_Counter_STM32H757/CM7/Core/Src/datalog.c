/**
 * @file    datalog.c
 * @brief   Append-only kruhovy log stability mereni. Viz datalog.h.
 *
 * ── Rozvrzeni na ulozisti ──────────────────────────────────────────────────
 * Region = pole zaznamu po 32 B. Erase blok (W25Q sektor 4096 B) pojme presne
 * 128 zaznamu. Zapisuje se cistě dopredu; na hranici bloku se blok NEJDRIV smaze
 * (u NOR; u SD backendu erase_size==0 a krok odpada) a tim se zahodi 128
 * nejstarsich zaznamu -> kruh. Zadna metadata mimo zaznamy: pozice zapisu se po
 * bootu ODVODI ze seq cisel (viz find_head), takze log prezije reset i vypadek
 * napajeni bez zvlastni hlavicky.
 *
 * `seq` je monotonni pres cely region a zaroven slouzi jako priznak obsazenosti:
 * smazana flash = 0xFF -> seq == DATALOG_SEQ_EMPTY == volny slot.
 */
#include "datalog.h"
#include "w25q.h"
#include "w25q_map.h"
#include "freertos_shared.h"   /* g_sensors, g_rtc_text, g_spi_ok, qspiMutexHandle */
#include "fpga_freq.h"
#include "gps.h"
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"
#include <stdio.h>
#include <string.h>

/* Timeouty QSPI mutexu: tick bezi v defaultTask (krmi watchdog) -> pri obsazene
 * sbernici zapis radeji vynecha a zkusi za DATALOG_PERIOD_S znovu. Cteni/erase
 * z UI/UART smi cekat dele (erase sektoru az ~400 ms). */
#define DL_LOCK_TICK_MS   10u
#define DL_LOCK_READ_MS   500u
#define DL_LOCK_ERASE_MS  2000u

#define RECS_PER_BLOCK(b) ((b)->erase_size ? (b)->erase_size / DATALOG_REC_SIZE : 0u)

static const datalog_backend_t *s_be;
static bool     s_ready;
static bool     s_enabled = true;      /* prepisuje syscfg_load pres datalog_set_enabled */
static uint32_t s_head;                /* offset dalsiho zapisu [B] */
static uint32_t s_seq;                 /* seq posledniho zapsaneho (0 = zadny) */
static uint32_t s_count;               /* pocet platnych zaznamu (strop = capacity_rec) */
static uint32_t s_errors;
static bool     s_wrapped;
static uint32_t s_next_ms;             /* HAL_GetTick kdy vzorkovat priste */

/* ── Serializace zaznamu (LE, bez zavislosti na paddingu struktury) ────────── */

static uint16_t crc16(const uint8_t *d, uint32_t n)
{
    uint16_t c = 0xFFFF;                       /* CRC-16/CCITT-FALSE (jako w25q_store/FPGA) */
    for (uint32_t i = 0; i < n; i++) {
        c ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void put_u64(uint8_t *p, uint64_t v) { put_u32(p, (uint32_t)v); put_u32(p + 4, (uint32_t)(v >> 32)); }
static uint16_t get_u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t get_u64(const uint8_t *p) { return (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4) << 32); }

/* Rozvrzeni 32B zaznamu: 0 seq, 4 t_unix, 8 freq, 16 t_ocxo, 18 t_board,
 * 20 vc_mv, 22 rf_dbm10, 24 flags, 25 sats, 26 hdop10, 27 spare,
 * 28 CRC16 (bajty 0..27), 30 rezerva. */
static void pack_rec(uint8_t *b, const datalog_rec_t *r)
{
    memset(b, 0, DATALOG_REC_SIZE);
    put_u32(b + 0, r->seq);
    put_u32(b + 4, r->t_unix);
    put_u64(b + 8, r->freq_x100000);
    put_u16(b + 16, (uint16_t)r->t_ocxo_c100);
    put_u16(b + 18, (uint16_t)r->t_board_c100);
    put_u16(b + 20, (uint16_t)r->ocxo_vc_mv);
    put_u16(b + 22, (uint16_t)r->rf_dbm10);
    b[24] = r->flags;
    b[25] = r->sats;
    b[26] = r->hdop10;
    put_u16(b + 28, crc16(b, 28));
}

static bool unpack_rec(const uint8_t *b, datalog_rec_t *r)
{
    uint32_t seq = get_u32(b + 0);
    if (seq == DATALOG_SEQ_EMPTY || seq == 0) return false;    /* prazdny slot */
    if (crc16(b, 28) != get_u16(b + 28)) return false;         /* poskozeny zaznam */
    r->seq           = seq;
    r->t_unix        = get_u32(b + 4);
    r->freq_x100000  = get_u64(b + 8);
    r->t_ocxo_c100   = (int16_t)get_u16(b + 16);
    r->t_board_c100  = (int16_t)get_u16(b + 18);
    r->ocxo_vc_mv    = (int16_t)get_u16(b + 20);
    r->rf_dbm10      = (int16_t)get_u16(b + 22);
    r->flags         = b[24];
    r->sats          = b[25];
    r->hdop10        = b[26];
    return true;
}

/* ── Cas: "YYYY-MM-DD HH:MM:SS" (g_rtc_text, UTC) -> unix ──────────────────
 * RTC pise defaultTask, datalog_tick bezi ve stejnem tasku -> retezec je
 * konzistentni bez zamku. Ciste kalendarni (dny od epochy), bez time.h. */
static uint32_t days_from_civil(int y, int m, int d)
{
    y -= (m <= 2);                                   /* rok zacina brezenem */
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (uint32_t)((int32_t)era * 146097 + (int32_t)doe - 719468);   /* 1970-01-01 = 0 */
}

/* Rucni parser misto sscanf (mensi kod, zadna libc scanf zavislost) — cte pevne
 * "YYYY-MM-DD HH:MM:SS" a hlida ze na pozicich cislic OPRAVDU jsou cislice. */
static bool rd_num(const char *s, int n, int *out)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return true;
}

static uint32_t datalog_text_to_unix(const char *s)
{
    int Y, M, D, h, mi, sec;
    if (s == NULL || strlen(s) < 19) return 0;
    if (s[4] != '-' || s[7] != '-' || s[13] != ':' || s[16] != ':') return 0;
    if (!rd_num(s, 4, &Y) || !rd_num(s + 5, 2, &M) || !rd_num(s + 8, 2, &D)) return 0;
    if (!rd_num(s + 11, 2, &h) || !rd_num(s + 14, 2, &mi) || !rd_num(s + 17, 2, &sec)) return 0;
    if (Y < 1970 || Y > 2199 || M < 1 || M > 12 || D < 1 || D > 31) return 0;
    if (h > 23 || mi > 59 || sec > 60) return 0;
    return days_from_civil(Y, M, D) * 86400u + (uint32_t)(h * 3600 + mi * 60 + sec);
}

/* ── W25Q backend ─────────────────────────────────────────────────────────── */

/* Probe = plny w25q_init(), NE jen JEDEC: bez initu neni zapnute 4-byte
 * adresovani (EN4B) a `w25q_read` nad 16 MB by cetl uplne jinam. Volani je
 * idempotentni — syscfg_load ho uz mozna udelal, opakovani jen znovu srovna cip
 * (bezi pod qspiMutexHandle, takze do toho nikdo jiny nesahne). */
static bool w25q_be_probe(void) { return w25q_init(); }
static bool w25q_be_read(uint32_t off, uint8_t *buf, uint32_t len)
{
    return w25q_read(W25Q_DATA_BASE + off, buf, len);
}
static bool w25q_be_write(uint32_t off, const uint8_t *buf, uint32_t len)
{
    return w25q_write(W25Q_DATA_BASE + off, buf, len);
}
static bool w25q_be_erase(uint32_t off)
{
    return w25q_erase_sector(W25Q_DATA_BASE + off);
}

const datalog_backend_t datalog_backend_w25q = {
    .name = "W25Q", .probe = w25q_be_probe, .read = w25q_be_read,
    .write = w25q_be_write, .erase = w25q_be_erase,
    .erase_size = W25Q_SECTOR_SIZE, .capacity = W25Q_DATALOG_SIZE,  /* jen 1/3 DATA regionu, viz w25q_map.h */
};

/* ── Hledani pozice zapisu po bootu ───────────────────────────────────────────
 * Blok, ktery ma v PRVNIM zaznamu nejvyssi seq, je nejnovejsi (seq roste
 * monotonne pres cely region). V nem se pak linearne najde prvni volny slot.
 * Cte CELY 32B zaznam na blok a overuje jeho CRC (2026-08-16; drive jen 4 B se
 * seq, coz propustilo garbage — viz komentar ve skenu). Pri 5445 blocich
 * (1/3 DATA regionu) je to ~174 kB, ~38 ms @ 4,6 MB/s. Zbytecne to neroste:
 * QSPI cteni je stropovane pollingem FIFO, ne velikosti bloku.
 * ⚠️ Pro SD backend (erase_size == 0) je "blok" cely region a vnitrni scan by
 * byl linearni pres miliony zaznamu -> az bude SD ziva, nahradit BINARNIM
 * hledanim hranice seq (log je monotonni, takze binarni scan je primocary). */
static void find_head(void)
{
    uint32_t nblk = s_be->erase_size ? s_be->capacity / s_be->erase_size : 1u;
    uint32_t rpb  = RECS_PER_BLOCK(s_be);
    if (rpb == 0) rpb = s_be->capacity / DATALOG_REC_SIZE;   /* SD: jeden "blok" = cely region */

    uint32_t best_blk = 0, best_seq = 0;
    uint32_t blk_with_data = 0;         /* kolik bloku ma platny prvni zaznam (viz wrap nize) */
    bool found = false;
    for (uint32_t i = 0; i < nblk; i++) {
        /* ⚠️ Cte se CELY 32B zaznam a overuje se CRC (`unpack_rec`), ne jen 4 bajty
         * seq jako driv. Bez te kontroly prosel jako platny i GARBAGE z flash —
         * a protoze se bere blok s NEJVYSSIM seq, staci jediny poskozeny bajt,
         * aby si datalog myslel, ze hlava je uplne jinde. Presne to nastalo po
         * zmene kapacity regionu 2026-08-15: `seq` vyslo 3823341906 (~1,2 mld let
         * pri 10 s/zaznam) a stav logu byl cely mimo. */
        uint8_t b[DATALOG_REC_SIZE];
        datalog_rec_t probe;
        if (!s_be->read(i * s_be->erase_size, b, DATALOG_REC_SIZE)) continue;
        if (!unpack_rec(b, &probe)) continue;    /* prazdny NEBO poskozeny -> preskoc */
        uint32_t seq = probe.seq;
        blk_with_data++;
        if (!found || seq > best_seq) { found = true; best_seq = seq; best_blk = i; }
        /* ⚠️ Sken je 16352 QSPI transakci (cely DATA region po 4 KB blocich) a
         * `w25q_read` uvnitr POLLUJE FIFO bez yieldu. Bez tohoto ustoupeni by
         * defaultTask drzel CPU v kuse — a protoze prave defaultTask obnovuje
         * IWDG (watchdog_supervise), byl by dost dlouhy sken WATCHDOG RESET
         * (boot grace je jen 8 s). Yield po kazdych 512 blocich pusti ke slovu
         * UiTask/FpgaTask (heartbeaty) i zbytek smycky defaultTask. */
        if ((i & 511u) == 511u) osDelay(1);
    }

    if (!found) {                       /* prazdne uloziste */
        s_head = 0; s_seq = 0; s_count = 0; s_wrapped = false;
        return;
    }

    /* Prvni volny slot v nejnovejsim bloku.
     * ⚠️ Stejna CRC kontrola jako vyse (unpack_rec), NE jen syrovy `seq` — bez ni
     * tenhle vnitrni scan verí garbage stejne, jako to do 2026-08-16 delal vnejsi
     * (viz komentar u nej). Overeno na HW pres sondu 2026-08-17 PO fixu vnejsiho
     * scanu: `s_count` uz vyslo spravne (47043), ale `s_seq` byl 3823345955 —
     * temer identicka hodnota jako historicka garbage 3823341906 zminovana vyse
     * (stary obsah flash zpred zmenseni regionu 2026-08-15, ktery neni cisty 0xFF,
     * ale ani platny zaznam). Poskozeny/neplatny slot se ted bere jako HRANICE
     * hlavy stejne jako prazdny — dalsi realny zapis ho proste prepise spravnymi
     * daty (samo-opravne), takze zadne zvlastni osetreni netreba. */
    uint32_t base = best_blk * s_be->erase_size;
    uint32_t used = rpb;                /* default: blok je plny */
    uint32_t last = best_seq;
    for (uint32_t k = 0; k < rpb; k++) {
        uint8_t b[DATALOG_REC_SIZE];
        datalog_rec_t probe;
        if (!s_be->read(base + k * DATALOG_REC_SIZE, b, DATALOG_REC_SIZE)) { used = k; break; }
        if (!unpack_rec(b, &probe)) { used = k; break; }   /* prazdny NEBO poskozeny -> hranice */
        last = probe.seq;
    }
    s_seq  = last;
    s_head = base + used * DATALOG_REC_SIZE;
    if (s_head >= s_be->capacity) s_head = 0;

    /* ⚠️ Kolik zaznamu v logu JE, se pozna ze STAVU FLASH — ne z absolutni hodnoty
     * `seq`. Driv tu stalo `s_wrapped = (s_seq > cap_rec)`, coz mlcky predpoklada,
     * ze log zacal na seq=1 a od te doby se nezmenila kapacita ani se nic nesmazalo.
     * Po zmensení regionu na 1/3 (2026-08-15) to hlasilo PLNY log (696960 zaznamu),
     * zatimco realne jich bylo 42989 — export pak zbytecne prosel 653k prazdnych
     * slotu. `seq` navic po dostatecne dlouhem behu pretece, takze na nem pocet
     * zaznamu stavet nelze vubec.
     *
     * Pocitame z toho, kolik BLOKU ma platna data: bloky se plni sekvencne, takze
     * (pocet plnych bloku - 1) * rpb + zaznamy v bloku s hlavou. Kruh povazujeme
     * za obehnuty, kdyz maji data uz vsechny bloky krome jednoho — pri wrapu se
     * totiz nejstarsi blok prave smazal, takze prazdny zustava. */
    uint32_t cap_rec = s_be->capacity / DATALOG_REC_SIZE;
    s_wrapped = (nblk > 1u) && (blk_with_data >= nblk - 1u);
    if (s_wrapped) {
        s_count = cap_rec;
    } else {
        uint32_t c = (blk_with_data ? (blk_with_data - 1u) * rpb : 0u) + used;
        s_count = (c > cap_rec) ? cap_rec : c;
    }
}

void datalog_init(void)
{
    s_be = NULL; s_ready = false;

    if (osMutexAcquire(qspiMutexHandle, DL_LOCK_READ_MS) != osOK) return;
    /* SD ma prednost (vetsi, vyjimatelna); dokud neni osazena, probe() = false. */
    if (datalog_backend_sd.probe && datalog_backend_sd.probe())      s_be = &datalog_backend_sd;
    else if (datalog_backend_w25q.probe())                           s_be = &datalog_backend_w25q;
    /* Kapacita 0 by v write_rec/read_back znamenala deleni nulou -> backend s
     * nesmyslnou kapacitou radeji odmitnout (napr. nedokoncena SD implementace). */
    if (s_be != NULL && s_be->capacity < DATALOG_REC_SIZE) s_be = NULL;
    if (s_be != NULL) { find_head(); s_ready = true; }
    osMutexRelease(qspiMutexHandle);

    s_next_ms = HAL_GetTick() + DATALOG_PERIOD_S * 1000u;
    printf("datalog: %s %s (%lu zazn., seq %lu)\n",
           s_ready ? s_be->name : "--", s_ready ? "ready" : "NEDOSTUPNE",
           (unsigned long)s_count, (unsigned long)s_seq);
}

/* ── Sber jednoho vzorku ze zivych globalu ─────────────────────────────────── */

static int16_t sens_c100(sensor_id_t id)
{
    const sensor_stat_t *s = &g_sensors[id];
    if (!s->valid || s->samples == 0) return DATALOG_INVALID16;
    float v = s->last * 100.0f;
    if (v > 32000.0f || v < -32000.0f) return DATALOG_INVALID16;
    return (int16_t)(v < 0 ? v - 0.5f : v + 0.5f);
}

static int16_t sens_mv(sensor_id_t id)
{
    const sensor_stat_t *s = &g_sensors[id];
    if (!s->valid || s->samples == 0) return DATALOG_INVALID16;
    float v = s->last;
    if (v > 32000.0f || v < -32000.0f) return DATALOG_INVALID16;
    return (int16_t)(v + 0.5f);
}

static void sample(datalog_rec_t *r)
{
    memset(r, 0, sizeof *r);

    fpga_meas_t m;
    int use16 = 0;
    if (fpga_freq_get_last(&m)) {
        /* fpga_freq_select() ma vnitrni stav a smi ji volat JEN FpgaTask ->
         * tady jen precteme, ktera odbocka ma platna data (bez hystereze). */
        use16 = (m.error_flags & FPGA_ERR_MEAS) && !(m.status2 & FPGA_ST2_DIV16_ERR);
        r->freq_x100000 = use16 ? m.freq16_x100000 : m.frequency_x100000;
    }

    r->t_unix       = g_rtc_synced ? datalog_text_to_unix((const char *)g_rtc_text) : 0u;
    r->t_ocxo_c100  = sens_c100(SENS_T49);    /* OCXO */
    r->t_board_c100 = sens_c100(SENS_T48);    /* STM deska */
    r->ocxo_vc_mv   = sens_mv(SENS_ADS0);     /* ladici napeti */
    /* RF: ADS AIN1 je mV z AD8307; prevod na dBm dela UI (kalibrace g_calib).
     * Do logu jde SYROVE mV — kalibrace se muze zmenit, syrova hodnota ne. */
    r->rf_dbm10     = sens_mv(SENS_ADS1);

    gps_data_t g;
    gps_get(&g);
    uint8_t f = 0;
    if (g.valid)                                f |= DATALOG_F_GPS_VALID;
    if (g.fix_mode >= 3)                        f |= DATALOG_F_FIX_3D;
    if (g_spi_ok)                               f |= DATALOG_F_FPGA_LINK;
    if (g_freq_stale)                           f |= DATALOG_F_SIGNAL_LOST;
    if (use16)                                  f |= DATALOG_F_DIV16;
    if (!g.valid && g.fixes > 0)                f |= DATALOG_F_HOLDOVER;
    r->flags  = f;
    r->sats   = g.num_sat;
    r->hdop10 = (g.hdop > 0.0f && g.hdop < 25.0f) ? (uint8_t)(g.hdop * 10.0f + 0.5f) : 255u;
}

/* Zapise zaznam na s_head (vc. erase na hranici bloku). Ocekava drzeny mutex. */
static bool write_rec(const datalog_rec_t *r)
{
    if (s_be->erase_size && (s_head % s_be->erase_size) == 0) {
        /* Novy blok -> smazat (zahodi 128 nejstarsich zaznamu = kruh). */
        if (s_be->erase == NULL || !s_be->erase(s_head)) return false;
        if (s_head == 0 && s_seq != 0) s_wrapped = true;   /* obeh dokola */
    }
    uint8_t b[DATALOG_REC_SIZE];
    pack_rec(b, r);
    if (!s_be->write(s_head, b, DATALOG_REC_SIZE)) return false;

    s_head = (s_head + DATALOG_REC_SIZE) % s_be->capacity;
    s_seq  = r->seq;
    uint32_t cap_rec = s_be->capacity / DATALOG_REC_SIZE;
    if (s_count < cap_rec) s_count++;
    return true;
}

void datalog_tick(void)
{
    if (!s_ready || !s_enabled) return;
    if ((int32_t)(HAL_GetTick() - s_next_ms) < 0) return;
    s_next_ms += DATALOG_PERIOD_S * 1000u;

    datalog_rec_t r;
    sample(&r);
    r.seq = s_seq + 1u;

    /* Kratky timeout: pri obsazene flash (calib_save / syscfg / UART qspitest)
     * vzorek zahodime a jedeme dal — defaultTask nesmi zdrzet watchdog. */
    if (osMutexAcquire(qspiMutexHandle, DL_LOCK_TICK_MS) != osOK) { s_errors++; return; }
    bool ok = write_rec(&r);
    osMutexRelease(qspiMutexHandle);
    if (!ok) s_errors++;
}

void datalog_set_enabled(bool en) { s_enabled = en; }
bool datalog_enabled(void)         { return s_enabled; }

void datalog_get_status(datalog_status_t *out)
{
    if (out == NULL) return;
    out->backend      = s_ready ? s_be->name : "--";
    out->ready        = s_ready;
    out->enabled      = s_enabled;
    out->records      = s_count;
    out->capacity_rec = s_ready ? s_be->capacity / DATALOG_REC_SIZE : 0u;
    out->last_seq     = s_seq;
    out->write_errors = s_errors;
    out->wrapped      = s_wrapped;
}

bool datalog_read_back(uint32_t from_newest, datalog_rec_t *out)
{
    if (!s_ready || out == NULL || from_newest >= s_count) return false;
    /* s_head ukazuje ZA posledni zapsany -> zpet o (from_newest+1) zaznamu. */
    uint32_t back = (from_newest + 1u) * DATALOG_REC_SIZE;
    uint32_t off  = (s_head + s_be->capacity - (back % s_be->capacity)) % s_be->capacity;

    uint8_t b[DATALOG_REC_SIZE];
    if (osMutexAcquire(qspiMutexHandle, DL_LOCK_READ_MS) != osOK) return false;
    bool ok = s_be->read(off, b, DATALOG_REC_SIZE);
    osMutexRelease(qspiMutexHandle);
    return ok && unpack_rec(b, out);
}

void datalog_format_status(char *buf, int buflen)
{
    if (buf == NULL || buflen <= 0) return;
    if (!s_ready) { snprintf(buf, (size_t)buflen, "DATALOG NEDOSTUPNE (uloziste)"); return; }
    snprintf(buf, (size_t)buflen, "DATALOG %s %s %lu/%lu rec seq:%lu err:%lu%s",
             s_be->name, s_enabled ? "ON" : "OFF",
             (unsigned long)s_count, (unsigned long)(s_be->capacity / DATALOG_REC_SIZE),
             (unsigned long)s_seq, (unsigned long)s_errors, s_wrapped ? " WRAP" : "");
}

bool datalog_erase_all(void)
{
    if (!s_ready) return false;
    if (osMutexAcquire(qspiMutexHandle, DL_LOCK_ERASE_MS) != osOK) return false;
    bool ok = true;
    if (s_be->erase_size && s_be->erase) {
        for (uint32_t off = 0; off < s_be->capacity; off += s_be->erase_size) {
            if (!s_be->erase(off)) { ok = false; break; }
        }
    }
    if (ok) { s_head = 0; s_seq = 0; s_count = 0; s_wrapped = false; s_errors = 0; }
    osMutexRelease(qspiMutexHandle);
    return ok;
}

/* ── Selftest (pure logic, bez HW — soucast UART "selftest") ───────────────── */
bool datalog_selftest(void)
{
    /* 1) round-trip serializace vcetne zapornych teplot a plneho u64 kmitoctu */
    datalog_rec_t a = {
        .seq = 0x01020304u, .t_unix = 1782000000u,
        .freq_x100000 = 1000000012345ull,
        .t_ocxo_c100 = -1234, .t_board_c100 = 4567,
        .ocxo_vc_mv = 2500, .rf_dbm10 = -455,
        .flags = DATALOG_F_GPS_VALID | DATALOG_F_DIV16, .sats = 9, .hdop10 = 12,
    };
    uint8_t b[DATALOG_REC_SIZE];
    datalog_rec_t r;
    pack_rec(b, &a);
    if (!unpack_rec(b, &r)) return false;
    if (r.seq != a.seq || r.t_unix != a.t_unix || r.freq_x100000 != a.freq_x100000) return false;
    if (r.t_ocxo_c100 != a.t_ocxo_c100 || r.t_board_c100 != a.t_board_c100) return false;
    if (r.ocxo_vc_mv != a.ocxo_vc_mv || r.rf_dbm10 != a.rf_dbm10) return false;
    if (r.flags != a.flags || r.sats != a.sats || r.hdop10 != a.hdop10) return false;

    /* 2) poskozeny bajt musi CRC odhalit */
    b[10] ^= 0xFFu;
    if (unpack_rec(b, &r)) return false;

    /* 3) prazdny (smazana flash) slot neni zaznam */
    memset(b, 0xFF, sizeof b);
    if (unpack_rec(b, &r)) return false;

    /* 4) kalendar -> unix (vektory: epocha, prestupny rok, znamy datum) */
    if (datalog_text_to_unix("1970-01-01 00:00:00") != 0u) return false;
    if (datalog_text_to_unix("2000-03-01 00:00:00") != 951868800u) return false;
    if (datalog_text_to_unix("2026-07-20 12:34:56") != 1784550896u) return false;
    if (datalog_text_to_unix("nesmysl") != 0u) return false;

    /* 5) SD backend: 512B read-modify-write layer (SD zatím neaktivní, jen logika) */
    if (!datalog_sd_selftest()) return false;
    return true;
}
