/*
 * gen_fonts.js — generate native libprim font tables (bpp4) from TTF.
 *
 * Rasterizes each glyph with opentype.js using 4x supersampling and a nonzero
 * scanline fill, then quantizes coverage to 4 bits and packs MSB-first, row-
 * major, continuous (matching prim text.c glyph_cov / font_data.h). Emits one
 * C file per font into ../../src/fonts/.
 *
 * Run from this directory:
 *   <repo>\tools\node-v20.18.1-win-x64\node.exe gen_fonts.js
 */
'use strict';

const path = require('path');
const fs = require('fs');

// opentype.js bundled inside lv_font_conv's node_modules.
const REPO = path.resolve(__dirname, '..', '..', '..', '..');
const opentype = require(path.join(
    REPO, 'tools', 'node-v20.18.1-win-x64', 'node_modules',
    'lv_font_conv', 'node_modules', 'opentype.js'));

const TTF_DIR = path.join(REPO, 'tools', 'ttf');
const OUT_DIR = path.resolve(__dirname, '..', '..', 'src', 'fonts');

const MONO = 'JetBrainsMono-Medium.ttf';
const SANS = 'Inter-Regular.ttf';
const SS = 4;          // supersample factor
const BPP = 4;

// 13 descriptors required by libui/include/ui/fonts.h.
const FONTS = [
    ['ui_font_mono_14', MONO, 14], ['ui_font_mono_16', MONO, 16],
    ['ui_font_mono_18', MONO, 18], ['ui_font_mono_20', MONO, 20],
    ['ui_font_mono_25', MONO, 25], ['ui_font_mono_30', MONO, 30],
    // 75px (and the 52px fade for invalid digits) are used only for the main
    // number's digits → restrict to 0-9 to save flash.
    ['ui_font_mono_75', MONO, 75, '0123456789'],
    ['ui_font_mono_52', MONO, 52, '0123456789'],
    ['ui_font_sans_14', SANS, 14], ['ui_font_sans_16', SANS, 16],
    ['ui_font_sans_17', SANS, 17], ['ui_font_sans_20', SANS, 20],
    ['ui_font_sans_32', SANS, 32],
];

// Unicode subset (libprim text.h §9).
function codepoints() {
    const set = new Set();
    const range = (a, b) => { for (let c = a; c <= b; c++) set.add(c); };
    range(0x20, 0x7E);          // ASCII
    range(0xA0, 0x17F);         // Latin-1 + Latin Ext-A (czech, ·±×°)
    range(0x0391, 0x03C9);      // Greek (Δ Ω α β π σ τ μ …)
    range(0x2070, 0x209F);      // super/subscripts
    range(0x2190, 0x2199);      // arrows ← ↑ → ↓ …
    [0x2022, 0x2261, 0x25B6, 0x25B7, 0x25CF].forEach((c) => set.add(c));
    return [...set].sort((a, b) => a - b);
}

// Flatten path commands into contours (arrays of {x,y}) in pixel space.
function flatten(commands) {
    const contours = [];
    let cur = null, sx = 0, sy = 0, cx = 0, cy = 0;
    const quad = (x1, y1, x, y, steps) => {
        for (let i = 1; i <= steps; i++) {
            const t = i / steps, u = 1 - t;
            cur.push({
                x: u * u * cx + 2 * u * t * x1 + t * t * x,
                y: u * u * cy + 2 * u * t * y1 + t * t * y,
            });
        }
    };
    const cubic = (x1, y1, x2, y2, x, y, steps) => {
        for (let i = 1; i <= steps; i++) {
            const t = i / steps, u = 1 - t;
            cur.push({
                x: u*u*u*cx + 3*u*u*t*x1 + 3*u*t*t*x2 + t*t*t*x,
                y: u*u*u*cy + 3*u*u*t*y1 + 3*u*t*t*y2 + t*t*t*y,
            });
        }
    };
    for (const c of commands) {
        if (c.type === 'M') { cur = [{ x: c.x, y: c.y }]; contours.push(cur);
                              sx = cx = c.x; sy = cy = c.y; }
        else if (c.type === 'L') { cur.push({ x: c.x, y: c.y }); cx = c.x; cy = c.y; }
        else if (c.type === 'Q') { quad(c.x1, c.y1, c.x, c.y, 8); cx = c.x; cy = c.y; }
        else if (c.type === 'C') { cubic(c.x1, c.y1, c.x2, c.y2, c.x, c.y, 12);
                                   cx = c.x; cy = c.y; }
        else if (c.type === 'Z') { if (cur) { cur.push({ x: sx, y: sy }); }
                                   cx = sx; cy = sy; }
    }
    return contours;
}

// Nonzero scanline fill into a coverage grid (w x h, values 0..SS*SS).
function rasterize(contours, x0, y0, w, h) {
    const cov = new Uint16Array(w * h);
    const edges = [];
    for (const ct of contours) {
        for (let i = 0; i + 1 < ct.length; i++) {
            let a = ct[i], b = ct[i + 1];
            if (a.y === b.y) continue;
            const dir = a.y < b.y ? 1 : -1;
            if (a.y > b.y) { const t = a; a = b; b = t; }
            edges.push({ y0: a.y, y1: b.y, x: a.x, dxdy: (b.x - a.x) / (b.y - a.y), dir });
        }
    }
    const subH = h * SS, subW = w * SS;
    for (let sy = 0; sy < subH; sy++) {
        const yPix = y0 + (sy + 0.5) / SS;       // sample center in glyph space
        const xs = [];
        for (const e of edges) {
            if (yPix >= e.y0 && yPix < e.y1) {
                xs.push({ x: (e.x + (yPix - e.y0) * e.dxdy - x0) * SS, dir: e.dir });
            }
        }
        if (xs.length < 2) continue;
        xs.sort((p, q) => p.x - q.x);
        let wind = 0;
        for (let k = 0; k + 1 < xs.length; k++) {
            wind += xs[k].dir;
            if (wind === 0) continue;
            let xa = xs[k].x, xb = xs[k + 1].x;
            let start = Math.ceil(xa - 0.5), end = Math.ceil(xb - 0.5);
            if (start < 0) start = 0;
            if (end > subW) end = subW;
            const py = (sy / SS) | 0;
            for (let sx = start; sx < end; sx++) cov[py * w + ((sx / SS) | 0)]++;
        }
    }
    return cov;
}

// Fallback TTFs tried (in order) when the primary font lacks a glyph — e.g.
// JetBrains Mono has no superscript-minus (U+207B), Inter has no ≡ (U+2261).
const FALLBACK_TTF = [MONO, SANS];

function buildFont([name, ttf, size, subset]) {
    const font = opentype.loadSync(path.join(TTF_DIR, ttf));
    const fallbacks = FALLBACK_TTF.filter((t) => t !== ttf)
        .map((t) => opentype.loadSync(path.join(TTF_DIR, t)));
    const chain = [font, ...fallbacks];
    const scale = size / font.unitsPerEm;
    const ascent = Math.round(font.ascender * scale);     // metrics from primary
    const descent = Math.round(-font.descender * scale);
    const lineHeight = ascent + descent;

    const cps = subset
        ? [...new Set([...subset].map((ch) => ch.codePointAt(0)))].sort((a, b) => a - b)
        : codepoints();
    const bytes = [];
    const glyphs = [];
    for (const cp of cps) {
        // Resolve the glyph from the primary font, else a fallback TTF.
        let src = null, g = null;
        for (const cand of chain) {
            const cg = cand.charToGlyph(String.fromCodePoint(cp));
            if (cg && (cg.index !== 0 || cp === 0x20)) { src = cand; g = cg; break; }
        }
        if (!g) continue;
        const gScale = size / src.unitsPerEm;
        const adv = Math.round((g.advanceWidth || 0) * gScale);
        const gp = g.getPath(0, 0, size);
        const bb = gp.getBoundingBox();
        let w = 0, h = 0, ox = 0, oy = 0, off = bytes.length;
        const hasInk = isFinite(bb.x1) && (bb.x2 > bb.x1) && (bb.y2 > bb.y1);
        if (hasInk) {
            const gx0 = Math.floor(bb.x1), gy0 = Math.floor(bb.y1);
            const gx1 = Math.ceil(bb.x2), gy1 = Math.ceil(bb.y2);
            w = gx1 - gx0; h = gy1 - gy0; ox = gx0; oy = -gy0;
            const cov = rasterize(flatten(gp.commands), gx0, gy0, w, h);
            const maxv = SS * SS;
            // pack bpp4, MSB-first, continuous; pad glyph to byte boundary.
            let nib = 0, acc = 0;
            for (let i = 0; i < w * h; i++) {
                const v = Math.round(cov[i] * 15 / maxv);
                if (nib === 0) { acc = v << 4; nib = 1; }
                else { bytes.push(acc | v); nib = 0; }
            }
            if (nib === 1) bytes.push(acc);
        }
        glyphs.push({ cp, off, w, h, ox, oy, adv });
    }
    return { name, glyphs, bytes, lineHeight, ascent, descent };
}

function emit(f) {
    const sym = f.name;
    let s = `/* Auto-generated by tools/font_gen/gen_fonts.js. Do not edit. */\n`;
    s += `#include <prim/text.h>\n`;
    s += `#include <ui/fonts.h>   /* UI_API extern decl -> correct visibility */\n\n`;
    s += `static const uint8_t ${sym}_bmp[] = {\n`;
    for (let i = 0; i < f.bytes.length; i += 16) {
        s += '    ' + f.bytes.slice(i, i + 16).map((b) => '0x' + b.toString(16).padStart(2, '0')).join(', ') + ',\n';
    }
    if (f.bytes.length === 0) s += '    0x00\n';
    s += `};\n\n`;
    s += `static const prim_glyph_t ${sym}_glyphs[] = {\n`;
    for (const g of f.glyphs) {
        s += `    {${g.cp}, ${g.off}, ${g.w}, ${g.h}, ${g.ox}, ${g.oy}, ${g.adv}},\n`;
    }
    s += `};\n\n`;
    s += `const prim_font_t ${sym} = {\n`;
    s += `    .bitmap_data = ${sym}_bmp,\n`;
    s += `    .glyphs = ${sym}_glyphs,\n`;
    s += `    .glyph_count = ${f.glyphs.length},\n`;
    s += `    .line_height = ${f.lineHeight},\n`;
    s += `    .baseline = ${f.ascent},\n`;
    s += `    .ascent = ${f.ascent},\n`;
    s += `    .descent = ${f.descent},\n`;
    s += `    .bpp = ${BPP},\n`;
    s += `};\n`;
    return s;
}

fs.mkdirSync(OUT_DIR, { recursive: true });
for (const spec of FONTS) {
    const f = buildFont(spec);
    fs.writeFileSync(path.join(OUT_DIR, spec[0] + '.c'), emit(f));
    console.log(`${spec[0]}: ${f.glyphs.length} glyphs, ${f.bytes.length} bytes`);
}
console.log('done.');
