// ============================================================
// ides_pps.v -- v3 NAVRH (audit 2026-10-20, etapa E4), NEZAPOJENO, NEOVERENO SIMULACI ANI P&R.
// 1PPS s rozlisenim 1,25 ns: rPLL (100 MHz z GCLK pinu 35 -> 400 MHz, VCO 800 MHz) + CLKDIV /4 (PCLK 100 MHz) + IDES8 na pinu 33.
// sigma kvantizace 0,36 ns (dnes 2,9 ns); jitter PLL (< 300 ps p-p cyklovy podle DS100) ~0,05 ns rms -> celkem ~0,37 ns.
// Jednotka vystupu = T_clk/16384 jako TDC (1,25 ns = 2048 jednotek): znacka = pcnt*16384 + poloha*2048.
// Osa PCLK ma vuci tick_hi PEVNY posun (PLL zamcena na clk_p0); pro kmitocet a GPSDO se vyrusi, pro interval vuci A/B kalibrovat.
// Po ztrate LOCK: rec_lock = 0, STM zahodi fazovou kotvu (posun CLKDIV muze skocit o k x 2,5 ns).
// 🔴 OVERIT: (1) IDES8 + HCLK na IOB23A (zkusebni P&R), (2) poradi bitu Q0..Q7 (parametr Q0_FIRST; test: rozdil proti hrubemu
// pps_cap musi lezet v [-10, 0] ns rovnomerne), (3) CLKIN rPLL ze site GCLK (Gowin dovoli i vnitrni hodiny, napr. vystup DCS).
// ============================================================
module ides_pps #(
    parameter Q0_FIRST = 1                      // 1 = Q0 je nejdrivejsi vzorek [?]
)(
    input  wire        clk100,                  // clk_p0_100m (GCLK)
    input  wire        pin,                     // gps_1pps (PIN33)
    input  wire        clk,                     // = clk100: domena vystupu
    output reg         rec_tgl,                 // preklopi se s kazdou novou hranou (domena clk)
    output reg  [47:0] rec_ts,                  // cas hrany [T/16384] na ose PCLK
    output reg  [7:0]  rec_seq,
    output wire        lock_o
);
    wire fclk, lock, pclk;
    rPLL #(
        .FCLKIN("100"), .DEVICE("GW1NR-9C"),
        .IDIV_SEL(0), .FBDIV_SEL(3), .ODIV_SEL(2),            // 100 * 4 / 1 = 400 MHz, VCO = 400 * 2 = 800 MHz
        .DYN_IDIV_SEL("false"), .DYN_FBDIV_SEL("false"), .DYN_ODIV_SEL("false"),
        .PSDA_SEL("0000"), .DUTYDA_SEL("1000"), .DYN_DA_EN("false"),
        .CLKOUT_FT_DIR(1'b1), .CLKOUTP_FT_DIR(1'b1), .CLKOUT_DLY_STEP(0), .CLKOUTP_DLY_STEP(0),
        .CLKFB_SEL("internal"), .CLKOUT_BYPASS("false"), .CLKOUTP_BYPASS("false"), .CLKOUTD_BYPASS("false"),
        .DYN_SDIV_SEL(2), .CLKOUTD_SRC("CLKOUT"), .CLKOUTD3_SRC("CLKOUT")
    ) u_pll (
        .CLKOUT(fclk), .LOCK(lock), .CLKOUTP(), .CLKOUTD(), .CLKOUTD3(),
        .RESET(1'b0), .RESET_P(1'b0), .CLKIN(clk100), .CLKFB(1'b0),
        .FBDSEL(6'd0), .IDSEL(6'd0), .ODSEL(6'd0), .PSDA(4'd0), .DUTYDA(4'd0), .FDLY(4'd0)
    );
    assign lock_o = lock;
    // CLKDIV: reset uvolnit synchronne (deterministicka faze PCLK vuci FCLK po kazdem zamceni)
    reg [1:0] lk_s = 2'b00;
    always @(posedge clk100) lk_s <= {lk_s[0], lock};
    CLKDIV #(.DIV_MODE("4"), .GSREN("false")) u_div (.HCLKIN(fclk), .RESETN(lk_s[1]), .CALIB(1'b0), .CLKOUT(pclk));
    wire [7:0] q;
    IDES8 #(.GSREN("false"), .LSREN("true")) u_ides (
        .D(pin), .FCLK(fclk), .PCLK(pclk), .CALIB(1'b0), .RESET(~lk_s[1]),
        .Q0(q[0]), .Q1(q[1]), .Q2(q[2]), .Q3(q[3]), .Q4(q[4]), .Q5(q[5]), .Q6(q[6]), .Q7(q[7]));
    // domena PCLK: casova osa a detekce prvni nabezne hrany (vcetne rozhrani slov)
    wire [7:0] w = (Q0_FIRST != 0) ? q : {q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]};   // w[0] = nejdrivejsi
    reg  [33:0] pcnt = 34'd0;
    reg         last = 1'b0;                     // posledni vzorek predchoziho slova
    reg  [47:0] p_ts = 48'd0;
    reg         p_tgl = 1'b0;
    reg  [2:0]  pos;
    reg         hit;
    integer     i;
    always @* begin
        hit = 1'b0; pos = 3'd0;
        for (i = 7; i >= 0; i = i - 1)
            if (w[i] && ((i == 0) ? ~last : ~w[i-1])) begin hit = 1'b1; pos = i[2:0]; end   // nejdrivejsi 0->1
    end
    always @(posedge pclk) begin
        pcnt <= pcnt + 34'd1;
        last <= w[7];
        if (hit) begin p_ts <= {pcnt, 14'd0} + {32'd0, pos, 11'd0}; p_tgl <= ~p_tgl; end   // pos * 2048
    end
    // do domeny clk: toggle-handshake (udalosti 1 Hz, p_ts drzeno 1 s)
    reg [2:0] t_s = 3'b000;
    initial begin rec_tgl = 1'b0; rec_ts = 48'd0; rec_seq = 8'd0; end
    always @(posedge clk) begin
        t_s <= {t_s[1:0], p_tgl};
        if (t_s[2] ^ t_s[1]) begin rec_ts <= p_ts; rec_seq <= rec_seq + 8'd1; rec_tgl <= ~rec_tgl; end
    end
endmodule
