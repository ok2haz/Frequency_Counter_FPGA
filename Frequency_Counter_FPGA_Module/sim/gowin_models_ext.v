// ============================================================
// gowin_models_ext.v -- 2026-10-25: simulacni modely primitiv Gowin GW1N, ktere gowin_models.v nema (ten se nemeni).
// Deterministicke (bez $urandom), takze slouzi i pro stopu (sim/trace_v3.py). Modeluji JEN pouzitou podmnozinu
// parametru; nepodporovana konfigurace skonci hlaskou $display("CHYBA MODELU ...") a $finish.
//
//   SDPB   (UG285-1.4E kap. 3.3): pamet 16 Kbit adresovana po BITECH -- sirka w pouziva adresu AD[13:log2 w]
//          (tab. 2-2: 32 b -> [13:5], 8 b -> [13:3]); port A zapisuje (CEA = povoleni zapisu), port B cte.
//          Ruzne sirky portu A a B jsou povolene (tab. 2-4, 512 x 32 / 2K x 8 = "Yes"). Bajt k 32b slova na adrese W
//          je tedy bajt na 8b adrese 4W + k (DI[8k+7:8k]). Pri sirce 16 / 32 jsou AD[1:0] / AD[3:0] povoleni bajtu
//          (priklad UG285: .ADA({ada[9:0], 2'b00, 2'b11}) pro 16 b). READ_MODE 0 (bypass): DO plati po hrane CLKB,
//          ktera zachytila ADB (= jako registrovane cteni "rdata <= mem[raddr]"). RESETB synchronni nuluje DO.
//          ⚠️ Mapovani bajtu pri ruzne sirce portu je PREDPOKLAD podle UG285 (adresovani po bitech) a modelu yosys
//          (techlibs/gowin/brams.txt, "widths ... per_port") -- overit sim/tb_prim_chk.sv proti prim_sim.v z Gowin IDE.
// ============================================================
`timescale 1ps/1ps

module SDPB #(
    parameter READ_MODE   = 1'b0,
    parameter BIT_WIDTH_0 = 32,
    parameter BIT_WIDTH_1 = 32,
    parameter BLK_SEL_0   = 3'b000,
    parameter BLK_SEL_1   = 3'b000,
    parameter RESET_MODE  = "SYNC"
)(
    input  wire        CLKA, CEA, CLKB, CEB, OCE, RESETA, RESETB,
    input  wire [13:0] ADA, ADB,
    input  wire [31:0] DI,
    input  wire [2:0]  BLKSELA, BLKSELB,
    output wire [31:0] DO
);
    reg        m [0:16383];
    reg [31:0] dor = 32'd0;
    integer    i, wa, ra, wb;
    initial begin
        for (i = 0; i < 16384; i = i + 1) m[i] = 1'b0;              // INIT_RAM_xx = 0 (vychozi Gowin)
        if (READ_MODE != 1'b0 || RESET_MODE != "SYNC" ||
            !(BIT_WIDTH_0 == 1 || BIT_WIDTH_0 == 2 || BIT_WIDTH_0 == 4 || BIT_WIDTH_0 == 8 || BIT_WIDTH_0 == 16 || BIT_WIDTH_0 == 32) ||
            !(BIT_WIDTH_1 == 1 || BIT_WIDTH_1 == 2 || BIT_WIDTH_1 == 4 || BIT_WIDTH_1 == 8 || BIT_WIDTH_1 == 16 || BIT_WIDTH_1 == 32)) begin
            $display("CHYBA MODELU SDPB %m: nepodporovana konfigurace"); $finish;
        end
    end
    assign DO = dor;
    // zapis (port A): adresa po bitech = AD s nulovanymi dolnimi log2(w) bity
    always @(posedge CLKA) if (CEA && BLKSELA == BLK_SEL_0) begin
        wa = ADA & ~(BIT_WIDTH_0 - 1);
        for (i = 0; i < BIT_WIDTH_0; i = i + 1) begin
            wb = (BIT_WIDTH_0 == 32) ? ADA[i / 8] : (BIT_WIDTH_0 == 16) ? ADA[i / 8] : 1;   // povoleni bajtu
            if (wb) m[wa + i] = DI[i];
        end
    end
    // cteni (port B), bypass: DO = slovo z adresy zachycene touto hranou
    always @(posedge CLKB) begin
        if (RESETB) dor <= 32'd0;
        else if (CEB && BLKSELB == BLK_SEL_1) begin
            ra = ADB & ~(BIT_WIDTH_1 - 1);
            for (i = 0; i < 32; i = i + 1) dor[i] <= (i < BIT_WIDTH_1) ? m[ra + i] : 1'b0;
        end
    end
endmodule
