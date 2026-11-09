# timer-ultrasonic

简体中文 | [English](README.en.md)

对调用方提供的真实定时器脉宽捕获事件进行一次性超声波距离换算。模块只接收 pulse_ticks、显式 tick_hz、新事件序号与捕获时间，不读取固定 CC 快照、不产生 TRIG/PWM、不接管 SysTick 或 ISR。配置量程和声速，距离单位为毫米；每次 arm 最多交付一个成功或错误结果。来源、上游修订与缺失的许可授权见 [SOURCE.md](SOURCE.md)；此模块没有附带许可授权。

## 环境与硬件责任

核心为 C99，仅依赖 stdbool.h/stdint.h/stddef.h；归类于 MSPM0G3507 平台，应用 glue 使用 MSPM0 SDK 2.09.00.01 DriverLib/CMSIS 和 TI Arm Clang 4.0.4.LTS。编译 [源文件](src/timer_ultrasonic.c)，添加 [头文件](include/timer_ultrasonic.h) 所在 include 目录。核心不依赖 SDK；下方 glue 示例还需 SDK source、source/third_party/CMSIS/Core/Include 与 __MSPM0G3507__。C++ 应用在 extern "C" 中包含核心头文件并按 C 编译实现。

应用通过 SysConfig 选择实际 TimerG、CC 通道、ECHO CCP 引脚和分频，使用与输出原始脉宽 tick 相符的 pulse-width capture up-counting 配置。tick_hz 是 counter 的实际 Hz，不是 CPU Hz；不能只复制 prescaler 数字。ECHO 电平必须在 MCU 容限内，必要时转换电平并共地。TRIG 由应用独立 GPIO 或 PWM 产生，符合传感器要求的约 10 µs 高脉冲及低电平/重触发间隔。模块不承诺上游示例中 50% 占空比 PWM 对特定传感器适用。

应用确认所选模式的 CC 值为本次高电平脉宽；若硬件给出自由运行边沿时间戳，应由应用先做对应位宽/方向/溢出检查并相减，再 feed 宽度。max_ticks 必须处于所选 timer 的真实位宽/LOAD 周期内，不能把截断后的 counter 值当成完整脉宽。应用拥有 ISR、NVIC、事件读取/清除、捕获周期间复位和溢出/overcapture 检测；按实际硅版本遵循 TI timer 捕获勘误（例如 TIMER_ERR_01 的 reload 要求）。模块无法从一个合法数值判断是否存在硬件溢出或丢边沿。

## 调用顺序与新鲜度

init → 清理旧硬件事件/旧邮箱并复位捕获周期 → arm(now_us, last_sequence) → 发出 TRIG/等待新捕获 → feed → take。无事件时持续 step(now_us) 检测超时。模块不自动开始下一次测量，未消费终态不可再次 arm。

last_sequence 为 arm 时已产生的最后硬件事件序号，首次可用 0；每个真实完整捕获（包括失败/overflow 事件）序号加一，uint32_t 回绕允许。feed 只接受相对基线前进 1..0x7FFFFFFF 的序号；相等、反向或恰好半范围都拒绝。captured_us 与 arm/step 的 now_us 必须来自同一持续前进、模 2^32 的微秒时基；调用间隔和相关事件时间差必须小于 2^31 µs。captured_us 指当前测量周期真实捕获完成时的时间（或及时 ISR 时间，误差由应用评估），不能用消费旧 CC 时的当前时间重新标记旧数据。

feed 拒绝相对 arm 早于起点/半范围以外的 timestamp；处于起点至 deadline 之前的新事件才可能成功。序号/时间戳只是去重和时间窗口检查；arm 前就已开始的 ECHO、未清的中断标志或被误标新序号的旧 CC 仍可能通过。应用必须在 arm 前丢弃旧事件、确认 ECHO/捕获周期干净，并且只把本次有效完整脉冲送入。若延迟消费，先排空已确认的捕获邮箱并 feed，再对当前时间 step；step 已生成 TIMEOUT 后不会被迟到 feed 改写。

## 类型与全部字段

所有配置都必填，没有默认值；下表示例取 1 MHz、340 m/s、6 m 量程。

| 名称 | 类型、范围、单位与含义 |
| --- | --- |
| TimerUltrasonicConfig.tick_hz | uint32_t，1..UINT32_MAX Hz，脉宽计数真实 tick rate；示例 1000000。 |
| TimerUltrasonicConfig.max_ticks | uint32_t，1..UINT32_MAX ticks；还必须满足硬件位宽/周期与脉宽有效范围；示例 49999 对应 1 MHz/LOAD 50000 的保守上界。 |
| TimerUltrasonicConfig.timeout_us | uint32_t，1..0x7FFFFFFF µs，从 arm 到捕获完成的期限；示例 50000。 |
| TimerUltrasonicConfig.speed_mm_s | uint32_t，1..UINT32_MAX mm/s，声速；示例 340000，按介质/温度调整。 |
| TimerUltrasonicConfig.max_distance_mm | uint32_t，1..UINT32_MAX mm；示例 6000。 |
| TimerUltrasonicResult.status | TimerUltrasonicStatus 终态；仅 TIMER_ULTRASONIC_OK 为有效距离。 |
| TimerUltrasonicResult.sequence | uint32_t，已消费 feed 的序号；step 超时时为 arm 基线/最后已接受的序号，不代表新捕获。 |
| TimerUltrasonicResult.captured_us | uint32_t，feed 事件的 timestamp；step 超时时为检测超时的 now_us，不代表捕获时间。 |
| TimerUltrasonicResult.pulse_ticks | uint32_t，OK/RANGE 为输入原始 ticks；TIMEOUT/OVERFLOW 为 0。 |
| TimerUltrasonicResult.distance_mm | uint32_t，OK 为向下取整的正毫米距离；所有错误为 0，不得当作有效零距离。 |
| TimerUltrasonic.config | 配置内部副本，不直接改动。 |
| TimerUltrasonic.status | 内部 TimerUltrasonicStatus 状态，不直接改动。 |
| TimerUltrasonic.armed_us | uint32_t，内部 arm 起点，µs。 |
| TimerUltrasonic.last_sequence | uint32_t，内部去重基线/已接受序号。 |
| TimerUltrasonic.result | 内部一次性结果存储，供 take 复制。 |

TimerUltrasonicStatus 全部枚举带 TIMER_ULTRASONIC_ 前缀：IDLE 可 arm；WAITING 等待；OK 成功；TIMEOUT 为 step 到期或 feed timestamp 达到/超过期限；OVERFLOW 为调用方 overflow=true 或 ticks>max_ticks；RANGE 为 ticks=0、换算结果小于 1 mm 或超过 max_distance_mm。优先级为时间期限 → overflow → range → OK。没有浮点、隐含 1 MHz 或无效距离兼任错误码。

换算为 floor(pulse_ticks × speed_mm_s / (2 × tick_hz))。uint64_t 同时容纳 uint32_t×uint32_t 与 2×tick_hz；先检查距离上限再窄化，因此零频率/中间溢出/输出截断不会伪造有效值。

## API

```c
bool timer_ultrasonic_init(TimerUltrasonic *sensor, const TimerUltrasonicConfig *config);
bool timer_ultrasonic_arm(TimerUltrasonic *sensor, uint32_t now_us, uint32_t last_sequence);
bool timer_ultrasonic_feed(TimerUltrasonic *sensor, uint32_t sequence,
                           uint32_t pulse_ticks, bool overflow, uint32_t captured_us);
void timer_ultrasonic_step(TimerUltrasonic *sensor, uint32_t now_us);
bool timer_ultrasonic_take(TimerUltrasonic *sensor, TimerUltrasonicResult *result);
```

| API | 参数、返回与错误 |
| --- | --- |
| timer_ultrasonic_init | sensor 为非 NULL 可写实例，config 为非 NULL 输入；字段满足上表。true 按值复制配置并设 IDLE；NULL、任一配置值为零或 timeout_us≥0x80000000 返回 false，不修改实例。不操作硬件，不能校验 tick rate/位宽是否与硬件相符。首次无需清零；重新 init 仅在无等待且结果已消费时调用。 |
| timer_ultrasonic_arm | sensor 已初始化，now_us 为本次 arm 时的 uint32_t µs，last_sequence 为当前流最后一个已产生事件序号。仅 IDLE 返回 true，记录时间/基线进入 WAITING；NULL、WAITING 或未消费终态返回 false 且不改状态。它不清硬件、不触发测量。 |
| timer_ultrasonic_feed | sensor 已初始化；sequence 为真实新事件序号，pulse_ticks 为 uint32_t 脉宽 tick，overflow 为该脉冲已知溢出/overcapture/不完整标志，captured_us 为完成时的微秒时间。NULL、非 WAITING、序号不前进或 timestamp 早于 arm/超半范围返回 false，不改对象；正常新事件返回 true 并生成一个终态，即使终态为错误。elapsed≥timeout 为 TIMEOUT；其次 overflow 或 ticks>max_ticks 为 OVERFLOW；其次零/超距离为 RANGE；否则 OK。返回 true 不是有效距离保证。 |
| timer_ultrasonic_step | sensor 已初始化，now_us 为当前同一时基的 µs；NULL/非 WAITING 为空操作，返回 void。elapsed≥timeout 则生成 TIMEOUT；没有循环或阻塞。时钟必须前进，停止调用就不会自动超时。 |
| timer_ultrasonic_take | sensor 已初始化，result 为可选 TimerUltrasonicResult* 输出，可 NULL 丢弃。存在终态时 true、复制完整成功或错误结果并回到 IDLE；NULL/未完成 false 且输出不变。result 仅同步写入，不被模块保存。 |

## 完整 MSPM0 应用接入示例

下例是可直接加入应用的接入函数，硬件 timer/CC 从应用传入，不定义 ISR 向量或虚构时钟。应用调用 SYSCFG_DL_init，按实际 timer 配置调用 app_range_init(&app, selected_timer, selected_cc, actual_hz, safe_max_ticks)。随后在序列化上下文清旧硬件/邮箱、app_range_begin、发真实 TRIG；完整 CC 事件时调用 app_range_snapshot 冻结 ticks/时间/序号，原子地发布邮箱；主任务取走邮箱并调用 app_range_service。

snapshot 可由调用方 ISR 执行，但它访问 app.sequence；begin/service 与 ISR 之间必须由应用临界区或正确的消息传递串行化，不能直接并发读写。示例不实现通用邮箱或临界区，因为其所有权属于应用。snapshot 必须只对确认的新完整选定通道事件调用，读 CC 之前保证它尚未被新捕获覆盖；事件确认/清除顺序由应用依据模式确定。overflow 必须关联本脉冲，而不能读取一个未清的跨周期旧状态。CC 若并非直接 pulse width，先在应用换算差值并直接 feed 宽度，替换 snapshot 的直接读取。

```c
#include "timer_ultrasonic.h"
#include <stddef.h>
#include <ti/driverlib/dl_timerg.h>

typedef struct {
    TimerUltrasonic sensor;
    GPTIMER_Regs *timer;
    DL_TIMER_CC_INDEX cc;
    uint32_t sequence;
    TimerUltrasonicResult latest;
} AppRange;

typedef struct {
    uint32_t sequence;
    uint32_t captured_us;
    uint32_t pulse_ticks;
    bool overflow;
} AppCaptureEvent;

bool app_range_init(AppRange *app, GPTIMER_Regs *timer, DL_TIMER_CC_INDEX cc,
                    uint32_t actual_tick_hz, uint32_t safe_max_ticks)
{
    if (!app || !timer) return false;
    *app = (AppRange){0};
    const TimerUltrasonicConfig config = {
        .tick_hz = actual_tick_hz,
        .max_ticks = safe_max_ticks,
        .timeout_us = 50000U,
        .speed_mm_s = 340000U,
        .max_distance_mm = 6000U
    };
    if (!timer_ultrasonic_init(&app->sensor, &config)) return false;
    app->timer = timer;
    app->cc = cc;
    return true;
}

/* Call after discarding old events and before issuing the next trigger. */
bool app_range_begin(AppRange *app, uint32_t now_us)
{
    return timer_ultrasonic_arm(&app->sensor, now_us, app->sequence);
}

/* Called only for a verified NEW completed width event of the selected CC.
 * The caller owns event acknowledgement and latches overflow for this pulse.
 * Copy the returned event atomically into an application-owned mailbox.
 */
AppCaptureEvent app_range_snapshot(AppRange *app, uint32_t captured_us,
                                   bool overflow)
{
    AppCaptureEvent event = {
        .sequence = ++app->sequence,
        .captured_us = captured_us,
        .pulse_ticks = DL_TimerG_getCaptureCompareValue(app->timer, app->cc),
        .overflow = overflow
    };
    return event;
}

/* Execute in one task. Atomically drain any current-cycle mailbox event
 * before this call. A NULL event means there is no new hardware event.
 */
bool app_range_service(AppRange *app, const AppCaptureEvent *event, uint32_t now_us)
{
    if (event) {
        (void)timer_ultrasonic_feed(&app->sensor, event->sequence,
            event->pulse_ticks, event->overflow, event->captured_us);
    }
    timer_ultrasonic_step(&app->sensor, now_us);
    TimerUltrasonicResult result;
    if (!timer_ultrasonic_take(&app->sensor, &result)) return false;
    app->latest = result;
    if (result.status == TIMER_ULTRASONIC_OK) {
        /* app->latest.distance_mm is a new valid sample. */
    } else {
        /* Distinguish TIMEOUT, OVERFLOW and RANGE in application logic. */
    }
    return true;
}
```

## 所有权与限制

核心所有函数立即返回，不访问硬件、不等待、不分配堆内存。核心 API 在单一任务/主循环串行调用，不支持 IRQ 与任务或多任务直接并发；ISR 应只快照事件并通过应用自己的同步机制送给该上下文。不同实例独立；同一流的每个事件应有唯一递增序号。

配置按值复制，可在 init 后释放输入配置。实例在使用期间地址固定、有效，输出结果按值复制；feed 不保存任何调用方缓冲指针。无自动覆盖未消费结果，无取消、销毁、触发或硬件停止 API。停止时应用负责停止 PWM/捕获 IRQ、清理邮箱和硬件引用；核心等待态需等 step/feed 完成并 take 后再重用。连续声学串扰、错误声速、线缆电平、timer 精度、延迟 ISR 与未报告的溢出无法由纯数值接口判断。应用负责物理确认与硅勘误处理。

## 构建与权限

将核心编译为 C，MSPM0 glue 按 SDK include/设备定义编译并链接 DriverLib。无硬件验证声明。来源与缺失的许可授权见 [SOURCE.md](SOURCE.md)。
