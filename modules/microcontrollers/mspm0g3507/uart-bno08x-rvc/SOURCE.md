# Source and permission status

- Upstream: [Torris-Yin/mspm0-modules](https://github.com/Torris-Yin/mspm0-modules).
- Revision: `66dc0f67b274f4fcb14022d6db9dace2f975445d`.
- Original source files:
  - [Template/Drivers/MSPM0/interrupt.c](https://github.com/Torris-Yin/mspm0-modules/blob/66dc0f67b274f4fcb14022d6db9dace2f975445d/Template/Drivers/MSPM0/interrupt.c)
  - [Template/Drivers/BNO08X_UART_RVC/bno08x_uart_rvc.c](https://github.com/Torris-Yin/mspm0-modules/blob/66dc0f67b274f4fcb14022d6db9dace2f975445d/Template/Drivers/BNO08X_UART_RVC/bno08x_uart_rvc.c)
  - [Template/Drivers/BNO08X_UART_RVC/bno08x_uart_rvc.h](https://github.com/Torris-Yin/mspm0-modules/blob/66dc0f67b274f4fcb14022d6db9dace2f975445d/Template/Drivers/BNO08X_UART_RVC/bno08x_uart_rvc.h)

No root LICENSE or license grant is present in the supplied upstream revision. The listed source files contain no copyright or license notices. This directory does not grant permission to use, copy, modify, distribute or sublicense upstream code or its derivatives. Public availability is not a license. Obtain the necessary permission from the rights holder before use or redistribution. No repository-wide license is applied to this module.

The upstream headers contain SysConfig setup instructions, not license notices. The extracted parser owns no UART, DMA, interrupt vector or GPIO configuration.

## Adaptation

Protocol cross-reference: [Adafruit RVC implementation, commit c207266e8e6b72a088312a90fd2154c30dfb6afe](https://github.com/adafruit/Adafruit_BNO08x_RVC/blob/c207266e8e6b72a088312a90fd2154c30dfb6afe/Adafruit_BNO08x_RVC.cpp) ([readable mirror](http://git-mirror.rt-thread.com:12236/adafruit/Adafruit_BNO08x_RVC/raw/commit/c207266e8e6b72a088312a90fd2154c30dfb6afe/Adafruit_BNO08x_RVC.cpp)). Its read path consumes 17 bytes after the two header bytes and sums the first 16, establishing full-frame checksum coverage of bytes 2 through 17. This reference is used only to confirm protocol facts; its code is not included. The extraction corrects the source interrupt handler checksum coverage of bytes 2 through 14. Acceleration is decoded as signed mg.
