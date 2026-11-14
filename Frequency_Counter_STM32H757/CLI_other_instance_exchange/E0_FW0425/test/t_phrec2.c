/* audit 4: Σ gate_cc_ps proti presnemu Σ (prenos zaokrouhleni) a chovani pri irregularnim znackovani (f > 16 MHz) */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "fpga_phrec.h"
static double run_xdiv(unsigned xdiv){
  fpga_phrec_reset(); const double u=1.0/(16384.0*100e6); double f=21.37e6; unsigned long long per=(unsigned long long)(f*0.25);
  unsigned long long dtu=(unsigned long long)(per/f/u+0.5); unsigned lcg=7;
  for(int k=0;k<1200;k++){ fpga_meas_t m; memset(&m,0,sizeof m); m.caps=FPGA_CAP_DT|FPGA_CAP_REGR; m.clk_mhz=100; m.regr_used=1; m.rg_ok=3;
    m.edge_count=per; m.dt_units=dtu; m.f_regr_hz=f; m.rg_xdiv=(uint16_t)xdiv; lcg=lcg*1664525u+1013904223u; double frac=(lcg>>8)/16777216.0;
    double xbar=per*0.125+frac; double ybar=xbar/f; m.rg_xm[0]=(unsigned long long)(xbar*xdiv+0.5); m.rg_ym[0]=(unsigned long long)(ybar/u*16.0+0.5);
    fpga_phrec_feed(&m,0,1); }
  uint32_t t; return fpga_phrec_adev(4,&t); }
int main(void){
  int fails=0; fpga_phrec_reset();
  const double u=1.0/(16384.0*100e6), G=0.25; double f=13.37e6;           /* nesoudelny kmitocet -> zlomky */
  unsigned long long per=(unsigned long long)(f*G); unsigned long long dtu=(unsigned long long)(per/f/u+0.5);
  double dsum=0; unsigned lcg=1;
  for(int k=0;k<20000;k++){ fpga_meas_t m; memset(&m,0,sizeof m);
    m.caps=FPGA_CAP_DT|FPGA_CAP_REGR; m.clk_mhz=100; m.regr_used=1; m.rg_ok=3; m.edge_count=per; m.dt_units=dtu; m.f_regr_hz=f;
    lcg=lcg*1664525u+1013904223u; double frac=(lcg>>8)/16777216.0;             /* nepravidelne znackovani: zlomek teziste */
    double xbar=per*0.125+frac; double ybar=xbar/f;
    m.rg_xm[0]=(unsigned long long)(xbar*256.0+0.5); m.rg_ym[0]=(unsigned long long)(ybar/u*16.0+0.5);
    fpga_phrec_feed(&m,0,1);
    if(m.cc_ok){ dsum += (double)m.gate_cc_ps - (double)m.edge_count/m.f_cc_hz*1e12; } }
  printf("T8 Σ(gate_cc - presne): %.3f ps po 20000 oknech (bez prenosu by byl ~%.0f ps)\n", dsum, 0.29*sqrt(20000.0));
  if(fabs(dsum)>1.0){printf("  CHYBA\n");fails++;}
  double a256=run_xdiv(256u), a4096=run_xdiv(4096u);
  printf("T9 21,37 MHz, nepravidelne znackovani, bez sumu: ADEV(1 s) xm 1/256 = %.2e, xm 1/4096 = %.2e (cil: pod sumem teziste ~6e-12)\n", a256, a4096);
  if(!(a4096 < 0.1*a256 && a4096 < 2e-11)){printf("  CHYBA\n");fails++;}
  printf("VYSLEDEK: %s\n", fails?"SELHALO":"VSE PROSLO"); return fails; }
