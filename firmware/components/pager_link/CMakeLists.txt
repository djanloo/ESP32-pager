idf_component_register(SRCS "pager_link.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event
                       PRIV_REQUIRES pager_core)
