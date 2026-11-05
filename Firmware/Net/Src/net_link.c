/**
 * @file    net_link.c
 * @brief   Portable Ethernet link observer (see net_link.h).
 */
#include "net_link.h"

#include <stddef.h>
#include <string.h>

static net_link_status_t s_status;

void net_link_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.speed = NET_SPEED_NONE;
}

void net_link_set_phy_init(bool ok, uint8_t phy_addr)
{
    s_status.phy_init_ok = ok;
    s_status.phy_addr    = phy_addr;
}

bool net_link_on_report(const net_phy_report_t *report, uint32_t now_ms)
{
    if (report == NULL) {
        return false;
    }

    const bool was_up = s_status.link_up;
    const bool is_up  = report->link_up;

    if (is_up) {
        s_status.speed       = report->speed;
        s_status.full_duplex = report->full_duplex;
    } else {
        /* A down link has no speed. Reporting the last negotiated speed
         * while the cable is out would read as a live link to anything
         * that only glances at the snapshot. */
        s_status.speed       = NET_SPEED_NONE;
        s_status.full_duplex = false;
    }
    s_status.link_up = is_up;

    if (is_up == was_up) {
        return false;
    }

    if (is_up) {
        s_status.up_count++;
    } else {
        s_status.down_count++;
    }
    s_status.last_change_ms = now_ms;
    return true;
}

void net_link_get(net_link_status_t *out)
{
    if (out != NULL) {
        *out = s_status;
    }
}

bool net_link_is_up(void)
{
    return s_status.link_up;
}
