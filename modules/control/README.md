# 控制与运动算法

简体中文 | [English](README.en.md)

芯片无关的控制与运动算法模块，运行在任意带整数运算的 MCU 上，不依赖具体外设。

## 模块

| 模块 | 语言 | 职责 |
| --- | --- | --- |
| [pid-controller](pid-controller/README.md) | C | 定点 PID 控制器：比例、积分、测量值微分、死区、限幅、低通与方向 |

文件存放规则见 [docs/architecture.md](../docs/architecture.md)，模块使用说明模板见 [docs/module-template.md](../docs/module-template.md)。
