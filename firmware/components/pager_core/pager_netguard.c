#include "pager_netguard.h"

pager_net_action_t pager_netguard_handle(bool *link_running, pager_net_event_t ev)
{
    if (ev == PAGER_NET_WIFI_LOST && *link_running) {
        *link_running = false;
        return PAGER_NET_STOP_LINK;
    }
    if (ev == PAGER_NET_IP_GOT && !*link_running) {
        *link_running = true;
        return PAGER_NET_START_LINK;
    }
    return PAGER_NET_NONE;
}
