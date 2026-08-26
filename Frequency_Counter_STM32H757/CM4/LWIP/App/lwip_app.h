/**
  ******************************************************************************
  * @file    lwip_app.h
  * @brief   lwIP (NO_SYS=1) na CM4: init + obsluha ze smycky + DHCP klient.
  *
  * ⚠️ NENI to CubeMX `LWIP/App/lwip.c` — lwIP je do projektu zaveden RUCNE
  * (viz ETH_BRINGUP_CHECKLIST F5), takze jmeno je zamerne jine, aby budouci
  * `Generate Code` s LWIP nezpusobil kolizi dvou souboru se stejnou roli.
  ******************************************************************************
  */
#ifndef __LWIP_APP_H__
#define __LWIP_APP_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Inicializuje lwIP a sitove rozhrani. Vola se JEDNOU po MX_ETH_Init().
 *  Bezpecne i kdyz ETH nenabehlo — rozhrani pak zustane "down" (degradovane). */
void lwip_app_init(void);

/** Obsluha ze hlavni smycky CM4. MUSI se volat casto (radove kazdych par ms):
 *  vybira prijate ramce, tika lwIP timery (DHCP/ARP) a hlida stav linky.
 *  Vysledek publikuje pres IPC do CM7 (`ipc_cm4_set_net`). */
void lwip_app_process(void);

/* ⚠️ `lwip_app_has_ip()`/`lwip_app_ip()` odstraneny (revize 2026-08-26) — nikdo
 * je nevolal. Stav linky a IP tecou ven pres `ipc_cm4_set_net()` (viz .c). */

#ifdef __cplusplus
}
#endif
#endif /* __LWIP_APP_H__ */
