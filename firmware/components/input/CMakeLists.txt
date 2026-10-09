idf_component_register(SRCS "input.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event
                       PRIV_REQUIRES esp_driver_gpio)
