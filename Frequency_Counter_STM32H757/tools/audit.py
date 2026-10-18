"""Staticky audit: prisna varovani + -fanalyzer nad NASIM kodem (bez vendoru).

Flagy se berou z GENEROVANEHO subdir.mk toho ktereho adresare, aby sedely
s realnym buildem — a hlavne aby sedely INCLUDE CESTY. (Pokus pouzit jeden
spolecny flag set z app/subdir.mk selhal: Core/Src pak nenajde `adc.h`
a 57 souboru se TISE nepreloz  ilo, takze audit hlasil "0 varovani",
aniž by cokoli zkontroloval.)
"""
import glob, io, os, re, subprocess, sys

R = r"C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_STM32H757"

# 🔴 NEJNOVEJSI toolchain, ne `glob(...)[0]`. V IDE jsou nainstalovane DVA
# (gnu-tools-for-stm32 13.3 a 14.3) a puvodni `[0]` bral podle abecedy ten
# STARSI (13.3), zatimco firmware se stavi 14.3. Audit tim tise kontroloval
# jinym kompilatorem nez realny build: 14.3 najde `-Wformat-truncation`
# v `app_gpsdo.c`, kterou 13.3 NEVIDI (zmereno 2026-09-04, STATUS #135).
# Verze se parsuje cislem — abecedni razeni by u 9.x vs 14.x selhalo.
def _pick_gcc():
    cands = glob.glob(r"C:\ST\STM32CubeIDE*\STM32CubeIDE\plugins"
                      r"\*gnu-tools*\tools\bin\arm-none-eabi-gcc.exe")
    if not cands:
        sys.exit("arm-none-eabi-gcc nenalezen")
    def ver(p):
        m = re.search(r"gnu-tools-for-stm32\.(\d+)\.(\d+)", p)
        return (int(m.group(1)), int(m.group(2))) if m else (0, 0)
    return max(cands, key=ver)

GCC = _pick_gcc()

W = ("-Wall -Wextra -Wshadow -Wcast-align -Wnull-dereference -Wduplicated-cond "
     "-Wduplicated-branches -Wlogical-op -Wformat=2 -Wstrict-aliasing=2 "
     "-Wmaybe-uninitialized").split()

DIRS = [
    ("CM7/Core/Src",            "CM7/Release/Core/Src",            "cortex-m7"),
    ("CM7/app",                 "CM7/Release/app",                 "cortex-m7"),
    ("CM7/app/screens",         "CM7/Release/app/screens",         "cortex-m7"),
    ("CM7/app/hal/stm32",       "CM7/Release/app/hal/stm32",       "cortex-m7"),
    ("CM7/libui/src",           "CM7/Release/libui/src",           "cortex-m7"),
    ("CM7/libprim/src",         "CM7/Release/libprim/src",         "cortex-m7"),
    ("CM7/libprim/src/internal","CM7/Release/libprim/src/internal","cortex-m7"),
    ("CM4/Core/Src",            "CM4/Release/Core/Src",            "cortex-m4"),
]

def flags_of(subdir_mk, cpu):
    txt = open(subdir_mk, encoding="utf-8", errors="replace").read()
    m = re.search(r'(-mcpu=' + cpu + r'.*?)\s+-o\s+"\$@"', txt, re.S)
    if not m:
        return None
    return [a for a in m.group(1).replace('"', '').split() if a]

# Verzi VYPSAT — kdyby se v IDE objevil dalsi toolchain, at je hned videt,
# cim se auditovalo (tise pouzity jiny kompilator byl prave ten problem).
_v = subprocess.run([GCC, "-dumpversion"], capture_output=True, text=True).stdout.strip()
print("gcc:", _v, "(" + os.path.basename(os.path.dirname(os.path.dirname(os.path.dirname(GCC)))) + ")")

ok = bad = warn_files = 0
log = []
for src, rel, cpu in DIRS:
    mk = os.path.join(R, rel, "subdir.mk")
    if not os.path.isfile(mk):
        print("CHYBI", mk); continue
    fl = flags_of(mk, cpu)
    if not fl:
        print("NEROZPARSOVANO", mk); continue
    for f in sorted(glob.glob(os.path.join(R, src, "*.c"))):
        if "ui_font_" in os.path.basename(f):
            continue                     # generovane tabulky glyfu
        p = subprocess.run([GCC] + fl + W + ["-fanalyzer", "-c", f,
                                             "-o", os.path.join(os.environ.get("TEMP", "."), "audit_o.o")],
                           cwd=os.path.join(R, "CM4/Release" if cpu=="cortex-m4" else "CM7/Release"),
                           capture_output=True, text=True)
        err = p.stderr
        if p.returncode != 0:
            bad += 1
            log.append("### SELHALO %s\n%s" % (f, err[:1500]))
        else:
            ok += 1
            if "warning:" in err:
                warn_files += 1
                log.append("### %s\n%s" % (f, err))

print("prelozeno OK: %d   SELHALO: %d   souboru s varovanim: %d" % (ok, bad, warn_files))
out = os.path.join(os.environ.get("TEMP", "."), "audit_w.log")
open(out, "w", encoding="utf-8").write("\n".join(log))
print("log:", out)


# ══════════════════════════════════════════════════════════════════════════════
# VRSTVY, KTERE PREKLADAC NEDOKAZE  (viz ../AUDIT.md)
#
# 🔴 Duvod, proc tohle vzniklo: sest vad v tomto projektu proslo CISTYM buildem
# i cistym `-fanalyzer` auditem — mrtvy encoder (#106), zahozeny `errlog`
# (obraz narostl o 16 B), SDRAM init ignorujici vsech 6 navratovych hodnot,
# `qspi_recv_fast` uvadejici QUADSPI do BUSY, ETH TX na adrese z CM4 aliasu
# a ztrata AF na PG8/PG11. Prekladac kontroluje, jestli je kod spravne NAPSANY;
# nerekne, jestli se dostal do obrazu, jestli na tu adresu nekdo dosahne
# a jestli neco odkazuje na to, co uz neexistuje.
#
# ⚠️ Kazda kontrola tady musi byt UZKA. Kontrola, ktera hlasi sum, se zacne
# ignorovat — a pak je horsi nez zadna, protoze vyrába dojem pokryti.
# (Zmereno: naivni test "zahozena navratova hodnota" dal 96 nalezu, ale
# vetsina z nich byly funkce vracejici `void`. Takova kontrola tu proto NENI.)
# ══════════════════════════════════════════════════════════════════════════════

NM = os.path.join(os.path.dirname(GCC), "arm-none-eabi-nm.exe")
ELF7 = os.path.join(R, "CM7/Release/H757_LED_CM7.elf")
ELF4 = os.path.join(R, "CM4/Release/H757_LED_CM4.elf")
SIZEDB = os.path.join(R, "tools", ".audit_sizes.txt")

problems = []
ALL_FILES = []

def nm_syms(elf, args=("--print-size", "--radix=d")):
    """Vrati {jmeno: (typ, velikost)} ze slinkovaneho obrazu / objektu."""
    if not os.path.isfile(elf):
        return {}
    out = subprocess.run([NM] + list(args) + [elf], capture_output=True, text=True).stdout
    d = {}
    for ln in out.split("\n"):
        p = ln.split()
        if len(p) == 4:
            d[p[3]] = (p[2], int(p[1]) if p[1].isdigit() else 0)
        elif len(p) == 3:
            d[p[2]] = (p[1], 0)
    return d


def check_image():
    """1) Dostala se zmena do OBRAZU? Kdyz pridas kod a `.text` neroste,
    `--gc-sections` ho zahodil a build i audit pritom mlci (stalo se 2x)."""
    print("\n== obraz ==")
    prev = {}
    if os.path.isfile(SIZEDB):
        for ln in io.open(SIZEDB, encoding="utf-8"):
            k, _, v = ln.strip().partition("=")
            if v.isdigit():
                prev[k] = int(v)
    cur = {}
    for name, elf in (("CM7", ELF7), ("CM4", ELF4)):
        if not os.path.isfile(elf):
            continue
        sz = subprocess.run([os.path.join(os.path.dirname(GCC), "arm-none-eabi-size.exe"), elf],
                            capture_output=True, text=True).stdout.split("\n")
        if len(sz) < 2 or not sz[1].split():
            continue
        text = int(sz[1].split()[0])
        cur[name] = text
        if name in prev:
            d = text - prev[name]
            mark = "" if d else "   <== BEZE ZMENY (pridal jsi kod? linker ho zahodil)"
            print("  %s .text = %d B  (%+d proti minulemu auditu)%s" % (name, text, d, mark))
        else:
            print("  %s .text = %d B  (prvni zaznam)" % (name, text))
    io.open(SIZEDB, "w", encoding="utf-8").write(
        "".join("%s=%d\n" % (k, v) for k, v in sorted(cur.items())))


# Handlery, ktere NESMI byt prazdne. Telo <= 4 B = jedina instrukce `b .`,
# tedy TICHE zamrznuti (STATUS #225: ctyri takove se nasly na CM7, jeden na CM4).
HANDLERS = ["HardFault_Handler", "NMI_Handler", "MemManage_Handler",
            "BusFault_Handler", "UsageFault_Handler", "Error_Handler"]

def check_handlers():
    print("\n== chybove handlery (telo <= 4 B = tiche zamrznuti) ==")
    for name, elf in (("CM7", ELF7), ("CM4", ELF4)):
        syms = nm_syms(elf)
        for h in HANDLERS:
            if h not in syms:
                print("  %s %-20s CHYBI v obrazu" % (name, h))
                problems.append("%s: handler %s chybi" % (name, h))
                continue
            _, size = syms[h]
            if size <= 4:
                print("  %s %-20s %3d B  <== PRAZDNY/while(1)" % (name, h, size))
                problems.append("%s: handler %s je nemy (%d B)" % (name, h, size))
            else:
                print("  %s %-20s %3d B  ok" % (name, h, size))


# Buffery, na ktere saha ETH DMA. Ta je AHB master v D2 a tamni SRAM vidi
# VYHRADNE na `0x30xxxxxx`; alias `0x10xxxxxx` je pohled JADRA CM4.
# Presne tohle zpusobilo, ze DHCP nikdy neproslo (TX buffer mel 0x1002845E).
DMA_SYMS = {"DMARxDscrTab": "3004", "DMATxDscrTab": "3004",
            "memp_memory_RX_POOL_base": "3004", "ram_heap": "1002"}

def check_dma_addrs():
    print("\n== adresy pro ETH DMA (CM4) ==")
    if not os.path.isfile(ELF4):
        print("  CM4 .elf chybi"); return
    out = subprocess.run([NM, ELF4], capture_output=True, text=True).stdout
    addr = {}
    for ln in out.split("\n"):
        p = ln.split()
        if len(p) == 3:
            addr[p[2]] = p[0]
    for sym, want in DMA_SYMS.items():
        a = addr.get(sym)
        if a is None:
            print("  %-26s NENALEZEN" % sym); continue
        ok_ = a.lower().startswith(want)
        print("  %-26s @0x%s  %s" % (sym, a, "ok" if ok_ else "<== MA BYT 0x" + want + "xxxx"))
        if not ok_:
            problems.append("%s je na 0x%s, ma byt 0x%sxxxx" % (sym, a, want))


# Verejne API knihoven a zamerne pripravene funkce — NEJSOU to nalezy.
# (SKILL §7c: sken mrtveho kodu ma dve tridy falesne pozitivnich.)
# ⚠️ Bez tohohle seznamu hlasi kontrola 46 "nalezu", z nichz je vetsina
# ZAMERNA — a zaslepena kontrola se prestane cist (SKILL §7c: sken mrtveho kodu
# ma dve tridy falesne pozitivnich — verejne API knihoven a vendor stuby).
DEAD_OK_PREFIX = ("prim_", "ui_", "_")                 # verejne API + newlib stuby
DEAD_OK_SUFFIX = ("_selftest", "_MspDeInit", "_MspInit")
DEAD_OK_EXACT  = {
    "SystemInit", "SystemCoreClockUpdate", "Reset_Handler",
    "PeriphCommonClock_Config",              # generuje CubeMX, vola se jen na CM7
    "initialise_monitor_handles",            # newlib semihosting
    "iwdg2_init",                            # ZAMERNE nevolany (IWDG2 vypnuty, viz CLAUDE.md)
    "sdram_log_read_back", "sdram_log_invalidate",   # API pro planovaneho konzumenta (#77)
    "ws_panel_set_portc",                    # vypnuto prepinacem I2C4_RECOVERY_TOUCHES_ATTINY
    # ⚠️ Dolozene falesne pozitivni (STATUS #106): funkce se POUZIVA, jen ji
    # prekladac inlinoval, takze v .elf nema vlastni symbol.
    "scpi_gate_s",
    # Diagnosticke pristupove funkce — ctou se SONDOU z .bss, ne volanim.
    "scpi_selftest_fail_line", "httpd_min_selftest_fail_line",
    "mon_ocxo_dt",                           # pripravena pristupova funkce prahoveho monitoru
}
# Soubory, ktere generuje CubeMX/ST — jejich nepouzite funkce nejsou nas nalez.
DEAD_OK_OBJ = ("syscalls.o", "sysmem.o", "system_stm32h7xx", "usbd_", "sd_diskio.o",
               "user_diskio.o", "fatfs.o", "stm32h7xx_hal_msp.o",
               "bsp_driver_sd.o", "stm32h7xx_hal_timebase_tim.o")

def check_dead_code():
    """4) Globalni funkce, ktera je v .o, ale ne v .elf = nikdo ji nevola.
    Presne timhle byl cely encoder (#106) mrtvy, aniz by to build ohlasil."""
    print("\n== mrtvy kod (definovano, ale linker to zahodil) ==")
    for name, elf, reldir in (("CM7", ELF7, "CM7/Release"), ("CM4", ELF4, "CM4/Release")):
        if not os.path.isfile(elf):
            continue
        live = set(nm_syms(elf).keys())
        objs = []
        for root, _d, fs in os.walk(os.path.join(R, reldir)):
            low = root.replace("\\", "/").lower()
            if "/drivers/" in low or "/middlewares/" in low or "/lwip/" in low:
                continue
            objs += [os.path.join(root, f) for f in fs if f.endswith(".o")]
        found = []
        for o in objs:
            for sym, (typ, _sz) in nm_syms(o).items():
                if typ != "T" or sym in live:
                    continue
                if (sym.startswith(DEAD_OK_PREFIX) or sym.endswith(DEAD_OK_SUFFIX)
                        or sym in DEAD_OK_EXACT
                        or any(k in os.path.basename(o) for k in DEAD_OK_OBJ)):
                    continue
                found.append((os.path.basename(o), sym))
        if found:
            for o, sym in sorted(found)[:20]:
                print("  %s %-34s (%s)" % (name, sym, o))
            print("  %s celkem %d" % (name, len(found)))
            problems.append("%s: %d mrtvych globalnich funkci" % (name, len(found)))
        else:
            print("  %s bez nalezu" % name)


# Znacky, ze komentar mluvi o necem, co UZ NEEXISTUJE nebo se tak zamerne
# nejmenuje. Takova zminka NENI zastaraly odkaz, ale zaznam "proc".
ZANIKLO = ("odstran", "zrus", "smaz", "uz neexistuje", "neexistuje",
           "byval", "drive ", "driv ", "puvodn", "zaniklo", "vyrazen",
           "nahrazen", "misto ", "jmeno ne", "ne `", "mrtv",
           "neni to", "zamerne jin", "kolizi")

def strip_comments(txt):
    """Odstrani C komentare — potreba, aby se identifikator zminovany JEN
    v komentari nepovazoval za pouzity (jinak by kontrola nize nic nenasla)."""
    out, i, n = [], 0, len(txt)
    st = 0   # 0 kod, 1 blok, 2 radek, 3 retezec, 4 znak
    while i < n:
        c = txt[i]; nx = txt[i+1] if i+1 < n else ""
        if st == 0:
            if c == "/" and nx == "*": st = 1; i += 2; continue
            if c == "/" and nx == "/": st = 2; i += 2; continue
            if c == '"': st = 3
            elif c == "'": st = 4
            out.append(c)
        elif st == 1:
            if c == "*" and nx == "/": st = 0; i += 2; continue
        elif st == 2:
            if c == "\n": st = 0; out.append(c)
        elif st in (3, 4):
            out.append(c)
            if c == "\\": out.append(nx); i += 2; continue
            if (st == 3 and c == '"') or (st == 4 and c == "'"): st = 0
        i += 1
    return "".join(out)


def own_sources():
    for root, _d, fs in os.walk(R):
        low = root.replace("\\", "/").lower()
        if any(k in low for k in ("/drivers/", "/middlewares/", "/release/", "/debug/", "/.git")):
            continue
        for f in fs:
            if f.endswith((".c", ".h")) and not f.startswith("ui_font_"):
                yield os.path.join(root, f)


def check_comments():
    """5) Aktualnost komentaru. UZKE testy, aby to nesumelo:
       (a) `neco()` v komentari -> identifikator musi v kodu existovat
       (b) `cesta/soubor` v komentari -> soubor musi existovat
    Presne tahle trida se tu opakovane rozesla: komentar hlasil "selftest 13/13"
    pri 16 testech a "datalog ~600 dni" pri ~80 (STATUS #35). """
    print("\n== aktualnost komentaru ==")
    srcs = list(own_sources())
    global ALL_FILES
    ALL_FILES = []
    for root, _d, fs in os.walk(os.path.dirname(R)):
        if "\\.git" in root:
            continue
        ALL_FILES += [os.path.join(root, f) for f in fs]
    universe, comments = set(), []
    for p in srcs:
        try:
            txt = io.open(p, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        universe |= set(re.findall(r"[A-Za-z_][A-Za-z0-9_]{2,}", strip_comments(txt)))
        for m in re.finditer(r"/\*.*?\*/|//[^\n]*", txt, re.S):
            comments.append((p, m.group(0)))

    stale_sym, stale_file = [], []
    for p, c in comments:
        # 🔑 Komentar, ktery SAM rika, ze vec zanikla (nebo ze se tak ZAMERNE
        # nejmenuje), je SPRAVNY — chybejici symbol je tam ocekavany.
        # ⚠️ Bez tohohle filtru hlasila kontrola 12 "nalezu" a VSECHNY byly
        # falesne: sla o historicke poznamky typu "`prim_version()` ODSTRANENO".
        # Kontrola by tim tlacila na mazani prave te historie, kvuli ktere
        # tenhle projekt komentare pise — tedy by aktivne skodila.
        if any(k in c.lower() for k in ZANIKLO):
            continue
        for m in re.finditer(r"`([a-z_][a-z0-9_]{3,})\(\)`", c):
            if m.group(1) not in universe:
                stale_sym.append((os.path.basename(p), m.group(1)))
        for m in re.finditer(r"`((?:[A-Za-z0-9_.\-]+/)+[A-Za-z0-9_.\-]+\.(?:c|h|md|py|ps1|sh|ld))`", c):
            rel = m.group(1)
            # ⚠️ Trojí pokus, jinak kontrola hlasi falesne pozitivni: komentare
            # pouzivaji jak cesty RELATIVNE k souboru (`../../../CM7/...`), tak
            # zkracene od korene projektu (`app/hal/...` misto `CM7/app/hal/...`).
            cands = [os.path.join(os.path.dirname(p), rel),
                     os.path.join(R, rel),
                     os.path.join(os.path.dirname(R), rel)]
            if any(os.path.isfile(os.path.normpath(c)) for c in cands):
                continue
            tail = rel.replace("/", os.sep)
            if any(f.endswith(tail) for f in ALL_FILES):
                continue
            stale_file.append((os.path.basename(p), rel))

    for what, lst in (("odkaz na NEEXISTUJICI funkci", stale_sym),
                      ("odkaz na NEEXISTUJICI soubor", stale_file)):
        uniq = sorted(set(lst))
        if uniq:
            for f, x in uniq[:15]:
                print("  %-26s %s" % (f, x))
            print("  %s: %d" % (what, len(uniq)))
            problems.append("%s: %d" % (what, len(uniq)))
        else:
            print("  %s: 0" % what)


check_image()
check_handlers()
check_dma_addrs()
check_dead_code()
check_comments()

print("\n" + "=" * 60)
if problems:
    print("NALEZY (%d):" % len(problems))
    for x in problems:
        print("  - " + x)
else:
    print("bez nalezu v rozsirenych vrstvach")
