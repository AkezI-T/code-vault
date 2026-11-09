# 模块目录

简体中文 | [English](README.en.md)

将可复用代码放在 `<domain>/<module-name>/` 下。每个模块自行管理源码、公开入口、依赖清单和中英文 README。

## 功能领域

| 位置 | 内容 |
| --- | --- |
| [microcontrollers/](microcontrollers/README.md) | 可复用的单片机固件模块 |

## 模块

| 芯片 | 模块 | 语言 | 职责 |
| --- | --- | --- | --- |
| esp32-s3 | [uart](microcontrollers/esp32-s3/uart/README.md) | C | 可配置 UART 收发与生命周期 |
| esp32-s3 | [ws2812](microcontrollers/esp32-s3/ws2812/README.md) | C | RMT 驱动整条灯带统一颜色 |
| mspm0g3507 | [async-i2c](microcontrollers/mspm0g3507/async-i2c/README.md) | C | 实例化非阻塞 I2C 事务队列 |
| mspm0g3507 | [button](microcontrollers/mspm0g3507/button/README.md) | C | 消抖、短按、双击与长按事件 |
| mspm0g3507 | [i2c-line-sensor](microcontrollers/mspm0g3507/i2c-line-sensor/README.md) | C | I2C 八路巡线数据读取 |
| mspm0g3507 | [i2c-jy901s](microcontrollers/mspm0g3507/i2c-jy901s/README.md) | C | I2C JY901S 姿态角读取 |
| mspm0g3507 | [uart-bno08x-rvc](microcontrollers/mspm0g3507/uart-bno08x-rvc/README.md) | C | UART RVC 字节流解析 |
| mspm0g3507 | [uart-imu-55](microcontrollers/mspm0g3507/uart-imu-55/README.md) | C | UART 0x55 IMU 数据帧解析 |
| mspm0g3507 | [gpio-ultrasonic](microcontrollers/mspm0g3507/gpio-ultrasonic/README.md) | C | 可配置 GPIO 超声波测距 |
| mspm0g3507 | [timer-ultrasonic](microcontrollers/mspm0g3507/timer-ultrasonic/README.md) | C | 捕获脉宽与距离换算 |
| mspm0g3507 | [i2c-spi-ssd1306](microcontrollers/mspm0g3507/i2c-spi-ssd1306/README.md) | C | 可接入 I2C/SPI 的 SSD1306 显示核心 |
| k230 | [alpha-beta-tracker](microcontrollers/k230/alpha-beta-tracker/README.md) | Python | 可配置一维位置与速度估计 |

文件存放规则见 [docs/architecture.md](../docs/architecture.md)，模块使用说明模板见 [docs/module-template.md](../docs/module-template.md)。
