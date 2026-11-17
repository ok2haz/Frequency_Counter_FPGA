set_device GW1NR-LV9QN88PC6/I5 -name GW1NR-9C
add_file -type verilog v3_top_synth.v
add_file -type verilog ../seg_acc.v
add_file -type verilog ../pin_cnt.v
add_file -type verilog ../ides_pps.v
set_option -top_module v3_top_synth
set_option -verilog_std sysv2017
set_option -output_base_name v3syn
set_option -gen_text_timing_rpt 0
run syn
