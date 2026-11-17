import json, sys, urllib.request

def get(host="10.0.0.106"):
    return json.loads(urllib.request.urlopen("http://%s/api/fpga" % host, timeout=5).read())

def dec(d):
    w = d["w"]
    u = lambda i: w[i] & 0xFFFFFFFF
    o = {}
    o["gen"] = d["g"]
    o["fw"] = hex(u(0) & 0xFFFF); o["caps"] = hex(u(0) >> 16)
    o["clk_status"] = u(1) & 0xFF; o["clk_loss"] = (u(1) >> 8) & 0xFF
    o["clk_mhz"] = (u(1) >> 16) & 0xFF; o["tdc_status"] = hex((u(1) >> 24) & 0xFF)
    o["chan"] = u(2) & 0xFF; o["meas_status"] = hex((u(2) >> 8) & 0xFF)
    o["status_flags"] = hex((u(2) >> 16) & 0xFF); o["phase_status"] = hex((u(2) >> 24) & 0xFF)
    o["error_flags"] = hex(u(3)); o["seq"] = u(4); o["edges"] = u(5)
    o["gate_ps"] = u(6) | (u(7) << 32); o["gate2_ps"] = u(8) | (u(9) << 32)
    o["dt_b_ps"] = u(10) | (u(11) << 32); o["edges_b"] = u(12)
    s8 = lambda x: x - 256 if x > 127 else x
    o["ccd_p"] = s8(u(13) & 0xFF); o["ccd_s"] = s8((u(13) >> 8) & 0xFF)
    o["rg_ok"] = (u(13) >> 16) & 0xFF; o["regr_used"] = (u(13) >> 24) & 0xFF
    o["rg_n"] = (u(14), u(15))
    o["chk_count/clock/window"] = (u(16), u(17), u(18))
    o["mc_pos/neg"] = (u(19), u(20))
    o["rej_on"] = u(21) & 0xFF; o["poll_chk"] = (u(21) >> 8) & 0xFF
    o["link_ok"] = (u(21) >> 16) & 0xFF; o["rx0"] = hex((u(21) >> 24) & 0xFF)
    o["crc"] = u(22); o["seq_gaps"] = u(23)
    o["frames/new/valid"] = (u(24), u(25), u(26))
    o["rej_seq"] = u(27)
    o["frame_seen/last_seen/sent_chan/sent_win"] = (u(28) & 0xFF, (u(28) >> 8) & 0xFF, (u(28) >> 16) & 0xFF, (u(28) >> 24) & 0xFF)
    o["code_end/st"] = (u(29) & 0xFFFF, u(29) >> 16)
    return o

if __name__ == "__main__":
    for k, v in dec(get(*sys.argv[1:2])).items():
        print("%-40s %s" % (k, v))

def ext(d):
    w = [x & 0xFFFFFFFF for x in d["w"]]
    return {"ym0": w[30] | (w[31] << 32), "ym1": w[32] | (w[33] << 32), "xm0": w[34], "xm1": w[35]}
