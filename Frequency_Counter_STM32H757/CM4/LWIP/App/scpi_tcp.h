/**
  ******************************************************************************
  * @file    scpi_tcp.h
  * @brief   SCPI server na TCP portu 5025 (F6 / W3) — raw lwIP API, NO_SYS=1.
  ******************************************************************************
  */
#ifndef __SCPI_TCP_H__
#define __SCPI_TCP_H__

#ifdef __cplusplus
extern "C" {
#endif

/** Otevre naslouchajici socket na portu 5025. Vola se JEDNOU z `lwip_app_init()`
 *  — nezavisi na stavu linky/DHCP (tcp_bind na IP_ADDR_ANY funguje driv, nez
 *  prijde adresa; spojeni proste nikdo nenavaze, dokud IP neni). */
void scpi_tcp_init(void);

#ifdef __cplusplus
}
#endif
#endif /* __SCPI_TCP_H__ */
