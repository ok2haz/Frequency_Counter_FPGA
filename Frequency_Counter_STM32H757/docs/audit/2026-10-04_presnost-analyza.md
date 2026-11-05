# Analýza: jak zlepšit přesnost měření a zobrazovaná místa (2026-10-04)

> Stav: FW CM7 s cestou A (spike-reject), FPGA 0x040C. Měřeno na desce + ETH.
> Čísla: σ okna syrově **880 ps** (3,5·10⁻⁹), s cestou A **340 ps** (1,4·10⁻⁹),
> dobrá hrana (bin) ~80 ps, giant bin kód 134 = 19,5 % hran.

## 1. Měřicí řetězec a kde je mez

```
RF vstup -> tvarovac -> FPGA carry-chain TDC (hrana okna, ~80 ps bin)
                        + reciproky citac (edges / dt)
                        -> dt_a [ps] -> f = edges / dt
reference: OCXO 10 MHz -> Si5356 4×100 MHz -> hodiny FPGA
```

**Rozlišení kmitočtu za jedno okno = σ_hrany · √2 / T_hradlo.** Mez při různých délkách
(σ_hrany efektivně ~240 ps po cestě A, tj. σ_okna/√2):

| T_hradlo | rel. rozlišení | @10 MHz |
|---|---|---|
| 0,25 s (okno FPGA) | 1,4·10⁻⁹ | 0,014 Hz |
| 1 s (vzorek statistiky) | 3,4·10⁻¹⁰ | 0,0034 Hz |
| 10 s | 3,4·10⁻¹¹ | 0,34 mHz |
| 100 s | 3,4·10⁻¹² | 34 µHz |

🔑 **Dominantní mez na KRÁTKÝCH τ je TDC, ne OCXO.** Dobrý OCXO má σy@1s ~1·10⁻¹¹;
TDC podlaha je dnes ~3·10⁻¹⁰ @1s → OCXO „neuvidíme", dokud se τ neprotáhne (~10–30 s),
kde TDC podlaha klesne pod OCXO. **Každé zlepšení TDC posune, odkdy vidíme skutečný OCXO.**

## 2. Zpřesnění MĚŘENÍ — páky podle přínosu

### 🥇 (1) Delší hradlo / průměrování — NEJVĚTŠÍ a nejlevnější páka
Rozlišení roste LINEÁRNĚ s dobou hradla (TDC chyba je fixní ~340 ps, dělí se delším oknem).
- **Dnes:** vzorek statistiky = Σhradel ≥ ~1 s (`fpga_acc`, `fpga_freq.c:729`). Headline tedy
  ukazuje ~1s průměr. Allan graf UŽ ukazuje delší τ (1/10/100 s…), takže dlouhé τ jsou
  dostupné v grafu — ale **headline (velké číslo) a „σ@1s" stojí na 1 s**.
- **Optimalizace:** volitelně prodloužit integraci headline (uživatelský gate 1/10/100 s —
  SCPI `SENS:FREQ:GATE` presety už existují, ale řídí jen zobrazení, ne akumulaci).
  Napojit presety na délku akumulačního vzorku → 10 s gate = 10× víc důvěryhodných cifer.
- **Riziko:** nízké (jen delší akumulace). Cena: pomalejší obnova headline.

### 🥈 (2) TDC giant bin — rozpracováno (cesta A hotová, cesta B čeká)
- **Cesta A (HOTOVO):** spike-reject poškozených oken → **2,6× lepší σ** (880→340 ps), ověřeno.
- **Cesta A+ (rychlá):** utáhnout práh `TDC_SPIKE_REL_DIV` (teď 5·10⁻⁹) → chytí i menší
  giant-bin chyby. Odhad 3–4×, cena ~víc vyřazených oken (dnes ~17 %). Nízké riziko.
- **Cesta B (RTL):** odstranit giant bin v `tdc.v` → σ okna k ~54 ps (16×), žádné zahazování,
  čistá i syrová data. ⚠️ Mechanismus nejistý (0x040E 2× selhal), iterativní, vysoké riziko.
  Hypotéza: pomalý stupeň řetězu na přechodu řad GW1NR-9 (placement), ne logika.

### 🥉 (3) Reference / GPSDO disciplinace — ABSOLUTNÍ přesnost
Krátkodobou σy drží OCXO (~1·10⁻¹¹), ale ABSOLUTNÍ kmitočet drift­uje s OCXO.
- **DAC OCXO (AD5693R 0x4C) NENÍ osazen** (`status`: „nenalezen") → OCXO nikdo neladí.
  Osadit + zapnout GPSDO smyčku (driver připraven, `ad5693.h`) → absolutní přesnost na GPS.
- Zlepšuje accuracy (dlouhodobě), ne krátkodobou σ.

### (4) Menší páky
- **Dvoukanálové měření A−B** (stejný signál na CH_A i CH_B): rozdíl vyruší drift OCXO →
  nezávislý odhad σ TDC. Dnes CH_B nebuzen.
- **Gap-free timestamping** (FPGA #62): souvislá fáze → lepší dlouhé τ ADEV/MTIE.
- **Medián/průměr filtr** (#5) — už je, jen na zobrazení.

## 3. Zobrazovaná místa (`freq_uncertain_frac`, `screen_main.c:915`)

### Jak to je dnes
Počet „důvěryhodných" cifer = z `u_res = √2 · MP_TDC_PS / gate_s`, s **`MP_TDC_PS = 57 ps`**
(ideální STA model) a **`gate_ps` = per-okno 0,25 s**.

### 🔴 Dvě chyby, které se částečně ruší (ale obě jsou špatně)
1. **Příliš optimistické TDC:** 57 ps je ideál. Skutečná efektivní σ hrany je ~240 ps
   (cesta A) až ~620 ps (syrově) kvůli obřímu binu → `u_res` je **6–11× lepší, než realita**
   → display podtrhává/ukazuje jako důvěryhodné cifry, které jsou **šum**.
2. **Příliš pesimistické hradlo:** `gate_ps` = 0,25 s (jedno okno), ale zobrazená hodnota je
   průměr za ~1 s (4 okna) → skutečná σ je **2× lepší**, než počítá display → zbytečně
   málo cifer z druhé strany.

Výsledek: display počítá nejistotu z ideálního TDC a krátkého hradla — **nesedí ani jedno**.

### ✅ Optimalizace zobrazovaných míst (doporučeno, nízké riziko)
**Navázat počet cifer na SKUTEČNĚ naměřenou σ, ne na teoretický model.** Firmware už σ
statistiky počítá (Allan `screen_main_adev_1s` / rozptyl vzorků). Počet důvěryhodných cifer =
`log10(hz / σ_namerena)`. Tím:
- cifry odpovídají realitě (po cestě A se jich pár ubere = poctivě),
- s delším průměrováním (páka 1) jich automaticky PŘIBUDE (σ klesá),
- zmizí závislost na hádaném `MP_TDC_PS`.

Mezikrok (když nechceme sahat na statistiku): aspoň
- `MP_TDC_PS` → efektivní změřená hodnota (~240 ps po cestě A, ne 57), a
- `gate_ps` do `freq_uncertain_frac` → akumulovaná délka vzorku (1 s), ne per-okno 0,25 s.

## 4. Doporučené pořadí (přínos / riziko / práce)

| # | opatření | přínos | riziko | práce |
|---|---|---|---|---|
| 1 | **cifry z naměřené σ** (bod 3) | poctivý display hned | nízké | malá (CM7) |
| 2 | **delší průměrování headline** (gate presety) | ×4–×40 rozlišení | nízké | malá (CM7) |
| 3 | **cesta A+ utáhnout práh** | ×1,5 σ | nízké | triviální |
| 4 | **cesta B — RTL giant bin** | ×16 σ, čistá data | vysoké | velká, iterativní |
| 5 | **osadit DAC + GPSDO smyčka** | absolutní přesnost | střední (HW) | střední |

🔑 **Nejlepší poměr: 1 + 2 + 3** (vše CM7, nízké riziko) dá poctivý display + výrazně víc
rozlišení bez rizikového RTL. Cesta B a GPSDO jsou větší samostatné projekty.
