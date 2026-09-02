"""Staticky audit: prisna varovani + -fanalyzer nad NASIM kodem (bez vendoru).

Flagy se berou z GENEROVANEHO subdir.mk toho ktereho adresare, aby sedely
s realnym buildem — a hlavne aby sedely INCLUDE CESTY. (Pokus pouzit jeden
spolecny flag set z app/subdir.mk selhal: Core/Src pak nenajde `adc.h`
a 57 souboru se TISE nepreloz  ilo, takze audit hlasil "0 varovani",
aniž by cokoli zkontroloval.)
"""
import glob, os, re, subprocess, sys

R = r"C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_STM32H757"
GCC = glob.glob(r"C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\plugins\*gnu-tools*\tools\bin\arm-none-eabi-gcc.exe")[0]

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
