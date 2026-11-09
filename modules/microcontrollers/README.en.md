# Microcontroller modules

[简体中文](README.md) | English

Store reusable microcontroller firmware modules, including peripheral drivers, communication protocols, and control algorithms. Group them by chip at `<chip>/<module-name>/`, such as `esp32-s3`, `mspm0g3507`, and `k230`. Each module owns its source and dependencies.

Use English names for files and directories. Module names describe capabilities without brand names and include the applicable communication method, for example `i2c-line-sensor`. The named communication method must match the implementation.

Every `<module-name>/` root must contain a default Simplified Chinese `README.md` and an English `README.en.md`. Keep both versions equivalent and provide reciprocal language links at the top. Follow [docs/architecture.md](../../docs/architecture.md) and [docs/module-template.md](../../docs/module-template.md) to document supported chips and frameworks, wiring and configuration, a complete usage example, and every public API's signature, parameters, return values, errors, and calling constraints.

## Modules by chip

| Chip | Module | Language | Responsibility |
| --- | --- | --- | --- |
| esp32-s3 | [uart](esp32-s3/uart/README.en.md) | C | Configurable UART I/O and lifecycle |
| esp32-s3 | [ws2812](esp32-s3/ws2812/README.en.md) | C | Uniform strip color over RMT |
| mspm0g3507 | [async-i2c](mspm0g3507/async-i2c/README.en.md) | C | Per-bus nonblocking I2C transaction queue |
| mspm0g3507 | [button](mspm0g3507/button/README.en.md) | C | Debounce, short, double and long press events |
| mspm0g3507 | [i2c-line-sensor](mspm0g3507/i2c-line-sensor/README.en.md) | C | Eight-channel I2C line-sensor data |
| mspm0g3507 | [i2c-jy901s](mspm0g3507/i2c-jy901s/README.en.md) | C | JY901S attitude angles over I2C |
| k230 | [alpha-beta-tracker](k230/alpha-beta-tracker/README.en.md) | Python | Configurable one-dimensional position and velocity estimation |

Host-side development tools live in [tools/microcontrollers/](../../tools/microcontrollers/README.en.md).
