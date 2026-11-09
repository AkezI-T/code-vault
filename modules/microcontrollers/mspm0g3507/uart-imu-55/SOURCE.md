# Source and permission status

- Upstream: [Torris-Yin/mspm0-modules](https://github.com/Torris-Yin/mspm0-modules).
- Revision: `66dc0f67b274f4fcb14022d6db9dace2f975445d`.
- Original source files:
  - [Template/Drivers/MSPM0/interrupt.c](https://github.com/Torris-Yin/mspm0-modules/blob/66dc0f67b274f4fcb14022d6db9dace2f975445d/Template/Drivers/MSPM0/interrupt.c)
  - [Template/Drivers/WIT/wit.c](https://github.com/Torris-Yin/mspm0-modules/blob/66dc0f67b274f4fcb14022d6db9dace2f975445d/Template/Drivers/WIT/wit.c)
  - [Template/Drivers/WIT/wit.h](https://github.com/Torris-Yin/mspm0-modules/blob/66dc0f67b274f4fcb14022d6db9dace2f975445d/Template/Drivers/WIT/wit.h)

No root LICENSE or license grant is present in the supplied upstream revision. The listed source files contain no copyright or license notices. This directory does not grant permission to use, copy, modify, distribute or sublicense upstream code or its derivatives. Public availability is not a license. Obtain the necessary permission from the rights holder before use or redistribution. No repository-wide license is applied to this module.

The upstream headers contain SysConfig setup instructions, not license notices. The extracted parser owns no UART, DMA, interrupt vector or GPIO configuration.

## Adaptation

The extraction preserves the 11-byte 0x55 frames and 0x51/0x52/0x53 field mapping. Acceleration and gyro conversion now retains floating-point fractions. Each accepted event contains only its own frame data instead of updating a shared structure containing fields of different ages. Default scaling corresponds to +/-16 g and +/-2000 degrees/s. Temperature and version-word interpretation match the supplied source and must be confirmed for each sensor model.
