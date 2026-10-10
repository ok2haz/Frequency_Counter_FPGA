# HW test oprav modulu 24 — průběžný záznam

**Datum:** 2026-09-27
**Deska:** STM32H757 (rev V), **bez FPGA desky** (`FPGA: link NOLINK`), GPS fix, Ethernet 10.0.0.106
**Obraz:** CM7 `a83bf1d` (Release), CM4 `d448b57` (Release, SPA s F-0190), IPC v19
**Flash:** `STM32_Programmer_CLI -c port=SWD -w … -v -rst` (obě banky, verifikace OK),
pak znovu jen CM7 (přidal opravu regrese F-0189, `a83bf1d`). Po spontánním výpadku
(viz níže) **fyzický power-cyklus uživatelem** — od té chvíle konzole (COM8) běžná.

## Co je ověřené

| co | jak | výsledek |
|---|---|---|
| deska po flashi běží | `GET /api/state` (CM4) | `uptime_s` roste od resetu, žádná smyčka resetů |
| boot selftest po power-cyklu (16 testů vč. nových vektorů) | UART `selftest` | **`SELFTEST: 16/16 PASS`**, vč. `fpga: select hystereze + ticky okna + souvislost SEQ selftest OK` |
| CM4 nový obraz | `/api/state` → `cm4.ipc_version`; `GET /` | IPC **19**; SPA **158 626 B** a obsahuje `ingestM`/`mTau0` (F-0190) = přesně extrakce z `check.py` |
| SCPI přes HTTP | `*IDN?`, `SYST:ERR?` | `OK2HAZ,GPSDO-Counter,0,gpsdo-ui v0.9.0`, `0,"No error"` |
| **F-0189 POR skip** (krok 21, část) | `status` po `Reset: power-on` | `ADEV rekonstrukce: preskocena — start po zapnuti napajeni` (opraveno `a83bf1d`) |
| **F-0186** hi-res bez systematické chyby | `fpgasim on 10000000` a `on 33333333` (asynchronní k 4e8 tickům), 5× `scpi MEAS:FREQ?` | vždy **přesně** `10000000.0000000` / `33333333.0000000`, žádný posun i přes náhodnou fázi hrany v každém měření (dřív by floor hradla dal příležitostně `+6,7e-5` u 33 333 333 Hz) |
| F-0180/F-0185 (USB vs IPC shoda, mimo modul) | `scpi ipc MEAS:FREQ?` | `CM7 : 10000000.0000000` / `IPC : 10000000.0000000` → **SHODA** |
| **F-0171/#27** statistika ze skutečné kadence | `status full` po ~65 s `fpgasim on` | `STATISTIKA: tau0 = 1000 ms, kolisa 0 % (16 vzorku, ztraceno 0)` |
| **F-0188/F-0192** nulování při změně signálu | `fpgasim on 12000000` (z 33 333 333) → `status full` | `(1 vzorku, ztraceno 0)` — pyramida se vynulovala právě jednou, žádný starý/smíšený vzorek nenavázal |
| **F-0193** — čítač děr | `status` v klidu | `SEQ FPGA: diry 0 (zmeskano 0 mereni), resync 0` |
| **F-0193** — injektor díry | `fpgasim fault gap` → `status` | `SEQ FPGA: diry 1 (zmeskano 3 mereni), resync 1  <== FpgaTask nestihal` — `resync` správně z předchozí změny `fpgasim on` (skok SEQ o 1000), `diry`/`zmeskano` z injektoru |
| F-0187 fázová pyramida (jednotkové vektory na reálném křemíku) | `selftest` (16/16 PASS zahrnuje test přidaný pro F-0187) | PASS — poprvé běžel na skutečném HW, ne jen v JS simulaci |

⚠️ **Vizuální kroky (F-0177 podtržení, F-0191 pás v okně ALLAN, tabulka MDEV/TDEV) nejsou
ověřené.** Zkusil jsem stáhnout snímek displeje (`screenshot`, holý přenos přes USB CDC) —
kanál je podle vlastního komentáře v `screenshot.c` „best-effort" a při čtení hostem přes
tento nástroj se opakovaně zasekl na ~30 % přenosu (343–355 kB z 1 152 054 B). Karta SD je
sice vložená (`sd` → `namountovano`), ale `screenshot sd`/`screenshot all` ukládají soubor
na kartu **uvnitř přístroje** — bez jiné cesty ven z desky (souborový přenos po UART není)
se k datům nedostanu. Vizuální kroky proto zůstávají neověřené touto cestou; potřebují buď
fyzické vytažení SD karty, nebo pohled na displej.

## Co ověřit NEŠLO a proč

🔴 **USB CDC konzole (COM8) po SW resetu přes SWD mlčela** — port byl vyčíslený a stabilní,
otevřít šlo, ale na `ping` nepřišlo nic (ani s DTR/RTS). Firmware přitom běžel (viz výše).
Řešeno power-cyklem (viz níže) — po něm konzole i všechny kroky fungovaly normálně.

🔴 **Spontánní výpadek desky během čekání na krok (21)** — cca po 1 minutě běhu
`fpgasim on 12000000` přestala odpovídat **konzole (COM8 zmizel z výčtu portů), síť
(`ping` → `Destination host unreachable`) i SWD** (`STM32_Programmer_CLI -c port=SWD`
bez `-r32`, tedy bez haltu — `mode=HOTPLUG` i `mode=UR` → `No STM32 target found`).
ST-LINK sonda samotná zůstala funkční (`-l st-link` ji viděla). Vyřešeno **fyzickým
power-cyklem uživatelem**.
⚠️ **Chybná první hypotéza (opravena):** nejdřív jsem se domníval, že to způsobil právě
odeslaný `cm4 restart` (dělá `NVIC_SystemReset()` po 2 s). **Vyvráceno dvěma důkazy:**
(1) `errlog` po zotavení **neobsahuje** událost `NET reboot`, kterou `cm4 restart`
zapisuje těsně před resetem — příkaz se tedy k firmwaru vůbec nedostal; (2) samotné
odeslání selhalo na úrovni otevření portu (`"The port 'COM8' does not exist."`), tedy
port zmizel **ještě před** pokusem o odeslání. `cm4 restart` tento test **neprovedl**.
🔑 **Skutečná příčina zůstává neznámá** — `Reset: power-on` po zotavení (ne
`stall:` z crash black-boxu, jak by ukázal IWDG/watchdog reset) ukazuje spíš na
**ztrátu napájení cílové desky** (souběžný výpadek USB CDC + Ethernetu + SWD, zatímco
ST-LINK zůstal enumerovaný, sedí na to líp než na softwarové zaseknutí — SWD běžně
přežije zaseknutou aplikaci). HYPOTÉZA, jeden výskyt, nepotvrzeno opakovaně.
⚠️ **`cm4 restart` v této session zůstává NEOTESTOVANÝ** — vzhledem k nejistotě
kolem výpadku jsem ho záměrně znovu nezkoušel; test rekonstrukce po WARM resetu
(nikoli POR) proto v kroku (21) chybí.

Kroky TODO #255 (1)–(24), které stojí na konzoli, proběhly po power-cyklu (viz níže).

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
- **Vztah k lekcím:** L-0098 (loader neplní pole), L-0018 (dvojčata musí říct totéž),
  L-0106 (nový, třetí výskyt téže třídy u `scpi_src_t`).
- **Stav:** opraveno 2026-09-27 (`72bf839`), podle návrhu — jeden řádek
  (`s->selftest_pass = (sn->selftest_res == 1)`), bez bumpu `IPC_VERSION`, plus
  test v `ipc_selftest` (obě hodnoty PASS/FAIL). ⬜ neověřeno na HW.
