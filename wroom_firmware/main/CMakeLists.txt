idf_component_register(
    SRCS
        "main.c"
        "audio.c"
        "boards.c"
        "nfc.c"
        "uart_link.c"
        "sd_card.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        bt nvs_flash
        esp_driver_dac esp_driver_gpio
        esp_driver_uart
        esp_driver_spi
        esp_driver_sdspi
        esp_timer
        fatfs
        sdmmc
        pn532

    EMBED_FILES
        "sounds/ka_ching.raw"
        "sounds/error.raw"
        "sounds/dice_louder.raw"
        "sounds/build.raw"
        "sounds/robber.raw"
)
