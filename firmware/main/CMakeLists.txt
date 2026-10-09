idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_event esp_timer nvs_flash pager_core notifier input wifi_conn pager_link)
