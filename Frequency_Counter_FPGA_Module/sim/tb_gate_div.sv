// tb_gate_div.sv -- gate_div: floor(dt*5/8192) pro nahodne i hranicni hodnoty
`timescale 1ns/1ps
module tb_gate_div;
    reg clk = 0; always #50 clk = ~clk;
    reg start = 0; reg [47:0] dt = 0;
    wire [63:0] g; wire v, b;
    gate_div dut (.clk(clk), .start(start), .dt(dt), .gate_ns(g), .valid(v), .busy(b));
    integer errors = 0, i;
    reg [47:0] vec [0:11];
    task chk(input [47:0] x);
        begin
            dt = x; @(posedge clk); start = 1; @(posedge clk); start = 0;
            wait (v); @(posedge clk);
            if (g !== ((x * 64'd5) >> 13)) begin
                $display("FAIL dt=%0d: %0d, ocekavano %0d", x, g, (x*5)>>13); errors = errors + 1;
            end
        end
    endtask
    initial begin
        vec[0]=0; vec[1]=999; vec[2]=1000; vec[3]=1001; vec[4]=250000000000; vec[5]=999999999999;
        vec[6]=48'h0000FFFFFFFF; vec[7]=48'h7FFFFFFFFFFF; vec[8]=1999; vec[9]=2000; vec[10]=100000000000; vec[11]=281474976;
        for (i = 0; i < 12; i = i + 1) chk(vec[i]);
        for (i = 0; i < 300; i = i + 1) chk({$urandom, $urandom} & 48'h3FFFFFFFFFFF);
        if (errors == 0) $display("PASS: tb_gate_div"); else $display("FAIL: tb_gate_div (%0d)", errors);
        $finish;
    end
endmodule
