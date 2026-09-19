/*
 * usb_console.c — USB CDC konzole: neblokujici TX (ring buffer) + RX most do
 * UartRxQueue. Aktivuje se prepinacem USE_USB_CDC_CONSOLE (usb_console.h) AZ po
 * CubeMX regenu (USB_DEVICE/CDC). Do te doby = prazdne stuby (build-safe).
 *
 * Proc ring buffer: CDC_Transmit_FS NENI blokujici jako HAL_UART_Transmit —
 * vraci USBD_BUSY, dokud predchozi USB paket neodejde. Bez fronty by se printf
 * ztracel. _write (main.c) jen plni ring; pump() ho dava do CDC.
 */
#include "usb_console.h"

#if USE_USB_CDC_CONSOLE

#include "usbd_cdc_if.h"   /* CDC_Transmit_FS, USBD_OK / USBD_BUSY */
#include "cmsis_os2.h"

extern osMessageQueueId_t UartRxQueueHandle;   /* freertos.c — fronta konzole (USB CDC RX; USART1 jde do GpsRxQueue) */

#define TXRING_SZ   1024u            /* mocnina 2 */
#define TXRING_MASK (TXRING_SZ - 1u)
static uint8_t s_tx[TXRING_SZ];
static volatile uint16_t s_head;     /* zapis (_write) */
static volatile uint16_t s_tail;     /* POTVRZENE odeslane (uvolnene) */
/* 🔴 Bajtu od `s_tail`, ktere CDC uz PRIJALO, ale JESTE VYSILA (audit F-0127).
 * Cesta CDC je zero-copy: `USBD_CDC_SetTxBuffer` si jen ulozi ukazatel,
 * `USBD_CDC_TransmitPacket` vrati `USBD_OK` pred prenosem a pri
 * `dma_enable = DISABLE` plni FIFO az obsluha preruseni USB — primo z `s_tx`.
 * Slot se proto NESMI uvolnit pri prijeti, ale az kdyz je prenos dokonceny. */
static volatile uint16_t s_pending;
/* Zahozene bajty. Tichá ztráta bez počítadla je vada sama o sobě (L-0017);
 * hlásí je `status` (řádek KONZOLE). */
static volatile uint32_t s_tx_dropped;
static volatile uint32_t s_rx_dropped;

void usb_console_tx_pump(void)
{
  /* ⚠️ Volano ze DVOU kontextu: defaultTask (bez zamku) i _write->usb_console_tx
   * (pod uartTxMutex). Bez serializace by dva soubezne pumpy videly stejny tail,
   * poslaly stejny blok 2x a rozjely s_tail. PRIMASK kriticka sekce = plna
   * mutualni exkluze; funguje i PRED spustenim scheduleru (early printf z main
   * USER CODE 2) a nezavisi na FreeRTOS. CDC_Transmit_FS je neblokujici (jen
   * naprogramuje endpoint / vrati USBD_BUSY) -> maskovani IRQ na par us je OK. */
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  /* `s_pending` bajtu od `s_tail` jeste LETI v CDC -> poslat jde teprve to za nimi.
   * Invariant `s_pending <= used` drzi sam: `chunk` se vybira z `avail`. */
  uint16_t used  = (uint16_t)(s_head - s_tail);
  uint16_t avail = (uint16_t)(used - s_pending);
  if (avail) {
    /* CDC bere souvisly buffer -> posli blok od konce letici casti do konce ringu. */
    uint16_t t     = (uint16_t)((s_tail + s_pending) & TXRING_MASK);
    uint16_t chunk = (uint16_t)(TXRING_SZ - t);
    if (chunk > avail) chunk = avail;
    if (CDC_Transmit_FS(&s_tx[t], chunk) == USBD_OK) {
      /* 🔴 `USBD_OK` znamena, ze `TxState` bylo 0 — tedy ze PREDCHOZI prenos
       * DOKONCIL. Teprve TED je jeho blok volny (audit F-0127).
       * 🔑 Proc takhle a ne v `CDC_TransmitCplt_FS`: tahle varianta se HOJI SAMA.
       * Kdyz se host odpoji uprostred prenosu, callback nikdy neprijde — uvolneni
       * navazane na nej by nechalo `s_pending` drzeny navzdy a konzole by se
       * jevila trvale plna. Tady staci, ze dalsi `CDC_Transmit_FS` projde.
       * ⚠️ Cena: posledni blok zustane rezervovany, dokud neprijde dalsi zapis
       * do konzole (ring je o nej docasne mensi). */
      s_tail    = (uint16_t)(s_tail + s_pending);
      s_pending = chunk;
    }
    /* USBD_BUSY -> data zustanou v ringu, zkusi se pri dalsim pump(). */
  }
  __set_PRIMASK(primask);
}

uint32_t usb_console_tx_dropped(void) { return s_tx_dropped; }
uint32_t usb_console_rx_dropped(void) { return s_rx_dropped; }

void usb_console_tx(const uint8_t *data, uint16_t len)
{
  for (uint16_t i = 0; i < len; i++) {
    if ((uint16_t)(s_head - s_tail) >= TXRING_MASK) {   /* plny ring */
      usb_console_tx_pump();
      if ((uint16_t)(s_head - s_tail) >= TXRING_MASK) {
        /* 🔴 Zahazuje se NOVY bajt, ne nejstarsi (audit F-0127 + F-0128).
         * Driv se posouval `s_tail` ("drop nejstarsi"), jenze letici blok zacina
         * PRESNE na `s_tail` — posunutim by se zahodila data, ktera CDC prave
         * vysila. Nejstarsi se tedy zahodit NESMI; zahodi se prichazejici.
         * Ztrata se POCITA, aby nebyla ticha (L-0017) — viz `status`.
         * ⚠️ `continue` preskoci jen zapis tohohle bajtu; zaverecny
         * `usb_console_tx_pump()` je AZ ZA smyckou, takze o nej neprijdeme
         * (L-0033 — u `continue` je potreba precist telo az na konec). */
        s_tx_dropped++;
        continue;
      }
    }
    /* volatile store bajtu -> kompilator ho nesmi presunout az ZA s_head++
     * (pump by jinak mohl poslat jeste nezapsany bajt) */
    *(volatile uint8_t *)&s_tx[s_head & TXRING_MASK] = data[i];
    s_head++;
  }
  usb_console_tx_pump();
}

void usb_console_on_rx(const uint8_t *data, uint32_t len)
{
  for (uint32_t i = 0; i < len; i++) {
    uint8_t b = data[i];
    /* ISR-safe (timeout 0). ⚠️ Navrat se VYHODNOCUJE: pri plne fronte se znak
     * prikazu tise ztratil a prikaz se rozpadl, aniz by to kdokoli poznal
     * (audit F-0128). Pocitadlo hlasi `status`. */
    if (osMessageQueuePut(UartRxQueueHandle, &b, 0u, 0u) != osOK) s_rx_dropped++;
  }
}

#else  /* ── USB jeste neaktivni: prazdne stuby (kompiluje se naprazdno) ── */

void usb_console_tx(const uint8_t *data, uint16_t len) { (void)data; (void)len; }
void usb_console_tx_pump(void) { }
void usb_console_on_rx(const uint8_t *data, uint32_t len) { (void)data; (void)len; }
uint32_t usb_console_tx_dropped(void) { return 0u; }
uint32_t usb_console_rx_dropped(void) { return 0u; }

#endif
