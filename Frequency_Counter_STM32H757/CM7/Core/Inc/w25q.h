/**
 * @file    w25q.h
 * @brief   Winbond W25Q512JV (64 MB) QSPI NOR flash driver. Viz w25q.c.
 *
 * Deska: STM32H757 QUADSPI (Bank1) -> W25Q512JVFIQ. Piny PF10 CLK / PG6 NCS /
 * PD11 IO0 / PD12 IO1 / PF7 IO2 / PD13 IO3 (viz quadspi.c MSP). >16 MB -> nutne
 * 4-bytove adresovani (EN4B). Bring-up: nejdriv `qspiid` (JEDEC ID EF4020).
 */
#ifndef INC_W25Q_H_
#define INC_W25Q_H_

#include <stdint.h>
#include <stdbool.h>

#define W25Q_JEDEC_ID     0xEF4020u                 /* Winbond W25Q512JV: EF 40 20 */
#define W25Q_SIZE_BYTES   (64u * 1024u * 1024u)     /* 512 Mbit = 64 MB */
#define W25Q_PAGE_SIZE    256u                       /* zapis po strankach */
#define W25Q_SECTOR_SIZE  4096u                      /* nejmensi erase = 4 KB */

/** Precte JEDEC ID (3 bajty -> 24-bit, [23:16]=vyrobce). Nevyzaduje w25q_init.
 *  0 nebo != W25Q_JEDEC_ID = chip nekomunikuje. Bring-up krok 1. */
uint32_t w25q_read_jedec(void);

/** Init chipu: SW reset, overi JEDEC ID, zapne 4-byte adresovani + quad.
 *  Vrati true kdyz ID sedi (chip pripraven). Nutne pred read/write/erase. */
bool w25q_init(void);

/** Read (32-bit adresa, 4-byte mode). Quad Fast Read 0x6C (4-line) po quad-enable,
 *  jinak fallback Fast Read 0x0C (1-line). Blokujici polling. Vrati true = OK. */
bool w25q_read(uint32_t addr, uint8_t *buf, uint32_t len);

/** Write (handluje hranice 256B stranek). ⚠️ Cilova oblast MUSI byt PREDEM
 *  smazana (flash umi jen 1->0); erase-before-write resi vyssi vrstva. */
bool w25q_write(uint32_t addr, const uint8_t *buf, uint32_t len);

/** Smaze jeden 4 KB sektor (adresa se zarovna dolu na sektor). */
bool w25q_erase_sector(uint32_t addr);

/** Diagnosticky radek: "W25Q512 64MB ID:EF4020 OK" / "QSPI NOLINK ID:...". */
void w25q_format_status(char *buf, int buflen);

#endif /* INC_W25Q_H_ */
