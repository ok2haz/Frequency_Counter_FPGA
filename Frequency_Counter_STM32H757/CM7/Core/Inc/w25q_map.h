/**
 * @file    w25q_map.h
 * @brief   Genericke rozdeleni W25Q512 (64 MB) flash na regiony.
 *
 * Deska je pouzitelna i pro jine ucely nez GPSDO -> regiony jsou OBECNE, ne
 * aplikacne specificke. Zarovnano na 64 KB bloky (16 sektoru a 4 KB). CONFIG a
 * CALIB jsou wear-leveled blob store (w25q_store.h); DATA je volny prostor
 * (raw w25q pristup / budouci log modul).
 */
#ifndef INC_W25Q_MAP_H_
#define INC_W25Q_MAP_H_

#include "w25q.h"   /* W25Q_SIZE_BYTES, W25Q_SECTOR_SIZE */

/* Runtime nastaveni (caste zmeny) — wear-leveled. */
#define W25Q_CONFIG_BASE     0x000000u
#define W25Q_CONFIG_SECTORS  16u                      /* 64 KB */

/* Kalibrace + zarizeni parametry (zridka menene) — wear-leveled. */
#define W25Q_CALIB_BASE      0x010000u
#define W25Q_CALIB_SECTORS   16u                      /* 64 KB */

/* Generic bulk / logy (app-defined, zatim nevyuzito). */
#define W25Q_DATA_BASE       0x020000u
#define W25Q_DATA_SIZE       (W25Q_SIZE_BYTES - W25Q_DATA_BASE)   /* ~63,875 MB */

#endif /* INC_W25Q_MAP_H_ */
