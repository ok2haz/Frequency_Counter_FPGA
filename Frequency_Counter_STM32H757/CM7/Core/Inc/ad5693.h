/**
 * @file    ad5693.h
 * @brief   AD5693R — 16bitovy DAC ladiciho napeti OCXO (I2C1, adresa 0x4C).
 *
 * ⬜ CIP ZATIM NENI OSAZEN (objednan, 2026-10-02). Kod je pripraveny tak, aby
 * po osazeni fungoval bez zasahu: SensorsTask cip sam najde (sonda kazdych
 * AD5693_REPROBE_MS), zapise control registr a precte aktualni kod. Dokud cip
 * chybi, jen hlasi "nenalezen" — zadny zapis do OCXO, zadna obnova sbernice.
 *
 * Zapojeni (netlist FPGA_Module_2_1, kicad-cli export 2026-10-02):
 *   U12 AD5693RxRM: VDD=+5V, VLOGIC=+3V3 (R108/D6), ~RESET=VLOGIC,
 *   ~LDAC=GND (vystup se meni kazdym zapisem), A0=GND -> 7bit adresa 0x4C.
 *   VREF (pin 9) ma jen C55 100n -> NUTNE verze AD5693**R** (interni 2,5 V
 *   reference). AD5693 bez "R" nema z ceho referenci brat.
 *   VOUT -> R78 47k / C68 1u -> U15 OPA365 -> R72 82R -> OCXO U13 pin VC.
 *   Skutecne napeti na pinu VC cte U14 (sledovac) -> /2,5 -> ADS1115 AIN0
 *   (SENS_ADS0) = nezavisla kontrola konec-konec, nezavisla na readbacku.
 *
 * Protokol (overeno proti Linux IIO driveru ad5686.c/ad5696-i2c.c, ktery
 * AD5693R obsluhuje register mapou AD5683, data 16 b bez posunu):
 *   zapis = 3 B: [CMD<<4 | 0][D15..D8][D7..D0]
 *   CMD 0x3 = zapis input + DAC registru, CMD 0x4 = control registr:
 *   D14:13 power-down (00 = normal), D12 REF (0 = interni reference ZAPNUTA),
 *   D11 GAIN (1 = x2, tj. 0..2*VREF = 0..5 V).
 *   Readback: prikazovy bajt NOP + opakovany START + 2 B (MSB prvni).
 *   ⚠️ HYPOTEZA: Linux posila pred opakovanym STARTem 3 B (NOP + 2 nulove),
 *   tady jen prikazovy bajt. Kdyby po osazeni `dac` hlasil "readback NESEDI",
 *   je to prvni podezreni — zapis tim dotceny neni, kontrolu dava i AIN0.
 *
 * Rozsah: GAIN x2 -> Vout = kod * 5000 mV / 65536 (76,3 uV/LSB). Duvod: VDD je
 * 5 V a delic AIN0 je dimenzovany na 0..5 V. Po zapnuti napajeni je DAC dle
 * datasheetu na nule s GAIN x1 -> 0 V; control registr se zapisuje pri kodu 0,
 * vystup se tim nehne. Pri resetu JEN STM32 (DAC napajeny dal) si DAC drzi
 * posledni kod — firmware ho pri startu proto NEPREPISUJE, jen precte.
 * ⬜ Power-on stav (nula/stred) overit hned po prvnim zapnuti s osazenym cipem:
 *   `dac` (readback) a `sensors` (AIN0) jeste PRED prvnim zapisem.
 *
 * Vlakna: I2C1 vlastni SensorsTask -> I2C transakce dela VYHRADNE jeho
 * `ad5693_service()` pod i2c1MutexHandle. UART/UI jen nastavi zadost
 * (`ad5693_request_*`) a cekaji na `req_* == 0` — vzor `g_si5356_clr_req`.
 * Obe strany bezi na CM7, volatile poradi pristupu staci (bez __DMB).
 * 🔴 Zapis kodu PRELADI OCXO = posune kmitocet cele reference. Kod se proto
 * NIKDY nezapisuje automaticky, jen na vyslovnou zadost (dnes UART `dac`).
 * Regulacni smycka GPSDO a perzistence kodu pres power-cyklus (syscfg) jsou
 * dalsi krok (Faze B), ne soucast tohoto driveru.
 */
#ifndef AD5693_H
#define AD5693_H

#include <stdint.h>

#define AD5693_ADDR7          0x4Cu
#define AD5693_ADDR8          (AD5693_ADDR7 << 1)

#define AD5693_CMD_NOP        0x00u                /* CMD 0x0 << 4 */
#define AD5693_CMD_WRITE_DAC  0x30u                /* CMD 0x3 << 4: input + DAC registr */
#define AD5693_CMD_CTRL       0x40u                /* CMD 0x4 << 4: control registr */

#define AD5693_CTRL_GAIN_X2   (1u << 11)
#define AD5693_CTRL_REF_OFF   (1u << 12)           /* 0 = interni reference ZAPNUTA */
/* Interni reference ZAP, GAIN x2, power-down normal (D14:13 = 00). */
#define AD5693_CTRL_VALUE     ((uint16_t)AD5693_CTRL_GAIN_X2)

#define AD5693_FS_MV          5000u                /* 2 x VREF (2500 mV) */
#define AD5693_REPROBE_MS     10000u               /* sonda, dokud cip chybi */

/* Vysledek posledni zadosti (`req_result`). */
enum {
    AD5693_RES_NONE = 0,      /* zadost jeste nevyrizena */
    AD5693_RES_OK,
    AD5693_RES_ABSENT,        /* cip neodpovida na 0x4C (neosazen?) */
    AD5693_RES_I2C_ERR,       /* cip je, ale transakce selhala */
    AD5693_RES_VERIFY_FAIL    /* zapis prosel, readback vratil jiny kod */
};

typedef struct {
    /* --- stav: zapisuje VYHRADNE SensorsTask --- */
    uint8_t  probed;          /* 1 = aspon jednou se sondovalo */
    uint8_t  present;         /* 1 = cip potvrdil adresu (ACK) */
    uint8_t  ctrl_ok;         /* 1 = control registr zapsan (int. ref, GAIN x2) */
    uint8_t  code_valid;      /* 1 = `code` je znamy (zapsany nebo prevzaty readbackem) */
    uint8_t  rb_ok;           /* 1 = posledni readback probehl */
    uint16_t code;            /* posledni zapsany, pri startu prevzaty kod */
    uint16_t code_rb;         /* posledni precteny kod */
    uint32_t writes;          /* uspesne zapisy kodu */
    uint32_t errors;          /* selhane I2C transakce s pritomnym cipem */
    uint32_t rb_mismatch;     /* readback != zapsany kod */
    uint32_t probe_ms;        /* HAL_GetTick() posledni sondy */
    /* --- zadosti: nastavuje UART/UI, nuluje SensorsTask po vyrizeni --- */
    uint16_t req_code;
    uint8_t  req_write;       /* 1 = zapsat `req_code` */
    uint8_t  req_probe;       /* 1 = znovu sondovat + zapsat control registr */
    uint8_t  req_result;      /* AD5693_RES_* posledni zadosti */
} ad5693_state_t;

extern volatile ad5693_state_t g_dac;   /* definice: freertos_task_sensors.c */

static inline uint32_t ad5693_code_to_mv(uint16_t code)
{
    return ((uint32_t)code * AD5693_FS_MV + 32768u) >> 16;   /* max 327 707 768 < 2^32 */
}

static inline uint16_t ad5693_mv_to_code(uint32_t mv)
{
    if (mv >= AD5693_FS_MV) return 0xFFFFu;
    return (uint16_t)((mv * 65536u + AD5693_FS_MV / 2u) / AD5693_FS_MV);   /* < 2^32 pro mv < 5000 */
}

/* Zadost o zapis kodu. Flag se nastavuje AZ POSLEDNI — SensorsTask cte
 * `req_code` teprve po videni flagu. @return 0 = predchozi zadost jeste bezi. */
static inline int ad5693_request_write(uint16_t code)
{
    if (g_dac.req_write || g_dac.req_probe) return 0;
    g_dac.req_code   = code;
    g_dac.req_result = AD5693_RES_NONE;
    g_dac.req_write  = 1u;
    return 1;
}

static inline int ad5693_request_probe(void)
{
    if (g_dac.req_write || g_dac.req_probe) return 0;
    g_dac.req_result = AD5693_RES_NONE;
    g_dac.req_probe  = 1u;
    return 1;
}

static inline const char *ad5693_res_text(uint8_t r)
{
    switch (r) {
        case AD5693_RES_OK:          return "OK";
        case AD5693_RES_ABSENT:      return "cip nenalezen (neosazen?)";
        case AD5693_RES_I2C_ERR:     return "chyba I2C";
        case AD5693_RES_VERIFY_FAIL: return "zapsano, ale readback NESEDI";
        default:                     return "nevyrizeno";
    }
}

#endif /* AD5693_H */
