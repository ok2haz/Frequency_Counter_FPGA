/**
 * @file    w25q_store.h
 * @brief   Genericky wear-leveled blob store nad W25Q regionem.
 *
 * Jeden blob (max W25Q_STORE_MAX_BLOB) na region. KRUHOVE wear-leveling: kazdy
 * zapis jde do DALSIHO sektoru (round-robin pres cely region), nejnovejsi platny
 * seq vyhrava. POWER-SAFE: payload se zapise PRVNI, hlavicka (s magic) NAPOSLED
 * -> vypadek napajeni uprostred zapisu = magic chybi = zaznam neplatny, stary
 * (nizsi seq, ale platny) zustava. Nezna aplikacni obsah — jen bajty.
 *
 * Pouziti: instancuj w25q_store_t na region z w25q_map.h (CONFIG/CALIB), volej
 * init (najde nejnovejsi zaznam), pak read/write. Vyzaduje uspesny w25q_init().
 */
#ifndef INC_W25Q_STORE_H_
#define INC_W25Q_STORE_H_

#include <stdint.h>
#include <stdbool.h>

#define W25Q_STORE_HDR       16u                       /* hlavicka zaznamu [B] */
#define W25Q_STORE_MAX_BLOB  (4096u - W25Q_STORE_HDR)  /* 4080 B (1 sektor - hlavicka) */

typedef struct {
    uint32_t base;      /* pocatek regionu (sector-aligned) */
    uint32_t sectors;   /* pocet 4 KB sektoru (>= 2) */
    uint32_t active;    /* addr sektoru s aktualnim zaznamem (platny jen kdyz seq>0) */
    uint32_t seq;       /* seq aktualniho zaznamu; 0 = PRAZDNO (jediny sentinel prazdna) */
    bool     ready;
} w25q_store_t;

/** Proskenuje region, najde nejnovejsi platny zaznam. Vrati true (i pro prazdny
 *  region -> ready, active=0). false jen pri sectors < 2. */
bool w25q_store_init(w25q_store_t *s, uint32_t base, uint32_t sectors);

/** Precte aktualni blob do buf. Vrati delku (0 = prazdno / nevalidni CRC / buf
 *  mensi nez ulozeny blob). ⚠️ maxlen musi byt >= ulozena delka (jinak 0). */
uint32_t w25q_store_read(w25q_store_t *s, void *buf, uint32_t maxlen);

/** Zapise blob (kruhove do dalsiho sektoru, seq+1, power-safe). true = OK. */
bool w25q_store_write(w25q_store_t *s, const void *buf, uint32_t len);

#endif /* INC_W25Q_STORE_H_ */
