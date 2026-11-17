// tb_cs_glitch.sv -- filtr CS v PHY v3 (src/spi_slave_phy_v3.v, CS_FILT) a citace zahozenych pulzu na CS (2026-10-26, L-0242).
// Nocni test SPI ukazal, ze FPGA vidi pulz na CS uprostred transakce a jeho `cs_fall` (asynchronni reset citacu PHY) restartuje vysilani
// od bajtu 0. Dve PHY vedle sebe se stejnym vstupem (SCK, CS, MOSI) a stejnou TX RAM:
//   A: CS_FILT = 16 (160 ns)   -- nova PHY, pulz kratsi nez 160 ns se zahodi a spocita
//   B: CS_FILT = 1             -- chovani dosavadni PHY (bez filtru) = negativni kontrola mereni: stejny pulz transakci rozbije
// Kontroluje:
//   1) pulz do 1 (CS dole) sirky 25..140 ns na bitove pozici 6, 15, 78, 97, 1000, 4000, 8000: A vyda ramec beze zmeny (MISO = TX RAM + CRC bloku),
//      rx_len_ok = 1, dbg_sck_cnt = 8192, frame_end_tgl se prepne prave jednou; B ramec pokazi (MISO se lisi, nebo delka);
//   2) citace A: prave jeden z 4 "aktivnich" binu se zvysi o 1 a lezi v ocekavanem rozsahu podle sirky, citac "klid" se nezmeni;
//   3) pulz do 0 (CS nahore) sirky 25 a 100 ns mezi transakcemi: citac "klid" +1, zadny frame_end_tgl, dalsi ramec v poradku;
//   4) pulz 220 ns (> filtr) A neodfiltruje: ramec se rozbije jako u B (kontrola, ze test filtr opravdu zkousi);
//   5) po vsech zkouskach cista transakce u A: ramec v poradku, citace se nemeni.
// Spusteni: iverilog -g2012 -s tb_cs_glitch -o t.vvp src/spi_slave_phy_v3.v sim/tb_cs_glitch.sv ; vvp t.vvp
`timescale 1ns/1ps
module tb_cs_glitch;
  reg clk100 = 0; always #5  clk100 = ~clk100;
  reg clk10  = 0; always #50 clk10  = ~clk10;
  reg sck = 0, cs = 1, mosi = 0;
  reg tx_half = 1, tx_we = 0; reg [10:0] tx_waddr = 0; reg [7:0] tx_wdata = 0; reg [9:0] rx_raddr = 0;
  wire missoA, missoB;
  wire txbA, txbB, busyA, busyB, feA, feB, lenA, lenB, rdhA, rdhB; wire [3:0] okA, okB;
  wire [7:0] b0A, b1A, b2A, p0A, p1A, p2A, p3A, rdA, b0B, b1B, b2B, p0B, p1B, p2B, p3B, rdB; wire [31:0] sqA, sqB; wire [15:0] dscA, dscB;
  wire [39:0] glA, glB;
  spi_slave_phy_v3 #(.CS_FILT(16)) pA(.clk(clk100), .clk_app(clk10), .sck_pin(sck), .cs_pin(cs), .mosi_pin(mosi), .miso(missoA),
      .tx_half(tx_half), .tx_we(tx_we), .tx_waddr(tx_waddr), .tx_wdata(tx_wdata), .tx_base(txbA), .busy(busyA),
      .rx_b0(b0A), .rx_b1(b1A), .rx_b2(b2A), .rx_seq(sqA), .rx_p0(p0A), .rx_p1(p1A), .rx_p2(p2A), .rx_p3(p3A),
      .rx_blk_ok(okA), .rx_len_ok(lenA), .frame_end_tgl(feA), .rx_rd_half(rdhA), .rx_raddr(rx_raddr), .rx_rdata(rdA), .dbg_sck_cnt(dscA), .cs_gl_gray(glA));
  spi_slave_phy_v3 #(.CS_FILT(1))  pB(.clk(clk100), .clk_app(clk10), .sck_pin(sck), .cs_pin(cs), .mosi_pin(mosi), .miso(missoB),
      .tx_half(tx_half), .tx_we(tx_we), .tx_waddr(tx_waddr), .tx_wdata(tx_wdata), .tx_base(txbB), .busy(busyB),
      .rx_b0(b0B), .rx_b1(b1B), .rx_b2(b2B), .rx_seq(sqB), .rx_p0(p0B), .rx_p1(p1B), .rx_p2(p2B), .rx_p3(p3B),
      .rx_blk_ok(okB), .rx_len_ok(lenB), .frame_end_tgl(feB), .rx_rd_half(rdhB), .rx_raddr(rx_raddr), .rx_rdata(rdB), .dbg_sck_cnt(dscB), .cs_gl_gray(glB));

  function [15:0] crc(input [15:0] c0, input [7:0] d);
    integer b; reg [15:0] c; begin c = c0 ^ {d, 8'd0}; for (b = 0; b < 8; b = b + 1) c = c[15] ? ((c << 1) ^ 16'h1021) : (c << 1); crc = c; end
  endfunction
  function [7:0] g2b(input [7:0] g);          // Gray -> binarne
    integer i; reg [7:0] r; begin r[7] = g[7]; for (i = 6; i >= 0; i = i - 1) r[i] = r[i+1] ^ g[i]; g2b = r; end
  endfunction

  reg [7:0] ram1[0:1023];
  reg [7:0] mo[0:1023], miA[0:1023], miB[0:1023];
  integer i, j, k, b, n, errors = 0, nchk = 0, feA_cnt = 0, feB_cnt = 0; reg [15:0] cc;
  integer half_ns = 20;
  always @(feA) feA_cnt = feA_cnt + 1;
  always @(feB) feB_cnt = feB_cnt + 1;

  task chk(input cond, input [8*80-1:0] what);
    begin nchk = nchk + 1; if (!cond) begin errors = errors + 1; $display("  CHYBA: %0s", what); end end
  endtask
  task wr(input [9:0] a, input [7:0] d);
    begin @(posedge clk10); tx_waddr <= {1'b1, a}; tx_wdata <= d; tx_we <= 1; @(posedge clk10); tx_we <= 0; end
  endtask
  // citace A (binarne): [0]=aktivni 1-2, [1]=3-4, [2]=5-8, [3]=9+, [4]=klid
  function [7:0] cnt(input integer w);
    cnt = g2b(glA[w * 8 +: 8]);
  endfunction
  integer c0[0:4], c1[0:4];
  task snap(input integer which);
    integer q; begin for (q = 0; q < 5; q = q + 1) if (which == 0) c0[q] = cnt(q); else c1[q] = cnt(q); end
  endtask

  // jedna transakce 1024 B; pulz na CS po bitu gpos (gpos < 0 = zadny): glitch_up = 1 -> CS nahoru (pri CS dole), sirka gw ns
  task xfer(input integer gpos, input integer gw);
    integer bit_i;
    begin
      cs = 0; #1000; bit_i = 0;
      for (i = 0; i < 1024; i = i + 1) begin miA[i] = 0; miB[i] = 0;
        for (j = 7; j >= 0; j = j - 1) begin
          mosi = mo[i][j]; #(half_ns); sck = 1; miA[i][j] = missoA; miB[i][j] = missoB; #(half_ns); sck = 0;
          bit_i = bit_i + 1;
          if (bit_i == gpos) begin @(negedge clk100); #1; cs = 1; #(gw); cs = 0; end   // start 4 ns pred hranou clk100: sirka k*10 ns = presne k vzorku
        end
      end
      #1000; cs = 1; #3000;
    end
  endtask
  // ocekavany MISO: bajt 0 MAGIC, bajty z TX RAM, 254/255 kazdeho bloku CRC; vrati pocet rozdilnych bajtu
  function integer miso_diff(input integer which);
    integer bb, ii, kk, nn; reg [15:0] c2; reg [7:0] got, got2;
    begin nn = 0;
      for (bb = 0; bb < 4; bb = bb + 1) begin
        c2 = 16'hFFFF;
        for (ii = 0; ii < 254; ii = ii + 1) begin
          kk = (bb == 0 && ii == 0) ? 8'hA5 : ram1[bb * 256 + ii];
          got = which ? miB[bb * 256 + ii] : miA[bb * 256 + ii];
          if (got !== kk[7:0]) nn = nn + 1;
          c2 = crc(c2, kk[7:0]);
        end
        got = which ? miB[bb * 256 + 254] : miA[bb * 256 + 254];
        got2 = which ? miB[bb * 256 + 255] : miA[bb * 256 + 255];
        if (got !== c2[7:0] || got2 !== c2[15:8]) nn = nn + 1;
      end
      miso_diff = nn;
    end
  endfunction

  integer gp_list[0:6]; integer gw_list[0:7];
  integer gi, wi, dA, dB, fa, fb, sum_act, q, bin_ok, nlo, nhi;
  initial begin
    for (i = 0; i < 1024; i = i + 1) begin ram1[i] = (i * 131 + 21) & 8'hFF; mo[i] = (i * 53 + 7) & 8'hFF; end
    mo[0] = 8'hA5; mo[1] = 8'h03; mo[2] = 8'h06;
    for (b = 0; b < 4; b = b + 1) begin
      cc = 16'hFFFF; for (i = 0; i < 254; i = i + 1) cc = crc(cc, mo[b * 256 + i]);
      mo[b * 256 + 254] = cc[7:0]; mo[b * 256 + 255] = cc[15:8];
    end
    for (i = 0; i < 1024; i = i + 1) wr(i[9:0], ram1[i]);
    gp_list[0] = 6; gp_list[1] = 15; gp_list[2] = 78; gp_list[3] = 97; gp_list[4] = 1000; gp_list[5] = 4000; gp_list[6] = 8000;
    gw_list[0] = 20; gw_list[1] = 30; gw_list[2] = 40; gw_list[3] = 50; gw_list[4] = 80; gw_list[5] = 90; gw_list[6] = 120; gw_list[7] = 150;   // k = 2,3,4,5,8,9,12,15 vzorku
    tx_half = 1; #3000;

    // 0) kontrola bez pulzu: obe PHY v poradku, citace nulove
    snap(0);
    xfer(-1, 0);
    chk(miso_diff(0) == 0 && miso_diff(1) == 0, "bez pulzu: MISO neodpovida TX RAM");
    chk(lenA && lenB && dscA == 16'd8192, "bez pulzu: delka");
    snap(1);
    for (q = 0; q < 5; q = q + 1) chk(c1[q] == c0[q], "bez pulzu: citac pulzu se zmenil");

    // 1) + 2) pulz do 1 pri CS dole
    for (gi = 0; gi < 7; gi = gi + 1) begin
      for (wi = 0; wi < 8; wi = wi + 1) begin
        snap(0); fa = feA_cnt; fb = feB_cnt;
        xfer(gp_list[gi] + (wi % 3), gw_list[wi]);
        snap(1);
        dA = miso_diff(0); dB = miso_diff(1);
        chk(dA == 0, "filtr: MISO PHY A (CS_FILT=16) pokazen pulzem na CS");
        chk(lenA == 1'b1 && dscA == 16'd8192, "filtr: rx_len_ok / dbg_sck_cnt PHY A");
        chk(feA_cnt == fa + 1, "filtr: frame_end_tgl PHY A se neprepnul prave jednou");
        chk(dB != 0 || lenB == 1'b0, "negativni kontrola: PHY B (bez filtru) pulz neprojevil");
        chk(feB_cnt >= fb + 2, "negativni kontrola: PHY B ma mit vice nez jeden frame_end_tgl");
        // citace: prave jeden aktivni bin +1; ocekavane takty = sirka / 10 (+-1 podle faze vzorkovani)
        sum_act = 0; for (q = 0; q < 4; q = q + 1) sum_act = sum_act + (c1[q] - c0[q]);
        chk(sum_act == 1, "citac: celkem aktivnich pulzu se nezvysil o 1");
        chk(c1[4] == c0[4], "citac: citac klidu se zmenil pri pulzu za provozu");
        nlo = gw_list[wi] / 10;                                  // presny pocet vzorku (pulz zarovnany na clk100)
        bin_ok = (nlo <= 2) ? (c1[0] - c0[0] == 1) : (nlo <= 4) ? (c1[1] - c0[1] == 1) : (nlo <= 8) ? (c1[2] - c0[2] == 1) : (c1[3] - c0[3] == 1);
        chk(bin_ok == 1, "citac: pulz zapocten do binu, ktery sirce neodpovida");
      end
    end

    // 3) pulz do 0 pri CS nahore (mezi transakcemi): citac klidu, zadny frame_end_tgl
    snap(0); fa = feA_cnt;
    #1000; cs = 0; #25; cs = 1; #3000;
    #1000; cs = 0; #100; cs = 1; #3000;
    snap(1);
    chk(c1[4] - c0[4] == 2, "klid: dva pulzy do 0 pri CS nahore nebyly oba spocteny");
    chk(feA_cnt == fa, "klid: pulz do 0 pri CS nahore vyvolal frame_end_tgl u PHY A");
    xfer(-1, 0);
    chk(miso_diff(0) == 0, "po pulzech v klidu: dalsi ramec PHY A pokazen");

    // 4a) pulz presne CS_FILT vzorku (160 ns) se jeste PRIJME (filtr zahazuje jen kratsi nez CS_FILT): ramec se rozbije
    snap(0); fa = feA_cnt;
    xfer(97, 160);
    chk(miso_diff(0) != 0 || lenA == 1'b0, "hranice filtru: pulz presne 16 vzorku ma byt prijat (rozbit ramec)");
    snap(1);
    for (q = 0; q < 5; q = q + 1) chk(c1[q] == c0[q], "hranice filtru: prijaty pulz se nema pocitat mezi zahozene");
    // 4) pulz 220 ns (> CS_FILT * 10 ns) filtr neodstrani: ramec se rozbije stejne jako u B
    snap(0); fa = feA_cnt;
    xfer(97, 220);
    chk(miso_diff(0) != 0 || lenA == 1'b0, "kontrola testu: pulz 220 ns ma PHY A rozbit (je delsi nez filtr)");
    chk(feA_cnt >= fa + 2, "kontrola testu: pulz 220 ns ma vyvolat frame_end_tgl navic");
    snap(1);
    for (q = 0; q < 5; q = q + 1) chk(c1[q] == c0[q], "pulz delsi nez filtr se nema pocitat mezi zahozene");

    // 5) cista transakce na zaver
    snap(0);
    xfer(-1, 0);
    snap(1);
    chk(miso_diff(0) == 0 && lenA == 1'b1, "zaver: cista transakce PHY A");
    for (q = 0; q < 5; q = q + 1) chk(c1[q] == c0[q], "zaver: citace se menil bez pulzu");

    $display("kontrol %0d, chyb %0d", nchk, errors);
    if (errors == 0) $display("PASS: tb_cs_glitch"); else $display("FAIL: tb_cs_glitch (%0d chyb)", errors);
    $finish;
  end
endmodule
