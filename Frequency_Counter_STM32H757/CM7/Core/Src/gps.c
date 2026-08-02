/*
 * gps.c — u-blox NEO-7M, NMEA parser ($xxRMC + $xxGGA) na USART1 @ 9600.
 * Viz gps.h. RX: ISR -> GpsRxQueue -> defaultTask drain -> gps_feed_char().
 */
#include "gps.h"
#include "usart.h"        /* huart1, RxByte */

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>
#include <stdio.h>

/* ── Aktualni stav (zapisuje GpsTask pres parser, cte se pres gps_get) ──── */
static gps_data_t s_gps;

/* ── Skladani prijate vety ─────────────────────────────────────────────── */
static char    s_line[96];
static uint8_t s_len;

/* ── Diagnostika linky STM<->GPS ───────────────────────────────────────── */
static volatile uint32_t s_raw_bytes;   /* vsechny prijate bajty (i smeti) */
static char    s_last_raw[96];           /* posledni kompletni radek (pred parsem) */

/* ── Maly bezfloatovy/bezlibcovy helpers ───────────────────────────────── */
static int atoi_simple(const char *s)
{
  if (!s) return 0;
  int sign = 1;
  if (*s == '-') { sign = -1; s++; } else if (*s == '+') { s++; }
  int v = 0;
  while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
  return v * sign;
}

static float atof_simple(const char *s)
{
  if (!s) return 0.0f;
  float sign = 1.0f;
  if (*s == '-') { sign = -1.0f; s++; } else if (*s == '+') { s++; }
  float v = 0.0f;
  while (*s >= '0' && *s <= '9') { v = v * 10.0f + (float)(*s - '0'); s++; }
  if (*s == '.') {
    s++;
    float frac = 0.1f;
    while (*s >= '0' && *s <= '9') { v += (float)(*s - '0') * frac; frac *= 0.1f; s++; }
  }
  return v * sign;
}

static uint8_t d2(const char *s) { return (uint8_t)((s[0] - '0') * 10 + (s[1] - '0')); }

static uint8_t hexnib(char c)
{
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
  if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
  return 0;
}

/* "ddmm.mmmm" / "dddmm.mmmm" -> stupne (float), znamenko dle polokoule. */
static float nmea_coord(const char *s, char hemi)
{
  if (!s || !*s) return 0.0f;
  const char *dot = strchr(s, '.');
  if (!dot) return 0.0f;
  int intlen = (int)(dot - s);
  if (intlen < 3) return 0.0f;
  int dlen = intlen - 2;            /* delka casti se stupni (2 cislice = minuty) */
  if (dlen <= 0 || dlen > 6) return 0.0f;
  char degbuf[8];
  memcpy(degbuf, s, (size_t)dlen);
  degbuf[dlen] = '\0';
  float deg = (float)atoi_simple(degbuf);
  float minutes = atof_simple(s + dlen);   /* "mm.mmmm" */
  float val = deg + minutes / 60.0f;
  if (hemi == 'S' || hemi == 'W') val = -val;
  return val;
}

/* Rozdeli vetu (in-place) podle ',' na pole. Vraci pocet poli. */
static int tokenize(char *s, char **f, int max)
{
  int n = 0;
  f[n++] = s;
  for (char *p = s; *p && n < max; p++) {
    if (*p == ',') { *p = '\0'; f[n++] = p + 1; }
  }
  return n;
}

/* $xxRMC: 1=time 2=status 3=lat 4=N/S 5=lon 6=E/W 7=spd 9=date(ddmmyy) */
static void parse_rmc(char **f, int nf)
{
  if (nf < 10) return;
  uint8_t valid = (uint8_t)(f[2][0] == 'A');
  uint8_t hh = 0, mm = 0, ss = 0;
  if (strlen(f[1]) >= 6) { hh = d2(f[1]); mm = d2(f[1] + 2); ss = d2(f[1] + 4); }
  uint8_t dd = 0, mo = 0; uint16_t yy = 0;
  if (strlen(f[9]) >= 6) { dd = d2(f[9]); mo = d2(f[9] + 2); yy = (uint16_t)(2000 + d2(f[9] + 4)); }
  float lat = valid ? nmea_coord(f[3], f[4][0]) : 0.0f;
  float lon = valid ? nmea_coord(f[5], f[6][0]) : 0.0f;
  float spd = atof_simple(f[7]);

  taskENTER_CRITICAL();
  s_gps.valid = valid;
  s_gps.hour = hh; s_gps.minute = mm; s_gps.second = ss;
  if (dd) { s_gps.day = dd; s_gps.month = mo; s_gps.year = yy; }
  if (valid) { s_gps.lat_deg = lat; s_gps.lon_deg = lon; s_gps.speed_kn = spd; s_gps.fixes++; }
  s_gps.sentences++;
  taskEXIT_CRITICAL();
}

/* $xxGGA: 1=time 2=lat 3=N/S 4=lon 5=E/W 6=quality 7=numSat 8=HDOP 9=alt */
static void parse_gga(char **f, int nf)
{
  if (nf < 10) return;
  uint8_t q  = (uint8_t)atoi_simple(f[6]);
  uint8_t ns = (uint8_t)atoi_simple(f[7]);
  float   hd = atof_simple(f[8]);
  float   alt = atof_simple(f[9]);

  taskENTER_CRITICAL();
  s_gps.fix_quality = q;
  s_gps.num_sat = ns;
  s_gps.hdop = hd;
  if (q > 0) s_gps.alt_m = alt;
  s_gps.sentences++;
  taskEXIT_CRITICAL();
}

/* $xxGSA: 2=fixMode(1/2/3) ... 15=PDOP 16=HDOP 17=VDOP */
static void parse_gsa(char **f, int nf)
{
  if (nf < 18) return;
  uint8_t mode = (uint8_t)atoi_simple(f[2]);
  float pdop = atof_simple(f[15]);
  float hdop = atof_simple(f[16]);
  taskENTER_CRITICAL();
  s_gps.fix_mode = mode;
  s_gps.pdop = pdop;
  s_gps.hdop = hdop;
  s_gps.sentences++;
  taskEXIT_CRITICAL();
}

/* $xxGSV: 1=total 2=msgnum 3=inview, pak skupiny po 4: prn,elev,azim,snr(C/N0).
 * Akumuluje druzice napric zpravami davky (msgnum 1..total) do s_gsv_acc,
 * po posledni zprave (msgnum>=total) commitne do s_gps.sats. Bezi jen v defaultTask
 * (jediny kontext) -> akumulator bez zamku; kriticka sekce jen kolem s_gps.
 * POZOR: predpoklada JEDNO souhvezdi (GPGSV). Az bude GLONASS (viz [[gps-todo]]),
 * GLGSV davky maji vlastni total/msgnum a resetovaly by tento akumulator -> nutna
 * akumulace per-talker (GP/GL/GN) nebo spolecny buffer klicovany podle talkeru. */
static gps_sat_t s_gsv_acc[GPS_MAX_SATS];
static uint8_t   s_gsv_n;

static void parse_gsv(char **f, int nf)
{
  if (nf < 4) return;
  int total  = atoi_simple(f[1]);
  int msgnum = atoi_simple(f[2]);
  uint8_t inview = (uint8_t)atoi_simple(f[3]);

  if (msgnum <= 1) s_gsv_n = 0;            /* novy batch -> reset akumulatoru */
  for (int i = 4; i + 3 < nf && s_gsv_n < GPS_MAX_SATS; i += 4) {
    uint8_t prn = (uint8_t)atoi_simple(f[i]);
    if (prn == 0) continue;                /* prazdny slot */
    s_gsv_acc[s_gsv_n].prn  = prn;
    s_gsv_acc[s_gsv_n].elev = (uint8_t)atoi_simple(f[i + 1]);
    s_gsv_acc[s_gsv_n].azim = (uint16_t)atoi_simple(f[i + 2]);  /* 0..359, 0 = sever */
    s_gsv_acc[s_gsv_n].snr  = (uint8_t)atoi_simple(f[i + 3]);   /* prazdne -> 0 = netrackovana */
    s_gsv_n++;
  }

  taskENTER_CRITICAL();
  s_gps.sats_in_view = inview;
  if (total > 0 && msgnum >= total) {      /* davka kompletni -> commit */
    for (uint8_t k = 0; k < s_gsv_n; k++) s_gps.sats[k] = s_gsv_acc[k];
    s_gps.sat_count = s_gsv_n;
  }
  s_gps.sentences++;
  taskEXIT_CRITICAL();
}

static void parse_line(char *l)
{
  if (l[0] != '$') return;

  /* checksum *HH (XOR mezi '$' a '*') */
  char *star = strchr(l, '*');
  if (star) {
    if (star[1] == '\0' || star[2] == '\0') return;   /* useknuty checksum -> zahodit (i guard proti cteni za '\0') */
    uint8_t cs = 0;
    for (char *p = l + 1; p < star; p++) cs ^= (uint8_t)*p;
    uint8_t given = (uint8_t)((hexnib(star[1]) << 4) | hexnib(star[2]));
    if (cs != given) return;          /* poskozena veta -> zahodit */
    *star = '\0';
  }

  char *f[24];
  int nf = tokenize(l, f, 24);
  if (nf < 1 || strlen(f[0]) < 6) return;

  const char *typ = f[0] + 3;         /* preskoc "$" + 2-znaky talker (GP/GN/GL) */
  if      (strncmp(typ, "RMC", 3) == 0) parse_rmc(f, nf);
  else if (strncmp(typ, "GGA", 3) == 0) parse_gga(f, nf);
  else if (strncmp(typ, "GSA", 3) == 0) parse_gsa(f, nf);
  else if (strncmp(typ, "GSV", 3) == 0) parse_gsv(f, nf);
}

/* ── UBX odesilani (STM -> GPS, blokujici; volano jen pri init) ─────────── */
/* Slozi UBX ramec: B5 62 | cls id | len(LE) | payload | CK_A CK_B (Fletcher
 * pres cls..payload) a odvysila pres USART1 TX (PB14). */
static void ubx_send(uint8_t cls, uint8_t id, const uint8_t *pl, uint16_t n)
{
  uint8_t f[48];
  if (n > 32) return;
  uint16_t i = 0;
  f[i++] = 0xB5; f[i++] = 0x62;
  f[i++] = cls;  f[i++] = id;
  f[i++] = (uint8_t)(n & 0xFF); f[i++] = (uint8_t)(n >> 8);
  for (uint16_t k = 0; k < n; k++) f[i++] = pl[k];
  uint8_t a = 0, b = 0;
  for (uint16_t k = 2; k < i; k++) { a = (uint8_t)(a + f[k]); b = (uint8_t)(b + a); }
  f[i++] = a; f[i++] = b;
  HAL_UART_Transmit(&huart1, f, i, 100);
}

/* TIMEPULSE kmitocty. S FIXEM = GPSDO PLL reference (musi sedet s delickou OCXO,
 * JP2: 100 kHz / 1 MHz -> pro 1MHz zmen na 1000000). BEZ FIXU = 10 Hz: sama
 * FREKVENCE slouzi desce jako lock-indikator (detektor: 100 kHz -> disciplinuj,
 * 10 Hz -> hold VC OCXO = holdover). NIKDY nevystup 100 kHz z interniho osc modulu. */
#define GPS_TP_FREQ_HZ        100000u   /* s fixem: GPSDO PLL reference */
#define GPS_TP_FREQ_NOFIX_HZ  10u       /* bez fixu: MANDATORY 10 Hz (hold indikator) */

/* UBX-CFG-TP5 (0x06 0x31, 32 B): freqPeriodLock (fix) = 100 kHz disciplinovany na
 * GNSS + zarovnany na UTC (alignToTow); freqPeriod (no lock) = 10 Hz. 50% strida. */
void gps_config_timepulse(void)
{
  uint32_t fl = GPS_TP_FREQ_HZ;         /* s fixem */
  uint32_t fn = GPS_TP_FREQ_NOFIX_HZ;   /* bez fixu */
  uint8_t pl[32] = {0};
  pl[0]  = 0;                                  /* tpIdx = 0 (TIMEPULSE) */
  pl[8]  = (uint8_t)fn; pl[9]  = (uint8_t)(fn >> 8);  /* freqPeriod (no lock) = 10 Hz */
  pl[10] = (uint8_t)(fn >> 16); pl[11] = (uint8_t)(fn >> 24);
  pl[12] = (uint8_t)fl; pl[13] = (uint8_t)(fl >> 8);  /* freqPeriodLock (fix) = 100 kHz */
  pl[14] = (uint8_t)(fl >> 16); pl[15] = (uint8_t)(fl >> 24);
  pl[19] = 0x80;                               /* pulseLenRatio = 50% (2^31) */
  pl[23] = 0x80;                               /* pulseLenRatioLock = 50% */
  /* flags: active|lockGnssFreq|lockedOtherSet|isFreq|alignToTow|polarity = 0x6F */
  pl[28] = 0x6F;
  ubx_send(0x06, 0x31, pl, 32);
}

/* UBX-CFG-TMODE2 (0x06 0x3D, 28 B): timeMode 0=disabled 1=survey-in. Pro survey-in
 * naseto svinMinDur [s] (off 20) + svinAccLimit [mm] (off 24), zbytek 0. */
void gps_survey_in_cmd(uint32_t min_dur_s, uint32_t acc_limit_mm)
{
  uint8_t pl[28] = {0};
  pl[0] = 1;                                   /* timeMode = survey-in */
  pl[20] = (uint8_t)min_dur_s; pl[21] = (uint8_t)(min_dur_s >> 8);
  pl[22] = (uint8_t)(min_dur_s >> 16); pl[23] = (uint8_t)(min_dur_s >> 24);
  pl[24] = (uint8_t)acc_limit_mm; pl[25] = (uint8_t)(acc_limit_mm >> 8);
  pl[26] = (uint8_t)(acc_limit_mm >> 16); pl[27] = (uint8_t)(acc_limit_mm >> 24);
  ubx_send(0x06, 0x3D, pl, 28);
}
void gps_survey_disable_cmd(void)
{
  uint8_t pl[28] = {0};                        /* timeMode = 0 (disabled) */
  ubx_send(0x06, 0x3D, pl, 28);
}

/* ── Verejne API ───────────────────────────────────────────────────────── */
void gps_init(void)
{
  /* NEO-7M default 9600 8N1. Prenastav baud (MX generuje 115200) — regen-safe,
   * nezavisle na .ioc. */
  huart1.Init.BaudRate = 9600;
  HAL_UART_Init(&huart1);

  /* TIMEPULSE config (100 kHz GPSDO PLL reference; viz gps_config_timepulse).
   * Vyzaduje zapojene STM
   * PB14 (USART1 TX) -> GPS RX. Posila se v RAM modulu (plati do power-cyklu).
   * ⚠️ MUSI byt PRED HAL_UART_Receive_IT: HAL_UART_Transmit (blokujici) drzi
   * huart->Lock; kdyby uz bezel RX IT, RxCpltCallback by pri re-armu dostal
   * HAL_BUSY a RX by NAVZDY umrel (displej zamrzne na "acquiring"). Po TX uz na
   * USART1 zadny dalsi TX nebezi (printf -> USB) => re-arm RX uz nikdy nekoliduje. */
  gps_config_timepulse();

  /* Az ted nahodit RX v IT rezimu. */
  HAL_UART_Receive_IT(&huart1, &RxByte, 1);
}

void gps_feed_char(char c)
{
  s_raw_bytes++;                       /* dukaz, ze z GPS vubec neco chodi */
  if (c == '\r' || c == '\n') {
    if (s_len > 0) {
      s_line[s_len] = '\0';
      /* zachyt syrovy radek PRED parsem (parse_line meni s_line in-place) */
      taskENTER_CRITICAL();
      strncpy(s_last_raw, s_line, sizeof(s_last_raw) - 1);
      s_last_raw[sizeof(s_last_raw) - 1] = '\0';
      taskEXIT_CRITICAL();
      parse_line(s_line);
      s_len = 0;
    }
    return;
  }
  if (s_len < sizeof(s_line) - 1) s_line[s_len++] = c;
  else s_len = 0;                     /* preteceni -> reset (vadny ramec) */
}

void gps_get(gps_data_t *out)
{
  taskENTER_CRITICAL();
  *out = s_gps;
  taskEXIT_CRITICAL();
}

static void fmt_coord(float v, char pos, char neg, char *out, int n)
{
  char h = (v >= 0.0f) ? pos : neg;
  if (v < 0.0f) v = -v;
  int32_t ud = (int32_t)(v * 1000000.0f + 0.5f);   /* mikro-stupne */
  snprintf(out, (size_t)n, "%ld.%06ld%c", (long)(ud / 1000000), (long)(ud % 1000000), h);
}

void gps_format_status(char *buf, int n)
{
  gps_data_t g;
  gps_get(&g);
  if (!g.valid) {
    snprintf(buf, (size_t)n, "NO FIX (SAT:%02u SENT:%lu RAW:%lu)",
             g.num_sat, (unsigned long)g.sentences, (unsigned long)s_raw_bytes);
    return;
  }
  char la[16], lo[16];
  fmt_coord(g.lat_deg, 'N', 'S', la, sizeof la);
  fmt_coord(g.lon_deg, 'E', 'W', lo, sizeof lo);
  snprintf(buf, (size_t)n, "FIX:%u SAT:%02u %04u-%02u-%02u %02u:%02u:%02u %s %s ALT:%dm",
           g.fix_quality, g.num_sat, g.year, g.month, g.day,
           g.hour, g.minute, g.second, la, lo, (int)g.alt_m);
}

/* Selftest cistych parser helperu (nemeni s_gps ani s_line -> bezpecne z UartTasku
 * za behu; soucast UART "selftest"). Kontroluje prevod NMEA souradnic, ciselne
 * konverze a hex nibble (checksum aritmetika). */
bool gps_selftest(void)
{
  int ok = 1;
  float lat = nmea_coord("5007.7104", 'N');      /* 50° + 7.7104' = 50.128507° */
  ok &= (lat > 50.1284f && lat < 50.1287f);
  float lon = nmea_coord("01430.5000", 'W');     /* -(14° + 30.5') = -14.508333° */
  ok &= (lon < -14.5082f && lon > -14.5085f);
  ok &= (nmea_coord("123", 'N') == 0.0f);        /* bez tecky -> 0 (odmitnuto) */
  ok &= (atoi_simple("-123") == -123);
  ok &= (atoi_simple("047") == 47);
  float f = atof_simple("12.75");
  ok &= (f > 12.749f && f < 12.751f);
  ok &= (d2("47") == 47);
  ok &= (hexnib('a') == 10 && hexnib('F') == 15 && hexnib('7') == 7);
  /* XOR checksum vzoroveho tela vety: "GPGLL" = 0x47^0x50^0x47^0x4C^0x4C */
  uint8_t cs = 0; const char *b = "GPGLL";
  for (const char *p = b; *p; p++) cs ^= (uint8_t)*p;
  ok &= (cs == ('G' ^ 'P' ^ 'G' ^ 'L' ^ 'L'));
  printf("gps: parser selftest %s\n", ok ? "OK" : "FAIL");
  return ok != 0;
}

void gps_format_raw(char *buf, int n)
{
  char last[96];
  uint32_t raw, sent;
  taskENTER_CRITICAL();
  strncpy(last, s_last_raw, sizeof(last) - 1);
  last[sizeof(last) - 1] = '\0';
  raw  = s_raw_bytes;
  sent = s_gps.sentences;
  taskEXIT_CRITICAL();
  snprintf(buf, (size_t)n, "RAW:%lu SENT:%lu last=[%s]",
           (unsigned long)raw, (unsigned long)sent, last[0] ? last : "(zatim nic)");
}
