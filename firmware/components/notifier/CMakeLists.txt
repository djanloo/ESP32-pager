idf_component_register(SRCS "notifier.c"
                       INCLUDE_DIRS "include"
                       REQUIRES pager_core
                       PRIV_REQUIRES esp_timer esp_driver_gpio)
