/* Host test migrace ulozeneho bajtu `g_ui_cfg` (ipc_shared.h, v0.45.14, STATUS #371).
 * Preklad: cc -std=c11 -O2 -Wall -Wextra -ICM7/Core/Inc tests/host/t_uicfg_norm.c -o /tmp/t_uicfg_norm && /tmp/t_uicfg_norm
 * Kryje: (1) v1 (bez bitu 7) -> vychozi hradlo, mode+RUN zachovany; (2) v2 se STAROU tabulkou (bez bitu 6): index 0..2 beze
 * zmeny, 3 (0,5 s) a 4 (1 s) -> 1 s = novy index 3, ostatni bity zachovany; (3) v2 s NOVOU tabulkou se nemeni (jinak by volba
 * 10 s / 100 s zmizela, norm bezi v kazdem kole FpgaTasku); (4) SET_GATE bit 6 zachova a pokryva 0..5; (5) norm je idempotentni. */
#include <stdio.h>
#include <stdint.h>
#include "ipc_shared.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
    /* (1) v1: bit7 = 0, bit0 mode, bit4 RUN, ostatni (stary index hradla) se zahodi */
    for (unsigned c = 0; c < 0x80u; c++) {
        const uint8_t n = ipc_uicfg_norm((uint8_t)c);
        CHECK(n & IPC_UICFG_V2 && n & IPC_UICFG_GT, "v1 0x%02X -> 0x%02X bez znacek", c, n);
        CHECK(IPC_UICFG_GATE(n) == IPC_GATE_DEFAULT, "v1 0x%02X -> hradlo %u", c, IPC_UICFG_GATE(n));
        CHECK((n & 0x11u) == (c & 0x11u), "v1 0x%02X: mode/RUN se ztratily (0x%02X)", c, n);
    }
    /* (2) v2 se starou tabulkou (bit6 = 0): predpis stary index -> novy */
    static const uint8_t MAP[5] = { 0, 1, 2, 3, 3 };
    for (unsigned g = 0; g < 5; g++)
        for (unsigned rest = 0; rest < 0x40u; rest++) {
            uint8_t old = (uint8_t)(IPC_UICFG_V2 | (rest & 0x13u));       /* mode, kanal, RUN */
            old = IPC_UICFG_SET_GATE(old, g);
            const uint8_t n = ipc_uicfg_norm(old);
            CHECK(IPC_UICFG_GATE(n) == MAP[g], "stary index %u -> %u (cekano %u)", g, IPC_UICFG_GATE(n), MAP[g]);
            CHECK(n & IPC_UICFG_GT, "stary index %u: chybi znacka nove tabulky", g);
            CHECK((n & 0x13u) == (old & 0x13u), "stary index %u: mode/kanal/RUN se zmenily", g);
        }
    /* (3) nova tabulka: nesmi se menit, hlavne 10 s (4) a 100 s (5) */
    for (unsigned g = 0; g < IPC_GATE_N; g++) {
        uint8_t c = (uint8_t)(IPC_UICFG_V2 | IPC_UICFG_GT | 0x12u);
        c = IPC_UICFG_SET_GATE(c, g);
        CHECK(ipc_uicfg_norm(c) == c, "nova tabulka, index %u se zmenila (0x%02X -> 0x%02X)", g, c, ipc_uicfg_norm(c));
        CHECK(IPC_UICFG_GATE(c) == g, "SET_GATE/GATE index %u -> %u", g, IPC_UICFG_GATE(c));
        CHECK(c & IPC_UICFG_GT, "SET_GATE zahodil bit 6 (index %u)", g);
    }
    /* (4) idempotence pres vsechny bajty */
    for (unsigned c = 0; c < 256u; c++) {
        const uint8_t a = ipc_uicfg_norm((uint8_t)c);
        CHECK(ipc_uicfg_norm(a) == a, "norm neni idempotentni pro 0x%02X", c);
    }
    /* (5) vychozi hodnota g_ui_cfg (freertos.c) je stabilni a ma index 0,25 s */
    {
        const uint8_t def = 0xD8u;
        CHECK(ipc_uicfg_norm(def) == def, "vychozi 0xD8 se zmenila");
        CHECK(IPC_UICFG_GATE(def) == IPC_GATE_DEFAULT && (def & 0x10u) && (def & IPC_UICFG_V2) && (def & IPC_UICFG_GT),
              "vychozi 0xD8 neodpovida (hradlo/RUN/V2/GT)");
    }
    if (fails) { printf("t_uicfg_norm: %d selhani\n", fails); return 1; }
    printf("t_uicfg_norm: PASS\n");
    return 0;
}
