idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_dac esp_driver_uart
    EMBED_FILES
        "sounds/ka_ching.raw"
        "sounds/error.raw"
        "sounds/dice.raw"
        "sounds/build.raw"
)