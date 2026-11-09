idf_component_register(
    SRCS "src/uart_port.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart freertos
    PRIV_REQUIRES esp_driver_gpio
    REQUIRED_IDF_TARGETS esp32s3)
