# Výsledky simulací FW 0x0427 (zapsáno 2026-10-22, NEcommitnuto)

Běh `par_sim.ps1` (kopie `sim/run.ps1`, paralelně) nad RTL 0x0427, start 21:39 dne 2026-10-21. Logy: `Frequency_Counter_FPGA_Module/sim_par/*.log`.

## PASS
alias, coarse, coh, decequiv256/288/320, gatediv, link, lutequiv, n1, phase, pps, refguard, regracc, regrfull, txequiv, winregr; samostatně `stmcal` (512/512 zápisů, součet 3bd9).

## FAIL (timeout, `cal_valid` / `valid_a` se nenastaví)
tdc, tdcinl, tdcinl268, tdcinl_dual, topchan, topregr, topselfchk.

## Nedoběhlo
close2, regre2e (log bez RESULT).

## Diagnostika (neukončena)
- Spouštěč `-DSIM_AUTOCAL` předává, `tdc.v` makro `TDC_AUTOCAL_DEF` ho používá (ověřeno čtením).
- Ruční běh `tb_tdc` s ladicím výpisem: kalibrace uvízne, `busy_a=1`, `valid=0`, `fail=0`, `use_ro=1` trvale, `s_wait=0`, `s_cmp=0`. Tedy FSM kalibrace se nedostane za sběr histogramu (před `s_drn`/`s_cmp`), ne do čekání na STM.
- Podezření (neověřeno): vstup/stav v `tdc_chan` zavedený v 0x0427 (nové porty `lw_*` nezapojené v `tb_tdc` = `x`, nebo úprava stavového automatu) blokuje přechod sběr -> výpočet při AUTOCAL = 1.
- Důsledek: starší testbenche (AUTOCAL = 1) selhávají, výrobní tok (`tb_stmcal`, AUTOCAL = 0) a `tb_lutequiv` prošly. Nasazená deska s 0x0427 běží (kalibrace 360 ms, selftest 16/16).

## Stav repozitáře
Nic z 0x0426/0x0427/v0.31.1..v0.33.0 není commitnuto ani pushnuto (viz pokyn "commit + push" čekal na zelené simulace). Další krok po rozhodnutí uživatele: dohledat příčinu uvíznutí, nebo commitnout s poznámkou o selhávajících starých testbenchích.

## Aktualizace 2026-10-24 (uzavřeno)
- Příčina uvíznutí AUTOCAL: záporný posun v `tdc.v` při `CAL_LOG2 < 13`, opraveno commitem `1b6c9b5`; 0x0426/0x0427 jsou v repu.
- Běh `sim_par5` (2026-10-23, RTL 0x0506 před opravou `rdy_h`) dal PASS i u dřív selhávajících / nedoběhlých testů:
  `tdc`, `topchan`, `topregr`, `topselfchk`, `close2`, dále `lutequiv`, `stmcal`, `phyv3` a všech 8 `topv3*`.
- Na finálním RTL 0x0506 (commit `c07bd23`) zopakovány jen `topv3` a `topv3full` (`sim_par6`, PASS); zbylých 6 `topv3*`
  proběhne spolu s opravami N1/N2 (`docs/audit/2026-10-24_pps_tb.md`).
