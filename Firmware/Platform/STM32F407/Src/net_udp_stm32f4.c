/**
 * @file    net_udp_stm32f4.c
 * @brief   lwIP RAW-API UDP binding for C5P1 (see net_udp.h).
 *
 * Context, because it decides what is and is not allowed here: in lwIP's
 * NO_SYS configuration the receive path runs from ethernetif_input(), which
 * MX_LWIP_Process() calls in the superloop. **This callback is therefore
 * main-loop code, not an interrupt handler.** The ETH interrupt only
 * services the MAC's DMA.
 *
 * That is what makes it acceptable to decode a whole motion block inline.
 * The work is bounded - at most 32 records of 5 axes, no allocation, no
 * loop that depends on anything but record_count - and it is preemptible
 * throughout by the STEP-DMA vector at NVIC priority 2, which ADR-012 keeps
 * strictly above Ethernet's 5. Nothing here can delay a STEP edge.
 */
#include "net_udp.h"

#include "cnc_session.h"
#include "cnc_protocol.h"
#include "net_config.h"

#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

#include "stm32f4xx_hal.h"          /* HAL_GetTick */

/* Static rather than automatic: this path is single-threaded by contract
 * (superloop only, never an ISR), so there is no reentrancy to protect
 * against, and two 664-byte stack frames in the receive path would be a
 * needless spike. Both land in SRAM1 with everything else lwIP owns, which
 * is what ADR-012 requires. */
static uint8_t s_rx[CNC_MAX_PACKET];
static uint8_t s_tx[CNC_MAX_PACKET];

static struct udp_pcb *s_pcb;
static ip_addr_t       s_host_addr;
static uint16_t        s_host_port;
static bool            s_host_known;
static uint32_t        s_tx_count;
static uint32_t        s_tx_errors;

/* ---------------------------------------------------------------------- */

static void transmit(const uint8_t *data, uint16_t len,
                     const ip_addr_t *addr, uint16_t port)
{
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);

    if (p == NULL) {
        s_tx_errors++;
        return;
    }
    if (pbuf_take(p, data, len) != ERR_OK) {
        s_tx_errors++;
        pbuf_free(p);
        return;
    }

    if (udp_sendto(s_pcb, p, addr, port) != ERR_OK) {
        s_tx_errors++;
    } else {
        s_tx_count++;
    }
    pbuf_free(p);
}

static void on_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                    const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    (void)pcb;

    if (p == NULL) {
        return;
    }

    /* A datagram larger than any legal packet cannot become one by being
     * truncated, so it is dropped whole rather than parsed in part. */
    if (p->tot_len <= (uint16_t)sizeof(s_rx)) {
        const uint16_t n = pbuf_copy_partial(p, s_rx, p->tot_len, 0);

        /* Free the pbuf before doing the work: lwIP's RX pool is small, and
         * holding a buffer across the decode would shrink the headroom the
         * next burst has. */
        pbuf_free(p);

        const size_t reply = cnc_session_on_datagram(s_rx, n, HAL_GetTick(),
                                                     s_tx, sizeof(s_tx));
        /* Learn the endpoint only from a packet the session answered, which
         * means it passed magic, version, length and CRC. A corrupt
         * datagram gets no reply and must not move the status stream to
         * whatever source sent it. */
        if (reply > 0u) {
            ip_addr_copy(s_host_addr, *addr);
            s_host_port  = port;
            s_host_known = true;
            transmit(s_tx, (uint16_t)reply, addr, port);
        }
        return;
    }

    pbuf_free(p);
}

/* ---------------------------------------------------------------------- */

bool net_udp_init(void)
{
    cnc_session_init();

    s_host_known = false;
    s_tx_count   = 0;
    s_tx_errors  = 0;

    s_pcb = udp_new();
    if (s_pcb == NULL) {
        return false;
    }

    /* Bound to every local address: the device has exactly one, and binding
     * to IP_ANY keeps the socket working if the address is ever changed. */
    if (udp_bind(s_pcb, IP_ADDR_ANY, (u16_t)NET_UDP_PORT) != ERR_OK) {
        udp_remove(s_pcb);
        s_pcb = NULL;
        return false;
    }

    udp_recv(s_pcb, on_recv, NULL);
    return true;
}

void net_udp_poll(void)
{
    if (s_pcb == NULL || !s_host_known) {
        /* Nothing is transmitted until a valid packet has said where. */
        return;
    }

    const size_t n = cnc_session_tick(HAL_GetTick(), s_tx, sizeof(s_tx));
    if (n > 0u) {
        transmit(s_tx, (uint16_t)n, &s_host_addr, s_host_port);
    }
}

bool net_udp_ready(void)       { return s_pcb != NULL; }
uint32_t net_udp_tx_count(void)  { return s_tx_count; }
uint32_t net_udp_tx_errors(void) { return s_tx_errors; }
