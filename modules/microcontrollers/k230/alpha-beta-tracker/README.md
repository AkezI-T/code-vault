# alpha-beta-tracker

简体中文 | [English](README.en.md)

通过标量位置测量实现一维 alpha-beta 跟踪、速度估计、丢帧外推和动态关联门限。位置单位由应用选择，例如像素或毫米；全部位置配置必须采用同一单位，速度单位为位置单位/秒。模块不负责检测目标、选择候选或采集时间。

## 环境与接入

- 适用于 K230 CanMV MicroPython，也可在 Python 3 主机环境直接导入。仅使用 Python 基础语法、内置类型和浮点运算，无第三方包或硬件依赖。
- 将 [alpha_beta_tracker.py](src/alpha_beta_tracker.py) 复制到应用导入路径。在本模块目录运行主机示例时，将 `src` 加入路径即可。
- 构造实例后，每个采样帧调用一次 `update` 或 `miss`。需要候选关联时，先在同一时间戳调用 `predicted_x` 和 `dynamic_gate`，再由应用选择测量。
- 每个目标使用独立实例；同一实例访问必须串行化。方法同步执行、不等待、不启动线程、不读取时钟、不占用外设，无关闭或释放接口。浮点、元组及异常可能分配内存，不适合硬中断上下文。

## 最小主机示例

在本模块目录执行 `python`，粘贴以下完整代码。时间戳明确表示采样时刻，单位为毫秒。

```python
import sys
sys.path.insert(0, "src")
from alpha_beta_tracker import AlphaBetaTracker1D

tracker = AlphaBetaTracker1D(lower_bound=0, upper_bound=639)
for now_ms, measurement in ((0, 100), (20, 110), (40, None)):
    if measurement is None:
        position, velocity = tracker.miss(now_ms)
    else:
        position, velocity = tracker.update(measurement, now_ms)
    if position is None:
        print("search")
    else:
        print(round(position, 2), velocity)
print(tracker.predicted_x(60), tracker.dynamic_gate(60))
tracker.reset()
```

前三行输出为 `100.0 0`、`107.0 100`、`109.0 100`。实时主机应用可传入 `time.monotonic() * 1000` 的采样值，也可把调用方计算的非负毫秒增量累加成时间戳。不要把每帧增量直接当作 `now_ms`。

### MicroPython 回绕时钟

```python
import time
from alpha_beta_tracker import AlphaBetaTracker1D

tracker = AlphaBetaTracker1D(0, 639, time_diff_ms=time.ticks_diff)
now_ms = time.ticks_ms()
position, velocity = tracker.update(100, now_ms)
next_ms = time.ticks_add(now_ms, 20)
position, velocity = tracker.miss(next_ms)
```

上例用 `ticks_add` 生成演示的下一采样时刻；实际应用应采集新的 `ticks_ms()`。使用回绕值必须传入配套 `ticks_diff`，默认减法无法处理回绕。任一被比较的时刻（当前时刻与 `state_ms` 或 `last_measurement_ms`）间隔必须小于 tick 周期的一半，遵守运行时 `ticks_diff` 契约。暂停超过该范围时先 `reset()`，不要依靠 `stale_ms` 修复无法判定的回绕。模块不探测时钟类型，也不生成后备时间。

## 构造与配置

```python
AlphaBetaTracker1D(
    lower_bound, upper_bound, alpha=0.70, beta=0.20,
    base_gate=60, max_gate=180, miss_gate_growth=12,
    max_missed_frames=5, stale_ms=250, residual_limit=120,
    max_velocity=5000, velocity_deadband=45,
    stationary_delta=2, stationary_frames_required=3,
    time_diff_ms=None,
)
```

返回未初始化实例。除两个帧数字段及回调外，配置接受有限 `int` 或 `float`，拒绝 `bool`。下表 U 表示位置单位。全部配置在构造时确定，公开同名属性仅供读取；修改配置时创建新实例。

| 字段 | 单位与有效范围 | 默认值 | 作用 |
| --- | --- | --- | --- |
| `lower_bound` | U，有限数，≤ upper_bound | 必填 | 有效位置下界，含端点 |
| `upper_bound` | U，有限数，≥ lower_bound | 必填 | 有效位置上界，允许等于下界 |
| `alpha` | 无量纲，0…1 | 0.70 | 位置残差增益 |
| `beta` | 无量纲，0…1 | 0.20 | 速度残差增益 |
| `base_gate` | U，0…max_gate | 60 | 最小关联距离 |
| `max_gate` | U，≥ base_gate | 180 | 最大关联距离；未初始化时直接返回 |
| `miss_gate_growth` | U/丢帧调用，≥0 | 12 | 每次连续丢帧增加的门限 |
| `max_missed_frames` | `int`，帧数，≥0 | 5 | 允许连续丢帧数；第 N+1 次丢帧失锁 |
| `stale_ms` | ms，≥0 | 250 | 距上次测量超过此值时重新初始化或失锁 |
| `residual_limit` | U，≥0 | 120 | 位置残差绝对值上限 |
| `max_velocity` | U/s，≥0 | 5000 | 内部速度绝对值上限 |
| `velocity_deadband` | U/s，≥0 | 45 | 输出速度绝对值严格低于此值时输出 0 |
| `stationary_delta` | U，≥0 | 2 | 相邻原始测量差不大于此值时累加静止计数 |
| `stationary_frames_required` | `int`，次数，≥1 | 3 | 静止计数达到此值时吸附测量位置并清零速度 |
| `time_diff_ms` | `None` 或可调用对象 | `None` | `f(now_ms, then_ms)` 返回有限、带符号毫秒差；`None` 使用普通减法 |

计数字段同样拒绝 `bool`。零残差上限使常规校正量为零；零最大速度使内部速度为零；零死区仍执行整数截断；零静止距离仅计相等测量。静止计数没有禁用开关，较大的正帧数可推迟吸附。门限仅供调用方关联参考，不自动拒绝 `update` 的测量。

类型不符合要求时抛出 `TypeError`；非有限数、负阈值、逆序边界/门限、增益越界或帧数低于最小值时抛出 `ValueError`。构造器不调用时间回调。

## 公共方法

全部 `now_ms` 参数均为调用方传入的有限 `int/float` 毫秒时间戳，拒绝 `bool`。同一实例必须使用同一时钟域和差值规则，正常使用单调时间。方法不会主动推进时间。

| 精确签名 | 参数与调用前提 | 返回值与状态效果 |
| --- | --- | --- |
| `reset(self)` | 无参数；任意状态可调用 | 返回 `None`；清除当前轨迹，保留 `max_observed_misses` |
| `predicted_x(self, now_ms)` | 当前/待采样时间戳 | 未初始化返回 `None`，否则返回限幅位置；只读，不执行过期判断 |
| `dynamic_gate(self, now_ms)` | 当前/待采样时间戳 | 返回 U 单位的数值门限；未初始化为 `max_gate`；只读，不执行过期判断 |
| `update(self, measured_x, now_ms)` | `measured_x` 为有限 `int/float`（非 `bool`），U 单位；任意状态可调用 | 返回 `(position, velocity)`；位置为数值，速度为 U/s 的 `int`；接收测量并重置连续丢帧计数 |
| `miss(self, now_ms)` | 此采样帧无有效测量时调用；任意状态可调用 | 返回 `(position, velocity)`；失锁/未初始化时为 `(None, 0)`，有效轨迹时外推位置并增加丢帧计数 |
| `output_velocity(self)` | 无参数；任意状态可调用 | 返回 U/s 的 `int`；未初始化或绝对速度严格低于死区时为 0，其他情况向零截断内部速度；只读 |

位置、门限及内部速度的数值类型可以是 `int` 或 `float`，由输入及限幅边界决定；可见输出速度始终为 `int`。以下方法示例可接在主机示例的构造行之后：

```python
position, velocity = tracker.update(100, 0)
prediction = tracker.predicted_x(20)
gate = tracker.dynamic_gate(20)
if prediction is not None and abs(110 - prediction) <= gate:
    position, velocity = tracker.update(110, 20)
else:
    position, velocity = tracker.miss(20)
velocity_only = tracker.output_velocity()
tracker.reset()
assert tracker.predicted_x(40) is None
```

输入类型错误抛出 `TypeError`，NaN/无穷值抛出 `ValueError`；时间差回调返回值也执行同样检查。回调自身异常直接传出，由应用处理。回调应为无副作用、相同输入结果相同的函数，一次方法调用可能多次计算差值。回调在 `miss` 的状态推进后也会执行，异常不保证回滚。数值及计算中间结果须处于运行时浮点表示范围；过大的值可能导致 `OverflowError` 或失效的数值状态，不提供数值饱和兜底。

## 算法与时序规则

1. 首次测量或距离上次测量严格超过 `stale_ms` 时，设 `x = clamp(measured_x, lower_bound, upper_bound)`、`v = 0`，清零当前丢帧和静止计数。
2. 常规更新取自上次状态推进的秒数 `dt`；非正差值使用 `0.001 s`。计算 `predicted = x + v * dt`（此中间预测不预先限幅），将残差 `measured_x - predicted` 限制到 `±residual_limit`；然后 `x = clamp(predicted + alpha * residual)`、`v = clamp(v + beta * residual / dt)`。位置和速度分别使用配置边界。
3. 相邻原始测量差 `<= stationary_delta` 时计数加一，否则归零。计数达到阈值后吸附限幅后的测量位置并清零速度。默认需要首次测量之后再有三次满足条件的更新。`miss` 不清零静止计数，也不修改上次原始测量；静止条件可跨短暂丢帧累积。
4. `miss` 使用 `predicted_x` 更新位置及状态时间、增加计数并记录历史峰值，然后检查 `missed_frames > max_missed_frames` 或严格过期；任一满足即重置。有效丢帧期间内部速度不衰减，触及位置边界也不清零速度。
5. 只读预测取 `max(差值, 0) / 1000`，输出限幅位置；门限为 `clamp(base_gate + abs(v) * elapsed_since_measurement, base_gate, max_gate)`，其中限幅前还加上 `missed_frames * miss_gate_growth`。负时间差按零处理，过期比较仍使用带符号差值。重复/倒序时间戳按这些规则计算，不会被拒绝，但不能代表正常采样顺序。

位置边界同时用于初始化、校正、静止吸附和预测，扩展了仅对校正/预测限幅的行为。原始测量保持原值参与残差及静止比较，应用仍负责测量有效性。查询过期轨迹仍返回预测/门限，只有 `update` 或 `miss` 执行过期处理。

## 可观察状态

以下属性用于只读诊断，直接写入不受支持。构造后及 `reset` 后均为表中初值，丢帧历史峰值例外。未初始化时的 `x = 0.0` 是空状态占位，可能位于配置边界外。

| 属性 | 类型、单位、初值 | 含义 |
| --- | --- | --- |
| `initialized` | `bool`，`False` | 是否持有有效轨迹 |
| `x` | 数值，U，0.0 | 最近一次更新/外推的位置 |
| `v` | 数值，U/s，0.0 | 内部速度，尚未执行输出死区/整数截断 |
| `state_ms` | 时间戳数值或 `None` | 最近一次 `update/miss` 推进时刻 |
| `last_measurement_ms` | 时间戳数值或 `None` | 最近一次接受测量的时刻 |
| `last_raw_x` | `float`，U，或 `None` | 最近一次原始测量，用于静止比较 |
| `missed_frames` | `int`，帧数，0 | 当前连续丢帧调用数 |
| `stationary_frames` | `int`，次数，0 | 连续满足原始测量差条件的次数，超过阈值后可继续增长 |
| `max_observed_misses` | `int`，帧数，构造时为 0 | 实例生命周期内的连续丢帧峰值，包含触发失锁的那一次；重置保留，新建实例清除 |

## 限制与许可

这是常速度模型，不估计加速度或置信度，不自动关联多目标。默认参数是像素场景的数值，改用其他单位或采样频率时需调整阈值和增益。固定 1 ms 仅用于非正更新间隔的除法规则，不代表真实时钟或新的采样时间。有限但过大的输入、长时间不推进状态或错误的时钟差值均可能产生不合适的估计。K230 实际时钟周期、浮点精度和采样调度由固件及调用方决定。

无需构建步骤。在本模块目录运行 `python -B -c "import sys; sys.path.insert(0, 'src'); from alpha_beta_tracker import AlphaBetaTracker1D; print(AlphaBetaTracker1D(0, 639).update(100, 0))"` 可完成主机导入与单次调用。

采用 [MIT 许可](LICENSE)。
