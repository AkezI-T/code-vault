# 通信协议

简体中文 | [English](README.en.md)

跨芯片、跨语言的通信协议契约与可移植编解码器，不依赖具体外设。

## 模块

| 模块 | 语言 | 职责 |
| --- | --- | --- |
| [uart-frame-protocol](uart-frame-protocol/README.md) | C + Python | 通用 UART 帧传输：定长头 + 变长载荷 + CRC8，含编码器与逐字节接收状态机 |

文件存放规则见 [docs/architecture.md](../docs/architecture.md)，模块使用说明模板见 [docs/module-template.md](../docs/module-template.md)。
