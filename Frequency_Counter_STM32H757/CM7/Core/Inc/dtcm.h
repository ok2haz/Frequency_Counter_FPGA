#ifndef INC_DTCM_H_
#define INC_DTCM_H_

/**
 * @file    dtcm.h
 * @brief   Data v DTCM (hornich 64 kB, 0x20010000) -- STATUS #320, audit A2 V9.
 *
 * Do DTCM patri JEN data, na ktera sahne vyhradne CPU (CM7): Allan/trend pyramida a plochy ring statistiky.
 * Nulovou hodnotu NEZARUCUJE startup (nuluje jen .bss) -- `main()` ji nuluje pred prvnim pouzitim
 * (`_sdtcm_bss`..`_edtcm_bss`, linker). Spodnich 64 kB je testovaci oblast `membench` (zapisuje do ni vzory!).
 *
 * ⚠️ NIKDY sem nedavej buffer, ktery kdy pujde do DMA (DMA1/2, SDMMC IDMA, LTDC, DMA2D, ETH) ani data sdilena s CM4 --
 * ty DTCM nedosahnou (L-0001). TCM neni cachovana, takze tu nehrozi ani nekoherence cache.
 */
#define DTCM_BSS __attribute__((section(".dtcm_bss")))

#endif /* INC_DTCM_H_ */
