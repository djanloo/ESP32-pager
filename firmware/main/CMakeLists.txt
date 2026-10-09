idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_event nvs_flash pager_core wifi_conn pager_link)
