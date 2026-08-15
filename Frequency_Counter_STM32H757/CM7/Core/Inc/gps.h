/*
 * gps.h — u-blox NEO-7M GPS na USART1 (9600 8N1), NMEA parser.
 *
 * USART1 je vyhrazen pro GPS (konzole je na USB CDC). RX bajty jdou z ISR
 * (HAL_UART_RxCpltCallback v usart.c) do GpsRxQueue; GpsTask je vybira a krmi
 * gps_feed_char(), ktery sklada NMEA vety a parsuje $xxRMC + $xxGGA.
 *
 * Bez float v printf: souradnice se drzi jako float (HW FPU), ale formatuji se
 * pres integer extrakci (newlib-nano nelinkuje %f).
 *
 * Hotovo: NMEA parser, UI (GPS okno + header), TIMEPULSE (UBX-CFG-TP5).
 * Navazujici (viz [[gps-todo]]): RTC sync hodin, GLONASS (UBX-CFG-GNSS).
 */
#ifndef INC_GPS_H_
#define INC_GPS_H_

#include <stdint.h>
#include <stdbool.h>

#define GPS_MAX_SATS 24   /* max druzic v poli sats[] (GSV, multi-souhvezdi); vic ignorujeme */

/* Souhvezdi (index do GSV akumulatoru + barveni sky plotu). Odvozeno z 2-znak
 * NMEA talkeru: GP=GPS, GL=GLONASS, GA=Galileo, GB/BD=BeiDou. */
typedef enum {
  GPS_CONSTEL_GPS = 0,
  GPS_CONSTEL_GLONASS,
  GPS_CONSTEL_GALILEO,
  GPS_CONSTEL_BEIDOU,
  GPS_CONSTEL_N          /* pocet podporovanych souhvezdi */
} gps_constel_t;

/* Jedna druzice z GSV: PRN, elevace, sila signalu C/N0. */
typedef struct {
  uint8_t  prn;      /* cislo druzice */
  uint8_t  elev;     /* elevace [°] (0..90) */
  uint8_t  snr;      /* C/N0 [dB-Hz], 0 = netrackovana (prazdne pole v GSV) */
  uint8_t  constel;  /* gps_constel_t — souhvezdi (per-talker, sky plot) */
  uint16_t azim;     /* azimut [°] (0..359, 0 = sever) — sky plot v GPS okne */
} gps_sat_t;

typedef struct {
  uint8_t  valid;       /* 1 = posledni veta dava platny fix (RMC status 'A') */
  uint8_t  fix_quality; /* GGA: 0 = no fix, 1 = GPS, 2 = DGPS */
  uint8_t  num_sat;     /* GGA: pocet pouzitych druzic */
  uint8_t  hour, minute, second;   /* UTC cas */
  uint8_t  day, month;
  uint16_t year;        /* 4-mistny (2000+) */
  float    lat_deg;     /* stupne, + sever / - jih */
  float    lon_deg;     /* stupne, + vychod / - zapad */
  float    alt_m;       /* nadmorska vyska [m] (GGA) */
  float    speed_kn;    /* rychlost nad zemi [uzly] (RMC) */
  uint8_t  fix_mode;    /* GSA: 1 = no fix, 2 = 2D, 3 = 3D */
  uint8_t  sats_in_view;/* GSV: pocet viditelnych druzic */
  float    hdop;        /* GGA/GSA horizontalni DOP */
  float    pdop;        /* GSA pozicni DOP */
  uint32_t sentences;   /* pocet naparsovanych vet RMC/GGA/GSA/GSV (CRC ok) — diag */
  uint32_t fixes;       /* pocet platnych fixu — diag */
  gps_sat_t sats[GPS_MAX_SATS];  /* druzice v dosahu (PRN + C/N0), z GSV */
  uint8_t   sat_count;           /* pocet platnych polozek v sats[] */
} gps_data_t;

/* Inicializace: prepne USART1 na 9600 8N1 (regen-safe, nezavisle na .ioc),
 * nahodi RX v IT rezimu a posle UBX-CFG-TP5 (TIMEPULSE 100 kHz/10 Hz).
 * Vola se na zacatku draineru v defaultTask. */
void gps_init(void);

/* UBX-CFG-TP5: TIMEPULSE = s fixem 100 kHz (GPSDO PLL reference, disciplinovane
 * na GNSS), bez fixu 10 Hz (frekvence = lock indikator -> deska drzi VC OCXO).
 * Vyzaduje STM PB14 (USART1 TX) -> GPS RX. Vola gps_init; lze i samostatne. */
void gps_config_timepulse(void);

/* Self-survey (UBX-CFG-TMODE2): pozadá přijímač o survey-in (průměrování polohy
 * → time-only mód → lepší 1PPS). ⚠️ Účinné jen na timing-grade přijímačích
 * (LEA-6T/M8T…); NEO-7M příkaz nejspíš NAKne = neškodné. Firmwarové průměrování
 * polohy (app vrstva) běží nezávisle na tomto příkazu. Blokující TX (jen na tap). */
void gps_survey_in_cmd(uint32_t min_dur_s, uint32_t acc_limit_mm);
/* Vypne time-mód (timeMode=0). */
void gps_survey_disable_cmd(void);

/* UBX-CFG-GNSS (0x06 0x3E): zapne GPS+SBAS+GLONASS(+QZSS) souběžně → přijímač
 * začne vysílat GLGSV a per-talker GSV se skládá do sats[] napříč souhvezdími.
 * ⚠️ Best-effort a NEvolá se automaticky z gps_init: reconfig GNSS nejde bez HW
 * ověřit a špatný blok by mohl vypnout GPS → spouští se JEN explicitně (UART
 * "gps glonass") na HW, kde uživatel výsledek vidí. NEO-7M příkaz může NAKnout
 * (jednosouhvězdí firmware) = neškodné; parser je na GLGSV připraven tak jako tak. */
void gps_config_gnss(void);

/* Krmeni parseru jednim bajtem (vola GpsTask z GpsRxQueue). */
void gps_feed_char(char c);

/* Atomicky zkopiruje aktualni stav GPS. */
void gps_get(gps_data_t *out);

/* Jednoradkovy stav pro konzoli/displej, napr.:
 *   "FIX:1 SAT:07 2026-06-28 12:34:56 50.123456N 14.654321E" nebo "NO FIX (SAT:03)". */
void gps_format_status(char *buf, int n);

/* Diagnostika linky STM<->GPS: pocet syrovych bajtu, validnich vet a posledni
 * prijaty NMEA radek doslova (i pri vadnem checksumu). */
void gps_format_raw(char *buf, int n);

/* Selftest cistych parser helperu (souradnice/cisla/hex) — nemeni zadny sdileny
 * stav, bezpecne za behu. Soucast UART "selftest". @return true = OK. */
bool gps_selftest(void);

#endif /* INC_GPS_H_ */
