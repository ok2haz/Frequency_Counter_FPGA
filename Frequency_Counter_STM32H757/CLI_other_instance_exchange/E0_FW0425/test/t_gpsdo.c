#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#define GPSDO_HIST_PERIOD_S 10u
static double gp_clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
#define GPSDO_PM_NS_INIT    2.9       /* = MP_PPS_PM_NS (meas_present.h), 10h beh 2026-10-17/18 */
#define GPSDO_PM_NS_MIN     0.3       /* dolni mez (1PPS po IDES8 ~0,4 ns + GPS) */
#define GPSDO_PM_NS_MAX     30.0      /* horni mez (puvodni hypoteza) */
#define GPSDO_PM_NS_FLL     30.0
static double   s_pm_ns = GPSDO_PM_NS_INIT, s_pm_ewma = 0.0, s_pm_eprev = 0.0;
static uint8_t  s_pm_have = 0u;
static uint32_t s_pm_n = 0u;

static void gp_pm_update(double e)
{
    if (s_pm_have) {
        double d  = fabs(e - s_pm_eprev);
        double sd = s_pm_ewma * 1.2533141373155;
        if (s_pm_n > 16u && sd > 0.0 && d > 3.0 * sd) d = 3.0 * sd;
        s_pm_ewma = (s_pm_n == 0u) ? d : s_pm_ewma + (d - s_pm_ewma) * (1.0 / 256.0);
        if (s_pm_n < 0xFFFFFFFFu) s_pm_n++;
        if (s_pm_n >= 64u)
            s_pm_ns = gp_clamp(s_pm_ewma * 1.2533141373155 / 2.449489742783178, GPSDO_PM_NS_MIN, GPSDO_PM_NS_MAX);
    }
    s_pm_eprev = e; s_pm_have = 1u;
}
double gpsdo_pm_ns(void) { return s_pm_ns; }
static int ref_old(const int16_t *x_ns, const uint32_t *code_q16, int n, double kdac, double *u_ppb, double *drift_ppb_h)
{
    static const int MS[4] = { 100, 50, 20, 10 };
    *u_ppb = 0.0; *drift_ppb_h = 0.0;
    for (int k = 0; k < 4; k++) {
        int m = MS[k];
        if (n - 2 * m < 10) continue;
        double s = 0.0; int c = 0;
        for (int i = 0; i + 2 * m < n; i++) {
            double d = (double)x_ns[i + 2 * m] - 2.0 * (double)x_ns[i + m] + (double)x_ns[i];
            s += d * d; c++;
        }
        *u_ppb = sqrt(0.5 * s / (double)c) / ((double)m * (double)GPSDO_HIST_PERIOD_S);   /* ns/s = ppb */
        break;
    }
    if (n >= 180 && kdac > 0.0) {
        double cm = 0.0;
        for (int i = 0; i < n; i++) cm += (double)code_q16[i] / 65536.0;
        cm /= (double)n;
        const double tc = 0.5 * (double)(n - 1);
        double sxy = 0.0, sxx = 0.0;
        for (int i = 0; i < n; i++) {
            double t = (double)i - tc;
            sxy += t * ((double)code_q16[i] / 65536.0 - cm);
            sxx += t * t;
        }
        double slope = (sxx > 0.0) ? sxy / sxx / (double)GPSDO_HIST_PERIOD_S : 0.0;   /* LSB / s */
        *drift_ppb_h = fabs(slope) * kdac * 3600.0 * 1e9;
    }
    return *u_ppb > 0.0;
}

static int ref_new(const int32_t *x_ps, const uint32_t *code_q16, int n, double kdac, double *u_ppb, double *drift_ppb_h)
{
    static const int MS[4] = { 100, 50, 20, 10 };
    *u_ppb = 0.0; *drift_ppb_h = 0.0;
    for (int k = 0; k < 4; k++) {
        int m = MS[k];
        if (n - 2 * m < 10) continue;
        double s = 0.0; int c = 0;
        for (int i = 0; i + 2 * m < n; i++) {
            double d = ((double)x_ps[i + 2 * m] - 2.0 * (double)x_ps[i + m] + (double)x_ps[i]) * 1e-3;   /* [ns] */
            s += d * d; c++;
        }
        *u_ppb = sqrt(0.5 * s / (double)c) / ((double)m * (double)GPSDO_HIST_PERIOD_S);   /* ns/s = ppb */
        break;
    }
    if (n >= 180 && kdac > 0.0) {
        double cm = 0.0;
        for (int i = 0; i < n; i++) cm += (double)code_q16[i] / 65536.0;
        cm /= (double)n;
        const double tc = 0.5 * (double)(n - 1);
        double sxy = 0.0, sxx = 0.0;
        for (int i = 0; i < n; i++) {
            double t = (double)i - tc;
            sxy += t * ((double)code_q16[i] / 65536.0 - cm);
            sxx += t * t;
        }
        double slope = (sxx > 0.0) ? sxy / sxx / (double)GPSDO_HIST_PERIOD_S : 0.0;   /* LSB / s */
        *drift_ppb_h = fabs(slope) * kdac * 3600.0 * 1e9;
    }
    return *u_ppb > 0.0;
}

static double gauss(void){double a=(rand()+1.0)/(RAND_MAX+2.0),b=(rand()+1.0)/(RAND_MAX+2.0);return sqrt(-2*log(a))*cos(6.283185307*b);}
int main(void){ int fails=0; srand(3);
  /* T5: e_k = x_k - x_(k-1) + y*1e9 [ns], bily PM sigma_x; kroky DAC (FLL) a 1 % odlehlych hodnot */
  double sx[3]={0.4,2.0,5.0};
  for(int t=0;t<3;t++){ s_pm_ns=GPSDO_PM_NS_INIT; s_pm_ewma=0; s_pm_have=0; s_pm_n=0;
    double xp=sx[t]*gauss();
    for(int k=0;k<3000;k++){ double x=sx[t]*gauss(); double e=x-xp+0.05; xp=x;
      if(k%300==150) e+=40.0;            /* krok DAC */
      if(rand()%100==0) e+=30.0*sx[t];    /* odlehla hrana */
      gp_pm_update(e); }
    double r=s_pm_ns/sx[t]; printf("T5 sigma_x=%.1f ns: odhad %.3f ns (pomer %.3f)\n",sx[t],s_pm_ns,r);
    if(r<0.85||r>1.20) {printf("   CHYBA\n");fails++;} }
  /* T6: gp_ref_core int16 ns (stara) vs int32 ps (nova) na stejnych datech */
  static int16_t xo[360]; static int32_t xn[360]; static uint32_t tc[360]; uint32_t rnd=0x1234ABCDu;
  for(int i=0;i<360;i++){ rnd=rnd*1664525u+1013904223u; xo[i]=(int16_t)((int)((rnd>>16)%11u)-5); xn[i]=xo[i]*1000;
     tc[i]=(uint32_t)((24920.0+0.76/3600.0*10.0*(double)i)*65536.0+0.5); }
  double uo,dro,un,drn; int mo=ref_old(xo,tc,360,3.4e-11,&uo,&dro), mn=ref_new(xn,tc,360,3.4e-11,&un,&drn);
  printf("T6 gp_ref_core: stara u=%.6e drift=%.6e | nova u=%.6e drift=%.6e\n",uo,dro,un,drn);
  if(mo!=mn||fabs(uo-un)>1e-12*fabs(uo)||fabs(dro-drn)>1e-12*fabs(dro)+1e-30){printf("   CHYBA\n");fails++;}
  /* T6b: nova verze rozlisi pod 1 ns (stara by zaokrouhlila na 0) */
  for(int i=0;i<360;i++){ rnd=rnd*1664525u+1013904223u; xn[i]=(int32_t)((int)((rnd>>16)%601u)-300); xo[i]=(int16_t)lround(xn[i]/1000.0);}
  mo=ref_old(xo,tc,360,3.4e-11,&uo,&dro); mn=ref_new(xn,tc,360,3.4e-11,&un,&drn);
  printf("T6b faze +-0,3 ns: stara u=%.3e (zaokrouhleno na ns), nova u=%.3e\n",uo,un);
  if(!(un>0.0)){printf("   CHYBA\n");fails++;}
  printf("VYSLEDEK GPSDO: %s\n", fails?"SELHALO":"VSE PROSLO"); return fails; }
