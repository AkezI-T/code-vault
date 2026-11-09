# uart-imu-55

简体中文 | [English](README.en.md)

解析以 `0x55` 开头的11字节 IMU 串口帧，支持 `0x51` 加速度/温度、`0x52` 角速度和 `0x53` 欧拉角/版本原始字。每次校验成功输出单帧事件，不把不同帧的数据组合成同一份新样本。适用于 MSPM0G3507，也可在提供 C99/C11 标准库与固定宽度整数的主机或其他 MCU 使用，无 TI SDK 或硬件依赖。

## 接入与量程配置

1. 将 [imu55.c](src/imu55.c) 加入编译，将 [imu55.h](include/imu55.h) 所在目录加入包含路径。
2. 每个独立字节流使用一个 `imu55_parser_t`；先调用 `imu55_init`，传入当前传感器协议使用的加速度/角速度满量程。
3. 按接收顺序逐字节调用 `imu55_feed_byte`，任意 UART/DMA/文件分片均可。只在 `IMU55_SAMPLE` 时读取本次样本，按 `valid_mask` 处理当前帧具有的数据。
4. 接收溢出、断流重启或量程改变后，重新 init 以丢弃半帧并设定正确比例。

默认宏表示 ±16 g 与 ±2000°/s，分别得到 0.48828125 mg/LSB 和 0.06103515625°/s/LSB，与此帧格式常用量程一致。应用必须确认设备文档：有些设备即使内部测量量程改变，串口输出仍使用固定协议比例。配置参数改变解码比例，不会发送配置命令或改变传感器本身。所有 UART 引脚、电平、波特率、FIFO/DMA/IRQ、缓冲生命周期由应用负责，模块不初始化外设。

## 完整主机示例

将下面程序保存为本模块目录的 `main.c`。它动态生成三帧校验和，分别输出加速度、角速度和角度；无硬件要求。数值为 ax=1000 mg、T=25°C、gx=1000°/s、roll=90°、version=0x1234。

```c
#include "imu55.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

int main(void)
{
    imu55_parser_t parser;
    imu55_sample_t sample;
    uint8_t frames[3][IMU55_FRAME_SIZE] = {
        {0x55, 0x51, 0x00, 0x08, 0, 0, 0, 0, 0xC4, 0x09, 0},
        {0x55, 0x52, 0x00, 0x40, 0, 0, 0, 0, 0, 0, 0},
        {0x55, 0x53, 0x00, 0x40, 0, 0, 0, 0, 0x34, 0x12, 0}
    };
    size_t frame;
    size_t i;
    if (imu55_init(&parser, IMU55_DEFAULT_ACCEL_RANGE_G,
                   IMU55_DEFAULT_GYRO_RANGE_DPS) != 0) {
        return 1;
    }
    for (frame = 0; frame < 3; ++frame) {
        uint8_t sum = 0;
        for (i = 0; i < IMU55_FRAME_SIZE - 1; ++i) {
            sum = (uint8_t)(sum + frames[frame][i]);
        }
        frames[frame][IMU55_FRAME_SIZE - 1] = sum;
        for (i = 0; i < IMU55_FRAME_SIZE; ++i) {
            imu55_event_t event = imu55_feed_byte(&parser, frames[frame][i], &sample);
            if (event == IMU55_SAMPLE) {
                if ((sample.valid_mask & IMU55_VALID_ACCEL) != 0) {
                    printf("ax=%.3f mg T=%.2f C\n",
                           (double)sample.accel_mg[0], (double)sample.temperature_c);
                } else if ((sample.valid_mask & IMU55_VALID_GYRO) != 0) {
                    printf("gx=%.3f deg/s\n", (double)sample.gyro_dps[0]);
                } else if ((sample.valid_mask & IMU55_VALID_ANGLES) != 0) {
                    printf("roll=%.3f deg version=0x%04X\n",
                           (double)sample.angles_deg[0], (unsigned)sample.version_raw);
                }
            } else if (event != IMU55_NONE) {
                return 2;
            }
        }
    }
    return 0;
}
```

在本模块目录编译运行：

```sh
cc -std=c99 -Wall -Wextra -pedantic -Iinclude src/imu55.c main.c -o imu55-example
./imu55-example
```

C11 可使用 `-std=c11`，Windows 可能生成 `imu55-example.exe`。无需 SDK 或数学库。固件中以真实串口接收字节替代示例数组。

## API

在 [imu55.h](include/imu55.h) 中声明：

```c
int imu55_init(imu55_parser_t *parser, float accel_range_g, float gyro_range_dps);
imu55_event_t imu55_feed_byte(imu55_parser_t *parser, uint8_t byte,
                             imu55_sample_t *sample);
```

### imu55_init

| 参数 | 类型/单位/有效范围 | 作用 |
| --- | --- | --- |
| `parser` | 非空 `imu55_parser_t *`，调用方可写实例 | 每流独立存储，不得与外部并发使用 |
| `accel_range_g` | `float`，g，有限正数 | 对应原始值32768的正满量程；内部 mg/LSB = range/32768×1000 |
| `gyro_range_dps` | `float`，°/s，有限正数 | 对应原始值32768的正满量程；内部 °/s/LSB = range/32768 |

返回0表示成功，清零缓冲及计数并保存比例。空指针、非正/NaN/无穷量程、计算比例为零/非有限或加速度比例过大（大于 `FLT_MAX / 32768`）时返回-1，实例保持原值。极小正量程也可能因浮点下溢被拒绝。init 可用于重置；失败后首次未初始化的实例不可 feed，应修正配置重试。成功重置不修改独立样本对象。

### imu55_feed_byte

`parser` 必须成功初始化，`byte` 是当前接收的0…255字节，`sample` 是非空可写输出。parser/sample对象不得重叠；模块不保存 sample 指针。返回：

| 枚举值 | 数值 | 含义与输出效果 |
| --- | --- | --- |
| `IMU55_INVALID_ARGUMENT` | -2 | 空指针、used越界或实例比例非正/非有限；不消费字节、不修改输出 |
| `IMU55_BAD_CHECKSUM` | -1 | 完整候选帧校验失败，保留后续候选帧头；不修改输出 |
| `IMU55_NONE` | 0 | 忽略噪声或帧未完整；不修改输出 |
| `IMU55_SAMPLE` | 1 | 已校验且类型支持，完整覆盖输出样本；只设置当前数据组有效位 |
| `IMU55_UNSUPPORTED_TYPE` | 2 | 完整且校验正确，但类型非0x51/52/53；消费整帧，不修改输出 |

校验失败可记录后继续提交字节，未支持类型可以忽略；参数错误须修正指针或重新 init。非 sample 事件下 sample 保持上次内容，不表示新测量。支持帧会先构造全零样本，再只填本类型的数据，不携带其他组的历史值。

## 类型、常量与有效位

`IMU55_FRAME_SIZE = 11u`。`IMU55_DEFAULT_ACCEL_RANGE_G = 16.0f`、`IMU55_DEFAULT_GYRO_RANGE_DPS = 2000.0f` 是 init 可用的比例默认值，不自动生效。

| 有效位常量 | 值 | 本帧可读取字段 |
| --- | --- | --- |
| `IMU55_VALID_ACCEL` | 0x01 | accel_mg[3] |
| `IMU55_VALID_GYRO` | 0x02 | gyro_dps[3] |
| `IMU55_VALID_ANGLES` | 0x04 | angles_deg[3] |
| `IMU55_VALID_TEMPERATURE` | 0x08 | temperature_c |
| `IMU55_VALID_VERSION_RAW` | 0x10 | version_raw |

| `imu55_sample_t` 字段 | 类型、单位与意义 |
| --- | --- |
| `type` | `uint8_t`，本次已接受的0x51/52/53 |
| `valid_mask` | `uint8_t`，仅标记当前帧有效数据；0x51为0x09、0x52为0x02、0x53为0x14 |
| `accel_mg[3]` | `float`，X/Y/Z，mg，raw×accel_mg_per_lsb；1 mg=0.001 g |
| `gyro_dps[3]` | `float`，X/Y/Z，°/s，raw×gyro_dps_per_lsb |
| `angles_deg[3]` | `float`，roll/pitch/yaw，°，raw×180/32768，编码范围[-180,180) |
| `temperature_c` | `float`，°C，0x51字节8…9的有符号16位值×0.01，编码范围-327.68…327.67 |
| `version_raw` | `uint16_t`，0…65535，0x53字节8…9原始无符号字；不解释版本编码 |

轴值原始编码范围为 -32768…32767。加速度范围为 [-range_g×1000, range_g×1000)，角速度为 [-range_dps, range_dps)。这些是编码范围，模块不检查传感器物理合理性。无效字段为零，但零值不表示有效；必须检查对应位。温度/版本字语义按本格式解读，其他传感器可能在字节8…9输出不同内容，需确认型号协议。0x52字节8…9只参与校验，不解码。

| `imu55_parser_t` 字段 | 类型、含义 |
| --- | --- |
| `buffer[11]` | `uint8_t` 内部候选帧缓冲，仅模块写入 |
| `used` | `uint8_t`，持久范围0…10，已收候选字节数，仅模块写入 |
| `accel_mg_per_lsb` | `float`，初始化计算的 mg/LSB 比例，只读诊断 |
| `gyro_dps_per_lsb` | `float`，初始化计算的°/s/LSB比例，只读诊断 |

没有共享最新值、跨类型缓存、时间戳或持久有效位。需要汇总时由应用分别存储各组、接收时间和有效状态；一组更新不代表其他组也新鲜。

## 帧格式与恢复

| 偏移 | 内容 |
| --- | --- |
| 0 | 0x55 |
| 1 | 类型0x51/52/53 |
| 2…7 | 三个低字节在前的有符号16位轴数据 |
| 8…9 | 类型特定附加字，仍低字节在前 |
| 10 | 字节0…9累加后模256 |

缓冲收满11字节才校验，不因负载出现0x55而提前重启。校验失败时从偏移1起搜索最早的0x55，保留此后缀继续收帧；可能连续报告坏帧，直到真实候选完整。校验正确的支持/未支持帧均整帧消费，避免把合法负载重复解释成帧头。加法校验不是 CRC，不能检测所有噪声或确认设备身份。

## 上下文、所有权与限制

函数同步、有界、不阻塞，不分配堆内存、不访问时钟、不调用回调。每实例固定缓冲11字节，另含计数与两个float比例，结构大小用 `sizeof` 获取。模块不拥有外设，无 deinit。同一实例 init/feed 必须串行，不可重入；不同实例独立。ISR可按应用约束调用，但浮点转换和内存移动增加耗时；任务/中断共享实例及样本须由应用同步。

没有超时，半帧可保留任意时长，应用需要时用 init 清除。没有传感器配置、量程探测、航向标定或多帧时间同步。不要在量程未核实的设备上默认假设换算正确。来源及未发现许可授权的状态见 [SOURCE.md](SOURCE.md)，本模块不授予上游代码或衍生物的使用/分发权限。
