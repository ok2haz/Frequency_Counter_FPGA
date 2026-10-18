/**
  ******************************************************************************
  * @file    scpi_tcp.h
  * @brief   SCPI server na TCP portu 5025 (F6 / W3) — raw lwIP API, NO_SYS=1.
  ******************************************************************************
  */
#ifndef __SCPI_TCP_H__
#define __SCPI_TCP_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Otevre naslouchajici socket na portu 5025. Vola se JEDNOU z `lwip_app_init()`
 *  — nezavisi na stavu linky/DHCP (tcp_bind na IP_ADDR_ANY funguje driv, nez
 *  prijde adresa; spojeni proste nikdo nenavaze, dokud IP neni). */
void scpi_tcp_init(void);

/** Vytizeni poolu spojeni (STATUS #307): `used` = ted obsazenych slotu, `max` = velikost poolu, `peak` = nejvic soucasne
 *  od startu, `rej` = kolikrat se novy klient nevesel (strop dosazen, spojeni zruseno; saturuje). NULL = nezajima. */
void scpi_tcp_stats(uint8_t *used, uint8_t *max, uint8_t *peak, uint32_t *rej);

#ifdef __cplusplus
}
#endif
#endif /* __SCPI_TCP_H__ */
