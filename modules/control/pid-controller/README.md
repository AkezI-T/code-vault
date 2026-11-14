# pid-controller

简体中文 | [English](README.en.md)

通用定点 PID 控制器：一个可移植的 C99 头文件，配一份逐位等价的 Python 参考实现。输入设定值与测量值，输出一路控制量，全程整数运算——热路径没有浮点，也没有除法。

控制律由比例、积分、测量值微分、死区、输出限幅、一阶低通和方向符号组成，所有增益与限制都是写进配置结构体的定点整数。适用于任何「每一拍或每一帧算一个输出」的闭环：像素偏差到云台转速、角度到电机速度、温度到加热功率等。

## 环境要求

- 芯片：任意 32 位 MCU。C 实现只依赖 `stdint.h` 与 `stddef.h`，不含平台头文件、操作系统接口或动态内存。
- 框架与工具链：C99 编译器（已在主机 gcc 16.1.0 与 arm-none-eabi-gcc 16.1.0 `-mcpu=cortex-m3 -mthumb` 上以 `-std=c99 -Wall -Wextra -pedantic -Werror` 零警告编译）；Python 3.7 及以上（只用标准库）。
- 依赖：无。不需要同仓库其他模块。

## 接线与配置

这是纯算法模块，没有引脚、总线或中断归属。每个控制轴需要一个自己的 `pid_controller_t`，由调用方按固定周期喂入设定值、测量值和周期长度。

配置项就是 `pid_config_t` 的十个字段：

| 配置项 | 类型 | 单位与有效范围 | 默认值 | 是否必需 | 说明 |
| --- | --- | --- | --- | --- | --- |
| `kp` | `int32_t` | Q`scale_bits` | 无 | 是 | 比例增益 |
| `ki` | `int32_t` | Q`scale_bits`，每秒 | 0 | 是 | 积分增益；0 表示不用积分 |
| `kd` | `int32_t` | Q`scale_bits`，每秒 | 0 | 是 | 微分增益，作用在测量值上 |
| `deadzone` | `int32_t` | Q`scale_bits`，≥ 0，与误差同单位 | 0 | 是 | 误差绝对值不超过它就输出 0；0 表示不要死区（仅误差恰为 0 时归零） |
| `max_output` | `int32_t` | Q`scale_bits`，≥ 0 | 无 | 是 | 输出对称限幅 |
| `alpha` | `int32_t` | Q`scale_bits`，0 ~ 1.0 | 1.0 | 是 | 一阶低通权重，满量程即关闭滤波 |
| `direction` | `int32_t` | +1 或 −1 | +1 | 是 | 输出方向；负值按 −1 处理 |
| `integral_min` | `int32_t` | Q`scale_bits`，≤ `integral_max` | 无 | 是 | 积分下限，防积分饱和 |
| `integral_max` | `int32_t` | Q`scale_bits`，≥ `integral_min` | 无 | 是 | 积分上限 |
| `scale_bits` | `int32_t` | 0 ~ 16 | 16 | 是 | 小数位数，决定 Q 格式 |

表中「默认值」一列给的是推荐初值，C 结构体本身没有内置默认值，必须逐字段填写。

`pid_init()` 会把越界的配置夹回有效范围：`scale_bits` 夹到 0~16，`direction` 归一为 ±1，`alpha` 夹到 0~1.0，`deadzone` 与 `max_output` 取绝对值，`integral_min`/`integral_max` 颠倒时自动交换。

## 定点约定

| 项目 | 约定 |
| --- | --- |
| 格式 | Q`scale_bits`：1.0 = `1 << scale_bits`。默认 Q16，即 1.0 = 65536，分辨率 1/65536 |
| 数据宽度 | 全部字段、入参、返回值是 `int32_t`；中间乘积与积分累加是 `int64_t` |
| 可表示范围 | Q16 下约为 ±32768.0（int32 上限 32767.99998）。物理单位要选到「满量程 × 65536 不溢出」 |
| 乘法后移位 | 两个 Q`scale_bits` 相乘得到 Q`2*scale_bits`，先算成 `int64_t`，再右移 `scale_bits` 位回到 Q`scale_bits` |
| 移位语义 | 算术右移，等于 floor(x / 2^scale_bits)，负数同样向下取整（`-1 >> 16 == -1`）。实现用无符号移位完成，既不依赖「负数右移」的实现定义行为，也不产生除法指令 |
| 逐级饱和 | 每一级移位结果立刻夹到 `int32_t`。于是后续任何 `int64_t` 乘积最大 2^62，不会溢出 |
| dt | Q`scale_bits` 秒，必须大于 0；小于等于 0 时本次调用返回 0 且不改状态 |
| inv_dt | `(1 << 2*scale_bits) / dt`，即 dt 的定点倒数。只在 dt 与上一次不同时算一次并缓存在控制器里，之后只用乘法 |
| inv_dt 上限 | 饱和到 `INT32_MAX`，等价于 Q16 下 dt 下限约 1/32768 s |
| alpha | Q`scale_bits`，0 ~ 1.0；等于满量程时低通关闭，输出直接等于限幅后的 raw |
| 舍入 | 一律向下取整，不做四舍五入。与浮点版实现相比会有 ±1 LSB 级别的差异，这是定点的预期行为 |
| 缩放限制 | 只支持 2 的幂缩放，没有非 2 的幂定标换算 |

`inv_dt` 之所以要缓存：`dt` 通常由固定的控制周期决定，整个运行期只变一次或几次；把它变成乘数后，每次 `pid_update()` 只剩乘法和移位。

## 控制律

一次 `pid_update(pid, setpoint, measurement, dt)` 依次计算：

```
error  = setpoint - measurement
P      = kp * error
I     += ki * error * dt                     然后夹到 [integral_min, integral_max]
D      = kd * (prev_measurement - measurement) * inv_dt
raw    = P + I + D
若 |error| <= deadzone   → raw = 0
raw    = clamp(raw, -max_output, +max_output)
out    = out_prev * (1 - alpha) + raw * alpha
out    = out * direction
```

每一步的含义：

- **P**：误差本身。比例增益越大响应越快，越容易振荡。
- **I**：误差随时间的累积，用来消掉稳态误差。累加之后立刻按 `[integral_min, integral_max]` 钳位，防止积分饱和（windup）。死区内也会继续累加——死区只把输出清零，不动积分器。
- **D**：对**测量值**求差分再乘 `kd`，不是对误差求差分。设定值阶跃时测量值不会突变，因此不会出现微分冲击；测量值突变时照常产生阻尼。恒定设定值下 `error - prev_error = -(measurement - prev_measurement)`，与原实现对误差逐帧差分的符号一致。
- **死区**：`|error| <= deadzone` 时输出直接清零，抑制目标在中心附近抖动。判据用的是误差，不是输出。
- **限幅**：`±max_output` 对称钳位，适配执行器能力。
- **低通**：一阶平滑，`alpha` 越小越平滑、相位滞后越大；`alpha` 等于满量程即完全不滤波。
- **方向**：机构正方向相反的轴用 −1 翻转，避免在调用处手写取负。`out_prev` 保存的是翻转**之后**的输出，所以低通与方向可以任意顺序组合而状态始终是实际下发值。

状态更新：`prev_measurement = measurement`、`prev_error = error`、`out_prev = out`、`integral` 为钳位后的值。

## 接入步骤

1. 把 [`include/pid_controller.h`](include/pid_controller.h) 拷进工程的公共头文件目录。它是 header-only 的，函数都是 `static inline`，不需要额外的 `.c` 文件和链接步骤。
2. 每个控制轴声明一个 `pid_controller_t`，用 `pid_init()` 传入该轴的 `pid_config_t`。配置可以是 `const` 全局量，初始化时按值拷进来。
3. 在固定周期的控制中断或任务里调用 `pid_update()`，把返回值下发到执行器。`dt` 用周期长度（Q`scale_bits` 秒），同一周期内不要改变它的值。

命令均在本模块目录下执行，示例里的数值是占位值，需要按机构重新标定。

## 最小使用示例

C：

```c
#include "pid_controller.h"

static pid_controller_t g_yaw;

void gimbal_control_init(void)
{
    pid_config_t cfg;
    cfg.kp = 16384;              /* 0.25 */
    cfg.ki = 0;                  /* 不用积分 */
    cfg.kd = 13107;              /* 0.20 */
    cfg.deadzone = 983040;       /* 15.0 像素 */
    cfg.max_output = 1966080;    /* 30.0 rpm */
    cfg.alpha = 45875;           /* 0.7，开低通 */
    cfg.direction = -1;          /* 机构反装 */
    cfg.integral_min = -1966080;
    cfg.integral_max = 1966080;
    cfg.scale_bits = 16;
    pid_init(&g_yaw, &cfg);
}

/* 每 5 ms 或在每个视觉帧调用一次 */
int32_t gimbal_update(int32_t setpoint_px_q16, int32_t measurement_px_q16)
{
    int32_t dt = 327;            /* 5 ms = 0.005 * 65536 ≈ 327 */
    int32_t rpm_q16 = pid_update(&g_yaw, setpoint_px_q16, measurement_px_q16, dt);

    /* rpm_q16 是 Q16 的转速，右移 16 位后交给驱动器 */
    return rpm_q16;
}
```

Python（与 C 逐位等价，用来在主机上复算同一组数）：

```python
import sys

sys.path.insert(0, "src")
import pid_controller as pid

cfg = pid.PidConfig(
    kp=16384,            # 0.25
    ki=0,
    kd=13107,            # 0.20
    deadzone=983040,     # 15.0 px
    max_output=1966080,  # 30.0 rpm
    alpha=45875,         # 0.7
    direction=-1,
    integral_min=-1966080,
    integral_max=1966080,
    scale_bits=16,
)
ctrl = pid.pid_init(cfg)

out = pid.pid_update(ctrl, 100 << 16, 0, 65536)  # 100 px 误差，dt = 1 s
print(out)  # -1146875，即 -17.5 rpm
```

## API 使用说明

### 常量

| 常量 | 值 | 说明 |
| --- | --- | --- |
| `PID_SCALE_BITS_MAX` | 16 | 支持的最大小数位数；越界配置会被夹回此值 |
| `PID_DEFAULT_SCALE_BITS` | 16 | 文档与示例使用的默认小数位数 |

头文件里另有 `pid_sat_i32()`、`pid_shr_floor()`、`pid_abs_i32_sat()` 三个 `static inline` 辅助函数，负责逐级饱和、向下取整移位与取绝对值。它们属于实现细节，不构成对外接口。

### pid_config_t

**签名**

```c
typedef struct {
    int32_t kp;
    int32_t ki;
    int32_t kd;
    int32_t deadzone;
    int32_t max_output;
    int32_t alpha;
    int32_t direction;
    int32_t integral_min;
    int32_t integral_max;
    int32_t scale_bits;
} pid_config_t;
```

**用途与调用前提**

一组增益与限制，全部是 Q`scale_bits` 定点值（`scale_bits` 自身除外）。`pid_init()` 会按值复制并归一化，调用之后原结构体可以丢弃或复用。字段含义、单位与范围见上面的[配置项](#接线与配置)表。

**字段**

| 字段 | 类型 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| `kp` | `int32_t` | Q，无默认 | 比例增益 |
| `ki` | `int32_t` | Q/s，无默认 | 积分增益 |
| `kd` | `int32_t` | Q/s，无默认 | 微分增益 |
| `deadzone` | `int32_t` | Q，≥ 0 | 死区，与误差同单位 |
| `max_output` | `int32_t` | Q，≥ 0 | 输出限幅 |
| `alpha` | `int32_t` | Q，0 ~ 1.0 | 低通权重，1.0 关闭 |
| `direction` | `int32_t` | +1 / −1 | 输出方向 |
| `integral_min` | `int32_t` | Q | 积分下限 |
| `integral_max` | `int32_t` | Q | 积分上限 |
| `scale_bits` | `int32_t` | 0 ~ 16，默认 16 | 小数位数 |

### pid_init

**签名**

```c
void pid_init(pid_controller_t *pid, const pid_config_t *cfg);
```

**用途与调用前提**

初始化控制器：复制并归一化 `cfg`，然后清零全部状态（`integral`、`prev_measurement`、`prev_error`、`out_prev`、`dt`、`inv_dt`）。必须在第一次 `pid_update()` 之前调用；之后重新调用等于重新上电复位。

**参数**

| 参数 | 类型与方向 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| `pid` | `pid_controller_t *`；输出 | 不可为空 | 待初始化的控制器，生命周期由调用方保证 |
| `cfg` | `const pid_config_t *`；输入 | 不可为空 | 配置来源；越界值会被归一化 |

**返回值与错误处理**

无返回值。任一指针为空时直接返回、不做任何修改。配置越界不会报错，只按[定点约定](#定点约定)中的规则夹回有效范围。

**调用约束**

非线程安全：一个控制器只应被一个上下文（中断或任务）驱动。可在中断中调用，不做阻塞、不带锁、不分配内存。

**调用示例**

```c
pid_config_t cfg = {16384, 0, 13107, 983040, 1966080, 45875, -1,
                    -1966080, 1966080, 16};
pid_controller_t yaw;
pid_init(&yaw, &cfg);
```

### pid_update

**签名**

```c
int32_t pid_update(pid_controller_t *pid, int32_t setpoint,
                   int32_t measurement, int32_t dt);
```

**用途与调用前提**

执行一拍控制：按[控制律](#控制律)算出输出，更新控制器状态并返回。必须先经 `pid_init()` 初始化。同一个控制器连续调用即可，两次调用之间的实际间隔必须等于 `dt`。

**参数**

| 参数 | 类型与方向 | 单位、范围与默认值 | 说明 |
| --- | --- | --- | --- |
| `pid` | `pid_controller_t *`；输入输出 | 不可为空 | 控制器，状态在本次调用中更新 |
| `setpoint` | `int32_t`；输入 | Q`scale_bits`，与测量值同单位 | 目标值 |
| `measurement` | `int32_t`；输入 | Q`scale_bits` | 本次采样到的实际值 |
| `dt` | `int32_t`；输入 | Q`scale_bits` 秒，> 0 | 距上一次调用的时间间隔；变化时重算并缓存 `inv_dt` |

**返回值与错误处理**

返回本拍输出，Q`scale_bits`，范围 `[-max_output, +max_output]`，且已乘上 `direction`。`pid` 为空或 `dt <= 0` 时返回 0 且不改动任何状态——这是唯一的错误出口，没有错误码，调用方需要自己保证参数有效。

**调用约束**

非线程安全、非可重入。执行时间与数值无关（无循环、无按数据分支的除法），适合放在定时中断里。不阻塞、不等待、不访问硬件。

**调用示例**

```c
/* 5 ms 周期中断 */
void timer_5ms_isr(void)
{
    int32_t measurement = read_vision_y_q16();
    int32_t out = pid_update(&g_yaw, 0, measurement, 327);
    motor_set_speed_q16(out);
}
```

### pid_controller_t

**签名**

```c
typedef struct {
    pid_config_t cfg;
    int64_t integral;
    int32_t prev_measurement;
    int32_t prev_error;
    int32_t out_prev;
    int32_t dt;
    int64_t inv_dt;
} pid_controller_t;
```

**用途与调用前提**

配置与状态的载体，由调用方按轴各分配一个（静态或栈上皆可）。字段由 `pid_init()` 与 `pid_update()` 维护，外部只读：`prev_error` 便于观测，`integral` 便于看积分是否撞上限，其余字段是内部状态。

**字段**

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `cfg` | `pid_config_t` | `pid_init()` 归一化后的配置副本 |
| `integral` | `int64_t` | 积分累加器，Q`scale_bits`，已钳位 |
| `prev_measurement` | `int32_t` | 上一次的测量值，供微分使用 |
| `prev_error` | `int32_t` | 上一次的误差，只读观测用 |
| `out_prev` | `int32_t` | 上一次的输出（已乘 `direction`），供低通使用 |
| `dt` | `int32_t` | 当前缓存的 `dt` |
| `inv_dt` | `int64_t` | 与 `dt` 配套的倒数，`dt == 0` 时无效 |

### Python 参考实现

**导入方式**

```python
import pid_controller as pid

cfg = pid.PidConfig(kp=16384, ki=0, kd=13107, deadzone=983040,
                    max_output=1966080, alpha=45875, direction=-1,
                    integral_min=-1966080, integral_max=1966080, scale_bits=16)
ctrl = pid.pid_init(cfg)
out = pid.pid_update(ctrl, 100 << 16, 0, 65536)
```

**用途与调用前提**

与 C 头文件逐位等价的参考实现，用于在主机上复算控制律、做参数预演和交叉验证。语义相同：同样的归一化、同样的逐级饱和、同样的向下取整移位、同样的 `inv_dt` 缓存。不用于量产固件。

**参数**

| 参数 | 类型 | 说明 |
| --- | --- | --- |
| `cfg` | `PidConfig` | 与 `pid_config_t` 一一对应 |
| `pid` | `PidController` | `pid_init()` 的返回值，状态在调用中更新 |
| `setpoint` / `measurement` | `int` | Q`scale_bits` |
| `dt` | `int` | Q`scale_bits` 秒，> 0 |

**返回值与错误处理**

`pid_update()` 返回本拍输出的 `int`。`dt <= 0` 时返回 0 且状态不变。Python 侧没有空指针情形。

**调用约束**

纯计算，无 I/O、无全局状态、可重入（每个 `PidController` 各自持有状态）。

## 配置示例表

下面把四份现成的视觉伺服实现映射成本模块的配置，用来证明这套参数足够表达它们。全部 `scale_bits = 16`，`ki = 0`（原实现都没有积分项），`integral_min`/`integral_max` 取 `∓max_output`（`ki = 0` 时积分器不会动，这里只是给出安全边界）。定点换算规则：`定点值 = round(物理值 × 65536)`。

| 来源 | 轴 | `kp` | `ki` | `kd` | `deadzone` | `max_output` | `alpha` | `direction` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| follow 基准 | X | 16384 (0.25) | 0 | 13107 (0.20) | 983040 (15.0) | 1966080 (30.0) | 45875 (0.7) | −1 |
| follow 基准 | Y | 26214 (0.4) | 0 | 16384 (0.25) | 983040 (15.0) | 1966080 (30.0) | 45875 (0.7) | +1 |
| bl32 硬化 | X | 45875 (0.7) | 0 | 1311 (0.02) | 196608 (3.0) | 1966080 (30.0) | 65536 (1.0，关) | −1 |
| bl32 硬化 | Y | 16384 (0.25) | 0 | 1311 (0.02) | 196608 (3.0) | 655360 (10.0) | 65536 (1.0，关) | −1 |
| st32 | X | 45875 (0.7) | 0 | 1311 (0.02) | 131072 (2.0) | 1966080 (30.0) | 65536 (1.0，关) | −1 |
| st32 | Y | 26214 (0.4) | 0 | 1311 (0.02) | 131072 (2.0) | 655360 (10.0) | 65536 (1.0，关) | +1 |
| MSPM0 步进 | X | 52429 (0.80) | 0 | 1704 (0.026) | 65536 (1.0) | 1310720 (20.0) | 65536 (1.0，关) | −1 |
| MSPM0 步进 | Y | 24576 (0.375) | 0 | 983 (0.015) | 65536 (1.0) | 655360 (10.0) | 65536 (1.0，关) | +1 |

括号外是写进 `pid_config_t` 的定点整数，括号内是原始物理值。三份 `alpha = 1.0` 的原实现没有输出滤波，所以低通关闭；`follow` 的 `FILTER_ALPHA = 0.7` 直接对应 `alpha = 45875`。

映射时有三件事留在模块外，由调用方承担：

- **触发周期**：`follow` 按视觉帧触发，另外三份按 5 ms 定时器。`dt` 填实际周期（5 ms 对应 327）。若要完全复现「逐帧差分」的原始语义，把 `dt` 填 1.0（65536），此时 `inv_dt = 1.0`，微分项等于逐帧差值。
- **分段增益与前馈**：`bl32`/`st32` 的路段增益（0.4 ~ 1.1）、编码器前馈、IMU 偏航补偿都属于上层，模块只负责增益为 1 的那一层控制律。
- **丢帧保持**：原实现「丢帧 200 ms 后才清状态」的逻辑在调用方实现；本模块不感知目标是否丢失。

## 构建与检查

C 头文件没有独立的编译单元，单独做语法检查：

```bash
gcc -std=c99 -Wall -Wextra -pedantic -Werror -fsyntax-only \
    -x c include/pid_controller.h
```

交叉编译到 Cortex-M3：

```bash
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c99 -Wall -Wextra -pedantic -Werror \
    -c -x c include/pid_controller.h -o /tmp/pid_controller.o
```

Python 单元测试（在本模块目录执行）：

```bash
python -m unittest discover -s tests -v
```

测试覆盖比例项、测量值微分、积分累加与钳位、死区、限幅、方向、低通开关、`dt` 变化后的重算，以及与 C 头文件共用的那组黄金数值。

## 限制与资源

- **没有浮点**：头文件里没有 `float`/`double`，也没有 `math.h`；编译出的目标文件里不存在浮点指令或浮点辅助调用。
- **热路径没有除法**：`pid_update()` 全程只有乘法与移位。唯一的除法是 `inv_dt` 的倒数计算，只在 `dt` 变化时执行一次。
- **只支持 2 的幂缩放**：`scale_bits` 是移位量，不支持任意定标系数或物理单位换算（毫弧度、0.1° 这类非 2 的幂单位需要调用方自己折算）。
- **数值范围**：Q16 下所有量必须落在 ±32768 之内。误差、测量值或输出超出这个范围的场合，要么降低 `scale_bits`，要么换物理单位。
- **抗饱和只有钳位**：没有条件积分、反算或微分先行等其他抗饱和手段；需要更复杂的策略时，在调用方改写 `integral` 字段。
- **死区内的积分仍累加**：死区只把输出清零，积分器照常工作。若希望死区内冻结积分，由调用方按需清零 `integral`。
- **无目标丢失处理**：控制器不判断测量值是否过期。丢帧时保持最后一次输出、超时后清零，属于调用方的策略。
- **单轴单实例**：每个控制轴一个 `pid_controller_t`，模块不提供多轴协调、逆解或前馈合成。
- **不保证与浮点实现逐位一致**：定点舍入向下取整、增益量化到 1/65536，与浮点参考实现会有 ±1 LSB 级差异。
