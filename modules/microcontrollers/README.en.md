# Microcontroller modules

[简体中文](README.md) | English

Store reusable microcontroller firmware modules, including peripheral drivers, communication protocols, and control algorithms. Group them by chip at `<chip>/<module-name>/`, such as `esp32-s3`, `mspm0g3507`, and `k230`. Each module owns its source and dependencies.

Use English names for files and directories. Module names describe capabilities without brand names and include the applicable communication method, for example `i2c-line-sensor`. The named communication method must match the implementation.

Every `<module-name>/` root must contain a default Simplified Chinese `README.md` and an English `README.en.md`. Keep both versions equivalent and provide reciprocal language links at the top. Follow [docs/architecture.md](../../docs/architecture.md) and [docs/module-template.md](../../docs/module-template.md) to document supported chips and frameworks, wiring and configuration, a complete usage example, and every public API's signature, parameters, return values, errors, and calling constraints.

Host-side development tools live in [tools/microcontrollers/](../../tools/microcontrollers/README.en.md).
