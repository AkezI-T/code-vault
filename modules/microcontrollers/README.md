# 单片机模块

简体中文 | [English](README.en.md)

存放可复用的单片机固件模块，例如外设驱动、通信协议和控制算法。按芯片类别存放在 `<chip>/<module-name>/` 下，例如 `esp32-s3`、`mspm0g3507`、`k230`。每个模块自行管理源码和依赖。

文件和文件夹使用英文命名。模块名称描述功能，不带品牌名；适用的通信方式写入名称，例如巡线模块命名为 `i2c-line-sensor`。名称中的通信方式必须与实际实现一致。

每个 `<module-name>/` 根目录必须包含默认简体中文 `README.md` 和英文 `README.en.md`，两版内容保持一致，顶部提供双向语言切换链接。按 [docs/architecture.md](../../docs/architecture.md) 和 [docs/module-template.md](../../docs/module-template.md) 写清支持的芯片与框架、接线与配置、完整调用示例，以及每个公开 API 的签名、参数、返回值、错误和调用约束。

## 按芯片分类

| 芯片 | 模块 | 语言 | 职责 |
| --- | --- | --- | --- |
| esp32-s3 | [uart](esp32-s3/uart/README.md) | C | 可配置 UART 收发与生命周期 |
| esp32-s3 | [ws2812](esp32-s3/ws2812/README.md) | C | RMT 驱动整条灯带统一颜色 |
| mspm0g3507 | [async-i2c](mspm0g3507/async-i2c/README.md) | C | 实例化非阻塞 I2C 事务队列 |
| mspm0g3507 | [button](mspm0g3507/button/README.md) | C | 消抖、短按、双击与长按事件 |
| mspm0g3507 | [i2c-line-sensor](mspm0g3507/i2c-line-sensor/README.md) | C | I2C 八路巡线数据读取 |
| mspm0g3507 | [i2c-jy901s](mspm0g3507/i2c-jy901s/README.md) | C | I2C JY901S 姿态角读取 |
| k230 | [alpha-beta-tracker](k230/alpha-beta-tracker/README.md) | Python | 可配置一维位置与速度估计 |

电脑端使用的开发工具放在 [tools/microcontrollers/](../../tools/microcontrollers/README.md)。
