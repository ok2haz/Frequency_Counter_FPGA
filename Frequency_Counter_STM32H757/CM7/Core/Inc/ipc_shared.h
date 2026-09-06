#pragma once
/**
 * @file ipc_shared.h
 * @brief Sdilena pamet + protokol CM7 <-> CM4 (SRAM4 / domena D3, 0x38000000).
 *
 * ZDROJ PRAVDY pro OBA jadra. Bez OpenAMP — jednoduchy auditovatelny protokol
 * (styl FPGA ramce). Ctyri kanaly:
 *   - ipc_snapshot_t  (CM7 -> CM4)  mereni+stav, SEQLOCK (lock-free cteni)
 *   - cmd ring        (CM4 -> CM7)  SCPI/web SETy (GATE, RUN/STOP, CHAN, log)
 *   - resp ring       (CM7 -> CM4)  odpovedi + async eventy
 *   - cm4 status      (CM4 -> CM7)  heartbeat (liveness) + vlastni CPU %
 *
 * ⚠️ UMISTENI: pevna adresa `IPC_BASE` (= zacatek SRAM4). Oba cory sahaji pres
 * makro `g_ipc` -> SHODNA adresa nezavisle na linkeru. Region je NON-CACHEABLE
 * (MPU region 2, `main.c` `MPU_Config`) + vyhrazeny linkerem (sekce `.ipc_shared`
 * @RAM_D3). Bez non-cacheable by cteni videlo stara data z D-cache a IPC by
 * "skoro fungovalo" a padalo nahodne.
 *
 * ⚠️ LAYOUT struktury MUSI byt identicky na obou jadrech — stejny header + ARM
 * EABI zarovnani to zaruci. Pri ZMENE layoutu zvednout `IPC_VERSION` (CM4 overi
 * `magic`+`version`+`size` po bootu; nesouhlas -> IPC vypnout, jet degradovane).
 *
 * ✅ STAV (2026-08-14): OBE strany bezi na HW — CM7 publikuje, CM4 cte a posila
 * heartbeat zpet. Header sdili CM4 pres RELATIVNI include
 * (`../../../CM7/Core/Inc/ipc_shared.h`) — regen-safe, bez zasahu do include path.
 *
 * Migrace: krok M4 zebriku (STATUS.md) je timto splneny.
 */
#include <stdint.h>

#define IPC_BASE     0x38000000u   /* SRAM4 / D3 — viz linker sekce .ipc_shared + MPU region 2 */
#define IPC_MAGIC    0x31435049u   /* "IPC1" (LE) */
#define IPC_VERSION  13u            /* v2: plna sada senzoru+kalibrace; v3 (2026-08-09): Math/limit
                                       cfg mirror ve snapshotu + IPC_CMD_CFG_SET (config sync CM4<->CM7);
                                       v4 (2026-08-13): sens_valid (maska platnosti) + t_fpga_c100;
                                       v5 (2026-08-22, F1): stav ETH linky/IP v ipc_cm4_status_t;
                                       v6 (2026-08-22, F3): eth_phy_id + eth_init_ok (CM4 cte PHY pres HAL);
                                       v7 (2026-08-23, W2): scpi_selftest_ok — dukaz, ze scpi.c+ipc_scpi.c
                                       skutecne BEZI na CM4 (ne jen se prelozi), viz WEB_UI_PLAN.md;
                                       v8 (2026-08-23, W3): web_ctrl_en (byval `_pad_cfg` — velikost
                                       struktury beze zmeny, ale bumpnuto stejne kvuli detekci nesouladu
                                       bank, viz CLAUDE.md „Nesoulad bank byl do v6 prakticky neviditelny");
                                       v9 (2026-08-23, W4): httpd_selftest_ok (byval posledni `_eth_rsvd`
                                       bajt — opet velikost beze zmeny);
                                       v10 (2026-08-23, W5): web_user/web_pass (HTTP Basic Auth) — SKUTECNY
                                       rust snapshotu o 36 B, ne recyklovany padding;
                                       v11 (2026-08-23): ui_cfg (byval `_pad_s` -> velikost beze zmeny).
                                       ⚠️ OPRAVA CHYBY, ne nova funkce: snapshot nenesl NASTAVENI mereni
                                       (brana, kanal), takze `SENS:FREQ:GATE?`/`CHAN?` pres TCP/HTTP
                                       vracely porad 0,1 s / kanal 0 bez ohledu na skutecnost — SET se
                                       PROVEDL, jen nebyl videt. USB cesta byla spravne (dekoduje tytez
                                       bity z `g_ui_cfg`, scpi_src_load_cm7) => dve ruzne pravdy o tomtez
                                       pristroji, presne to, cemu ma `sens_valid`/`_Static_assert` branit;
                                       v12 (2026-08-24): rozsireni webu — #4 alarmy/prahy/selftest ve
                                       snapshotu, #5 GPS druzice (sky plot), #6 datalog transfer kanal
                                       (dlouha historie 24h/7d/30d + CSV export z W25Q).
                                       ⚠️⚠️ v12 MENI LAYOUT PRED `cm4` blokem (snapshot roste o alarmy+
                                       druzice) -> gracefull detekce nesouladu bank (cm4_ipc_version na
                                       znamem offsetu) TADY NEFUNGUJE. MUSI se preflashnout OBE banky;
                                       jinak si jadra prestanou rozumet uz v adrese cm4 bloku.
                                       v13 (2026-08-26): `IPC_F_SIM` (jen bit ve `flags` — snapshot NEroste,
                                       emulovana data uz nejdou zamenit za mereni na webu/SCPI) + min/max
                                       OBALKA kmitoctu v `ipc_log_rec_t` a davkovane cteni datalogu
                                       (`req_env`/`resp_scanned`/`resp_full_env`).
                                       ⚠️ Roste jen `log`, ktery je AZ ZA `cm4` blokem -> detekce nesouladu
                                       bank u v13 FUNGUJE (na rozdil od v12). Flashnout stejne obe banky. */

/* ── Maska platnosti hodnot ve snapshotu (`sens_valid`) ──────────────────────
 * ⚠️ Bitove pozice jsou ZAMERNE SHODNE s `SCPI_V_*` (scpi.h), aby CM4 SCPI
 * backend mohl udelat proste `src->valid = snap.sens_valid;` a choval se
 * BIT ZA BIT stejne jako CM7 na USB. Shodu hlida `_Static_assert` v ipc.c —
 * ty dva hlavickove soubory se jinak nepotkaji v jedne translation unit.
 *
 * Duvod existence: do v3 se neplatna napeti publikovala jako 0 a neplatne
 * teploty dokonce jako POSLEDNI DOBRA hodnota (bez priznaku, ze je stara).
 * CM7 pritom na USB vraci `9.91E37`. `MEAS:VOLT?` by tedy pres USB rekl NaN
 * a pres TCP „0.00 V" — dve ruzne pravdy o tomtez pristroji. */
#define IPC_V_FREQ    (1u << 0)   /* platne mereni /4 (CRC+VALID+FRESH+nova SEQ) */
#define IPC_V_DIV16   (1u << 1)   /* platna /16 vetev */
#define IPC_V_FRAME   (1u << 2)   /* existuje posledni DATA ramec (gate/kanal) */
#define IPC_V_T_OCXO  (1u << 3)
#define IPC_V_T_BOARD (1u << 4)
#define IPC_V_T_MCU   (1u << 5)
#define IPC_V_T_FPGA  (1u << 6)
#define IPC_V_VC      (1u << 7)
#define IPC_V_RF      (1u << 8)
#define IPC_V_V12     (1u << 9)
#define IPC_V_V5      (1u << 10)
#define IPC_V_VREF    (1u << 11)
#define IPC_V_VBAT    (1u << 12)
#define IPC_V_GPS     (1u << 13)  /* GPS fix (cas/poloha platne) */

#define IPC_ADEV_PTS 12            /* ADEV bodu ve snapshotu (tau pyramida) */
#define IPC_RING_N   16            /* slotu v cmd/resp ringu — MUSI byt mocnina 2 */
#define IPC_GPS_MAX_SATS 24        /* v12: druzice ve snapshotu (sky plot); == GPS_MAX_SATS (hlida _Static_assert v ipc.c) */
#define IPC_LOG_CHUNK    96        /* v12: datalog zaznamu na jeden transfer round-trip (dlouha historie webu) */

/* ── v12: kompaktni kopie gps_sat_t pro sky plot na webu. ipc_shared.h nesmi
 * tahnout gps.h (sdili se s CM4), takze layout se DRZI SHODNY s gps_sat_t rucne
 * a kontroluje se `_Static_assert`em v ipc.c (offsetof kazdeho pole). 6 B. */
typedef struct {
    uint8_t  prn;      /* cislo druzice */
    uint8_t  elev;     /* elevace [°] 0..90 */
    uint8_t  snr;      /* C/N0 [dB-Hz], 0 = netrackovana */
    uint8_t  constel;  /* gps_constel_t: 0=GPS 1=GLONASS 2=Galileo 3=BeiDou */
    uint16_t azim;     /* azimut [°] 0..359 */
} ipc_sat_t;

/* ── Snapshot mereni + stavu: CM7 -> CM4 (~0,3 kB). SEQLOCK: `seq` licha = zapis. */
typedef struct {
    uint32_t magic;                 /* IPC_MAGIC — CM4 overi po bootu */
    uint16_t version;               /* IPC_VERSION */
    uint16_t size;                  /* sizeof(ipc_snapshot_t) — sanity check */
    volatile uint32_t seq;          /* seqlock (lichy = probiha zapis) */

    /* Mereni (freq × 1e5, delicka /4 i /16 uz zahrnuta — jako FPGA protokol). */
    uint64_t freq_x100000;          /* zvoleny zdroj (/4 nebo /16) */
    uint64_t freq4_x100000;         /* pin28 /4 */
    uint64_t freq16_x100000;        /* pin27 /16 */
    uint32_t gate_ns;               /* ≈250e6, kolisa */
    uint32_t seq_meas;              /* SEQUENCE posledniho platneho DATA ramce */

    /* Statistika (float — CM4 jen zobrazuje/serviruje, POCITA CM7 (double FPU)). */
    float    sigma_tau[IPC_ADEV_PTS]; /* ADEV σy(τ) body */
    float    tau_s[IPC_ADEV_PTS];     /* odpovidajici τ [s] */
    float    offset;                  /* frakcni offset (f-f0)/f0 */
    float    drift;                   /* drift / den */

    /* GPS. */
    int32_t  gps_lat_e7;            /* stupne × 1e7 */
    int32_t  gps_lon_e7;
    int32_t  gps_alt_cm;
    uint32_t rtc_unix;             /* UTC cas (unix) */
    float    gps_hdop;
    uint8_t  gps_valid;
    uint8_t  gps_fix_mode;         /* 0 / 2 / 3 */
    uint8_t  gps_num_sat;
    uint8_t  _pad_gps;

    /* Senzory — PLNA sada (v2), aby CM4 (SCPI/web) obslouzil dotazy BEZ pristupu ke
     * g_sensors/g_calib (na CM4 nejsou). Teploty × 100 °C, napeti mV, kalibrace RF. */
    float    ad8307_slope_mv_db;   /* AD8307 kalibrace (aby CM4 spocital MEAS:POWer? dBm) */
    float    ad8307_intercept_dbm;
    int16_t  t_ocxo_c100;          /* OCXO (TMP117 0x49) × 100 [°C] */
    int16_t  t_board_c100;         /* STM deska (TMP117 0x48) */
    int16_t  t_mcu_c100;           /* MCU jadro (ADC3) */
    int16_t  t_fpga_c100;          /* FPGA deska (TMP117 0x4A — dnes NEOSAZEN -> bit v sens_valid = 0).
                                    * Do v3 pole chybelo uplne, takze `SYST:TEMP? FPGA` na CM4
                                    * nesla vubec zodpovedet — dalsi rozdil proti USB. */
    uint16_t ocxo_vc_mv;           /* EFC ladici napeti (AIN0) */
    uint16_t rf_mv;                /* RF level SYROVE mV (AD8307, AIN1) */
    uint16_t v_12v_mv;             /* 12V vetev (AIN2, uz po gain) */
    uint16_t v_5v_mv;              /* 5V vetev (AIN3) */
    uint16_t vref_mv;              /* VREF+ ~2,5 V (ADC3) */
    uint16_t vbat_mv;              /* VBAT (ADC3) */
    uint8_t  channel_id;           /* aktivni kanal FPGA */
    uint8_t  si5356_status;        /* Si5356 reg 218 (reference lock: LOS_CLKIN/PLL_LOL) */
    uint8_t  si5356_ok;            /* 1 = status precten */
    /* v11: NASTAVENI mereni (`g_ui_cfg`) — bit0 mode, bit1 kanal, bity3:2 index brany,
     * bit4 RUN. ⚠️ Je to NASTAVENI, ne mereni: `channel_id`/`gate_ns` vyse hlasi, co
     * skutecne rekl FPGA ramec (a pri mrtvem linku jsou nulove), kdezto tohle je to,
     * co je na pristroji navolene. Bez tohohle bajtu nemel CM4 z ceho odpovedet na
     * `SENS:FREQ:GATE?`/`CHAN?` a vracel vychozi 0,1 s / 0. Byval to `_pad_s`. */
    uint8_t  ui_cfg;
    uint32_t sens_valid;           /* IPC_V_* — KTERE hodnoty vyse jsou platne (v4).
                                    * Hodnota bez nastaveneho bitu je NEPLATNA a nesmi se
                                    * servirovat jako mereni (SCPI -> 9.91E37). */

    /* Zdravi / stav. */
    uint32_t flags;                /* IPC_F_* */
    uint8_t  sys_level;            /* 0=OK 1=warn 2=err (agregace do SYS pilulky) */
    uint8_t  alarm_active;
    uint16_t _pad_h;
    uint32_t uptime_s;
    uint32_t cm7_cpu_pct;
    uint32_t reset_cause;          /* RCC->RSR (raw) */

    /* Math/limit konfigurace (g_meas_cfg mirror — CM4 pro CALC: readback + CALC:DATA?/LIM?).
     * ⚠️ Zapis z CM4 jde OPACNE pres cmd ring (IPC_CMD_CFG_SET -> CM7 aplikuje na g_meas_cfg),
     * projevi se pak tady. Snapshot je z pohledu CM4 READ-ONLY. Odpovida meas_cfg_t (meas_math.h). */
    double   math_m, math_b, null_ref, lim_lo, lim_hi;
    uint8_t  math_en, null_en, limit_en;
    /* v8 (2026-08-23, W3): hlavni vypinac vzdaleneho OVLADANI (okno PRISTUP na CM7).
     * ⚠️ Byval to `_pad_cfg` (nevyuzity padding) -> rozsireni je bezplatne, velikost
     * struktury se nemeni. Cteni je VZDY povolene (nezavisi na tomhle bitu); TCP SCPI
     * server na CM4 ho kontroluje PRED tim, nez prirad `src->set_cfg`, takze zakazane
     * ovladani znovu pouzije uz existujici NULL-callback ochranu parseru (zadna nova
     * chybova cesta). Viz WEB_UI_PLAN.md W3. */
    uint8_t  web_ctrl_en;
    /* v10 (2026-08-23, W5): prihlasovaci udaje pro HTTP Basic Auth (okno PRISTUP).
     * ⚠️ SKUTECNY rust struktury (ne recyklovany padding jako v8/v9) — porad se
     * vejde daleko pod limit SRAM4 (64 KB), viz _Static_assert na konci souboru.
     * Duvod, proc je to tady a ne jen `web_ctrl_en`: TCP 5025 (VISA raw socket)
     * nema koncept HTTP hlavicek, takze tam autentizace nedava smysl a spoleha
     * jen na `web_ctrl_en` (viz W3). Web je ale prohlizec — vic exponovany, jmeno
     * heslo si zada navic. `web_ctrl_en` zustava HLAVNI vypinac pro OBA transporty;
     * spravne jmeno+heslo je DALSI podminka navic, kterou HTTP transport pridava. */
    char     web_user[16];
    char     web_pass[20];

    /* ── v12 (2026-08-24): rozsireni webu. ────────────────────────────────────
     * #4 alarmy/prahy/selftest — dashboard karta STAV. Pocitadla nasycena na
     * 0xFFFF (pro zobrazeni bohate). `mon_*_bad` = aktualni stav prahoveho
     * monitoru (1 = mimo mez), `selftest_res` = g_selftest_res (0/1/2). */
    uint16_t alarm_fpga_lost, alarm_gps_lost, alarm_limit_fail;
    uint16_t alarm_vbat, alarm_ocxo, alarm_adev;
    uint8_t  mon_vbat_bad, mon_ocxo_bad, mon_adev_bad;
    uint8_t  selftest_res;

    /* #5 GPS druzice (sky plot na webu). `gps_sat_count` = pocet platnych. */
    uint8_t  gps_sat_count;
    uint8_t  _pad_sk[3];
    ipc_sat_t gps_sats[IPC_GPS_MAX_SATS];
} ipc_snapshot_t;

/* ── v12 #6: datalog transfer kanal (CM7 W25Q -> web) ───────────────────────
 * Zvlast od snapshotu (seqlock), protoze prenos je NA VYZADANI a bulk.
 * Handshake generaci: CM4 zapise pozadavek + zvedne `req_gen`; CM7 (defaultTask,
 * `ipc_datalog_service`) precte az IPC_LOG_CHUNK zaznamu z datalogu (s decimaci),
 * naplni `rec[]` a nastavi `resp_gen = req_gen`. CM4 pak sestavi JSON/CSV.
 * ⚠️ Datalog cte JEN CM7 (W25Q je na CM7); CM4 na nej nema pristup -> tudy. */
typedef struct {
    uint32_t t_unix;               /* UTC [s]; 0 = RTC nesynchronizovano */
    uint64_t freq_x100000;         /* kmitocet × 1e5 (zvoleny zdroj) — reprezentant bucketu */
    /* v13: MIN/MAX kmitoctu v ramci bucketu (obalka). Prosta decimace („ber kazdy
     * N-ty zaznam") vykyv MEZI vzorky NEUKAZE — u okna 24 h pripada na jeden bod
     * ~30 min, takze by se ztratilo skoro vse. 0 = nedostupne (obalka nevyzadana). */
    uint64_t freq_min_x100000, freq_max_x100000;
    int16_t  t_ocxo_c100;          /* OCXO [0,01 °C]; DATALOG_INVALID16 = neplatne */
    int16_t  t_board_c100;         /* STM deska [0,01 °C] */
    uint16_t ocxo_vc_mv;           /* ladici napeti [mV] */
    uint16_t rf_mv;                /* RF SYROVE mV (dBm dopocita CM4 pres kalibraci) */
    uint16_t vbat_mv;              /* VBAT [mV]; 0 = nezaznamenano */
    uint8_t  flags;                /* DATALOG_F_* */
    uint8_t  sats;                 /* pocet druzic */
    uint8_t  hdop10;               /* HDOP × 10; 255 = n/a */
    uint8_t  _pad;
} ipc_log_rec_t;

/* ⚠️ Strop CTENI na JEDEN pozadavek (v13). Poctiva obalka by musela precist VSECHNY
 * zaznamy v okne (24 h = 8640, 7 dni = 60 480, 30 dni = 259 200); pri ~128 ctenich
 * na tik defaultTasku (100 Hz) by 30 dni trvalo pres 20 s. Nad timto stropem se
 * proto bucket VZORKUJE (min/max z casti zaznamu) a odpoved to PRIZNA pres
 * `resp_full_env` — obalka z podvzorku se nesmi vydavat za uplnou. */
#define IPC_LOG_SCAN_MAX    20000u   /* max. prectenych zaznamu na pozadavek (~1,6 s) */
#define IPC_LOG_SCAN_BUDGET   128u   /* max. cteni na JEDNO volani service (~5 ms, viz "zadny spin >10 ms") */

typedef struct {
    volatile uint32_t req_gen;     /* CM4 zvedne pri NOVEM pozadavku (0 = zadny) */
    uint32_t req_from;             /* index nejnovejsiho zaznamu (0 = posledni zapsany) */
    uint16_t req_count;            /* kolik zaznamu (<= IPC_LOG_CHUNK) */
    uint16_t req_step;             /* decimace: ber kazdy `req_step`-ty (>=1) */
    uint8_t  req_env;              /* v13: 1 = spocitej min/max obalku kmitoctu v bucketu */
    uint8_t  _pad_rq[3];
    volatile uint32_t resp_gen;    /* CM7 nastavi = req_gen po naplneni `rec[]` */
    uint16_t resp_count;           /* kolik zaznamu SKUTECNE nacteno */
    uint16_t resp_total;           /* kolik zaznamu v logu vubec je (pro UI rozsah) */
    uint32_t resp_scanned;         /* v13: kolik zaznamu se pro obalku opravdu precetlo */
    uint8_t  resp_full_env;        /* v13: 1 = obalka z KAZDEHO zaznamu, 0 = z podvzorku */
    uint8_t  _pad_rs[3];
    ipc_log_rec_t rec[IPC_LOG_CHUNK];
} ipc_datalog_xfer_t;

/* ── Prikaz CM4 -> CM7 + odpoved CM7 -> CM4. */
typedef struct {
    uint8_t  type;                 /* IPC_CMD_* */
    uint8_t  key;                  /* pro IPC_CMD_CFG_SET: ktere pole (IPC_CFG_*) */
    uint16_t id;                   /* pro parovani s odpovedi */
    uint32_t arg;                  /* celociselny arg (GATE index / CHAN / bool 0|1) */
    double   argd;                 /* double arg (config: m/b/lo/hi) — cely rozsah kmitoctu */
} ipc_cmd_t;

typedef struct {
    uint16_t id;                   /* echo id prikazu */
    uint8_t  status;               /* 0=OK, jinak chybovy kod */
    uint8_t  _pad;
    uint32_t value;                /* navratova hodnota */
} ipc_resp_t;

/* SPSC ring (jeden producent, jeden konzument). head/tail = volne bezici citace. */
typedef struct { volatile uint32_t head, tail; ipc_cmd_t  slot[IPC_RING_N]; } ipc_cmd_ring_t;
typedef struct { volatile uint32_t head, tail; ipc_resp_t slot[IPC_RING_N]; } ipc_resp_ring_t;

/* ── CM4 -> CM7: heartbeat (liveness) + vlastni zatez (pro "CM4:xx%" v headeru). */
typedef struct {
    uint32_t magic;                /* IPC_MAGIC — potvrdi, ze CM4 opravdu zapisuje */
    volatile uint32_t heartbeat;   /* CM4 inkrementuje ~1/s; CM7 hlida stari (liveness) */
    uint32_t cm4_cpu_pct;          /* CM4 idle-based zatez [%] */
    uint32_t cm4_uptime_s;
    /* v5 (F1 ETH, 2026-08-22): stav ETH linky (CM4 -> CM7). Dnes CM4 hlasi natvrdo
     * down/0 (lwIP prijde az F5); zobrazovaci retez (System Health) se ladi uz tady. */
    uint32_t net_ip;               /* IPv4 jako oktety: bajt0=a .. bajt3=d (a.b.c.d); 0 = zadna IP */
    uint8_t  net_link;             /* 0 = down, 1 = up */
    uint8_t  net_speed_mbps;       /* 10 / 100 / 0 (neznamo) */
    uint8_t  net_duplex;           /* 0 = half, 1 = full */
    uint8_t  net_rsvd;             /* zarovnani na 4 */
    /* v6 (2026-08-22, F3): dukaz, ze ETH obsluhuje CM4 (ne CM7 bit-bang). */
    uint32_t eth_phy_id;           /* PHYID1<<16|PHYID2; LAN8742A = 0x0007C131, 0 = neprecteno */
    uint8_t  eth_init_ok;          /* 1 = HAL_ETH_Init na CM4 proslo (bezi RMII REF_CLK) */
    /* ⚠️ IPC_VERSION, se kterou byl PRELOZEN OBRAZ CM4 (razitkuje se v kazdem heartbeatu).
     * Duvod: nesoulad verzi byl do ted PRAKTICKY NEVIDITELNY. CM4 pri nesouladu jen
     * prestane prijimat snapshot (`s_ready=0`), ale heartbeat publikuje DAL a bez podminky
     * -> `ipc_cm4_alive()` (magic + rust heartbeatu) vraci 1 a v headeru svití "4:xx%",
     * jako by bylo vse v poradku. Jediny priznak byla LED_2 nereagujici na GPS fix.
     * Tenhle bajt to zviditelni: CM7 porovna s vlastni IPC_VERSION.
     * ⚠️ Stara CM4 (bez tohoto pole) ho nikdy nezapise -> zustane 0 po memsetu v ipc_init,
     * coz je taky spravna odpoved ("obraz je starsi, nehlasi verzi").
     * ⚠️ Detekce funguje jen dokud se nemeni layout PRED `cm4` blokem (snap/cmd/resp) —
     * pak si obe strany prestanou rozumet uz v adrese. Pro v5->v6 to plati (snap beze zmeny). */
    uint8_t  cm4_ipc_version;      /* IPC_VERSION obrazu CM4; 0 = nehlasi (stary obraz) */
    /* v7 (W2, 2026-08-23): dukaz, ze `scpi.c`+`ipc_scpi.c` na CM4 SKUTECNE BEZI (ne jen
     * se preloz) — CM4 spusti `scpi_selftest()` jednou pri bootu a vysledek publikuje.
     * 0 = jeste nedobehl / neni CM4, 1 = PASS, 2 = FAIL (viz scpi_selftest_fail_line). */
    uint8_t  scpi_selftest_ok;
    /* v9 (W4, 2026-08-23): dukaz, ze parser HTTP pozadavku na CM4 SKUTECNE BEZI —
     * stejny idiom jako `scpi_selftest_ok` (byval posledni volny `_eth_rsvd` bajt).
     * 0 = jeste nedobehl, 1 = PASS, 2 = FAIL. */
    uint8_t  httpd_selftest_ok;
} ipc_cm4_status_t;

/* ── Cela sdilena struktura (musi se vejit do 64 KB SRAM4). */
typedef struct {
    ipc_snapshot_t   snap;         /* CM7 -> CM4 */
    ipc_cmd_ring_t   cmd;          /* CM4 -> CM7 */
    ipc_resp_ring_t  resp;         /* CM7 -> CM4 */
    ipc_cm4_status_t cm4;          /* CM4 -> CM7 */
    ipc_datalog_xfer_t log;        /* CM4 <-> CM7 (v12, bulk historie na vyzadani) */
} ipc_shared_t;

_Static_assert(sizeof(ipc_shared_t) <= 65536, "IPC struktura se nevejde do SRAM4 (64 KB)");

/* Pristup k pevne adrese (oba cory -> shodne). */
#define g_ipc (*(volatile ipc_shared_t *)IPC_BASE)

/* ── Bity `flags`. */
#define IPC_F_FPGA_LINK    (1u << 0)   /* SPI link ziva */
#define IPC_F_SIGNAL_LOST  (1u << 1)   /* FPGA SIGNAL_LOST */
#define IPC_F_DIV16_ACTIVE (1u << 2)   /* zobrazeny zdroj = /16 */
#define IPC_F_GPS_VALID    (1u << 3)
#define IPC_F_HOLDOVER     (1u << 4)
#define IPC_F_SI5356_LOS   (1u << 5)   /* ztrata 10 MHz reference (bit3 reg218) */
#define IPC_F_DATALOG_ON   (1u << 6)
#define IPC_F_RUNNING      (1u << 7)   /* mereni bezi (RUN) */
/* ⚠️ Kmitocet pochazi z EMULATORU ramcu (`fpgasim`), ne z FPGA. Bez tohoto bitu
 * servirovaly web i SCPI pres TCP/HTTP emulovana data jako mereni — displej,
 * UART `status` i datalog (`DATALOG_F_SIM`) je pritom oznacuji. Volny bit ve
 * `flags`, takze snapshot NEroste a `IPC_VERSION` se kvuli nemu nezvedá. */
#define IPC_F_SIM          (1u << 8)   /* data z emulatoru fpgasim (NE realne mereni) */

/* ── Typy prikazu (CM4 -> CM7). */
enum {
    IPC_CMD_NOP = 0,   /* zdravi ringu (test M5) — CM7 jen odpovi echo */
    IPC_CMD_GATE,      /* arg = index 0..3 (0,1 / 1 / 10 / 100 s) */
    IPC_CMD_RUNSTOP,   /* arg = 0 stop / 1 run */
    IPC_CMD_CHAN,      /* arg = kanal */
    IPC_CMD_LOG,       /* arg = 0 off / 1 on (datalog) */
    IPC_CMD_CFG_SET,   /* Math/limit config-set: key=IPC_CFG_*, hodnota v arg (bool) nebo argd (double) */
};

/* ── Klice pro IPC_CMD_CFG_SET (config sync Math/limity, CM4 SCPI/web -> g_meas_cfg na CM7).
 * CM7 aplikuje pres ipc_cfg_apply (mirror scpi.c CALC SET). Cteni zpet = snapshot cfg mirror. */
enum {
    IPC_CFG_MATH_EN = 0,  /* arg 0/1 */
    IPC_CFG_MATH_M,       /* argd */
    IPC_CFG_MATH_B,       /* argd */
    IPC_CFG_NULL_EN,      /* arg 0/1 */
    IPC_CFG_NULL_ACQ,     /* akce: zachyt aktualni kmitocet jako null_ref (potrebuje platne mereni) */
    IPC_CFG_LIM_EN,       /* arg 0/1 */
    IPC_CFG_LIM_LO,       /* argd */
    IPC_CFG_LIM_HI,       /* argd */
    /* Instrument SET (2026-08-15) — stav mereni, ne Math. ⚠️ Poradi 1:1 se `SCPI_CFG_*`.
     * Rozsireni VYCTU nemeni layout `ipc_cmd_t` (klic je uint8_t), takze `IPC_VERSION`
     * se NEZVYSUJE: stara CM4 nove klice neposila a nova CM7 jim rozumi. */
    IPC_CFG_GATE,         /* arg = index brany 0..3 */
    IPC_CFG_CHAN,         /* arg = kanal 0/1 */
    IPC_CFG_RUN,          /* arg = 0 STOP / 1 RUN */
};

/* ── Pametova bariera (core-agnostic; funguje na CM7 i CM4, bez CMSIS zavislosti). */
#ifndef IPC_DMB
#define IPC_DMB() __asm volatile ("dmb 0xF" ::: "memory")
#endif

/* ── SEQLOCK jadro — pracuje nad DANYM snapshotem (ne jen g_ipc) → testovatelne
 * na lokalni kopii bez sdileneho stavu. Publikace JEN CM7, cteni JEN CM4. */
static inline void ipc_snap_wr_begin(volatile ipc_snapshot_t *s) { s->seq++; IPC_DMB(); }
static inline void ipc_snap_wr_end  (volatile ipc_snapshot_t *s) { IPC_DMB(); s->seq++; }
static inline uint32_t ipc_snap_rd_begin(volatile ipc_snapshot_t *s)             { uint32_t v = s->seq; IPC_DMB(); return v; }
static inline int      ipc_snap_rd_retry(volatile ipc_snapshot_t *s, uint32_t v) { IPC_DMB(); return (v & 1u) || (v != s->seq); }

/* ── SPSC ring jadro (nad danym ringem). Vraci 1 = uspech. */
static inline int ipc_ring_cmd_push(volatile ipc_cmd_ring_t *rg, const ipc_cmd_t *c) {
    uint32_t h = rg->head;
    if ((h - rg->tail) >= IPC_RING_N) return 0;            /* plno */
    rg->slot[h & (IPC_RING_N - 1u)] = *c;
    IPC_DMB(); rg->head = h + 1u;                          /* zverejni az PO zapisu slotu */
    return 1;
}
static inline int ipc_ring_cmd_pop(volatile ipc_cmd_ring_t *rg, ipc_cmd_t *c) {
    uint32_t t = rg->tail;
    if (rg->head == t) return 0;                           /* prazdno */
    IPC_DMB(); *c = rg->slot[t & (IPC_RING_N - 1u)];
    IPC_DMB(); rg->tail = t + 1u;
    return 1;
}
static inline int ipc_ring_resp_push(volatile ipc_resp_ring_t *rg, const ipc_resp_t *r) {
    uint32_t h = rg->head;
    if ((h - rg->tail) >= IPC_RING_N) return 0;
    rg->slot[h & (IPC_RING_N - 1u)] = *r;
    IPC_DMB(); rg->head = h + 1u;
    return 1;
}
static inline int ipc_ring_resp_pop(volatile ipc_resp_ring_t *rg, ipc_resp_t *r) {
    uint32_t t = rg->tail;
    if (rg->head == t) return 0;
    IPC_DMB(); *r = rg->slot[t & (IPC_RING_N - 1u)];
    IPC_DMB(); rg->tail = t + 1u;
    return 1;
}

/* ── g_ipc-vazane zkratky pro PRODUKCNI kod (call-sites beze zmeny).
 *   SEQLOCK PUBLIKACE (JEN CM7): ipc_snap_publish_begin(); ...zapis poli...; ipc_snap_publish_end();
 *   SEQLOCK CTENI (JEN CM4): do { s = ipc_snap_read_begin(); local = g_ipc.snap; } while (ipc_snap_read_retry(s));
 *   cmd ring: CM4 push / CM7 pop.  resp ring: CM7 push / CM4 pop. */
static inline void ipc_snap_publish_begin(void) { ipc_snap_wr_begin(&g_ipc.snap); }
static inline void ipc_snap_publish_end(void)   { ipc_snap_wr_end(&g_ipc.snap); }
static inline uint32_t ipc_snap_read_begin(void)       { return ipc_snap_rd_begin(&g_ipc.snap); }
static inline int      ipc_snap_read_retry(uint32_t s) { return ipc_snap_rd_retry(&g_ipc.snap, s); }
static inline int ipc_cmd_push(const ipc_cmd_t *c)   { return ipc_ring_cmd_push(&g_ipc.cmd, c); }
static inline int ipc_cmd_pop(ipc_cmd_t *c)          { return ipc_ring_cmd_pop(&g_ipc.cmd, c); }
static inline int ipc_resp_push(const ipc_resp_t *r) { return ipc_ring_resp_push(&g_ipc.resp, r); }
static inline int ipc_resp_pop(ipc_resp_t *r)        { return ipc_ring_resp_pop(&g_ipc.resp, r); }

/* ── CM7-strana IPC (ipc.c). CM4 si implementuje vlastni konzumenta; tyto
 * funkce bezi na CM7. Viz ipc.c. */
#ifdef __cplusplus
extern "C" {
#endif
void ipc_init(void);        /* orazitkuj snapshot + vynuluj ringy (1x pri bootu, pred publikaci) */
void ipc_publish(void);     /* CM7 -> CM4 snapshot pres seqlock (throttle ~2 Hz uvnitr) */
int  ipc_service(void);     /* zpracuj cmd ring -> resp ring; @return pocet prikazu */
void ipc_datalog_service(void); /* v12: obsluz datalog transfer (req_gen != resp_gen) -> naplni log.rec[]. VOLA defaultTask (blokujici W25Q cteni) */
int  ipc_cm4_alive(void);   /* 1 = CM4 heartbeat ziva (< ~3 s); bez CM4 vraci 0 */
uint32_t ipc_cm4_cpu_pct(void); /* CM4 vlastni zatez [%] z heartbeatu (0..100); 0 bez CM4 */
int  ipc_cm4_net(uint8_t *speed_mbps, uint8_t *duplex, uint32_t *ip); /* 1=link UP, ETH stav z CM4 (v5,F1) */
/* ETH bring-up stav z CM4 (v6, F3). @return 1 = HAL_ETH_Init na CM4 proslo.
 * `phy_id` (nepovinne) = PHYID1<<16|PHYID2, 0 = neprecteno. Bez ziveho CM4 vraci 0. */
int  ipc_cm4_eth(uint32_t *phy_id);
/* IPC_VERSION, se kterou byl prelozen obraz CM4. 0 = CM4 nezapsala magic, nebo je to
 * starsi obraz, ktery verzi nehlasi. ⚠️ Kdyz != IPC_VERSION, CM4 IGNORUJE snapshot
 * (heartbeat ale bezi dal, takze "4:xx%" klame) -> je potreba preflashnout obe banky. */
uint8_t ipc_cm4_ipc_version(void);
/* Vysledek `scpi_selftest()` na CM4 (v7, W2). @return 0 = jeste nedobehl / neni CM4,
 * 1 = PASS, 2 = FAIL. Dukaz, ze scpi.c+ipc_scpi.c na CM4 skutecne BEZI, ne jen se prelozi. */
uint8_t ipc_cm4_scpi_selftest(void);
/* Vysledek `httpd_min_selftest()` na CM4 (v9, W4). Stejny vyznam navratove hodnoty
 * jako `ipc_cm4_scpi_selftest`. */
uint8_t ipc_cm4_httpd_selftest(void);
int  ipc_selftest(void);    /* pure-logic: seqlock parita + ring push/pop/wrap; 1 = PASS */

/* ── CM4 -> CM7: publikace stavu ETH linky (v5, F1). Vola CM4 (dnes natvrdo down,
 * po lwIP realne). speed_mbps=10/100/0, duplex 0=half/1=full, ip=oktety a.b.c.d. */
void ipc_cm4_set_net(uint8_t link_up, uint8_t speed_mbps, uint8_t duplex, uint32_t ip);

/* ── CM4 -> CM7: vysledek ETH bring-upu (v6, F3). Vola CM4 jednou po MX_ETH_Init. */
void ipc_cm4_set_eth(uint8_t init_ok, uint32_t phy_id);

/* ── CM4 -> CM7: vysledek `scpi_selftest()` na CM4 (v7, W2). ok: 1=PASS, 0=FAIL. */
void ipc_cm4_set_scpi_selftest(uint8_t ok);

/* ── CM4 -> CM7: vysledek `httpd_min_selftest()` na CM4 (v9, W4). ok: 1=PASS, 0=FAIL. */
void ipc_cm4_set_httpd_selftest(uint8_t ok);

/* ── SCPI nad IPC snapshotem (priprava CM4 backendu, #25) ────────────────────
 * Naplni `scpi_src_t` VYHRADNE z IPC snapshotu — presne to, co bude delat CM4,
 * az na nem SCPI pojede pres TCP. Deklarace je `void *`, aby `ipc_shared.h`
 * nemusel tahnout `scpi.h` (a naopak) — implementace v `ipc_scpi.c` (linkuje se do OBOU jader).
 *
 * ⚠️ SMYSL: nejvetsi riziko TCP poloviny #25 neni socket, ale otazka "nese
 * snapshot vsechno, co SCPI potrebuje, a sedi bity platnosti?". Tohle se da
 * overit UZ TED na CM7 — `scpi ipc <cmd>` proti `scpi <cmd>` musi dat SHODNOU
 * odpoved. Staticke asserty hlidaji, ze SCPI_V_* == IPC_V_*, ale runtime dukaz
 * do ted neexistoval.
 *
 * @param snap  ukazatel na `ipc_snapshot_t` (typicky prectena kopie).
 * @return 1 = snapshot vypada platne (magic/verze), 0 = nepouzitelny. */
int ipc_scpi_src_from_snap(void *src_out, const void *snap);
#ifdef __cplusplus
}
#endif
