idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_dac esp_driver_uart
    EMBED_FILES
        "ka_ching.raw"
        "error.raw"
        "dice.raw"
        "build.raw"
)