# uart-frame-protocol

简体中文 | [English](README.en.md)

通用 UART 帧传输协议：定长 4 字节头 + 变长载荷 + 1 字节 CRC8，长度定界、二进制安全，附带逐字节喂入的接收状态机。适用于 MCU 与主控之间传送小消息（目标坐标、IMU 数据、命令等），发送端和接收端不必用同一种语言，也不必是同一块芯片。

传输层只负责「把 TYPE/LEN/SEQ/PAYLOAD 装成帧、校验 CRC、拆回字段」，不解释载荷含义；载荷长什么样，由下面的消息注册表定义。新增一种消息只需在注册表加一行，传输层代码不动。

## 环境要求

- 芯片：任意带 UART 的 MCU。C 实现仅依赖 `stdint.h` 与 `stddef.h`，无平台、无操作系统、无动态内存。
- 框架与工具链：C99 编译器（已在 arm-none-eabi-gcc 16.1.0 / Cortex-M3 与主机 gcc 16.1.0 上编译通过）；Python 3.7 及以上（标准库，无第三方包）。
- 依赖：无。协议本身不需要同仓库其他模块。

## 帧格式

一帧由 6 个字段组成，总长 = 5 + LEN 字节：

```
+------+------+------+------+---------------------+------+
| SOF  | TYPE | LEN  | SEQ  | PAYLOAD (LEN 字节)  | CRC8 |
| 0xAA | 1 B  | 1 B  | 1 B  | 0 ~ 255 B           | 1 B  |
+------+------+------+------+---------------------+------+
 偏移 0    1      2      3     4 .. 3+LEN           4+LEN
```

| 字段 | 长度 | 字节序 | 取值 | 说明 |
| --- | --- | --- | --- | --- |
| SOF | 1 B | 无（单字节） | 固定 `0xAA` | 帧起始。接收端在 IDLE 态丢弃非 `0xAA` 字节 |
| TYPE | 1 B | 无（单字节） | `0x00` ~ `0xFF` | 消息类型，见消息注册表；`0x00` 保留为 ACK |
| LEN | 1 B | 无（单字节） | `0` ~ `255` | 只计载荷字节数，是唯一的定界依据 |
| SEQ | 1 B | 无（单字节） | `0` ~ `255`，回绕 | 发送端每帧 +1；在 ACK 帧中表示被确认帧的 SEQ |
| PAYLOAD | LEN B | 多字节字段一律小端 LE | 任意字节 | 二进制安全：不转义，载荷内允许出现 `0xAA` |
| CRC8 | 1 B | 无（单字节） | `0x00` ~ `0xFF` | 校验范围为 SOF 到 PAYLOAD 末字节，不含 CRC 字节自身 |

定界方式是长度定界：收到 LEN 后按计数继续收 LEN 字节载荷，再收 1 字节 CRC。因此 `0xAA` 只在 IDLE 态具有「帧起始」语义，载荷里出现的 `0xAA` 不会被误判。

单帧最大长度 = 4 + 255 + 1 = 260 字节。

## CRC8 参数与校验向量

| 参数 | 值 |
| --- | --- |
| 算法名 | CRC-8/SMBus |
| 多项式 | `0x07`（x^8 + x^2 + x + 1） |
| 初值 | `0x00` |
| 输入反射 | 否（MSB-first） |
| 输出反射 | 否 |
| 最终异或 | `0x00` |
| 计算范围 | `SOF` 起，至 `PAYLOAD` 最后一个字节（共 4 + LEN 字节） |
| 校验向量 | ASCII `"123456789"` → `0xF4` |

实现使用逐位算法，不占查表空间；C 与 Python 两端结果一致。

## 接收状态机

```
       非 0xAA 丢弃
   ┌───────────────────┐
   ▼                   │
 [IDLE] ──0xAA──▶ [HEADER] ──收满 TYPE/LEN/SEQ──▶ [BODY] ──收满 LEN+1 字节──▶ 校验
   ▲                   │                            │                        │
   │                   └────── 空闲超时 ────────────┘                        │
   └───────────── CRC 失败丢弃 / 帧校验通过产出 ─────────────────────────────┘
```

| 当前状态 | 输入 | 动作 | 下一状态 |
| --- | --- | --- | --- |
| IDLE | 字节 = `0xAA` | 记下帧起始 | HEADER |
| IDLE | 其他字节 | 丢弃 | IDLE |
| HEADER | 第 1 ~ 3 个字节 | 依次存入 TYPE、LEN、SEQ | 收满 3 字节后进 BODY |
| BODY | 前 LEN 个字节 | 存入载荷缓冲区 | BODY |
| BODY | 第 LEN+1 个字节 | 与本地计算的 CRC 比较 | 一致：产出帧并回 IDLE；不一致：丢弃整帧并回 IDLE |
| 任意 | 空闲超过配置的超时值 | 丢弃半帧 | IDLE |

超时用于防止半帧卡死：例如线路只送来一个 `0xAA`，解码器会卡在 BODY 态，一直等一个永远不会来的字节。模块不持有定时器，调用方在空闲检测里调用 `ufp_decoder_reset()`（Python 侧 `Decoder.reset()`）即可回到 IDLE。超时值由应用配置，建议不小于 3 个字节的传输时间。

## ACK 机制

`TYPE = 0x00` 保留为 ACK。ACK 帧的 `SEQ` 字段填被确认帧的 SEQ，载荷固定 1 字节 status：

```
AA 00 01 <被确认帧的 SEQ> <status> <CRC8>      LEN = 1，status 0x00 表示 OK
```

哪些消息需要 ACK 由注册表逐类型标记，而不是由传输层硬编码：

- 流式消息（坐标、IMU 数据这类）不需要 ACK，丢了就等下一帧，避免为每帧往返一次。
- 可靠消息（`CMD`）需要 ACK。
- ACK 自身不需要再被确认。

传输层只提供「标记 + 状态回传」，不实现自动重传、超时重发和去重缓存。发送方按注册表的 `needs_ack` 决定是否等待 ACK 以及如何处理超时；收到 status 非 0 的 ACK 时如何处置也由上层决定。

## SEQ 与丢帧、重复、乱序

发送端每发出一帧就把 SEQ 加 1 并按 0~255 回绕。接收端把收到的 SEQ 与自己维护的期望值比较：

- 等于期望值：按序到达，期望值加 1。
- 大于期望值（模 256 差值 d）：中间丢了 d 帧。
- 等于上一次收到的 SEQ：重复帧。
- 在 128 帧窗口内「倒退」：乱序到达。

传输层只负责携带 SEQ、不做判断，也不维护每条链路的接收状态表；上述比较由应用层实现，这样同一条链路可以并行承载多个独立序号空间。

## LEN 扩展预留

本版 LEN 固定 1 字节，载荷上限 255 字节。当需要更大载荷时的预留方案：

- 把 `TYPE` 最高位（`0x80`）定义为扩展标志。该位置 1 时，LEN 字段改为 2 字节小端长度（0~65535），帧结构相应延长 1 字节；未置位时保持本版 1 字节 LEN，旧节点可继续工作。
- 备选方案是引入版本字节，但会改变所有帧的偏移，因此优先保留 `TYPE` 最高位。

本版不实现该扩展：`TYPE` 的最高位没有特殊含义，收发两端都不解析扩展帧。传输层不校验注册表，未注册的 TYPE 会被正常拆包后交给上层，上层应按「未知类型」丢弃。

## 消息注册表

注册表是「数据」，不是代码分支。加一种消息 = 加一行注册表，传输层不动。

| TYPE | 名称 | 需 ACK | 载荷长度 | 载荷 schema | 说明 |
| --- | --- | --- | --- | --- | --- |
| `0x00` | ACK | 否（自身不再确认） | 1 B | `status(1B)`，`0x00` = OK | SEQ 字段填被确认帧的 SEQ |
| `0x01` | AIM_TARGET | 否（流式） | 6 B | `dx(int16 LE), dy(int16 LE), area(uint16 LE)` | 示例消息（仅演示），`dx`/`dy` 为相对画面中心的偏移 |
| `0x02` | IMU_DATA | 否（流式） | 预留 | 未定义 | 后续按实际传感器输出定义 |
| `0x03` | CMD | 是（可靠） | 预留 | 未定义；示例 `cmd(1B) + value(uint16 LE)` | 需 ACK 确认 |

**AIM_TARGET 只是示例消息，不代表实际使用。** 它的 6 字节布局（两个 int16 坐标 + 一个 uint16 面积/尺度，均小端）用来演示帧格式，`dx`/`dy` 按「相对画面中心的偏移」理解即可；实际要用什么消息，按上面「怎么新增一条消息」自己加。

同一张表在代码里的落点：

- C：`include/uart_frame_protocol.h` 里的 `UFP_TYPE_*` 常量。
- Python：`src/uart_frame_protocol.py` 里的 `TYPE_*` 常量与 `MESSAGE_REGISTRY`（含名称、是否需 ACK、固定载荷长度、schema 描述）。

编解码器本身不读注册表：`pack`/`unpack` 与 C 的 `ufp_pack`/`ufp_unpack` 对任何 TYPE 一视同仁，只做装箱与校验。

### 每一列是什么意思

- **TYPE**：消息编号，0~255。`0x00` 固定留给 ACK，其余按需分配；线上只传这一个字节，不传名称。
- **名称**：给人看的名字，只出现在文档和代码常量里。
- **需 ACK**：这条消息丢了要不要重发。流式数据（坐标、IMU）填否——丢了等下一帧；指令类填是。
- **载荷长度**：载荷的定长字节数；变长或还没定的写「预留」。
- **载荷 schema**：载荷里每个字段的名字、类型、字节序。这一列才是真正定义「载荷长什么样」的地方。
- **说明**：补充约定，比如 ACK 的 SEQ 填什么。

### 怎么新增一条消息

加消息只动注册表，帧格式、CRC、状态机都不用改。以加一条心跳 `PING` 为例：

1. 挑一个没占用的 TYPE，比如 `0x10`。
2. 定三件事：名字 `PING`、要不要 ACK（心跳一般不用）、载荷布局（这里用空载荷，`LEN=0`）。
3. C 侧在 `uart_frame_protocol.h` 加 `#define UFP_TYPE_PING 0x10`。
4. Python 侧在 `uart_frame_protocol.py` 加 `TYPE_PING = 0x10`，并在 `MESSAGE_REGISTRY` 里加一条 `MessageSpec(type_id=0x10, name="PING", needs_ack=False, payload_len=0, schema="无载荷")`。
5. 收发两端各自按 schema 处理载荷即可，传输层和编解码器一行都不用动。

## 接线与配置

UART 侧只需要常规三线连接：TX 接对端 RX，RX 接对端 TX，并共地，电平按两端芯片要求（常见 3.3 V）。协议不规定引脚、也不用硬件流控。

| 配置项 | 类型 | 单位与有效范围 | 默认值 | 是否必需 | 说明 |
| --- | --- | --- | --- | --- | --- |
| 波特率 | `uint32_t` | 常见 9600 ~ 921600 | 示例 115200 | 是 | 两端必须一致；上限取决于 UART 外设 |
| 数据位 / 校验 / 停止位 | — | 8 / 无 / 1 | 8N1 | 是 | 帧内已有 CRC8，通常不需要串口校验位 |
| 空闲超时 | `uint32_t` | 毫秒，建议不小于 3 字节传输时间 | 示例 20 ms | 是 | 超时后调用解码器 reset，丢弃半帧 |
| 发送 SEQ 初值 | `uint8_t` | `0` ~ `255` | `0` | 是 | 每个发送方向各自维护，每帧 +1 回绕 |
| 接收超时检查周期 | — | 毫秒，建议 1 ~ 10 | 示例 5 ms | 否 | 周期任务轮询最后一次收字节的时间 |

## 接入步骤

C 侧（header-only，无需构建配置）：

1. 把 `include/uart_frame_protocol.h` 拷进工程，编译选项至少 `-std=c99`；不需要额外的 `.c` 文件或链接步骤。
2. 每个 UART 端口定义一个 `ufp_decoder_t`（约 264 字节），上电调用 `ufp_decoder_init()`。
3. 在 UART 接收中断或接收回调里对每个字节调用 `ufp_decoder_feed()`；返回 1 时 `decoder.frame` 里就是完整帧，应立即复制到队列，不要在中断里做耗时处理。
4. 发送侧维护一个 `uint8_t` SEQ 计数器，填好载荷后调用 `ufp_pack()`，再把写出的字节交给自己平台的 UART 发送函数。
5. 空闲超时检测到线路静默后调用 `ufp_decoder_reset()`。

Python 侧：

1. 把 `src/uart_frame_protocol.py` 加入工程（或加进 `sys.path`），无第三方依赖。
2. 发送用 `pack()`；整块缓冲用 `unpack()`，逐字节或分片到达用 `Decoder`。
3. 长时间没有新字节时调用 `Decoder.reset()`。

## 最小使用示例

C：发送一帧 AIM_TARGET，并在接收中断里逐字节解码。

```c
#include <stdint.h>
#include "uart_frame_protocol.h"

/* 平台适配层提供，按实际工程替换。 */
int board_uart_write(const uint8_t *data, size_t len);
void board_handle_cmd(uint8_t cmd, uint16_t value);

static ufp_decoder_t g_decoder;
static uint8_t g_tx_seq;

void link_init(void)
{
    ufp_decoder_init(&g_decoder);
    g_tx_seq = 0u;
}

/* 线路静默超时后调用，丢弃半帧。 */
void link_idle_timeout(void)
{
    ufp_decoder_reset(&g_decoder);
}

static void put_u16le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

/* 载荷布局来自注册表：dx(int16 LE) + dy(int16 LE) + area(uint16 LE)。 */
int link_send_aim_target(int16_t dx, int16_t dy, uint16_t area)
{
    uint8_t payload[6];
    uint8_t frame[UFP_FRAME_MAX];
    size_t frame_len = 0u;
    int status;

    put_u16le(&payload[0], (uint16_t)dx);
    put_u16le(&payload[2], (uint16_t)dy);
    put_u16le(&payload[4], area);

    status = ufp_pack(UFP_TYPE_AIM_TARGET, g_tx_seq, payload, sizeof(payload),
                      frame, sizeof(frame), &frame_len);
    if (status != UFP_OK) {
        return status;
    }
    g_tx_seq = (uint8_t)(g_tx_seq + 1u); /* 0~255 回绕 */

    /* board_uart_write() 换成自己平台的发送函数。 */
    return board_uart_write(frame, frame_len) == (int)frame_len ? UFP_OK : -1;
}

static void on_frame(uint8_t type, uint8_t seq, const uint8_t *payload, uint8_t payload_len);

/* UART 接收中断里逐字节调用。 */
void link_rx_byte(uint8_t byte)
{
    if (ufp_decoder_feed(&g_decoder, byte) == 1) {
        on_frame(g_decoder.frame.type, g_decoder.frame.seq,
                 g_decoder.frame.payload, g_decoder.frame.payload_len);
    }
}

static void on_frame(uint8_t type, uint8_t seq, const uint8_t *payload, uint8_t payload_len)
{
    if (type == UFP_TYPE_CMD && payload_len == 3u) {
        /* 注册表里的示例 schema：cmd(1B) + value(uint16 LE)。 */
        uint8_t cmd = payload[0];
        uint16_t value = (uint16_t)((uint16_t)payload[1] | ((uint16_t)payload[2] << 8));
        uint8_t ack_status = 0x00u;
        uint8_t ack[UFP_FRAME_MAX];
        size_t ack_len = 0u;

        board_handle_cmd(cmd, value);

        /* ACK 的 SEQ 填被确认帧的 SEQ，载荷是 1 字节 status。 */
        if (ufp_pack(UFP_TYPE_ACK, seq, &ack_status, 1u, ack, sizeof(ack), &ack_len) == UFP_OK) {
            board_uart_write(ack, ack_len);
        }
    }
}
```

Python：编码 G2 黄金帧，再流式解码回来。

```python
import struct

import uart_frame_protocol as ufp

seq = 1  # G2 的 SEQ
decoder = ufp.Decoder()


def encode_aim_target(dx, dy, area):
    global seq
    frame = ufp.pack(ufp.TYPE_AIM_TARGET, seq, struct.pack("<hhH", dx, dy, area))
    seq = (seq + 1) % 256
    return frame


# 分片到达也要能拼回完整帧，这里故意按 3 字节一段喂入。
stream = encode_aim_target(10, -20, 4500)
for start in range(0, len(stream), 3):
    for frame in decoder.feed_bytes(stream[start:start + 3]):
        spec = ufp.MESSAGE_REGISTRY[frame.type]
        print(spec.name, frame.seq, struct.unpack("<hhH", frame.payload))
# AIM_TARGET 1 (10, -20, 4500)
```

## API 使用说明

### ufp_crc8

**签名**

```c
uint8_t ufp_crc8(const uint8_t *data, size_t len);
```

**用途与调用前提**

计算 CRC-8/SMBus。无前置初始化，任意上下文可调用。

**参数**

| 参数 | 类型与方向 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| `data` | `const uint8_t *`；输入 | 长度由 `len` 指定 | 待校验字节；`NULL` 时按 0 字节处理并返回 `0x00` |
| `len` | `size_t`；输入 | `0` ~ `SIZE_MAX` | 字节数 |

**返回值与错误处理**

返回 CRC 值（`0x00` ~ `0xFF`）。无错误码。

**调用约束**

无全局状态、可重入，可在中断中调用；计算时间正比于 `len`。

**调用示例**

```c
static const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
uint8_t crc = ufp_crc8(check, sizeof(check)); /* 0xF4 */
```

### ufp_crc8_update

**签名**

```c
uint8_t ufp_crc8_update(uint8_t crc, const uint8_t *data, size_t len);
```

**用途与调用前提**

在已有 CRC 基础上继续累加字节，用于分片计算；初值传 `0x00`。

**参数**

| 参数 | 类型与方向 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| `crc` | `uint8_t`；输入 | `0x00` ~ `0xFF` | 上一次的 CRC 结果，首次调用传 `0x00` |
| `data` | `const uint8_t *`；输入 | 长度由 `len` 指定 | 待累加字节；`NULL` 时原样返回 `crc` |
| `len` | `size_t`；输入 | `0` ~ `SIZE_MAX` | 字节数 |

**返回值与错误处理**

返回累加后的 CRC。无错误码。

**调用约束**

无全局状态、可重入。

**调用示例**

```c
uint8_t crc = ufp_crc8_update(0x00u, header, 4u);
crc = ufp_crc8_update(crc, payload, payload_len);
```

### ufp_pack

**签名**

```c
int ufp_pack(uint8_t type, uint8_t seq, const uint8_t *payload,
             size_t payload_len, uint8_t *out,
             size_t out_capacity, size_t *out_len);
```

**用途与调用前提**

把类型、序号与载荷装成一帧并追加 CRC。对载荷内容不做任何解释。

**参数**

| 参数 | 类型与方向 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| `type` | `uint8_t`；输入 | `0x00` ~ `0xFF` | 消息类型 |
| `seq` | `uint8_t`；输入 | `0x00` ~ `0xFF` | 帧序号，调用方维护 |
| `payload` | `const uint8_t *`；输入 | `NULL` 仅当 `payload_len == 0` | 载荷字节，允许含 `0xAA` |
| `payload_len` | `size_t`；输入 | `0` ~ `255` | 载荷字节数；超过 255 返回 `UFP_ERR_ARG` |
| `out` | `uint8_t *`；输出 | 容量不少于 `5 + payload_len` | 帧输出缓冲区 |
| `out_capacity` | `size_t`；输入 | `0` ~ `SIZE_MAX` | `out` 的字节容量 |
| `out_len` | `size_t *`；输出 | 指向一个 `size_t` | 成功时写入帧总长 |

**返回值与错误处理**

`UFP_OK`（0）成功；`UFP_ERR_ARG`（-1）指针为空或 `payload_len` 超限；`UFP_ERR_CAPACITY`（-2）缓冲区放不下整帧。失败时 `*out_len` 不被修改，调用方应把整帧丢弃。

**调用约束**

无全局状态、可重入、可在中断中调用。输出缓冲区归调用方所有。

**调用示例**

```c
uint8_t payload[6] = {0};
uint8_t frame[UFP_FRAME_MAX];
size_t frame_len = 0u;
if (ufp_pack(UFP_TYPE_AIM_TARGET, 0u, payload, sizeof(payload),
             frame, sizeof(frame), &frame_len) == UFP_OK) {
    /* frame_len == 11，内容为黄金帧 G1 */
}
```

### ufp_unpack

**签名**

```c
int ufp_unpack(const uint8_t *frame, size_t frame_len, ufp_frame_t *out_frame);
```

**用途与调用前提**

校验并拆解一帧完整数据：检查 SOF、LEN 与帧长是否一致、CRC 是否正确。

**参数**

| 参数 | 类型与方向 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| `frame` | `const uint8_t *`；输入 | 至少 5 字节 | 完整帧缓冲 |
| `frame_len` | `size_t`；输入 | `0` ~ `260` | 缓冲中的帧字节数，必须等于 `5 + LEN` |
| `out_frame` | `ufp_frame_t *`；输出 | 非空 | 成功时填入 type/seq/payload/payload_len |

**返回值与错误处理**

`UFP_OK` 成功；`UFP_ERR_ARG`（-1）指针为空；`UFP_ERR_LENGTH`（-4）长度不足或与 LEN 不一致；`UFP_ERR_SOF`（-3）首字节不是 `0xAA`；`UFP_ERR_CRC`（-5）校验失败。失败时 `*out_frame` 内容不确定，调用方应丢弃。

**调用约束**

无全局状态、可重入。成功时 `out_frame->payload` 指向传入的 `frame` 缓冲区内部，只在 `frame` 存活期间有效；载荷长度为 0 时该指针为 `NULL`。

**调用示例**

```c
ufp_frame_t frame;
if (ufp_unpack(rx_buffer, rx_len, &frame) == UFP_OK) {
    if (frame.type == UFP_TYPE_ACK && frame.payload_len == 1u) {
        uint8_t status = frame.payload[0];
    }
}
```

### ufp_decoder_init

**签名**

```c
void ufp_decoder_init(ufp_decoder_t *decoder);
```

**用途与调用前提**

把解码器置为 IDLE，清空半帧状态。使用前调用一次；`NULL` 安全（直接返回）。

**参数**

| 参数 | 类型与方向 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| `decoder` | `ufp_decoder_t *`；输入输出 | 非必须非空 | 解码器实例，每个 UART 端口一个 |

**返回值与错误处理**

无返回值。

**调用约束**

非可重入（会写 `decoder`）；在接收中断开始前调用，避免与 `ufp_decoder_feed` 并发。

**调用示例**

```c
static ufp_decoder_t decoder;

void link_init(void)
{
    ufp_decoder_init(&decoder);
}
```

### ufp_decoder_reset

**签名**

```c
void ufp_decoder_reset(ufp_decoder_t *decoder);
```

**用途与调用前提**

丢弃当前半帧并回到 IDLE，是空闲超时的重置入口。与 `ufp_decoder_init` 等价。

**参数**

| 参数 | 类型与方向 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| `decoder` | `ufp_decoder_t *`；输入输出 | 非必须非空 | 解码器实例 |

**返回值与错误处理**

无返回值。

**调用约束**

非可重入。只在确认线路静默（超过配置的空闲超时）后调用，否则会丢掉正在接收的合法帧。

**调用示例**

```c
if (now_ms - last_rx_ms > LINK_IDLE_TIMEOUT_MS) {
    ufp_decoder_reset(&decoder);
    last_rx_ms = now_ms;
}
```

### ufp_decoder_feed

**签名**

```c
int ufp_decoder_feed(ufp_decoder_t *decoder, uint8_t byte);
```

**用途与调用前提**

把一个接收字节喂进状态机。应逐字节调用，顺序与线上到达顺序一致。

**参数**

| 参数 | 类型与方向 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| `decoder` | `ufp_decoder_t *`；输入输出 | 非空 | 已初始化的解码器 |
| `byte` | `uint8_t`；输入 | `0x00` ~ `0xFF` | 一个接收字节 |

**返回值与错误处理**

返回 1 表示一帧已通过 CRC 校验并完成，结果在 `decoder->frame`；返回 0 表示尚未完成、或该帧因 CRC 错误/噪声被丢弃。无错误码。

**调用约束**

非可重入（同一实例只能在一个上下文调用）。`decoder->frame.payload` 指向解码器内部缓冲区，下一帧到来即被覆盖，必须在返回 1 的当次拷贝出去。单次调用耗时与字节数无关，可以放在 UART 中断里。

**调用示例**

```c
void link_rx_byte(uint8_t byte)
{
    if (ufp_decoder_feed(&decoder, byte) == 1) {
        queue_push(&rx_queue, decoder.frame.type, decoder.frame.seq,
                   decoder.frame.payload, decoder.frame.payload_len);
    }
}
```

### ufp_frame_t

**签名**

```c
typedef struct {
    uint8_t type;
    uint8_t seq;
    const uint8_t *payload;
    uint8_t payload_len;
} ufp_frame_t;
```

**字段**

| 字段 | 类型 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| `type` | `uint8_t` | `0x00` ~ `0xFF` | 消息类型 |
| `seq` | `uint8_t` | `0x00` ~ `0xFF` | 帧序号 |
| `payload` | `const uint8_t *` | 长度 0 时为 `NULL` | 载荷指针，生命周期由来源（输入帧缓冲或解码器）决定 |
| `payload_len` | `uint8_t` | `0` ~ `255` | 载荷字节数 |

**调用约束**

`ufp_unpack` 产出的 `payload` 指向调用方传入的帧缓冲；`ufp_decoder_feed` 产出的指向解码器内部缓冲。两者都不分配内存。

### ufp_decoder_t

**签名**

```c
typedef struct {
    uint8_t state;
    uint8_t header[3];
    uint8_t header_len;
    uint8_t body_len;
    uint8_t payload[UFP_PAYLOAD_MAX];
    ufp_frame_t frame;
} ufp_decoder_t;
```

**字段**

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `state` | `uint8_t` | 当前状态，取值见 `ufp_decoder_state_t` |
| `header[3]` | `uint8_t[3]` | 收到的 TYPE、LEN、SEQ |
| `header_len` | `uint8_t` | 已收到的头部字节数（0 ~ 3） |
| `body_len` | `uint8_t` | 已收到的载荷字节数 |
| `payload[255]` | `uint8_t[255]` | 载荷缓冲区 |
| `frame` | `ufp_frame_t` | 最近一帧的拆解结果，返回 1 时有效 |

**调用约束**

结构体内容由解码器维护，调用方只读 `frame`。整型大小约 264 字节，按端口分配在静态区或任务栈外。

### Python：crc8 / pack / unpack

```python
def crc8(data: bytes) -> int
def pack(frame_type: int, seq: int, payload: bytes = b"") -> bytes
def unpack(frame: bytes) -> Frame
```

`crc8` 返回 `0` ~ `255` 的整数；`pack` 返回完整帧字节串，`frame_type`/`seq` 超出 `0` ~ `255` 或载荷超过 255 字节时抛 `ValueError`；`unpack` 返回 `Frame`，SOF 错误、长度与 LEN 不一致、CRC 错误时抛 `UartFrameProtocolError`（`ValueError` 子类）。三者都是纯函数，无共享状态。

| 参数 | 类型与方向 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| `data` | `bytes`；输入 | 任意长度 | 待校验字节 |
| `frame_type` | `int`；输入 | `0` ~ `255` | 消息类型 |
| `seq` | `int`；输入 | `0` ~ `255` | 帧序号 |
| `payload` | `bytes`；输入 | `0` ~ `255` 字节，默认 `b""` | 载荷，允许含 `0xAA` |
| `frame` | `bytes`；输入 | 至少 5 字节 | 完整帧 |

```python
import uart_frame_protocol as ufp

wire = ufp.pack(ufp.TYPE_ACK, 0x2A, b"\x00")
assert wire == bytes.fromhex("AA00012A0069")
frame = ufp.unpack(wire)
assert (frame.type, frame.seq, frame.payload) == (ufp.TYPE_ACK, 0x2A, b"\x00")
```

### Python：Frame

```python
@dataclass(frozen=True)
class Frame:
    type: int
    seq: int
    payload: bytes
```

不可变数据类。`payload` 是原样字节串，含义由注册表解释。

### Python：MessageSpec 与 MESSAGE_REGISTRY

```python
@dataclass(frozen=True)
class MessageSpec:
    type_id: int
    name: str
    needs_ack: bool
    payload_len: Optional[int]
    schema: str

MESSAGE_REGISTRY: Dict[int, MessageSpec]
```

注册表以 TYPE 为键。`payload_len` 是固定载荷字节数，`None` 表示预留或变长；`needs_ack` 标记可靠消息；`schema` 是给人看的字段描述。编解码器不读取该表，它只服务于收发两侧的语义层与测试。

### Python：Decoder

```python
class Decoder:
    def reset(self) -> None
    def feed(self, byte: int) -> Optional[Frame]
    def feed_bytes(self, data: bytes) -> List[Frame]
```

字节喂入式状态机，行为与 C 的 `ufp_decoder_feed` 一致：噪声、CRC 错误、半帧都被丢弃且不抛异常。`feed` 接收一个字节，完成一帧时返回 `Frame`，否则返回 `None`；`feed_bytes` 返回该批数据里完成的所有帧（按到达顺序）。`reset` 是空闲超时入口。`byte` 超出 `0` ~ `255` 抛 `ValueError`。

| 参数 | 类型与方向 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| `byte` | `int`；输入 | `0` ~ `255` | 一个接收字节 |
| `data` | `bytes`；输入 | 任意长度 | 一批接收字节，可分片 |

```python
decoder = ufp.Decoder()
frames = decoder.feed_bytes(b"\x00\x55\xBB" + ufp.pack(ufp.TYPE_IMU_DATA, 3))
assert len(frames) == 1
decoder.reset()
```

## 构建与检查

Python 单元测试（含 5 个黄金帧的 pack/unpack/CRC 逐字节断言）：

```bash
cd modules/protocols/uart-frame-protocol
python -m unittest discover -s tests -v
```

C 侧只做编译检查即可（header-only，没有单独的编译单元）：

```bash
cd modules/protocols/uart-frame-protocol
gcc -std=c99 -Wall -Wextra -pedantic -Werror -I include -c your_file.c -o your_file.o
```

黄金帧（两端共用同一组字节）：

| # | 字节 | 含义 |
| --- | --- | --- |
| G1 | `AA 01 06 00 00 00 00 00 B8 0B F3` | AIM_TARGET dx=0 dy=0 area=3000, SEQ=0 |
| G2 | `AA 01 06 01 0A 00 EC FF 94 11 82` | AIM_TARGET dx=+10 dy=-20 area=4500, SEQ=1 |
| G3 | `AA 01 06 05 00 00 00 00 00 00 8F` | AIM_TARGET 全零, SEQ=5 |
| G4 | `AA 00 01 2A 00 69` | ACK 确认 SEQ=0x2A, status=OK |
| G5 | `AA 03 03 10 01 19 00 43` | CMD cmd=1 value=25, SEQ=16 |

## 限制与资源

- 载荷上限 255 字节、帧上限 260 字节，来自 1 字节 LEN；更大载荷需要 LEN 扩展（本版未实现）。
- 不做转义、分片、流控和自动重传。ACK 只是标记与状态回传，重传、去重、超时策略由应用层实现。
- 不做多链路管理：每个 UART 端口一个解码器实例，序号空间由应用划分。
- 空闲超时不在协议层实现；调用方必须提供定时器并调用 reset，否则半帧会一直占着解码器。
- 只有 1 字节 CRC8，适合短帧与低噪声链路；错误检测能力未做误码率评估，高噪声链路应加应用层确认或更强校验。
- 不分配内存、不做 I/O，端口读写留给平台适配层。
- AIM_TARGET 仅为示例消息，不作为实际使用的类型；需要真实消息时按注册表说明新增。
- 无第三方代码与资源；C 与 Python 实现均在本仓库内独立完成。
