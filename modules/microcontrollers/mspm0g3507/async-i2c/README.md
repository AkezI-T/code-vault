# async-i2c

简体中文 | [English](README.en.md)

MSPM0G3507 DriverLib I2C controller 的非阻塞 FIFO 事务队列，支持纯发送、纯接收、repeated START 寄存器读取和带 STOP 的分离读取。每个对象独立管理一条硬件总线，8 个排队槽位加 1 个活动事务，不分配堆内存。

## 环境与接入

需要 C API（以 C 编译；C++ 应用需自行用 extern "C" 包含头文件）、C99、MSPM0 SDK 2.09.00.01 DriverLib/CMSIS、MSPM0G3507 设备定义和 Cortex-M0+ 工具链（TI Arm Clang 4.0.4.LTS）。将 [源文件](src/msp_i2c_async.c) 加入应用，把 [include 目录中的头文件](include/msp_i2c_async.h) 所在目录加入搜索路径；同时添加 SDK 的 source 和 source/third_party/CMSIS/Core/Include，定义 __MSPM0G3507__ 并链接 DriverLib。模块不配置电源、时钟、引脚复用、I2C 速率或上拉。

在应用 SysConfig 中创建名为 I2C_BUS 的 I2C controller，选择实际 SDA/SCL 引脚、时钟和总线速率，启用 controller；配置为 7 位地址、普通 FIFO 中断模式，关闭 DMA、target 模式和其他 I2C 中断。两线接传感器对应引脚并共地；按板级电压要求供电，SDA/SCL 上拉到 MCU 可接受的逻辑电压。只让本模块操作该外设及对应 NVIC IRQ。应用提供生成的 IRQ 名称对应的处理函数，模块不定义中断向量。

初始化顺序为 SYSCFG_DL_init → msp_i2c_init → setup → submit；在同一应用上下文持续调用 step（建议约 1 ms 一次）和 take_result，IRQ 中仅转发 irq_handler。示例的 SysTick 是应用独占的 1 ms 时基；若已有 RTOS/时基，使用其连续毫秒计数并保留现有中断处理。

## 完整最小示例

生成的 I2C_BUS_INST、I2C_BUS_INST_INT_IRQN、I2C_BUS_INST_IRQHandler 和 CPUCLK_FREQ 仅出现在应用；更改名称与硬件参数时同步调整示例。读取对象和 RX 缓冲区均为静态寿命。

```c
#include "ti_msp_dl_config.h"
#include "msp_i2c_async.h"

static MspI2cBus bus;
static MspI2cTransaction transaction = {0};
static uint8_t rx[1];
static volatile uint32_t ticks_ms;
static volatile MspI2cStatus last_result = MSP_I2C_STATUS_IDLE;

void SysTick_Handler(void) { ++ticks_ms; }
void I2C_BUS_INST_IRQHandler(void) { msp_i2c_irq_handler(&bus); }

int main(void)
{
    SYSCFG_DL_init();
    const MspI2cConfig config = {
        .instance = I2C_BUS_INST,
        .irqn = I2C_BUS_INST_INT_IRQN,
        .irq_priority = 3U
    };
    if (!msp_i2c_init(&bus, &config) ||
        SysTick_Config(CPUCLK_FREQ / 1000U) != 0U) {
        for (;;) {}
    }
    if (!msp_i2c_setup_read_reg8(&transaction, 0x12U, 0x30U,
        rx, sizeof(rx), 10U, true) || !msp_i2c_submit(&bus, &transaction)) {
        for (;;) {}
    }
    for (;;) {
        msp_i2c_step(&bus, ticks_ms);
        MspI2cStatus result;
        if (msp_i2c_take_result(&transaction, &result)) {
            last_result = result;
            if (result == MSP_I2C_STATUS_OK) {
                /* Consume rx[0] here. */
            } else {
                /* Apply the application's retry or fault policy here. */
            }
        }
    }
}
```

## 类型、常量与字段

| 名称 | 含义与约束 |
| --- | --- |
| MSP_I2C_MAX_TX_BYTES | 16 字节，包含寄存器地址。 |
| MSP_I2C_QUEUE_SIZE | 8 个等待事务；活动事务不占此计数。 |
| MSP_I2C_DEFAULT_TIMEOUT_MS | timeout_ms 为 0 时采用 20 ms。 |
| MspI2cStatus | IDLE → QUEUED → ACTIVE → OK/ERROR/TIMEOUT；所有枚举名带 MSP_I2C_STATUS_ 前缀。IDLE 表示可准备/提交，QUEUED 等待，ACTIVE 执行，OK 完成，ERROR 为 NACK、仲裁丢失或接收字节数不符，TIMEOUT 为活动事务超时。没有重试或更细错误码。 |
| MspI2cConfig.instance | I2C_Regs*，已配置的有效硬件实例，不可为 NULL。 |
| MspI2cConfig.irqn | IRQn_Type，该实例对应的外部 IRQ，必须正确匹配；负值被拒绝。 |
| MspI2cConfig.irq_priority | uint32_t，未移位的 CMSIS 优先级，0 至 (1<<__NVIC_PRIO_BITS)-1；G3507 为 0..3，0 最高。 |
| MspI2cTransaction.address | uint8_t，未移位的 7 位地址 0..0x7F。保留地址的适用性由应用决定。 |
| MspI2cTransaction.tx / tx_len | 内嵌 uint8_t[16] / uint8_t，发送字节与数量 0..16；由 setup 拷贝。 |
| MspI2cTransaction.rx / rx_len | uint8_t* / uint8_t，调用方可写接收区及长度 0..255；长度非零时指针必需。 |
| MspI2cTransaction.timeout_ms | uint16_t，准备后为 1..65535 ms，覆盖整个活动事务的两个阶段。 |
| MspI2cTransaction.stop_between_phases | bool，两阶段发送/接收之间是否产生 STOP；setup 默认为 false，read_reg8 由 split 指定。 |
| MspI2cTransaction.status | volatile MspI2cStatus，模块拥有状态；初次使用必须零初始化，不得手动复位排队或活动事务。 |
| MspI2cBus.config | 配置的内部副本，初始化后不要改动。 |
| MspI2cBus.queue | MspI2cTransaction*[8]，内部等待指针环形队列。 |
| MspI2cBus.head / tail / count | uint8_t，内部出队位置、入队位置和等待数量。 |
| MspI2cBus.active | volatile 指针，当前活动事务或 NULL。 |
| MspI2cBus.hw_state | volatile uint8_t，内部硬件阶段，不是公开协议枚举。 |
| MspI2cBus.tx_count / rx_count | volatile uint8_t，已写入 FIFO / 已读出 FIFO 的字节数。 |
| MspI2cBus.start_ms | uint32_t，活动事务开始的毫秒时间。 |

MspI2cBus 的字段只用于无堆存储，应用不得直接修改。每个外设只能有一个 bus，一个事务只能属于一条总线；不同硬件实例使用不同 bus 和 ISR。

## API

准确签名见 [公开头文件](include/msp_i2c_async.h)：

```c
bool msp_i2c_init(MspI2cBus *bus, const MspI2cConfig *config);
bool msp_i2c_setup(MspI2cTransaction *transaction, uint8_t address,
                   const uint8_t *tx, uint8_t tx_len,
                   uint8_t *rx, uint8_t rx_len, uint16_t timeout_ms);
bool msp_i2c_setup_read_reg8(MspI2cTransaction *transaction, uint8_t address,
                             uint8_t reg, uint8_t *rx, uint8_t rx_len,
                             uint16_t timeout_ms, bool split);
bool msp_i2c_setup_write_reg8(MspI2cTransaction *transaction, uint8_t address,
                              uint8_t reg, const uint8_t *data,
                              uint8_t data_len, uint16_t timeout_ms);
bool msp_i2c_submit(MspI2cBus *bus, MspI2cTransaction *transaction);
bool msp_i2c_take_result(MspI2cTransaction *transaction, MspI2cStatus *result);
void msp_i2c_step(MspI2cBus *bus, uint32_t now_ms);
bool msp_i2c_is_idle(const MspI2cBus *bus);
uint8_t msp_i2c_queue_count(const MspI2cBus *bus);
void msp_i2c_irq_handler(MspI2cBus *bus);
```


| API | 参数、结果与错误 |
| --- | --- |
| msp_i2c_init | bus 为可写存储，config 为配置输入，均非 NULL；配置按值复制。成功 true；空指针、空实例、负 IRQ 或优先级越界返回 false，不改状态。成功时清空 bus，设置 FIFO 一字节阈值、清理传输、启用模块中断并设置/清除 pending/启用 NVIC IRQ。只允许首次初始化或所有事务均已消费且硬件空闲时调用，不得用它取消事务。 |
| msp_i2c_setup | transaction 非 NULL 且 status 为 IDLE；tx/tx_len 为发送输入，rx/rx_len 为接收输出，address/timeout_ms 含义见上表。两长度不能同时为零，tx_len≤16；非零长度需要对应非 NULL 指针。true 表示已拷贝 TX 并保存 RX 指针，未提交；上述错误或 address>0x7F 返回 false 且保留对象。timeout_ms=0 使用 20 ms。可只发或只收。 |
| msp_i2c_setup_read_reg8 | transaction/address/rx/timeout_ms 同 setup；reg 为 uint8_t 寄存器选择字节，rx_len 必须 1..255；split=false 用 repeated START，split=true 写后 STOP，后续 step 确认 controller idle 再 START 读取。返回规则同 setup；没有额外固定延时。 |
| msp_i2c_setup_write_reg8 | transaction/address/reg/timeout_ms 同上；data 为非 NULL 输入区，data_len 为 1..15 字节。将 reg 和数据拷贝到 TX；成功 true，长度、指针或 setup 前提不符 false。输入区返回后即可复用。 |
| msp_i2c_submit | bus 非 NULL 且已初始化；transaction 必须准备成功、IDLE，指向有效接收区。成功 true 并置 QUEUED，传输到下次 step 才开始。队满、空参数、非 IDLE、无效地址/长度/零有效超时返回 false；队满时准备好的事务保持 IDLE，可稍后重试；其他拒绝保留原状态。不得修改字段以绕过 setup。 |
| msp_i2c_take_result | transaction 非 NULL；result 是可选 MspI2cStatus* 输出。仅 OK/ERROR/TIMEOUT 时 true，写出终态并复位 IDLE；空事务或未完成时 false，不改输出。不检查总线，不阻塞。 |
| msp_i2c_step | bus 为非 NULL 的已初始化实例；now_ms 为 uint32_t 单调毫秒，允许模 2^32 回绕，调用间隔小于 2^31 ms。返回 void；处理超时、分离读第二阶段及启动下一个事务。必须持续调用，才能推进队列及检测超时。 |
| msp_i2c_is_idle | bus 为非 NULL 已初始化实例，返回 bool：活动事务为空且等待数量为 0；这是软件队列状态，不表示物理 SDA/SCL 已空闲。 |
| msp_i2c_queue_count | bus 同上，返回 uint8_t 等待数 0..8，不含活动事务。 |
| msp_i2c_irq_handler | bus 为中断所属的非 NULL 已初始化实例；只从该实例的 ISR 调用，返回 void。读取并处理一次最高优先级 pending 事件，剩余事件由 NVIC 再次进入处理。不可由应用轮询调用或在转发前读取 IIDX。 |

寄存器写入调用例：

```c
uint8_t value = 0x01U;
if (msp_i2c_setup_write_reg8(&transaction, 0x50U, 0x00U, &value, 1U, 15U)) {
    bool accepted = msp_i2c_submit(&bus, &transaction);
    /* Retry submission later if the queue is full. */
    (void)accepted;
}
```


## 时序、寿命与并发

API 不等待 I2C 事务完成；init、step 和 IRQ 路径会调用 SDK 的 FIFO flush，其中 DriverLib 轮询 FIFO 清空且没有软件截止时间。正常硬件下该清理很短，硬件异常时可能阻塞，事务 timeout 不限制该 SDK 等待。step/submit 使用全局关中断临界区，并恢复进入前的 PRIMASK；step 中的 FIFO 清理也在临界区内。除 irq_handler 外，所有 API 必须由同一个主循环或任务串行调用；不支持多个任务或其他 ISR 并发调用，也不可在 IRQ 中 setup/submit/take/step。应用在调用时保持正常 IRQ 服务；长期屏蔽 IRQ 会让超时先于已到达的硬件完成事件生效。

事务对象自 submit 成功至 take_result 完成均由队列/IRQ 使用，不能移动、释放、重用、重新 setup 或改字段。RX 缓冲区至少保存到 take_result；完成前不得访问，失败时可能含旧数据或部分更新，只能在 OK 时使用。TX 输入在 setup 时复制，所以输入数组可在 setup 返回后离开作用域。配置输入在 init 返回后可离开作用域；bus 必须覆盖所有引用和中断的寿命。模块无动态分配、取消、关闭或反初始化 API。停止使用时，应用先消费全部结果，确认软件和物理总线均空闲，再禁用该 NVIC IRQ 和 controller 并释放对象。

超时从 step 真正启动事务时计算，排队等待和启动前的 bus busy 不计入；总线一直忙时队列可无限等待。活动事务的 TX、等待 split idle 和 RX 共用一个超时预算，不在换阶段时重置。只在 step 发现 elapsed≥timeout 时上报 TIMEOUT，额外延迟受调度间隔影响。NACK/仲裁丢失和超时会 reset controller transfer、清 FIFO；不执行 GPIO 时钟恢复，也不保证被外设拉低的总线能恢复。一次完成后，下一笔只由 step 启动。split 写 STOP 与读 START 之间的间隔由 step 调度决定；约 1 ms 调用保持相应调度粒度。

## 构建与许可

在现有 G3507 SysConfig 应用中编译并链接 [源文件](src/msp_i2c_async.c) 和 SDK DriverLib。硬件相关的总线电平、上拉、速率、外设兼容性与实际时序需要应用确认。MIT 许可见 [LICENSE](LICENSE)。
