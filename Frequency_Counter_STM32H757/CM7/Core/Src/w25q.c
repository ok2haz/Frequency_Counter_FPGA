/**
 * @file    w25q.c
 * @brief   Winbond W25Q512JV (64 MB) QSPI NOR flash driver. Viz w25q.h.
 *
 * Pouziva hqspi (QUADSPI, generovano CubeMX v quadspi.c). SCK 60 MHz (init prepne
 * z CubeMX 10 MHz). READ = Quad Fast Read 0x6C (4 datove linky, 8 dummy); zapis/
 * erase/registry 1-line. >16 MB -> 4-byte adresovani (EN4B + nativni 4B prikazy).
 */
#include "w25q.h"
#include "stm32h7xx_hal.h"
#include "cmsis_os2.h"   /* osDelay/osKernelGetState — yield ve wait_ready */
#include <stdio.h>

extern QSPI_HandleTypeDef hqspi;

/* === W25Q prikazy === */
#define CMD_RDID     0x9F   /* Read JEDEC ID (3 B) */
#define CMD_WREN     0x06   /* Write Enable (WEL) */
#define CMD_RDSR1    0x05   /* Read Status Reg 1 (bit0 WIP) */
#define CMD_RDSR2    0x35   /* Read Status Reg 2 (bit1 QE) */
#define CMD_WRSR2    0x31   /* Write Status Reg 2 */
#define CMD_FASTRD4B 0x0C   /* Fast Read, 32-bit adresa, 8 dummy (1-line, do 133 MHz) */
#define CMD_QREAD4B  0x6C   /* Quad Output Fast Read, 32-bit adresa, 8 dummy (data 4-line) */
#define CMD_PP4B     0x12   /* Page Program, 32-bit adresa */
#define CMD_SE4B     0x21   /* Sector Erase 4 KB, 32-bit adresa */
#define CMD_EN4B     0xB7   /* Enter 4-byte address mode */
#define CMD_RSTEN    0x66   /* Enable Reset */
#define CMD_RST      0x99   /* Reset Device */

#define SR1_WIP      0x01u  /* Write In Progress */
#define SR2_QE       0x02u  /* Quad Enable */

#define QSPI_TMO     1000u  /* HAL timeout [ms] pro prikaz/prenos */
#define W25Q_SCK_PRESCALER 3u  /* SCK = 240 MHz/(3+1) = 60 MHz (CubeMX default byl 10 MHz) */

static bool s_ready = false;
static bool s_quad  = false;   /* 1 = quad read (0x6C, 4 datove linky) aktivni */

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

/* Ceka na WIP=0 (konec zapisu/erase) s timeoutem [ms].
 *
 * ⚠️ USTUPUJE SCHEDULERU (osDelay(1) mezi dotazy), pokud uz bezi. Sector erase
 * trva 50-400 ms a drive se cely tenhle cas SPINOVALO bez yieldu — volajici je
 * pritom bud defaultTask (syscfg auto-save, datalog) nebo UiTask (calib_save).
 * defaultTask ma prioritu Normal, UiTask BelowNormal, takze erase v defaultTasku
 * UiTask uplne vyhladovel a jeho watchdog heartbeat mezitim starnul — primy
 * ukrojek z 2,5s limitu watchdog_supervise (a defaultTask po tu dobu sam
 * neobnovoval IWDG, protoze se nedostal na dalsi iteraci smycky).
 * Dotazovani 1x/ms je pro operaci delky desitek az stovek ms bohate dost. */
static bool wait_ready(uint32_t tmo_ms)
{
    uint32_t t0 = HAL_GetTick();
    bool sched = (osKernelGetState() == osKernelRunning);
    for (;;) {
        uint8_t sr;
        if (!rd_reg(CMD_RDSR1, &sr, 1)) return false;
        if (!(sr & SR1_WIP)) return true;
        if (HAL_GetTick() - t0 > tmo_ms) return false;
        if (sched) osDelay(1);   /* pred schedulerem (w25q_init z main.c) se spinuje */
    }
}

/* Quad Enable (SR2 bit1) — povoli 4-line read (0x6C). Neuspech NENI fatalni:
 * w25q_read spadne na 1-line Fast Read (0x0C). Idempotentni (kdyz uz QE, jen vrati). */
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
    s_ready = false; s_quad = false;

    /* Zvednuti SCK z 10 MHz (CubeMX) na 60 MHz. ⚠️ read MUSI byt Fast/Quad (8 dummy)
     * — plain Read 0x13 (0 dummy) je u W25Q stropovan 50 MHz; ostatni prikazy (RDID/
     * RDSR/PP/quad) jedou do 133 MHz, takze 60 MHz je bezpecne s rezervou. */
    hqspi.Init.ClockPrescaler = W25Q_SCK_PRESCALER;
    if (HAL_QSPI_Init(&hqspi) != HAL_OK) return false;

    /* SW reset (66h+99h) -> zname vychozi (napr. po warm resetu STM zustal chip
     * v jinem modu). Po resetu tRST ~30 us -> 1 ms delay bohate staci. */
    cmd_only(CMD_RSTEN);
    cmd_only(CMD_RST);
    HAL_Delay(1);

    if (w25q_read_jedec() != W25Q_JEDEC_ID) return false;   /* overi i vyssi SCK */

    if (!cmd_only(CMD_EN4B)) return false;   /* 4-byte adresovani (64 MB > 16 MB) */
    s_quad = quad_enable();                  /* uspech -> read pojede 4-line (0x6C) */

    s_ready = true;
    return true;
}

bool w25q_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!s_ready || buf == NULL || len == 0) return false;
    QSPI_CommandTypeDef c = {0};
    c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    c.AddressMode     = QSPI_ADDRESS_1_LINE;
    c.AddressSize     = QSPI_ADDRESS_32_BITS;
    c.Address         = addr;
    c.DummyCycles     = 8;                    /* Fast/Quad read: 8 dummy (nutne >50 MHz) */
    c.NbData          = len;
    if (s_quad) { c.Instruction = CMD_QREAD4B;  c.DataMode = QSPI_DATA_4_LINES; }  /* 4-line */
    else        { c.Instruction = CMD_FASTRD4B; c.DataMode = QSPI_DATA_1_LINE;  }  /* fallback */
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
