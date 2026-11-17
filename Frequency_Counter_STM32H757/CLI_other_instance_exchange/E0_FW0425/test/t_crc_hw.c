/* Host test fpga_crc.c s FPGA_CRC_USE_HW = 1: jednotka CRC nahrazena EMULATOREM podle RM0399 (POL/INIT/POLYSIZE 16 b,
 * vstup po bajtech MSB-first, bez reflexe). Overuje logiku ovladace (selftest, zamek, pojistka HW/SW), NE kremik. */
#include <stdio.h>
#include <stdint.h>
static uint16_t e_pol, e_init, e_dr; static int e_poly16, e_fault, e_lock_busy, e_running = 2;
static void e_setup(void) { e_pol = 0x1021; e_init = 0xFFFF; e_poly16 = 1; e_dr = e_init; }
static void e_wr8(uint8_t b) { uint16_t c = e_dr ^ (uint16_t)(b << 8); for (int i = 0; i < 8; i++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ e_pol) : (uint16_t)(c << 1); e_dr = c; }
static uint16_t e_rd(void) { return (uint16_t)(e_dr ^ (e_fault ? 0x0001 : 0)); }
#define FPGA_CRC_USE_HW 1
#define FPGA_CRC_HW_SETUP()  e_setup()
#define FPGA_CRC_HW_WR8(b)   e_wr8((uint8_t)(b))
#define FPGA_CRC_HW_RD()     e_rd()
#define FPGA_CRC_CLK_EN()    do{}while(0)
#define FPGA_CRC_IN_ISR()    (0)
#include "cmsis_os2.h"
osKernelState_t osKernelGetState(void){ return e_running; }
osMutexId_t osMutexNew(const void*a){(void)a;return (void*)1;}
osStatus_t osMutexAcquire(osMutexId_t m,uint32_t t){(void)m;(void)t;return e_lock_busy?-1:0;}
osStatus_t osMutexRelease(osMutexId_t m){(void)m;return 0;}
#include "../fpga_crc.c"
static int fails; static void ck(int c,const char*m){printf("  %s %s\n",c?"OK   ":"CHYBA",m); if(!c)fails++;}
int main(void){
  fpga_crc_init(); fpga_crc_stats_t s; fpga_crc_stats(&s);
  ck(s.selftest_ok && s.hw_on, "selftest HW = SW (emulator) -> HW zapnuto");
  uint8_t f[128]; for(int i=0;i<126;i++) f[i]=(uint8_t)(i*7+3); uint16_t c=fpga_crc16_sw(f,126);
  ck(fpga_crc16(f,126)==c, "HW vypocet = SW");
  ck(fpga_crc16_check(f,126,c), "kontrola platneho ramce");
  ck(!fpga_crc16_check(f,126,(uint16_t)(c^0x100)), "kontrola vadneho ramce -> false");
  e_lock_busy=1; uint32_t sw0=(fpga_crc_stats(&s),s.sw_calls); (void)fpga_crc16(f,126); fpga_crc_stats(&s);
  ck(s.sw_calls==sw0+1 && s.hw_busy>=1, "zamek obsazen -> SW cesta"); e_lock_busy=0;
  e_fault=1; ck(fpga_crc16_check(f,126,c), "HW vadna (emulace): platny ramec presto prijat (SW pojistka)");
  fpga_crc_stats(&s); ck(s.mismatch==1 && !s.hw_on, "nesoulad HW/SW -> HW trvale vypnuto, zapocitano");
  ck(fpga_crc16(f,126)==c, "po vypnuti vse SW");
  e_fault=1; fpga_crc_init(); fpga_crc_stats(&s); ck(!s.selftest_ok && !s.hw_on, "vadna HW pri startu -> selftest neprojde, HW vypnuto");
  e_fault=0; e_running=0; fpga_crc_init(); fpga_crc_stats(&s); ck(s.hw_on, "pred schedulerem (bez zamku) HW funguje");
  printf("VYSLEDEK CRC: %s\n", fails?"SELHALO":"VSE PROSLO"); return fails; }
