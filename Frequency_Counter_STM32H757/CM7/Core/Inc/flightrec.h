/**
 * @file    flightrec.h
 * @brief   Flight recorder — kontext systemu TESNE PRED resetem (STATUS.md #18).
 *
 * PROC: crash black-box (BKP_DR3..9) rekne CO se stalo (`stall:UiTask`,
 * `stack:UartTask`, `HF@<pc>`), ale ne CO SE DELO PREDTIM. TODO #18 (nahodne
 * watchdog resety) proto visi od 2026-07-20 se stavem "cekame na dalsi vyskyt" —
 * az nastane, stejne se nedozvime nic navic. Tohle doplnuje chybejici kontext.
 *
 * ── Model ───────────────────────────────────────────────────────────────────
 * Kruhovy buffer poslednich FR_DEPTH sekund drzime v RAM (levne, zadne opotrebeni
 * flash) a do W25Q se sype AZ pri poruse. Dumpuje:
 *   - `watchdog_supervise()` kdyz odmitne obnovit IWDG (= detekovany stall) —
 *     presne scenar #18,
 *   - FreeRTOS hooky pri preteceni zasobniku / selhani malloc,
 *   - UART `flightrec test` (overeni cele cesty bez cekani na skutecnou poruchu).
 *
 * ⚠️ CILOVY SEKTOR JE PREDEM SMAZANY. Pri poruse uz zbyva do IWDG resetu jen
 * ~1,5 s a `w25q_erase_sector` trva 50-400 ms — erase az v tom okamziku by se
 * casto nestihl a dump by prisel vniveč. Maze se proto dopredu, za normalniho
 * behu; pri poruse zbyva jen zapis stranek (jednotky ms).
 *
 * ⚠️ NEZAPISUJE se z HardFault handleru: tam bezi kod s vypnutymi preruseními a
 * `w25q.c wait_ready()` ustupuje scheduleru (`osDelay`) — v takovem kontextu by
 * to zatuhlo. HardFault ma svuj black-box (PC/LR/CFSR) a ten staci.
 */
#ifndef INC_FLIGHTREC_H_
#define INC_FLIGHTREC_H_

#include <stdint.h>
#include <stdbool.h>

#define FR_DEPTH        60u    /* kolik sekund historie (1 vzorek/s) */
#define FR_REC_SIZE     16u    /* bajtu na vzorek (viz flightrec.c) */

/** Inicializace: najde ve flash posledni dump (pro `flightrec_report`) a
 *  pripravi predem smazany cilovy sektor. Volat po `w25q_init` z defaultTask. */
void flightrec_init(void);

/** Vzorkovac — volat z defaultTask; throttle 1 Hz uvnitr. Jen RAM, zadna flash. */
void flightrec_tick(void);

/** Ulozi RAM historii do flash. `reason` = kratky duvod (max 15 znaku), objevi
 *  se v reportu. ⚠️ BLOKUJE (zapis stranek, jednotky ms) — volat jen z kontextu,
 *  kde uz stejne o nic nejde (detekovany stall, hook pred spinem). */
void flightrec_dump(const char *reason);

/** Vypise posledni ulozeny dump na konzoli (UART `flightrec`). @return false =
 *  zadny zaznam. ⚠️ BLOKUJE (cteni flash) -> jen z UartTasku. */
bool flightrec_report(void);

/** Je ve flash nejaky dump? (pro `status` — at nemusi cist celou historii.) */
bool flightrec_have(void);

#endif /* INC_FLIGHTREC_H_ */
