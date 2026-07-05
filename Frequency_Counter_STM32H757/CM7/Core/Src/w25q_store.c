/**
 * @file    w25q_store.c
 * @brief   Genericky wear-leveled blob store nad W25Q. Viz w25q_store.h.
 *
 * Hlavicka zaznamu (16 B) na zacatku sektoru, vse LITTLE-ENDIAN:
 *   0  u32  MAGIC = 0x53544F52 ("STOR")
 *   4  u8   VERSION = 1
 *   5  u8   rezerva = 0
 *   6  u16  LEN (delka payloadu, <= W25Q_STORE_MAX_BLOB)
 *   8  u32  SEQ (monotonni; vyssi = novejsi)
 *   12 u16  CRC16 payloadu (CRC-16/CCITT-FALSE)
 *   14 u16  rezerva = 0
 *   16 ..   PAYLOAD (LEN bajtu)
 */
#include "w25q_store.h"
#include "w25q.h"
#include <string.h>

#define STORE_MAGIC   0x53544F52u   /* "STOR" */
#define STORE_VER     0x01u

/* CRC-16/CCITT-FALSE (shodne s fpga_freq / FPGA protokolem). */
static uint16_t crc16(const uint8_t *d, uint32_t n)
{
    uint16_t c = 0xFFFF;
    for (uint32_t i = 0; i < n; i++) {
        c ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

/* Precte hlavicku sektoru. true = platna (magic+ver+len_sane). seq, len, crc ven. */
static bool read_hdr(uint32_t addr, uint32_t *seq, uint16_t *len, uint16_t *crc)
{
    uint8_t h[W25Q_STORE_HDR];
    if (!w25q_read(addr, h, sizeof h)) return false;
    uint32_t magic = (uint32_t)h[0] | ((uint32_t)h[1] << 8) |
                     ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
    if (magic != STORE_MAGIC || h[4] != STORE_VER) return false;
    uint16_t l = (uint16_t)(h[6] | (h[7] << 8));
    if (l > W25Q_STORE_MAX_BLOB) return false;
    *len = l;
    *seq = (uint32_t)h[8] | ((uint32_t)h[9] << 8) |
           ((uint32_t)h[10] << 16) | ((uint32_t)h[11] << 24);
    *crc = (uint16_t)(h[12] | (h[13] << 8));
    return true;
}

bool w25q_store_init(w25q_store_t *s, uint32_t base, uint32_t sectors)
{
    s->base = base; s->sectors = sectors;
    s->active = 0; s->seq = 0; s->ready = false;
    if (sectors < 2) return false;

    /* Init cte jen 16B hlavicky (zadny velky buffer na stacku). Diky poradi
     * zapisu (hlavicka naposled) je pritomnost magic zaruka platneho payloadu;
     * payload CRC se overuje az v read(). */
    bool found = false; uint32_t best_seq = 0, best_addr = 0;
    for (uint32_t i = 0; i < sectors; i++) {
        uint32_t addr = base + i * W25Q_SECTOR_SIZE;
        uint32_t seq; uint16_t len, crc;
        if (!read_hdr(addr, &seq, &len, &crc)) continue;
        if (!found || seq > best_seq) { found = true; best_seq = seq; best_addr = addr; }
    }
    if (found) { s->active = best_addr; s->seq = best_seq; }
    s->ready = true;
    return true;
}

uint32_t w25q_store_read(w25q_store_t *s, void *buf, uint32_t maxlen)
{
    /* seq==0 = prazdno (NE active==0: adresa 0x0 je platny sektor CONFIG regionu). */
    if (!s->ready || s->seq == 0 || buf == NULL) return 0;
    uint32_t seq; uint16_t len, crc;
    if (!read_hdr(s->active, &seq, &len, &crc)) return 0;
    if (len == 0 || len > maxlen) return 0;
    if (!w25q_read(s->active + W25Q_STORE_HDR, buf, len)) return 0;
    if (crc16((const uint8_t *)buf, len) != crc) return 0;   /* poskozeny payload */
    return len;
}

bool w25q_store_write(w25q_store_t *s, const void *buf, uint32_t len)
{
    if (!s->ready || len > W25Q_STORE_MAX_BLOB || (len && buf == NULL)) return false;

    /* Dalsi sektor kruhove; prvni zapis (seq==0) -> sektor 0. Sentinel je seq,
     * NE active — adresa 0x0 je platny sektor 0 CONFIG regionu (base 0x000000). */
    uint32_t next_i = 0;
    if (s->seq != 0) {
        uint32_t cur_i = (s->active - s->base) / W25Q_SECTOR_SIZE;
        next_i = (cur_i + 1u) % s->sectors;
    }
    uint32_t addr = s->base + next_i * W25Q_SECTOR_SIZE;
    uint32_t seq  = s->seq + 1u;

    if (!w25q_erase_sector(addr)) return false;
    /* Payload PRVNI (offset +16). */
    if (len && !w25q_write(addr + W25Q_STORE_HDR, buf, len)) return false;
    /* Hlavicka NAPOSLED (offset 0) -> pri vypadku pred timto krokem magic chybi
     * -> zaznam neplatny -> stary zustava. */
    uint16_t crc = crc16((const uint8_t *)buf, len);
    uint8_t h[W25Q_STORE_HDR] = {0};
    h[0]  = (uint8_t)STORE_MAGIC;       h[1]  = (uint8_t)(STORE_MAGIC >> 8);
    h[2]  = (uint8_t)(STORE_MAGIC >> 16); h[3] = (uint8_t)(STORE_MAGIC >> 24);
    h[4]  = STORE_VER;
    h[6]  = (uint8_t)len;               h[7]  = (uint8_t)(len >> 8);
    h[8]  = (uint8_t)seq;               h[9]  = (uint8_t)(seq >> 8);
    h[10] = (uint8_t)(seq >> 16);       h[11] = (uint8_t)(seq >> 24);
    h[12] = (uint8_t)crc;               h[13] = (uint8_t)(crc >> 8);
    if (!w25q_write(addr, h, sizeof h)) return false;

    s->active = addr; s->seq = seq;
    return true;
}
