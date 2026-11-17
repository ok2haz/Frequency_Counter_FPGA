/**
  ******************************************************************************
  * @file    LwIP/LwIP_HTTP_Server_Raw/Inc/ethernetif.h 
  * @author  MCD Application Team
  * @brief   Header for ethernetif.c module
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#ifndef __ETHERNETIF_H__
#define __ETHERNETIF_H__


#include "lwip/err.h"
#include "lwip/netif.h"

/* Exported types ------------------------------------------------------------*/
err_t ethernetif_init(struct netif *netif);
void ethernetif_input(struct netif *netif);
void ethernet_link_check_state(struct netif *netif);

/* Pocitadla vyslani (v18, audit F-0138). Volne bezici; do snapshotu je saturovane
 * uklada `ipc_cm4_set_eth_tx()`, volana ze smycky v `main.c`. Odlisi „nevysilame
 * vubec" od „vysilame, ale nic se nevraci" — bez toho to slo precist jen ladici
 * sondou, ktera za behu zabiji I2C4 do power-cyklu. */
extern uint32_t g_eth_tx_ok;
extern uint32_t g_eth_tx_err;
#endif
