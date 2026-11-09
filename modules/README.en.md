# Module catalog

[简体中文](README.md) | English

Store reusable code at `<domain>/<module-name>/`. Each module owns its source, public entry point, dependency manifest, and Chinese and English READMEs.

## Domains

| Location | Contents |
| --- | --- |
| [microcontrollers/](microcontrollers/README.en.md) | Reusable microcontroller firmware modules |

## Modules

| Chip | Module | Language | Responsibility |
| --- | --- | --- | --- |
| esp32-s3 | [uart](microcontrollers/esp32-s3/uart/README.en.md) | C | Configurable UART I/O and lifecycle |
| esp32-s3 | [ws2812](microcontrollers/esp32-s3/ws2812/README.en.md) | C | Uniform strip color over RMT |
| mspm0g3507 | [async-i2c](microcontrollers/mspm0g3507/async-i2c/README.en.md) | C | Per-bus nonblocking I2C transaction queue |
| mspm0g3507 | [button](microcontrollers/mspm0g3507/button/README.en.md) | C | Debounce, short, double and long press events |
| mspm0g3507 | [i2c-line-sensor](microcontrollers/mspm0g3507/i2c-line-sensor/README.en.md) | C | Eight-channel I2C line-sensor data |
| mspm0g3507 | [i2c-jy901s](microcontrollers/mspm0g3507/i2c-jy901s/README.en.md) | C | JY901S attitude angles over I2C |
| mspm0g3507 | [uart-bno08x-rvc](microcontrollers/mspm0g3507/uart-bno08x-rvc/README.en.md) | C | UART RVC byte-stream parsing |
| mspm0g3507 | [uart-imu-55](microcontrollers/mspm0g3507/uart-imu-55/README.en.md) | C | UART 0x55 IMU frame parsing |
| mspm0g3507 | [gpio-ultrasonic](microcontrollers/mspm0g3507/gpio-ultrasonic/README.en.md) | C | Configurable GPIO ultrasonic ranging |
| mspm0g3507 | [timer-ultrasonic](microcontrollers/mspm0g3507/timer-ultrasonic/README.en.md) | C | Captured pulse width and distance conversion |
| mspm0g3507 | [i2c-spi-ssd1306](microcontrollers/mspm0g3507/i2c-spi-ssd1306/README.en.md) | C | SSD1306 display core with I2C/SPI transport callbacks |
| k230 | [alpha-beta-tracker](microcontrollers/k230/alpha-beta-tracker/README.en.md) | Python | Configurable one-dimensional position and velocity estimation |

See [docs/architecture.md](../docs/architecture.md) for file placement and [docs/module-template.md](../docs/module-template.md) for the module documentation template.
