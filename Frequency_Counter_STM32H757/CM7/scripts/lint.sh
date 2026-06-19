#!/usr/bin/env bash
# lint.sh — enforce the machine-checkable rules from zadani_coding_standards.md.
# Run from the CM7/ directory:  ./scripts/lint.sh
set -u
fail=0

echo "→ Literal colors outside theme.h (libui)"
if grep -RInE '0x[0-9A-Fa-f]{6}' libui/src libui/include \
     --include='*.c' --include='*.h' \
   | grep -v '/fonts/' | grep -v 'theme.h' ; then
    echo "FAIL: literal 6-hex color outside theme.h"; fail=1
fi

echo "→ Raster images in libraries"
if find libprim libui app -name '*.png' -o -name '*.jpg' -o -name '*.bmp' \
   | grep -q . ; then
    echo "FAIL: raster image found in a library/app source tree"; fail=1
fi

echo "→ Cross-layer includes (libprim must not know libui/app)"
if grep -RInE '#include[[:space:]]*[<\"]ui/' libprim/include libprim/src \
   | grep -q . ; then
    echo "FAIL: libprim includes ui/*"; fail=1
fi
if grep -RInE '#include[[:space:]]*[<\"](app|screens)/' libprim libui \
   | grep -q . ; then
    echo "FAIL: a library includes app/screens"; fail=1
fi

echo "→ malloc in render hot path (libprim/libui)"
if grep -RInE '\b(malloc|calloc|realloc|free)\b' \
     libprim/src libui/src --include='*.c' \
   | grep -viE 'path_create|path_destroy' | grep -q . ; then
    echo "WARN: allocation outside path_create/destroy — verify it is not in a render path"
fi

echo "→ Raw colors in screens (must use UI_COLOR_*)"
if grep -RInE '0x[0-9A-Fa-f]{6}' app/screens --include='*.c' | grep -q . ; then
    echo "FAIL: literal color in a screen (use UI_COLOR_*)"; fail=1
fi

if [ "$fail" -eq 0 ]; then echo "OK"; fi
exit $fail
