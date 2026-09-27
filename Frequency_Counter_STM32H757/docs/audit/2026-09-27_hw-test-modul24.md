# HW test oprav modulu 24 — průběžný záznam

**Datum:** 2026-09-27
**Deska:** STM32H757 (rev V), **bez FPGA desky** (`FPGA: link NOLINK`), GPS fix, Ethernet 10.0.0.106
**Obraz:** CM7 `a83bf1d` (Release), CM4 `d448b57` (Release, SPA s F-0190), IPC v19
**Flash:** `STM32_Programmer_CLI -c port=SWD -w … -v -rst` (obě banky, verifikace OK),
pak znovu jen CM7. ⚠️ **Power-cyklus zatím NEPROBĚHL** — výsledky níže jsou po SW resetu.

## Co je ověřené

| co | jak | výsledek |
|---|---|---|
| deska po flashi běží | `GET /api/state` (CM4) | `uptime_s` roste od resetu (29 → 45 s), žádná smyčka resetů |
| boot selftest (16 testů vč. nových vektorů F-0186 ticky, F-0187 fázová pyramida, F-0193 `fpga_seq_gap`) | `/api/state` → `selftest` = `g_selftest_res` (`freertos.c:527`) | **1 = PASS** |
| CM4 nový obraz | `/api/state` → `cm4.ipc_version`; `GET /` | IPC **19**; SPA **158 626 B** a obsahuje `ingestM`/`mTau0` (F-0190) = přesně extrakce z `check.py` |
| SCPI přes HTTP | `*IDN?`, `SYST:ERR?` | `OK2HAZ,GPSDO-Counter,0,gpsdo-ui v0.9.0`, `0,"No error"` |

## Co ověřit NEŠLO a proč

🔴 **USB CDC konzole (COM8) po SW resetu přes SWD mlčí** — port je vyčíslený a stabilní,
otevřít jde, ale na `ping` nepřijde nic (ani s DTR/RTS). Firmware přitom běží (viz výše).
Před flashem na téže desce konzole odpovídala (`uptime 3707s`, `Reset: power-on`).
Moje změny na boot časování nesahají (`main.c` beze změny), takže HYPOTÉZA je týž jev jako
L-0084 (USB CDC vůči okamžiku resetu), jen obráceně. **Ověřit:** power-cyklus (odpojit
napájení i USB) → `ping`. Čtení stavu sondou (`uwTick`, `g_uptime_s`) nebylo povoleno —
halt by shodil I2C4.
Kroky TODO #255 (11)–(24) stojí na `fpgasim` a `status`, tedy na konzoli → čekají.

## Nalezeno při testu

### Regrese mého F-0189 — `status` hlásil čekání rekonstrukce jako běh (OPRAVENO `a83bf1d`)

Po F-0189 stav 3 rekonstrukce ADEV **čeká** na první reálné měření — bez FPGA libovolně
dlouho. `app_gpsdo_stats_seed_progress` ho dál vracel jako „běží", takže `status` by
trvale tvrdil `ADEV rekonstrukce: BEZI … <== zive vzorkovani zatim stoji`, přestože živé
vzorkování (i SIM fallback) běží. Nalezeno úvahou při testu nad deskou bez FPGA (výpis
samotný kvůli mrtvé konzoli vidět nebyl). Lekce L-0105.

---

### F-0194 [S3] SCPI `*TST?` přes TCP/HTTP hlásí FAIL vždy — CM4 nenaplní `selftest_pass`

- **Místo:** `CM7/Core/Src/ipc_scpi.c:24-97` (`ipc_scpi_src_from_snap` — `selftest_pass`
  se nikde nenastaví, zůstane 0 z memsetu), `CM7/Core/Src/scpi.c:458`
  (`*TST?` → `selftest_pass ? 0 : 1`), zastaralý komentář `CM7/Core/Src/ipc.c:857-860`
  („`selftest_pass` snapshot dnes nenese" — nese: `ipc_shared.h:265` `selftest_res`,
  plněno `ipc.c:285`).
- **Popis:** Přes USB (CM7, `scpi.c:1098`) vrací `*TST?` správně 0 při PASS. Přes TCP 5025
  a `POST /api/scpi` (CM4) jde zdroj ze snapshotu a `selftest_pass` je vždy 0 → `*TST?`
  vždy **1 (FAIL podle IEEE 488.2)**. Dvě pravdy o jednom přístroji: web `/api/state`
  hlásí `"selftest":1` (PASS) a tentýž web přes SCPI selhání.
- **Důkaz (HW, 2026-09-27):** `GET /api/state` → `"selftest":1`; `POST /api/scpi *TST?` → `1`.
- **Dopad:** automatizovaný test přes VISA (`*TST?` je první dotaz běžných skriptů) označí
  zdravý přístroj za vadný; na USB se to neukáže. Stejný vzor jako slepý readback (IPC v11)
  a F-0185 (L-0098): pole existuje, loader ho neplní.
- **Návrh (A):** v `ipc_scpi_src_from_snap` doplnit `s->selftest_pass = (sn->selftest_res == 1);`
  (layout beze změny, bez bumpu `IPC_VERSION`) + opravit komentář v `ipc.c`; do selftestu
  `ipc_selftest`/`scpi` porovnání `*TST?` nad snapshotem s PASS.
  Ověření na HW: `scpi *TST?` proti `scpi ipc *TST?` → SHODA, web `*TST?` → `0`.
- **Vztah k lekcím:** L-0098 (loader neplní pole), L-0018 (dvojčata musí říct totéž).
- **Stav:** otevřeno (TODO #258 v `../STATUS.md`).
