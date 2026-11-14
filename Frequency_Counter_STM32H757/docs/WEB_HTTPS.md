# Web přes HTTPS: rozhodnutí a postup (TODO #241, uzavřeno 2026-10-27)

## 1. Rozhodnutí

Přístroj HTTPS **neumí a umět nebude**. TLS se terminuje mimo přístroj na malém reverzním proxy (Raspberry Pi, router, NAS, jakýkoli počítač v síti). Je to varianta (d) z TODO #241; (a) mbedTLS na CM4 padá na paměti a času, viz kap. 2.

Dnešní stav: `httpd_min.c` běží na HTTP/80. Čtení (`GET /api/*`, SPA) je otevřené, zápis (`POST /api/scpi`) vyžaduje `web_ctrl_en` a Basic Auth (`web_user`/`web_pass`); SPA skládá hlavičku `Authorization: Basic base64(user:pass)` v JS a posílá ji s každým zápisem. Heslo tedy jde sítí čitelně, dokud mezi prohlížečem a přístrojem není TLS.

## 2. Proč ne mbedTLS na CM4 (změřeno 2026-10-27, `arm-none-eabi-size -A CM4/Release/H757_LED_CM4.elf`)

| položka | hodnota |
|---|---|
| `.text` | 81,5 kB |
| `.rodata` | 212,3 kB (z toho `SPA_HTML` ≈ 139 kB) |
| `.bss` | **88,7 kB** z 128 kB SRAM2 (z toho `s_hconn` 42 kB, 5 slotů po 8,4 kB) |
| `.eth_dma` | 12,7 kB z 32 kB SRAM3 (jiná paměť, pro TLS nepoužitelná) |
| volno v SRAM2 | ≈ 39 kB, a z toho se bere zásobník CM4 (bare-metal, jeden stack) |

- **RAM:** TLS 1.2 potřebuje i v nejúspornější konfiguraci vstupní a výstupní záznamový buffer 16 kB + 16 kB, certifikát, kontext a špičku haldy při handshaku (dohromady řádově 40–60 kB na jedno spojení). Volných 39 kB nestačí ani pro jedno spojení, natož pro 5 slotů; zmenšení `HTTPD_MAX_CONN` by zhoršilo web (SPA + SSE + dotazy naráz).
- **Čas:** CM4 je bare-metal smyčka bez RTOS. Handshake s ECDHE P-256 (Cortex-M4 240 MHz) trvá stovky ms, s RSA-2048 až sekundy. Po tu dobu by stála `lwip_app_process()`, IPC heartbeat a krmení hlídače CM7 (`stall:CM4`).
- **Flash:** mbedTLS ≈ 40–80 kB `.text` by se vešel, ale bez RAM je to k ničemu.

Zvětšení RAM CM4 (přesun SPA bufferů, jiný rozpis SRAM1/SRAM2) by byl zásah do linkeru a rozdělení paměti obou jader, které je pojištěné proti ztrátě konfigurace ETH (viz CLAUDE.md „D2 SRAM split"). Přínos (HTTPS na embedded webu) tu riziko neospravedlňuje.

## 3. Postup: reverzní proxy

Vždy platí: proxy musí být **v téže síti** (nebo na přímém kabelu) jako přístroj a **přístroj nesmí být dostupný z internetu**. Úsek proxy → přístroj zůstává HTTP; kdo chce, ať ho vede po samostatném kabelu nebo VLAN.

### Caddy (nejjednodušší, vlastní certifikát vydá sám)

```
gpsdo.lan {
    tls internal
    reverse_proxy 10.0.0.106:80 {
        flush_interval -1        # SSE (/api/stream) se nesmí bufferovat
    }
}
```

Prohlížeč jednou odsouhlasí kořenový certifikát Caddy (`caddy trust`), pak je spojení bez varování.

### nginx

```
server {
    listen 443 ssl;
    server_name gpsdo.lan;
    ssl_certificate     /etc/ssl/gpsdo.crt;
    ssl_certificate_key /etc/ssl/gpsdo.key;
    location / {
        proxy_pass http://10.0.0.106;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
        proxy_buffering off;          # SSE
        proxy_read_timeout 3600s;
    }
}
```

Samopodepsaný certifikát: `openssl req -x509 -newkey rsa:2048 -nodes -keyout gpsdo.key -out gpsdo.crt -days 3650 -subj "/CN=gpsdo.lan"`.

### Poznámky

- Přístroj zavírá nečinné SSE spojení po 120 s od posledního potvrzeného odeslání (`HTTPD_SSE_IDLE_MS`); SPA se připojí znovu, proxy to nemění.
- SPA volá adresy od kořene (`/api/state`, `/api/scpi`, `EventSource('/api/stream')`), proxy na kořeni domény tedy nemusí nic přepisovat. (⬜ neověřeno proti skutečné proxy, ověřit při prvním nasazení.)
- **TCP 5025 (SCPI) nemá autorizaci** a TLS nemá nikdy. Nevystavovat mimo důvěryhodnou síť (firewall), případně tunelovat (`ssh -L`).
- Heslo ovládání: dlouhé, jedinečné; `web_ctrl_en` vypnout, když se z webu neovládá.

## 4. Co zůstalo v přístroji

Beze změny. Kdyby se někdy zvětšila RAM CM4 (nový čip, jiné rozdělení), vrátit se ke kap. 2 a přeměřit.
