# ARCHITECTURE.md — skutečný stav projektu

> Vyplňuje se ve fázi F1 auditu. **Jen doložená fakta** s odkazem `soubor:řádek`.
> Nedoložené položky zůstávají `?` — to je samo o sobě nález (chybí dokumentace).

## 1. Rozdělení jader

| Jádro | Frekvence | Role | Vstupní bod | Boot |
|---|---|---|---|---|
| CM7 | ? MHz | ? | `?` | ? |
| CM4 | ? MHz | ? | `?` | ? (option byte BCM4 = ?) |

Synchronizace při náběhu: `?` (HSEM ID, kdo čeká na koho, timeout)

## 2. Mapa paměti (skutečná, z linker skriptů a `.map`)

| Oblast | Adresa | Velikost | Kdo používá | Cache | Dostupné pro |
|---|---|---|---|---|---|
| ITCM | 0x0000_0000 | 64 kB | ? | — | CPU, MDMA |
| DTCM | 0x2000_0000 | 128 kB | ? | — | CPU, MDMA (NE DMA1/2) |
| AXI SRAM (D1) | 0x2400_0000 | 512 kB | ? | ? | vše |
| SRAM1/2/3 (D2) | 0x3000_0000 | 288 kB | ? | ? | DMA1/2, periferie D2 |
| SRAM4 (D3) | 0x3804_0000 | 64 kB | ? | ? | BDMA, D3 periferie |
| Backup SRAM | 0x3880_0000 | 4 kB | ? | — | ? |
| Flash bank 1 | 0x0800_0000 | 1 MB | ? | — | — |
| Flash bank 2 | 0x0810_0000 | 1 MB | ? | — | — |
| Sdílená paměť CM7↔CM4 | ? | ? | oba | musí být NC | ? |
| Externí SDRAM / QSPI | ? | ? | ? | ? | ? |

## 3. Konfigurace MPU (obě jádra)

| # | Adresa | Velikost | Atributy | Účel | Zdroj |
|---|---|---|---|---|---|
| 0 | ? | ? | ? | ? | `?:?` |

Kontrola: MPU je nastavena **před** `SCB_EnableDCache()`? `?`

## 4. Hodinový strom

- Zdroj: `?` (HSE ? MHz / HSI)
- PLL1: `?` → SYSCLK `?` MHz, VOS `?`, overdrive `?`
- HCLK `?`, APB1 `?`, APB2 `?`, APB3 `?`, APB4 `?`, TIMPRE `?`
- Zdroj: `soubor:řádek`

## 5. Vlastnictví periferií

| Periferie | Domény | Jádro | DMA / stream | Buffer (sekce) | Zámek |
|---|---|---|---|---|---|
| USART3 | D2 | ? | ? | ? | ? |

Konflikty (dvě jádra / dva tasky na jedné periferii): `?`

## 6. Přerušení

| IRQ | Jádro | Priorita | Volá `...FromISR`? | Handler |
|---|---|---|---|---|
| ? | ? | ? | ? | `?:?` |

`NVIC_PRIORITYGROUP_?`, `configMAX_SYSCALL_INTERRUPT_PRIORITY = ?`

## 7. Tasky / hlavní smyčka

| Task | Jádro | Priorita | Stack | Perioda | Blokující volání |
|---|---|---|---|---|---|
| ? | ? | ? | ? | ? | ? |

## 8. Moduly a závislosti

```
<<stromeček nebo seznam: modul -> na čem závisí>>
```

## 9. Otevřené otázky pro zadavatele

1. `?`
