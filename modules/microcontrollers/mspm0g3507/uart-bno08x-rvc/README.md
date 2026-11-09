# uart-bno08x-rvc

简体中文 | [English](README.en.md)

解析 BNO08x UART-RVC 的 19 字节姿态/加速度帧。逐字节处理任意分片、连续帧和输入噪声，校验成功时生成一份完整样本。适用于 MSPM0G3507，也可在具备 C99/C11 标准库和固定宽度整数的主机/其他 MCU 使用；不依赖 TI SDK、UART、DMA、GPIO、时钟或中断向量。

## 接入

1. 把 [bno08x_rvc.c](src/bno08x_rvc.c) 加入编译，把 [头文件目录](include/bno08x_rvc.h) 加入包含路径。
2. 每个独立字节流分配一个 `bno08x_rvc_parser_t`，首先调用 `bno08x_rvc_init`。
3. 将 UART FIFO、DMA 环形缓冲或文件中的字节按接收顺序调用 `bno08x_rvc_feed_byte`。DMA 块边界无需与协议帧对齐；不得漏字节或重复提交。
4. 只有返回 `BNO08X_RVC_SAMPLE` 时读取输出样本。链路重启或接收溢出后可重新调用 init 丢弃半帧。

实际传感器须配置成 UART-RVC 模式，典型串口为 115200、8N1；引脚、电平、波特率、FIFO/DMA/IRQ及接收缓冲的所有权由应用和传感器手册确定。模块不配置硬件。

## 完整主机示例

将以下程序保存为本模块目录的 `main.c`。程序动态计算校验和并逐字节提交完整帧，无硬件要求。预期输出 index=1、yaw=12.34°、ax=1000 mg。

```c
#include "bno08x_rvc.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

int main(void)
{
    bno08x_rvc_parser_t parser;
    bno08x_rvc_sample_t sample;
    uint8_t frame[BNO08X_RVC_FRAME_SIZE] = {
        0xAA, 0xAA, 1, 0xD2, 0x04, 0, 0, 0, 0,
        0xE8, 0x03, 0, 0, 0, 0, 1, 2, 3, 0
    };
    size_t i;
    uint8_t checksum = 0;
    if (bno08x_rvc_init(&parser) != 0) {
        return 1;
    }
    for (i = 2; i < sizeof(frame) - 1; ++i) {
        checksum = (uint8_t)(checksum + frame[i]);
    }
    frame[sizeof(frame) - 1] = checksum;
    for (i = 0; i < sizeof(frame); ++i) {
        bno08x_rvc_event_t event = bno08x_rvc_feed_byte(&parser, frame[i], &sample);
        if (event == BNO08X_RVC_SAMPLE) {
            printf("index=%u yaw=%.2f deg ax=%.1f mg\n",
                   (unsigned)sample.index, (double)sample.yaw_deg,
                   (double)sample.accel_mg[0]);
        } else if (event == BNO08X_RVC_BAD_CHECKSUM ||
                   event == BNO08X_RVC_INVALID_ARGUMENT) {
            return 2;
        }
    }
    return 0;
}
```

在本模块目录使用标准 C 编译器运行：

```sh
cc -std=c99 -Wall -Wextra -pedantic -Iinclude src/bno08x_rvc.c main.c -o rvc-example
./rvc-example
```

Windows 编译器可能生成 `rvc-example.exe`。C11 可将选项改为 `-std=c11`；不需要数学库或 SDK。固件中用真实接收字节替换示例数组循环。

## API

在 [bno08x_rvc.h](include/bno08x_rvc.h) 中声明：

```c
int bno08x_rvc_init(bno08x_rvc_parser_t *parser);
bno08x_rvc_event_t bno08x_rvc_feed_byte(bno08x_rvc_parser_t *parser,
                                      uint8_t byte,
                                      bno08x_rvc_sample_t *sample);
```

### bno08x_rvc_init

`parser` 是调用方拥有的可写实例指针，必须非空；无其他配置。成功返回 0，清零缓冲和 `used`，可用于首次初始化或重置。空指针返回 -1。成功返回前实例可安全接收字节；函数不修改独立的样本对象。

### bno08x_rvc_feed_byte

`parser` 必须由 init 成功初始化，`byte` 为当前接收的一个 0…255 字节，`sample` 为非空、可写的调用方输出对象。parser 和 sample 不得重叠，调用后模块不保存 sample 指针。每次调用只消费一个字节，返回事件如下：

| 枚举值 | 数值 | 含义与输出效果 |
| --- | --- | --- |
| `BNO08X_RVC_INVALID_ARGUMENT` | -2 | 空 parser/sample 或实例 used 越界；不消费输入、不修改输出 |
| `BNO08X_RVC_BAD_CHECKSUM` | -1 | 一个完整候选帧校验失败；保留后续候选头，不修改输出 |
| `BNO08X_RVC_NONE` | 0 | 噪声、头部搜索或未完整帧；不修改输出 |
| `BNO08X_RVC_SAMPLE` | 1 | 帧头和校验通过，完整覆盖 sample，缓冲回到等待帧头 |

错误事件可计数并继续提交后续字节；参数错误应修正指针，实例损坏时重新 init。非成功事件保留旧输出，旧值不可视为新样本。

## 公开类型与字段

`BNO08X_RVC_FRAME_SIZE = 19u` 为帧字节数。

| 类型/字段 | 单位、范围与含义 |
| --- | --- |
| `bno08x_rvc_parser_t.buffer[19]` | 内部接收缓冲，`uint8_t`；仅模块修改 |
| `bno08x_rvc_parser_t.used` | `uint8_t`，0…18 为持久状态，记录已收候选字节数；仅模块修改 |
| `bno08x_rvc_sample_t.index` | `uint8_t`，0…255，传感器帧序号原值；模块不检查丢帧或重复 |
| `yaw_deg / pitch_deg / roll_deg` | `float`，度；各自原始有符号 16 位值 ×0.01，编码范围 -327.68…327.67，不做归一化 |
| `accel_mg[3]` | `float`，X/Y/Z 加速度，mg（1 mg = 0.001 g）；编码范围 -32768…32767 mg |
| `reserved[3]` | `uint8_t`，帧15…17的原始保留字节，0…255；不解释状态语义 |

样本是校验成功的同一帧快照，各字段同时有效；没有持久缓存或额外有效位。样本是否足够新、index 是否重复以及物理量是否可信由应用判断，并可在接受事件时附加本地时间戳。

## 协议与恢复规则

所有双字节字段低字节在前，以有符号 16 位解码，避免依赖编译器对越界无符号转有符号的处理。

| 帧内偏移 | 内容 |
| --- | --- |
| 0…1 | `AA AA` 帧头 |
| 2 | index |
| 3…8 | yaw、pitch、roll，各2字节 |
| 9…14 | ax、ay、az，各2字节，mg |
| 15…17 | 保留数据，仍参与校验 |
| 18 | 字节2…17累加后模256 |

在未收满19字节前，不因负载内出现帧头而提前重启。坏帧后搜索缓冲内偏移1起最早的 `AA AA` 并保留其后缀；没有完整头时保留末尾单个 `AA`。因此 `AA AA AA` 的重叠候选不会被全部丢弃。输入不可靠时可能产生连续坏帧事件，直到收齐真实帧。合法帧接受后整帧消费，不把合法负载中的头部重复作为新帧。

校验范围包含三个保留字节；校验和不是 CRC，也不保证检测所有噪声。无超时：只要实例仍存在，半帧可跨任意时间保存；需要时间边界时由应用主动 init。

## 所有权与限制

所有状态保存在调用方实例，固定缓冲19字节，结构实际大小使用 `sizeof`。无堆分配、阻塞、回调或外设资源，无 deinit。不同实例互不影响；同一实例的 init/feed必须串行调用，不可重入。可在应用选择的接收上下文调用，但浮点计算、结构复制和缓冲移动会增加 ISR 工作量；中断与任务共享实例/样本时由应用同步。

只支持 UART-RVC，不支持 SHTP、传感器配置命令或多种消息格式。软件不进行姿态校准、单位范围约束、超时或样本缓存。
