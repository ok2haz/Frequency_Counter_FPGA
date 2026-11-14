"""webhook_listen.py - zkusebni prijimac pro webhook alarmu GPSDO (TODO #249, STM v0.41.0).

Pouziti (na PC ve stejne siti jako pristroj):
    python tools/webhook_listen.py [port]            # vychozi port 8080, posloucha na vsech rozhranich
    pak na pristroji (UART):  alarmpush url http://<IP_PC>:8080/hook
                              alarmpush on
                              alarmpush test         # musi prijit {"device":"gpsdo","event":"test",...}
Vypise cas, zdroj a telo kazdeho POST a odpovi 200. Parametr --fail-first N: prvnich N pozadavku odpovi 500 (test opakovani).
"""
import argparse
import datetime
import http.server
import json


class H(http.server.BaseHTTPRequestHandler):
    fail_left = 0

    def do_POST(self):
        n = int(self.headers.get('Content-Length', '0') or 0)
        body = self.rfile.read(n).decode('utf-8', 'replace')
        try:
            j = json.loads(body)
            txt = json.dumps(j, ensure_ascii=False)
        except ValueError:
            txt = 'NEPLATNY JSON: ' + body
        code = 200
        if H.fail_left > 0:
            H.fail_left -= 1
            code = 500
        print('%s %s %s %s -> %d' % (datetime.datetime.now().strftime('%H:%M:%S'), self.client_address[0], self.path, txt, code), flush=True)
        self.send_response(code)
        self.send_header('Content-Length', '0')
        self.send_header('Connection', 'close')
        self.end_headers()

    def log_message(self, *a):
        pass


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('port', nargs='?', type=int, default=8080)
    ap.add_argument('--fail-first', type=int, default=0)
    a = ap.parse_args()
    H.fail_left = a.fail_first
    print('posloucham na portu %d (Ctrl+C = konec)' % a.port, flush=True)
    http.server.ThreadingHTTPServer(('0.0.0.0', a.port), H).serve_forever()
