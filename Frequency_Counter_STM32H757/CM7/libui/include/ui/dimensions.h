#pragma once
/**
 * @file dimensions.h
 * @brief Layout dimensions — PUBLIC API. No magic numbers elsewhere.
 */

#define UI_DIM_SCREEN_W            800
#define UI_DIM_SCREEN_H            480

#define UI_DIM_HEADER_H            56
#define UI_DIM_FOOTER_H            64
#define UI_DIM_BODY_Y              UI_DIM_HEADER_H
#define UI_DIM_BODY_H              (UI_DIM_SCREEN_H - UI_DIM_HEADER_H - UI_DIM_FOOTER_H)

#define UI_DIM_PADDING_X           25
#define UI_DIM_PADDING_Y           13

#define UI_DIM_PILL_RADIUS         21
/* 46 px = 5,4 mm na 4,3" panelu (8,54 px/mm). Bylo 30->36->42, ted 46 — skoro
 * strop bez kolize s headerem (UI_DIM_HEADER_H=56): pill y=(56-46)/2=5,
 * spodek=51, spodni linka headeru je na y=55 -> 4 px rezerva (min. bezpecna,
 * dal uz nejde bez zvetseni headeru). Vyska NEOVLIVNUJE sirku (ta je dana
 * textem) -> zadny dopad na "at pilulky nepretekaji" pozadavek z fontu (viz
 * PILL_GAP/INNER_GAP nize). Interaktivni pilulky (GNSS/SYS) maji navic
 * hit-slop pres celou vysku headeru (pt_in_pill v screen_main.c) -> efektivni
 * cil ~52 px (6,1 mm), nezavisle na teto konstante. */
#define UI_DIM_PILL_H              46
#define UI_DIM_PILL_PAD_X          12
#define UI_DIM_PILL_PAD_Y          6
/* GAP 4, INNER_GAP 5 (2026-07-19, finalni po revizi): label font v pill.c
 * narostl mono_14->mono_16, mezery o 1 px uzsi to kompenzuji. Revize tehoz dne
 * odhalila, ze rozpoctem rady NENI levy okraj hodin (x=674), ale LEVY OKRAJ
 * CLEAR ZON sekundoveho redrawu casu/data (x=648 resp. 644 — viz
 * screen_main_redraw_time; datova zona s pilulkami vyska 46 nove i svisle
 * koliduje) -> docasne vracene prostornejsi 5/6 by v TYPICKEM stavu nechalo
 * radu koncit na 647 a redraw hodin by orezaval HOLD pilulku. Se 4/5 typicka
 * rada konci na 636 <= limit 640 (vsech 6 pilulek se vejde); nejhorsi soubeh
 * ("GNSS FIX"+"SYS ERR") preteka a resi ho fit-check v render_header
 * (HDR_PILL_LIMIT — posledni pilulka se vynecha). Sirky overeny tabulkami
 * fontu (advance soucty), viz UI_SIZES.md. */
#define UI_DIM_PILL_GAP            4
#define UI_DIM_PILL_INNER_GAP      5

#define UI_DIM_CARD_RADIUS         16
#define UI_DIM_CARD_PAD_X          6
#define UI_DIM_CARD_PAD_Y          9

#define UI_DIM_BUTTON_RADIUS       14
#define UI_DIM_BUTTON_H            60
#define UI_DIM_BUTTON_GAP          10
