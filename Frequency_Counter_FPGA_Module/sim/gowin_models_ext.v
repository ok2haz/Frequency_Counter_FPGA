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
//   ALU54D (UG287-1.4E kap. 4.1): jen ALUD_MODE 0 (DOUT = ACC/0 +/- B +/- A), C_ADD_SUB = 0, AREG = BREG = 0,
//          ASIGN_REG = BSIGN_REG = ACCLOAD_REG = 0, OUT_REG = 1, ALU_RESET_MODE "SYNC": DOUT <= (ACCLOAD ? DOUT : 0)
//          + A -/+ B (B_ADD_SUB 1 = minus) pri CE; RESET ma prednost a nuluje vystupni registr. Pri C_ADD_SUB = 0 je
//          znamenko A i akumulatoru plus v obou ctenich UG287 (C_ADD_SUB ridi bud A, nebo akumulator).
//          ⚠️ Overit sim/tb_prim_chk.sv proti prim_sim.v z Gowin IDE.
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

module ALU54D #(
    parameter AREG = 1'b0, parameter BREG = 1'b0, parameter ASIGN_REG = 1'b0, parameter BSIGN_REG = 1'b0,
    parameter ACCLOAD_REG = 1'b0, parameter OUT_REG = 1'b0, parameter B_ADD_SUB = 1'b0, parameter C_ADD_SUB = 1'b0,
    parameter ALUD_MODE = 0, parameter ALU_RESET_MODE = "SYNC"
)(
    input  wire [53:0] A, B,
    input  wire        ASIGN, BSIGN, ACCLOAD,
    input  wire [54:0] CASI,
    input  wire        CLK, CE, RESET,
    output wire [53:0] DOUT,
    output wire [54:0] CASO
);
    reg [53:0] acc = 54'd0;
    initial begin
        if (AREG != 1'b0 || BREG != 1'b0 || ASIGN_REG != 1'b0 || BSIGN_REG != 1'b0 || ACCLOAD_REG != 1'b0 || OUT_REG != 1'b1 ||
            C_ADD_SUB != 1'b0 || ALUD_MODE != 0 || ALU_RESET_MODE != "SYNC") begin
            $display("CHYBA MODELU ALU54D %m: nepodporovana konfigurace"); $finish;
        end
    end
    always @(posedge CLK) begin
        if (RESET) acc <= 54'd0;
        else if (CE) acc <= (ACCLOAD ? acc : 54'd0) + A + (B_ADD_SUB ? -B : B);
    end
    assign DOUT = acc;
    assign CASO = {1'b0, acc};
endmodule
