idf_component_register(
    SRCS "src/ws2812.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio
    PRIV_REQUIRES esp_driver_rmt esp_rom
    REQUIRED_IDF_TARGETS esp32s3)
