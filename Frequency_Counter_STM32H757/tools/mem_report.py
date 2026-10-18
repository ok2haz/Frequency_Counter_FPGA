# -*- coding: utf-8 -*-
"""Pamet podle funkci: kolik jaka funkce (modul/funkcni celek) zabira a KDE (FLASH / RAM_D1 / DTCM / SDRAM / SRAM4 ...).

    python tools/mem_report.py            # vypise Markdown na stdout (CM7 + CM4 z Release/*.map)
    python tools/mem_report.py --write    # zapise docs/PAMET_FUNKCI.md

Zdroj pravdy je MAPA LINKERU (`CM?/Release/*.map`) = co se doopravdy slinkovalo, ne odhad ze zdrojaku. Kazdy vstupni oddil se priradi
objektu (.o) a ten funkcnimu celku podle tabulky pravidel nize; pravidla se hodnoti v poradi, prvni shoda vyhrava. Co zadne pravidlo
nechyti, spadne do „Ostatni“ a vypise se (aby tabulka nelhala o vlastnim zbytku).
Pridava i STATICKE oblasti, ktere v mape nejsou (framebuffery v SDRAM, SRAM4 IPC, ETH DMA) -- ty jsou dany linkerem/MPU, ne symboly.
"""
import glob
import os
import re
import subprocess
import sys
from collections import defaultdict

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

RULES_CM7 = [
    (r"libui/src/fonts/|ui_font_", "Fonty (libui)"),
    (r"libui/", "libui - komponenty UI"),
    (r"libprim/", "libprim - 2D grafika"),
    (r"prim_stm32|app/hal/", "Graficky HAL (LTDC, DMA2D, framebuffery)"),
    (r"screens/screen_main", "Hlavni obrazovka + statistika (Allan, trend, histogram, spektrogram)"),
    (r"app/app_gpsdo", "UI okna (menu, nastaveni, GPS, ADRET, kalibrace, ...)"),
    (r"app/", "app - ostatni (encoder, anim, ...)"),
    (r"fpga_freq|freertos_task_fpga|gpsdo|meas_present|meas_math|phase_noise|sdram_log", "Citac FPGA + smycka GPSDO + metrologie"),
    (r"/gps\.o|/rtc\.o|gps\.o$", "GPS + RTC"),
    (r"datalog|sd_export|FatFs|/ff\.o|diskio|ff_gen|syscall|ffunicode|fatfs|bsp_driver_sd", "Datalog + SD (FatFs)"),
    (r"freertos_task_ui|encoder|tc358762|ws_panel|ft5x06", "UI vstup a panel (dotyk, encoder, bridge)"),
    (r"scpi|ipc", "SCPI + IPC"),
    (r"w25q|/setup\.o|calib|syscfg|errlog|flightrec|quadspi", "Uloziste (W25Q, nastaveni, kalibrace, errlog)"),
    (r"freertos_task_sensors|sensor|ads1115|ad5693|si5356|/i2c|alarm|autocal|beeper|bootled|watchdog", "Senzory, I2C, alarmy, watchdog"),
    (r"membench|screenshot|freertos_task_uart|selftest|freertos_hooks", "Diagnostika (UART prikazy, benchmark, screenshot)"),
    (r"Middlewares/.*FreeRTOS|/freertos\.o|cmsis_os|heap_|/tasks\.o|/queue\.o|/list\.o|/timers\.o|/port\.o|event_groups|stream_buffer", "FreeRTOS"),
    (r"USB|usbd|usb_", "USB CDC"),
    (r"Drivers/STM32H7xx_HAL|stm32h7xx_hal|stm32h7xx_ll", "HAL ST"),
    (r"libc|libm|libgcc|lib_a|libnosys|libg|crt", "libc / libm / libgcc"),
    (r"startup|system_stm32|sysmem|syscalls|/main\.o|stm32h7xx_it|_hal_msp|/gpio\.o|/usart\.o|/spi\.o|/dma|/adc\.o|/fmc\.o|/ltdc|/dsihost|/tim\.o|/sdmmc", "Start, main, periferie (CubeMX)"),
]
RULES_CM4 = [
    (r"httpd_min", "Web (HTTP server + SPA)"),
    (r"scpi_tcp|/scpi\.o|ipc_scpi|meas_math|meas_present", "SCPI (TCP 5025, parser)"),
    (r"LwIP|lwip|lwip_app", "lwIP"),
    (r"ethernetif|lan8742|/eth\.o|hal_eth", "Ethernet (MAC, PHY)"),
    (r"ipc_cm4|ipc", "IPC"),
    (r"iwdg2|beep|/main\.o|stm32h7xx_it|startup|system_stm32|syscalls|sysmem|gpio|/tim\.o", "Start, main, periferie (CubeMX)"),
    (r"Drivers/STM32H7xx_HAL|stm32h7xx_hal", "HAL ST"),
    (r"libc|libm|libgcc|lib_a|libnosys|libg|crt", "libc / libm / libgcc"),
]

# Pojmenovane datove struktury (RAM), ktere stoji za samostatnou radku -- hleda se v `nm` podle jmena symbolu.
DATA_SYMS_CM7 = [
    ("s_adev", "Allan pyramida (filtrovana): akumulatory ADEV/MDEV/HDEV, 7 stage"),
    ("s_adev_raw", "Allan pyramida SUROVA (bez filtru odlehlych)"),
    ("s_tr", "Trend pyramida (az 60 dni, 9 stage)"),
    ("s_y", "Plochy ring statistik (120 s) - offset, drift, sigma@1s"),
    ("s_wf", "Ring spektrogramu (720 s)"),
    ("s_hy", "Ring HISTOGRAMU (3600 s)"),
    ("s_hscr", "Scratch histogramu (median/MAD kvickselektem)"),
    ("s_ad_x", "Body fazoveho grafu ADRET"),
    ("g_sensors", "Statistiky senzoru (10 senzoru)"),
    ("s_hist", "Historie senzoru (sensor_hist, ~3,4 h)"),
]


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace").stdout


def find_tool(name):
    c = sorted(glob.glob(r"C:\ST\STM32CubeIDE*\STM32CubeIDE\plugins\*gnu-tools*\tools\bin\%s.exe" % name))
    return c[-1] if c else None


def mem_regions(text):
    reg = []
    m = re.search(r"Memory Configuration\n\nName [^\n]*\n(.*?)\n\nLinker script", text, re.S)
    for ln in m.group(1).splitlines():
        p = ln.split()
        if len(p) >= 3 and p[1].startswith("0x") and p[0] != "*default*":
            reg.append((p[0], int(p[1], 16), int(p[2], 16)))
    return reg


def region_of(addr, regs):
    for n, o, l in regs:
        if o <= addr < o + l:
            return n
    return "?"


def parse_map(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    regs = mem_regions(text)
    body = text[text.index("Linker script and memory map"):]
    out = []
    cur_out = None
    lines = body.splitlines()
    i = 0
    while i < len(lines):
        ln = lines[i]
        m = re.match(r"^(\.[\w.]+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)", ln)
        if m:
            cur_out = m.group(1)
            i += 1
            continue
        if re.match(r"^\.[\w.]+$", ln):                       # nazev vystupni sekce na vlastnim radku
            cur_out = ln.strip()
            i += 1
            continue
        if ln.startswith(" ") and re.match(r"^ [.\w*]", ln) and cur_out:
            toks = ln.split()
            if len(toks) == 1 and toks[0].startswith(".") and i + 1 < len(lines):   # dlouhe jmeno: data na dalsim radku
                toks = toks + lines[i + 1].split()
                i += 1
            if len(toks) >= 4 and toks[1].startswith("0x") and toks[2].startswith("0x"):
                size = int(toks[2], 16)
                if size:
                    out.append((cur_out, toks[0], int(toks[1], 16), size, " ".join(toks[3:])))
        i += 1
    return regs, out


def classify(rules, obj):
    o = obj.replace("\\", "/")
    for rx, name in rules:
        if re.search(rx, o):
            return name
    return "Ostatni"


def flash_ram(sec_out, in_name, addr, regs):
    """-> ('flash'|'ram', region) podle vystupni sekce"""
    r = region_of(addr, regs)
    return r


def report(core, mappath, elf, rules, extra_static):
    regs, ents = parse_map(mappath)
    cap = {n: l for n, o, l in regs}
    per = defaultdict(lambda: defaultdict(int))     # feature -> region -> bytes
    other_objs = defaultdict(int)
    # .data se pocita 2x: v RAM (adresa) i ve FLASH (load address) -- load adresu mapa u input sekci neuvadi, proto ji pricteme zvlast
    data_total = defaultdict(int)
    for sec_out, in_name, addr, size, obj in ents:
        if sec_out in (".comment", ".ARM.attributes") or sec_out.startswith(".debug") or addr == 0 and sec_out != ".itcm":
            continue
        reg = region_of(addr, regs)
        feat = classify(rules, obj)
        if feat == "Ostatni":
            other_objs[obj.replace("\\", "/").split("/")[-1]] += size
        per[feat][reg] += size
        if sec_out == ".data":
            data_total[feat] += size
    for f, b in data_total.items():
        per[f]["FLASH"] += b                          # kopie inicializovanych dat ve FLASH
    return regs, cap, per, other_objs


def fmt_kb(b):
    return "%.1f" % (b / 1024.0) if b else "-"


def table(core, regs, cap, per, other_objs, region_cols):
    rows = []
    for feat, d in per.items():
        rows.append((feat, d))
    rows.sort(key=lambda r: -sum(r[1].values()))
    s = "| Funkcni celek | " + " | ".join("%s [kB]" % c for c in region_cols) + " |\n"
    s += "|---|" + "---:|" * len(region_cols) + "\n"
    tot = defaultdict(int)
    for feat, d in rows:
        if not any(d.get(c, 0) for c in region_cols):
            continue
        s += "| %s | " % feat + " | ".join(fmt_kb(d.get(c, 0)) for c in region_cols) + " |\n"
        for c in region_cols:
            tot[c] += d.get(c, 0)
    s += "| **CELKEM** | " + " | ".join("**%s**" % fmt_kb(tot[c]) for c in region_cols) + " |\n"
    s += "| kapacita | " + " | ".join(fmt_kb(cap.get(c, 0)) for c in region_cols) + " |\n"
    s += "| vyuziti | " + " | ".join(("%.1f %%" % (100.0 * tot[c] / cap[c])) if cap.get(c) else "-" for c in region_cols) + " |\n"
    return s


def top_symbols(nm, elf, regs, n=18, ram_only=True):
    out = run([nm, "--print-size", "--size-sort", "--radix=x", "-C", elf]).splitlines()
    rows = []
    for ln in out:
        p = ln.split(None, 3)
        if len(p) < 4:
            continue
        addr, size, typ, name = int(p[0], 16), int(p[1], 16), p[2], p[3]
        reg = region_of(addr, regs)
        if ram_only and typ.lower() not in ("b", "d"):
            continue
        rows.append((size, name, reg, typ))
    rows.sort(reverse=True)
    return rows[:n]


def main():
    write = "--write" in sys.argv
    nm = find_tool("arm-none-eabi-nm")
    md = []
    md.append("# Pamet podle funkci - kolik zabira a kde\n")
    md.append("> Generuje `python tools/mem_report.py --write` z **mapy linkeru** (`CM7/Release/H757_LED_CM7.map`, `CM4/Release/H757_LED_CM4.map`) = co se doopravdy slinkovalo. "
              "Funkcni celek se urcuje podle objektoveho souboru (pravidla v tools/mem_report.py); co zadne pravidlo nechyti, je v radku „Ostatni“. "
              "Staticke oblasti mimo mapu (framebuffery, IPC, ETH DMA) jsou v samostatne tabulce. FLASH u `.data` zahrnuje load kopii inicializovanych dat.\n")
    for core, rules, cols in (("CM7", RULES_CM7, ["FLASH", "RAM_D1", "DTCM_APP", "RAM_D2", "SDRAM", "SDRAM_LOG", "RAM_D3"]),
                              ("CM4", RULES_CM4, ["FLASH", "RAM", "RAM_D2", "ETH_D2"])):
        mp = os.path.join(ROOT, core, "Release", "H757_LED_%s.map" % core)
        elf = os.path.join(ROOT, core, "Release", "H757_LED_%s.elf" % core)
        if not os.path.exists(mp):
            md.append("## %s\n\n(mapa chybi: %s)\n" % (core, mp))
            continue
        regs, cap, per, other = report(core, mp, elf, rules, None)
        cols = [c for c in cols if any(per[f].get(c) for f in per) or c in cap]
        cols = [c for c in cols if cap.get(c)]
        md.append("## %s\n" % core)
        md.append(table(core, regs, cap, per, other, cols))
        if other:
            top = sorted(other.items(), key=lambda kv: -kv[1])[:8]
            md.append("\n*Ostatni (nezarazene objekty, top):* " + ", ".join("%s %s kB" % (k, fmt_kb(v)) for k, v in top) + "\n")
        if nm and os.path.exists(elf):
            md.append("\n### %s - nejvetsi datove objekty v RAM\n" % core)
            md.append("| Symbol | Velikost [kB] | Kde |\n|---|---:|---|\n")
            for size, name, reg, typ in top_symbols(nm, elf, regs, 16):
                md.append("| `%s` | %s | %s |\n" % (name, fmt_kb(size), reg))
            if core == "CM7":
                md.append("\n### CM7 - pojmenovane statisticke struktury (pod Allanem, trendem, histogramem...)\n")
                md.append("| Struktura | Velikost [kB] | Kde | Co to je |\n|---|---:|---|---|\n")
                syms = {}
                for ln in run([nm, "--print-size", "--radix=x", "-C", elf]).splitlines():
                    p = ln.split(None, 3)
                    if len(p) == 4:
                        syms[p[3].strip()] = (int(p[1], 16), region_of(int(p[0], 16), regs))
                for key, desc in DATA_SYMS_CM7:
                    for nmz, (sz, rg) in syms.items():
                        if nmz == key or nmz.endswith("::" + key) or nmz == key + "":
                            md.append("| `%s` | %s | %s | %s |\n" % (nmz, fmt_kb(sz), rg, desc))
                            break
    md.append("\n## Staticke oblasti mimo mapu (dane linkerem / MPU / nastavenim)\n")
    md.append("| Oblast | Adresa | Velikost | Co v ni je |\n|---|---|---:|---|\n")
    for a, ad, sz, what in (
        ("SDRAM FB0/FB1/FB2 (triple buffering)", "0xC0000000 / 0xC0100000 / 0xC0200000", "3 x 750 kB (800x480 RGB565 = 768 000 B)", "3 framebuffery, MPU region 0 (4 MB WT)"),
        ("SDRAM scratch", "0xC0400000", "512 kB testovano", "membench / bgcheck (MPU region 1)"),
        ("SDRAM `.sdram`", "0xC0800000", "viz tabulka CM7 (SDRAM)", "bg_cache pozadi (768 000 B), atlas glyfu (256 kB), masky; Device pamet"),
        ("SDRAM `.measlog`", "0xC1000000", "8 MB", "datova cache mereni: 262 144 zaznamu po 32 B (~18 h pri 4 mereni/s), MPU region 3"),
        ("SRAM4 / D3 `.ipc_shared`", "0x38000000", "64 kB", "IPC CM7<->CM4 (snapshot, cmd/resp ring, log/errlog/raw kanaly), MPU non-cacheable"),
        ("SRAM3 / ETH DMA", "0x30040000", "32 kB (12,7 kB pouzito)", "ETH deskriptory + RX pool (CM4)"),
        ("DTCM spodni 64 kB", "0x20000000", "64 kB", "VYHRAZENO pro `membench` (nic tam nedavat)"),
        ("Interni FLASH banky", "0x08000000 (CM7), bank 2 (CM4)", "2 x 1 MB", "CM7 + CM4 obraz; fonty ~256 kB jsou v CM7 .rodata"),
        ("W25Q512 (QSPI)", "externi", "64 MB", "CONFIG / CALIB / SETUP / DATA (datalog ~22 MB, ~80 dni) - viz w25q_map.h"),
    ):
        md.append("| %s | `%s` | %s | %s |\n" % (a, ad, sz, what))
    txt = "".join(md)
    # Markdown: prazdny radek pred nadpisem a pred tabulkou (generator sklada kousky bez nich)
    txt = re.sub(r"([^\n])\n(#{1,3} )", r"\1\n\n\2", txt)
    txt = re.sub(r"([^\n|])\n(\|)", r"\1\n\n\2", txt)
    txt = re.sub(r"(\n> [^\n]*)\n([^\n])", r"\1\n\n\2", txt)
    if write:
        p = os.path.join(ROOT, "docs", "PAMET_FUNKCI.md")
        open(p, "w", encoding="utf-8").write(txt)
        print("zapsano", p, len(txt), "B")
    else:
        print(txt)


if __name__ == "__main__":
    main()
