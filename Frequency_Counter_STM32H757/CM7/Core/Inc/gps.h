/*
 * gps.h — u-blox NEO-7M GPS na USART1 (9600 8N1), NMEA parser.
 *
 * USART1 je vyhrazen pro GPS (konzole je na USB CDC). RX bajty jdou z ISR
 * (HAL_UART_RxCpltCallback v usart.c) do GpsRxQueue; vybira je drain
 * v defaultTasku (freertos.c, vlastni GpsTask neexistuje) a krmi
 * gps_feed_char(), ktery sklada NMEA vety a parsuje $xxRMC/GGA/GSA/GSV.
 *
 * Bez float v printf: souradnice se drzi celociselne ve stupnich x 1e7
 * (lat_e7/lon_e7, audit F-0070) a formatuji se pres integer extrakci
 * (newlib-nano nelinkuje %f).
 *
 * STM -> GPS: UBX-CFG-TP5 (TIMEPULSE: s fixem 1PPS na FPGA PIN33, bez fixu 10 Hz;
 * 1PPS do STM32 nevede), na vyzadani UBX-CFG-TMODE2 (SURVEY) a UBX-CFG-GNSS
 * (UART `gps glonass`). Navazujici: viz [[gps-todo]].
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
  /* 🔴 CELOCISELNE v 1e-7 stupne, NE float (audit F-0070). `float` ma pro
   * hodnotu ~50 stupnu ULP 2^-18 = 3,81e-6 stupne, tedy **kvantizacni krok
   * 0,42 m** — a self-survey (#53) z techto hodnot pocita horizontalni rozptyl,
   * ktery ma byt meritkem konvergence. Pod tu mez se tedy nedostal, at bezel jak
   * chtel dlouho, a vypadalo to jako vlastnost anteny.
   * 1e-7 stupne = ~1,1 cm, tedy pod rozlisenim NMEA i pod tim, co GPS umi.
   * ⚠️ Skutecny zisk limituje POCET DESETIN MINUT, ktere prijimac posila:
   * `ddmm.mmmm` (4) = 1,85 m, `ddmm.mmmmm` (5) = 18,5 cm. Typ uz tedy prestal
   * byt uzkym hrdlem, ale rozliseni zaznamu ano. */
  int32_t  lat_e7;      /* stupne x 1e7, + sever / - jih */
  int32_t  lon_e7;      /* stupne x 1e7, + vychod / - zapad */
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
 * nahodi RX v IT rezimu a posle UBX-CFG-TP5 (TIMEPULSE 1PPS s fixem / 10 Hz bez).
 * Vola se na zacatku draineru v defaultTask. */
void gps_init(void);

/* UBX-CFG-TP5: TIMEPULSE = s fixem 1PPS (pulz 100 ms, nabezna hrana na zacatku
 * UTC sekundy; jde na FPGA PIN33 pres GPS_CLK_Buff — do STM nevede), bez fixu
 * 10 Hz se stridou 50 % (frekvence = indikator fixu).
 * Vyzaduje STM PB14 (USART1 TX) -> GPS RX. Vola gps_init a gps_tick (1x/min).
 * TX bezi v preruseni; volat jen z tasku pri bezicim scheduleru. */
void gps_config_timepulse(void);

/* Periodicka obnova TP5 (1x/min, audit F-0219) — konfigurace zije jen v RAM
 * modulu a ACK se necte. Volat VYHRADNE z defaultTasku (vedle drainu GPS). */
void gps_tick(void);

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
 * (jednosouhvězdí firmware) = neškodné; parser je na GLGSV připraven tak jako tak.
 * @return true = přenos spuštěn (doručení modulu nedokazuje), false = neodesláno. */
bool gps_config_gnss(void);

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
