idf_component_register(
    SRCS
        "main.c"
        "audio.c"
        "nfc.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        esp_driver_dac
        pn532

    EMBED_FILES
        "sounds/ka_ching.raw"
        "sounds/error.raw"
        "sounds/dice.raw"
        "sounds/build.raw"
)
