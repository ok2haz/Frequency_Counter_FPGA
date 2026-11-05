"""cdc_lag_test.py - test zpozdeni USB CDC konzole (STATUS #298): posle N x ping a vypise, co a kdy dorazi.
Zdrava konzole: kazda odpoved "ping\r\npong\r\r\n\r\n" dorazi cela do ~100 ms. Chybna: odpoved dorazi az s DALSIM prikazem (opozdena o jeden prenos).
Pouziti: python tools/cdc_lag_test.py COM10 4   (potrebuje pyserial; DTR/RTS se zapina)"""
import sys, time, serial
port = sys.argv[1]; n = int(sys.argv[2])
s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 0.05; s.dtr = True; s.rts = True
s.open()
t0 = time.time()
def drain(sec, tag):
    t1 = time.time()
    while time.time() - t1 < sec:
        d = s.read(8192)
        if d:
            print("  [%s] +%.0f ms: %d B %r" % (tag, (time.time() - t0) * 1000, len(d), d[:100]))
drain(0.5, "open")
for i in range(n):
    print("# ping %d" % (i + 1))
    s.write(b"ping\r\n")
    drain(2.0, "p%d" % (i + 1))
s.close()
