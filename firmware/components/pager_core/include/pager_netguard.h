#pragma once

#include <stdbool.h>

/* Decide quando fermare/riavviare il collegamento al server in base al Wi-Fi:
 * senza Wi-Fi il socket resterebbe "connesso" fino al timeout del keepalive. */
typedef enum {
    PAGER_NET_WIFI_LOST,
    PAGER_NET_IP_GOT,
} pager_net_event_t;

typedef enum {
    PAGER_NET_NONE,
    PAGER_NET_STOP_LINK,
    PAGER_NET_START_LINK,
} pager_net_action_t;

/* link_running: stato del client WebSocket, aggiornato dalla funzione. */
pager_net_action_t pager_netguard_handle(bool *link_running, pager_net_event_t ev);
