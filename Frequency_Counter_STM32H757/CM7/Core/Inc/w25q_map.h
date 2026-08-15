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

/* Ulozene sestavy (instrument setup profily, #) — wear-leveled blob store.
 * Vsech N slotu = JEDEN blob (male, << 4080 B cap). ⚠️ Pridano 2026-08-01:
 * DATA_BASE posunuto 0x020000 -> 0x030000 (o 64 KB) => datalog se po tomto flashi
 * jednou zalozi znovu (stare zaznamy na 0x020000 osiroti — benigni, bring-up). */
#define W25Q_SETUP_BASE      0x020000u
#define W25Q_SETUP_SECTORS   16u                      /* 64 KB (wear-leveling headroom) */

/* Generic bulk / logy (datalog). */
#define W25Q_DATA_BASE       0x030000u
#define W25Q_DATA_SIZE       (W25Q_SIZE_BYTES - W25Q_DATA_BASE)   /* ~63,8 MB */

/* ⚠️ Datalog ZAMERNE vyuzije jen ~1/3 DATA regionu (zbytek zustava volny pro dalsi
 * bulk pouziti — fonty XIP, rekonstrukce Allan pyramidy apod.). Zarovnano DOLU na
 * erase sektor (4 KB), aby to byl cely pocet bloku. 1/3 = ~22,3 MB = 696 960 zaznamu
 * po 32 B => pri 10s vzorkovani ~80 dni kruhoveho logu (pak se prepisuje nejstarsi).
 * Pozn.: plny region by byl ~242 dni (NE „600", jak driv chybne uvadela dokumentace). */
#define W25Q_DATALOG_SIZE    (((W25Q_DATA_SIZE / 3u) / W25Q_SECTOR_SIZE) * W25Q_SECTOR_SIZE)

#endif /* INC_W25Q_MAP_H_ */
