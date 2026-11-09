# button

简体中文 | [English](README.en.md)

单按钮非阻塞状态机，输出延迟短按、双击和单次长按事件。状态机接收电平与时间，可在 MSPM0G3507 下由应用 GPIO 采样，也可由其他 C 程序提供输入。无 DriverLib 依赖、全局状态或堆分配。

## 环境与配置

C99 的 C API；C++ 调用方需在 extern "C" 中包含头文件。将 [源文件](src/msp_button.c) 加入应用，添加 [头文件](include/msp_button.h) 所在 include 目录。GPIO 示例使用 MSPM0 SDK 2.09.00.01 DriverLib、SysConfig 和 TI Arm Clang 4.0.4.LTS；状态机本身只依赖 stdbool.h/stdint.h。

在 SysConfig 创建 GPIO 组 BUTTON、输入 INPUT，对应 BUTTON_PORT/BUTTON_INPUT_PIN。选择实际引脚，低有效示例用上拉和按下接地的开关，高有效则匹配下拉和逻辑电平。保持电平在 MCU 允许范围。调用 SYSCFG_DL_init 后初始化状态机，每约 1 ms 采样一次，将物理高电平作为 true 传入 scan。示例独占 SysTick 作为 1 ms 时钟；已有 RTOS/时基时使用现有连续毫秒计数。

| 类型/字段 | 单位、范围、默认和含义 |
| --- | --- |
| MSP_BUTTON_CONFIG_DEFAULT | MspButtonConfig 初始化器：低有效、20 ms 消抖、700 ms 长按、500 ms 双击间隔。 |
| MspButtonConfig.active_high | bool；true 高有效，false 低有效；默认 false。 |
| MspButtonConfig.debounce_ms | uint16_t，1..65535 ms；首次按压确认和第二次按压检查延迟，默认 20。 |
| MspButtonConfig.long_press_ms | uint16_t，debounce_ms..65535 ms；长按阈值，默认 700。 |
| MspButtonConfig.double_gap_ms | uint16_t，1..65535 ms；首次释放到等待短按到期的间隔，默认 500。 |
| MspButton.config | 配置内部副本；调用 init 后不直接修改。 |
| MspButton.state | uint8_t 内部阶段，不是公开枚举；由 init/scan 管理。 |
| MspButton.press_ms | uint32_t 内部长按起点，毫秒。 |
| MspButton.release_ms | uint32_t 内部首次释放起点，毫秒。 |
| MspButton.debounce_ms | uint32_t 内部消抖起点，毫秒；与同名配置时长不同。 |
| MspButtonEvent | 枚举：MSP_BUTTON_EVENT_NONE 无事件；MSP_BUTTON_EVENT_SHORT 短按；MSP_BUTTON_EVENT_DOUBLE 双击；MSP_BUTTON_EVENT_LONG 长按。 |

## 完整最小示例

GPIO 名称、CPUCLK_FREQ 均由应用 SysConfig 生成，按实际命名和时钟调整。示例以 last_event 表示交给应用的最近事件。


```c
#include "ti_msp_dl_config.h"
#include "msp_button.h"

static MspButton button;
static volatile uint32_t ticks_ms;
static volatile MspButtonEvent last_event;

void SysTick_Handler(void) { ++ticks_ms; }

int main(void)
{
    SYSCFG_DL_init();
    const MspButtonConfig config = MSP_BUTTON_CONFIG_DEFAULT;
    if (!msp_button_init(&button, &config, 0U) ||
        SysTick_Config(CPUCLK_FREQ / 1000U) != 0U) {
        for (;;) {}
    }
    uint32_t previous = ticks_ms;
    for (;;) {
        uint32_t now = ticks_ms;
        if (now != previous) {
            previous = now;
            bool high = DL_GPIO_readPins(BUTTON_PORT, BUTTON_INPUT_PIN) != 0U;
            MspButtonEvent event = msp_button_scan(&button, high, now);
            if (event != MSP_BUTTON_EVENT_NONE) last_event = event;
        }
    }
}
```

## API


```c
bool msp_button_init(MspButton *button, const MspButtonConfig *config, uint32_t now_ms);
MspButtonEvent msp_button_scan(MspButton *button, bool level_high, uint32_t now_ms);
```

msp_button_init 的 button 是非 NULL 可写实例，config 是非 NULL 输入，now_ms 是初始化时的 uint32_t 毫秒时间。配置按值复制，三个时间戳设为 now_ms，阶段置释放；成功 true。空指针、debounce_ms=0、long_press_ms<debounce_ms、double_gap_ms=0 返回 false 且不修改对象。初始化无需先清零；再次 init 会丢弃当前手势。示例中演示默认初始化。

msp_button_scan 的 button 必须已成功初始化且非 NULL；level_high 是采样的物理高电平（true=高，false=低），不预先按极性反转。now_ms 为 uint32_t 单调毫秒时间，允许模 2^32 回绕，调用间隔小于 2^31 ms。返回 MspButtonEvent，每次调用最多一个事件；无事件返回 NONE。函数不提供错误状态码或 NULL 检查，应用应满足前提。示例中的采样与事件判断可直接用于任务循环。

## 精确手势语义与限制

首次检测按下时开始 debounce，达到阈值时仅检查该次采样仍为按下；不要求整个区间每次都稳定。首次长按从消抖确认时计时。首次释放立即开始 double_gap，不单独消抖释放；只有保持释放且 gap 到期才发 SHORT。等待期间先判断按下，再判断 gap 到期，所以稀疏采样时在到期调用观察到按下会进入第二次按压。

第二次按压从首次观察到按下时同时计 debounce 和长按；达到 debounce 后若观察到释放立即发 DOUBLE，即使中途提前释放也不会重置这个延迟。第二次持续按住达到 long_press 时发 LONG 并吞掉首次短按。首次长按与第二次长按都只发一次，释放后重新就绪；释放判断优先于长按阈值。不存在释放消抖或连续重复长按事件。

所有调用都立即返回，不休眠、不访问硬件。不限制不同实例数量，同一实例必须在单个主循环/任务串行使用，不支持并发任务或 ISR 同时访问；不需要也不提供内部关中断。应用提供稳定时间基准和足够采样频率，事件精度受采样周期限制，不会补发漏过的边沿。实例在扫描期间保持有效；无异步缓冲区、无分配、无清理 API，停止调用后即可释放。

## 构建与许可

在应用中编译 [源文件](src/msp_button.c) 并包含 [头文件](include/msp_button.h)；GPIO 适配放在应用。时钟、电平、按键电气特性和实际抖动由应用确认。MIT 许可见 [LICENSE](LICENSE)。
