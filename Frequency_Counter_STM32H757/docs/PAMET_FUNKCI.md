# Pamet podle funkci - kolik zabira a kde
> Generuje `python tools/mem_report.py --write` z **mapy linkeru** (`CM7/Release/H757_LED_CM7.map`, `CM4/Release/H757_LED_CM4.map`) = co se doopravdy slinkovalo. Funkcni celek se urcuje podle objektoveho souboru (pravidla v tools/mem_report.py); co zadne pravidlo nechyti, je v radku „Ostatni“. Staticke oblasti mimo mapu (framebuffery, IPC, ETH DMA) jsou v samostatne tabulce. FLASH u `.data` zahrnuje load kopii inicializovanych dat.

## CM7

| Funkcni celek | FLASH [kB] | RAM_D1 [kB] | DTCM_APP [kB] | RAM_D2 [kB] | SDRAM [kB] | SDRAM_LOG [kB] | RAM_D3 [kB] |
|---|---:|---:|---:|---:|---:|---:|---:|
| Citac FPGA + smycka GPSDO + metrologie | 35.2 | 119.3 | - | - | - | 8192.0 | - |
| Hlavni obrazovka + statistika (Allan, trend, histogram, spektrogram) | 33.5 | 35.5 | 51.1 | - | 750.0 | - | - |
| Graficky HAL (LTDC, DMA2D, framebuffery) | 2.4 | 2.0 | - | - | 256.0 | - | - |
| Fonty (libui) | 250.0 | - | - | - | - | - | - |
| UI okna (menu, nastaveni, GPS, ADRET, kalibrace, ...) | 97.9 | 13.3 | - | - | - | - | - |
| libprim - 2D grafika | 7.3 | 3.5 | - | - | 100.0 | - | - |
| Diagnostika (UART prikazy, benchmark, screenshot) | 55.4 | 41.9 | - | - | - | - | - |
| Senzory, I2C, alarmy, watchdog | 69.1 | 16.1 | - | - | - | - | - |
| Datalog + SD (FatFs) | 28.9 | 42.4 | - | - | - | - | - |
| FreeRTOS | 14.3 | 36.9 | - | - | - | - | - |
| SCPI + IPC | 29.5 | 21.4 | - | - | - | - | - |
| HAL ST | 37.9 | 0.1 | - | - | - | - | - |
| libc / libm / libgcc | 31.8 | 0.4 | - | - | - | - | - |
| USB CDC | 5.5 | 11.2 | - | - | - | - | - |
| Uloziste (W25Q, nastaveni, kalibrace, errlog) | 12.4 | 3.1 | - | - | 1.0 | - | - |
| GPS + RTC | 8.2 | 3.1 | - | - | - | - | - |
| Start, main, periferie (CubeMX) | 7.5 | 0.9 | - | - | - | - | - |
| libui - komponenty UI | 5.6 | 0.3 | - | - | - | - | - |
| UI vstup a panel (dotyk, encoder, bridge) | 4.6 | 0.6 | - | - | - | - | - |
| **CELKEM** | **737.1** | **351.9** | **51.1** | **-** | **1107.0** | **8192.0** | **-** |
| kapacita | 1024.0 | 512.0 | 64.0 | 128.0 | 4096.0 | 8192.0 | 64.0 |
| vyuziti | 72.0 % | 68.7 % | 79.8 % | 0.0 % | 27.0 % | 100.0 % | 0.0 % |

### CM7 - nejvetsi datove objekty v RAM

| Symbol | Velikost [kB] | Kde |
|---|---:|---|
| `s_buf` | 8192.0 | SDRAM_LOG |
| `bg_cache` | 750.0 | SDRAM |
| `s_glyph_atlas` | 256.0 | SDRAM |
| `g_mask_b` | 50.0 | SDRAM |
| `g_mask_a` | 50.0 | SDRAM |
| `s_inl` | 48.0 | RAM_D1 |
| `ucHeap` | 32.0 | RAM_D1 |
| `sbuf.2` | 32.0 | RAM_D1 |
| `s_axi_buf` | 32.0 | RAM_D1 |
| `s_alg_x` | 23.4 | RAM_D1 |
| `s_alg_p` | 23.4 | RAM_D1 |
| `t.0` | 15.6 | RAM_D1 |
| `s_hist` | 15.5 | RAM_D1 |
| `s_hy` | 14.1 | DTCM_APP |
| `s_hscr` | 14.1 | RAM_D1 |
| `s_adev_raw` | 12.0 | DTCM_APP |

### CM7 - pojmenovane statisticke struktury (pod Allanem, trendem, histogramem...)

| Struktura | Velikost [kB] | Kde | Co to je |
|---|---:|---|---|
| `s_adev` | 12.0 | DTCM_APP | Allan pyramida (filtrovana): akumulatory ADEV/MDEV/HDEV, 7 stage |
| `s_adev_raw` | 12.0 | DTCM_APP | Allan pyramida SUROVA (bez filtru odlehlych) |
| `s_tr` | 9.2 | DTCM_APP | Trend pyramida (az 60 dni, 9 stage) |
| `s_y` | 0.9 | DTCM_APP | Plochy ring statistik (120 s) - offset, drift, sigma@1s |
| `s_wf` | 2.8 | DTCM_APP | Ring spektrogramu (720 s) |
| `s_hy` | 14.1 | DTCM_APP | Ring HISTOGRAMU (3600 s) |
| `s_hscr` | 14.1 | RAM_D1 | Scratch histogramu (median/MAD kvickselektem) |
| `s_ad_x` | 1.3 | RAM_D1 | Body fazoveho grafu ADRET |
| `g_sensors` | 0.3 | RAM_D1 | Statistiky senzoru (10 senzoru) |
| `s_hist` | 15.5 | RAM_D1 | Historie senzoru (sensor_hist, ~3,4 h) |

## CM4

| Funkcni celek | FLASH [kB] | RAM [kB] | ETH_D2 [kB] |
|---|---:|---:|---:|
| Web (HTTP server + SPA) | 201.5 | 42.6 | - |
| lwIP | 40.3 | 41.9 | - |
| SCPI (TCP 5025, parser) | 38.4 | 1.4 | - |
| Ethernet (MAC, PHY) | 5.0 | 0.3 | 12.4 |
| libc / libm / libgcc | 9.5 | 0.4 | - |
| Start, main, periferie (CubeMX) | 2.9 | 0.1 | - |
| HAL ST | 2.5 | 0.0 | - |
| IPC | 0.6 | 0.0 | - |
| **CELKEM** | **300.8** | **86.8** | **12.4** |
| kapacita | 1024.0 | 128.0 | 32.0 |
| vyuziti | 29.4 % | 67.8 % | 38.9 % |

### CM4 - nejvetsi datove objekty v RAM

| Symbol | Velikost [kB] | Kde |
|---|---:|---|
| `s_hconn` | 41.0 | RAM |
| `memp_memory_PBUF_POOL_base` | 24.1 | RAM |
| `ram_heap` | 14.0 | RAM |
| `memp_memory_RX_POOL_base` | 12.3 | ETH_D2 |
| `memp_memory_TCP_PCB_base` | 1.5 | RAM |
| `st.0` | 1.5 | RAM |
| `s_conn` | 0.6 | RAM |
| `memp_memory_FRAG_PBUF_base` | 0.4 | RAM |
| `__sf` | 0.3 | RAM |
| `memp_memory_TCP_SEG_base` | 0.3 | RAM |
| `memp_memory_PBUF_base` | 0.3 | RAM |
| `arp_table` | 0.2 | RAM |
| `memp_memory_TCP_PCB_LISTEN_base` | 0.2 | RAM |
| `txbuf.0` | 0.2 | RAM |
| `sv.0` | 0.2 | RAM |
| `src.1` | 0.2 | RAM |

## Staticke oblasti mimo mapu (dane linkerem / MPU / nastavenim)

| Oblast | Adresa | Velikost | Co v ni je |
|---|---|---:|---|
| SDRAM FB0/FB1/FB2 (triple buffering) | `0xC0000000 / 0xC0100000 / 0xC0200000` | 3 x 750 kB (800x480 RGB565 = 768 000 B) | 3 framebuffery, MPU region 0 (4 MB WT) |
| SDRAM scratch | `0xC0400000` | 512 kB testovano | membench / bgcheck (MPU region 1) |
| SDRAM `.sdram` | `0xC0800000` | viz tabulka CM7 (SDRAM) | bg_cache pozadi (768 000 B), atlas glyfu (256 kB), masky; Device pamet |
| SDRAM `.measlog` | `0xC1000000` | 8 MB | datova cache mereni: 262 144 zaznamu po 32 B (~18 h pri 4 mereni/s), MPU region 3 |
| SRAM4 / D3 `.ipc_shared` | `0x38000000` | 64 kB | IPC CM7<->CM4 (snapshot, cmd/resp ring, log/errlog/raw kanaly), MPU non-cacheable |
| SRAM3 / ETH DMA | `0x30040000` | 32 kB (12,7 kB pouzito) | ETH deskriptory + RX pool (CM4) |
| DTCM spodni 64 kB | `0x20000000` | 64 kB | VYHRAZENO pro `membench` (nic tam nedavat) |
| Interni FLASH banky | `0x08000000 (CM7), bank 2 (CM4)` | 2 x 1 MB | CM7 + CM4 obraz; fonty ~256 kB jsou v CM7 .rodata |
| W25Q512 (QSPI) | `externi` | 64 MB | CONFIG / CALIB / SETUP / DATA (datalog ~22 MB, ~80 dni) - viz w25q_map.h |
