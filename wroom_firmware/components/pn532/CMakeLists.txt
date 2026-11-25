idf_component_register(SRCS "pn532.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver log esp_driver_gpio esp_driver_i2c)
