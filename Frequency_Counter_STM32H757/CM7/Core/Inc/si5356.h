/**
 * @file    si5356.h
 * @brief   Silicon Labs Si5356A clock generator @ I2C1 0x70 (FPGA deska).
 *
 * Aplikuje kompletni register map z ClockBuilder Pro (CBPro) + oficialni
 * SiLabs "apply" proceduru (disable outputs -> soft reset -> enable outputs).
 * Register map je export z CBPro (Si5356-RevB-Registers.h): CLK0/1 = 100 MHz
 * reference FPGA, CLK2/3, CLK4/5, CLK6/7 vystupy na konektory (vychozi 50/150/200 MHz,
 * dal je meni okno GENERATOR pres si5356_gen_*). Frekvence jsou DANE TOUTO MAPOU.
 *
 * Pri zmene konfigurace: v CBPro nastav vystupy/fazi, exportuj "C Code Header"
 * a nahrad obsah REGMAP[] v si5356.c (format {addr, val, mask} je stejny).
 */
#ifndef SI5356_H
#define SI5356_H

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define SI5356_I2C_ADDR   (0x70u << 1)   /* 7-bit 0x70 -> HAL 8-bit */

/* ── Bitove masky stavu (AN565) ─────────────────────────────────────────────
 * 🔴 JEDINY ZDROJ. Do 2026-09-02 byly tytez masky definovane DVAKRAT
 * (`SI5356_*` v `app_gpsdo.c`, `SI_*` v `screen_main.c`) — presne ten druh
 * duplicity, pred kterou projekt varuje: jednou uz se u tohohle cipu bitova
 * mapa rozesla s realitou (bit2 vs bit3, viz [[si5356-los-clkin]]).
 *
 * ⚠️ TYTEZ pozice plati pro ZIVY registr 218 (0xDA) I pro STICKY 247 (0xF7).
 * ⚠️ `SI5356_LOS_XTAL` se na teto desce NEHODNOTI — krystal XA/XB neni osazen
 *    (piny uzemnene), takze bit je TRVALE 1; status `0x04` je normalni stav.
 * ⚠️ `SI5356_PLL_LOL` se pri FYZICKE ztrate vstupu NEasertuje (AN565: LOL je
 *    rozdil >5000 ppm na PFD, ne odpojeny vstup) -> ztratu reference hlasi
 *    `SI5356_LOS_CLKIN`. */
#define SI5356_SYS_CAL    (1u << 0)
#define SI5356_LOS_XTAL   (1u << 2)
#define SI5356_LOS_CLKIN  (1u << 3)
#define SI5356_PLL_LOL    (1u << 4)

/** Bity, ktere na teto desce znamenaji SKUTECNY problem s referenci. */
#define SI5356_FAULT_MASK (SI5356_LOS_CLKIN | SI5356_PLL_LOL)

/** Aplikuje register map + apply proceduru. Volat jednou (boot) na I2C1.
 *  @return true pokud vsechny I2C zapisy ACKovaly. */
bool si5356_init(I2C_HandleTypeDef *hi2c);

/** Vysledek `si5356_init_quiet`. */
typedef struct {
    uint8_t present;     /* 1 = cip odpovedel na 0x70 */
    uint8_t writes_ok;   /* 1 = vsechny zapisy register mapy i apply procedury ACKovaly */
    uint8_t st_ok;       /* 1 = status 218 po initu precten */
    uint8_t st;          /* status 218 po initu (platny jen pri st_ok) */
} si5356_init_res_t;

/** Totez co `si5356_init`, ale BEZ printf — pro SensorsTask (automaticka re-inicializace pri trvale ztrate
 *  zamku, STATUS #295), ktery printf nesmi. Stejne jako `si5356_init` vrati generator na vychozi stav REGMAP.
 *  @return true pokud vsechny I2C zapisy ACKovaly. */
bool si5356_init_quiet(I2C_HandleTypeDef *hi2c, si5356_init_res_t *r);

/** Precte status reg 218 (0xDA). Bitova mapa dle AN565: bit0=SYS_CAL (probiha
 *  kalibrace), bit2=LOS_XTAL (krystal XA/XB — na teto desce NEOSAZEN, piny
 *  uzemnene -> bit je TRVALE 1, benigni), bit3=LOS_CLKIN (ztrata 10 MHz na
 *  pinu 4 — SKUTECNA ztrata reference; pozor, PLL_LOL se pri fyzicke ztrate
 *  vstupu neasertuje), bit4=PLL_LOL (rozdil >5000 ppm na PFD). */
bool si5356_read_status(I2C_HandleTypeDef *hi2c, uint8_t *status);

/* ── STICKY stav (registr 247 = 0xF7) ───────────────────────────────────────
 * 🔴 PROC to existuje: registr 218 je ZIVY, takze kratky vypadek reference mezi
 * dvema cteními (polling 2x/s) je pro firmware NEVIDITELNY. U kmitoctoveho
 * normalu je to metrologicky podstatne — kolisani BEHEM mereni znehodnocuje
 * vysledek, a pristroj o nem dosud nemel jak vedet.
 *
 * AN565 (overeno primo v `Si5356A_AN565.pdf`): 247 je STICKY verze 218 se
 * SHODNYMI bitovymi pozicemi, typ R/W, a plati *„Only a soft or POR reset or
 * writing a 0 to this bit will clear it."* Sticky bit tedy podrzi i
 * mikrosekundovy glitch a polling 2x/s ho spolehlive zachyti.
 *
 * ⚠️ INTR pin se kvuli tomu zapojovat NEMUSI — pridal by jen latenci (us vs
 * 500 ms), ne detekci. (A na teto desce ma zustat NC bez pull-upu, viz STATUS #103.) */
#define SI5356_REG_STICKY  0xF7u   /* 247 */

/* 🔴 JEDNORAZOVE ARMOVANI po startu. ZMERENO na desce 2026-09-02: hned po bootu
 * hlasi sticky **LOS_CLKIN + PLL_LOL + SYS_CAL**, zatimco zivy registr 218 je
 * cisty (`0x04` = jen neosazeny krystal). Je to HISTORIE ZE ZASYNCHRONIZOVANI —
 * `si5356_init` dela soft reset a nez se PLL chytne, vsechny tri bity legitimne
 * probliknou. Overeno: po vynulovani zustava sticky 0 i po desitkach sekund.
 *
 * Bez armovani by tedy pristroj kricel "vypadek reference" po KAZDEM zapnuti —
 * a uzivatel by se to naucil ignorovat, cimz by hlaseni ztratilo smysl
 * (tatáz chyba jako falesne `SBERNICE MRTVA`, STATUS #114).
 *
 * ⚠️ Armuje se BEZ OHLEDU na zivy stav: kdyby reference chybela i po arm case,
 * hlasi to ZIVY registr 218 (SYS pilulka cervena). Sticky ma jediny ukol —
 * zachytit to, co mezi dvema ctenimi ziveho registru zmizi. */
#define SI5356_STICKY_ARM_MS  5000u

/** Precte sticky registr 247. Bitove pozice = stejne jako u `si5356_read_status`. */
bool si5356_read_sticky(I2C_HandleTypeDef *hi2c, uint8_t *sticky);

/** Vynuluje sticky bity uvedene v `mask` (AN565: maze se ZAPISEM NULY).
 *  Read-modify-write, takze ostatni sticky bity zustanou zachovane. */
bool si5356_clear_sticky(I2C_HandleTypeDef *hi2c, uint8_t mask);

/* ── GENERATOR (CLK2/3, CLK4/5, CLK6/7) ─────────────────────────────────────
 * Kanal 0/1/2 = dvojice CLK2/3, CLK4/5, CLK6/7 (MultiSynth B/C/D). CLK0/1 (reference
 * FPGA, 100 MHz) se odsud NEMENI. VCO je spolecny 10 MHz x 220 = 2,2 GHz.
 * Meze dle datasheetu Si5356A (Tab. 3, Clock Output Frequency): 1 .. 200 MHz.
 * MultiSynth sam umi 5..200 MHz (AN565), pod 5 MHz ho doplni R delic (1..32). */
#define SI5356_VCO_HZ         2200000000ull
#define SI5356_GEN_N          3u
#define SI5356_MS_F_MIN_HZ    5000000ull
#define SI5356_GEN_F_MIN_HZ   1000000u       /* datasheet Tab. 3: FO min 1 MHz (R=8 z MS 8 MHz) */
#define SI5356_GEN_F_MAX_HZ   200000000u     /* datasheet Tab. 3: FO max 200 MHz */
/* Zlomek Hz v mHz (0..999), v0.42.13: test TDC s pomalu ujizdejici fazi proti taktu FPGA
 * (napr. 10 000 000,040 Hz -> faze projde 10 ns za 2,5 s). Jmenovatel P3 ma 30 bitu, takze
 * presny zlomek se nemusi vejit -> nejlepsi aproximace retezovym zlomkem; SKUTECNY kmitocet
 * (z P1/P2/P3) vypisuje `gen` na uHz. Zlomek se NEUKLADA (syscfg nese jen cele Hz). */
#define SI5356_GEN_MHZ_MAX    999u
#define SI5356_MS_P3_MAX      0x3FFFFFFFu    /* P3 (a P2) maji v registrech 30 bitu */

enum { SI5356_GEN_OK = 0, SI5356_GEN_ERR_I2C = 1, SI5356_GEN_ERR_RANGE = 2 };

typedef struct {
    volatile uint32_t freq_hz;   /* pozadovany kmitocet, cela cast [Hz] */
    volatile uint16_t mhz;       /* zlomek [mHz] 0..999 (neuklada se) */
    volatile uint8_t  on;        /* 1 = vystup povolen */
    volatile uint8_t  pend;      /* 1 = zadost ceka na SensorsTask */
    volatile uint8_t  state;     /* SI5356_GEN_* (vysledek posledniho zapisu) */
} si5356_gen_t;

/** Stav a zadosti generatoru. UI/UART zapisuji pres si5356_gen_request, zapis do cipu
 *  dela VYHRADNE SensorsTask (vlastnik I2C1) pres si5356_gen_service. */
extern volatile si5356_gen_t g_si_gen[SI5356_GEN_N];

/** Parametry MultiSynthu: f_ms = VCO/(a+b/c) = f_out * 2^rexp. */
typedef struct { uint8_t rexp; uint32_t p1, p2, p3; } si5356_ms_t;

/** Cista funkce (bez I2C): kmitocet `f_hz` + `mhz` [mHz] -> P1/P2/P3/R. false = mimo rozsah.
 *  Presny racionalni vypocet; kdyz se presny zlomek do P3 nevejde (jen se zlomkem Hz), nejlepsi
 *  aproximace (posledni sblizeny zlomek s P3 <= SI5356_MS_P3_MAX). Pro cela Hz (mhz = 0) dava
 *  TYTEZ registry jako vypocet do v0.42.12 (host test tests/host/t_si5356_gen.c). */
bool     si5356_gen_calc_frac(uint32_t f_hz, uint16_t mhz, si5356_ms_t *m);
/** Zpetny prepocet: jaky kmitocet [uHz] z techto parametru cip vyrobi (relativni
 *  presnost double ~1e-16, tj. < 0,1 uHz do 200 MHz). */
uint64_t si5356_gen_actual_uhz(const si5356_ms_t *m);
/** Zakoduje P1/P2/P3 do 10 bajtu registru MS (B+0..B+9). */
void     si5356_gen_pack(const si5356_ms_t *m, uint8_t b[10]);
/** Zapise jednu dvojici do cipu (OEB_x -> R + MS -> OEB_x), bez soft resetu. */
bool     si5356_gen_apply(I2C_HandleTypeDef *hi2c, unsigned ch, uint32_t f_hz, uint16_t mhz, bool on);
/** Zadost z UI/UART (neblokuje), cele Hz: zlomek se vynuluje. */
void     si5356_gen_request(unsigned ch, uint32_t f_hz, bool on);
/** Zadost se zlomkem [mHz]. */
void     si5356_gen_request_frac(unsigned ch, uint32_t f_hz, uint16_t mhz, bool on);
/** Obsluha zadosti — jen SensorsTask pod i2c1Mutex. */
void     si5356_gen_service(I2C_HandleTypeDef *hi2c);
/** Vychozi stav = to, co zapsal REGMAP (vola si5356_init). */
void     si5356_gen_defaults(void);
/** Precte R(1) + MS(10) + reg 230(1) pro diagnostiku (`gen`). */
bool     si5356_gen_readback(I2C_HandleTypeDef *hi2c, unsigned ch, uint8_t regs[12]);

#endif /* SI5356_H */
