#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAGER_NAME_MAX 32

typedef enum {
    PAGER_MSG_CALL,     /* qualcuno ha chiamato la pausa */
    PAGER_MSG_IGNORED,  /* JSON valido ma tipo sconosciuto o assente */
} pager_msg_kind_t;

typedef struct {
    pager_msg_kind_t kind;
    char from[PAGER_NAME_MAX + 1];  /* "" se assente o non stringa */
    int64_t ts;                     /* 0 se assente o non numerico */
} pager_msg_t;

/* Interpreta un messaggio dal server. data può non essere terminato da '\0'.
 * Ritorna false se data non contiene un oggetto JSON. */
bool pager_proto_parse(const char *data, size_t len, pager_msg_t *out);

/* Testo da inviare al server per chiamare la pausa. */
const char *pager_proto_call_message(void);

/* true se name rispetta [a-z0-9_-]{1,32}, come richiesto dal server. */
bool pager_proto_valid_name(const char *name);
