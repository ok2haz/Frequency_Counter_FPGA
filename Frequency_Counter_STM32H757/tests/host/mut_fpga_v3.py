#!/usr/bin/env python3
"""Negativni kontroly host testu t_fpga_v3: kazda mutace fpga_v3.c (docasna kopie, repo se nemeni) musi test shodit.
Pouziti (z adresare Frequency_Counter_STM32H757): python3 tests/host/mut_fpga_v3.py <vystupni adresar>"""
import os, subprocess, sys

out = sys.argv[1]
src = open('CM7/Core/Src/fpga_v3.c', encoding='utf-8').read()
M = [
    ('rozbaleni T0 jen 40 b', 't = c->t_prev + ((r->t0 - c->t_prev_raw) & MASK48);',
     't = c->t_prev + ((r->t0 - c->t_prev_raw) & MASK40);'),
    ('zaznamy i z vadneho bloku', "if (!((h->blk_ok >> k) & 1u) || b[0] != (uint8_t)k", "if (b[0] != (uint8_t)k"),
    ('ACK i za hlavu kruhu', 'if ((uint32_t)(a - e->rec_ack) <= (uint32_t)(e->rec_head - e->rec_ack)) {', 'if (1) {'),
    ('LSQ: sklon x(t) misto t(x)', 'out->f_lsq_hz = out->lsq_ok ? 1e12 / ((vxt / vxx) * up) : 0.0;',
     'out->f_lsq_hz = out->lsq_ok ? 1e12 / ((vxx / vxt) * up) : 0.0;'),
    ('teziste bez SX/N', 'const double X = (double)(x - c->x_first) + (double)r->sx / w;',
     'const double X = (double)(x - c->x_first);'),
    ('bez kontroly souvislosti segmentu', 'if (c->open && r->seg_idx != (uint16_t)(c->seg_last + 1u)) c->open = 0u;', ''),
    ('CRC bloku jen pres 253 B', 'return fpga_crc16(blk, 254);', 'return fpga_crc16(blk, 253);'),
    ('znacka bloku se nekontroluje', "|| b[0] != (uint8_t)k || b[1] != (uint8_t)h->seq) break;", ") break;"),
    ('emulator: ST bez casti periody', 'double st = (p / unit_s) * (double)N * nm1 * 0.5;', 'double st = 0.0;'),
    ('prazdny segment = ztrata (drivejsi)', 'if (c->empty > tol) c->open = 0u; else c->k++;', 'c->open = 0u;'),
    ('mezera bez pravidla 3 period', 'if (t3 < (double)tol) tol = (uint32_t)t3;', '(void)t3;'),
    ('mezera bez stropu 2,5 s', 'uint32_t tol = g->max_gap_ms / L;', 'uint32_t tol = 0xFFFFFFFFu; (void)L;'),
    ('okno konci jen pri k == K', 'if (c->open && c->k >= g->k_target) {', 'if (c->open && c->k == g->k_target) {'),
    ('link: prekryv jako mezera', 'else if (d != 0u) {                                                       /* prekryv: duplicity preskocit */',
     'else if (0) {'),
    ('link: bez detekce resetu FPGA', 'else if ((uint32_t)(L->expect - head1) - 1u < 0x7FFFFFFFu) {', 'else if (0) {'),
    ('deje: N = 2 jen prvni hrana', '    out[1].t_u = e->t_u + r->st;\n    return 2u;', '    out[1].t_u = e->t_u + r->st;\n    return 1u;'),
    ('deje: rozbaleni bez casu STM', '        const double  k   = floor((e_u - (double)ds) / 281474976710656.0 + 0.5);',
     '        const double  k   = 0.0 * e_u;'),
    ('deje: bez serazeni v case', 'for (uint32_t j = i; j > 0 && o[j]->t_u < o[j - 1]->t_u; j--)', 'for (uint32_t j = i; j > 0 && 0; j--)'),
    ('deje: stop i pred startem', '} else if (e->ch == 1u && e->t_u > p->t_start) {', '} else if (e->ch == 1u) {'),
    ('deje: souhrn B pres start', 'if (e->ch == 1u && e->kind == FPGA_V3_EV_BURST && e->t_u <= p->t_start &&', 'if (0 &&'),
    ('par: znacky pri CS se pouziji', 'const int bad = (r->flags & (FPGA_V3_R_INVALID | FPGA_V3_R_CS)) != 0u;',
     'const int bad = (r->flags & FPGA_V3_R_INVALID) != 0u;'),
    ('par: po prepnuti B bez drzeni', '    if (p->skip) {', '    if (0) {'),
    ('1PPS: d se necte', '        h->pps[i].d = (int8_t)q[11];\n', ''),
    ('1PPS: jemny i bez PCLK', '(h->caps & FPGA_V3_CAP_PPS_IDES) && (h->pps_status & FPGA_V3_PPS_ST_LOCK) && (h->pps[i].pflags & FPGA_V3_PF_FINE)',
     '(h->caps & FPGA_V3_CAP_PPS_IDES) && (h->pps[i].pflags & FPGA_V3_PF_FINE)'),
    ('1PPS: jemny i bez CAPS', '(h->caps & FPGA_V3_CAP_PPS_IDES) && (h->pps_status & FPGA_V3_PPS_ST_LOCK) && (h->pps[i].pflags & FPGA_V3_PF_FINE)',
     '(h->pps_status & FPGA_V3_PPS_ST_LOCK) && (h->pps[i].pflags & FPGA_V3_PF_FINE)'),
    ('1PPS: zlomek z hornich bitu', '*frac  = (uint16_t)(h->pps[i].pts & 0x3FFFu);', '*frac  = (uint16_t)(h->pps[i].pts >> 34);'),
    ('grp_break nic nedela', '{ g->c[ch & 1u].open = 0u; }', '{ (void)g; (void)ch; }'),
    ('grp_break vzdy kanal A', '{ g->c[ch & 1u].open = 0u; }', '{ g->c[0].open = 0u; (void)ch; }'),
]
cc = ['cc', '-std=c11', '-O2', '-ICM7/Core/Inc', None, 'CM7/Core/Src/fpga_crc.c', 'CM7/Core/Src/fpga_tdccal.c',
      'tests/host/t_fpga_v3.c', '-lm', '-o', os.path.join(out, 't_mut')]
bad = 0
for name, a, b in M:
    if src.count(a) != 1:
        print('%-40s KOTVA NENALEZENA' % name); bad += 1; continue
    p = os.path.join(out, 'fpga_v3_mut.c')
    open(p, 'w', encoding='utf-8').write(src.replace(a, b))
    cc[4] = p
    r = subprocess.run(cc, capture_output=True, text=True)
    if r.returncode:
        print('%-40s PREKLAD SELHAL' % name); bad += 1; continue
    o = subprocess.run([os.path.join(out, 't_mut')], capture_output=True, text=True, timeout=120)
    caught = o.returncode != 0
    first = next((l.strip() for l in o.stdout.splitlines() if 'CHYBA' in l or 'FAIL' in l), '')
    print('%-40s %s  %s' % (name, 'ZACHYCENO' if caught else '!!! NEZACHYCENO', first[:70]))
    bad += 0 if caught else 1
print('\nvsechny mutace zachyceny' if bad == 0 else '\n%d mutaci NEZACHYCENO' % bad)
sys.exit(1 if bad else 0)
