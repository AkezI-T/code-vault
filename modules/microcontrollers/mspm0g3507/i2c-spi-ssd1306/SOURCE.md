# Source and licensing status

- Upstream repository: [Torris-Yin/mspm0-modules](https://github.com/Torris-Yin/mspm0-modules).
- Upstream commit: [66dc0f67b274f4fcb14022d6db9dace2f975445d](https://github.com/Torris-Yin/mspm0-modules/tree/66dc0f67b274f4fcb14022d6db9dace2f975445d).
- Source directory: Template/Drivers.

## Source files

- OLED_Hardware_I2C/oled_hardware_i2c.c
- OLED_Hardware_I2C/oled_hardware_i2c.h
- OLED_Hardware_SPI/oled_hardware_spi.c
- OLED_Hardware_SPI/oled_hardware_spi.h
- OLED_Software_I2C/oled_software_i2c.c
- OLED_Software_I2C/oled_software_i2c.h
- OLED_Software_SPI/oled_software_spi.c
- OLED_Software_SPI/oled_software_spi.h
- The oledfont.h file in each of the four directories is inspected for layout and notices, but its arrays are not included here.

## Adaptation

The common SSD1306 page-addressing, positioning, clearing, inversion, display power, orientation and bitmap behavior is consolidated into a caller-owned instance with synchronous command/data transport callbacks. Board-specific GPIO, I2C, SPI, reset, SysTick, delay and bus recovery code is not included. Transport errors propagate to the caller and invalidate the instance because partial controller writes cannot be rolled back.

Geometry selects multiplex ratio height - 1 and COM configuration 0x02 for 128x32 or 0x12 for 128x64. The common source initializes only 128x64. The VCOMH parameter is 0x20 (0.77 x VCC), replacing source value 0x40, which is outside the documented SSD1306 values. Internal charge-pump operation, page mode and the source's normal/180-degree segment/COM mappings remain explicit. The controller specification is [SSD1306 data sheet](https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf).

Text is supplied by the caller as page-formatted glyph bitmaps. No upstream ASCII/Chinese font arrays or images are copied. The small geometric tile in both README examples is authored for the example.

## Licensing status

The referenced upstream commit has no root LICENSE or COPYING file. The listed OLED implementations, headers and font files contain no explicit copyright or license grant. SysConfig setup comments in the headers describe board configuration and are not license grants. The unrelated Fusion subdirectory's license does not cover these OLED files.

No MIT license or other permission grant is assigned to this adaptation. This provenance record does not grant rights to upstream code, derived code or font data. Obtain the appropriate permission from the relevant rights holders before use or redistribution. Preserve any applicable upstream notices if additional upstream material is introduced.
