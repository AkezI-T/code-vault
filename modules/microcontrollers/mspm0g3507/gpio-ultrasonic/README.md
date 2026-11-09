# gpio-ultrasonic

简体中文 | [English](README.en.md)

配置化 GPIO 超声波测距：start 发出 10 µs TRIG 脉冲，step 采样 ECHO 上升和下降，独占的 TimerG 统计采样到的高电平时长，take 消费一次结果。实例互相独立，距离单位为毫米，声速和 timer Hz 显式配置。来源与使用权限见 [SOURCE.md](SOURCE.md)；此模块没有附带许可授权。

## 环境、硬件与接入

需要 C99、MSPM0G3507、MSPM0 SDK 2.09.00.01 DriverLib/CMSIS；工具链可使用 TI Arm Clang 4.0.4.LTS。将 [源文件](src/gpio_ultrasonic.c) 加入应用，添加 [头文件](include/gpio_ultrasonic.h) 所在 include 目录、SDK source 和 source/third_party/CMSIS/Core/Include，定义 __MSPM0G3507__。C++ 应用须在 extern "C" 中包含头文件并按 C 编译实现。

应用通过 SysConfig 配置 TRIG 为输出、初始低，ECHO 为输入；两者使用真实 GPIO_Regs* 和单比特 pin mask，可以位于不同端口。按传感器规格供电、共地；若 ECHO 超过 MCU 输入容限，使用电平转换。开始前保证 TRIG 已保持低且设备已满足上电/再次触发间隔，模块不提供周期重触发策略。

测量 TimerG 必须由该实例独占，配置为 one-shot up counting、无自动启动，counter 初值 0，timer_hz 是分频后的实际 counter Hz；LOAD 处生成 LOAD_EVENT 且不能由其他 ISR/代码清除。max_ticks 是应用允许的最大原始计数，必须小于或等于真实计数宽度/LOAD 能表达的范围；建议不超过 LOAD-1。模块只停止、清零、启动、读取 timer 和清 LOAD_EVENT，不配置时钟、LOAD、模式、引脚或 NVIC。应用禁止该测量 timer 的中断；无需 timer ISR。init 会停表并拉低 TRIG；不要把共享 PWM/系统时基传作测量 timer。

now_us 回调必须提供持续前进的 uint32_t 微秒单调计数，允许模 2^32 回绕。任意有效测量和 step 调用间隔必须小于 2^31 µs，不能只提供会在 16 位回绕的原始计数。应用可用独立 32 位自由运行 timer 或正确扩展后的软件计数；模块无时钟猜测或 fallback。delay_us 回调必须在硬件实际至少 10 µs 后可靠返回，不依赖会被当前上下文屏蔽的中断。start 只同步等待这段触发延时，模块没有轮询 ECHO 的等待循环。

## 配置、类型和字段

没有隐含默认配置；下表的示例值用于 1 MHz timer、约 340 m/s 声速。

| 名称 | 类型、范围、单位和含义 |
| --- | --- |
| GPIO_ULTRASONIC_TRIGGER_US | 宏 10，TRIG 高电平回调时长，µs。 |
| GpioUltrasonicNowUs | uint32_t (*)(void *context)，返回持续微秒时基，无错误返回；context 原样传入。 |
| GpioUltrasonicDelayUs | void (*)(void *context, uint32_t duration_us)，阻塞至少所给 µs 后返回；模块以 10 调用，无错误返回。 |
| GpioUltrasonicConfig.trig_port / echo_port | GPIO_Regs*，非 NULL 的已配置端口。 |
| GpioUltrasonicConfig.trig_pin / echo_pin | uint32_t 非零单比特掩码；必须是该端口上真实存在的引脚；TRIG/ECHO 不能是同一端口同一 pin。 |
| GpioUltrasonicConfig.timer | GPTIMER_Regs*，非 NULL、已配置的独占 TimerG。 |
| GpioUltrasonicConfig.timer_hz | uint32_t，1..UINT32_MAX Hz，实际 counter tick rate；示例 1000000。 |
| GpioUltrasonicConfig.max_ticks | uint32_t，1..UINT32_MAX ticks；还须满足硬件位宽/LOAD 边界，示例 39999（LOAD=40000）。 |
| GpioUltrasonicConfig.timeout_us | uint32_t，11..0x7FFFFFFF µs，从触发开始之前计时，覆盖 10 µs 脉冲、等上升和高电平阶段；示例 50000。 |
| GpioUltrasonicConfig.speed_mm_s | uint32_t，1..UINT32_MAX mm/s；示例 340000。应用按介质/温度选择。 |
| GpioUltrasonicConfig.max_distance_mm | uint32_t，1..UINT32_MAX mm；示例 6000。 |
| GpioUltrasonicConfig.now_us / delay_us | 上述非 NULL 回调，初始化仅保存，不校验实际时钟或等待精度。 |
| GpioUltrasonicConfig.context | void* 借用上下文，可为 NULL，需覆盖回调使用寿命。 |
| GpioUltrasonicResult.status | GpioUltrasonicStatus 终态；仅 OK 表示有效距离。 |
| GpioUltrasonicResult.pulse_ticks | uint32_t；OK/RANGE 为采样后的原始 ticks，其他错误为 0。 |
| GpioUltrasonicResult.distance_mm | uint32_t；OK 为向下取整的正毫米距离，所有错误为 0，不能把 0 当作有效测距。 |
| GpioUltrasonic.config | 配置内部副本，初始化后不得直接修改。 |
| GpioUltrasonic.status | 内部 GpioUltrasonicStatus 状态。 |
| GpioUltrasonic.started_us | uint32_t，内部开始时间，µs。 |
| GpioUltrasonic.result | 内部一次性结果存储。 |

GpioUltrasonicStatus 的全部枚举使用 GPIO_ULTRASONIC_ 前缀：IDLE 可 start；WAIT_RISE 等上升；WAIT_FALL 等下降；OK 成功；TIMEOUT 为总等待到期；OVERFLOW 为 LOAD_EVENT 或采样计数大于 max_ticks；RANGE 为 ticks=0、换算后小于 1 mm 或距离超过上限；ECHO_HIGH 为 start 时 ECHO 已高。时间到期的判断优先于同次 step 的回波与溢出判断。配置/对象字段公开仅为无堆存储，应用不得直接操作内部状态。

距离公式为 floor(pulse_ticks × speed_mm_s / (2 × timer_hz))。乘法和分母使用 uint64_t，任何两个 uint32_t 乘积均可容纳；转回 uint32_t 前先与 max_distance_mm 比较，避免溢出或截断为伪有效距离。

## API

```c
bool gpio_ultrasonic_init(GpioUltrasonic *sensor, const GpioUltrasonicConfig *config);
bool gpio_ultrasonic_start(GpioUltrasonic *sensor);
void gpio_ultrasonic_step(GpioUltrasonic *sensor);
bool gpio_ultrasonic_take(GpioUltrasonic *sensor, GpioUltrasonicResult *result);
```

| API | 参数、返回与前提 |
| --- | --- |
| gpio_ultrasonic_init | sensor 为非 NULL 可写存储，config 为非 NULL 输入，字段满足上表。成功 true，按值复制配置、初始化为 IDLE、停表并拉低 TRIG；空端口/timer/回调、非单比特 pin、同 pin、零频率/范围/声速、timeout 越界返回 false，不改对象或硬件。不能检测错误的硬件地址、模式或实际位宽，应用负责一致性。首次使用无需预先清零；重新 init 只允许无活动测量且结果已消费。 |
| gpio_ultrasonic_start | sensor 必须成功 init。NULL、非 IDLE（包括未消费终态）返回 false，不开始工作。true 表示接受一次测量；先停表清零/清 LOAD_EVENT，读取开始时间；若 ECHO 已高，立即保存 ECHO_HIGH 并且不发脉冲，仍返回 true。正常时发 10 µs 脉冲后进入 WAIT_RISE。true 不保证得到距离，必须 take 查看终态。 |
| gpio_ultrasonic_step | sensor 为已初始化实例；NULL/非等待态为空操作，返回 void。每次只采样一次：总超时→错误；观察上升→启动 timer；观察下降→停表/检查 LOAD_EVENT/换算结果。不会等待边沿，不会自行重触发。 |
| gpio_ultrasonic_take | sensor 为已初始化实例，result 为可选 GpioUltrasonicResult* 输出，可 NULL 以丢弃。终态时 true 并复制完整结果、回到 IDLE；NULL sensor/尚未完成返回 false 且输出不变。错误结果也会被复制，只有 status=OK 的 distance_mm 有效。 |

## 完整应用示例

应用 SysConfig 生成 GPIO_ULTRA_PORT、GPIO_ULTRA_TRIG_PIN、GPIO_ULTRA_ECHO_PIN（同组示例）；TIMER_ECHO_INST 为上面描述的 1 MHz/LOAD 40000 单次向上 timer；TIMER_CLOCK_INST 为独立、已启动的 32 位 TimerG、1 MHz、continuous up counting、LOAD=0xFFFFFFFF。选择支持 32 位计数的实例（G3507 的 TIMG12），不能拿 16 位 timer 原始值充当此时基。CPUCLK_FREQ 是真实 CPU Hz，示例要求为整数 MHz 且 (CPUCLK_FREQ/1000000)×10 可放入 uint32_t。delayCycles 指令开销会使脉冲略长，应用确认传感器的触发容限；需要更精确脉冲时替换延时回调。此示例提交一次测量，持续 step，无 SysTick ISR。

```c
#include "ti_msp_dl_config.h"
#include "gpio_ultrasonic.h"

static GpioUltrasonic sensor;
static volatile GpioUltrasonicResult latest;

static uint32_t app_now_us(void *context)
{
    (void)context;
    return DL_TimerG_getTimerCount(TIMER_CLOCK_INST);
}

static void app_delay_us(void *context, uint32_t duration_us)
{
    (void)context;
    DL_Common_delayCycles((CPUCLK_FREQ / 1000000U) * duration_us);
}

int main(void)
{
    SYSCFG_DL_init();
    const GpioUltrasonicConfig config = {
        .trig_port = GPIO_ULTRA_PORT,
        .trig_pin = GPIO_ULTRA_TRIG_PIN,
        .echo_port = GPIO_ULTRA_PORT,
        .echo_pin = GPIO_ULTRA_ECHO_PIN,
        .timer = TIMER_ECHO_INST,
        .timer_hz = 1000000U,
        .max_ticks = 39999U,
        .timeout_us = 50000U,
        .speed_mm_s = 340000U,
        .max_distance_mm = 6000U,
        .now_us = app_now_us,
        .delay_us = app_delay_us,
        .context = NULL
    };
    if (!gpio_ultrasonic_init(&sensor, &config)) {
        for (;;) {}
    }
    bool requested = gpio_ultrasonic_start(&sensor);
    if (!requested) {
        for (;;) {}
    }
    for (;;) {
        gpio_ultrasonic_step(&sensor);
        GpioUltrasonicResult result;
        if (gpio_ultrasonic_take(&sensor, &result)) {
            latest = result;
            if (result.status == GPIO_ULTRASONIC_OK) {
                /* Use result.distance_mm as a new sample. */
            } else {
                /* Apply the application's timeout/fault policy. */
            }
        }
    }
}
```

## 调度、寿命与限制

全部非回调逻辑有界；仅 start 调用阻塞延时回调约 10 µs，其余立即返回。回调若不返回、时钟停止或不调用 step，模块无法强制到期。由于 GPIO 采样，step 周期必须远小于目标最短 ECHO 脉宽；不能只以 1 ms 任务扫描短距离回波。上升和下降均有采样延迟，计数从观察到上升才开始；误差受调度和中断延迟影响，可能漏掉整个短脉冲。高精度场景使用 [timer-ultrasonic](../timer-ultrasonic/README.md) 的真实捕获数据路径。

单个主循环/任务串行调用所有 API；回调也在该上下文中执行。不支持多任务、ISR 与主循环并发访问同一实例。模块不关中断、不拥有任何 ISR。每个实例需独立 TRIG/ECHO 和测量 timer；now_us 时基可以共享。配置按值复制，输入配置可在 init 后失效，回调 context 和硬件必须持续有效。实例无借用异步数据缓冲区，结果复制到调用方；未 take 前不覆盖。停止 step 会使活动 timer 继续运行至单次周期结束；模块无取消/销毁 API，完成并 take 后 timer 已停、TRIG 低，可停止调用并释放实例。不要在活动测量时重新 init。

## 构建与权限

在现有 G3507 SysConfig 工程中编译实现与示例，DriverLib 接口来自 SDK。硬件、采样精度、声速、最大量程和设备间隔由应用负责。未提供硬件验证。来源、上游修订和缺失的许可授权见 [SOURCE.md](SOURCE.md)。
