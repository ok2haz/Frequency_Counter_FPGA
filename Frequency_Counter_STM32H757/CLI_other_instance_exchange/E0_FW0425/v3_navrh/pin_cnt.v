// ============================================================
// pin_cnt.v -- v3 NAVRH (audit 2026-10-20, etapa E5), NEZAPOJENO, NEOVERENO SIMULACI ANI P&R.
// Pocitani KAZDE periody vstupu uvnitr FPGA: 4b citac taktovany PRIMO pinem (lokalni vedeni, NE globalni sit).
// Do retezu TDC jde vybrany vystup: P = 1 (primo), 2, 4, 8, 16. Volbu dela STM AUTOMATICKY (fpga_range.c) z rozsahove sondy:
// uzivatel kmitocet nenastavuje. Pocet period = P x pocet delenych hran (P mocnina 2 -> posun). Presnost znacky se nemeni
// (pridany jitter 1 KO ~ jednotky ps), regresi omezuje strop TDC (~20 Mznacek/s), ne delicka.
// Rozsahova sonda: vystup /16 se pocita v domene 100 MHz VZDY (i v primem rezimu) -> STM pozna kmitocet az do 16 x ~40 MHz.
// Strop: vstupni buffer LVCMOS33 + prvni KO; Gowin uvadi Fmax LVCMOS33 150/300 MHz (vystup, 30 pF). 400 MHz pravdepodobne
// jen diferencialnim vstupem (H2) nebo delickou na vstupni desce -- viz TODO.
// 🔴 SDC: create_clock na pinu (napr. -period 2.5), set_clock_groups -asynchronous vuci clk_p0_100m; zakazat promoci na GCLK [?].
// ============================================================
module pin_cnt (
    input  wire       pin,        // vstup kanalu (asynchronni)
    input  wire       clk,        // clk_p0_100m
    input  wire [2:0] sel,        // log2 P: 0 = primo, 1..4 = /2../16 (kvazistaticke; meni se jen v hold, STM okno zahodi)
    output wire       sig_out,    // do tdc_chan.sig_raw a do kontrolniho pocitani
    input  wire       seg_tick,   // 1 takt: hranice segmentu (zachyceni sondy)
    output reg [19:0] probe       // hrany /16 za posledni segment (L = 25 ms: f_probe = 16 * probe / L, do 640 MHz)
);
    reg [3:0] c = 4'd0;
    always @(posedge pin) c <= c + 4'd1;          // jediny stupen na plnem kmitoctu: c[0] (/2)
    assign sig_out = (sel == 3'd0) ? pin :
                     (sel == 3'd1) ? c[0] :
                     (sel == 3'd2) ? c[1] :
                     (sel == 3'd3) ? c[2] : c[3];
    // sonda: c[3] (/16) synchronizovat 3 FF, pocitat nabezne hrany
    reg [2:0] s = 3'b000;
    reg [19:0] pc = 20'd0;
    always @(posedge clk) begin
        s <= {s[1:0], c[3]};
        if (seg_tick) begin probe <= pc + {19'd0, (s[1] & ~s[2])}; pc <= 20'd0; end
        else if (s[1] & ~s[2]) pc <= pc + 20'd1;
    end
    initial probe = 20'd0;
endmodule
