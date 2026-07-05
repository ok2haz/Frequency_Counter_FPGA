/**
 * @file    w25q.c
 * @brief   Winbond W25Q512JV (64 MB) QSPI NOR flash driver. Viz w25q.h.
 *
 * Pouziva hqspi (QUADSPI, generovano CubeMX v quadspi.c). Vsechny prenosy jsou
 * zatim 1-line (bezpecne pro bring-up); quad se zapina jen pro budouci rychle
 * cteni. >16 MB -> 4-byte adresovani (EN4B + nativni 4-byte prikazy 0x13/0x12/0x21).
 */
#include "w25q.h"
#include "stm32h7xx_hal.h"
#include <stdio.h>

extern QSPI_HandleTypeDef hqspi;

/* === W25Q prikazy === */
#define CMD_RDID     0x9F   /* Read JEDEC ID (3 B) */
#define CMD_WREN     0x06   /* Write Enable (WEL) */
#define CMD_RDSR1    0x05   /* Read Status Reg 1 (bit0 WIP) */
#define CMD_RDSR2    0x35   /* Read Status Reg 2 (bit1 QE) */
#define CMD_WRSR2    0x31   /* Write Status Reg 2 */
#define CMD_READ4B   0x13   /* Read Data, 32-bit adresa, 0 dummy */
#define CMD_PP4B     0x12   /* Page Program, 32-bit adresa */
#define CMD_SE4B     0x21   /* Sector Erase 4 KB, 32-bit adresa */
#define CMD_EN4B     0xB7   /* Enter 4-byte address mode */
#define CMD_RSTEN    0x66   /* Enable Reset */
#define CMD_RST      0x99   /* Reset Device */

#define SR1_WIP      0x01u  /* Write In Progress */
#define SR2_QE       0x02u  /* Quad Enable */

#define QSPI_TMO     1000u  /* HAL timeout [ms] pro prikaz/prenos */

static bool s_ready = false;

/* Posle prikaz bez adresy i dat (WREN, EN4B, reset, ...). */
static bool cmd_only(uint8_t instr)
{
    QSPI_CommandTypeDef c = {0};
    c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    c.Instruction     = instr;
    c.AddressMode     = QSPI_ADDRESS_NONE;
    c.DataMode        = QSPI_DATA_NONE;
    return HAL_QSPI_Command(&hqspi, &c, QSPI_TMO) == HAL_OK;
}

/* Precte N bajtu z registru (RDID/RDSR): instr, bez adresy, N dat na 1 lince. */
static bool rd_reg(uint8_t instr, uint8_t *buf, uint32_t n)
{
    QSPI_CommandTypeDef c = {0};
    c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    c.Instruction     = instr;
    c.AddressMode     = QSPI_ADDRESS_NONE;
    c.DataMode        = QSPI_DATA_1_LINE;
    c.NbData          = n;
    if (HAL_QSPI_Command(&hqspi, &c, QSPI_TMO) != HAL_OK) return false;
    return HAL_QSPI_Receive(&hqspi, buf, QSPI_TMO) == HAL_OK;
}

uint32_t w25q_read_jedec(void)
{
    uint8_t id[3] = {0};
    if (!rd_reg(CMD_RDID, id, 3)) return 0;
    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | (uint32_t)id[2];
}

/* Ceka na WIP=0 (konec zapisu/erase) s timeoutem [ms]. */
static bool wait_ready(uint32_t tmo_ms)
{
    uint32_t t0 = HAL_GetTick();
    for (;;) {
        uint8_t sr;
        if (!rd_reg(CMD_RDSR1, &sr, 1)) return false;
        if (!(sr & SR1_WIP)) return true;
        if (HAL_GetTick() - t0 > tmo_ms) return false;
    }
}

/* Quad Enable (SR2 bit1) — pro budouci 4-line cteni. Neni fatalni pro 1-line. */
static bool quad_enable(void)
{
    uint8_t sr2 = 0;
    if (!rd_reg(CMD_RDSR2, &sr2, 1)) return false;
    if (sr2 & SR2_QE) return true;                  /* uz zapnuto */
    if (!cmd_only(CMD_WREN)) return false;
    QSPI_CommandTypeDef c = {0};
    c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    c.Instruction     = CMD_WRSR2;
    c.DataMode        = QSPI_DATA_1_LINE;
    c.NbData          = 1;
    uint8_t v = (uint8_t)(sr2 | SR2_QE);
    if (HAL_QSPI_Command(&hqspi, &c, QSPI_TMO) != HAL_OK) return false;
    if (HAL_QSPI_Transmit(&hqspi, &v, QSPI_TMO) != HAL_OK) return false;
    return wait_ready(50);
}

bool w25q_init(void)
{
    s_ready = false;

    /* SW reset (66h+99h) -> zname vychozi (napr. po warm resetu STM zustal chip
     * v jinem modu). Po resetu tRST ~30 us -> 1 ms delay bohate staci. */
    cmd_only(CMD_RSTEN);
    cmd_only(CMD_RST);
    HAL_Delay(1);

    if (w25q_read_jedec() != W25Q_JEDEC_ID) return false;   /* chip nekomunikuje */

    if (!cmd_only(CMD_EN4B)) return false;   /* 4-byte adresovani (64 MB > 16 MB) */
    (void)quad_enable();                     /* volitelne; 1-line jede i bez nej */

    s_ready = true;
    return true;
}

bool w25q_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!s_ready || buf == NULL || len == 0) return false;
    QSPI_CommandTypeDef c = {0};
    c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    c.Instruction     = CMD_READ4B;
    c.AddressMode     = QSPI_ADDRESS_1_LINE;
    c.AddressSize     = QSPI_ADDRESS_32_BITS;
    c.Address         = addr;
    c.DataMode        = QSPI_DATA_1_LINE;
    c.NbData          = len;
    if (HAL_QSPI_Command(&hqspi, &c, QSPI_TMO) != HAL_OK) return false;
    return HAL_QSPI_Receive(&hqspi, buf, QSPI_TMO) == HAL_OK;
}

/* Zapis do JEDNE stranky (<=256 B, bez preteceni pres hranici stranky). */
static bool page_program(uint32_t addr, const uint8_t *buf, uint32_t n)
{
    if (!cmd_only(CMD_WREN)) return false;
    QSPI_CommandTypeDef c = {0};
    c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    c.Instruction     = CMD_PP4B;
    c.AddressMode     = QSPI_ADDRESS_1_LINE;
    c.AddressSize     = QSPI_ADDRESS_32_BITS;
    c.Address         = addr;
    c.DataMode        = QSPI_DATA_1_LINE;
    c.NbData          = n;
    if (HAL_QSPI_Command(&hqspi, &c, QSPI_TMO) != HAL_OK) return false;
    if (HAL_QSPI_Transmit(&hqspi, (uint8_t *)buf, QSPI_TMO) != HAL_OK) return false;
    return wait_ready(10);   /* page program ~0.4-3 ms */
}

bool w25q_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (!s_ready || buf == NULL) return false;
    while (len) {
        uint32_t off   = addr % W25Q_PAGE_SIZE;
        uint32_t chunk = W25Q_PAGE_SIZE - off;      /* do konce aktualni stranky */
        if (chunk > len) chunk = len;
        if (!page_program(addr, buf, chunk)) return false;
        addr += chunk; buf += chunk; len -= chunk;
    }
    return true;
}

bool w25q_erase_sector(uint32_t addr)
{
    if (!s_ready) return false;
    if (!cmd_only(CMD_WREN)) return false;
    QSPI_CommandTypeDef c = {0};
    c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    c.Instruction     = CMD_SE4B;
    c.AddressMode     = QSPI_ADDRESS_1_LINE;
    c.AddressSize     = QSPI_ADDRESS_32_BITS;
    c.Address         = addr & ~(W25Q_SECTOR_SIZE - 1u);   /* zarovnat na sektor */
    c.DataMode        = QSPI_DATA_NONE;
    if (HAL_QSPI_Command(&hqspi, &c, QSPI_TMO) != HAL_OK) return false;
    return wait_ready(1000);   /* sector erase ~50-400 ms */
}

void w25q_format_status(char *buf, int buflen)
{
    uint32_t id = w25q_read_jedec();
    if (id == W25Q_JEDEC_ID)
        snprintf(buf, buflen, "W25Q512 64MB ID:%06lX OK", (unsigned long)id);
    else
        snprintf(buf, buflen, "QSPI NOLINK ID:%06lX (cekam EF4020)", (unsigned long)id);
}
