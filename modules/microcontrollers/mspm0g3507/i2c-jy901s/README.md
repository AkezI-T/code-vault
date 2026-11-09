# i2c-jy901s

简体中文 | [English](README.en.md)

JY901S I2C 的异步 roll/pitch/yaw 角度读取。协议为发送一个寄存器选择字节，repeated START 后读取六字节并 STOP。默认未移位地址 0x50、起始寄存器 0x3D、活动超时 15 ms；只支持角度读取，不含串口、加速度、角速度、校准或设备配置功能。每个实例自带事务和接收区，无全局传感器状态、无堆分配。

## 环境、接线与接入

C99 的 C API；C++ 应用需用 extern "C" 包含头文件。平台为 MSPM0G3507，依赖 MSPM0 SDK 2.09.00.01 DriverLib/CMSIS、TI Arm Clang 4.0.4.LTS 和同级 [async-i2c 模块](../async-i2c/README.md)。把本模块 [源文件](src/msp_jy901s.c) 与 [async-i2c 源文件](../async-i2c/src/msp_i2c_async.c) 加入应用，添加两个模块的 include 目录、SDK source 和 source/third_party/CMSIS/Core/Include，定义 __MSPM0G3507__ 并链接 DriverLib。

应用 SysConfig 创建 I2C_BUS controller，选择实际 SDA/SCL 引脚、外设时钟和双方支持的总线速率，启用 controller，使用 7 位地址、普通 FIFO 中断。关闭 DMA、target 和其他 I2C 中断；对应硬件实例和 IRQ 由唯一 MspI2cBus 拥有。传感器 SDA/SCL 连到所选引脚、共地，按硬件规格供电，上拉到 MCU 允许的逻辑电压；供电、板载上拉和地址设置按实际模块确认。模块不做引脚、电源、时钟或传感器上电等待配置；开始请求前应由应用满足设备就绪时间。

顺序：SYSCFG_DL_init → msp_i2c_init → msp_jy901s_init → request。应用单一主循环/任务每约 1 ms 调用总线 step 并轮询 take；真实 ISR 只转发 msp_i2c_irq_handler。多个不同地址的传感器可共享同一 bus，由队列串行处理；每个传感器使用独立对象。同一外设不得创建多个 bus。示例独占 SysTick 作为 1 ms 连续时基；已有 RTOS/时基则复用现有毫秒计数与中断。

## 配置与公开类型

| 类型/字段 | 单位、范围、默认与含义 |
| --- | --- |
| MSP_JY901S_CONFIG_DEFAULT(bus_ptr) | MspJy901sConfig 初始化器，bus_ptr 为已初始化 MspI2cBus*；其余默认值见下表。 |
| MspJy901sConfig.bus | MspI2cBus*，必需且非 NULL，借用已初始化的总线；没有默认硬件实例。 |
| MspJy901sConfig.address | uint8_t，未移位 7 位地址 0..0x7F，默认 0x50；保留地址是否适用由应用决定。 |
| MspJy901sConfig.reg | uint8_t，0..255 的一字节寄存器选择，默认 0x3D；读取长度固定六字节，目标寄存器必须提供兼容的 roll/pitch/yaw 布局。 |
| MspJy901sConfig.timeout_ms | uint16_t，输入 0..65535 ms，默认 15；0 采用 async-i2c 的 20 ms。仅从硬件开始执行计时，覆盖所有阶段。 |
| MspJy901s.config | 配置的内部副本，初始化后不可改动。 |
| MspJy901s.transaction | MspI2cTransaction 内部异步事务，类型字段见 [async-i2c](../async-i2c/README.md)；应用不直接操作。 |
| MspJy901s.raw | uint8_t[6]，内部接收区，初始化为零；依次为 roll、pitch、yaw 的低/高字节，失败可部分更新。 |
| MspJy901sAngles.roll_x10 | int16_t，roll，0.1°，-1800..1799。 |
| MspJy901sAngles.pitch_x10 | int16_t，pitch，0.1°，-1800..1799。 |
| MspJy901sAngles.yaw_x10 | int16_t，yaw，0.1°，-1800..1799。 |
| MspI2cStatus | 来自依赖：MSP_I2C_STATUS_IDLE 可请求、QUEUED 等待、ACTIVE 传输；终态 OK 成功，ERROR 为 NACK/仲裁丢失/接收数量不符，TIMEOUT 为活动超时，前缀均为 MSP_I2C_STATUS_。 |

六字节按有符号小端 16 位整数解释，每轴执行 (raw * 1800) / 32768，整数除法向零截断，结果单位为 0.1°。不滤波、不展开 ±180° 边界、不修改坐标系或安装方向。

## 完整最小示例

I2C_BUS_INST、I2C_BUS_INST_INT_IRQN、I2C_BUS_INST_IRQHandler 和 CPUCLK_FREQ 是应用生成名称，按实际 SysConfig 调整。示例持续提供 step/IRQ，提交一次读取，并记录成功数据或终态；队满时仅重试尚未接受的 request，失败完成后的重试策略由应用决定。


```c
#include "ti_msp_dl_config.h"
#include "msp_jy901s.h"

static MspI2cBus bus;
static MspJy901s sensor;
static volatile uint32_t ticks_ms;
static volatile MspI2cStatus last_result = MSP_I2C_STATUS_IDLE;
static volatile int16_t last_yaw_x10;

void SysTick_Handler(void) { ++ticks_ms; }
void I2C_BUS_INST_IRQHandler(void) { msp_i2c_irq_handler(&bus); }

int main(void)
{
    SYSCFG_DL_init();
    const MspI2cConfig bus_config = {
        .instance = I2C_BUS_INST,
        .irqn = I2C_BUS_INST_INT_IRQN,
        .irq_priority = 3U
    };
    const MspJy901sConfig config = MSP_JY901S_CONFIG_DEFAULT(&bus);
    if (!msp_i2c_init(&bus, &bus_config) ||
        !msp_jy901s_init(&sensor, &config) ||
        SysTick_Config(CPUCLK_FREQ / 1000U) != 0U) {
        for (;;) {}
    }
    bool requested = false;
    bool done = false;
    for (;;) {
        msp_i2c_step(&bus, ticks_ms);
        if (!requested && !done) requested = msp_jy901s_request(&sensor);
        MspI2cStatus result;
        MspJy901sAngles angles;
        if (msp_jy901s_take(&sensor, &angles, &result)) {
            last_result = result;
            if (result == MSP_I2C_STATUS_OK) {
                last_yaw_x10 = angles.yaw_x10;
            } else {
                /* Choose retry or fault handling in application code. */
            }
            done = true;
        }
    }
}
```

## API


```c
bool msp_jy901s_init(MspJy901s *sensor, const MspJy901sConfig *config);
bool msp_jy901s_request(MspJy901s *sensor);
bool msp_jy901s_take(MspJy901s *sensor, MspJy901sAngles *angles, MspI2cStatus *result);
```

| API | 参数、前提、结果与错误 |
| --- | --- |
| msp_jy901s_init | sensor 为非 NULL 可写 MspJy901s*；config 为非 NULL 输入 MspJy901sConfig*，其中 bus 已初始化且 address≤0x7F。按值复制配置，清零内部事务；成功 true。空指针、空 bus 或越界地址返回 false 且不改对象。不访问硬件，不探测设备，不等待。仅首次初始化，或全部请求结果已消费且对象不在队列/IRQ 使用时重新初始化；不能用 init 取消请求。 |
| msp_jy901s_request | sensor 为非 NULL 已初始化实例；没有时间参数，超时由 bus step 的时基驱动。内部事务必须 IDLE；true 表示已排队，不表示设备已响应。空 sensor、状态为 QUEUED/ACTIVE 或存在未消费终态、队满或 setup/submit 前提不符时 false。失败不产生完成事件；队满时事务保持 IDLE，可以稍后 request 重试。未消费结果不会被新请求覆盖。 |
| msp_jy901s_take | sensor 为非 NULL 已初始化实例，angles 为可选 MspJy901sAngles* 输出，result 为可选 MspI2cStatus* 输出；输出指针可为 NULL。只有已有 OK/ERROR/TIMEOUT 时 true，写出 result 并消费结果，使实例回到 IDLE；只有 OK 时才写 angles，失败时数据输出保持原值。空 sensor 或未完成时 false，所有输出保持原值。可用 NULL 数据指针丢弃结果。输出存储只在该同步调用中使用，不需异步寿命。 |

## 异步寿命、线程与错误处理

三个 API 均立即返回，不休眠。应用必须单一主循环/任务串行调用 sensor API 与 bus 的非 ISR API；不支持并发任务或从其他 ISR 调用。IRQ 仅操作总线及其持有的事务。配置对象在 init 返回后可离开作用域；sensor 与其 bus 自 request 成功至 take 消费结果必须保持地址不变且有效，禁止释放、移动、重初始化或直接改内部字段。传感器接收区包含在实例内，不借用调用方临时缓冲区；应用只使用 take 成功且 result==OK 后的输出。无动态分配、取消或销毁 API。停止使用时先消费所有结果；bus 的停用流程见依赖说明。

request=false 只表示尚未接受本次工作，不会自动重试。take=true 且 result=ERROR/TIMEOUT 表示一次失败已完成，应用决定是否再次 request；错误不写数据输出，也不能把旧输出当作新样本。所有 TX、RX 共用一个 active 超时，不含排队和启动前等待硬件 idle 的时间；SDA/SCL 被外设拉住或 controller 始终非 idle 时队列可能永久等待。没有 GPIO 总线恢复；活动事务超时后只复位传输/FIFO。bus init/step/IRQ 中的 SDK FIFO flush 会无截止时间地轮询清空；硬件异常时可能阻塞，事务超时无法约束这个底层等待。step 停止会使启动、超时停止推进。TX_DONE ISR 中直接开始 repeated START 接收，不插入单独 STOP 或任务延时。

## 构建与许可

在现有 G3507 SysConfig 工程编译本模块和 async-i2c，并按上述 include/DriverLib 配置链接。物理总线、设备上电时间、寄存器兼容性和实际时序由应用确认。MIT 许可见 [LICENSE](LICENSE)。
