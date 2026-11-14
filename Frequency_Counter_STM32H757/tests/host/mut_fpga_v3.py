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
    ('LSQ: sklon x(t) misto t(x)', 'double sl = (vxx > 0.0) ? vxt / vxx : 0.0;', 'double sl = (vxt > 0.0) ? vxx / vxt : 0.0;'),
    ('teziste bez SX/N', 'c->pX = (double)(x - c->x_first) + (double)r->sx / w;',
     'c->pX = (double)(x - c->x_first);'),
    ('bez kontroly souvislosti segmentu', 'if (r->seg_idx != (uint16_t)(c->seg_last + 1u)) { c->open = 0u; c->clean_ok = 0u; }', ''),
    ('CRC bloku jen pres 253 B', 'return fpga_crc16(blk, 254);', 'return fpga_crc16(blk, 253);'),
    ('znacka bloku se nekontroluje', "|| b[0] != (uint8_t)k || b[1] != (uint8_t)h->seq) break;", ") break;"),
    ('emulator: ST bez casti periody', 'double st = (p / unit_s) * (double)N * nm1 * 0.5;', 'double st = 0.0;'),
    ('prazdny segment = ztrata (drivejsi)', 'if (c->empty > tol) c->open = 0u; else c->k++;', 'c->open = 0u;'),
    ('mezera bez pravidla 3 period', 'if (t3 < (double)tol) tol = (uint32_t)t3;', '(void)t3;'),
    ('mezera bez stropu 2,5 s', 'uint32_t tol = g->max_gap_ms / L;', 'uint32_t tol = 0xFFFFFFFFu; (void)L;'),
    ('okno: bunka okna se nenastavi', 'c->clean_ok = 0u; c->cell = cell;', 'c->clean_ok = 0u;'),
    ('link: prekryv jako mezera', 'else if (d != 0u) {                                                       /* prekryv: duplicity preskocit */',
     'else if (0) {'),
    ('link: bez detekce resetu FPGA', 'else if ((uint32_t)(L->expect - head1) - 1u < 0x7FFFFFFFu) {', 'else if (0) {'),
    ('deje: N = 2 jen prvni hrana', '    out[1].t_u = e->t_u + r->st;\n    return 2u;', '    out[1].t_u = e->t_u + r->st;\n    return 1u;'),
    ('deje: rozbaleni bez casu STM', '        const double  k   = floor((e_u - (double)ds) / 281474976710656.0 + 0.5);',
     '        const double  k   = 0.0 * e_u;'),
    ('deje: bez serazeni v case', 'for (uint32_t j = i; j > 0 && o[j]->t_u < o[j - 1]->t_u; j--)', 'for (uint32_t j = i; j > 0 && 0; j--)'),
    ('deje: stop i pred startem', '} else if (e->ch == 1u && e->t_u > p->t_start) {', '} else if (e->ch == 1u) {'),
    ('deje: souhrn B pres start', 'if (e->ch == 1u && e->kind == FPGA_V3_EV_BURST && e->t_u <= p->t_start &&', 'if (0 &&'),
    ('deje: druha hrana souhrnu bez periody', 'const double d = (double)e->t_u + e->per_u - (double)p->t_start;',
     'const double d = (double)e->t_u - (double)p->t_start;'),
    ('deje: druha hrana souhrnu i pred startem', 'if (d > 0.0 && p->t_start - e->t_u >= FPGA_V3_AB_GUARD_U) {',
     'if (p->t_start - e->t_u >= FPGA_V3_AB_GUARD_U) {'),
    ('deje: druha hrana souhrnu bez ochrany', 'if (d > 0.0 && p->t_start - e->t_u >= FPGA_V3_AB_GUARD_U) {', 'if (d > 0.0) {'),
    ('par: CS bez meze intervalu', 'if (c && (uint64_t)dt_u[i] >= FPGA_V3_AB_CS_LIM_U) {', 'if (0) {'),
    ('deje: cas startu se neulozi', '                if (out < FPGA_V3_AB_TST) p->t_st[out] = p->t_start;\n                dt_u[out++] = (int64_t)(e->t_u - p->t_start);',
     '                dt_u[out++] = (int64_t)(e->t_u - p->t_start);'),
    ('par: CS se nerozpozna', 'const int cs  = !inv && (r->flags & FPGA_V3_R_CS) != 0u;', 'const int cs  = 0;'),
    ('par: CS jen u B', 'cs || (same && p->pa_cs), dt_u + out', 'cs, dt_u + out'),
    ('par: start z CS bez priznaku', 'p->arm_cs = (uint8_t)(p->ab.armed && c);', 'p->arm_cs = 0u;'),
    ('par: zaznamy pri CS se zahodi (drivejsi)', 'if (inv) { p->inv_b++; ab_reset(&p->ab); p->pa_have = 0u; return 0u; }',
     'if (inv || cs) { p->inv_b++; ab_reset(&p->ab); p->pa_have = 0u; return 0u; }'),
    ('par: po prepnuti B bez drzeni', '    if (p->skip) {', '    if (0) {'),
    ('1PPS: d se necte', '        h->pps[i].d = (int8_t)q[11];\n', ''),
    ('1PPS: jemny i bez PCLK', '(h->caps & FPGA_V3_CAP_PPS_IDES) && (h->pps_status & FPGA_V3_PPS_ST_LOCK) && (h->pps[i].pflags & FPGA_V3_PF_FINE)',
     '(h->caps & FPGA_V3_CAP_PPS_IDES) && (h->pps[i].pflags & FPGA_V3_PF_FINE)'),
    ('1PPS: jemny i bez CAPS', '(h->caps & FPGA_V3_CAP_PPS_IDES) && (h->pps_status & FPGA_V3_PPS_ST_LOCK) && (h->pps[i].pflags & FPGA_V3_PF_FINE)',
     '(h->pps_status & FPGA_V3_PPS_ST_LOCK) && (h->pps[i].pflags & FPGA_V3_PF_FINE)'),
    ('1PPS: zlomek z hornich bitu', '*frac  = (uint16_t)(h->pps[i].pts & 0x3FFFu);', '*frac  = (uint16_t)(h->pps[i].pts >> 34);'),
    ('okna: hrany bez P', 'out->edges = (x - c->x_first) << c->p_log2;', 'out->edges = (x - c->x_first);'),
    ('okna: LSQ bez P', 'out->f_lsq_hz = out->lsq_ok ? (double)(1u << c->p_log2) * 1e12', 'out->f_lsq_hz = out->lsq_ok ? 1e12'),
    ('rozsah: bez hystereze', 'f / (double)(1u << (cur - 1u)) < FPGA_V3_RANGE_DOWN_HZ', 'f / (double)(1u << (cur - 1u)) < FPGA_V3_RANGE_UP_HZ'),
    ('emulator: P plati hned bez neplatnych', '        if (e->p_inv[c]) { e->p_inv[c]--; r.flags |= FPGA_V3_R_INVALID; }\n', ''),
    ('PSRAM: DEEP_N 32 b', 'h->deep_n = (uint32_t)rdn(f + 124, 3);', 'h->deep_n = (uint32_t)rdn(f + 124, 4);'),
    ('PSRAM: okno z bajtu 129', 'h->deep_win = (uint16_t)rdn(f + 130, 2);', 'h->deep_win = (uint16_t)rdn(f + 129, 2);'),
    ('PSRAM: bajt 127 se necte', 'h->deep_ack = f[127];', 'h->deep_ack = 0u;'),
    ('rozsah: sonda 0 beze zmeny', 'FPGA_V3_PROBE_HZ(probe)) : 0u;', 'FPGA_V3_PROBE_HZ(probe)) : cur;'),
    ('grp_break nic nedela', '{ g->c[ch & 1u].open = 0u; }', '{ (void)g; (void)ch; }'),
    ('grp_break vzdy kanal A', '{ g->c[ch & 1u].open = 0u; }', '{ g->c[0].open = 0u; (void)ch; }'),
    # v0.40.0: uzka hlavicka (FW 0x0507, CAPS bit22) -- rozbaleni 16 -> 32 b, restart FPGA, historie 1PPS, BUB/OVL
    ('uzka: rozbaleni bez prenosu', 'return prev + (uint32_t)(int32_t)(int16_t)(uint16_t)(v16 - prev);',
     'return (prev & 0xFFFF0000u) | (v16 & 0xFFFFu);'),
    ('uzka: restart FPGA se nepozna', 'if (w->have && (uint32_t)(uint16_t)(s16 - w->seq) > FPGA_V3_WRAP_SEQ_MAX) {', 'if (0) {'),
    ('uzka: REC_HEAD = pocet (bez -1)', 'h->rec_head = w->head ? w->head - 1u : 0u;', 'h->rec_head = w->head;'),
    ('uzka: PSEQ nerozbalen', 'w->pseq = ext16(w->pseq, h->pps[0].pseq);', 'w->pseq = h->pps[0].pseq & 0xFFFFu;'),
    ('uzka: 1PPS predchozi bez kontroly pseq-1', '(h->pps[0].pseq == w->pps_last.pseq + 1u) ? w->pps_last : (fpga_v3_pps_t){ 0 }',
     'w->pps_last'),
    ('uzka: 1PPS predchozi z bajtu 84..95', 'if (w->pps_have && h->pps[0].pseq == w->pps_last.pseq) h->pps[1] = w->pps_prev;', 'if (0) {}'),
    ('uzka: BUB z hlavicky', "h->bub[c] = (rec[j].flags & FPGA_V3_R_BUBBLE) ? 1u : 0u;", "h->bub[c] = f[112u + c];"),
    ('uzka: OVL ze vsech zaznamu (OR)', "h->ovl[c] = (rec[j].flags & FPGA_V3_R_CS) ? 1u : 0u;",
     "h->ovl[c] |= (rec[j].flags & FPGA_V3_R_CS) ? 1u : 0u;"),
    ('uzka: emulator ACK 32 b', 'a = e->rec_ack + (uint16_t)((uint16_t)a - (uint16_t)e->rec_ack);', '(void)0;'),
    ('uzka: SEQ 32 b i v uzke', 'h->seq = (uint32_t)rdn(f + 4, wn);', 'h->seq = (uint32_t)rdn(f + 4, 4);'),
    # v0.42.5 (L-0247): kontrola teziste segmentu a pojistka okna
    ('teziste: bez ST', '*r_ps   = ((double)a->st - per * (double)a->sx)', '*r_ps   = (0.0 * (double)a->st - 0.0 * per * (double)a->sx)'),
    ('okno: seg_bad se nenastavi', '{ c->seg_bad = 1u; pbad = 1; }', '{ pbad = 1; }'),
    ('okno: seg_bad se nenuluje', 'c->seg_bad = 0u; c->seg_max_ps = 0.0;', 'c->seg_max_ps = 0.0;'),
    # v0.42.6 (L-0247): presne meze SX
    ('SX: bez dolni meze', 'return a->sx >= lo && a->sx <= hi;', 'return a->sx <= hi;'),
    ('SX: bez horni meze', 'return a->sx >= lo && a->sx <= hi;', 'return a->sx >= lo;'),
    ('okno: bez kontroly mezi SX', 'if (fabs(rp) > FPGA_V3_SEG_BAD_PS || !fpga_v3_seg_sx_ok(&c->prev, r)) {', 'if (fabs(rp) > FPGA_V3_SEG_BAD_PS) {'),
    # v0.42.7 (L-0247): X0 o 64*k hran
    ('X0: bez opravy', 'rc = *r; rc.x0 = (r->x0 - (uint64_t)dv) & MASK40; r = &rc;', 'rc = *r; (void)rc;'),
    ('X0: oprava i jine odchylky', 'k != 0.0 && fabs(d - 64.0 * k) < 0.1;', 'k != 0.0;'),
    ('X0: bez podminky ciste kontroly', 'int fix = c->clean && k', 'int fix = k'),
    ('X0: rozbaleni bez opravy', 'dx = (uint64_t)((int64_t)dx - dv) & MASK40;', 'dx = dx;'),
    ('LSQ: vadny segment se nevyradi', 'if (c->open && !pbad) {', 'if (c->open) {'),
    ('LSQ: segment se do souctu neprida', 'c->swxt += w * X * T; c->kl++;', 'c->kl++;'),
    # v0.42.11 (L-0247): oprava SX (vynulovane bity 6, 7, 32, 33 indexu) a mez 800 ps
    ('SX: bez opravy', 'const int how = sx_repair(&c->prev, r, c->prev_dv, clk_mhz, &rp);', 'const int how = 0; (void)sx0;'),
    ('SX: bez opravy z mezi', 'if (sx_lo32_fit(n, dx, a->sx, &cand[nc])) kind[nc++] = 1;', ''),
    ('SX: bez opravy podle X0', 'if (dv != 0 && sx_lo32_fit(', 'if (0 && sx_lo32_fit('),
    ('SX: oprava podle X0 opacne', '(a->sx + (uint64_t)((int64_t)(n - 1u) * dv)) & MASK48', '(a->sx - (uint64_t)((int64_t)(n - 1u) * dv)) & MASK48'),
    ('SX: kandidat bez kontroly teziste', 'if (fpga_v3_seg_resid(&t, b, clk_mhz, &rp, &sd) && fabs(rp) <= FPGA_V3_SEG_BAD_PS) {',
     'if (fpga_v3_seg_resid(&t, b, clk_mhz, &rp, &sd)) {'),
    ('SX: pX se neupravi', 'if (c->pend) c->pX += dsx / c->pw;', '(void)0;'),
    ('SX: xc0 se neupravi', 'if (c->k == 1u) c->xc0 += dsx / (double)c->prev.n;', '(void)0;'),
    ('SX: rozdil mod 2^48', 'const double dsx = (double)c->prev.sx - (double)sx0;',
     'const double dsx = (double)((c->prev.sx - sx0) & MASK48);'),
    ('SX: dv se neuklada', 'c->prev_ok = 1u; c->prev_dv = dv_now;', 'c->prev_ok = 1u; c->prev_dv = 0;'),
    ('mez segmentu 2 ns', 'if (fabs(rp) > FPGA_V3_SEG_BAD_PS || !fpga_v3_seg_sx_ok(&c->prev, r)) {',
     'if (fabs(rp) > 2000.0 || !fpga_v3_seg_sx_ok(&c->prev, r)) {'),
    # v0.42.12: okna na spolecne mrizce a parovani A/B
    ('mrizka: okno konci po K segmentech', 'if (c->open && cell != c->cell) {', 'if (c->open && c->k >= g->k_target) {'),
    ('mrizka: okno zacina kdekoli', 'if (!closed && pos != 0u && !(c->clean_ok && c->clean_cell == cell)) { c->clean_ok = 0u; return 0; }', ''),
    ('mrizka: bunka bez hrany se nepamatuje', 'if (pos == 0u) { c->clean_ok = 1u; c->clean_cell = cell; }', 'if (0) {}'),
    ('mrizka: cislo segmentu bez rozbaleni', 'else g->seg_u += (uint16_t)(r->seg_idx - g->seg_raw);', 'else g->seg_u = r->seg_idx;'),
    ('par: bez cekani na B', 'else if (cs->open && cs->cell == w->cell) { p->a = *w; p->a_ok = 1u; }', ''),
    ('par: B bez kontroly bunky', 'if (p->b_ok && p->b.cell == w->cell) { pair_out(&out[n++], w, &p->b); p->b_ok = 0u; }',
     'if (p->b_ok) { pair_out(&out[n++], w, &p->b); p->b_ok = 0u; }'),
    ('par: cekajici bez kontroly bunky', 'if (p->b_ok && p->b.cell == p->a.cell) {', 'if (p->b_ok) {'),
    ('par: cekajici se neodesle', 'else if (!(cs->open && cs->cell == p->a.cell)) { pair_late(p, cs, &p->a, &out[n++]); p->a_ok = 0u; }', ''),
    ('par: pomaly B se nepouzije', 'const int slow = cs->open && (int32_t)(cs->cell - a->cell) <= 0 && p->b_ok;', 'const int slow = 0; (void)cs;'),
    ('par: pomaly B jen z drivejsi bunky', 'const int slow = cs->open && (int32_t)(cs->cell - a->cell) <= 0 && p->b_ok;',
     'const int slow = cs->open && (int32_t)(cs->cell - a->cell) < 0 && p->b_ok;'),
    ('par: okno bez bunky', 'out->seg_bad = c->seg_bad; out->seg_max_ps = c->seg_max_ps; out->cell = c->cell;',
     'out->seg_bad = c->seg_bad; out->seg_max_ps = c->seg_max_ps; out->cell = 0u;'),
    # v0.43.0 (FW 0x0509, krok 1): kalibrace TDC ze vstupniho pinu
    ('calpin: bez meze osmin', 'if (worst > 100 || worst < -100) return 2;', 'if (0) return 2;'),
    ('calpin: mez kodu bez nz_ref', 'const uint32_t nz_min = nz_ref ? ((uint32_t)nz_ref * 9u + 9u) / 10u : 64u;',
     'const uint32_t nz_min = 64u; (void)nz_ref;'),
    ('calpin: bin cely do prvni osminy', 'if (hi > lo) occ[k] += h * (hi - lo) / (c1 - c0);', 'if (hi > lo) { occ[k] += h; break; }'),
    ('calpin: osminy bez reference', 'const double c1 = (rtot > 0.0) ? c0 + (double)ref_h[a] * 8.0 / rtot : c0;',
     'const double c1 = (rtot > 0.0) ? c0 + 8.0 / (double)nc : c0;'),
    ('calpin: prazdny = pouzitelny', 'if (tot <= 0.0) return 3;', 'if (tot <= 0.0) return 0;'),
    ('pokryti: posun faze = f / 100 MHz', 'const double al = r - floor(r);', 'const double al = 1.0 / r;'),
    ('pokryti: zalomeni faze na 0', 'if (ph >= 1.0) ph -= 1.0;', 'if (ph >= 1.0) ph = 0.0;'),
    # v0.44.4 (STATUS #360): clen parity segmentu v LSQ okna
    ('LSQ parita: sklon bez korekce', 'sl = (vxt * vpp - vpt * vxp) / det;', 'sl = vxt / vxx;'),
    ('LSQ parita: parita se nenastavi', 'c->pP = (r->seg_idx & 1u) ? -1.0 : 1.0;', 'c->pP = 1.0;'),
    ('LSQ parita: soucty se nenuluji', 'c->swp = c->swpx = c->swpt = 0.0;', '(void)0;'),
    ('LSQ parita: soucty se nepricitaji', 'c->swp += w * P; c->swpx += w * P * X; c->swpt += w * P * T;', '(void)P;'),
    ('LSQ parita: vychozi vypnuto', 'g->par_en = 1u;', 'g->par_en = 0u;'),
    ('LSQ parita: i pri vypnuti', 'if (g->par_en && c->kl >= FPGA_V3_PAR_MIN_SEG && vxx > 0.0) {',
     'if (c->kl >= FPGA_V3_PAR_MIN_SEG && vxx > 0.0) {'),
    ('LSQ parita: c opacne', 'out->par_u = (vpt * vxx - vxt * vxp) / det;', 'out->par_u = -(vpt * vxx - vxt * vxp) / det;'),
    ('LSQ parita: od 3 segmentu', 'c->kl >= FPGA_V3_PAR_MIN_SEG && vxx > 0.0', 'c->kl >= 3u && vxx > 0.0'),
    ('LSQ parita: bez meze zavislosti x a p', 'if (det > 0.5 * vxx * vpp) {', 'if (det > 0.0) {'),
    ('emulator: parita bez znamenka', '((e->seg_idx & 1u) ? -e->par_ps[c] : e->par_ps[c])', 'e->par_ps[c]'),
    ('lin: perioda bez 2^rexp', 'return d * (double)(1u << rexp) * 64.0 / (11.0 * (double)p3);', 'return d * 64.0 / (11.0 * (double)p3);'),
    ('lin: perioda 16384/2816 zaokrouhleno', 'return d * (double)(1u << rexp) * 64.0 / (11.0 * (double)p3);',
     'return d * (double)(1u << rexp) * 5.818 / (double)p3;'),
    ('lin: bez odstraneni driftu', 'm[k] = (L->s[k] - beta * L->sd[k]) / nk;', 'm[k] = L->s[k] / nk;'),
    ('lin: dil z faze x misto t', 'const uint32_t k = (uint32_t)(((t_u & 0x3FFFu) * FPGA_V3_LIN_NB) >> 14);',
     'const uint32_t k = (uint32_t)(((x & 0x3FFFu) * FPGA_V3_LIN_NB) >> 14);'),
    ('lin: tabulka bez odecteni prumeru', 'double v = m[k] - mu;', 'double v = m[k];'),
    ('lin: interpolace bez posunu na stred', 'const int32_t p = (int32_t)(t_u & 0x3FFFu) - (int32_t)(w / 2u);',
     'const int32_t p = (int32_t)(t_u & 0x3FFFu);'),
    ('lin: pod stredem dilu 0 drzet misto extrapolace', 'const int32_t f = p - i0 * (int32_t)w;',
     'const int32_t f = (p < 0) ? 0 : p - i0 * (int32_t)w;'),
    ('lin: kruhove pres hranici periody (pred v0.45.9)', 'const int32_t a = tab[i0], b = tab[i0 + 1];',
     'const int32_t a = (p < 0) ? tab[FPGA_V3_LIN_NB - 1] : tab[i0], b = (p < 0) ? tab[0] : tab[i0 + 1];'),
    ('lin: bez kontroly nejmene v dilu', 'if (L->N < 2u || o.nmin < nmin_req || o.nmin == 0u)', 'if (L->N < 2u || o.nmin == 0u)'),
]
# prekladac: CC (napr. "zig cc"), jinak cc; na Windows spustitelny soubor s .exe
cc = os.environ.get('CC', 'cc').split() + ['-std=c11', '-O2', '-ICM7/Core/Inc', None, 'CM7/Core/Src/fpga_crc.c',
      'CM7/Core/Src/fpga_tdccal.c', 'tests/host/t_fpga_v3.c', '-lm', '-o', os.path.join(out, 't_mut' + ('.exe' if os.name == 'nt' else ''))]
isrc = cc.index(None)
bad = 0
os.makedirs(out, exist_ok=True)
for name, a, b in M:
    if src.count(a) != 1:
        print('%-40s KOTVA NENALEZENA' % name); bad += 1; continue
    p = os.path.join(out, 'fpga_v3_mut.c')
    open(p, 'w', encoding='utf-8').write(src.replace(a, b))
    cc[isrc] = p
    r = subprocess.run(cc, capture_output=True, text=True)
    if r.returncode:
        print('%-40s PREKLAD SELHAL' % name); bad += 1; continue
    o = subprocess.run([cc[-1]], capture_output=True, text=True, timeout=120)
    caught = o.returncode != 0
    first = next((l.strip() for l in o.stdout.splitlines() if 'CHYBA' in l or 'FAIL' in l), '')
    print('%-40s %s  %s' % (name, 'ZACHYCENO' if caught else '!!! NEZACHYCENO', first[:70]))
    bad += 0 if caught else 1
print('\nvsechny mutace zachyceny' if bad == 0 else '\n%d mutaci NEZACHYCENO' % bad)
sys.exit(1 if bad else 0)
